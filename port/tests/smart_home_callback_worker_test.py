"""Host regression: blocking cloud callbacks must not own the MQTT poll stack."""

import os
import runpy
import threading
import time

fixtures = runpy.run_path(os.path.join(os.path.dirname(__file__), 'smart_home_mqtt_test.py'))
module = fixtures['module']


def test_slow_callback_does_not_block_mqtt_poll():
    fixtures['reset_fake_clients']()
    home = module.SmartHomeMQTT('localhost', 'test', 'user', 'password', 'topic')
    home.connect()
    home.subscribe_commands()
    entered = threading.Event()
    release = threading.Event()
    returned = threading.Event()
    finished = threading.Event()

    def slow_callback():
        entered.set()
        release.wait(2)
        finished.set()

    home.add_event('slow', lambda: True, '==', True, slow_callback, 0, 0)

    def poll():
        home._scheduled_command_poll(home.command_timer_generation)
        returned.set()

    caller = threading.Thread(target=poll)
    caller.start()
    try:
        assert entered.wait(1), 'callback never started'
        assert returned.wait(0.2), 'slow callback held up the MQTT scheduled poll'
        assert not finished.is_set()
        home._scheduled_command_poll(home.command_timer_generation)
        assert home.client.check_calls == 2
    finally:
        release.set()
        caller.join(3)
        assert finished.wait(1)
        home.disconnect()


test_slow_callback_does_not_block_mqtt_poll()


def test_callback_queue_is_bounded_fifo_and_reports_failures():
    fixtures['reset_fake_clients']()
    home = module.SmartHomeMQTT('localhost', 'test', 'user', 'password', 'topic')
    rules = home.rule_engine
    entered, release, finished = threading.Event(), threading.Event(), threading.Event()
    calls = []

    def blocker():
        entered.set()
        release.wait(3)

    def fail():
        raise ValueError('cloud_timeout')

    rules.add_event('blocker', lambda: True, '==', True, blocker, 0, 0)
    rules.tick()
    assert entered.wait(1)
    for number in range(10):
        rules.add_event('event%d' % number, lambda: True, '==', True,
                        lambda n=number: calls.append(n), 0, 0)
    rules.tick()
    assert rules.callback_rejections == 2
    release.set()
    # The final admitted callback signals completion after its visible side effect.
    deadline = time.monotonic() + 2
    while len(calls) < 8 and time.monotonic() < deadline:
        time.sleep(0.01)
    assert calls == list(range(8))
    rules.add_event('bad', lambda: True, '==', True, fail, 0, 0)
    rules.add_event('after', lambda: True, '==', True, finished.set, 0, 0)
    rules.tick()
    assert finished.wait(1), 'callback failure killed queue processing'
    assert rules.callback_failures == 1
    assert rules.last_callback_error == {'event_id': 'bad', 'error': 'cloud_timeout'}


test_callback_queue_is_bounded_fifo_and_reports_failures()


def wait_worker(rules):
    deadline = time.monotonic() + 2
    while rules._callback_worker_running and time.monotonic() < deadline:
        time.sleep(0.005)
    assert not rules._callback_worker_running


def test_pending_event_coalesces_and_disconnect_cancels_queued_work():
    home = module.SmartHomeMQTT('localhost', 'test', 'user', 'password', 'topic')
    rules = home.rule_engine
    reading = [True]
    entered, release = threading.Event(), threading.Event()
    calls = []

    def blocker():
        entered.set()
        release.wait(2)
        calls.append('running')

    rules.add_event('running', lambda: reading[0], '==', True, blocker, 0, 0)
    rules.tick()
    assert entered.wait(1)
    try:
        rules.add_event('queued', lambda: True, '==', True, lambda: calls.append('stale'), 0, 0)
        rules.tick()
        reading[0] = False
        rules.tick()
        reading[0] = True
        rules.tick()
        home.disconnect()
    finally:
        release.set()
        wait_worker(rules)
    assert calls == ['running']
    assert rules.callback_rejections == 0


def test_worker_start_failure_is_visible_and_next_edge_can_retry():
    import smart_home_rules
    rules = smart_home_rules.SmartHomeRules(object(), now_ms=lambda: 0)
    real_start = smart_home_rules._thread.start_new_thread
    reading, calls = [True], []

    def fail_start(*args):
        raise OSError('no_thread_memory')

    rules.add_event('retry', lambda: reading[0], '==', True, lambda: calls.append('ok'), 0, 0)
    try:
        smart_home_rules._thread.start_new_thread = fail_start
        rules.tick()
        assert rules.callback_rejections == 1
        assert 'no_thread_memory' in rules.last_callback_error['error']
        assert calls == []
    finally:
        smart_home_rules._thread.start_new_thread = real_start
    reading[0] = False
    rules.tick()
    reading[0] = True
    rules.tick()
    wait_worker(rules)
    assert calls == ['ok']


