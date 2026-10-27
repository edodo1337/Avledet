#!/usr/bin/env python3
"""Find the native function behind a Mono internal call in UnityPlayer.so (stripped, one dynamic symbol).
The registration loop at 0x1d8caa0 walks two parallel arrays of 3746 (0xea2) entries filled by
R_X86_64_RELATIVE relocations: names at 0x22e5fc0, functions at 0x22ed4d0. Valid for the 1.0.20
server build; for another build find the loop again (it calls mono_add_internal_call through a pointer).
Usage: unity-icall.py <UnityPlayer.so> <name substring>...   then: objdump -d --start-address=<addr>
Found so far: Quaternion::LookRotation_Injected 0xc85e80 (MatrixToQuaternion 0x1220ea0),
Internal_FromEulerRad_Injected 0xc859d0, Internal_ToEulerRad_Injected 0xc85b90."""
import sys, struct
path = sys.argv[1]; wanted = sys.argv[2:]
data = open(path, 'rb').read()
e_shoff = struct.unpack_from('<Q', data, 0x28)[0]; e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', data, 0x3a)
secs = [struct.unpack_from('<IIQQQQIIQQ', data, e_shoff + i * e_shentsize) for i in range(e_shnum)]
shstr = secs[e_shstrndx]
def sname(sh): o = shstr[4] + sh[0]; return data[o:data.index(b'\0', o)].decode()
def va2off(va):
    for sh in secs:
        if sh[3] and sh[3] <= va < sh[3] + sh[5] and sh[1] != 8: return sh[4] + va - sh[3]
rela = [sh for sh in secs if sname(sh) == '.rela.dyn'][0]
off = {}
for o in range(rela[4], rela[4] + rela[5], 24):
    r_off, r_info, r_add = struct.unpack_from('<QQq', data, o)
    if r_info & 0xffffffff == 8: off[r_off] = r_add
def run(start):
    out = []; o = start
    while o in off: out.append(off[o]); o += 8
    return out
names = run(0x22e5fc0)[:0xea2]
def cstr(va):
    o = va2off(va); return data[o:data.index(b'\0', o)].decode('latin1')
for i, n in enumerate(names):
    s = cstr(n)
    if any(w in s for w in wanted): f = off.get(0x22ed4d0 + 8 * i); print(s, hex(f) if f else '-')
