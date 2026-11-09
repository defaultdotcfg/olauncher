// ============================================================================
// Windows.Xbox.Storage (Connected Storage) -> plain files on disk.
//   <game dir>\save\<player name>\<container>\<blob>
// The game keeps its profile/progress in Connected Storage. This used to be a
// mock that failed every call, so nothing was ever saved (tutorial replayed on
// every boot). Interface layouts follow WinDurango's Windows.Xbox.Storage.idl.
// Every call completes synchronously; async ops report Completed (or Error).
// ============================================================================
#include <functional>

static const GUID IID_CS_IBufferByteAccess = {0x905a0fef, 0xbc53, 0x11df, {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda}};
// IIterable<IKeyValuePair<String, IBuffer>> (pinterface IID, computed)
static const GUID IID_CS_IterableKvpStrBuf = {0x3c9ffa92, 0x5123, 0x5ac4, {0xb1, 0x11, 0x03, 0xc2, 0x15, 0xf0, 0xc5, 0x1c}};
static const GUID IID_CS_Statics = {0x07020414, 0xb3e9, 0x47c8, {0xa3, 0x6f, 0xf2, 0x4e, 0x2d, 0xc6, 0x55, 0x73}};
static const GUID IID_CS_Statics2 = {0xdf3fe390, 0x13fa, 0x4814, {0x91, 0xc1, 0xfb, 0x92, 0xf9, 0x0f, 0x64, 0x1d}};
static const GUID IID_CS_Space2 = {0xbc5b91a7, 0x2afc, 0x46fd, {0x81, 0xd4, 0x74, 0xb8, 0x18, 0x8d, 0x74, 0x8a}};
static const GUID IID_CS_Container2 = {0xaf964b72, 0xec65, 0x46de, {0x93, 0x5c, 0x84, 0x5a, 0xd0, 0x19, 0x4f, 0x0d}};
static const GUID IID_CS_CInfo2Query = {0xf8a8cdfe, 0x1d10, 0x42a5, {0xb0, 0x1e, 0x41, 0x71, 0x8e, 0x18, 0x3c, 0xa9}};

// ---- HSTRING helpers ----
typedef PCWSTR(WINAPI *PFN_CsGetRaw)(HSTRING, UINT32 *);
typedef HRESULT(WINAPI *PFN_CsDelete)(HSTRING);
static std::wstring CsStr(HSTRING h) {
  static PFN_CsGetRaw s_get = (PFN_CsGetRaw)GetProcAddress(LoadLibraryW(L"combase.dll"), "WindowsGetStringRawBuffer");
  if (!h || !s_get) return std::wstring();
  UINT32 n = 0; PCWSTR p = s_get(h, &n);
  return p ? std::wstring(p, n) : std::wstring();
}
static void CsFreeStr(HSTRING h) {
  static PFN_CsDelete s_del = (PFN_CsDelete)GetProcAddress(LoadLibraryW(L"combase.dll"), "WindowsDeleteString");
  if (h && s_del) s_del(h);
}
static std::string CsNarrow(const std::wstring &w) {
  std::string s; for (wchar_t c : w) s += (c < 128 ? (char)c : '?'); return s;
}

// ---- paths ----
static std::wstring CsEncode(const std::wstring &n) {  // file-name safe, reversible
  std::wstring o; wchar_t b[8];
  for (wchar_t c : n) {
    bool last = &c == &n.back();
    if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' ||
        ((c == L'.' || c == L' ') && !last) || (c >= 0xC0 && c < 0xD800))   // accented letters (João) stay readable
      o += c;
    else { swprintf_s(b, L"%%%04X", (unsigned)c); o += b; }
  }
  return o.empty() ? L"%0000" : o;
}
static std::wstring CsDecode(const std::wstring &n) {
  std::wstring o;
  for (size_t i = 0; i < n.size(); ++i) {
    if (n[i] == L'%' && i + 4 < n.size()) { o += (wchar_t)wcstoul(n.substr(i + 1, 4).c_str(), nullptr, 16); i += 4; }
    else o += n[i];
  }
  return o;
}
static std::wstring CsPlayerName() { return OrPlayerName(); }
static std::wstring CsRoot(bool machine) {
  // Save folder chosen in the launcher (ONRUSH_SAVE_DIR), else <game dir>\save.
  wchar_t p[MAX_PATH] = {};
  std::wstring r;
  DWORD n = GetEnvironmentVariableW(L"ONRUSH_SAVE_DIR", p, MAX_PATH);
  if (n > 0 && n < MAX_PATH) r = p;
  else {
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    wchar_t *s = wcsrchr(p, L'\\'); if (s) *s = 0;
    r = std::wstring(p) + L"\\save";
  }
  while (!r.empty() && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
  for (size_t i = 3; i <= r.size(); ++i)   // create every missing level
    if (i == r.size() || r[i] == L'\\') CreateDirectoryW(r.substr(0, i).c_str(), nullptr);
  r += L"\\" + (machine ? std::wstring(L"_machine") : CsEncode(CsPlayerName()));
  CreateDirectoryW(r.c_str(), nullptr);
  return r;
}

static volatile LONG g_csTraceN = 0;
#define CS_TRACE(n) do { if (InterlockedIncrement(&g_csTraceN) < 4000) log_line("[cs] . %s\r\n", n); } while (0)
static void CsTraceQi(const char *who, REFIID r) {
  if (InterlockedIncrement(&g_csTraceN) >= 4000) return;
  log_line("[cs] . QI %s {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\r\n", who, r.Data1, r.Data2, r.Data3,
           r.Data4[0], r.Data4[1], r.Data4[2], r.Data4[3], r.Data4[4], r.Data4[5], r.Data4[6], r.Data4[7]);
}
// ---- COM boilerplate ----
template <class D> struct CsBase : public IInspectable {
  LONG m_ref = 1;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void **p) override {
    if (!p) return E_POINTER;
    void *o = static_cast<D *>(this)->Qi(r);
    CsTraceQi(__FUNCTION__, r);
    if (!o) { *p = nullptr; return E_NOINTERFACE; }
    *p = o; AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
  ULONG STDMETHODCALLTYPE Release() override {
    LONG n = InterlockedDecrement(&m_ref);
    if (n == 0) delete static_cast<D *>(this);
    return n;
  }
  HRESULT STDMETHODCALLTYPE GetIids(ULONG *c, IID **i) override { if (c) *c = 0; if (i) *i = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *n) override { if (n) *n = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *l) override { if (l) *l = BaseTrust; return S_OK; }
};
// Secondary interface of an object: forwards identity to the parent.
template <class P> struct CsSub : public IInspectable {
  P *m_p = nullptr;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void **p) override { return m_p->QueryInterface(r, p); }
  ULONG STDMETHODCALLTYPE AddRef() override { return m_p->AddRef(); }
  ULONG STDMETHODCALLTYPE Release() override { return m_p->Release(); }
  HRESULT STDMETHODCALLTYPE GetIids(ULONG *c, IID **i) override { if (c) *c = 0; if (i) *i = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *n) override { if (n) *n = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *l) override { if (l) *l = BaseTrust; return S_OK; }
};

