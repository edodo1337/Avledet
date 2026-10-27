#!/usr/bin/env python3
"""Scripted Valheim 1.0.20 peer over plain TCP (ZSocket2 framing), to run the same scenario against the vanilla
server (with tools/tcp-listen) and Avledet (experimental.server-tcp) and diff what each server sends.
Usage: vbot.py <host> <port> <scenario> <out.jsonl> [password]
Every message from the server goes to out.jsonl as one JSON line; ZDOs are decoded."""
import hashlib
import json
import os
import queue
import socket
import struct
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def stable_hash(s):
    h1, h2 = 5381, 5381
    for i in range(0, len(s), 2):
        h1 = ((h1 << 5) + h1 ^ ord(s[i])) & 0xFFFFFFFF
        if i + 1 < len(s):
            h2 = ((h2 << 5) + h2 ^ ord(s[i + 1])) & 0xFFFFFFFF
    v = (h1 + h2 * 1566083941) & 0xFFFFFFFF
    return v - (1 << 32) if v >= 1 << 31 else v


NAMES = {stable_hash(n): n for n in open(os.path.join(HERE, 'names.txt')).read().split('\n') if n}


class W:
    def __init__(self): self.b = bytearray()
    def raw(self, fmt, *v): self.b += struct.pack('<' + fmt, *v); return self
    def int(self, v): return self.raw('i', v)
    def uint(self, v): return self.raw('I', v)
    def long(self, v): return self.raw('q', v)
    def float(self, v): return self.raw('f', v)
    def bool(self, v): return self.raw('B', 1 if v else 0)
    def v3(self, v): return self.raw('3f', *v)
    def zdoid(self, uid, id): return self.long(uid).uint(id)
    def bytes(self, b): self.int(len(b)); self.b += b; return self
    def pkg(self, w): return self.bytes(bytes(w.b))

    def str(self, s):
        b = s.encode('utf-8')
        n = len(b)
        while n >= 0x80:
            self.b.append(n & 0x7F | 0x80)
            n >>= 7
        self.b.append(n)
        self.b += b
        return self


class R:
    def __init__(self, b): self.b, self.p = b, 0
    def raw(self, fmt):
        v = struct.unpack_from('<' + fmt, self.b, self.p); self.p += struct.calcsize('<' + fmt); return v
    def u8(self): return self.raw('B')[0]
    def u16(self): return self.raw('H')[0]
    def int(self): return self.raw('i')[0]
    def uint(self): return self.raw('I')[0]
    def long(self): return self.raw('q')[0]
    def float(self): return self.raw('f')[0]
    def double(self): return self.raw('d')[0]
    def bool(self): return self.u8() != 0
    def v3(self): return list(self.raw('3f'))
    def quat(self): return list(self.raw('4f'))
    def zdoid(self): return [self.long(), self.uint()]
    def end(self): return self.p >= len(self.b)

    def str(self):
        n = sh = 0
        while True:
            c = self.u8(); n |= (c & 0x7F) << sh; sh += 7
            if not c & 0x80: break
        v = self.b[self.p:self.p + n].decode('utf-8', 'replace'); self.p += n; return v

    def bytes(self):
        n = self.int(); v = self.b[self.p:self.p + n]; self.p += n; return v

    def pkg(self): return R(self.bytes())

    def num_items(self):
        a = self.u8()
        return ((a & 0x7F) << 8) | self.u8() if a & 0x80 else a


def read_zdo(r):
    """ZDO.Serialize (network form)"""
    flags = r.u16()
    z = {'prefab': NAMES.get(h := r.int(), h), 'persistent': bool(flags & 0x100), 'distant': bool(flags & 0x200),
         'type': flags >> 10 & 3}
    if flags & 0x1000:
        z['rot'] = r.v3()
    if flags & 0xFF:
        if flags & 1:
            z['conn'] = [r.u8(), r.zdoid()]
        for bit, tag, read in [(2, 'f', r.float), (4, 'v', r.v3), (8, 'q', r.quat), (0x10, 'i', r.int),
                               (0x20, 'l', r.long), (0x40, 's', r.str), (0x80, 'b', lambda: r.bytes().hex())]:
            if flags & bit:
                z[tag] = {str(r.int()): read() for _ in range(r.num_items())}
    return z


def read_zdodata(r):
    invalid = [r.zdoid() for _ in range(r.int())]
    zdos = []
    while True:
        uid = r.zdoid()
        if uid == [0, 0]:
            break
        z = {'id': uid, 'orev': r.u16(), 'drev': r.uint(), 'owner': r.long(), 'pos': r.v3()}
        z.update(read_zdo(r.pkg()))
        zdos.append(z)
    return {'invalid': invalid, 'zdos': zdos}


