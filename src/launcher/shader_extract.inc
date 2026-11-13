// ============================================================================
// Shader extraction straight from the game's packs, without running the game.
// <game>\dat\*.dat is one LZ4 stream per file: an 8-byte header, then raw LZ4 blocks that each decode to
// 128 KB, back to back, with no block headers and matches reaching into the previous block (64 KB window).
// The shaders sit in the decoded data as complete DXBC containers, byte for byte what the game hands to
// CreateGraphicsPipelineState / CreateComputePipelineState, so FNV-1a64 of the container is the same key the
// bridge uses: each one is written as shaders\pso_h<fnv>_<vs|ps|cs>.dxbc, exactly like a dump the game made.
// The packs are memory-mapped and decoded through a small sliding window, so memory stays at a few MB per thread.
// ============================================================================
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

struct ShaderExtractState
{
    std::atomic<long long> bytesDone{ 0 }, bytesTotal{ 0 };
    std::atomic<int> found{ 0 }, written{ 0 }, tessPairs{ 0 }, badFiles{ 0 };
    std::atomic<bool> *cancel = nullptr;
    std::mutex lock;
    std::unordered_set<uint64_t> seen;   // containers already handled (any pack) or present on disk
    std::wstring shadersDir;
};

static uint64_t ExFnv1a64(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}

// Program type of a DXBC container (SHEX/SHDR version token, bits 16..31): 0 PS, 1 VS, 3 HS, 4 DS, 5 CS; -1 if unreadable.
static int ExShaderType(const uint8_t *c, size_t n)
{
    if (n < 32) return -1;
    uint32_t parts = *(const uint32_t *)(c + 28);
    if (parts > 64 || 32 + 4ull * parts > n) return -1;
    for (uint32_t k = 0; k < parts; ++k)
    {
        uint32_t o = *(const uint32_t *)(c + 32 + 4 * k);
        if ((size_t)o + 12 > n) return -1;
        if (!memcmp(c + o, "SHEX", 4) || !memcmp(c + o, "SHDR", 4)) return (int)(*(const uint32_t *)(c + o + 8) >> 16);
    }
    return -1;
}

// Writes a file once (atomically); true if it was written now.
static bool ExWriteOnce(const wchar_t *path, const uint8_t *c, size_t n)
{
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return false;
    wchar_t tmp[MAX_PATH];
    swprintf_s(tmp, MAX_PATH, L"%ls.%lu.tmp", path, GetCurrentThreadId());
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    BOOL ok = WriteFile(f, c, (DWORD)n, &w, nullptr) && w == n;
    CloseHandle(f);
    if (ok && MoveFileExW(tmp, path, 0)) return true;   // no replace: another thread may have written the same file
    DeleteFileW(tmp);
    return false;
}

// test hook: every container found (any kind), with its pack and decoded offset
static void (*g_exTrace)(const wchar_t *pack, unsigned long long offset, const uint8_t *c, size_t n) = nullptr;

// VS / PS / CS -> shaders\pso_h<fnv>_<kind>.dxbc, the name the bridge gives its own dumps.
static void ExEmit(ShaderExtractState *st, uint64_t h, int type, const uint8_t *c, size_t n)
{
    {
        std::lock_guard<std::mutex> g(st->lock);
        if (!st->seen.insert(h).second) return;
    }
    st->found++;
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%ls\\pso_h%016llx_%ls.dxbc", st->shadersDir.c_str(), (unsigned long long)h, type == 0 ? L"ps" : type == 1 ? L"vs" : L"cs");
    if (ExWriteOnce(path, c, n)) st->written++;
}