test_pending_event_coalesces_and_disconnect_cancels_queued_work()
test_worker_start_failure_is_visible_and_next_edge_can_retry()


def test_callback_exit_cannot_strand_later_tasks():
    rules = module.SmartHomeRules(object(), now_ms=lambda: 0)
    done = threading.Event()

    def exit_callback():
        raise SystemExit('callback_exit')

    rules.add_event('exit', lambda: True, '==', True, exit_callback, 0, 0)
    rules.add_event('after_exit', lambda: True, '==', True, done.set, 0, 0)
    rules.tick()
    assert done.wait(1), 'exiting callback stranded queued work'
    wait_worker(rules)
    assert rules.last_callback_error == {'event_id': 'exit', 'error': 'callback_exit'}


test_callback_exit_cannot_strand_later_tasks()


def test_callback_mqtt_publish_waits_for_poll_thread():
    fixtures['reset_fake_clients']()
    home = module.SmartHomeMQTT('localhost', 'test', 'user', 'password', 'topic')
    home.connect()
    home.subscribe_commands()
    client = home.client
    callback_entered = threading.Event()
    publish_callback = threading.Event()
    callback_returned = threading.Event()
    poll_entered = threading.Event()
    release_poll = threading.Event()
    overlap = []
    inside_poll = [False]
    original_publish = client.publish

    def guarded_publish(*args, **kwargs):
        if inside_poll[0]:
            overlap.append(True)
        return original_publish(*args, **kwargs)

    def slow_check_msg():
        inside_poll[0] = True
        poll_entered.set()
        assert release_poll.wait(2)
        inside_poll[0] = False
        client.check_calls += 1

    client.publish = guarded_publish

    def callback():
        callback_entered.set()
        assert publish_callback.wait(2)
        home.publish_event('controller_01', 'callback_done', {'ok': True})
        callback_returned.set()

    home.add_event('mqtt_publish', lambda: True, '==', True, callback, 0, 0)
    first_poll = threading.Thread(target=home._scheduled_command_poll,
                                  args=(home.command_timer_generation,))
    first_poll.start()
    first_poll.join(1)
    assert not first_poll.is_alive()
    assert callback_entered.wait(1)

    client.check_msg = slow_check_msg
    second_poll = threading.Thread(target=home._scheduled_command_poll,
                                   args=(home.command_timer_generation,))
    second_poll.start()
    try:
        assert poll_entered.wait(1)
        publish_callback.set()
        assert callback_returned.wait(1)
        assert not overlap, 'callback published through the MQTT socket while check_msg was active'
    finally:
        release_poll.set()
        second_poll.join(2)
        wait_worker(home.rule_engine)
    assert not overlap
    assert any(fixtures['ujson'].loads(message).get('event') == 'callback_done'
               for _, message, _, _ in client.published)


test_callback_mqtt_publish_waits_for_poll_thread()


def test_disconnect_invalidates_callback_mqtt_publish():
    fixtures['reset_fake_clients']()
    home = module.SmartHomeMQTT('localhost', 'test', 'user', 'password', 'topic')
    home.connect()
    home.subscribe_commands()
    entered, release = threading.Event(), threading.Event()

    def callback():
        entered.set()
        assert release.wait(2)
        home.publish_event('controller_01', 'stale_after_disconnect', {'ok': True})

    home.add_event('disconnect_publish', lambda: True, '==', True, callback, 0, 0)
    home._scheduled_command_poll(home.command_timer_generation)
    assert entered.wait(1)
    home.disconnect()
    release.set()
    wait_worker(home.rule_engine)
    assert home.callback_publish_rejections == 1
    assert not home._callback_publish_queue
    assert not any(fixtures['ujson'].loads(message).get('event') == 'stale_after_disconnect'
                   for _, message, _, _ in home.client.published)


test_disconnect_invalidates_callback_mqtt_publish()


def test_callback_publish_flush_has_per_poll_budget():
    fixtures['reset_fake_clients']()
    home = module.SmartHomeMQTT('localhost', 'test', 'user', 'password', 'topic')
    home.connect()
    home.subscribe_commands()
    generation = home.rule_engine._callback_generation
    home._begin_rule_callback(generation)
    for number in range(10):
        home.publish_event('controller_01', 'budget%d' % number, {})
    home._end_rule_callback(generation)
    before = len(home.client.published)
    home._scheduled_command_poll(home.command_timer_generation)
    assert len(home.client.published) - before == 8
    home._scheduled_command_poll(home.command_timer_generation)
    assert len(home.client.published) - before == 10


test_callback_publish_flush_has_per_poll_budget()
print('smart-home callback worker host tests passed')
