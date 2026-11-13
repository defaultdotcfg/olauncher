// ============================================================================
// "Fotos" tab: the shots taken in game (F12 / photo mode), from <game dir>\fotos
// (bridge: src/d3d12_x/photo_capture.h). Thumbnail gallery over the art with check boxes;
// mark all / unmark, open, delete the marked ones (Recycle Bin) and open the folder.
// ============================================================================
#define ID_TAB_PHOTOS    1080
#define ID_PH_LIST       1081
#define ID_PH_ALL        1082
#define ID_PH_OPEN       1083
#define ID_PH_DELETE     1084
#define ID_PH_FOLDER     1085
#define PH_THUMB_W       160
#define PH_THUMB_H       90

static HWND g_hTabPhotos = nullptr, g_hPhList = nullptr, g_hPhAll = nullptr, g_hPhOpen = nullptr, g_hPhDelete = nullptr, g_hPhFolder = nullptr;
static HIMAGELIST g_phImages = nullptr;
struct PhotoEntry { std::wstring name; ULONGLONG size; FILETIME time; };
static std::vector<PhotoEntry> g_photos;
static std::wstring g_phLoadedKey;   // folder + names + sizes of what the gallery shows

enum PhotoStr { F_TAB, F_TITLE, F_HINT, F_COUNT_FMT, F_MARKED_FMT, F_ALL, F_NONE, F_OPEN, F_DELETE, F_FOLDER, F_EMPTY,
                F_CONFIRM_DEL, F_PICK, F_COUNT };
static const wchar_t *kPhotoStr[F_COUNT][3] = {
    { L"Fotos", L"Photos", L"Fotos" },
    { L"FOTOS", L"PHOTOS", L"FOTOS" },
    { L"As fotos tiradas no jogo (F12 ou modo foto) ficam aqui. Marque as caixas para escolher várias.",
      L"Shots taken in game (F12 or photo mode) show up here. Tick the boxes to pick several.",
      L"Las fotos tomadas en el juego (F12 o modo foto) aparecen aquí. Marca las casillas para elegir varias." },
    { L"%d foto(s) · %ls", L"%d photo(s) · %ls", L"%d foto(s) · %ls" },
    { L"%d marcada(s)", L"%d ticked", L"%d marcada(s)" },
    { L"Marcar todas", L"Tick all", L"Marcar todas" },
    { L"Desmarcar todas", L"Untick all", L"Desmarcar todas" },
    { L"Abrir foto", L"Open photo", L"Abrir foto" },
    { L"Excluir marcadas", L"Delete ticked", L"Eliminar marcadas" },
    { L"Abrir pasta das fotos", L"Open photos folder", L"Abrir carpeta de fotos" },
    { L"Nenhuma foto ainda. No jogo, aperte F12 (ou use o modo foto) para tirar uma.",
      L"No photos yet. In game, press F12 (or use photo mode) to take one.",
      L"Todavía no hay fotos. En el juego, pulsa F12 (o usa el modo foto) para tomar una." },
    { L"Excluir %d foto(s)?\n\nElas vão para a Lixeira (dá para restaurar de lá).",
      L"Delete %d photo(s)?\n\nThey go to the Recycle Bin (they can be restored from there).",
      L"¿Eliminar %d foto(s)?\n\nVan a la Papelera (se pueden restaurar desde allí)." },
    { L"Marque ou selecione uma foto primeiro.", L"Tick or select a photo first.", L"Marca o selecciona una foto primero." },
};
static const wchar_t *FT(PhotoStr s) { return kPhotoStr[s][g_uiLang]; }

static std::wstring PhDir()
{
    wchar_t d[MAX_PATH];
    swprintf_s(d, MAX_PATH, L"%ls\\fotos", g_gameDirShown[0] ? g_gameDirShown : g_exeDir);
    return d;
}

static std::wstring PhFmtSize(ULONGLONG b)
{
    wchar_t t[32];
    if (b >= (1ull << 30)) swprintf_s(t, L"%.1f GB", b / 1073741824.0);
    else if (b >= (1ull << 20)) swprintf_s(t, L"%.1f MB", b / 1048576.0);
    else swprintf_s(t, L"%llu KB", (b + 1023) / 1024);
    return t;
}

