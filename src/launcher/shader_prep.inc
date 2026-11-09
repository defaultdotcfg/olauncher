// ============================================================================
// "Preparando shaders" step that runs when JOGAR is pressed, before the game starts.
//  1. Compiles any translated shader (<game>\shaders\translated\*.hlsl) whose .cso is missing or older.
//  2. Rebuilds the pipeline disk cache (<game>\shaders\pso_cache, see src/common/pso_cache_format.h)
//     for every pipeline the game recorded in manifest.bin that the current library.bin does not have
//     (first run after a driver update, or pipelines the game created but never got to save).
// A popup with a progress bar appears if the work takes longer than a moment; "Pular" starts the game
// right away (whatever was already built is kept).
// ============================================================================
#include <d3d12.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "../common/pso_cache_format.h"

#define WM_SHADER_PREP_DONE (WM_USER + 120)
#define ID_PREP_SKIP        1030

struct ShaderPrepState
{
    wchar_t gameDir[MAX_PATH] = {};
    std::atomic<int> stage{ 0 };   // 0 = checking, 1 = compiling .cso, 2 = building pipelines
    std::atomic<int> done{ 0 }, total{ 0 };
    std::atomic<bool> cancel{ false }, finished{ false };
    int compiled = 0, compileFailed = 0, psoBuilt = 0, psoFailed = 0, psoCached = 0;
};

static ShaderPrepState *g_prep = nullptr;
static HWND g_hPrepWnd = nullptr;
static ULONGLONG g_prepStartTick = 0;

static bool ReadWholeFile(const wchar_t *path, std::vector<uint8_t> &out)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz = {};
    GetFileSizeEx(h, &sz);
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    BOOL ok = out.empty() || ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr);
    CloseHandle(h);
    return ok && got == out.size();
}

