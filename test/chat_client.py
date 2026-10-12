#!/usr/bin/env python3
# Length-prefixed protocol client. Pure ASCII, no encoding issues.
# Wire format: [4-byte big-endian length][JSON payload]
import socket
import struct
import json
import sys

HOST = '127.0.0.1'
PORT = 8888


def send_msg(sock, obj):
    payload = json.dumps(obj, ensure_ascii=False).encode('utf-8')
    packet = struct.pack('>I', len(payload)) + payload
    sock.sendall(packet)
    print('  >> SENT (%dB): %s' % (len(payload), payload.decode('utf-8')))


def recv_msg(sock):
    header = b''
    while len(header) < 4:
        chunk = sock.recv(4 - len(header))
        if not chunk:
            return None
        header += chunk
    length = struct.unpack('>I', header)[0]

    body = b''
    while len(body) < length:
        chunk = sock.recv(length - len(body))
        if not chunk:
            return None
        body += chunk
    return body.decode('utf-8', errors='replace'), body


def recv_all(sock, timeout=0.8):
    sock.settimeout(timeout)
    out = []
    while True:
        try:
            r = recv_msg(sock)
            if r is None:
                break
            out.append(r)
        except socket.timeout:
            break
    sock.settimeout(None)
    return out


def show(tag, results):
    if not results:
        print('  << %s: (no response)' % tag)
        return
    for i, (text, raw) in enumerate(results):
        print('  << %s[%d] payload=%dB  raw: %s' % (tag, i, len(raw), text))
        try:
            obj = json.loads(text)
            print('       JSON VALID  keys=%s' % list(obj.keys()))
            if 'unread' in obj:
                print('       unread field present, %d items' % len(obj['unread']))
        except json.JSONDecodeError as e:
            print('       JSON INVALID: %s' % e)


def connect():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.connect((HOST, PORT))
    except ConnectionRefusedError:
        print('cannot connect to %s:%d - is the server running?' % (HOST, PORT))
        sys.exit(1)
    return s


def test_logout():
    print('=== scenario: login then logout (deadlock test) ===')
    s = connect()
    send_msg(s, {"type": "login", "username": "zhule", "password": "123456"})
    show('login', recv_all(s))
    send_msg(s, {"type": "logout", "username": "zhule"})
    show('logout', recv_all(s))
    s.close()
    print('')
    print('OK   = both responses received, no hang -> deadlock is FIXED')
    print('HANG = stuck after logout -> deadlock STILL PRESENT')


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'logout':
        test_logout()
        return

    print('=== scenario: full flow ===')
    s = connect()

    print('[1] login')
    send_msg(s, {"type": "login", "username": "zhule", "password": "123456"})
    show('login', recv_all(s))

    print('[2] heartbeat')
    send_msg(s, {"type": "heartbeat", "username": "zhule"})
    show('heartbeat', recv_all(s))

    print('[3] history')
    send_msg(s, {"type": "history", "username": "zhule", "limit": "5"})
    show('history', recv_all(s))

    print('[4] chat to self')
    send_msg(s, {"type": "chat", "from": "zhule", "to": "zhule", "content": "selftest"})
    show('chat', recv_all(s))

    print('[5] history with missing limit')
    send_msg(s, {"type": "history", "username": "zhule"})
    show('history-noLimit', recv_all(s))

    print('[6] unknown type')
    send_msg(s, {"type": "foobar"})
    show('unknown', recv_all(s))

    s.close()
    print('')
    print('connection closed - check server log for offline message')


if __name__ == '__main__':
    main()