// ---- async operation / action ----
enum CsResKind { CS_VOID, CS_PTR, CS_U32, CS_I32, CS_I64 };
struct CsAsyncOp;
struct CsAsyncInfo : public CsSub<CsAsyncOp> {
  virtual HRESULT STDMETHODCALLTYPE get_Id(unsigned *id);
  virtual HRESULT STDMETHODCALLTYPE get_Status(int *st);
  virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT *hr);
  virtual HRESULT STDMETHODCALLTYPE Cancel() { CS_TRACE("Cancel"); return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE Close() { CS_TRACE("Close"); return S_OK; }
};
struct CsAsyncOp : public CsBase<CsAsyncOp> {
  CsResKind m_kind = CS_VOID; IUnknown *m_ptr = nullptr; int64_t m_num = 0; HRESULT m_err = S_OK;
  IUnknown *m_handler = nullptr; CsAsyncInfo m_info;
  CsAsyncOp() { m_info.m_p = this; }
  ~CsAsyncOp() { if (m_ptr) m_ptr->Release(); if (m_handler) m_handler->Release(); }
  void *Qi(REFIID r) { if (memcmp(&r, &IID_IAsyncInfo, sizeof(GUID)) == 0) return &m_info; return this; }
  // Slot 6: put_Completed
  virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown *h) { CS_TRACE("put_Completed");
    if (!h) return S_OK;
    h->AddRef(); if (m_handler) m_handler->Release(); m_handler = h;
    typedef HRESULT(STDMETHODCALLTYPE * PFN_Inv)(void *, void *, int);
    PFN_Inv inv = (PFN_Inv)(*(void ***)h)[3];
    inv(h, this, SUCCEEDED(m_err) ? 1 : 3);
    return S_OK;
  }
  // Slot 7: get_Completed
  virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown **h) { CS_TRACE("get_Completed");
    if (!h) return E_POINTER; *h = m_handler; if (m_handler) m_handler->AddRef(); return S_OK;
  }
  // Slot 8: GetResults (IAsyncAction: no argument; out is not touched)
  virtual HRESULT STDMETHODCALLTYPE GetResults(void *out) { CS_TRACE("GetResults");
    if (FAILED(m_err)) return m_err;
    switch (m_kind) {
      case CS_VOID: return S_OK;
      case CS_PTR: if (!out) return E_POINTER; *(IUnknown **)out = m_ptr; if (m_ptr) m_ptr->AddRef(); return S_OK;
      case CS_U32: if (!out) return E_POINTER; *(UINT32 *)out = (UINT32)m_num; return S_OK;
      case CS_I32: if (!out) return E_POINTER; *(INT32 *)out = (INT32)m_num; return S_OK;
      case CS_I64: if (!out) return E_POINTER; *(INT64 *)out = m_num; return S_OK;
    }
    return S_OK;
  }
};
inline HRESULT STDMETHODCALLTYPE CsAsyncInfo::get_Id(unsigned *id) { CS_TRACE("CsAsyncInfo::get_Id"); if (id) *id = 1; return S_OK; }
inline HRESULT STDMETHODCALLTYPE CsAsyncInfo::get_Status(int *st) { CS_TRACE("CsAsyncInfo::get_Status"); if (st) *st = SUCCEEDED(m_p->m_err) ? 1 : 3; return S_OK; }
inline HRESULT STDMETHODCALLTYPE CsAsyncInfo::get_ErrorCode(HRESULT *hr) { CS_TRACE("CsAsyncInfo::get_ErrorCode"); if (hr) *hr = m_p->m_err; return S_OK; }