static bool WriteWholeFile(const wchar_t *path, const void *data, size_t n)
{
    wchar_t tmp[MAX_PATH];
    swprintf_s(tmp, MAX_PATH, L"%ls.tmp", path);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    BOOL ok = WriteFile(h, data, (DWORD)n, &w, nullptr) && w == n;
    CloseHandle(h);
    return ok && MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

// ---- step 1: translated HLSL -> .cso ----
typedef HRESULT(WINAPI *PFN_PrepD3DCompile)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

static void PrepCompileShaders(ShaderPrepState *st)
{
    wchar_t dir[MAX_PATH], pattern[MAX_PATH];
    swprintf_s(dir, MAX_PATH, L"%ls\\shaders\\translated", st->gameDir);
    swprintf_s(pattern, MAX_PATH, L"%ls\\*.hlsl", dir);
    std::vector<std::wstring> todo;
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(pattern, &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do
    {
        if (_wcsnicmp(fd.cFileName, L"tess_", 5) == 0) continue;  // built by tess_bypass.py (.dxil.cso), not here
        std::wstring hl = std::wstring(dir) + L"\\" + fd.cFileName;
        std::wstring cso = hl.substr(0, hl.size() - 5) + L".cso";
        WIN32_FILE_ATTRIBUTE_DATA ca;
        if (!GetFileAttributesExW(cso.c_str(), GetFileExInfoStandard, &ca) || CompareFileTime(&ca.ftLastWriteTime, &fd.ftLastWriteTime) < 0)
            todo.push_back(hl);
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
    if (todo.empty()) return;

    wchar_t dll[MAX_PATH];
    swprintf_s(dll, MAX_PATH, L"%ls\\D3DCompiler_47.dll", st->gameDir);
    HMODULE hc = LoadLibraryExW(dll, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hc) hc = LoadLibraryW(L"D3DCompiler_47.dll");
    PFN_PrepD3DCompile compile = hc ? (PFN_PrepD3DCompile)GetProcAddress(hc, "D3DCompile") : nullptr;
    if (!compile) { LogPrintf(L"[Shaders] D3DCompiler_47.dll não encontrado; %zu shader(s) ficam sem .cso\r\n", todo.size()); return; }

    st->stage = 1; st->done = 0; st->total = (int)todo.size();
    std::atomic<size_t> next{ 0 };
    std::mutex logLock;
    auto work = [&]() {
        for (size_t i; !st->cancel && (i = next++) < todo.size();)
        {
            std::vector<uint8_t> src;
            const std::wstring &hl = todo[i];
            bool ok = false;
            if (ReadWholeFile(hl.c_str(), src))
            {
                std::string head((const char *)src.data(), min(src.size(), (size_t)300));
                size_t k = head.find("kind=");
                std::string kind = k != std::string::npos ? head.substr(k + 5, 2) : "";
                if (kind == "vs" || kind == "ps" || kind == "cs")
                {
                    std::string target = kind + "_5_1";
                    ID3DBlob *blob = nullptr, *err = nullptr;
                    if (SUCCEEDED(compile(src.data(), src.size(), "translated.hlsl", nullptr, nullptr, "main", target.c_str(), 0, 0, &blob, &err)) && blob)
                    {
                        std::wstring cso = hl.substr(0, hl.size() - 5) + L".cso";
                        ok = WriteWholeFile(cso.c_str(), blob->GetBufferPointer(), blob->GetBufferSize());
                    }
                    if (blob) blob->Release();
                    if (err) err->Release();
                }
            }
            {
                std::lock_guard<std::mutex> g(logLock);
                if (ok) st->compiled++;
                else { st->compileFailed++; LogPrintf(L"[Shaders] falha ao compilar %ls\r\n", PathFindFileNameW(hl.c_str())); }
            }
            st->done++;
        }
    };
    unsigned n = max(1u, min(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> th;
    for (unsigned t = 0; t < n; ++t) th.emplace_back(work);
    for (auto &t : th) t.join();
}

// ---- step 2: pipeline library ----
typedef HRESULT(WINAPI *PFN_PrepCreateDevice)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);

struct PrepRecord { uint64_t key; std::vector<uint8_t> payload; };

static void PrepBuildPipelines(ShaderPrepState *st)
{
    wchar_t cacheDir[MAX_PATH], manifestPath[MAX_PATH], libPath[MAX_PATH], statePath[MAX_PATH];
    swprintf_s(cacheDir, MAX_PATH, L"%ls\\shaders\\pso_cache", st->gameDir);
    swprintf_s(manifestPath, MAX_PATH, L"%ls\\manifest.bin", cacheDir);
    swprintf_s(libPath, MAX_PATH, L"%ls\\library.bin", cacheDir);
    swprintf_s(statePath, MAX_PATH, L"%ls\\state.txt", cacheDir);

    std::vector<uint8_t> man;
    if (!ReadWholeFile(manifestPath, man) || man.size() <= 8 || memcmp(man.data(), psocache::kManifestMagic, 8) != 0)
        return;  // game never ran with the cache yet: it records pipelines itself on the first run
    std::vector<PrepRecord> recs;
    size_t o = 8;
    while (o + 16 <= man.size())
    {
        uint32_t mg, sz; uint64_t key;
        memcpy(&mg, &man[o], 4); memcpy(&key, &man[o + 4], 8); memcpy(&sz, &man[o + 12], 4);
        if (mg != psocache::kRecMagic || o + 16 + sz > man.size()) break;
        recs.push_back({ key, std::vector<uint8_t>(man.begin() + o + 16, man.begin() + o + 16 + sz) });
        o += 16 + sz;
    }
    const long long manifestBytes = (long long)o;

    HMODULE hd = LoadLibraryW(L"d3d12.dll");
    PFN_PrepCreateDevice createDevice = hd ? (PFN_PrepCreateDevice)GetProcAddress(hd, "D3D12CreateDevice") : nullptr;
    ID3D12Device1 *dev = nullptr;
    if (!createDevice || FAILED(createDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device1), (void **)&dev)) || !dev)
    {
        LogPrintf(L"[Shaders] não foi possível abrir o Direct3D 12 para preparar o cache de pipelines\r\n");
        return;
    }

    std::vector<uint8_t> libData;
    ID3D12PipelineLibrary *lib = nullptr;
    bool libValid = false;
    if (ReadWholeFile(libPath, libData) && !libData.empty() &&
        SUCCEEDED(dev->CreatePipelineLibrary(libData.data(), libData.size(), __uuidof(ID3D12PipelineLibrary), (void **)&lib)))
        libValid = true;
    else
    {
        if (!libData.empty()) LogPrintf(L"[Shaders] cache de pipelines é de outro driver/GPU: reconstruindo\r\n");
        libData.clear();
        if (FAILED(dev->CreatePipelineLibrary(nullptr, 0, __uuidof(ID3D12PipelineLibrary), (void **)&lib))) { dev->Release(); return; }
    }
    if (libValid)
    {
        std::vector<uint8_t> s;
        if (ReadWholeFile(statePath, s) && _atoi64(std::string(s.begin(), s.end()).c_str()) == manifestBytes)
        {   // library already covers every recorded pipeline
            st->psoCached = (int)recs.size();
            lib->Release(); dev->Release();
            return;
        }
    }

    st->stage = 2; st->done = 0; st->total = (int)recs.size();
    std::mutex lock;  // guards lib, the caches and the counters
    std::unordered_map<uint64_t, ID3D12RootSignature *> rootSigs;
    std::unordered_map<uint64_t, std::vector<uint8_t>> shaders;  // by content hash
    auto shaderBytes = [&](const psocache::ShaderRef &r, D3D12_SHADER_BYTECODE &bc) -> bool {
        bc = {};
        if (r.variant == psocache::SH_NONE) return true;
        {
            std::lock_guard<std::mutex> g(lock);
            auto it = shaders.find(r.content);
            if (it != shaders.end()) { bc = { it->second.data(), it->second.size() }; return !it->second.empty(); }
        }
        wchar_t rel[MAX_PATH], full[MAX_PATH];
        psocache::ShaderFile(r, rel, MAX_PATH);
        swprintf_s(full, MAX_PATH, L"%ls\\%ls", st->gameDir, rel);
        std::vector<uint8_t> b;
        if (!ReadWholeFile(full, b) || psocache::Fnv(b.data(), b.size()) != r.content) b.clear();  // shader changed since: stale record
        std::lock_guard<std::mutex> g(lock);
        auto &slot = shaders[r.content];
        if (slot.empty()) slot = std::move(b);
        bc = { slot.data(), slot.size() };
        return !slot.empty();
    };
    auto rootSig = [&](const std::vector<uint8_t> &blob) -> ID3D12RootSignature * {
        uint64_t h = psocache::Fnv(blob.data(), blob.size());
        std::lock_guard<std::mutex> g(lock);
        auto it = rootSigs.find(h);
        if (it != rootSigs.end()) return it->second;
        ID3D12RootSignature *rs = nullptr;
        if (FAILED(dev->CreateRootSignature(0, blob.data(), blob.size(), __uuidof(ID3D12RootSignature), (void **)&rs))) rs = nullptr;
        rootSigs[h] = rs;
        return rs;
    };

    std::atomic<size_t> next{ 0 };
    bool added = false;
    auto work = [&]() {
        for (size_t i; !st->cancel && (i = next++) < recs.size();)
        {
            const PrepRecord &rec = recs[i];
            wchar_t name[17];
            psocache::KeyName(rec.key, name);
            psocache::Reader rd(rec.payload.data(), rec.payload.size());
            uint8_t kind = rd.u8();
            ID3D12PipelineState *pso = nullptr;
            int result = 0;  // 0 failed/stale, 1 already cached, 2 built
            if (kind == psocache::KIND_GFX)
            {
                psocache::GfxRecord g;
                if (psocache::ReadGfx(rd, g) && (g.desc.pRootSignature = rootSig(g.rootSig)) != nullptr &&
                    shaderBytes(g.vs, g.desc.VS) && shaderBytes(g.ps, g.desc.PS))
                {
                    if (libValid) { std::lock_guard<std::mutex> l(lock); if (SUCCEEDED(lib->LoadGraphicsPipeline(name, &g.desc, __uuidof(ID3D12PipelineState), (void **)&pso))) result = 1; }
                    if (!result && SUCCEEDED(dev->CreateGraphicsPipelineState(&g.desc, __uuidof(ID3D12PipelineState), (void **)&pso))) result = 2;
                }
            }
            else if (kind == psocache::KIND_CS)
            {
                psocache::CsRecord c;
                if (psocache::ReadCs(rd, c) && (c.desc.pRootSignature = rootSig(c.rootSig)) != nullptr && shaderBytes(c.cs, c.desc.CS))
                {
                    if (libValid) { std::lock_guard<std::mutex> l(lock); if (SUCCEEDED(lib->LoadComputePipeline(name, &c.desc, __uuidof(ID3D12PipelineState), (void **)&pso))) result = 1; }
                    if (!result && SUCCEEDED(dev->CreateComputePipelineState(&c.desc, __uuidof(ID3D12PipelineState), (void **)&pso))) result = 2;
                }
            }
            {
                std::lock_guard<std::mutex> l(lock);
                if (result == 2 && SUCCEEDED(lib->StorePipeline(name, pso))) { added = true; st->psoBuilt++; }
                else if (result == 1) st->psoCached++;
                else st->psoFailed++;  // stale record (shader changed) or rejected pipeline
            }
            if (pso) pso->Release();
            st->done++;
        }
    };
    unsigned n = max(1u, std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 1 : 1u);
    std::vector<std::thread> th;
    for (unsigned t = 0; t < n; ++t) th.emplace_back(work);
    for (auto &t : th) t.join();

    if (added || !libValid)
    {
        SIZE_T sz = lib->GetSerializedSize();
        std::vector<uint8_t> out(sz);
        if (sz && SUCCEEDED(lib->Serialize(out.data(), sz)) && WriteWholeFile(libPath, out.data(), sz) && !st->cancel)
        {
            char s[32];
            int k = sprintf_s(s, "%lld\n", manifestBytes);
            WriteWholeFile(statePath, s, (size_t)k);
        }
    }
    else if (!st->cancel)
    {   // nothing new and nothing missing except stale records: mark as covered
        char s[32];
        int k = sprintf_s(s, "%lld\n", manifestBytes);
        WriteWholeFile(statePath, s, (size_t)k);
    }
    for (auto &kv : rootSigs) if (kv.second) kv.second->Release();
    lib->Release();
    dev->Release();
}

static DWORD WINAPI ShaderPrepThread(LPVOID p)
{
    ShaderPrepState *st = (ShaderPrepState *)p;
    ULONGLONG t0 = GetTickCount64();
    PrepCompileShaders(st);
    if (!st->cancel) PrepBuildPipelines(st);
    ULONGLONG ms = GetTickCount64() - t0;
    if (st->compiled || st->compileFailed || st->psoBuilt || st->psoFailed)
        LogPrintf(L"[Shaders] preparo em %.1f s: %d shader(s) compilado(s), %d falha(s); %d pipeline(s) montado(s), %d já no cache, %d ignorado(s)%ls\r\n",
                  ms / 1000.0, st->compiled, st->compileFailed, st->psoBuilt, st->psoCached, st->psoFailed, st->cancel ? L" (pulado)" : L"");
    else if (st->psoCached)
        LogPrintf(L"[Shaders] cache de pipelines em dia (%d pipelines)\r\n", st->psoCached);
    st->finished = true;
    return 0;
}

// ---- popup ----
static void PaintPrepWindow(HWND hwnd, HDC hdc)
{
    RECT rc; GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, g_hBrushCard);
    HBRUSH eb = CreateSolidBrush(COLOR_BORDER); FrameRect(hdc, &rc, eb); DeleteObject(eb);
    SetBkMode(hdc, TRANSPARENT);
    ShaderPrepState *st = g_prep;
    int stage = st ? st->stage.load() : 0, done = st ? st->done.load() : 0, total = st ? st->total.load() : 0;

    SelectObject(hdc, g_hFontSmallCaps); SetTextColor(hdc, COLOR_ACCENT_ORANGE);
    TextOutW(hdc, 24, 20, L"PREPARANDO SHADERS", 18);
    SelectObject(hdc, g_hFontUIBold); SetTextColor(hdc, COLOR_TEXT_PRIMARY);
    const wchar_t *what = stage == 1 ? L"Compilando shaders traduzidos" : stage == 2 ? L"Otimizando pipelines para a sua GPU" : L"Verificando o cache de shaders";
    TextOutW(hdc, 24, 42, what, (int)wcslen(what));

    RECT bar = { 24, 76, rc.right - 24, 86 };
    FillRound(hdc, bar, 10, RGB(48, 54, 64), RGB(48, 54, 64));
    if (total > 0 && done > 0)
    {
        RECT f = bar;
        f.right = bar.left + max(10L, (LONG)((bar.right - bar.left) * (long long)min(done, total) / total));
        FillRound(hdc, f, 10, COLOR_ACCENT_ORANGE, COLOR_ACCENT_ORANGE);
    }
    wchar_t t[128];
    if (total > 0) swprintf_s(t, L"%d de %d  ·  %d%%", min(done, total), total, (int)(100LL * min(done, total) / total));
    else wcscpy_s(t, L"Aguarde…");
    SelectObject(hdc, g_hFontUI); SetTextColor(hdc, COLOR_TEXT_MUTED);
    TextOutW(hdc, 24, 96, t, (int)wcslen(t));
    const wchar_t *hint = L"Isso só demora na primeira vez ou após atualizar o driver.";
    TextOutW(hdc, 24, 118, hint, (int)wcslen(hint));
}

static LRESULT CALLBACK PrepWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TIMER:
        if (g_prep && g_prep->finished)
        {
            KillTimer(hwnd, 1);
            PostMessageW(g_hMainWnd, WM_SHADER_PREP_DONE, 0, 0);
            DestroyWindow(hwnd);
            return 0;
        }
        if (!IsWindowVisible(hwnd) && GetTickCount64() - g_prepStartTick > 300) ShowWindow(hwnd, SW_SHOWNA);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_PREP_SKIP && g_prep)
        {
            g_prep->cancel = true;
            EnableWindow((HWND)lParam, FALSE);
            SetWindowTextW((HWND)lParam, L"Pulando…");
        }
        return 0;
    case WM_DRAWITEM:
        DrawOwnerButton((const DRAWITEMSTRUCT *)lParam);
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        PaintPrepWindow(hwnd, mem);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        if (g_prep) g_prep->cancel = true;  // closing the popup = skip
        return 0;
    case WM_DESTROY:
        g_hPrepWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Starts the preparation; the main window gets WM_SHADER_PREP_DONE when the game can be launched.
static void StartShaderPrep(HWND owner, const wchar_t *gameDir)
{
    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = PrepWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"OnRushShaderPrep";
        RegisterClassW(&wc);
        s_registered = true;
    }
    delete g_prep;
    g_prep = new ShaderPrepState();
    wcscpy_s(g_prep->gameDir, MAX_PATH, gameDir);
    g_prepStartTick = GetTickCount64();

    const int W = 440, H = 196;
    RECT orc; GetWindowRect(owner, &orc);
    int x = orc.left + (orc.right - orc.left - W) / 2, y = orc.top + (orc.bottom - orc.top - H) / 2;
    g_hPrepWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"OnRushShaderPrep", L"Preparando shaders", WS_POPUP | WS_CLIPCHILDREN,
                                 x, y, W, H, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND skip = CreateWindowW(L"BUTTON", L"Pular", WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                              W - 24 - 100, H - 24 - 32, 100, 32, g_hPrepWnd, (HMENU)ID_PREP_SKIP, nullptr, nullptr);
    SendMessageW(skip, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    SetTimer(g_hPrepWnd, 1, 100, nullptr);
    CloseHandle(CreateThread(nullptr, 0, ShaderPrepThread, g_prep, 0, nullptr));
}
