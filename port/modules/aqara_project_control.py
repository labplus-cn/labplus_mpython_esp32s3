"""Project-scoped Aqara operations through the platform, never through Studio directly."""

_BASE = '/api/smart-home/project-control'
_ERRORS = ('project_key_invalid', 'endpoint_not_allowed', 'endpoint_offline', 'action_not_supported',
           'invalid_action_data', 'idempotency_conflict', 'broker_unavailable', 'command_not_found',
           'command_timeout', 'rate_limited', 'studio_disabled')


def _identifier(value):
    if not isinstance(value, str) or not 1 <= len(value) <= 128:
        raise ValueError('invalid_identifier')
    for char in value:
        if char not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-':
            raise ValueError('invalid_identifier')
    return value


def _secure_request(key, method, path, data):
    # This dedicated implementation refuses redirects and requires verified TLS.
    from aqara_project_https import request
    return request(key, method, path, data)


class AqaraProjectControl:
    def __init__(self, key, request=None):
        if (not isinstance(key, str) or len(key) != 70 or not key.startswith('aqpc1_') or
                any(char not in '0123456789abcdef' for char in key[6:])):
            raise ValueError('project_key_invalid')
        self._key = key
        self._request = request or _secure_request
        self.last_command = None
        self._last_request = None

    def _call(self, method, path, data=None):
        response = self._request(self._key, method, path, data)
        if not isinstance(response, dict) or response.get('flag') is not True:
            code = response.get('code') if isinstance(response, dict) else None
            raise ValueError(code if code in _ERRORS else 'project_control_failed')
        return response.get('result')

    def devices(self):
        return self._call('GET', _BASE + '/devices')

    def state(self, endpoint_uid):
        return self._call('GET', _BASE + '/devices/' + _identifier(endpoint_uid) + '/state')

    def command(self, endpoint_uid, action, data, request_id):
        self._last_request = None
        self.last_command = None
        endpoint_uid = _identifier(endpoint_uid)
        request_id = _identifier(request_id)
        if len(request_id) > 64 or action not in ('open', 'close', 'stop', 'set_position_pct') or not isinstance(data, dict):
            raise ValueError('invalid_action_data')
        if action == 'set_position_pct':
            position = data.get('position_pct')
            if (len(data) != 1 or isinstance(position, bool) or not isinstance(position, (int, float)) or
                    not 0 <= position <= 100):
                raise ValueError('invalid_action_data')
        elif data:
            raise ValueError('invalid_action_data')
        self._last_request = {'request_id': request_id, 'endpoint_uid': endpoint_uid, 'action': action, 'data': dict(data)}
        return self.retry_last_command()

    def retry_last_command(self):
        if self._last_request is None:
            raise ValueError('command_not_found')
        result = self._call('POST', _BASE + '/commands', self._last_request)
        self.last_command = result
        return result

    def command_status(self, command_id):
        return self._call('GET', _BASE + '/commands/' + _identifier(command_id))
