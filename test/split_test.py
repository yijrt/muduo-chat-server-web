#!/usr/bin/env python3
# Verify the length-prefix codec handles SPLIT and COALESCED TCP segments.
import socket, struct, json, time

HOST, PORT = '127.0.0.1', 8888

def pack(obj):
    p = json.dumps(obj, ensure_ascii=False).encode('utf-8')
    return struct.pack('>I', len(p)) + p

def recv_msg(s):
    h = b''
    while len(h) < 4:
        c = s.recv(4 - len(h))
        if not c: return None
        h += c
    n = struct.unpack('>I', h)[0]
    b = b''
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: return None
        b += c
    return b.decode('utf-8', errors='replace')

def drain(s, t=0.6):
    s.settimeout(t); out = []
    while True:
        try:
            r = recv_msg(s)
            if r is None: break
            out.append(r)
        except socket.timeout: break
    s.settimeout(None); return out

def main():
    s = socket.socket(); s.connect((HOST, PORT))

    # ---- TEST A: split ONE message into 3 chunks ----
    print('=' * 55)
    print('TEST A: split one login message into 2 / 5 / rest bytes')
    print('=' * 55)
    pkt = pack({"type": "login", "username": "zhule", "password": "123456"})
    print('total packet = %d bytes' % len(pkt))
    s.sendall(pkt[:2]);        print('  sent [0:2]   -> 2 bytes (incomplete length header)')
    time.sleep(0.3)
    s.sendall(pkt[2:7]);       print('  sent [2:7]   -> 5 bytes (header + 1 byte body)')
    time.sleep(0.3)
    s.sendall(pkt[7:]);        print('  sent [7:]    -> %d bytes (rest)' % (len(pkt) - 7))
    r = drain(s)
    if r:
        print('RESULT: server replied -> SPLIT HANDLING OK')
        for x in r: print('   << %s' % x.replace('\n', ' '))
    else:
        print('RESULT: NO reply -> SPLIT HANDLING BROKEN')
    s.close()

    # ---- TEST B: 3 messages in ONE send ----
    print()
    print('=' * 55)
    print('TEST B: coalesce 3 messages into one send (sticky packets)')
    print('=' * 55)
    s2 = socket.socket(); s2.connect((HOST, PORT))
    blob = (pack({"type": "heartbeat", "username": "zhule"}) +
            pack({"type": "heartbeat", "username": "zhule"}) +
            pack({"type": "heartbeat", "username": "zhule"}))
    print('one sendall of %d bytes containing 3 separate messages' % len(blob))
    s2.sendall(blob)
    r = drain(s2)
    pongs = sum(1 for x in r if '"pong"' in x)
    print('received %d responses, %d of them are pong' % (len(r), pongs))
    print('RESULT: %s' % ('COALESCED HANDLING OK (expect 3)' if pongs == 3
                          else 'COALESCED HANDLING BROKEN (expect 3, got %d)' % pongs))
    s2.close()

    # ---- TEST C: bogus length header ----
    print()
    print('=' * 55)
    print('TEST C: forged length header 0xFFFFFFFF')
    print('=' * 55)
    s3 = socket.socket(); s3.connect((HOST, PORT))
    s3.sendall(b'\xff\xff\xff\xff' + b'x' * 16)
    time.sleep(0.5)
    try:
        s3.settimeout(1.0)
        r = s3.recv(16)
        if r == b'':
            print('RESULT: server CLOSED the connection -> protocol error handled OK')
        else:
            print('RESULT: server still open, got %r -> expected disconnect' % r)
    except socket.timeout:
        print('RESULT: connection still open / hanging -> expected disconnect')
    s3.close()

if __name__ == '__main__':
    main()