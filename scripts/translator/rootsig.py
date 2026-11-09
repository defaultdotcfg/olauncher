"""rootsig.py file.dxbc -> dump RTS0 root signature (v1.0/1.1) parameters"""
import sys, struct
def get_rts0(path):
    b = open(path,'rb').read(); n = struct.unpack_from('<I', b, 0x1C)[0]
    for i in range(n):
        off = struct.unpack_from('<I', b, 0x20+4*i)[0]
        if b[off:off+4] == b'RTS0': return b[off+8:off+8+struct.unpack_from('<I', b, off+4)[0]]
def parse(r):
    ver, np_, po, ns, so, fl = struct.unpack_from('<6I', r, 0)
    ps = []
    for k in range(np_):
        t, vis, off = struct.unpack_from('<3I', r, po+12*k)
        p = dict(i=k, type={0:'TABLE',1:'CONST32',2:'CBV',3:'SRV',4:'UAV'}[t], vis=vis)
        if t == 0:
            nr, ro = struct.unpack_from('<2I', r, off); rs = []
            for j in range(nr):
                if ver == 1: rt, nd, br, sp, oi = struct.unpack_from('<5I', r, ro+20*j); rs.append(dict(t='SRV UAV CBV SAMP'.split()[rt] if rt<4 else rt, n=nd, reg=br, space=sp, off=oi))
                else: rt, nd, br, sp, fl2, oi = struct.unpack_from('<6I', r, ro+24*j); rs.append(dict(t='SRV UAV CBV SAMP'.split()[rt], n=nd, reg=br, space=sp, off=oi))
            p['ranges'] = rs
        elif t == 1: p['reg'], p['space'], p['n'] = struct.unpack_from('<3I', r, off)
        else: p['reg'], p['space'] = struct.unpack_from('<2I', r, off)[:2]
        ps.append(p)
    return ver, fl, ps
if __name__ == '__main__':
    r = get_rts0(sys.argv[1]); ver, fl, ps = parse(r)
    print('version', ver, 'flags', hex(fl))
    for p in ps: print(p)
