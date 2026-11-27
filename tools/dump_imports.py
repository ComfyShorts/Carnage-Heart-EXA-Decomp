#!/usr/bin/env python3
"""Dump PSP module info and import stubs from a decrypted PRX ELF (stdlib only)."""
import struct, sys, json

def main(path):
    d = open(path, 'rb').read()
    phoff, = struct.unpack_from('<I', d, 0x1c)
    _, off0, vaddr0, paddr0 = struct.unpack_from('<IIII', d, phoff)
    v2o = lambda va: va - vaddr0 + off0
    cstr = lambda va: d[v2o(va):d.index(b'\0', v2o(va))].decode('ascii', 'replace')
    mi = paddr0  # PRX: p_paddr of first LOAD = file offset of sceModuleInfo
    attr, ver, name = struct.unpack_from('<HH28s', d, mi)
    gp, ent_top, ent_end, stub_top, stub_end = struct.unpack_from('<5I', d, mi + 32)
    info = dict(name=name.rstrip(b'\0').decode(), attr=attr, version=ver, gp=gp,
                ent=(ent_top, ent_end), stub=(stub_top, stub_end), libs=[])
    p = stub_top
    while p < stub_end:
        nptr, lver, flags, esz, vcnt, fcnt, nids, funcs = struct.unpack_from('<IHHBBHII', d, v2o(p))
        lib = dict(name=cstr(nptr) if nptr else None, version=lver, flags=flags,
                   funcs=[], vars=[])
        for i in range(fcnt):
            nid, = struct.unpack_from('<I', d, v2o(nids) + 4 * i)
            lib['funcs'].append(dict(nid=nid, stub=funcs + 8 * i))
        if esz >= 6 and vcnt:
            vptr, = struct.unpack_from('<I', d, v2o(p) + 20)
            for i in range(vcnt):
                nid, addr = struct.unpack_from('<II', d, v2o(vptr) + 8 * i)
                lib['vars'].append(dict(nid=nid, addr=addr))
        info['libs'].append(lib)
        p += esz * 4
    return info

if __name__ == '__main__':
    print(json.dumps(main(sys.argv[1]), indent=1))
