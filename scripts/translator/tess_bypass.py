"""tess_bypass.py [game_dir]  -- tessellation bypass for the terrain PSOs (VS -> HS -> DS on the Xbox).

The bridge cannot run the Xbox hull/domain shaders, so those PSOs were not drawn (distant map vanishing).
For every dumped pair shaders/tess/vs<V>_ds<D>.dxbc this builds ONE vertex shader that runs, per vertex:
  LS program of the VS blob (GCN_PROG=<n>, LS_AS_FN=1)  -> writes the control point to static cp_[32]
  domain shader (DS_AS_VS=1) at domain coords (0,0)     -> reads cp_ / pc_ and exports pos + params
so every mesh vertex becomes the evaluated surface point at that vertex (no extra subdivision, but the
displacement / projection of the DS is applied). Patch constant dword 1 = CP0 dword 24 (what the HS writes),
the other patch constants (tess factors) = 1.0.
Output: shaders/translated/tess_<V>_<D>.hlsl + .dxil.cso (SM6: the DS uses wave intrinsics).
The bridge uses it for PSOs with HS+DS (HS/DS dropped, topology TRIANGLE, patch list 3 -> triangle list).
"""
import sys, os, re, glob, subprocess, struct, zlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gcn_dis as G
import hlslc
SM6 = b'// sm6' + bytes([10])

def programs(path):
    b = open(path, 'rb').read()
    n = struct.unpack_from('<I', b, 0x1C)[0]
    shex = None
    for k in range(n):
        off = struct.unpack_from('<I', b, 0x20 + 4 * k)[0]
        if b[off:off + 4] in (b'SHEX', b'SHDR'): shex = (off + 8, struct.unpack_from('<I', b, off + 4)[0])
    off, size = shex; d = b[off:off + size]; i = 2; out = []
    while i * 4 < size:
        t = struct.unpack_from('<I', d, i * 4)[0]
        if (t & 0x7FF) == 0x35:
            L = struct.unpack_from('<I', d, (i + 1) * 4)[0]; blob = d[(i + 2) * 4:(i + L) * 4]; i += L
            for o in range(0, min(40, len(blob) - 2)):
                if blob[o] == 0x78 and blob[o + 1] in (1, 0x5e, 0x9c, 0xda):
                    try: dec = zlib.decompressobj().decompress(blob[o:])
                    except Exception: continue
                    if b'\x00\x00\x81\xbf' in dec: out.append(dec)
                    break
            continue
        i += ((t >> 24) & 0x7F) or 1
    return out

def ls_index(path):
    for k, dec in enumerate(programs(path)):
        s, e = G.find_code(dec); txt = '\n'.join(G.disasm(dec[s:e]))
        if 'ds_write' in txt and ' exp ' not in txt and '\texp ' not in txt and 'exp pos' not in txt and 'exp param' not in txt:
            return k
    return None

def hs_pc1(ds_path):
    """The HS ends with ds_read_b32 vR <- CP0 byte X ... ds_write_b32 patch-const byte 4 <- vR: returns X/4."""
    hs = glob.glob(re.sub(r'_hs[0-9a-f]{16}\.dxbc$', '_hs*.dxbc', ds_path))
    if len(hs) != 1: return None
    L = G.disasm((lambda b: b[slice(*G.find_code(b))])(G.extract_code_from_dxbc(hs[0])))
    src = {}
    for ln in L:
        m = re.search(r'ds_read_b32 vdst:v(\d+) addr:v\d+ .*off0:(\d+) off1:(\d+)', ln)
        if m: src[int(m.group(1))] = (int(m.group(2)) | (int(m.group(3)) << 8)) // 4; continue
        m = re.search(r'ds_write_b32 vdst:v\d+ addr:v\d+ data0:v(\d+) .*off0:4 off1:0', ln)
        if m and int(m.group(1)) in src: return src[int(m.group(1))]
    return None

