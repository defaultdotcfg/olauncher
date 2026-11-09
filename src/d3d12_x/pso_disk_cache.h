// In-game side of the pipeline disk cache (format: src/common/pso_cache_format.h).
// Translated PSOs are looked up in an ID3D12PipelineLibrary loaded from shaders\pso_cache\library.bin;
// misses are created normally, stored in the library and appended to manifest.bin so the launcher can
// rebuild the library (with a progress popup) when the driver changes. A background thread re-serializes
// the library a few seconds after new pipelines appear. Disable with ONRUSH_NO_PSO_CACHE=1.
#pragma once
#include "../common/pso_cache_format.h"
#include <unordered_map>
#include <unordered_set>

namespace psodc {

// Root signature object -> the exact serialized blob it was created from.
static std::unordered_map<void *, std::vector<uint8_t>> g_rsBlobs;
static SRWLOCK g_rsLock = SRWLOCK_INIT;
// Translated shader blob -> where it came from (only file-backed blobs can be rebuilt by the launcher).
static std::unordered_map<const void *, psocache::ShaderRef> g_blobRefs;
static SRWLOCK g_blobLock = SRWLOCK_INIT;

static void NoteRootSignature(void *rs, const void *blob, size_t len) {
  if (!rs || !blob || !len) return;
  AcquireSRWLockExclusive(&g_rsLock);
  g_rsBlobs[rs].assign((const uint8_t *)blob, (const uint8_t *)blob + len);
  ReleaseSRWLockExclusive(&g_rsLock);
}
static bool RootSignatureBlob(void *rs, std::vector<uint8_t> &out) {
  AcquireSRWLockShared(&g_rsLock);
  auto it = g_rsBlobs.find(rs);
  bool ok = it != g_rsBlobs.end();
  if (ok) out = it->second;
  ReleaseSRWLockShared(&g_rsLock);
  return ok;
}
// Called when a translated blob is loaded from shaders\translated\<orig>.cso / .dxil.cso.
static void NoteShaderBlob(ID3DBlob *b, uint64_t orig, uint8_t variant) {
  if (!b) return;
  psocache::ShaderRef r;
  r.orig = orig; r.variant = variant; r.content = psocache::Fnv(b->GetBufferPointer(), b->GetBufferSize());
  AcquireSRWLockExclusive(&g_blobLock);
  g_blobRefs[b->GetBufferPointer()] = r;
  ReleaseSRWLockExclusive(&g_blobLock);
}
// Reference for the bytecode a desc points at; false if it is not a file-backed translated shader.
static bool ShaderRefFor(const D3D12_SHADER_BYTECODE &bc, psocache::ShaderRef &out) {
  out = {};
  if (!bc.pShaderBytecode || !bc.BytecodeLength) return true;  // stage unused
  AcquireSRWLockShared(&g_blobLock);
  auto it = g_blobRefs.find(bc.pShaderBytecode);
  bool ok = it != g_blobRefs.end();
  if (ok) out = it->second;
  ReleaseSRWLockShared(&g_blobLock);
  return ok;
}

// ---- library ----
static void **g_origDevVtbl = nullptr;  // desktop D3D12 device vtable (set where the bridge wraps the device)
static SRWLOCK g_libLock = SRWLOCK_INIT;
static ID3D12PipelineLibrary *g_lib = nullptr;
static std::vector<uint8_t> g_libData;           // must outlive g_lib
static std::unordered_set<uint64_t> g_manifestKeys;
static long long g_manifestBytes = 0;
static bool g_dirty = false, g_inited = false, g_disabled = false;
static volatile LONG g_hits = 0, g_misses = 0, g_uncacheable = 0;
static volatile LONG64 g_createUs = 0, g_loadUs = 0;

static const char *kDir = "shaders\\pso_cache";
static const char *kManifest = "shaders\\pso_cache\\manifest.bin";
static const char *kLibrary = "shaders\\pso_cache\\library.bin";
static const char *kState = "shaders\\pso_cache\\state.txt";

static bool ReadFileAll(const char *path, std::vector<uint8_t> &out) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? (size_t)n : 0);
  size_t got = n > 0 ? fread(out.data(), 1, (size_t)n, f) : 0;
  fclose(f);
  return got == out.size();
}

static void LoadManifestKeys() {
  std::vector<uint8_t> m;
  if (!ReadFileAll(kManifest, m) || m.size() < 8 || memcmp(m.data(), psocache::kManifestMagic, 8) != 0) {
    FILE *f = fopen(kManifest, "wb");
    if (f) { fwrite(psocache::kManifestMagic, 1, 8, f); fclose(f); }
    g_manifestBytes = 8;
    return;
  }
  size_t o = 8;
  while (o + 16 <= m.size()) {
    uint32_t mg, sz; uint64_t key;
    memcpy(&mg, &m[o], 4); memcpy(&key, &m[o + 4], 8); memcpy(&sz, &m[o + 12], 4);
    if (mg != psocache::kRecMagic || o + 16 + sz > m.size()) break;
    g_manifestKeys.insert(key);
    o += 16 + sz;
  }
  g_manifestBytes = (long long)o;
  if (o != m.size()) {  // torn tail (game killed mid-write): drop it
    FILE *f = fopen(kManifest, "wb");
    if (f) { fwrite(m.data(), 1, o, f); fclose(f); }
  }
}

