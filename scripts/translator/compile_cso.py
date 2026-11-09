"""compile_cso.py [translated_dir] -> compile every <hash>.hlsl to <hash>.cso (parallel). Target from the '// ... kind=xx' header."""
import sys, os, re, glob
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
def work(f):
    import hlslc
    src = open(f, 'rb').read()
    m = re.search(rb'kind=(vs|ps|cs)', src[:300])
    if not m: return f, 'skip'  # tess_*.hlsl: compiled by tess_bypass.py
    kind = m.group(1).decode()
    out = f[:-5] + '.cso'
    if kind in ('vs', 'ps') and b'// sm6' not in src:
        # DXIL twin so the bridge can pair this VS with an SM6 (DXIL) pixel shader in one PSO
        alt = f[:-5] + '.dxil.cso'
        if not os.path.exists(alt) or os.path.getmtime(alt) < os.path.getmtime(f) or os.environ.get('FORCE'):
            d2 = hlslc.compile_to_bytes(b'// sm6\n' + src, kind + '_6_0')
            if d2: open(alt, 'wb').write(d2)
    if os.path.exists(out) and os.path.getmtime(out) >= os.path.getmtime(f) and not os.environ.get('FORCE'): return f, 'skip'
    data = hlslc.compile_to_bytes(src, kind + '_5_1', int(os.environ.get('HLSL_FLAGS', '0')))
    if data is None: return f, 'FAIL'
    open(out, 'wb').write(data); return f, 'ok'
if __name__ == '__main__':
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', '..', 'dist_game', 'shaders', 'translated')
    fs = sorted(glob.glob(os.path.join(d, '*.hlsl')))
    import collections
    c = collections.Counter()
    with ThreadPoolExecutor(8) as p:
        for f, r in p.map(work, fs): c[r] += 1
    print(dict(c))
    for f in fs:
        if not os.path.basename(f).startswith('tess_') and not os.path.exists(f[:-5] + '.cso'): os.remove(f); print('removed (compile failed):', os.path.basename(f))
