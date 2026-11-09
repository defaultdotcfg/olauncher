"""compile_cso.py [translated_dir] [--fast] [--optimize] -> compile every <hash>.hlsl to <hash>.cso (parallel).
Target from the '// ... kind=xx' header.
--fast     : the compute shaders listed in shader_patches.json "_cs_fast" (fxc takes 3-60 s on each) are compiled
             without fxc optimizations and marked with <hash>.unopt. They work, but the driver takes longer to
             build their pipelines, so the launcher optimizes them later.
--optimize : recompile the marked ones with optimizations (the launcher does it after the game closes)."""
import sys, os, re, glob
from concurrent.futures import ProcessPoolExecutor, as_completed
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# translate_all.py: the launcher creates this file when the player presses "Pular"
CANCEL_FLAG = 'cancel.flag'
SKIP_OPTIMIZATION = 1 << 2  # D3DCOMPILE_SKIP_OPTIMIZATION
_FAST = None


def _fast_set():
    global _FAST
    if _FAST is None:
        import json
        p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shader_patches.json')
        try: _FAST = set(json.load(open(p, encoding='utf-8')).get('_cs_fast', []))
        except Exception: _FAST = set()
    return _FAST


def _write(path, data):
    """Atomic write: a cancelled or killed run never leaves a truncated .cso behind."""
    tmp = path + '.tmp'
    with open(tmp, 'wb') as fh: fh.write(data)
    os.replace(tmp, path)


def _newer(a, b):
    """True if a exists and is at least as new as b."""
    return os.path.exists(a) and os.path.getmtime(a) >= os.path.getmtime(b)


def _kind(f):
    with open(f, 'rb') as fh: m = re.search(rb'kind=(vs|ps|cs)', fh.read(300))
    return m.group(1).decode() if m else None


def needs_work(f, optimize=False):
    kind = _kind(f)
    if not kind: return False  # tess_*.hlsl: compiled by tess_bypass.py
    if optimize: return os.path.exists(f[:-5] + '.unopt')
    if os.environ.get('FORCE'): return True
    if not _newer(f[:-5] + '.cso', f): return True
    if kind in ('vs', 'ps'):
        with open(f, 'rb') as fh: sm6 = b'// sm6' in fh.read()
        if not sm6 and not _newer(f[:-5] + '.dxil.cso', f): return True
    return False


def work(f, fast=False, optimize=False):
    import hlslc
    if os.path.exists(os.path.join(os.path.dirname(f), CANCEL_FLAG)): return f, 'cancel'
    src = open(f, 'rb').read()
    m = re.search(rb'kind=(vs|ps|cs)', src[:300])
    if not m: return f, 'skip'  # tess_*.hlsl: compiled by tess_bypass.py
    kind = m.group(1).decode()
    out = f[:-5] + '.cso'
    unopt = f[:-5] + '.unopt'
    flags = int(os.environ.get('HLSL_FLAGS', '0'))
    if optimize:
        data = hlslc.compile_to_bytes(src, kind + '_5_1', flags)
        if data is None: return f, 'FAIL'  # keep the unoptimized .cso
        _write(out, data)
        os.remove(unopt)
        return f, 'ok'
    if kind in ('vs', 'ps') and b'// sm6' not in src:
        # DXIL twin so the bridge can pair this VS with an SM6 (DXIL) pixel shader in one PSO
        alt = f[:-5] + '.dxil.cso'
        if not _newer(alt, f) or os.environ.get('FORCE'):
            d2 = hlslc.compile_to_bytes(b'// sm6\n' + src, kind + '_6_0')
            if d2: _write(alt, d2)
    if _newer(out, f) and not os.environ.get('FORCE'): return f, 'skip'
    fast = fast and kind == 'cs' and os.path.basename(f)[:-5] in _fast_set()
    data = hlslc.compile_to_bytes(src, kind + '_5_1', flags | (SKIP_OPTIMIZATION if fast else 0))
    if data is None: return f, 'FAIL'
    _write(out, data)
    if fast: open(unopt, 'wb').close()
    elif os.path.exists(unopt): os.remove(unopt)
    return f, 'ok'


def _workers(n):
    """Parallel fxc processes: one per core (minus one), at most 8, and ~400 MB of free RAM each."""
    import ctypes
    class MS(ctypes.Structure):
        _fields_ = [('len', ctypes.c_ulong), ('load', ctypes.c_ulong)] + [(k, ctypes.c_ulonglong) for k in
                   ('total', 'avail', 'tpage', 'apage', 'tvirt', 'avirt', 'aext')]
    ms = MS(); ms.len = ctypes.sizeof(MS)
    ram = ms.avail if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(ms)) else 4 << 30
    return max(1, min(8, (os.cpu_count() or 4) - 1, int(ram // (400 << 20)), n))


def run(d, progress=None, log=print, cancelled=lambda: False, fast=False, optimize=False):
    """Compiles every .hlsl in d (skips up-to-date ones). .hlsl whose compile failed are removed.
    Returns (Counter, list of removed hashes)."""
    import collections
    fs = sorted(glob.glob(os.path.join(d, '*.hlsl')))
    todo = [f for f in fs if needs_work(f, optimize)]
    c = collections.Counter(skip=len(fs) - len(todo))
    if todo and not cancelled():
        # processes, not threads: D3DCompiler_47 serializes concurrent calls inside one process
        with ProcessPoolExecutor(_workers(len(todo))) as p:
            # slowest first (compute shaders), so the long ones overlap with everything else
            order = sorted(todo, key=lambda f: (_kind(f) != 'cs', -os.path.getsize(f)))
            for i, fut in enumerate(as_completed([p.submit(work, f, fast, optimize) for f in order])):
                f, r = fut.result()
                c[r] += 1
                if progress: progress(i + 1, len(todo))
    removed = []
    if cancelled() or optimize: return c, removed
    for f in todo:
        if not os.path.basename(f).startswith('tess_') and not os.path.exists(f[:-5] + '.cso'):
            os.remove(f); removed.append(os.path.basename(f)[:-5]); log('removed (compile failed): ' + os.path.basename(f))
    return c, removed


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    d = args[0] if args else os.path.join(os.path.dirname(__file__), '..', '..', 'dist_game', 'shaders', 'translated')
    c, _ = run(d, fast='--fast' in sys.argv, optimize='--optimize' in sys.argv)
    print(dict(c))