// One pack being decoded: the mapped compressed bytes and a sliding window of decoded output.
struct ExPack
{
    ShaderExtractState *st = nullptr;
    const uint8_t *src = nullptr;
    size_t n = 0, pos = 8;            // 8-byte pack header
    long long reported = 0;
    std::vector<uint8_t> out;
    size_t scanFrom = 0;              // first offset in `out` not yet searched for "DXBC"
    unsigned long long base = 0;      // decoded offset of out[0]
    const wchar_t *path = L"";
    // Tessellated materials are stored as VS, (PS), HS, DS; a second pass may add VS, (PS), DS reusing that HS.
    // So a DS pairs with the last VS and the last HS before it: shaders\tess\vs<V>_hs<H>.dxbc / vs<V>_ds<D>.dxbc,
    // the pair files the bridge writes when it meets such a pipeline (tess_bypass.py builds one VS from them).
    uint64_t lastVs = 0, lastHs = 0;
    std::vector<uint8_t> lastHsBytes;
    static const size_t kBlock = 128 * 1024, kWindow = 64 * 1024, kMaxShader = 8u << 20, kFlushAt = 24u << 20;

    // Emits complete containers; when trimming keeps the first incomplete one and the 64 KB match window.
    void Scan(bool final)
    {
        size_t keepFrom = out.size() > kWindow ? out.size() - kWindow : 0;
        size_t i = scanFrom;
        while (i + 4 <= out.size())
        {
            const uint8_t *p = (const uint8_t *)memchr(out.data() + i, 'D', out.size() - i - 3);
            if (!p) { i = out.size() - 3; break; }
            i = (size_t)(p - out.data());
            if (memcmp(p, "DXBC", 4)) { ++i; continue; }
            if (i + 32 > out.size()) { keepFrom = min(keepFrom, i); break; }
            uint32_t len = *(const uint32_t *)(p + 24);
            if (len < 64 || len > kMaxShader) { ++i; continue; }
            if (i + len > out.size())
            {
                if (final) { ++i; continue; }
                keepFrom = min(keepFrom, i);
                break;
            }
            if (g_exTrace) g_exTrace(path, base + i, p, len);
            OnShader(p, len);
            i += 4;
        }
        scanFrom = i;
        if (!final && keepFrom > 0)
        {
            out.erase(out.begin(), out.begin() + keepFrom);
            scanFrom -= min(scanFrom, keepFrom);
            base += keepFrom;
        }
    }

    void OnShader(const uint8_t *c, size_t len)
    {
        const int type = ExShaderType(c, len);
        if (type < 0) return;
        const uint64_t h = ExFnv1a64(c, len);
        if (type == 0 || type == 1 || type == 5) ExEmit(st, h, type, c, len);
        if (type == 1) lastVs = h;
        else if (type == 3) { lastHs = h; lastHsBytes.assign(c, c + len); }
        else if (type == 4 && lastVs && lastHs)
        {
            wchar_t hp[MAX_PATH], dp[MAX_PATH];
            swprintf_s(hp, MAX_PATH, L"%ls\\tess\\vs%016llx_hs%016llx.dxbc", st->shadersDir.c_str(), (unsigned long long)lastVs, (unsigned long long)lastHs);
            swprintf_s(dp, MAX_PATH, L"%ls\\tess\\vs%016llx_ds%016llx.dxbc", st->shadersDir.c_str(), (unsigned long long)lastVs, (unsigned long long)h);
            ExWriteOnce(hp, lastHsBytes.data(), lastHsBytes.size());
            if (ExWriteOnce(dp, c, len)) st->tessPairs++;
        }
    }

    bool Decode()
    {
        out.reserve(kFlushAt + kMaxShader + 2 * kBlock);
        bool ok = true;
        while (pos < n && ok)
        {
            if (st->cancel && *st->cancel) return true;
            size_t blockStart = out.size();
            for (;;)   // one block: literals/match sequences until it has produced 128 KB (it ends with literals)
            {
                if (pos >= n) break;
                uint8_t t = src[pos++];
                size_t ll = t >> 4;
                if (ll == 15) { uint8_t b; do { if (pos >= n) { ok = false; break; } b = src[pos++]; ll += b; } while (b == 255); }
                if (!ok || pos + ll > n) { ok = false; break; }
                out.insert(out.end(), src + pos, src + pos + ll);
                pos += ll;
                if (out.size() - blockStart >= kBlock || pos >= n) break;
                if (pos + 2 > n) { ok = false; break; }
                size_t off = src[pos] | (src[pos + 1] << 8);
                pos += 2;
                size_t ml = t & 15;
                if (ml == 15) { uint8_t b; do { if (pos >= n) { ok = false; break; } b = src[pos++]; ml += b; } while (b == 255); }
                ml += 4;
                if (!ok || off == 0 || off > out.size()) { ok = false; break; }
                size_t from = out.size() - off, at = out.size();
                out.resize(at + ml);
                uint8_t *o = out.data();
                if (off >= ml) memcpy(o + at, o + from, ml);
                else for (size_t k = 0; k < ml; ++k) o[at + k] = o[from + k];   // overlapping run
                if (out.size() - blockStart > kBlock) { ok = false; break; }
            }
            if (out.size() >= kFlushAt) Scan(false);
            if ((long long)pos - reported >= (4 << 20)) { st->bytesDone += (long long)pos - reported; reported = (long long)pos; }
        }
        if (ok) Scan(true);
        return ok;
    }
};

