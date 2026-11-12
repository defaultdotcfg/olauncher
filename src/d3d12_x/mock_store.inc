// Mock for Windows.ApplicationModel.Store.CurrentApp (used by the 1.80 build for its license check).
// On PC the real CurrentApp has no Store identity and get_LicenseInformation fails with
// 0x80070490, which the game turns into a fatal __abi_WinRTraiseCOMException. We report a full,
// active, non-trial license with no product (DLC) licenses listed.
//
// ICurrentApp (IInspectable + ...): 6 get_LicenseInformation, 7 get_LinkUri, 8 get_AppId,
//   9 RequestAppPurchaseAsync, 10 RequestProductPurchaseAsync, 11 LoadListingInformationAsync,
//   12 GetAppReceiptAsync, 13 GetProductReceiptAsync
// ILicenseInformation: 6 get_ProductLicenses, 7 get_IsActive, 8 get_IsTrial, 9 get_ExpirationDate,
//   10 add_LicenseChanged, 11 remove_LicenseChanged
// IMapView<HSTRING, ProductLicense>: 6 Lookup, 7 get_Size, 8 HasKey, 9 Split

class MockStoreBase : public IInspectable {
protected:
  const char *m_name;
  explicit MockStoreBase(const char *n) : m_name(n) {}
public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    // The game asks for the exact interface it then calls; every other WinRT helper interface
    // (IIterable, IWeakReferenceSource, IMarshal...) is refused.
    static const GUID kIterable = {0xfaa585ea, 0x6214, 0x4217, {0xaf, 0xda, 0x7f, 0x46, 0xde, 0x58, 0x69, 0xb3}};
    static const GUID kWeakSrc = {0x00000038, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
    static const GUID kMarshal = {0x00000003, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
    if (riid == kIterable || riid == kWeakSrc || riid == kMarshal) { *ppv = nullptr; return E_NOINTERFACE; }
    if (riid != __uuidof(IUnknown) && riid != IID_IInspectable && riid != IID_IAgileObject) {
      static volatile LONG s_n = 0;
      if (InterlockedIncrement(&s_n) <= 8) log_guid(m_name, &riid);
    }
    *ppv = this;
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE GetIids(ULONG *count, IID **iids) override {
    if (count) *count = 0;
    if (iids) *iids = nullptr;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override { if (name) *name = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *level) override { if (level) *level = BaseTrust; return S_OK; }
};

class MockProductLicenses : public MockStoreBase {
public:
  MockProductLicenses() : MockStoreBase("[store] ProductLicenses QI") {}
  virtual HRESULT STDMETHODCALLTYPE Lookup(HSTRING, void **item) {
    if (item) *item = nullptr;
    return E_BOUNDS;
  }
  virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32 *size) { if (size) *size = 0; return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE HasKey(HSTRING, boolean *found) { if (found) *found = false; return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE Split(void **a, void **b) {
    if (a) *a = nullptr;
    if (b) *b = nullptr;
    return S_OK;
  }
};
static MockProductLicenses g_mockProductLicenses;

class MockLicenseInformation : public MockStoreBase {
public:
  MockLicenseInformation() : MockStoreBase("[store] LicenseInformation QI") {}
  virtual HRESULT STDMETHODCALLTYPE get_ProductLicenses(void **value) {
    if (!value) return E_POINTER;
    *value = &g_mockProductLicenses;
    return S_OK;
  }
  virtual HRESULT STDMETHODCALLTYPE get_IsActive(boolean *v) { if (v) *v = true; return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE get_IsTrial(boolean *v) { if (v) *v = false; return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE get_ExpirationDate(INT64 *v) {
    if (v) *v = 0x7FFFFFFFFFFFFFFFLL;  // DateTime.MaxValue-like: never expires
    return S_OK;
  }
  virtual HRESULT STDMETHODCALLTYPE add_LicenseChanged(void *, INT64 *token) { if (token) *token = 1; return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE remove_LicenseChanged(INT64) { return S_OK; }
};
static MockLicenseInformation g_mockLicenseInformation;

class MockCurrentAppStatics : public MockStoreBase {
public:
  MockCurrentAppStatics() : MockStoreBase("[store] CurrentApp QI") {}
  virtual HRESULT STDMETHODCALLTYPE get_LicenseInformation(void **value) {
    if (!value) return E_POINTER;
    static volatile LONG s_n = 0;
    if (InterlockedIncrement(&s_n) <= 3) log_line("[store] CurrentApp::get_LicenseInformation -> full license\r\n");
    *value = &g_mockLicenseInformation;
    return S_OK;
  }
  virtual HRESULT STDMETHODCALLTYPE get_LinkUri(void **v) { if (v) *v = nullptr; return E_NOTIMPL; }
  virtual HRESULT STDMETHODCALLTYPE get_AppId(GUID *v) { if (v) *v = GUID{}; return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE RequestAppPurchaseAsync(boolean, void **op) { return StoreNotImpl("RequestAppPurchaseAsync", op); }
  virtual HRESULT STDMETHODCALLTYPE RequestProductPurchaseAsync(HSTRING, boolean, void **op) { return StoreNotImpl("RequestProductPurchaseAsync", op); }
  virtual HRESULT STDMETHODCALLTYPE LoadListingInformationAsync(void **op) { return StoreNotImpl("LoadListingInformationAsync", op); }
  virtual HRESULT STDMETHODCALLTYPE GetAppReceiptAsync(void **op) { return StoreNotImpl("GetAppReceiptAsync", op); }
  virtual HRESULT STDMETHODCALLTYPE GetProductReceiptAsync(HSTRING, void **op) { return StoreNotImpl("GetProductReceiptAsync", op); }
private:
  static HRESULT StoreNotImpl(const char *what, void **op) {
    if (op) *op = nullptr;
    log_line("[store] CurrentApp::%s -> E_NOTIMPL\r\n", what);
    return E_NOTIMPL;
  }
};
static MockCurrentAppStatics g_mockCurrentAppStatics;