static HRESULT CsMakeOp(void **out, CsResKind k, IUnknown *ptr = nullptr, int64_t num = 0, HRESULT err = S_OK) {
  if (!out) { if (ptr) ptr->Release(); return E_POINTER; }
  CsAsyncOp *op = new CsAsyncOp(); op->m_kind = k; op->m_ptr = ptr; op->m_num = num; op->m_err = err;
  *out = op; return S_OK;
}

// ---- IBuffer (results of GetAsync) ----
struct CsBuffer;
struct CsByteAccess : public IUnknown {
  CsBuffer *m_p = nullptr;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void **p) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  virtual HRESULT STDMETHODCALLTYPE Buffer(BYTE **v);
};
struct CsBuffer : public CsBase<CsBuffer> {
  std::vector<uint8_t> m_d; UINT32 m_len = 0; CsByteAccess m_ba;
  CsBuffer() { m_ba.m_p = this; }
  void *Qi(REFIID r) { if (memcmp(&r, &IID_CS_IBufferByteAccess, sizeof(GUID)) == 0) return &m_ba; return this; }
  virtual HRESULT STDMETHODCALLTYPE get_Capacity(UINT32 *v) { CS_TRACE("get_Capacity"); if (v) *v = (UINT32)m_d.size(); return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE get_Length(UINT32 *v) { CS_TRACE("get_Length"); if (v) *v = m_len; return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE put_Length(UINT32 v) { CS_TRACE("put_Length"); if (v > m_d.size()) return E_INVALIDARG; m_len = v; return S_OK; }
};
inline HRESULT STDMETHODCALLTYPE CsByteAccess::QueryInterface(REFIID r, void **p) { CS_TRACE("CsByteAccess::QueryInterface"); return m_p->QueryInterface(r, p); }
inline ULONG STDMETHODCALLTYPE CsByteAccess::AddRef() { return m_p->AddRef(); }
inline ULONG STDMETHODCALLTYPE CsByteAccess::Release() { return m_p->Release(); }
inline HRESULT STDMETHODCALLTYPE CsByteAccess::Buffer(BYTE **v) { CS_TRACE("CsByteAccess::Buffer"); if (!v) return E_POINTER; *v = m_p->m_d.empty() ? nullptr : m_p->m_d.data(); return S_OK; }

// ---- IMapView<String, IBuffer> + iteration (results of GetAsync) ----
struct CsBlobMap;
struct CsKvp : public CsBase<CsKvp> {
  std::wstring m_key; IUnknown *m_val = nullptr;
  ~CsKvp() { if (m_val) m_val->Release(); }
  void *Qi(REFIID) { return this; }
  virtual HRESULT STDMETHODCALLTYPE get_Key(HSTRING *k) { CS_TRACE("get_Key"); return CreateHString(m_key.c_str(), k); }
  virtual HRESULT STDMETHODCALLTYPE get_Value(IUnknown **v) { CS_TRACE("get_Value"); if (!v) return E_POINTER; *v = m_val; if (m_val) m_val->AddRef(); return S_OK; }
};
struct CsMapIter : public CsBase<CsMapIter> {
  CsBlobMap *m_map = nullptr; UINT32 m_i = 0;
  ~CsMapIter();
  void *Qi(REFIID) { return this; }
  virtual HRESULT STDMETHODCALLTYPE get_Current(IUnknown **kv);
  virtual HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean *b);
  virtual HRESULT STDMETHODCALLTYPE MoveNext(boolean *b);
  virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32 cap, IUnknown **items, UINT32 *actual);
};
struct CsMapIterable : public CsSub<CsBlobMap> {
  virtual HRESULT STDMETHODCALLTYPE First(IUnknown **it);
};
struct CsBlobMap : public CsBase<CsBlobMap> {
  std::vector<std::pair<std::wstring, CsBuffer *>> m_items; CsMapIterable m_iter;
  CsBlobMap() { m_iter.m_p = this; }
  ~CsBlobMap() { for (auto &e : m_items) e.second->Release(); }
  void *Qi(REFIID r) { if (memcmp(&r, &IID_CS_IterableKvpStrBuf, sizeof(GUID)) == 0) return &m_iter; return this; }
  virtual HRESULT STDMETHODCALLTYPE Lookup(HSTRING key, IUnknown **v) { CS_TRACE("Lookup");
    std::wstring k = CsStr(key);
    for (auto &e : m_items) if (e.first == k) { *v = e.second; e.second->AddRef(); return S_OK; }
    *v = nullptr; return E_BOUNDS;
  }
  virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32 *n) { CS_TRACE("get_Size"); if (n) *n = (UINT32)m_items.size(); return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE HasKey(HSTRING key, boolean *b) { CS_TRACE("HasKey");
    std::wstring k = CsStr(key); *b = 0; for (auto &e : m_items) if (e.first == k) *b = 1; return S_OK;
  }
  virtual HRESULT STDMETHODCALLTYPE Split(IUnknown **a, IUnknown **b) { CS_TRACE("Split"); if (a) *a = nullptr; if (b) *b = nullptr; return S_OK; }
};
inline CsMapIter::~CsMapIter() { if (m_map) m_map->Release(); }
inline HRESULT STDMETHODCALLTYPE CsMapIter::get_Current(IUnknown **kv) { CS_TRACE("CsMapIter::get_Current");
  if (!kv) return E_POINTER;
  if (m_i >= m_map->m_items.size()) { *kv = nullptr; return E_BOUNDS; }
  CsKvp *p = new CsKvp(); p->m_key = m_map->m_items[m_i].first; p->m_val = m_map->m_items[m_i].second; p->m_val->AddRef();
  *kv = p; return S_OK;
}
inline HRESULT STDMETHODCALLTYPE CsMapIter::get_HasCurrent(boolean *b) { CS_TRACE("CsMapIter::get_HasCurrent"); *b = m_i < m_map->m_items.size(); return S_OK; }
inline HRESULT STDMETHODCALLTYPE CsMapIter::MoveNext(boolean *b) { CS_TRACE("CsMapIter::MoveNext"); if (m_i < m_map->m_items.size()) ++m_i; *b = m_i < m_map->m_items.size(); return S_OK; }
inline HRESULT STDMETHODCALLTYPE CsMapIter::GetMany(UINT32 cap, IUnknown **items, UINT32 *actual) { CS_TRACE("CsMapIter::GetMany");
  UINT32 n = 0;
  while (n < cap && m_i < m_map->m_items.size()) { get_Current(&items[n]); ++n; ++m_i; }
  if (actual) *actual = n; return S_OK;
}
inline HRESULT STDMETHODCALLTYPE CsMapIterable::First(IUnknown **it) { CS_TRACE("CsMapIterable::First");
  CsMapIter *i = new CsMapIter(); i->m_map = m_p; m_p->AddRef(); *it = i; return S_OK;
}