def read_players(r):
    out = []
    for _ in range(r.int()):
        p = {'name': r.str(), 'char': r.zdoid(), 'id': r.str(), 'display': r.str(), 'assigned': r.str(),
             'playfab': r.str()}
        if r.bool():
            p['pos'] = r.v3()
        out.append(p)
    return out


def read_routed_params(name, r):
    if name == 'GlobalKeys':
        return [r.str() for _ in range(r.int())]
    if name == 'RPC_DiscoverLocationResponse':
        return [r.str(), r.int(), r.v3(), r.bool()]
    if name == 'SetEvent':
        return [r.str(), r.float(), r.v3()]
    if name in ('RPC_Pong', 'Pong'):
        return [r.float()]
    if name == 'ShowMessage':
        return [r.int(), r.str()]
    if name == 'ChatMessage':
        return [r.v3(), r.int(), r.str(), r.str(), r.str()]
    if name == 'DestroyZDO':
        p = r.pkg()
        return [p.zdoid() for _ in range(p.int())]
    if name == 'UpdateClientEventsList':
        return [r.str()]
    if name == 'LocationIcons':
        p = r.pkg()
        return [[p.v3(), p.str()] for _ in range(p.int())]
    return r.b[r.p:].hex()


# server -> client ZRpc decoders
def decode(name, r):
    if name == 'ClientHandshake':
        return [r.bool(), r.str()]
    if name in ('Error',):
        return [r.int()]
    if name in ('NetTime',):
        return [r.double()]
    if name in ('RemotePrint',):
        return [r.str()]
    if name == 'RPC_ValidatedSimulationDistance':
        p = r.pkg()
        return [p.int(), p.int(), p.bool()]
    if name == 'PeerInfo':
        p = r.pkg()
        return {'uid': p.long(), 'version': p.str(), 'net': p.uint(), 'refpos': p.v3(), 'name': p.str(),
                'playfab': p.str(), 'simdist': [p.int(), p.int(), p.bool()], 'world': p.str(), 'seed': p.int(),
                'seedName': p.str(), 'worldUid': p.long(), 'worldGen': p.int(), 'netTime': p.double()}
    if name == 'PlayerList':
        return read_players(r.pkg())
    if name == 'AdminList':
        p = r.pkg()
        return [p.str() for _ in range(p.int())]
    if name == 'HistoricalPlayerList':
        p = r.pkg()
        return [[p.str(), p.str(), p.str(), p.str()] for _ in range(p.int())]
    if name == 'ZDOData':
        return read_zdodata(r.pkg())
    if name == 'RoutedRPC':
        p = r.pkg()
        d = {'msg': p.long(), 'sender': p.long(), 'target': p.long(), 'zdo': p.zdoid()}
        h = p.int()
        d['method'] = NAMES.get(h, h)
        d['params'] = read_routed_params(d['method'], p.pkg())
        return d
    return r.b[r.p:].hex()


class Peer:
    def __init__(self, host, port, out):
        self.s = socket.create_connection((host, port))
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.out = open(out, 'w')
        self.t0 = time.time()
        self.q = queue.Queue()
        self.uid = int.from_bytes(os.urandom(7), 'little')
        self.server_uid = 0
        self.msg = 0
        self.lock = threading.Lock()
        threading.Thread(target=self._recv, daemon=True).start()

    def _recv(self):
        try:
            while True:
                n = struct.unpack('<i', self._exact(4))[0]
                self.q.put(self._exact(n))
        except (OSError, ConnectionError):
            self.q.put(None)

    def _exact(self, n):
        b = b''
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c:
                raise ConnectionError
            b += c
        return b

    def send(self, w):
        with self.lock:
            self.s.sendall(struct.pack('<i', len(w.b)) + bytes(w.b))

    def rpc(self, method, *args):
        w = W().int(stable_hash(method))
        for a in args:
            a(w)
        self.send(w)
        self.log('sent', method)

    def routed(self, method, write, target=None):
        params = W()
        write(params)
        self.msg += 1
        p = W().long(self.uid + self.msg).long(self.uid).long(self.server_uid if target is None else target)
        p.zdoid(0, 0).int(stable_hash(method)).pkg(params)
        self.rpc('RoutedRPC', lambda w: w.pkg(p))

    def log(self, kind, name, data=None):
        rec = {'t': round(time.time() - self.t0, 3), kind: name}
        if data is not None:
            rec['data'] = data
        self.out.write(json.dumps(rec) + '\n')
        self.out.flush()

    def pump(self, seconds, until=None):
        """handle server messages for some seconds (or until a ZRpc name arrives); returns its data"""
        end = time.time() + seconds
        while time.time() < end:
            try:
                b = self.q.get(timeout=max(0.01, end - time.time()))
            except queue.Empty:
                break
            if b is None:
                self.log('closed', 'socket')
                raise ConnectionError('closed by server')
            r = R(b)
            h = r.int()
            if h == 0:  # ZRpc ping: answer it
                if r.bool():
                    self.send(W().int(0).bool(False))
                continue
            name = NAMES.get(h, h)
            try:
                data = decode(name, r)
            except Exception as e:  # keep going, the raw bytes are in the log
                data = {'decode_error': str(e), 'hex': b.hex()}
            self.log('recv', name, data)
            if name == 'PeerInfo':
                self.server_uid = data['uid']
            if until and name == until:
                return data
        return None