def translate(src, env):
    out = os.path.join(os.environ.get('TEMP', '.'), 'tess_tmp_%d.hlsl' % os.getpid())
    e = dict(os.environ); e.update(env)
    # packaged translator (PyInstaller): the exe itself runs gcn2hlsl with --gcn2hlsl
    cmd = [sys.executable, '--gcn2hlsl', src, out] if getattr(sys, 'frozen', False) else [sys.executable, '-I', os.path.join(HERE, 'gcn2hlsl.py'), src, out]
    r = subprocess.run(cmd, env=e, capture_output=True, text=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if not os.path.exists(out): raise RuntimeError('translate failed: ' + (r.stderr or r.stdout)[-400:])
    t = open(out, encoding='utf-8').read(); os.remove(out)
    if '// TODO' in t: raise RuntimeError('TODO left: ' + ' | '.join(l.strip() for l in t.splitlines() if '// TODO' in l)[:400])
    return t

def split(t):
    """-> (global lines before the output struct, struct text, main signature params, main body text)"""
    a = t.index('struct VSOut {'); b = t.index('};', a) + 2
    m = re.search(r'VSOut main\((.*?)\) \{', t[b:])
    glob_ = [l for l in t[:a].splitlines() if l.strip()] + [l for l in t[b:b + m.start()].splitlines() if l.strip()]
    return glob_, t[a:b], m.group(1), t[b + m.end():]

DECL = re.compile(r'^(\w[\w<> ]*?) (\w+)\[(\d+)\] : (register\(.*\));$')
def merge_globals(*lists):
    out, seen, arr = [], set(), {}
    for L in lists:
        for l in L:
            if l.startswith('// generated') or l.startswith('// pso_h'): continue
            m = DECL.match(l)
            if m:
                ty, nm, n, reg = m.groups()
                if nm in arr:
                    j, oty, on, oreg = arr[nm]
                    if (ty, reg) != (oty, oreg): raise RuntimeError('decl conflict ' + nm)
                    if int(n) > on: out[j] = l; arr[nm] = (j, ty, int(n), reg)
                    continue
                arr[nm] = (len(out), ty, int(n), reg); out.append(l); continue
            if l in seen: continue
            seen.add(l); out.append(l)
    return out

def build(game, vs_h, ds_path):
    vs_dx = os.path.join(game, 'shaders', 'pso_h%s_vs.dxbc' % vs_h)
    if not os.path.exists(vs_dx): raise RuntimeError('no VS dump ' + vs_dx)
    k = ls_index(vs_dx)
    if k is None: raise RuntimeError('no LS program in ' + vs_dx)
    ls = translate(vs_dx, {'GCN_PROG': str(k), 'LS_AS_FN': '1'})
    ds = translate(ds_path, {'DS_AS_VS': '1'})
    pc1 = hs_pc1(ds_path.replace('_ds', '_hs', 1))
    lg, lst, lpar, lbody = split(ls)
    dg, dst, dpar, dbody = split(ds)
    if 'cp_[' not in lbody: raise RuntimeError('LS writes no control point')
    args = ', '.join(re.match(r'\s*\w+ (\w+)', p).group(1) for p in lpar.split(','))
    g = merge_globals(lg, dg)
    sm6 = any('Wave' in l or 'Quad' in l for l in g) or 'Wave' in dbody or 'Wave' in lbody
    src = ['// tess bypass: LS(prog %d) of VS %s + DS %s at domain (0,0)' % (k, vs_h, os.path.basename(ds_path))]
    src += g
    src.append('static uint cp_[64];')
    src.append('static uint pc_[16];')
    src.append(dst)
    lb = lbody[:lbody.index('  VSOut o;')]   # the LS exports nothing: drop the output block
    lb = '\n'.join(l for l in lb.splitlines() if not re.fullmatch(r'\s*(precise )?float4 (param\d+|pos\d)_o = 0;', l))
    if re.search(r'\b(param\d+|pos\d)_o\b', lb): raise RuntimeError('LS writes exports')
    src.append('void ls_main(' + lpar + ') {' + lb + '\n}')
    src.append('VSOut ds_main(uint vid) {' + dbody)
    src.append('VSOut main(' + lpar + ') {')
    src.append('  [unroll] for (uint k = 0; k < 64; ++k) cp_[k] = 0u;')
    src.append('  ls_main(' + args + ');')
    src.append('  [unroll] for (uint j = 0; j < 16; ++j) pc_[j] = asuint(1.0);')
    if pc1 is not None: src.append('  pc_[1] = cp_[%d];   // HS: patch constant dword 1 = control point 0 dword %d' % (pc1, pc1))
    src.append('  return ds_main(0u);')
    src.append('}')
    text = '\n'.join(src) + '\n'
    return text, sm6

def pairs(game):
    """[(vs_hash, ds_hash, ds_path)] of every dumped pair."""
    out = []
    for ds_path in sorted(glob.glob(os.path.join(game, 'shaders', 'tess', 'vs*_ds*.dxbc'))):
        m = re.search(r'vs([0-9a-f]{16})_ds([0-9a-f]{16})\.dxbc$', ds_path)
        if m: out.append((m.group(1), m.group(2), ds_path))
    return out


def build_pair(game, vs_h, ds_h, ds_path, changed_only=False):
    """Builds shaders/translated/tess_<vs>_<ds>.hlsl + .dxil.cso. Returns 'ok', 'unchanged' or raises."""
    base = os.path.join(game, 'shaders', 'translated', 'tess_%s_%s' % (vs_h, ds_h))
    text, sm6 = build(game, vs_h, ds_path)
    if changed_only and os.path.exists(base + '.dxil.cso') and os.path.exists(base + '.hlsl'):
        if open(base + '.hlsl', encoding='utf-8').read() == text: return 'unchanged'
    open(base + '.hlsl', 'w', encoding='utf-8').write(text)
    d, msg = hlslc._dxc(SM6 + text.encode(), 'vs_6_0')
    if not d: raise RuntimeError('dxc compile failed: ' + msg[-300:])
    tmp = base + '.dxil.cso.%d.tmp' % os.getpid()
    with open(tmp, 'wb') as fh: fh.write(d)
    os.replace(tmp, base + '.dxil.cso')
    return 'ok'


def run(game, skip_existing=False, log=print, cancelled=lambda: False, changed_only=False):
    """changed_only: retranslate everything but recompile only when the generated HLSL differs (translator revision bump)."""
    os.makedirs(os.path.join(game, 'shaders', 'translated'), exist_ok=True)
    ok = fail = 0
    for vs_h, ds_h, ds_path in pairs(game):
        if skip_existing and os.path.exists(os.path.join(game, 'shaders', 'translated', 'tess_%s_%s.dxil.cso' % (vs_h, ds_h))): continue
        if cancelled(): break
        try:
            if build_pair(game, vs_h, ds_h, ds_path, changed_only) == 'ok': log('ok   %s %s' % (vs_h, ds_h)); ok += 1
        except Exception as e:
            log('FAIL %s %s %s' % (vs_h, ds_h, str(e)[:300])); fail += 1
    log('done ok=%d fail=%d' % (ok, fail))
    return ok, fail

def main():
    run(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'dist_game'))

if __name__ == '__main__':
    main()
