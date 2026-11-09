"""build_shader_cache.py [shaders_dir] -> translate every unique dumped VS/PS to dist_game/shaders/translated/<fnv1a64(dxbc)>.hlsl
Only shaders that translate with no TODO and compile are written. Prints a summary."""
import sys, os, glob, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gcn2hlsl as T, hlslc, json
def _load_sif():
    pf = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shader_patches.json')
    return set(json.load(open(pf)).get('_scalar_if', [])) if os.path.exists(pf) else set()
_SIF = _load_sif()
def _load_list(key):
    pf = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shader_patches.json')
    return set(json.load(open(pf)).get(key, [])) if os.path.exists(pf) else set()
_STUB = _load_list('_stub_image')  # shaders whose unresolvable image_sample (dead path) may return 0
def apply_patches(hx, txt):
    """shader_patches.json: {"<hash>": [["exact old text", "new text"], ...]} applied after translation (temporary workarounds)."""
    pf = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shader_patches.json')
    if not os.path.exists(pf) or os.environ.get('NO_SHADER_PATCHES'): return txt
    for old, new in json.load(open(pf)).get(hx, []):
        if old in txt: txt = txt.replace(old, new) + f"\n// patched: {old[:60]!r}\n"
    return txt
def fnv64(b):
    h = 0xcbf29ce484222325
    for c in b: h = ((h ^ c) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h
d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', '..', 'dist_game', 'shaders')
out = os.path.join(d, 'translated'); os.makedirs(out, exist_ok=True)
seen = {}; stat = collections.Counter(); reasons = collections.Counter()
for f in sorted(glob.glob(os.path.join(d, 'pso_*_*.dxbc'))):
    kind = 'vs' if f.endswith('_vs.dxbc') else 'ps' if f.endswith('_ps.dxbc') else 'cs' if f.endswith('_cs.dxbc') else None
    if not kind: continue
    raw = open(f, 'rb').read(); h = fnv64(raw)
    if h in seen: continue
    seen[h] = f
    T.SIF_FORCE = f"{h:016x}" in _SIF
    if f"{h:016x}" in _STUB: os.environ['STUB_IMAGE'] = '1'
    else: os.environ.pop('STUB_IMAGE', None)
    try:
        txt, todo = T.translate(f)
    except Exception as e:
        stat[kind + ' fail'] += 1; reasons[f"{kind}: {type(e).__name__}: {str(e)[:80]}"] += 1; continue
    if todo:
        stat[kind + ' todo'] += 1
        for _, t in todo[:1]: reasons[f"{kind}: {t.split('!!')[-1].strip()[:80]}"] += 1
        continue
    ok, msg = (True, '') if os.environ.get('NOCHECK') else hlslc.compile_hlsl(txt.encode(), f"{kind}_5_1")
    if not ok and 'gradient' in msg:
        T.LOD0 = True
        T.SIF_FORCE = f"{h:016x}" in _SIF
        try: txt, todo = T.translate(f)
        finally: T.LOD0 = False
        ok, msg = hlslc.compile_hlsl(txt.encode(), f"{kind}_5_1")
    if not ok:
        stat[kind + ' compile'] += 1; reasons[f"{kind}: compile: {msg.splitlines()[0][-80:] if msg else ''}"] += 1; continue
    txt = apply_patches(f"{h:016x}", txt)
    open(os.path.join(out, f"{h:016x}.hlsl"), 'w').write(f"// {os.path.basename(f)} fnv={h:016x} kind={kind}\n" + txt)
    stat[kind + ' ok'] += 1
print(dict(stat))
for r, n in reasons.most_common(15): print(n, r)
