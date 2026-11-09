import ctypes
_d = ctypes.WinDLL(r"C:\Windows\System32\D3DCompiler_47.dll")
_d.D3DCompile.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_void_p)]
def compile_hlsl(src: bytes, target: str):
    blob = ctypes.c_void_p(); err = ctypes.c_void_p()
    hr = _d.D3DCompile(src, len(src), b"x", None, None, b"main", target.encode(), 0, 0, ctypes.byref(blob), ctypes.byref(err))
    msg = ''
    if err.value:
        vt = ctypes.cast(err, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p)))
        gp = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vt.contents[3]); gs = ctypes.CFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vt.contents[4])
        msg = ctypes.string_at(gp(err), gs(err)).decode(errors='replace')
    return (hr & 0xFFFFFFFF) == 0, msg

def compile_to_bytes(src: bytes, target: str, flags: int = 0):
    blob = ctypes.c_void_p(); err = ctypes.c_void_p()
    hr = _d.D3DCompile(src, len(src), b"x", None, None, b"main", target.encode(), flags, 0, ctypes.byref(blob), ctypes.byref(err))
    if (hr & 0xFFFFFFFF) != 0 or not blob.value: return None
    vt = ctypes.cast(blob, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p)))
    gp = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vt.contents[3]); gs = ctypes.CFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vt.contents[4])
    return ctypes.string_at(gp(blob), gs(blob))

# ---- SM6 (DXIL) path for shaders that use wave intrinsics: marked with "// sm6" by gcn2hlsl
import subprocess, tempfile, os as _os
def _find_dxc():
    """dxc.exe: $DXC, else the newest Windows SDK that has it, else PATH."""
    if _os.environ.get('DXC'): return _os.environ['DXC']
    import glob as _g
    for base in (r"C:\Program Files (x86)\Windows Kits\10\bin", r"C:\Program Files\Windows Kits\10\bin"):
        found = sorted(_g.glob(_os.path.join(base, '10.*', 'x64', 'dxc.exe')))
        if found: return found[-1]
    return 'dxc.exe'
_DXC = _find_dxc()
def _dxc(src: bytes, target: str):
    t6 = target.split('_')[0] + '_6_0'
    with tempfile.TemporaryDirectory() as td:
        fi = _os.path.join(td, 's.hlsl'); fo = _os.path.join(td, 's.cso')
        open(fi, 'wb').write(src)
        r = subprocess.run([_DXC, '-nologo', '-T', t6, '-E', 'main', '-O3', '-HV', '2018', '-Fo', fo, fi], capture_output=True, text=True)
        if r.returncode != 0 or not _os.path.exists(fo): return None, (r.stderr or r.stdout)[-2000:]
        return open(fo, 'rb').read(), ''
_fxc_compile_hlsl, _fxc_compile_to_bytes = compile_hlsl, compile_to_bytes
def compile_hlsl(src: bytes, target: str):
    if b'// sm6' in src:
        d, msg = _dxc(src, target); return (d is not None), msg
    return _fxc_compile_hlsl(src, target)
def compile_to_bytes(src: bytes, target: str, flags: int = 0):
    if b'// sm6' in src:
        d, msg = _dxc(src, target); return d
    return _fxc_compile_to_bytes(src, target, flags)
