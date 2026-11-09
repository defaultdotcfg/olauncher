// ============================================================================
// Game folder setup: turns the launcher's own folder (e.g. the extracted release zip,
// which has the port DLLs) into a playable game folder from the player's copy of ONRUSH.
//  - copies the game executable/metadata from <copy>\Mount and the console system
//    libraries from <copy>\EmbeddedXvd\Windows\System32
//  - links the big folders (Mount\dat, EmbeddedXvd) as NTFS junctions (nothing duplicated)
//  - writes onrush-pc.exe: a copy of game_release_xbox.exe whose version resource says
//    "ONRUSH(TM)" (what Task Manager shows). The player's copy is never modified.
// ============================================================================
#include <winioctl.h>
#include <string>

static const wchar_t *kPortFiles[] = { L"d3d12_x.dll", L"kernelx.dll", L"combase.dll", L"EtwPlus.dll", L"MMDevAPI.dll" };
static const wchar_t *kGameFiles[] = { L"game_release_xbox.exe", L"appxmanifest.xml", L"XboxIntegratedMultiplayer.dll", L"Bink2XboxOne.dll",
                                       L"appdata.bin", L"Catalog000.bin", L"resources.pri", L"saveversions.txt", L"SplashScreen.png" };
static const wchar_t *kSystemFiles[] = { L"AcpHal.dll", L"ACP_kernel.bin", L"xg_x.dll", L"xaudio2_9.dll", L"D3DCompiler_47.dll" };

static bool FileExistsIn(const wchar_t *dir, const wchar_t *name)
{
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%ls\\%ls", dir, name);
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES;
}

// A folder the launcher can start the game from: game executable + the port's D3D12 bridge.
static bool GameDirIsReady(const wchar_t *dir)
{
    return dir && dir[0] && (FileExistsIn(dir, L"onrush-pc.exe") || FileExistsIn(dir, L"game_release_xbox.exe")) && FileExistsIn(dir, L"d3d12_x.dll");
}

// Finds a copy of the game from the folder the user picked: the extracted package root
// (with Mount\ and EmbeddedXvd\) or its Mount folder. Fills the Mount and package root paths.
static bool FindGameCopy(const wchar_t *dir, wchar_t *mount, wchar_t *root)
{
    wchar_t full[MAX_PATH], p[MAX_PATH];
    if (!dir || !dir[0] || !GetFullPathNameW(dir, MAX_PATH, full, nullptr)) return false;
    size_t n = wcslen(full);
    while (n > 3 && full[n - 1] == L'\\') full[--n] = 0;
    swprintf_s(p, MAX_PATH, L"%ls\\Mount", full);
    if (FileExistsIn(p, L"game_release_xbox.exe")) { wcscpy_s(mount, MAX_PATH, p); wcscpy_s(root, MAX_PATH, full); return true; }
    if (FileExistsIn(full, L"game_release_xbox.exe"))
    {
        wcscpy_s(mount, MAX_PATH, full);
        wcscpy_s(root, MAX_PATH, full);
        wchar_t parent[MAX_PATH]; wcscpy_s(parent, MAX_PATH, full);
        PathRemoveFileSpecW(parent);
        swprintf_s(p, MAX_PATH, L"%ls\\EmbeddedXvd", parent);
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) wcscpy_s(root, MAX_PATH, parent);
        return true;
    }
    return false;
}

static bool PortFilesNextToLauncher()
{
    for (const wchar_t *f : kPortFiles) if (!FileExistsIn(g_exeDir, f)) return false;
    return true;
}

// NTFS junction (mount point) link -> target; no admin rights needed.
static bool CreateJunction(const wchar_t *link, const wchar_t *target)
{
    if (!CreateDirectoryW(link, nullptr)) return false;
    HANDLE h = CreateFileW(link, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) { RemoveDirectoryW(link); return false; }
    std::wstring sub = std::wstring(L"\\??\\") + target, print = target;
    WORD subB = (WORD)(sub.size() * 2), prB = (WORD)(print.size() * 2);
    WORD pathLen = (WORD)(subB + 2 + prB + 2);
    std::vector<BYTE> buf(16 + pathLen, 0);
    *(DWORD *)&buf[0] = IO_REPARSE_TAG_MOUNT_POINT;
    *(WORD *)&buf[4] = (WORD)(8 + pathLen);            // ReparseDataLength
    WORD *m = (WORD *)&buf[8];
    m[0] = 0; m[1] = subB; m[2] = (WORD)(subB + 2); m[3] = prB;  // substitute name, print name
    memcpy(&buf[16], sub.c_str(), subB);
    memcpy(&buf[16 + subB + 2], print.c_str(), prB);
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buf.data(), (DWORD)buf.size(), nullptr, 0, &ret, nullptr);
    CloseHandle(h);
    if (!ok) RemoveDirectoryW(link);
    return ok != FALSE;
}

