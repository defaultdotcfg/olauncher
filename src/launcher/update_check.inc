// ============================================================================
// Launcher update check: asks GitHub for the latest release of defaultdotcfg/rushlauncher
// and compares its tag (vX.Y.Z) with OLAUNCHER_VERSION. Runs on a worker thread and
// posts WM_UPDATE_RESULT back to the main window. When a newer release exists the user is offered
// to install it (self_update.inc), or to open its page when the release has no usable zip.
// ============================================================================
#include <string>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#include "version.h"

#define OLAUNCHER_REPO        L"defaultdotcfg/rushlauncher"
#define OLAUNCHER_RELEASES    L"https://github.com/defaultdotcfg/rushlauncher/releases"
#define WM_UPDATE_RESULT      (WM_USER + 130)

enum UpdateStatus { UPD_LATEST = 0, UPD_NEWER = 1, UPD_NO_RELEASE = 2, UPD_ERROR = 3 };
struct UpdateResult
{
    int status;
    bool manual;               // true = the user pressed the button (show a message box)
    HWND notify;               // window that receives WM_UPDATE_RESULT
    wchar_t tag[64];           // release tag, e.g. v0.2.0
    wchar_t url[512];          // release page (only github.com/defaultdotcfg/rushlauncher/...)
    wchar_t asset[512];        // release zip (only github.com/defaultdotcfg/rushlauncher/releases/download/...)
    char sha256[65];           // the zip's SHA-256 as published by GitHub ("digest": "sha256:...")
    wchar_t error[128];
};

// "v1.2.10" -> {1,2,10}; anything after the numbers (e.g. "-beta") is ignored
static void ParseVersion(const wchar_t *s, int out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
    if (*s == L'v' || *s == L'V') ++s;
    for (int i = 0; i < 4 && *s; ++i)
    {
        if (*s < L'0' || *s > L'9') break;
        out[i] = (int)wcstol(s, (wchar_t **)&s, 10);
        if (*s != L'.') break;
        ++s;
    }
}

static int CompareVersions(const wchar_t *a, const wchar_t *b)
{
    int va[4], vb[4];
    ParseVersion(a, va); ParseVersion(b, vb);
    for (int i = 0; i < 4; ++i) if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    return 0;
}

// Finds "key": "value" in the JSON text (first match) and copies the value. Enough for the
// flat fields of the GitHub release object (tag_name, html_url come before nested objects).
static bool JsonFindString(const std::string &json, const char *key, wchar_t *out, int outLen)
{
    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return false;
    p = json.find(':', p + k.size());
    if (p == std::string::npos) return false;
    p = json.find('"', p);
    if (p == std::string::npos) return false;
    size_t e = json.find('"', p + 1);
    if (e == std::string::npos || e - p - 1 >= (size_t)outLen) return false;
    std::string v = json.substr(p + 1, e - p - 1);
    return MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, out, outLen) > 0;
}

static DWORD WINAPI UpdateCheckThread(LPVOID param)
{
    UpdateResult *r = (UpdateResult *)param;
    r->status = UPD_ERROR;
    HINTERNET ses = WinHttpOpen(L"rushlauncher-update-check/" OLAUNCHER_VERSION, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET con = ses ? WinHttpConnect(ses, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : nullptr;
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", L"/repos/" OLAUNCHER_REPO L"/releases/latest", nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (ses) WinHttpSetTimeouts(ses, 5000, 5000, 8000, 8000);
    DWORD code = 0, sz = sizeof(code);
    std::string body;
    if (req && WinHttpSendRequest(req, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr) &&
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX))
    {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(req, &avail) && avail && body.size() < (1u << 20))
        {
            std::string chunk(avail, '\0'); DWORD got = 0;
            if (!WinHttpReadData(req, &chunk[0], avail, &got) || !got) break;
            body.append(chunk.data(), got);
        }
        if (code == 404) r->status = UPD_NO_RELEASE;  // repository has no published release yet
        else if (code == 200 && JsonFindString(body, "tag_name", r->tag, 64))
        {
            // only ever open pages of this repository
            if (!JsonFindString(body, "html_url", r->url, 512) || wcsncmp(r->url, OLAUNCHER_RELEASES L"/", wcslen(OLAUNCHER_RELEASES) + 1) != 0)
                wcscpy_s(r->url, 512, OLAUNCHER_RELEASES L"/latest");
            // release zip + its digest (for the automatic update)
            size_t a = body.find("\"browser_download_url\"");
            if (a != std::string::npos)
            {
                std::string asset = body.substr(a);
                wchar_t dig[96] = {};
                if (JsonFindString(asset, "browser_download_url", r->asset, 512) &&
                    (wcsncmp(r->asset, OLAUNCHER_RELEASES L"/download/", wcslen(OLAUNCHER_RELEASES L"/download/")) != 0 ||
                     wcslen(r->asset) < 5 || _wcsicmp(r->asset + wcslen(r->asset) - 4, L".zip") != 0))
                    r->asset[0] = 0;
                // "digest" belongs to the same asset object: look for it before the next asset starts
                size_t next = asset.find("\"browser_download_url\"", 1);
                size_t d = body.rfind("\"digest\"", a + (next == std::string::npos ? asset.size() : next));
                if (d != std::string::npos && JsonFindString(body.substr(d), "digest", dig, 96) && !wcsncmp(dig, L"sha256:", 7) && wcslen(dig) == 71)
                    for (int i = 0; i < 64; ++i) r->sha256[i] = (char)towlower(dig[7 + i]);
                if (!r->sha256[0]) r->asset[0] = 0;
            }
            r->status = CompareVersions(r->tag, OLAUNCHER_VERSION) > 0 ? UPD_NEWER : UPD_LATEST;
        }
        else swprintf_s(r->error, 128, L"HTTP %lu", code);
    }
    else swprintf_s(r->error, 128, L"WinHTTP error %lu", GetLastError());
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    if (!PostMessageW(r->notify, WM_UPDATE_RESULT, 0, (LPARAM)r)) delete r;
    return 0;
}

static bool g_updateChecking = false;
static UpdateResult g_updateLast = {};  // last finished check (status UPD_NEWER = highlight the button)
static bool g_updateKnown = false;

static void StartUpdateCheck(HWND notify, bool manual)
{
    if (g_updateChecking) return;
    g_updateChecking = true;
    UpdateResult *r = new UpdateResult();
    r->manual = manual; r->notify = notify;
    HANDLE t = CreateThread(nullptr, 0, UpdateCheckThread, r, 0, nullptr);
    if (t) CloseHandle(t); else { delete r; g_updateChecking = false; }
}
