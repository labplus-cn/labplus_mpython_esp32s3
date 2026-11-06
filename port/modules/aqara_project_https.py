"""Bounded, certificate-verifying HTTPS for the fixed project-control origin."""

# DigiCert Global Root G2, SHA-256 CB3CCBB76031E5E0138F8DD39A23F9DE47FFC35E43C1144CEA27D46A5AB1CB5F.
# If the platform moves to a different CA the firmware trust store must be updated.
_CA = b'''-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
'''
_HOST = 'iot.mpython.cn'
_MAX_BODY = 32768


def _write_all(sock, data):
    offset = 0
    while offset < len(data):
        sent = sock.write(data[offset:])
        if not sent:
            raise OSError('write_failed')
        offset += sent


def _request(key, method, path, data, socket, tls):
    import json
    import time
    if time.gmtime()[0] < 2025:
        raise ValueError('clock_not_set')
    if (method not in ('GET', 'POST') or not path.startswith('/api/smart-home/project-control/') or
            '\r' in path or '\n' in path or not isinstance(key, str) or not key.startswith('aqpc1_') or
            len(key) != 70 or any(char not in '0123456789abcdef' for char in key[6:])):
        raise ValueError('invalid_request')
    payload = b'' if data is None else json.dumps(data).encode('utf-8')
    if len(payload) > 4096:
        raise ValueError('request_too_large')
    raw = None
    wrapped = None
    try:
        address = socket.getaddrinfo(_HOST, 443, 0, socket.SOCK_STREAM)[0]
        raw = socket.socket(address[0], address[1], address[2])
        raw.settimeout(5)
        raw.connect(address[-1])
        context = tls.SSLContext(tls.PROTOCOL_TLS_CLIENT)
        context.verify_mode = tls.CERT_REQUIRED
        context.load_verify_locations(_CA)
        wrapped = context.wrap_socket(raw, server_hostname=_HOST)
        headers = (method + ' ' + path + ' HTTP/1.0\r\nHost: ' + _HOST +
                   '\r\nAuthorization: Bearer ' + key + '\r\nAccept: application/json\r\n' +
                   'Accept-Encoding: identity\r\nConnection: close\r\nContent-Type: application/json\r\n' +
                   'Content-Length: ' + str(len(payload)) + '\r\n\r\n').encode('utf-8')
        _write_all(wrapped, headers)
        if payload:
            _write_all(wrapped, payload)
        status = wrapped.readline(1024).split()
        if len(status) < 2 or status[1] != b'200':
            raise ValueError('http_error')
        total_headers = 0
        length = None
        while True:
            line = wrapped.readline(1024)
            total_headers += len(line)
            if not line or total_headers > 8192 or len(line) >= 1024:
                raise ValueError('invalid_response')
            if line == b'\r\n':
                break
            name, separator, value = line.partition(b':')
            if not separator:
                raise ValueError('invalid_response')
            name = name.strip().lower()
            if name == b'transfer-encoding':
                raise ValueError('invalid_response')
            if name == b'content-length':
                if length is not None:
                    raise ValueError('invalid_response')
                length = int(value.strip())
        if length is None or length < 0 or length > _MAX_BODY:
            raise ValueError('invalid_response')
        body = b''
        while len(body) < length:
            part = wrapped.read(min(1024, length - len(body)))
            if not part:
                raise ValueError('invalid_response')
            body += part
        return json.loads(body.decode('utf-8'))
    finally:
        if wrapped is not None:
            wrapped.close()
        elif raw is not None:
            raw.close()


def request(key, method, path, data):
    import socket
    import tls
    try:
        return _request(key, method, path, data, socket, tls)
    except (OSError, ValueError):
        # Avoid propagating backend/socket messages that could contain sensitive values.
        raise ValueError('secure_request_failed')
