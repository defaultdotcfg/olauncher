"""compile_cso.py [translated_dir] -> compile every <hash>.hlsl to <hash>.cso (parallel). Target from the '// ... kind=xx' header.
Every shader is compiled by dxc to DXIL (shader model 6.0). The bridge already ran every graphics pipeline with the
DXIL builds (the '.dxil.cso' twins, so that a depth prepass and its G-buffer pass get bit-identical positions), and fxc
took up to a minute on some compute shaders where dxc takes a fraction of a second.
<hash>.unopt markers left by older versions (compute shaders that fxc compiled without optimizations) are rebuilt."""
import sys, os, re, glob
from concurrent.futures import ProcessPoolExecutor, as_completed
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# translate_all.py: the launcher creates this file when the player presses "Pular"
CANCEL_FLAG = 'cancel.flag'


def _write(path, data):
    """Atomic write: a cancelled or killed run never leaves a truncated .cso behind."""
    tmp = path + '.%d.tmp' % os.getpid()
    with open(tmp, 'wb') as fh: fh.write(data)
    os.replace(tmp, path)


def _newer(a, b):
    """True if a exists and is at least as new as b."""
    return os.path.exists(a) and os.path.getmtime(a) >= os.path.getmtime(b)


def _kind(f):
    with open(f, 'rb') as fh: m = re.search(rb'kind=(vs|ps|cs)', fh.read(300))
    return m.group(1).decode() if m else None


def needs_work(f):
    if not _kind(f): return False  # tess_*.hlsl: compiled by tess_bypass.py
    if os.environ.get('FORCE'): return True
    return not _newer(f[:-5] + '.cso', f) or os.path.exists(f[:-5] + '.unopt')


def compile_one(f):
    """<hash>.hlsl -> <hash>.cso. Returns 'ok', 'FAIL' or 'skip'."""
    import hlslc
    src = open(f, 'rb').read()
    m = re.search(rb'kind=(vs|ps|cs)', src[:300])
    if not m: return 'skip'
    data, _ = hlslc.dxc_compile(src, m.group(1).decode())
    if data is None: return 'FAIL'
    _write(f[:-5] + '.cso', data)
    if os.path.exists(f[:-5] + '.unopt'): os.remove(f[:-5] + '.unopt')
    return 'ok'


def work(f):
    if os.path.exists(os.path.join(os.path.dirname(f), CANCEL_FLAG)): return f, 'cancel'
    return f, compile_one(f)


def workers(n, per_worker=200 << 20, cap=16):
    """Parallel processes: one per logical core, at most `cap`, and `per_worker` bytes of free RAM each.
    Measured on 16 threads (13360 jobs): 12 workers 200 s, 16 workers 171 s; a worker peaks near 85 MB."""
    import ctypes
    class MS(ctypes.Structure):
        _fields_ = [('len', ctypes.c_ulong), ('load', ctypes.c_ulong)] + [(k, ctypes.c_ulonglong) for k in
                   ('total', 'avail', 'tpage', 'apage', 'tvirt', 'avirt', 'aext')]
    if os.environ.get('TRANSLATOR_WORKERS'): return max(1, min(int(os.environ['TRANSLATOR_WORKERS']), n))   # measurements
    ms = MS(); ms.len = ctypes.sizeof(MS)
    ram = ms.avail if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(ms)) else 4 << 30
    return max(1, min(cap, os.cpu_count() or 4, int(ram // per_worker), n))


def run(d, progress=None, log=print, cancelled=lambda: False):
    """Compiles every .hlsl in d that has no up-to-date .cso. .hlsl whose compile failed are removed.
    Returns (Counter, list of removed hashes)."""
    import collections
    fs = sorted(glob.glob(os.path.join(d, '*.hlsl')))
    todo = [f for f in fs if needs_work(f)]
    c = collections.Counter(skip=len(fs) - len(todo))
    if todo and not cancelled():
        with ProcessPoolExecutor(workers(len(todo))) as p:
            order = sorted(todo, key=lambda f: -os.path.getsize(f))  # big ones first, so they overlap with the rest
            for i, fut in enumerate(as_completed([p.submit(work, f) for f in order])):
                f, r = fut.result()
                c[r] += 1
                if progress: progress(i + 1, len(todo))
    removed = []
    if cancelled(): return c, removed
    for f in todo:
        if not os.path.basename(f).startswith('tess_') and not os.path.exists(f[:-5] + '.cso'):
            os.remove(f); removed.append(os.path.basename(f)[:-5]); log('removed (compile failed): ' + os.path.basename(f))
    return c, removed


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    d = args[0] if args else os.path.join(os.path.dirname(__file__), '..', '..', 'dist_game', 'shaders', 'translated')
    c, _ = run(d)
    print(dict(c))
