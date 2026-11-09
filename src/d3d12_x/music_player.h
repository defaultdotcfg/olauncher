#pragma once
// ---------------------------------------------------------------------------
// Player's own playlist, played in the game process while the game music is held
// at 0 (see DetourSetRtpc). The launcher passes:
//   ONRUSH_MUSIC_PLAYLIST = full path of a UTF-8 .m3u8 (one file path per line, '#' lines ignored)
//   ONRUSH_MUSIC_SHUFFLE  = 1 to shuffle
//   ONRUSH_MUSIC_VOLUME   = 0..100 (default 70)
// Playback uses the Media Foundation media engine (audio only): mp3, m4a/aac, wma,
// wav, flac and anything else Windows can decode.
// ---------------------------------------------------------------------------
#include <mfapi.h>
#include <mfmediaengine.h>
#include <random>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace music {

struct EngineNotify : IMFMediaEngineNotify {
  volatile LONG refs = 1;
  HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // track ended or failed
  volatile bool failed = false;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **pp) override {
    if (id == __uuidof(IUnknown) || id == __uuidof(IMFMediaEngineNotify)) { *pp = this; AddRef(); return S_OK; }
    *pp = nullptr; return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
  ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&refs); if (!r) { CloseHandle(done); delete this; } return r; }
  HRESULT STDMETHODCALLTYPE EventNotify(DWORD ev, DWORD_PTR, DWORD) override {
    if (ev == MF_MEDIA_ENGINE_EVENT_ENDED) SetEvent(done);
    else if (ev == MF_MEDIA_ENGINE_EVENT_ERROR) { failed = true; SetEvent(done); }
    return S_OK;
  }
};

static std::vector<std::wstring> LoadPlaylist(const wchar_t *path) {
  std::vector<std::wstring> out;
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return out;
  DWORD size = GetFileSize(f, nullptr), got = 0;
  std::string data(size, '\0');
  ReadFile(f, data.data(), size, &got, nullptr);
  CloseHandle(f);
  size_t pos = data.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
  while (pos < data.size()) {
    size_t e = data.find('\n', pos);
    std::string line = data.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
    pos = e == std::string::npos ? data.size() : e + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    int n = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, w.data(), n);
    if (GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES) out.push_back(w);
  }
  return out;
}

static std::vector<std::wstring> g_tracks;

static DWORD WINAPI PlayerThread(void *) {
  wchar_t path[MAX_PATH] = {}, v[16] = {};
  GetEnvironmentVariableW(L"ONRUSH_MUSIC_PLAYLIST", path, MAX_PATH);
  bool shuffle = GetEnvironmentVariableW(L"ONRUSH_MUSIC_SHUFFLE", v, 16) && v[0] == L'1';
  double volume = GetEnvironmentVariableW(L"ONRUSH_MUSIC_VOLUME", v, 16) ? _wtoi(v) / 100.0 : 0.7;
  if (volume < 0) volume = 0; if (volume > 1) volume = 1;
  std::vector<std::wstring> &tracks = g_tracks;
  log_line("[music] playlist '%ls': %u tracks, shuffle=%d, volume=%.2f\r\n", path, (unsigned)tracks.size(), shuffle ? 1 : 0, volume);
  if (tracks.empty()) return 0;

  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) { log_line("[music] MFStartup failed\r\n"); return 0; }
  IMFMediaEngineClassFactory *factory = nullptr;
  IMFAttributes *attr = nullptr;
  IMFMediaEngine *engine = nullptr;
  EngineNotify *notify = new EngineNotify();
  HRESULT hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  if (SUCCEEDED(hr)) hr = MFCreateAttributes(&attr, 2);
  if (SUCCEEDED(hr)) hr = attr->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify);
  if (SUCCEEDED(hr)) attr->SetUINT32(MF_MEDIA_ENGINE_AUDIO_CATEGORY, 7 /* AudioCategory_GameMedia */);
  if (SUCCEEDED(hr)) hr = factory->CreateInstance(MF_MEDIA_ENGINE_AUDIOONLY, attr, &engine);
  if (FAILED(hr) || !engine) { log_line("[music] media engine creation failed (0x%08X)\r\n", (unsigned)hr); return 0; }
  engine->SetVolume(volume);

  std::vector<size_t> order(tracks.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::mt19937 rng((unsigned)GetTickCount64());
  int failsInRow = 0;
  for (size_t k = 0;; ++k) {
    size_t i = k % order.size();
    if (i == 0 && shuffle) std::shuffle(order.begin(), order.end(), rng);
    const std::wstring &t = tracks[order[i]];
    BSTR url = SysAllocString(t.c_str());
    notify->failed = false;
    ResetEvent(notify->done);
    engine->SetSource(url);
    SysFreeString(url);
    engine->Play();
    log_line("[music] playing %ls\r\n", t.c_str());
    WaitForSingleObject(notify->done, INFINITE);
    if (notify->failed) {
      log_line("[music] could not play %ls\r\n", t.c_str());
      if (++failsInRow >= (int)tracks.size()) { log_line("[music] no playable track; stopping\r\n"); break; }
    } else {
      failsInRow = 0;
    }
  }
  engine->Shutdown(); engine->Release(); attr->Release(); factory->Release(); notify->Release();
  return 0;
}

// Called once at startup. Returns true when the player's own playlist is active.
static bool Start() {
  wchar_t path[MAX_PATH] = {};
  if (!GetEnvironmentVariableW(L"ONRUSH_MUSIC_PLAYLIST", path, MAX_PATH)) return false;
  g_tracks = LoadPlaylist(path);
  if (g_tracks.empty()) { log_line("[music] playlist '%ls' has no playable files; game music kept\r\n", path); return false; }
  HANDLE h = CreateThread(nullptr, 0, PlayerThread, nullptr, 0, nullptr);
  if (h) CloseHandle(h);
  return true;
}

}  // namespace music
