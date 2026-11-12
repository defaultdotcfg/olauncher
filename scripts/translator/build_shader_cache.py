"""build_shader_cache.py [shaders_dir] -> translate every unique dumped VS/PS/CS (shaders_dir/pso_*_*.dxbc)
to shaders_dir/translated/<fnv1a64(dxbc)>.hlsl. Only shaders that translate with no TODO (and, unless NOCHECK=1,
compile) are written. Prints a summary.
As a module: build(shaders_dir, skip_existing=True, skip=set(), progress=cb) -> (stats, failed_hashes)."""
import sys, os, re, glob, collections, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gcn2hlsl as T, hlslc

PATCHES = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shader_patches.json')

def _patches():
    return json.load(open(PATCHES, encoding='utf-8')) if os.path.exists(PATCHES) else {}

def apply_patches(hx, txt, patches=None):
    """shader_patches.json: {"<hash>": [["exact old text", "new text"], ...]} applied after translation (temporary workarounds)."""
    if os.environ.get('NO_SHADER_PATCHES'): return txt
    for old, new in (patches if patches is not None else _patches()).get(hx, []):
        if old in txt: txt = txt.replace(old, new) + f"\n// patched: {old[:60]!r}\n"
    return txt

def fnv64(b):
    h = 0xcbf29ce484222325
    for c in b: h = ((h ^ c) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

def build(d, skip_existing=False, skip=(), progress=None, cancelled=lambda: False, pattern='pso_*_*.dxbc', changed_only=False):
    """Translates the dumps in d. skip_existing: leave hashes that already have a .hlsl; skip: hashes not to try.
    progress(done, total) is called per dump. Returns (Counter of results, set of failed hashes, Counter of reasons)."""
    patches = _patches()
    sif, stub = set(patches.get('_scalar_if', [])), set(patches.get('_stub_image', []))
    out = os.path.join(d, 'translated'); os.makedirs(out, exist_ok=True)
    files = sorted(glob.glob(os.path.join(d, pattern)))
    seen = set(); stat = collections.Counter(); reasons = collections.Counter(); failed = set()
    for i, f in enumerate(files):
        if progress: progress(i, len(files))
        if cancelled(): break
        kind = 'vs' if f.endswith('_vs.dxbc') else 'ps' if f.endswith('_ps.dxbc') else 'cs' if f.endswith('_cs.dxbc') else None
        if not kind: continue
        # current bridge dumps are named pso_h<fnv1a64>_xx.dxbc: no need to read/hash the ones already done
        m = re.match(r'pso_h([0-9a-f]{16})_', os.path.basename(f))
        if m and skip_existing and (m.group(1) in skip or os.path.exists(os.path.join(out, m.group(1) + '.hlsl'))):
            stat['skipped' if m.group(1) in skip else 'existing'] += 1; seen.add(int(m.group(1), 16)); continue
        raw = open(f, 'rb').read(); h = int(m.group(1), 16) if m else fnv64(raw); hx = f"{h:016x}"
        if h in seen: continue
        seen.add(h)
        if hx in skip: stat['skipped'] += 1; continue
        if skip_existing and os.path.exists(os.path.join(out, hx + '.hlsl')): stat['existing'] += 1; continue
        T.SIF_FORCE = hx in sif
        if hx in stub: os.environ['STUB_IMAGE'] = '1'
        else: os.environ.pop('STUB_IMAGE', None)
        try:
            txt, todo = T.translate(f)
        except Exception as e:
            stat[kind + ' fail'] += 1; failed.add(hx); reasons[f"{kind}: {type(e).__name__}: {str(e)[:80]}"] += 1; continue
        if todo:
            stat[kind + ' todo'] += 1; failed.add(hx)
            for _, t in todo[:1]: reasons[f"{kind}: {t.split('!!')[-1].strip()[:80]}"] += 1
            continue
        ok, msg = (True, '') if os.environ.get('NOCHECK') else hlslc.compile_hlsl(txt.encode(), f"{kind}_5_1")
        if not ok and 'gradient' in msg:
            T.LOD0 = True
            T.SIF_FORCE = hx in sif
            try: txt, todo = T.translate(f)
            finally: T.LOD0 = False
            ok, msg = hlslc.compile_hlsl(txt.encode(), f"{kind}_5_1")
        if not ok:
            stat[kind + ' compile'] += 1; failed.add(hx); reasons[f"{kind}: compile: {msg.splitlines()[0][-80:] if msg else ''}"] += 1; continue
        txt = apply_patches(hx, txt, patches)
        dst = os.path.join(out, hx + '.hlsl')
        body = f"// {os.path.basename(f)} fnv={hx} kind={kind}\n" + txt
        if changed_only and os.path.exists(dst):  # translator revision bump: keep (and do not recompile) unchanged shaders
            with open(dst, encoding='utf-8') as fh:
                if fh.read() == body: stat['unchanged'] += 1; continue
        with open(dst + '.tmp', 'w', encoding='utf-8') as fh: fh.write(body)
        os.replace(dst + '.tmp', dst)
        stat[kind + ' ok'] += 1
    os.environ.pop('STUB_IMAGE', None)
    if progress: progress(len(files), len(files))
    return stat, failed, reasons

if __name__ == '__main__':
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', '..', 'dist_game', 'shaders')
    stat, failed, reasons = build(d)
    print(dict(stat))
    for r, n in reasons.most_common(15): print(n, r)
