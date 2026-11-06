"""The bearer token must only be written after verified TLS, never on redirect."""
import os
import sys
import io

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'modules')))
import aqara_project_https as https


class FakeSocket:
    def __init__(self, response):
        self.response = io.BytesIO(response)
        self.writes = []
        self.closed = False
    def settimeout(self, value): self.timeout = value
    def connect(self, address): self.address = address
    def write(self, data): self.writes.append(data); return len(data)
    def readline(self, size): return self.response.readline(size)
    def read(self, size): return self.response.read(size)
    def close(self): self.closed = True


class SocketModule:
    SOCK_STREAM = 1
    def __init__(self, sock): self.sock = sock
    def getaddrinfo(self, host, port, family, kind):
        assert host == 'iot.mpython.cn' and port == 443
        return [(2, kind, 0, '', ('127.0.0.1', 443))]
    def socket(self, family, kind, protocol): return self.sock


class TLSModule:
    PROTOCOL_TLS_CLIENT = 1
    CERT_REQUIRED = 2
    def __init__(self, fail=False): self.fail = fail; self.context = None
    def SSLContext(self, mode):
        assert mode == self.PROTOCOL_TLS_CLIENT
        self.context = self
        return self
    def load_verify_locations(self, ca): self.ca = ca
    def wrap_socket(self, sock, server_hostname):
        assert self.verify_mode == self.CERT_REQUIRED
        assert server_hostname == 'iot.mpython.cn'
        assert b'BEGIN CERTIFICATE' in self.ca
        if self.fail: raise OSError('untrusted certificate')
        return sock


def with_fakes(response, fail=False):
    sock = FakeSocket(response)
    tls = TLSModule(fail)
    return sock, tls, SocketModule(sock)


def test_validated_tls_precedes_token_and_response_is_parsed():
    body = b'{"flag":true,"result":[]}'
    sock, tls, net = with_fakes(b'HTTP/1.1 200 OK\r\nContent-Length: 25\r\n\r\n' + body)
    result = https._request('aqpc1_' + 'a' * 64, 'GET', '/api/smart-home/project-control/devices', None, net, tls)
    assert result == {'flag': True, 'result': []}
    assert b'Authorization: Bearer aqpc1_' in b''.join(sock.writes)
    assert sock.timeout == 5 and sock.closed


def test_failed_handshake_never_writes_token():
    sock, tls, net = with_fakes(b'', fail=True)
    try:
        https._request('aqpc1_' + 'a' * 64, 'GET', '/api/smart-home/project-control/devices', None, net, tls)
        assert False, 'TLS verification must fail closed'
    except OSError:
        pass
    assert sock.writes == [] and sock.closed


def test_redirect_is_not_followed_and_body_limit_is_enforced():
    for response in [b'HTTP/1.1 302 Found\r\nLocation: https://evil.example/\r\nContent-Length: 0\r\n\r\n',
                     b'HTTP/1.1 200 OK\r\nContent-Length: 999999\r\n\r\n']:
        sock, tls, net = with_fakes(response)
        try:
            https._request('aqpc1_' + 'a' * 64, 'GET', '/api/smart-home/project-control/devices', None, net, tls)
            assert False, 'unsafe HTTP response must fail'
        except ValueError:
            pass
        assert sock.closed


test_validated_tls_precedes_token_and_response_is_parsed()
test_failed_handshake_never_writes_token()
test_redirect_is_not_followed_and_body_limit_is_enforced()
print('aqara project HTTPS tests passed')