// ---- VS_VERSIONINFO builder (same layout as scripts/brand_exe.py) ---------------------------
static void VerPad(std::vector<BYTE> &b) { while (b.size() % 4) b.push_back(0); }
static std::vector<BYTE> VerNode(const wchar_t *key, const std::vector<BYTE> &value, WORD type, const std::vector<BYTE> &children, int valueLen = -1)
{
    std::vector<BYTE> out(6, 0);
    const BYTE *k = (const BYTE *)key;
    out.insert(out.end(), k, k + (wcslen(key) + 1) * 2);
    VerPad(out);
    out.insert(out.end(), value.begin(), value.end());
    VerPad(out);
    out.insert(out.end(), children.begin(), children.end());
    WORD len = (WORD)out.size();
    WORD vlen = (WORD)(valueLen >= 0 ? valueLen : (type == 1 ? value.size() / 2 : value.size()));
    memcpy(&out[0], &len, 2); memcpy(&out[2], &vlen, 2); memcpy(&out[4], &type, 2);
    VerPad(out);
    return out;
}
static std::vector<BYTE> VerString(const wchar_t *s)
{
    const BYTE *b = (const BYTE *)s;
    return std::vector<BYTE>(b, b + (wcslen(s) + 1) * 2);
}

// onrush-pc.exe = copy of game_release_xbox.exe with the ONRUSH(TM) version resource.
static bool BrandGameExe(const wchar_t *dir)
{
    wchar_t src[MAX_PATH], dst[MAX_PATH];
    swprintf_s(src, MAX_PATH, L"%ls\\game_release_xbox.exe", dir);
    swprintf_s(dst, MAX_PATH, L"%ls\\onrush-pc.exe", dir);
    if (!CopyFileW(src, dst, FALSE)) return false;
    const DWORD ffi[13] = { 0xFEEF04BD, 0x00010000, 0x00010000, 0x00000001, 0x00010000, 0x00000001, 0x3F, 0, 0x40004, 1, 0, 0, 0 };
    std::vector<BYTE> ffiBytes((const BYTE *)ffi, (const BYTE *)ffi + sizeof(ffi));
    const wchar_t *kv[][2] = { { L"CompanyName", L"Codemasters" }, { L"FileDescription", L"ONRUSH™" }, { L"FileVersion", L"1.0.0.1" },
                               { L"InternalName", L"onrush-pc" }, { L"OriginalFilename", L"onrush-pc.exe" },
                               { L"ProductName", L"ONRUSH™" }, { L"ProductVersion", L"1.0.0.1" } };
    std::vector<BYTE> table, none;
    for (auto &e : kv) { auto n = VerNode(e[0], VerString(e[1]), 1, none); table.insert(table.end(), n.begin(), n.end()); }
    auto sfi = VerNode(L"StringFileInfo", none, 1, VerNode(L"041604B0", none, 1, table), 0);
    const WORD tr[2] = { 0x0416, 0x04B0 };
    auto vfi = VerNode(L"VarFileInfo", none, 1, VerNode(L"Translation", std::vector<BYTE>((const BYTE *)tr, (const BYTE *)tr + 4), 0, none), 0);
    std::vector<BYTE> kids = sfi; kids.insert(kids.end(), vfi.begin(), vfi.end());
    auto ver = VerNode(L"VS_VERSION_INFO", ffiBytes, 0, kids);
    // the game ships one version block (language 0x809); replace exactly that entry
    HANDLE h = BeginUpdateResourceW(dst, FALSE);
    if (!h) return true;  // plain copy still works, only the name in Task Manager differs
    BOOL ok = UpdateResourceW(h, MAKEINTRESOURCEW(16) /* RT_VERSION */, MAKEINTRESOURCEW(1), 0x0809, ver.data(), (DWORD)ver.size());
    if (!EndUpdateResourceW(h, ok ? FALSE : TRUE) || !ok) LogPrintf(L"[Setup] onrush-pc.exe sem o nome ONRUSH (recurso de versão não gravado)\r\n");
    return true;
}

