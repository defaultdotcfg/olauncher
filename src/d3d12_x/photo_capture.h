#pragma once
// Photo export (photo mode or anywhere): press F12 (ONRUSH_PHOTO_KEY = virtual-key code) and the next frame is
// saved at the output resolution chosen in the launcher, without the HUD / photo-mode menus.
//
// How: the final post-process shader (ceda6ed547dba75d, see shader_patches.json) mixes the UI layer into the
// image. While g_CtrBuf[0x80000] holds a non-zero token it also stores its colour BEFORE the UI mix at
// g_CtrBuf[1MB + (y*4096 + x)*4] and echoes the token at [0x80010]. The token is copied into g_CtrBuf by the
// per-frame counter reset (ResetCountersOnQueue -> PhotoTokenCopy), so exactly one frame carries it:
//   present P   : key -> token = T            (frame P+1 renders with T)
//   present P+1 : token = 0
//   present P+2 : read back echo + pixels on the same queue; echo == T -> clean image, else the backbuffer
//                 (with HUD) is used. Encoding (PNG / JPG) runs on a worker thread.
// Output: <exe dir>\fotos\OnRush_YYYY-MM-DD_HH-MM-SS.png|jpg (ONRUSH_PHOTO_DIR overrides the folder).
// Format: ONRUSH_PHOTO_FORMAT=png (default, lossless) | jpg; ONRUSH_PHOTO_JPGQ = JPEG quality (default 92).
#include "photo_encode.h"

static volatile LONG g_photoToken = 0;   // value copied into g_CtrBuf[0x80000] each frame
static ID3D12Resource* g_photoUpload = nullptr; static uint8_t* g_photoUploadPtr = nullptr;

static void PhotoTokenCopy(ID3D12GraphicsCommandList* l, UINT slot) {
  if (!l || !g_CtrBuf || !g_pMainDevice) return;
  if (!g_photoUpload) {
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd = {}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = 256; bd.Height = 1; bd.DepthOrArraySize = 1;
    bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(g_pMainDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                       __uuidof(ID3D12Resource), (void**)&g_photoUpload)) || !g_photoUpload) return;
    D3D12_RANGE r = { 0, 0 };
    if (FAILED(g_photoUpload->Map(0, &r, (void**)&g_photoUploadPtr))) { g_photoUploadPtr = nullptr; return; }
  }
  if (!g_photoUploadPtr) return;
  const UINT off = (slot & 3) * 16;   // one 16-byte slot per counter-reset list (they are fenced 4 deep)
  uint32_t v[4] = { (uint32_t)g_photoToken, 0, 0, 0 };
  memcpy(g_photoUploadPtr + off, v, 16);
  l->CopyBufferRegion(g_CtrBuf, 0x80000, g_photoUpload, off, 16);
}

static void PhotoSetTitle(const wchar_t* t) { if (g_desktopHwnd && IsWindow(g_desktopHwnd)) SetWindowTextW(g_desktopHwnd, t); }

struct PhotoJob { std::vector<uint8_t> rgb; int w, h; bool clean; };
static volatile LONG g_photoBusy = 0;
static wchar_t g_photoTitleRestore[256] = {}; static ULONGLONG g_photoTitleUntil = 0; static wchar_t g_photoTitleMsg[256] = {};

static DWORD WINAPI PhotoEncodeThread(void* p) {
  PhotoJob* job = (PhotoJob*)p;
  char fmt[16] = "png"; GetEnvironmentVariableA("ONRUSH_PHOTO_FORMAT", fmt, sizeof(fmt));
  const bool jpg = _stricmp(fmt, "jpg") == 0 || _stricmp(fmt, "jpeg") == 0;
  char qb[16] = {}; int q = GetEnvironmentVariableA("ONRUSH_PHOTO_JPGQ", qb, sizeof(qb)) > 0 ? atoi(qb) : 92;
  std::vector<uint8_t> out;
  const ULONGLONG t0 = GetTickCount64();
  const bool ok = jpg ? photoenc::PhotoEncodeJpg(job->rgb.data(), job->w, job->h, q, out) : photoenc::PhotoEncodePng(job->rgb.data(), job->w, job->h, out);
  wchar_t dir[MAX_PATH] = {};
  if (GetEnvironmentVariableW(L"ONRUSH_PHOTO_DIR", dir, MAX_PATH) == 0) {
    GetModuleFileNameW(nullptr, dir, MAX_PATH); wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    wcscat_s(dir, L"\\fotos");
  }
  CreateDirectoryW(dir, nullptr);
  SYSTEMTIME st; GetLocalTime(&st);
  wchar_t path[MAX_PATH]; const wchar_t* ext = jpg ? L"jpg" : L"png";
  swprintf_s(path, L"%s\\OnRush_%04u-%02u-%02u_%02u-%02u-%02u.%s", dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ext);
  for (int n = 2; GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES && n < 100; ++n)
    swprintf_s(path, L"%s\\OnRush_%04u-%02u-%02u_%02u-%02u-%02u_%d.%s", dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, n, ext);
  bool written = false;
  if (ok) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) { DWORD wr = 0; written = WriteFile(f, out.data(), (DWORD)out.size(), &wr, nullptr) && wr == out.size(); CloseHandle(f); }
  }
  log_line("[photo] %s %dx%d %s -> %ls (%zu bytes, %llu ms)\r\n", written ? "saved" : "FAILED", job->w, job->h,
           job->clean ? "clean (no HUD)" : "backbuffer (with HUD)", path, out.size(), (unsigned long long)(GetTickCount64() - t0));
  const wchar_t* name = wcsrchr(path, L'\\');
  swprintf_s(g_photoTitleMsg, L"OnRush  -  %s %s", written ? L"foto salva:" : L"falha ao salvar a foto", written && name ? name + 1 : L"");
  g_photoTitleUntil = GetTickCount64() + 4000;
  delete job; InterlockedExchange(&g_photoBusy, 0);
  return 0;
}