// ---- reading the game's collections (raw vtable calls) ----
typedef HRESULT(STDMETHODCALLTYPE *PFN_CsGetPtr)(void *, void **);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CsGetBool)(void *, boolean *);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CsGetU32)(void *, UINT32 *);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CsPutU32)(void *, UINT32);
static inline void **CsVt(void *o) { return *(void ***)o; }

// IIterable<String>: calls fn for every string.
static void CsForEachString(IUnknown *iterable, const std::function<void(const std::wstring &)> &fn) {
  if (!iterable) return;
  void *it = nullptr;
  if (FAILED(((PFN_CsGetPtr)CsVt(iterable)[6])(iterable, &it)) || !it) return;  // First
  boolean has = 0;
  ((PFN_CsGetBool)CsVt(it)[7])(it, &has);                                     // get_HasCurrent
  while (has) {
    HSTRING h = nullptr;
    ((PFN_CsGetPtr)CsVt(it)[6])(it, (void **)&h);                             // get_Current
    fn(CsStr(h)); CsFreeStr(h);
    ((PFN_CsGetBool)CsVt(it)[8])(it, &has);                                   // MoveNext
  }
  ((IUnknown *)it)->Release();
}
// IMapView<String, IBuffer>: calls fn(key, IBuffer*) for every pair.
static bool CsForEachBlob(IUnknown *map, const std::function<void(const std::wstring &, IUnknown *)> &fn) {
  if (!map) return true;
  IUnknown *iterable = nullptr;
  if (FAILED(map->QueryInterface(IID_CS_IterableKvpStrBuf, (void **)&iterable)) || !iterable) {
    log_line("[cs] map %p: no IIterable<IKeyValuePair<String,IBuffer>>\r\n", map);
    return false;
  }
  void *it = nullptr;
  ((PFN_CsGetPtr)CsVt(iterable)[6])(iterable, &it);
  boolean has = 0;
  if (it) ((PFN_CsGetBool)CsVt(it)[7])(it, &has);
  while (it && has) {
    void *kv = nullptr;
    ((PFN_CsGetPtr)CsVt(it)[6])(it, &kv);
    if (kv) {
      HSTRING k = nullptr; IUnknown *v = nullptr;
      ((PFN_CsGetPtr)CsVt(kv)[6])(kv, (void **)&k);
      ((PFN_CsGetPtr)CsVt(kv)[7])(kv, (void **)&v);
      fn(CsStr(k), v);
      CsFreeStr(k); if (v) v->Release(); ((IUnknown *)kv)->Release();
    }
    ((PFN_CsGetBool)CsVt(it)[8])(it, &has);
  }
  if (it) ((IUnknown *)it)->Release();
  iterable->Release();
  return true;
}
// IBuffer data pointer + length / capacity.
static BYTE *CsBufData(IUnknown *buf) {
  IUnknown *ba = nullptr; BYTE *p = nullptr;
  if (buf && SUCCEEDED(buf->QueryInterface(IID_CS_IBufferByteAccess, (void **)&ba)) && ba) {
    typedef HRESULT(STDMETHODCALLTYPE * PFN_B)(void *, BYTE **);
    ((PFN_B)CsVt(ba)[3])(ba, &p); ba->Release();
  }
  return p;
}