def md5_ascii(s):
    return hashlib.md5(s.encode('latin-1')).digest().decode('ascii', 'replace').replace('�', '?')


def join(p, password, name='Bot', refpos=(0, 30, 0), simdist=(2, 2, True)):
    p.rpc('ServerHandshake', lambda w: w.str(''))
    need_pw, salt = p.pump(10, until='ClientHandshake')
    hashed = md5_ascii(password + salt) if need_pw else ''
    info = W().long(p.uid).str('1.0.20').uint(41).v3(refpos).str(name).str('')
    info.int(simdist[0]).int(simdist[1]).bool(simdist[2]).str(hashed).str('').bytes(b'')
    p.rpc('PeerInfo', lambda w: w.pkg(info))
    p.pump(10, until='PeerInfo')


def move(p, pos, public=True, data=None):
    data = data or {}
    d = W().v3(pos).bool(public).int(len(data))
    for k, v in data.items():
        d.str(k).str(v)
    p.rpc('ServerSyncedPlayerData', lambda w: w.pkg(d))


def discover(p, location, pos, all=False):
    p.routed('RPC_DiscoverClosestLocation',
             lambda w: w.str(location).v3(pos).str('pin').int(3).bool(True).bool(all))


def scenario_basic(p, password):
    join(p, password)
    move(p, (0, 30, 0))
    # vanilla starts sending zones ~15 s after the join and needs ~20 s more for the area
    p.pump(45)
    p.routed('RPC_Ping', lambda w: w.float(1.5))
    discover(p, 'Vendor_BlackForest', (0, 0, 0))
    discover(p, 'Eikthyrnir', (0, 0, 0))
    discover(p, 'Runestone_Boars', (0, 0, 0), all=True)
    discover(p, 'NoSuchLocation', (0, 0, 0))
    p.pump(5)
    for pos in [(300, 30, 300), (-700, 30, 1200), (2000, 30, -1500)]:
        move(p, pos)
        p.pump(20)
    p.rpc('Disconnect')
    p.pump(3)


def scenario_raid(p, password):
    """a player near a base who can get army_eikthyr: the server starts it at the player's refPos"""
    join(p, password)
    move(p, (100, 30, 100), data={'possibleEvents': 'army_eikthyr,foo', 'baseValue': '5'})
    p.pump(40)
    p.rpc('Disconnect')
    p.pump(2)


def scenario_pevent(p, password):
    """start jotun_invasion as a destroyed BlackIce_Start would, list it, stop it
    (waits for the vanilla zone sends first, they start ~15 s after the join)"""
    join(p, password)
    move(p, (0, 30, 0))
    p.pump(45)
    p.routed('RequestStartEvent', lambda w: w.int(0))
    p.pump(15)
    p.routed('RequestActiveEventsList', lambda w: None)
    p.pump(3)
    p.routed('RequestStopEvent', lambda w: w.int(0))
    p.pump(3)
    p.rpc('Disconnect')
    p.pump(2)


def scenario_pevent_keep(p, password):
    """start jotun_invasion and leave it running (for save tests)"""
    join(p, password)
    move(p, (0, 30, 0))
    p.pump(5)
    p.routed('RequestStartEvent', lambda w: w.int(0))
    p.pump(5)
    p.rpc('Disconnect')
    p.pump(2)