// Copies `bytes` from `src` (buffer at srcOff, or texture subresource 0 when tex) into a new readback buffer on the
// game's direct queue and waits. Returns the mapped copy in `out`.
static bool PhotoReadback(ID3D12Resource* src, bool tex, UINT64 srcOff, UINT64 bytes, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* fp,
                          D3D12_RESOURCE_STATES texState, std::vector<uint8_t>& out) {
  ID3D12CommandQueue* q = g_LastDirectQueue ? g_LastDirectQueue : g_pDirectCommandQueue;
  if (!src || !q || !g_pMainDevice) return false;
  D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC bd = {}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = bytes; bd.Height = 1; bd.DepthOrArraySize = 1;
  bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ID3D12Resource* rb = nullptr; ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList* cl = nullptr; ID3D12Fence* fe = nullptr;
  bool ok = SUCCEEDED(g_pMainDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&rb)) &&
            SUCCEEDED(g_pMainDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&al)) &&
            SUCCEEDED(g_pMainDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&cl)) &&
            SUCCEEDED(g_pMainDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fe));
  if (ok) {
    if (tex) {
      D3D12_RESOURCE_BARRIER b = {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = src;
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; b.Transition.StateBefore = texState; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      if (texState != D3D12_RESOURCE_STATE_COPY_SOURCE) cl->ResourceBarrier(1, &b);
      D3D12_TEXTURE_COPY_LOCATION s = {}; s.pResource = src; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      D3D12_TEXTURE_COPY_LOCATION d = {}; d.pResource = rb; d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; d.PlacedFootprint = *fp;
      cl->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
      std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
      if (texState != D3D12_RESOURCE_STATE_COPY_SOURCE) cl->ResourceBarrier(1, &b);
    } else cl->CopyBufferRegion(rb, 0, src, srcOff, bytes);
    ok = SUCCEEDED(cl->Close());
  }
  if (ok) {
    ID3D12CommandList* l[] = { cl }; q->ExecuteCommandLists(1, l); q->Signal(fe, 1);
    HANDLE ev = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (fe->GetCompletedValue() < 1) { fe->SetEventOnCompletion(1, ev); ok = WaitForSingleObject(ev, 3000) == WAIT_OBJECT_0; }
    CloseHandle(ev);
  }
  if (ok) {
    void* p = nullptr; D3D12_RANGE r = { 0, (SIZE_T)bytes };
    ok = SUCCEEDED(rb->Map(0, &r, &p)) && p;
    if (ok) { out.assign((uint8_t*)p, (uint8_t*)p + bytes); D3D12_RANGE nw = { 0, 0 }; rb->Unmap(0, &nw); }
  }
  if (cl) cl->Release(); if (al) al->Release(); if (fe) fe->Release(); if (rb) rb->Release();
  return ok;
}