static DWORD WINAPI SaverThread(void *) {
  for (;;) {
    Sleep(4000);
    AcquireSRWLockExclusive(&g_libLock);
    if (g_dirty && g_lib) {
      SIZE_T n = g_lib->GetSerializedSize();
      std::vector<uint8_t> buf(n);
      HRESULT hr = n ? g_lib->Serialize(buf.data(), n) : E_FAIL;
      long long covered = g_manifestBytes;
      g_dirty = false;
      ReleaseSRWLockExclusive(&g_libLock);
      if (SUCCEEDED(hr)) {
        char tmp[MAX_PATH]; sprintf_s(tmp, "%s.tmp", kLibrary);
        FILE *f = fopen(tmp, "wb");
        if (f) {
          bool ok = fwrite(buf.data(), 1, n, f) == n;
          fclose(f);
          if (ok && MoveFileExA(tmp, kLibrary, MOVEFILE_REPLACE_EXISTING)) {
            FILE *s = fopen(kState, "wb");
            if (s) { fprintf(s, "%lld\n", covered); fclose(s); }
          }
        }
        log_line("[psocache] library saved: %zu KB, hits=%ld misses=%ld uncacheable=%ld create=%lldms load=%lldms\r\n",
                 (size_t)(n / 1024), g_hits, g_misses, g_uncacheable, (long long)(g_createUs / 1000), (long long)(g_loadUs / 1000));
      } else {
        log_line("[psocache] Serialize failed hr=0x%08lX\r\n", (unsigned long)hr);
      }
      continue;
    }
    ReleaseSRWLockExclusive(&g_libLock);
  }
}

// Lazily opens the library on the game's device. Returns false when the cache is off/unavailable.
static bool EnsureInit(ID3D12Device *dev) {
  if (g_inited) return !g_disabled;
  AcquireSRWLockExclusive(&g_libLock);
  if (!g_inited) {
    g_inited = true;
    g_disabled = GetEnvironmentVariableA("ONRUSH_NO_PSO_CACHE", nullptr, 0) > 0;
    // The game's device object carries the bridge's Xbox-layout vtable, so ID3D12Device1::CreatePipelineLibrary
    // (desktop slot 44) must be called through the original D3D12 vtable, not via a C++ virtual call.
    typedef HRESULT(STDMETHODCALLTYPE * PFN_CreatePipelineLibrary)(ID3D12Device *, const void *, SIZE_T, REFIID, void **);
    PFN_CreatePipelineLibrary createLib = g_origDevVtbl ? (PFN_CreatePipelineLibrary)g_origDevVtbl[44] : nullptr;
    if (!dev || !createLib) g_disabled = true;
    if (!g_disabled) {
      CreateDirectoryA("shaders", nullptr);
      CreateDirectoryA(kDir, nullptr);
      LoadManifestKeys();
      ReadFileAll(kLibrary, g_libData);
      HRESULT hr = g_libData.empty() ? E_FAIL
                                     : createLib(dev, g_libData.data(), g_libData.size(), __uuidof(ID3D12PipelineLibrary), (void **)&g_lib);
      if (FAILED(hr)) {
        if (!g_libData.empty()) log_line("[psocache] library.bin rejected (hr=0x%08lX, driver changed?) -> starting empty\r\n", (unsigned long)hr);
        g_libData.clear();
        g_lib = nullptr;
        hr = createLib(dev, nullptr, 0, __uuidof(ID3D12PipelineLibrary), (void **)&g_lib);
      }
      if (FAILED(hr) || !g_lib) { log_line("[psocache] CreatePipelineLibrary failed hr=0x%08lX -> cache off\r\n", (unsigned long)hr); g_disabled = true; }
      else {
        log_line("[psocache] library ready (%zu KB from disk, %zu manifest records)\r\n", g_libData.size() / 1024, g_manifestKeys.size());
        CloseHandle(CreateThread(nullptr, 0, SaverThread, nullptr, 0, nullptr));
      }
    }
  }
  ReleaseSRWLockExclusive(&g_libLock);
  return !g_disabled;
}

static void MaybeLogStats() {
  LONG n = g_hits + g_misses;
  if (n > 0 && (n % 200) == 0)
    log_line("[psocache] stats: hits=%ld misses=%ld uncacheable=%ld create=%lldms load=%lldms\r\n", g_hits, g_misses, g_uncacheable,
             (long long)(g_createUs / 1000), (long long)(g_loadUs / 1000));
}

static long long NowUs() {
  static LARGE_INTEGER f = {};
  if (!f.QuadPart) QueryPerformanceFrequency(&f);
  LARGE_INTEGER c; QueryPerformanceCounter(&c);
  return (long long)(c.QuadPart * 1000000 / f.QuadPart);
}