static void PhScan()
{
    g_photos.clear();
    std::wstring dir = PhDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const wchar_t *ext = PathFindExtensionW(fd.cFileName);
        if (_wcsicmp(ext, L".png") && _wcsicmp(ext, L".jpg") && _wcsicmp(ext, L".jpeg") && _wcsicmp(ext, L".bmp")) continue;
        g_photos.push_back({ fd.cFileName, ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow, fd.ftLastWriteTime });
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    // newest first
    std::sort(g_photos.begin(), g_photos.end(), [](const PhotoEntry &a, const PhotoEntry &b) { return CompareFileTime(&a.time, &b.time) > 0; });
}

// Letterboxed thumbnail of an image file (the file is closed again before returning).
static HBITMAP PhThumb(const std::wstring &path)
{
    Gdiplus::Bitmap thumb(PH_THUMB_W, PH_THUMB_H, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&thumb);
        g.Clear(GPC(g_pal.heroDim));
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
        Gdiplus::Bitmap *src = Gdiplus::Bitmap::FromFile(path.c_str());
        if (src && src->GetLastStatus() == Gdiplus::Ok && src->GetWidth() && src->GetHeight())
        {
            float sc = min((float)PH_THUMB_W / src->GetWidth(), (float)PH_THUMB_H / src->GetHeight());
            float w = src->GetWidth() * sc, hgt = src->GetHeight() * sc;
            g.DrawImage(src, Gdiplus::RectF((PH_THUMB_W - w) / 2.f, (PH_THUMB_H - hgt) / 2.f, w, hgt));
        }
        delete src;
    }
    HBITMAP hb = nullptr;
    thumb.GetHBITMAP(GPC(g_pal.heroDim), &hb);
    return hb;
}

static int PhCheckedCount()
{
    int n = 0, c = g_hPhList ? ListView_GetItemCount(g_hPhList) : 0;
    for (int i = 0; i < c; ++i) if (ListView_GetCheckState(g_hPhList, i)) ++n;
    return n;
}

// Reloads the gallery when the folder content changed (or always with force).
static void PhRefresh(bool force)
{
    PhScan();
    std::wstring key = PhDir();
    for (auto &p : g_photos) key += L"|" + p.name + L":" + std::to_wstring(p.size);
    if (!force && key == g_phLoadedKey) return;
    g_phLoadedKey = key;
    HCURSOR old = SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_WAIT));
    SendMessageW(g_hPhList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hPhList);
    ImageList_RemoveAll(g_phImages);
    std::wstring dir = PhDir();
    for (size_t i = 0; i < g_photos.size(); ++i)
    {
        HBITMAP hb = PhThumb(dir + L"\\" + g_photos[i].name);
        int img = hb ? ImageList_Add(g_phImages, hb, nullptr) : -1;
        if (hb) DeleteObject(hb);
        // label: date and time from the OnRush_YYYY-MM-DD_HH-MM-SS name, else the file name
        std::wstring label = g_photos[i].name;
        int y, mo, d, hh, mi, ss;
        if (swscanf_s(label.c_str(), L"OnRush_%d-%d-%d_%d-%d-%d", &y, &mo, &d, &hh, &mi, &ss) == 6)
        { wchar_t t[64]; swprintf_s(t, L"%02d/%02d/%04d %02d:%02d", d, mo, y, hh, mi); label = t; }
        LVITEMW it = {};
        it.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        it.iItem = (int)i; it.pszText = (LPWSTR)label.c_str(); it.iImage = img; it.lParam = (LPARAM)i;
        SendMessageW(g_hPhList, LVM_INSERTITEMW, 0, (LPARAM)&it);
    }
    SendMessageW(g_hPhList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hPhList, nullptr, TRUE);
    SetCursor(old);
}

static std::vector<int> PhTargets()
{
    std::vector<int> v;
    int c = ListView_GetItemCount(g_hPhList);
    for (int i = 0; i < c; ++i) if (ListView_GetCheckState(g_hPhList, i)) v.push_back(i);
    if (v.empty())
        for (int i = -1; (i = ListView_GetNextItem(g_hPhList, i, LVNI_SELECTED)) >= 0;) v.push_back(i);
    return v;
}