// Called at the top of every Present (before the per-frame counter reset).
static void Photo_OnPresent(ID3D12Resource* backBuffer) {
  static int s_state = 0; static LONG s_token = 0; static bool s_prevDown = false; static int s_key = -1;
  if (s_key < 0) { char kb[16] = {}; s_key = GetEnvironmentVariableA("ONRUSH_PHOTO_KEY", kb, sizeof(kb)) > 0 ? (int)strtol(kb, nullptr, 0) : VK_F12; if (s_key <= 0) s_key = VK_F12; }
  // window title feedback ("foto salva: ...") for a few seconds
  if (g_photoTitleUntil) {
    if (g_photoTitleMsg[0]) { if (!g_photoTitleRestore[0] && g_desktopHwnd) GetWindowTextW(g_desktopHwnd, g_photoTitleRestore, 256); PhotoSetTitle(g_photoTitleMsg); g_photoTitleMsg[0] = 0; }
    if (GetTickCount64() > g_photoTitleUntil) { if (g_photoTitleRestore[0]) PhotoSetTitle(g_photoTitleRestore); g_photoTitleRestore[0] = 0; g_photoTitleUntil = 0; }
  }
  const bool down = GameWindowFocused() && (GetAsyncKeyState(s_key) & 0x8000) != 0;
  bool pressed = down && !s_prevDown; s_prevDown = down;
  { static char s_at[256] = {}; static int s_atInit = 0;   // ONRUSH_PHOTO_AT=f1,f2: automatic shots (tests)
    if (!s_atInit) s_atInit = GetEnvironmentVariableA("ONRUSH_PHOTO_AT", s_at, sizeof(s_at)) > 0 ? 1 : 2;
    if (s_atInit == 1) { char fb[24]; sprintf_s(fb, ",%ld,", (long)g_rtLogFrame); char lst[260]; sprintf_s(lst, ",%s,", s_at); if (strstr(lst, fb)) pressed = true; } }
  if (s_state == 0) {
    if (pressed && !g_photoBusy && g_CtrBuf) {
      static LONG s_n = 0; s_token = (LONG)(0x50480000u | ((uint32_t)++s_n & 0xFFFF));
      InterlockedExchange(&g_photoToken, s_token); s_state = 1;
      log_line("[photo] requested (token %08lX)\r\n", (unsigned long)s_token);
    }
    return;
  }
  if (s_state == 1) { InterlockedExchange(&g_photoToken, 0); s_state = 2; return; }
  s_state = 0;
  D3D12_RESOURCE_DESC bd = {};
  if (!backBuffer || !SafeGetResourceDesc(backBuffer, &bd)) return;
  const int w = (int)bd.Width, h = (int)bd.Height;
  PhotoJob* job = new PhotoJob(); job->w = w; job->h = h; job->clean = false; job->rgb.resize((size_t)w * h * 3);
  std::vector<uint8_t> echo;
  if (w <= 4096 && h <= 2304 && PhotoReadback(g_CtrBuf, false, 0x80010, 16, nullptr, D3D12_RESOURCE_STATE_COMMON, echo) &&
      echo.size() >= 4 && *(const uint32_t*)echo.data() == (uint32_t)s_token) {
    std::vector<uint8_t> px;
    if (PhotoReadback(g_CtrBuf, false, 0x100000, (UINT64)h * 4096 * 4, nullptr, D3D12_RESOURCE_STATE_COMMON, px)) {
      for (int y = 0; y < h; ++y) { const uint8_t* s = px.data() + (size_t)y * 4096 * 4; uint8_t* d = job->rgb.data() + (size_t)y * w * 3;
        for (int x = 0; x < w; ++x) { d[x * 3] = s[x * 4]; d[x * 3 + 1] = s[x * 4 + 1]; d[x * 3 + 2] = s[x * 4 + 2]; } }
      job->clean = true;
    }
  }
  if (!job->clean) {   // the post-process pass did not run that frame (menus, loading): take the backbuffer
    const int f = (int)bd.Format;
    const bool rgba = f >= DXGI_FORMAT_R8G8B8A8_TYPELESS && f <= DXGI_FORMAT_R8G8B8A8_SINT, bgra = f >= DXGI_FORMAT_B8G8R8A8_UNORM && f <= DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    if (!rgba && !bgra) { log_line("[photo] backbuffer format %d not supported\r\n", f); delete job; return; }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {}; fp.Footprint.Format = rgba ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    if (f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) fp.Footprint.Format = (DXGI_FORMAT)f;
    fp.Footprint.Width = w; fp.Footprint.Height = h; fp.Footprint.Depth = 1; fp.Footprint.RowPitch = (w * 4 + 255) & ~255;
    std::vector<uint8_t> px;
    if (!PhotoReadback(backBuffer, true, 0, (UINT64)fp.Footprint.RowPitch * h, &fp, GetTrackedResourceState(backBuffer, D3D12_RESOURCE_STATE_COMMON), px)) {
      log_line("[photo] backbuffer readback failed\r\n"); delete job; return; }
    for (int y = 0; y < h; ++y) { const uint8_t* s = px.data() + (size_t)y * fp.Footprint.RowPitch; uint8_t* d = job->rgb.data() + (size_t)y * w * 3;
      for (int x = 0; x < w; ++x) { d[x * 3] = s[x * 4 + (rgba ? 0 : 2)]; d[x * 3 + 1] = s[x * 4 + 1]; d[x * 3 + 2] = s[x * 4 + (rgba ? 2 : 0)]; } }
  }
  InterlockedExchange(&g_photoBusy, 1);
  HANDLE th = CreateThread(nullptr, 0, PhotoEncodeThread, job, 0, nullptr);
  if (th) { SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(th); } else { delete job; InterlockedExchange(&g_photoBusy, 0); }
}
