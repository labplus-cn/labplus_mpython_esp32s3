"""Host tests for the project-scoped board client; no real tokens or devices."""
import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'modules')))
import aqara_project_control as module


def test_device_queries_and_commands_use_fixed_project_routes():
    calls = []
    def request(key, method, path, data):
        calls.append((key, method, path, data))
        return {'flag': True, 'result': {'ok': True}}
    client = module.AqaraProjectControl('aqpc1_' + 'a' * 64, request=request)
    assert client.last_command is None
    assert client.devices() == {'ok': True}
    client.state('endpoint-1')
    client.command('endpoint-1', 'open', {}, 'request-1')
    assert client.last_command == {'ok': True}
    client.command_status('command-1')
    assert calls[0][1:] == ('GET', '/api/smart-home/project-control/devices', None)
    assert calls[1][2] == '/api/smart-home/project-control/devices/endpoint-1/state'
    assert calls[2][3] == {'request_id': 'request-1', 'endpoint_uid': 'endpoint-1', 'action': 'open', 'data': {}}
    assert calls[3][2] == '/api/smart-home/project-control/commands/command-1'


def test_invalid_paths_and_commands_never_send_requests():
    calls = []
    client = module.AqaraProjectControl('aqpc1_' + 'a' * 64, request=lambda *args: calls.append(args))
    for action in [lambda: client.state('../other'),
                   lambda: client.command('endpoint-1', 'reboot', {}, 'r1'),
                   lambda: client.command('endpoint-1', 'set_position_pct', {'position_pct': 101}, 'r1')]:
        try:
            action()
            assert False, 'invalid input must fail'
        except ValueError:
            pass
    assert calls == []


def test_api_failure_is_not_treated_as_success():
    client = module.AqaraProjectControl('aqpc1_' + 'a' * 64,
        request=lambda *args: {'flag': False, 'code': 'endpoint_offline'})
    try:
        client.devices()
        assert False, 'API failure must raise'
    except ValueError as error:
        assert str(error) == 'endpoint_offline'


def test_retry_after_unknown_delivery_reuses_same_request_id():
    calls = []
    def request(key, method, path, data):
        calls.append(data.copy())
        if len(calls) == 1:
            raise ValueError('secure_request_failed')
        return {'flag': True, 'result': {'command_id': 'cmd', 'status': 'queued'}}
    client = module.AqaraProjectControl('aqpc1_' + 'a' * 64, request=request)
    try:
        client.command('endpoint-1', 'open', {}, 'same-request')
        assert False
    except ValueError:
        pass
    assert client.retry_last_command()['command_id'] == 'cmd'
    assert calls[0] == calls[1]


test_device_queries_and_commands_use_fixed_project_routes()
test_invalid_paths_and_commands_never_send_requests()
test_api_failure_is_not_treated_as_success()
test_retry_after_unknown_delivery_reuses_same_request_id()
print('aqara project client tests passed')