// A read error on the mapped file (e.g. the drive went away) raises an exception instead of failing a ReadFile.
static bool ExDecodeGuarded(ExPack &pk)
{
    __try { return pk.Decode(); }
    __except (GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
}

// Decodes one pack and emits its shaders. Returns false if it could not be read as expected.
static bool ExtractPack(ShaderExtractState *st, const wchar_t *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz = {};
    GetFileSizeEx(f, &sz);
    HANDLE m = sz.QuadPart > 16 ? CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
    const uint8_t *src = m ? (const uint8_t *)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0) : nullptr;
    bool ok = false;
    if (src)
    {
        ExPack pk;
        pk.st = st; pk.src = src; pk.n = (size_t)sz.QuadPart; pk.path = path;
        ok = ExDecodeGuarded(pk);
        st->bytesDone += (long long)pk.n - pk.reported;
        UnmapViewOfFile(src);
    }
    if (m) CloseHandle(m);
    CloseHandle(f);
    return ok;
}

// Extracts every VS/PS/CS shader of <gameDir>\dat into <gameDir>\shaders. Returns the number of new dumps.
static int ExtractGameShaders(const wchar_t *gameDir, std::atomic<bool> *cancel, ShaderExtractState &st)
{
    wchar_t pattern[MAX_PATH];
    swprintf_s(pattern, MAX_PATH, L"%ls\\dat\\*.dat", gameDir);
    std::vector<std::pair<long long, std::wstring>> packs;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        long long size = ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        packs.push_back({ size, std::wstring(gameDir) + L"\\dat\\" + fd.cFileName });
        st.bytesTotal += size;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(packs.begin(), packs.end(), [](auto &a, auto &b) { return a.first > b.first; });  // big ones first

    st.cancel = cancel;
    st.shadersDir = std::wstring(gameDir) + L"\\shaders";
    CreateDirectoryW(st.shadersDir.c_str(), nullptr);
    CreateDirectoryW((st.shadersDir + L"\\tess").c_str(), nullptr);
    // shaders already dumped: skip hashing them again
    WIN32_FIND_DATAW d;
    HANDLE hd = FindFirstFileW((st.shadersDir + L"\\pso_h*_*.dxbc").c_str(), &d);
    if (hd != INVALID_HANDLE_VALUE)
    {
        do { if (wcslen(d.cFileName) > 21) st.seen.insert(wcstoull(std::wstring(d.cFileName + 5, 16).c_str(), nullptr, 16)); } while (FindNextFileW(hd, &d));
        FindClose(hd);
    }

    std::atomic<size_t> next{ 0 };
    auto work = [&]() {
        for (size_t i; !(cancel && *cancel) && (i = next++) < packs.size();)
            if (!ExtractPack(&st, packs[i].second.c_str()))
            {
                st.badFiles++;
                LogPrintf(L"[Shaders] pacote não lido: %ls\r\n", PathFindFileNameW(packs[i].second.c_str()));
            }
    };
    // decoding is fast; reading the packs from disk is the limit, so a few threads are enough
    unsigned nt = max(1u, min(4u, std::thread::hardware_concurrency()));
    std::vector<std::thread> th;
    for (unsigned t = 0; t < nt; ++t) th.emplace_back(work);
    for (auto &t : th) t.join();
    return st.written.load();
}