// Store a freshly created PSO and record it in the manifest (caller holds no lock).
static void StoreNew(uint64_t key, const wchar_t *name, ID3D12PipelineState *pso, const std::vector<uint8_t> &payload) {
  AcquireSRWLockExclusive(&g_libLock);
  HRESULT hr = g_lib->StorePipeline(name, pso);
  if (SUCCEEDED(hr)) g_dirty = true;
  if (!g_manifestKeys.count(key)) {
    FILE *f = fopen(kManifest, "ab");
    if (f) {
      uint32_t mg = psocache::kRecMagic, sz = (uint32_t)payload.size();
      fwrite(&mg, 4, 1, f); fwrite(&key, 8, 1, f); fwrite(&sz, 4, 1, f); fwrite(payload.data(), 1, sz, f);
      fclose(f);
      g_manifestKeys.insert(key);
      g_manifestBytes += 16 + sz;
      g_dirty = true;
    }
  }
  ReleaseSRWLockExclusive(&g_libLock);
}

typedef HRESULT(STDMETHODCALLTYPE *PFN_TrueGfx)(ID3D12Device *, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *, REFIID, void **);
typedef HRESULT(STDMETHODCALLTYPE *PFN_TrueCs)(ID3D12Device *, const D3D12_COMPUTE_PIPELINE_STATE_DESC *, REFIID, void **);

static HRESULT CreateGraphics(PFN_TrueGfx trueFn, ID3D12Device *dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *d, REFIID riid, void **ppv) {
  psocache::ShaderRef vs, ps;
  std::vector<uint8_t> rs;
  bool cacheable = d && EnsureInit(dev) && !d->GS.pShaderBytecode && !d->HS.pShaderBytecode && !d->DS.pShaderBytecode &&
                   d->StreamOutput.NumEntries == 0 && !d->CachedPSO.pCachedBlob && d->pRootSignature &&
                   ShaderRefFor(d->VS, vs) && vs.variant != psocache::SH_NONE && ShaderRefFor(d->PS, ps) &&
                   RootSignatureBlob(d->pRootSignature, rs);
  if (!cacheable) { InterlockedIncrement(&g_uncacheable); return trueFn(dev, d, riid, ppv); }
  psocache::Writer w;
  psocache::WriteGfx(w, *d, rs, vs, ps);
  uint64_t key = psocache::Fnv(w.b.data(), w.b.size());
  wchar_t name[17]; psocache::KeyName(key, name);
  long long t0 = NowUs();
  AcquireSRWLockExclusive(&g_libLock);
  HRESULT hr = g_lib->LoadGraphicsPipeline(name, d, riid, ppv);
  ReleaseSRWLockExclusive(&g_libLock);
  if (SUCCEEDED(hr) && *ppv) { InterlockedIncrement(&g_hits); InterlockedAdd64(&g_loadUs, NowUs() - t0); MaybeLogStats(); return hr; }
  t0 = NowUs();
  hr = trueFn(dev, d, riid, ppv);
  InterlockedAdd64(&g_createUs, NowUs() - t0);
  InterlockedIncrement(&g_misses);
  MaybeLogStats();
  if (SUCCEEDED(hr) && *ppv) StoreNew(key, name, (ID3D12PipelineState *)*ppv, w.b);
  return hr;
}

static HRESULT CreateCompute(PFN_TrueCs trueFn, ID3D12Device *dev, const D3D12_COMPUTE_PIPELINE_STATE_DESC *d, REFIID riid, void **ppv) {
  psocache::ShaderRef cs;
  std::vector<uint8_t> rs;
  bool cacheable = d && EnsureInit(dev) && !d->CachedPSO.pCachedBlob && d->pRootSignature && ShaderRefFor(d->CS, cs) &&
                   cs.variant != psocache::SH_NONE && RootSignatureBlob(d->pRootSignature, rs);
  if (!cacheable) { InterlockedIncrement(&g_uncacheable); return trueFn(dev, d, riid, ppv); }
  psocache::Writer w;
  psocache::WriteCs(w, *d, rs, cs);
  uint64_t key = psocache::Fnv(w.b.data(), w.b.size());
  wchar_t name[17]; psocache::KeyName(key, name);
  long long t0 = NowUs();
  AcquireSRWLockExclusive(&g_libLock);
  HRESULT hr = g_lib->LoadComputePipeline(name, d, riid, ppv);
  ReleaseSRWLockExclusive(&g_libLock);
  if (SUCCEEDED(hr) && *ppv) { InterlockedIncrement(&g_hits); InterlockedAdd64(&g_loadUs, NowUs() - t0); MaybeLogStats(); return hr; }
  t0 = NowUs();
  hr = trueFn(dev, d, riid, ppv);
  InterlockedAdd64(&g_createUs, NowUs() - t0);
  InterlockedIncrement(&g_misses);
  MaybeLogStats();
  if (SUCCEEDED(hr) && *ppv) StoreNew(key, name, (ID3D12PipelineState *)*ppv, w.b);
  return hr;
}

}  // namespace psodc