// ---- file helpers ----
static bool CsReadFile(const std::wstring &path, std::vector<uint8_t> &d) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz = {}; GetFileSizeEx(f, &sz); d.resize((size_t)sz.QuadPart);
  DWORD got = 0; BOOL ok = d.empty() || ReadFile(f, d.data(), (DWORD)d.size(), &got, nullptr);
  CloseHandle(f); return ok && got == d.size();
}
static bool CsWriteFile(const std::wstring &path, const void *p, UINT32 n) {
  std::wstring tmp = path + L".tmp";   // write-then-rename: a crash mid-save keeps the old file
  HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD w = 0; BOOL ok = n == 0 || WriteFile(f, p, n, &w, nullptr);
  FlushFileBuffers(f); CloseHandle(f);
  return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
struct CsEntry { std::wstring name; uint64_t size; FILETIME mtime; };
static std::vector<CsEntry> CsList(const std::wstring &dir, bool dirs, const std::wstring &prefix) {
  std::vector<CsEntry> out;
  WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return out;
  do {
    bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0 || isDir != dirs) continue;
    size_t ln = wcslen(fd.cFileName);
    if (ln > 4 && _wcsicmp(fd.cFileName + ln - 4, L".tmp") == 0) continue;
    std::wstring name = CsDecode(fd.cFileName);
    if (!prefix.empty() && name.compare(0, prefix.size(), prefix) != 0) continue;
    uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    if (dirs) { size = 0; for (auto &b : CsList(dir + L"\\" + fd.cFileName, false, L"")) size += b.size; }
    out.push_back({ name, size, fd.ftLastWriteTime });
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return out;
}

// ---- IVectorView<BlobInfo | ContainerInfo | ContainerInfo2> ----
enum CsInfoKind { CS_BLOBINFO, CS_CONTAINERINFO, CS_CONTAINERINFO2 };
struct CsInfo2 { HSTRING Name; UINT64 TotalSize; HSTRING DisplayName; INT64 LastModified; boolean NeedsSync; };
struct CsInfoVector : public CsBase<CsInfoVector> {
  std::vector<CsEntry> m_e; CsInfoKind m_kind = CS_BLOBINFO;
  void *Qi(REFIID) { return this; }
  void Fill(UINT32 i, void *out) {
    const CsEntry &e = m_e[i];
    if (m_kind == CS_CONTAINERINFO2) {
      CsInfo2 *o = (CsInfo2 *)out; CreateHString(e.name.c_str(), &o->Name); o->TotalSize = e.size;
      CreateHString(e.name.c_str(), &o->DisplayName);
      o->LastModified = ((INT64)e.mtime.dwHighDateTime << 32) | e.mtime.dwLowDateTime; o->NeedsSync = 0;
    } else {   // BlobInfo / ContainerInfo: { HSTRING Name; UINT32 Size; }
      CreateHString(e.name.c_str(), (HSTRING *)out); *(UINT32 *)((uint8_t *)out + 8) = (UINT32)e.size;
    }
  }
  size_t Stride() const { return m_kind == CS_CONTAINERINFO2 ? sizeof(CsInfo2) : 16; }
  virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 i, void *out) { CS_TRACE("GetAt"); if (i >= m_e.size()) return E_BOUNDS; Fill(i, out); return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32 *n) { CS_TRACE("get_Size"); if (n) *n = (UINT32)m_e.size(); return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE IndexOf(void *, UINT32 *idx, boolean *found) { CS_TRACE("IndexOf"); if (idx) *idx = 0; if (found) *found = 0; return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 cap, void *items, UINT32 *actual) { CS_TRACE("GetMany");
    UINT32 n = 0;
    for (UINT32 i = start; i < m_e.size() && n < cap; ++i, ++n) Fill(i, (uint8_t *)items + n * Stride());
    if (actual) *actual = n; return S_OK;
  }
};
static HRESULT CsInfoOp(void **op, CsInfoKind kind, std::vector<CsEntry> e, UINT32 start, UINT32 max) {
  CsInfoVector *v = new CsInfoVector(); v->m_kind = kind;
  for (UINT32 i = start; i < e.size() && (max == 0 || v->m_e.size() < max); ++i) v->m_e.push_back(e[i]);
  return CsMakeOp(op, CS_PTR, v);
}

// ---- BlobInfoQueryResult ----
struct CsBlobQuery : public CsBase<CsBlobQuery> {
  std::wstring m_dir, m_prefix;
  void *Qi(REFIID) { return this; }
  virtual HRESULT STDMETHODCALLTYPE GetBlobInfoAsync2(UINT32 start, UINT32 max, void **op) { CS_TRACE("GetBlobInfoAsync2"); return CsInfoOp(op, CS_BLOBINFO, CsList(m_dir, false, m_prefix), start, max); }
  virtual HRESULT STDMETHODCALLTYPE GetBlobInfoAsync(void **op) { CS_TRACE("GetBlobInfoAsync"); return CsInfoOp(op, CS_BLOBINFO, CsList(m_dir, false, m_prefix), 0, 0); }
  virtual HRESULT STDMETHODCALLTYPE GetItemCountAsync(void **op) { CS_TRACE("GetItemCountAsync"); return CsMakeOp(op, CS_U32, nullptr, (int64_t)CsList(m_dir, false, m_prefix).size()); }
};

// ---- ContainerInfoQueryResult (+ IContainerInfo2QueryResult) ----
struct CsCInfoQuery;
struct CsCInfo2Sub : public CsSub<CsCInfoQuery> {
  virtual HRESULT STDMETHODCALLTYPE GetContainerInfo2Async2(UINT32 start, UINT32 max, void **op);
  virtual HRESULT STDMETHODCALLTYPE GetContainerInfo2Async(void **op);
};
struct CsCInfoQuery : public CsBase<CsCInfoQuery> {
  std::wstring m_root, m_prefix; CsCInfo2Sub m_i2;
  CsCInfoQuery() { m_i2.m_p = this; }
  void *Qi(REFIID r) { if (memcmp(&r, &IID_CS_CInfo2Query, sizeof(GUID)) == 0) return &m_i2; return this; }
  virtual HRESULT STDMETHODCALLTYPE GetContainerInfoAsync2(UINT32 start, UINT32 max, void **op) { CS_TRACE("GetContainerInfoAsync2"); return CsInfoOp(op, CS_CONTAINERINFO, CsList(m_root, true, m_prefix), start, max); }
  virtual HRESULT STDMETHODCALLTYPE GetContainerInfoAsync(void **op) { CS_TRACE("GetContainerInfoAsync");
    auto l = CsList(m_root, true, m_prefix);
    log_line("[cs] GetContainerInfoAsync(prefix='%s') -> %u containers\r\n", CsNarrow(m_prefix).c_str(), (unsigned)l.size());
    return CsInfoOp(op, CS_CONTAINERINFO, l, 0, 0);
  }
  virtual HRESULT STDMETHODCALLTYPE GetItemCountAsync(void **op) { CS_TRACE("GetItemCountAsync"); return CsMakeOp(op, CS_U32, nullptr, (int64_t)CsList(m_root, true, m_prefix).size()); }
};
inline HRESULT STDMETHODCALLTYPE CsCInfo2Sub::GetContainerInfo2Async2(UINT32 start, UINT32 max, void **op) { CS_TRACE("CsCInfo2Sub::GetContainerInfo2Async2"); return CsInfoOp(op, CS_CONTAINERINFO2, CsList(m_p->m_root, true, m_p->m_prefix), start, max); }
inline HRESULT STDMETHODCALLTYPE CsCInfo2Sub::GetContainerInfo2Async(void **op) { CS_TRACE("CsCInfo2Sub::GetContainerInfo2Async"); return CsInfoOp(op, CS_CONTAINERINFO2, CsList(m_p->m_root, true, m_p->m_prefix), 0, 0); }

// ---- ConnectedStorageContainer (+ IConnectedStorageContainer2) ----
struct CsSpace;
static CsSpace *CsNewSpace(bool machine);
struct CsContainer;
struct CsContainer2Sub : public CsSub<CsContainer> {
  virtual HRESULT STDMETHODCALLTYPE SubmitUpdatesAsync(IUnknown *writes, IUnknown *deletes, HSTRING displayName, void **op);
  virtual HRESULT STDMETHODCALLTYPE SubmitPropertySetUpdatesAsync(IUnknown *ps, IUnknown *deletes, HSTRING displayName, void **op);
  virtual HRESULT STDMETHODCALLTYPE CreateBlobInfoQuery(HSTRING prefix, void **q);
};
struct CsContainer : public CsBase<CsContainer> {
  std::wstring m_name, m_root; bool m_machine = false; CsContainer2Sub m_c2;
  CsContainer() { m_c2.m_p = this; }
  void *Qi(REFIID r) { if (memcmp(&r, &IID_CS_Container2, sizeof(GUID)) == 0) return &m_c2; return this; }
  std::wstring Dir() const { return m_root + L"\\" + CsEncode(m_name); }
  HRESULT Submit(IUnknown *writes, IUnknown *deletes, void **op) {
    std::wstring dir = Dir(); CreateDirectoryW(dir.c_str(), nullptr);
    HRESULT err = S_OK; unsigned nw = 0, nd = 0; uint64_t bytes = 0;
    if (!CsForEachBlob(writes, [&](const std::wstring &k, IUnknown *buf) {
          UINT32 len = 0; if (buf) ((PFN_CsGetU32)CsVt(buf)[7])(buf, &len);   // IBuffer::get_Length
          BYTE *p = CsBufData(buf);
          if (!CsWriteFile(dir + L"\\" + CsEncode(k), p, p ? len : 0)) err = E_FAIL;
          ++nw; bytes += len;
        })) err = E_FAIL;
    CsForEachString(deletes, [&](const std::wstring &k) { DeleteFileW((dir + L"\\" + CsEncode(k)).c_str()); ++nd; });
    log_line("[cs] SubmitUpdates '%s': %u blobs written (%llu bytes), %u deleted -> 0x%08lX\r\n",
             CsNarrow(m_name).c_str(), nw, (unsigned long long)bytes, nd, (unsigned long)err);
    return CsMakeOp(op, CS_VOID, nullptr, 0, err);
  }
  // Slot 6..11: IConnectedStorageContainer
  virtual HRESULT STDMETHODCALLTYPE get_Name(HSTRING *n) { CS_TRACE("get_Name"); return CreateHString(m_name.c_str(), n); }
  virtual HRESULT STDMETHODCALLTYPE get_OwningSpace(void **s);
  virtual HRESULT STDMETHODCALLTYPE SubmitUpdatesAsync(IUnknown *writes, IUnknown *deletes, void **op) { CS_TRACE("SubmitUpdatesAsync"); return Submit(writes, deletes, op); }
  virtual HRESULT STDMETHODCALLTYPE ReadAsync(IUnknown *reads, void **op) { CS_TRACE("ReadAsync");
    std::wstring dir = Dir(); HRESULT err = S_OK; unsigned n = 0;
    CsForEachBlob(reads, [&](const std::wstring &k, IUnknown *buf) {
      std::vector<uint8_t> d;
      if (!CsReadFile(dir + L"\\" + CsEncode(k), d)) { err = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND); log_line("[cs] Read '%s/%s': not found\r\n", CsNarrow(m_name).c_str(), CsNarrow(k).c_str()); return; }
      UINT32 cap = 0; ((PFN_CsGetU32)CsVt(buf)[6])(buf, &cap);           // IBuffer::get_Capacity
      BYTE *p = CsBufData(buf);
      if (!p || cap < d.size()) { err = E_NOT_SUFFICIENT_BUFFER; log_line("[cs] Read '%s': buffer %u < %u\r\n", CsNarrow(k).c_str(), cap, (unsigned)d.size()); return; }
      memcpy(p, d.data(), d.size());
      ((PFN_CsPutU32)CsVt(buf)[8])(buf, (UINT32)d.size());                // IBuffer::put_Length
      ++n;
    });
    log_line("[cs] ReadAsync '%s': %u blobs -> 0x%08lX\r\n", CsNarrow(m_name).c_str(), n, (unsigned long)err);
    return CsMakeOp(op, CS_VOID, nullptr, 0, err);
  }
  virtual HRESULT STDMETHODCALLTYPE GetAsync(IUnknown *keys, void **op) { CS_TRACE("GetAsync");
    std::wstring dir = Dir(); CsBlobMap *m = new CsBlobMap(); HRESULT err = S_OK;
    CsForEachString(keys, [&](const std::wstring &k) {
      std::vector<uint8_t> d;
      if (!CsReadFile(dir + L"\\" + CsEncode(k), d)) { err = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND); return; }
      CsBuffer *b = new CsBuffer(); b->m_d = d; b->m_len = (UINT32)d.size();
      m->m_items.push_back({ k, b });
    });
    log_line("[cs] GetAsync '%s': %u blobs -> 0x%08lX\r\n", CsNarrow(m_name).c_str(), (unsigned)m->m_items.size(), (unsigned long)err);
    if (FAILED(err)) { m->Release(); return CsMakeOp(op, CS_PTR, nullptr, 0, err); }
    return CsMakeOp(op, CS_PTR, m);
  }
  virtual HRESULT STDMETHODCALLTYPE SubmitPropertySetUpdatesAsync(IUnknown *, IUnknown *, void **op) { CS_TRACE("SubmitPropertySetUpdatesAsync");
    log_line("[cs] SubmitPropertySetUpdatesAsync: not implemented\r\n");
    return CsMakeOp(op, CS_VOID, nullptr, 0, E_NOTIMPL);
  }
};
inline HRESULT STDMETHODCALLTYPE CsContainer2Sub::SubmitUpdatesAsync(IUnknown *w, IUnknown *d, HSTRING, void **op) { CS_TRACE("CsContainer2Sub::SubmitUpdatesAsync"); return m_p->Submit(w, d, op); }
inline HRESULT STDMETHODCALLTYPE CsContainer2Sub::SubmitPropertySetUpdatesAsync(IUnknown *, IUnknown *, HSTRING, void **op) { CS_TRACE("CsContainer2Sub::SubmitPropertySetUpdatesAsync");
  log_line("[cs] SubmitPropertySetUpdatesAsync(2): not implemented\r\n");
  return CsMakeOp(op, CS_VOID, nullptr, 0, E_NOTIMPL);
}
inline HRESULT STDMETHODCALLTYPE CsContainer2Sub::CreateBlobInfoQuery(HSTRING prefix, void **q) { CS_TRACE("CsContainer2Sub::CreateBlobInfoQuery");
  if (!q) return E_POINTER;
  CsBlobQuery *b = new CsBlobQuery(); b->m_dir = m_p->Dir(); b->m_prefix = CsStr(prefix); *q = b;
  log_line("[cs] BlobInfoQuery '%s' prefix='%s' -> %u blobs\r\n", CsNarrow(m_p->m_name).c_str(), CsNarrow(b->m_prefix).c_str(),
           (unsigned)CsList(b->m_dir, false, b->m_prefix).size());
  return S_OK;
}