def scenario_admin(p, password):
    """remote admin commands that change world modifiers (bot host must be in adminlist.txt)"""
    join(p, password)
    move(p, (0, 30, 0))
    p.pump(3)
    for cmd in ['setworldmodifier raids muchless', 'setkey nomap', 'setkey bogus 1', 'setkey playerdamage 125',
                'removekey nomap', 'setworldmodifier combat hard', 'setworldmodifier nosuch less',
                'setworldpreset hardcore', 'setworldpreset nosuch', 'resetworldkeys', 'setworldpreset casual',
                'skiptime 1000']:
        p.log('cmd', cmd)
        p.rpc('RPC_RemoteCommand', lambda w, c=cmd: w.str(c))
        p.pump(2)
    p.rpc('Disconnect')
    p.pump(2)


def scenario_cheats(p, password):
    """devcommands from an admin turns on the server cheats: sleep, skiptime, resetkeys (bot host in adminlist.txt).
    Waits for the zone generation first: the vanilla time skip runs one fixed step per server frame"""
    join(p, password)
    move(p, (0, 30, 0))
    p.pump(45)
    for cmd in ['sleep', 'skiptime 500', 'devcommands', 'skiptime 1000', 'setkey bogus 1', 'resetkeys',
                'skiptime', 'sleep', 'listkeys', 'devcommands', 'skiptime 777']:
        p.log('cmd', cmd)
        p.rpc('RPC_RemoteCommand', lambda w, c=cmd: w.str(c))
        p.pump(4 if cmd == 'sleep' else 2)
    p.pump(14)
    p.rpc('Disconnect')
    p.pump(2)


def scenario_genloc(p, password):
    # far outside the world: no zone generates, both servers keep the same placed set
    join(p, password, refpos=(100000, 0, 100000))
    move(p, (100000, 0, 100000))
    p.pump(30)
    p.rpc('RPC_RemoteCommand', lambda w: w.str('genloc'))
    p.pump(40)
    p.rpc('Disconnect')
    p.pump(2)


def zdo_data(p, zdos):
    """ZDOData with new ZDOs of this peer: (id, prefab hash, flags, extra bytes)"""
    pkg = W().int(0)
    for id, prefab, flags, extra in zdos:
        z = W().raw('H', flags).int(prefab)
        z.b += extra
        pkg.zdoid(p.uid, id).raw('H', 1).uint(1).long(p.uid).v3((0, 30, 0)).pkg(z)
    pkg.zdoid(0, 0)
    p.rpc('ZDOData', lambda w: w.pkg(pkg))


def scenario_junk(p, password):
    """broken packages (an empty one is not possible: the test tcp framing drops it): a ping without its bool, an rpc without args, a ZDO with an
    unknown prefab and one cut short. The peer must stay and get zones after it"""
    join(p, password)
    move(p, (0, 30, 0))
    p.pump(45)
    p.log('junk', 'short ping')
    p.send(W().int(0))
    p.log('junk', 'no args')
    p.rpc('RPC_RemoteCommand')
    p.log('junk', 'bad zdos')
    zdo_data(p, [(1, 12345, 0, b''), (2, stable_hash('Player'), 1 << 1, b'')])
    p.pump(3)
    p.routed('RPC_Ping', lambda w: w.float(1.5))
    move(p, (300, 30, 300))
    p.pump(20)
    p.rpc('Disconnect')
    p.pump(2)


def scenario_churn(p, password):
    """a short visit to two already generated places, for memory checks over many joins"""
    join(p, password)
    move(p, (0, 30, 0))
    p.pump(8)
    move(p, (300, 30, 300))
    p.pump(8)
    p.rpc('Disconnect')
    p.pump(1)


def scenario_badpw(p, password):
    """wrong password: Error 6, the client leaves"""
    try:
        join(p, 'wrong' + password)
        p.pump(3)
    except ConnectionError:
        pass


SCENARIOS = {'churn': scenario_churn, 'junk': scenario_junk, 'cheats': scenario_cheats, 'badpw': scenario_badpw, 'genloc': scenario_genloc, 'admin': scenario_admin, 'basic': scenario_basic, 'raid': scenario_raid, 'pevent': scenario_pevent,
             'pevent_keep': scenario_pevent_keep}


def main():
    host, port, scenario, out = sys.argv[1:5]
    password = sys.argv[5] if len(sys.argv) > 5 else 'secret123'
    p = Peer(host, int(port), out)
    try:
        SCENARIOS[scenario](p, password)
    except ConnectionError as e:
        print('connection:', e)
    p.log('done', scenario)


if __name__ == '__main__':
    main()