// Prepares destDir from the copy at mount/root. Returns false with a reason in err.
static bool SetupGameFolder(const wchar_t *mount, const wchar_t *root, const wchar_t *destDir, std::wstring &err)
{
    wchar_t s[MAX_PATH], d[MAX_PATH];
    bool same = _wcsicmp(mount, destDir) == 0;
    int copied = 0;
    for (const wchar_t *f : kGameFiles)
    {
        swprintf_s(s, MAX_PATH, L"%ls\\%ls", mount, f);
        swprintf_s(d, MAX_PATH, L"%ls\\%ls", destDir, f);
        if (same || GetFileAttributesW(s) == INVALID_FILE_ATTRIBUTES) continue;
        if (!CopyFileW(s, d, FALSE)) { err = std::wstring(L"copiar ") + f + L" (erro " + std::to_wstring(GetLastError()) + L")"; return false; }
        ++copied;
    }
    for (const wchar_t *f : kSystemFiles)
    {
        swprintf_s(s, MAX_PATH, L"%ls\\EmbeddedXvd\\Windows\\System32\\%ls", root, f);
        swprintf_s(d, MAX_PATH, L"%ls\\%ls", destDir, f);
        if (GetFileAttributesW(s) == INVALID_FILE_ATTRIBUTES) continue;
        if (!CopyFileW(s, d, FALSE)) { err = std::wstring(L"copiar ") + f + L" (erro " + std::to_wstring(GetLastError()) + L")"; return false; }
        ++copied;
    }
    struct { const wchar_t *name; wchar_t target[MAX_PATH]; } links[2];
    links[0].name = L"dat"; swprintf_s(links[0].target, MAX_PATH, L"%ls\\dat", mount);
    links[1].name = L"EmbeddedXvd"; swprintf_s(links[1].target, MAX_PATH, L"%ls\\EmbeddedXvd", root);
    for (auto &l : links)
    {
        swprintf_s(d, MAX_PATH, L"%ls\\%ls", destDir, l.name);
        if (same || GetFileAttributesW(l.target) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW(d) != INVALID_FILE_ATTRIBUTES) continue;
        if (!CreateJunction(d, l.target)) { err = std::wstring(L"criar o atalho ") + l.name + L" (erro " + std::to_wstring(GetLastError()) + L")"; return false; }
        LogPrintf(L"[Setup] %ls -> %ls\r\n", d, l.target);
    }
    if (!BrandGameExe(destDir)) { err = L"criar onrush-pc.exe (erro " + std::to_wstring(GetLastError()) + L")"; return false; }
    LogPrintf(L"[Setup] Jogo preparado em %ls (%d arquivos copiados de %ls)\r\n", destDir, copied, root);
    return true;
}

// Asks to set the game up in the launcher's folder from the copy found in dir.
// Returns true when destDir (g_exeDir) is ready to play.
static bool OfferGameSetup(HWND hwnd, const wchar_t *dir)
{
    wchar_t mount[MAX_PATH], root[MAX_PATH], msg[1024];
    if (!FindGameCopy(dir, mount, root)) return false;
    if (!PortFilesNextToLauncher()) { MessageBoxW(hwnd, T(S_SETUP_NOPORT), L"ONRUSH", MB_ICONWARNING); return false; }
    swprintf_s(msg, 1024, T(S_SETUP_ASK), root, g_exeDir);
    if (MessageBoxW(hwnd, msg, L"ONRUSH", MB_ICONQUESTION | MB_YESNO) != IDYES) return false;
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::wstring err;
    bool ok = SetupGameFolder(mount, root, g_exeDir, err);
    SetCursor(old);
    if (!ok)
    {
        swprintf_s(msg, 1024, T(S_SETUP_FAIL), err.c_str());
        MessageBoxW(hwnd, msg, L"ONRUSH", MB_ICONERROR);
        return false;
    }
    return true;
}