// ---- ConnectedStorageSpace (+ IConnectedStorageSpace2) ----
struct CsSpace2Sub : public CsSub<CsSpace> {
  virtual HRESULT STDMETHODCALLTYPE GetRemainingBytesInQuota64Async(void **op) { CS_TRACE("GetRemainingBytesInQuota64Async"); return CsMakeOp(op, CS_I64, nullptr, 1ll << 30); }
};
struct CsSpace : public CsBase<CsSpace> {
  bool m_machine = false; std::wstring m_root; CsSpace2Sub m_s2;
  CsSpace() { m_s2.m_p = this; }
  void *Qi(REFIID r) { if (memcmp(&r, &IID_CS_Space2, sizeof(GUID)) == 0) return &m_s2; return this; }
  virtual HRESULT STDMETHODCALLTYPE get_User(void **u) { CS_TRACE("get_User"); if (u) *u = nullptr; return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE get_ServiceConfigurationId(HSTRING *s) { CS_TRACE("get_ServiceConfigurationId"); return CreateHString(L"00000000-0000-0000-0000-000000000000", s); }
  virtual HRESULT STDMETHODCALLTYPE get_IsReadOnly(boolean *b) { CS_TRACE("get_IsReadOnly"); if (b) *b = 0; return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE CreateContainer(HSTRING name, void **c) { CS_TRACE("CreateContainer");
    if (!c) return E_POINTER;
    CsContainer *k = new CsContainer(); k->m_name = CsStr(name); k->m_root = m_root; k->m_machine = m_machine;
    log_line("[cs] CreateContainer('%s')\r\n", CsNarrow(k->m_name).c_str());
    *c = k; return S_OK;
  }
  virtual HRESULT STDMETHODCALLTYPE DeleteContainerAsync(HSTRING name, void **op) { CS_TRACE("DeleteContainerAsync");
    std::wstring dir = m_root + L"\\" + CsEncode(CsStr(name));
    for (auto &b : CsList(dir, false, L"")) DeleteFileW((dir + L"\\" + CsEncode(b.name)).c_str());
    RemoveDirectoryW(dir.c_str());
    log_line("[cs] DeleteContainer('%s')\r\n", CsNarrow(CsStr(name)).c_str());
    return CsMakeOp(op, CS_VOID);
  }
  virtual HRESULT STDMETHODCALLTYPE CreateContainerInfoQuery(HSTRING prefix, void **q) { CS_TRACE("CreateContainerInfoQuery");
    if (!q) return E_POINTER;
    CsCInfoQuery *k = new CsCInfoQuery(); k->m_root = m_root; k->m_prefix = CsStr(prefix); *q = k; return S_OK;
  }
  virtual HRESULT STDMETHODCALLTYPE GetRemainingBytesInQuotaAsync(void **op) { CS_TRACE("GetRemainingBytesInQuotaAsync"); return CsMakeOp(op, CS_I32, nullptr, 256 << 20); }
};
static CsSpace *CsNewSpace(bool machine) {
  CsSpace *s = new CsSpace(); s->m_machine = machine; s->m_root = CsRoot(machine);
  log_line("[cs] ConnectedStorageSpace (%s) -> %s\r\n", machine ? "machine" : "user", CsNarrow(s->m_root).c_str());
  return s;
}
inline HRESULT STDMETHODCALLTYPE CsContainer::get_OwningSpace(void **s) { CS_TRACE("CsContainer::get_OwningSpace"); if (!s) return E_POINTER; *s = CsNewSpace(m_machine); return S_OK; }

// ---- ConnectedStorageSpace statics (activation factory) ----
struct CsStatics;
struct CsStatics2Sub : public CsSub<CsStatics> {
  virtual HRESULT STDMETHODCALLTYPE GetSyncOnDemandForUserAsync(void *, void **op) { CS_TRACE("GetSyncOnDemandForUserAsync"); return CsMakeOp(op, CS_PTR, CsNewSpace(false)); }
  virtual HRESULT STDMETHODCALLTYPE GetSyncOnDemandForUserAsync2(void *, HSTRING, void **op) { CS_TRACE("GetSyncOnDemandForUserAsync2"); return CsMakeOp(op, CS_PTR, CsNewSpace(false)); }
};
struct CsStatics : public IInspectable {
  CsStatics2Sub m_s2;
  CsStatics() { m_s2.m_p = this; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void **p) override {
    if (!p) return E_POINTER;
    *p = memcmp(&r, &IID_CS_Statics2, sizeof(GUID)) == 0 ? (void *)&m_s2 : (void *)this;
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE GetIids(ULONG *c, IID **i) override { if (c) *c = 0; if (i) *i = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *n) override { if (n) *n = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *l) override { if (l) *l = BaseTrust; return S_OK; }
  // Slot 6..9: IConnectedStorageSpaceStatics
  virtual HRESULT STDMETHODCALLTYPE GetForUserAsync(void *, void **op) { CS_TRACE("GetForUserAsync"); return CsMakeOp(op, CS_PTR, CsNewSpace(false)); }
  virtual HRESULT STDMETHODCALLTYPE GetForUserAsync2(void *, HSTRING, void **op) { CS_TRACE("GetForUserAsync2"); return CsMakeOp(op, CS_PTR, CsNewSpace(false)); }
  virtual HRESULT STDMETHODCALLTYPE GetForMachineAsync(void **op) { CS_TRACE("GetForMachineAsync"); return CsMakeOp(op, CS_PTR, CsNewSpace(true)); }
  virtual HRESULT STDMETHODCALLTYPE GetForMachineAsync2(HSTRING, void **op) { CS_TRACE("GetForMachineAsync2"); return CsMakeOp(op, CS_PTR, CsNewSpace(true)); }
};
static CsStatics g_csStatics;

static bool CsEnabled() {   // ONRUSH_CONNECTED_STORAGE=0 brings back the old failing mock
  wchar_t v[8] = {};
  return !(GetEnvironmentVariableW(L"ONRUSH_CONNECTED_STORAGE", v, 8) > 0 && v[0] == L'0');
}
