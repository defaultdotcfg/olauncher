"""In-process DXC: compiles HLSL with dxcompiler.dll (IDxcCompiler3 over COM, through ctypes) instead of starting
dxc.exe for every shader. Same arguments as the dxc.exe command line, so the output is byte-identical.
compile(src_bytes, target, args) -> (bytes or None, error text). One compiler instance per process (per thread)."""
import ctypes, os, sys, threading
from ctypes import wintypes as W


class GUID(ctypes.Structure):
    _fields_ = [('a', ctypes.c_uint32), ('b', ctypes.c_uint16), ('c', ctypes.c_uint16), ('d', ctypes.c_ubyte * 8)]

    def __init__(self, s):
        h = s.replace('-', '')
        super().__init__(int(h[0:8], 16), int(h[8:12], 16), int(h[12:16], 16), (ctypes.c_ubyte * 8)(*bytes.fromhex(h[16:])))


class DxcBuffer(ctypes.Structure):
    _fields_ = [('Ptr', ctypes.c_void_p), ('Size', ctypes.c_size_t), ('Encoding', ctypes.c_uint32)]


CLSID_DxcCompiler = GUID('73e22d93-e6ce-47f3-b5bf-f0664f39c1b0')
IID_IDxcCompiler3 = GUID('228b4687-5a6a-4730-900c-9702b2203f54')
IID_IDxcOperationResult = GUID('cedb484a-d4e9-445a-b991-ca21ca157dc2')
DXC_CP_UTF8 = 65001


def _method(obj, index, restype, *argtypes):
    vtbl = ctypes.cast(obj, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    return ctypes.WINFUNCTYPE(restype, ctypes.c_void_p, *argtypes)(vtbl[index])


def _release(obj):
    if obj: _method(obj, 2, ctypes.c_ulong)(obj)


def _blob_bytes(blob):
    if not blob: return b''
    p = _method(blob, 3, ctypes.c_void_p)(blob)
    n = _method(blob, 4, ctypes.c_size_t)(blob)
    return ctypes.string_at(p, n) if p and n else b''


_dll = None
_tls = threading.local()


def load(dll_path):
    """dxcompiler.dll (dxil.dll must sit next to it: dxcompiler signs the DXIL with it)."""
    global _dll
    if _dll is None:
        d = os.path.dirname(os.path.abspath(dll_path))
        if hasattr(os, 'add_dll_directory'): os.add_dll_directory(d)
        _dll = ctypes.WinDLL(dll_path)
        _dll.DxcCreateInstance.argtypes = [ctypes.POINTER(GUID), ctypes.POINTER(GUID), ctypes.POINTER(ctypes.c_void_p)]
        _dll.DxcCreateInstance.restype = ctypes.c_long
    return _dll


def _compiler():
    c = getattr(_tls, 'c', None)
    if c is None:
        p = ctypes.c_void_p()
        hr = _dll.DxcCreateInstance(ctypes.byref(CLSID_DxcCompiler), ctypes.byref(IID_IDxcCompiler3), ctypes.byref(p))
        if hr < 0 or not p: raise OSError('DxcCreateInstance failed 0x%08x' % (hr & 0xFFFFFFFF))
        c = _tls.c = p.value
    return c


def compile(src, target, args=(), name='s.hlsl'):
    """src: HLSL bytes (UTF-8). args: extra dxc arguments, as on the command line (e.g. '-O3', '-HV', '2018').
    name: source file name the compiler sees (dxc.exe was given s.hlsl)."""
    comp = _compiler()
    keep = ctypes.create_string_buffer(src, len(src))   # stays alive until Compile returns
    buf = DxcBuffer(ctypes.cast(keep, ctypes.c_void_p), len(src), DXC_CP_UTF8)
    argv = [name, '-T', target, '-E', 'main'] + list(args)
    arr = (ctypes.c_wchar_p * len(argv))(*argv)
    res = ctypes.c_void_p()
    hr = _method(comp, 3, ctypes.c_long, ctypes.POINTER(DxcBuffer), ctypes.POINTER(ctypes.c_wchar_p), ctypes.c_uint32,
                 ctypes.c_void_p, ctypes.POINTER(GUID), ctypes.POINTER(ctypes.c_void_p))(
        comp, ctypes.byref(buf), arr, len(argv), None, ctypes.byref(IID_IDxcOperationResult), ctypes.byref(res))
    if hr < 0 or not res: return None, 'IDxcCompiler3::Compile failed 0x%08x' % (hr & 0xFFFFFFFF)
    r = res.value
    try:
        status = ctypes.c_long()
        _method(r, 3, ctypes.c_long, ctypes.POINTER(ctypes.c_long))(r, ctypes.byref(status))
        err = ctypes.c_void_p()
        _method(r, 5, ctypes.c_long, ctypes.POINTER(ctypes.c_void_p))(r, ctypes.byref(err))
        msg = _blob_bytes(err.value).decode('utf-8', 'replace'); _release(err.value)
        if status.value < 0: return None, msg
        out = ctypes.c_void_p()
        _method(r, 4, ctypes.c_long, ctypes.POINTER(ctypes.c_void_p))(r, ctypes.byref(out))
        data = _blob_bytes(out.value); _release(out.value)
        return (data or None), msg
    finally:
        _release(r)
