#!/usr/bin/env python3
"""Diff what two servers sent to the same bot scenario (vbot.py logs).
Usage: compare_bot.py <vanilla bot.jsonl> <avledet bot.jsonl>
Prints: message counts and send intervals, handshake fields, routed RPC payloads, and the final ZDO state
(the last version of every ZDO the bot saw), matched by prefab and position."""
import json
import statistics
import sys
from collections import Counter, defaultdict

EPS = 1e-4


def load(path):
    recs = [json.loads(l) for l in open(path)]
    return [r for r in recs if 'recv' in r]


def key(r):
    k = str(r['recv'])
    if k == 'RoutedRPC' and isinstance(r.get('data'), dict):
        k = 'routed:' + str(r['data']['method'])
    return k


def cadence(recs):
    times = defaultdict(list)
    for r in recs:
        times[key(r)].append(r['t'])
    return {k: (len(t), round(statistics.median([b - a for a, b in zip(t, t[1:])]), 2) if len(t) > 2 else None)
            for k, t in times.items()}


def first(recs, k):
    return next((r['data'] for r in recs if key(r) == k), None)


def zdo_state(recs):
    last = {}
    for r in recs:
        if r['recv'] == 'ZDOData' and isinstance(r.get('data'), dict):
            for z in r['data']['zdos']:
                last[tuple(z['id'])] = z
    return last


def zkey(z):
    return (str(z['prefab']),) + tuple(round(c, 2) for c in z['pos'])


def close(a, b):
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(close(x, y) for x, y in zip(a, b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(close(a[k], b[k]) for k in a)
    if isinstance(a, float) and isinstance(b, (int, float)):
        return abs(a - b) <= EPS * max(1, abs(a))
    return a == b


def main():
    va, av = load(sys.argv[1]), load(sys.argv[2])
    print('== messages: count, median interval s (vanilla | avledet)')
    cv, ca = cadence(va), cadence(av)
    for k in sorted(set(cv) | set(ca)):
        mark = '' if (k in cv) == (k in ca) else '   <-- only one side'
        print('  %-40s %-14s | %-14s%s' % (k, cv.get(k), ca.get(k), mark))

    print('== handshake')
    for k in ['ClientHandshake', 'RPC_ValidatedSimulationDistance', 'PeerInfo', 'AdminList', 'HistoricalPlayerList']:
        a, b = first(va, k), first(av, k)
        if isinstance(a, dict) and isinstance(b, dict):
            for f in a:
                if not close(a[f], b.get(f)):
                    print('  %s.%s: %r | %r' % (k, f, a[f], b.get(f)))
        elif not close(a, b):
            print('  %s: %r | %r' % (k, a, b))
    print('  first messages vanilla:', [key(r) for r in va[:8]])
    print('  first messages avledet:', [key(r) for r in av[:8]])

    print('== routed payloads (in order, per method)')
    for m in ['routed:RPC_DiscoverLocationResponse', 'routed:GlobalKeys', 'routed:LocationIcons', 'routed:RPC_Pong']:
        a = [r['data']['params'] for r in va if key(r) == m]
        b = [r['data']['params'] for r in av if key(r) == m]
        if not close(sorted(a, key=json.dumps), sorted(b, key=json.dumps)):
            print('  %s: %d | %d differ' % (m, len(a), len(b)))
            sa, sb = Counter(map(json.dumps, a)), Counter(map(json.dumps, b))
            for x in list((sa - sb).elements())[:5]:
                print('    vanilla only', x)
            for x in list((sb - sa).elements())[:5]:
                print('    avledet only', x)
    ev_a = Counter(json.dumps(r['data']['params']) for r in va if key(r) == 'routed:SetEvent')
    ev_b = Counter(json.dumps(r['data']['params']) for r in av if key(r) == 'routed:SetEvent')
    print('  SetEvent values: %s | %s' % (dict(ev_a), dict(ev_b)))

    print('== ZDO final state')
    za, zb = zdo_state(va), zdo_state(av)
    owners_a = Counter('none' if z['owner'] == 0 else 'set' for z in za.values())
    owners_b = Counter('none' if z['owner'] == 0 else 'set' for z in zb.values())
    print('  zdos %d | %d, owners %s | %s' % (len(za), len(zb), dict(owners_a), dict(owners_b)))
    ia, ib = defaultdict(list), defaultdict(list)
    for z in za.values():
        ia[zkey(z)].append(z)
    for z in zb.values():
        ib[zkey(z)].append(z)
    only_a = [k for k in ia if k not in ib]
    only_b = [k for k in ib if k not in ia]
    print('  only vanilla %d, only avledet %d' % (len(only_a), len(only_b)))
    for k in only_a[:8]:
        print('    vanilla only', k)
    for k in only_b[:8]:
        print('    avledet only', k)
    fields = Counter()
    samples = {}
    bits = Counter()
    for k in ia.keys() & ib.keys():
        for a, b in zip(ia[k], ib[k]):
            bits['pairs'] += 1
            # the floats come as float32 widened to double: equal means bit exact
            for f in ('pos', 'rot'):
                bits[f] += a.get(f) != b.get(f)
            for f in sorted(set(a) | set(b)):
                if f in ('id', 'owner', 'orev', 'drev'):
                    continue
                if not close(a.get(f), b.get(f)):
                    fields[f] += 1
                    samples.setdefault(f, (k, a.get(f), b.get(f)))
    print('  matched %d, not bit exact: pos %d, rot %d' % (bits['pairs'], bits['pos'], bits['rot']))
    print('  matched with differing fields:', dict(fields))
    for f, s in samples.items():
        print('    %s e.g. %s: %s | %s' % (f, s[0], json.dumps(s[1])[:200], json.dumps(s[2])[:200]))


if __name__ == '__main__':
    main()