static void PhOpen(HWND hwnd, int row)
{
    if (row < 0 || row >= (int)g_photos.size()) { MessageBoxW(hwnd, FT(F_PICK), L"ONRUSH", MB_ICONINFORMATION); return; }
    ShellExecuteW(hwnd, L"open", (PhDir() + L"\\" + g_photos[row].name).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void PhDelete(HWND hwnd)
{
    std::vector<int> rows = PhTargets();
    if (rows.empty()) { MessageBoxW(hwnd, FT(F_PICK), L"ONRUSH", MB_ICONINFORMATION); return; }
    wchar_t m[256]; swprintf_s(m, FT(F_CONFIRM_DEL), (int)rows.size());
    if (MessageBoxW(hwnd, m, L"ONRUSH", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
    std::wstring list, dir = PhDir();
    for (int r : rows) { list += dir + L"\\" + g_photos[r].name; list.push_back(L'\0'); }
    list.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.hwnd = hwnd; op.wFunc = FO_DELETE; op.pFrom = list.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;  // Recycle Bin; the launcher already asked
    int r = SHFileOperationW(&op);
    LogPrintf(L"[Fotos] %d foto(s) enviada(s) para a Lixeira%ls\r\n", (int)rows.size(), r ? L" (com erro)" : L"");
    PhRefresh(true);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void PhApplyThemeColors()
{
    if (!g_hPhList) return;
    ListView_SetBkColor(g_hPhList, g_pal.heroDim);
    ListView_SetTextBkColor(g_hPhList, g_pal.heroDim);
    ListView_SetTextColor(g_hPhList, COLOR_TEXT_PRIMARY);
    g_phLoadedKey.clear();   // thumbnails are letterboxed with the background colour: rebuild them
    if (g_tab == 4) PhRefresh(true);
}

static void PhCreateControls(HWND hwnd)
{
    const int X = UI_PANEL_X, W = UI_PANEL_W;
    DWORD od = WS_CHILD | WS_TABSTOP | BS_OWNERDRAW;
    g_hTabPhotos = CreateWindowW(L"BUTTON", L"", od | WS_VISIBLE, 16, PLAYLIST_HIDDEN ? 276 : 324, 184, 42, hwnd, (HMENU)ID_TAB_PHOTOS, nullptr, nullptr);
    g_hPhList = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_TABSTOP | LVS_ICON | LVS_AUTOARRANGE | LVS_SHOWSELALWAYS,
                                220, 64, UI_HERO_W - 226, UI_H - 76, hwnd, (HMENU)ID_PH_LIST, nullptr, nullptr);
    SetWindowTheme(g_hPhList, g_pal.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    ListView_SetExtendedListViewStyle(g_hPhList, LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
    PhApplyThemeColors();
    g_phImages = ImageList_Create(PH_THUMB_W, PH_THUMB_H, ILC_COLOR32, 16, 16);
    ListView_SetImageList(g_hPhList, g_phImages, LVSIL_NORMAL);
    ListView_SetIconSpacing(g_hPhList, PH_THUMB_W + 12, PH_THUMB_H + 40);
    g_hPhAll = CreateWindowW(L"BUTTON", L"", od, X, 150, W, 32, hwnd, (HMENU)ID_PH_ALL, nullptr, nullptr);
    g_hPhOpen = CreateWindowW(L"BUTTON", L"", od, X, 190, W, 32, hwnd, (HMENU)ID_PH_OPEN, nullptr, nullptr);
    g_hPhDelete = CreateWindowW(L"BUTTON", L"", od, X, 230, W, 32, hwnd, (HMENU)ID_PH_DELETE, nullptr, nullptr);
    g_hPhFolder = CreateWindowW(L"BUTTON", L"", od, X, 290, W, 32, hwnd, (HMENU)ID_PH_FOLDER, nullptr, nullptr);
}

static void PhApplyLanguage()
{
    SetWindowTextW(g_hTabPhotos, FT(F_TAB));
    SetWindowTextW(g_hPhAll, PhCheckedCount() && PhCheckedCount() == ListView_GetItemCount(g_hPhList) ? FT(F_NONE) : FT(F_ALL));
    SetWindowTextW(g_hPhOpen, FT(F_OPEN));
    SetWindowTextW(g_hPhDelete, FT(F_DELETE));
    SetWindowTextW(g_hPhFolder, FT(F_FOLDER));
}

static void PhUpdateVisibility(HWND hwnd)
{
    bool on = g_tab == 4;
    HWND c[] = { g_hPhAll, g_hPhOpen, g_hPhDelete, g_hPhFolder };
    for (HWND h : c) if (h) ShowWindow(h, on ? SW_SHOW : SW_HIDE);
    if (on) PhRefresh(false);
    if (g_hPhList) ShowWindow(g_hPhList, on && !g_photos.empty() ? SW_SHOW : SW_HIDE);
    if (on) PhApplyLanguage();
    InvalidateRect(g_hTabPhotos, nullptr, TRUE);
}

// Left area while the tab is open: title over the gallery (or the empty state).
static void PhDrawHero(Gdiplus::Graphics &g)
{
    Gdiplus::SolidBrush dim(GPC(g_pal.heroDim));
    g.FillRectangle(&dim, 216, 0, UI_HERO_W - 216, UI_H);
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font fTitle(&ff, 22, Gdiplus::FontStyleBold, Gdiplus::UnitPixel), fSmall(&ff, 13, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush white(GPC(COLOR_TEXT_PRIMARY)), muted(GPC(COLOR_TEXT_MUTED));
    g.DrawString(FT(F_TITLE), -1, &fTitle, Gdiplus::PointF(232, 18), &white);
    ULONGLONG total = 0; for (auto &p : g_photos) total += p.size;
    wchar_t info[96]; swprintf_s(info, FT(F_COUNT_FMT), (int)g_photos.size(), PhFmtSize(total).c_str());
    Gdiplus::StringFormat right; right.SetAlignment(Gdiplus::StringAlignmentFar);
    g.DrawString(info, -1, &fSmall, Gdiplus::RectF(300, 26, UI_HERO_W - 320.f, 20), &right, &muted);
    if (g_photos.empty())
    {
        Gdiplus::StringFormat center; center.SetAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(FT(F_EMPTY), -1, &fSmall, Gdiplus::RectF(250, 280, UI_HERO_W - 284.f, 80), &center, &muted);
    }
}

static void PhDrawPanel(HDC hdc)
{
    const int X = UI_PANEL_X, W = UI_PANEL_W;
    SelectObject(hdc, g_hFontPlay); SetTextColor(hdc, COLOR_TEXT_PRIMARY);
    TextOutW(hdc, X, 20, FT(F_TITLE), (int)wcslen(FT(F_TITLE)));
    SelectObject(hdc, g_hFontUI); SetTextColor(hdc, COLOR_TEXT_MUTED);
    RECT hint = { X, 54, X + W, 110 };
    DrawTextW(hdc, FT(F_HINT), -1, &hint, DT_LEFT | DT_WORDBREAK);
    wchar_t t[64]; swprintf_s(t, FT(F_MARKED_FMT), PhCheckedCount());
    SetTextColor(hdc, COLOR_TEXT_PRIMARY);
    TextOutW(hdc, X, 120, t, (int)wcslen(t));
}

static bool PhHandleCommand(HWND hwnd, int id, int code)
{
    switch (id)
    {
    case ID_TAB_PHOTOS: PlSetTab(hwnd, 4); return true;
    case ID_PH_ALL:
    {
        int c = ListView_GetItemCount(g_hPhList);
        BOOL mark = !(PhCheckedCount() == c && c > 0);
        for (int i = 0; i < c; ++i) ListView_SetCheckState(g_hPhList, i, mark);
        PhApplyLanguage();
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    case ID_PH_OPEN:
    {
        std::vector<int> rows = PhTargets();
        PhOpen(hwnd, rows.empty() ? -1 : rows[0]);
        return true;
    }
    case ID_PH_DELETE: PhDelete(hwnd); return true;
    case ID_PH_FOLDER:
    {
        std::wstring d = PhDir();
        SHCreateDirectoryExW(nullptr, d.c_str(), nullptr);
        ShellExecuteW(hwnd, L"open", d.c_str(), nullptr, nullptr, SW_SHOW);
        return true;
    }
    }
    (void)code;
    return false;
}

static bool PhHandleNotify(HWND hwnd, NMHDR *nh)
{
    if (!g_hPhList || nh->hwndFrom != g_hPhList) return false;
    if (nh->code == NM_DBLCLK) PhOpen(hwnd, ((NMITEMACTIVATE *)nh)->iItem);
    else if (nh->code == LVN_KEYDOWN && ((NMLVKEYDOWN *)nh)->wVKey == VK_DELETE) PhDelete(hwnd);
    else if (nh->code == LVN_ITEMCHANGED && (((NMLISTVIEW *)nh)->uChanged & LVIF_STATE))
    {   // check box ticked/unticked: update the counter and the "tick all" label
        RECT r = { UI_PANEL_X, 116, UI_PANEL_X + UI_PANEL_W, 140 };
        InvalidateRect(hwnd, &r, FALSE);
        PhApplyLanguage();
    }
    return true;
}
