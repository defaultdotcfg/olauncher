// ============================================================================
// Launcher UI (v2): game art on the left, settings panel on the right.
// All buttons/toggles/combo items are owner-drawn so the dark theme is consistent.
// ============================================================================
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
// Common Controls v6 (themed scrollbars/combos; needed for the DarkMode_* themes)
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define ID_BTN_ADVANCED   1020
#define ID_BTN_SHOW_LOG   1021
#define ID_DISCORD_CHK    1022
#define ID_GAMEPLAY_CHK   1023
#define ID_SKIPINTRO_CHK  1024
#include <shobjidl.h>
#include "discord_rpc.inc"
#include "update_check.inc"
#include "self_update.inc"
#define ID_BTN_UPDATE     1030
static HWND g_hBtnUpdate = nullptr;

// Layout (client area)
#define UI_W        1000
#define UI_H        680
#define UI_HERO_W   600
#define UI_PANEL_X  (UI_HERO_W + 28)
#define UI_PANEL_W  (UI_W - UI_HERO_W - 56)

static ULONG_PTR g_gdiplusToken = 0;
static Gdiplus::Bitmap *g_heroArt = nullptr;
static bool g_vsyncOn = true, g_d3dDebugOn = false, g_verboseOn = false, g_quickGameplayOn = false, g_skipIntroOn = false;
static HWND g_hGameplayChk = nullptr, g_hSkipIntroChk = nullptr;
static bool g_showAdvanced = false, g_showLog = false;
static bool g_discordOn = true;
static char g_discordAppId[64] = {};
static HWND g_hDiscordChk = nullptr;
static HWND g_hAdvBtn = nullptr, g_hLogBtn = nullptr, g_hClearLogBtn = nullptr, g_hCopyLogBtn = nullptr;
static bool g_gameDirOk = false;
static wchar_t g_gameDirShown[MAX_PATH] = {};
static HFONT g_hFontSmallCaps = nullptr, g_hFontPlay = nullptr;

// ---- Launcher language (pt / en / es) -------------------------------------------------------
#define ID_UILANG_COMBO    1025
#define ID_TEXTLANG_COMBO  1026
#define ID_VOICELANG_COMBO 1027
#define ID_PLAYER_EDIT     1028
#define ID_PHOTOFMT_COMBO  1029
static HWND g_hPhotoFmtCombo = nullptr;
static HWND g_hPlayerEdit = nullptr;
static HWND g_hUiLangCombo = nullptr, g_hTextLangCombo = nullptr, g_hVoiceLangCombo = nullptr;
static HWND g_hBtnOpenGame = nullptr, g_hBtnOpenSave = nullptr;
static int g_uiLang = 0;  // 0 = Português, 1 = English, 2 = Español
static const wchar_t *kUiLangCodes[] = { L"pt", L"en", L"es" };
static const wchar_t *kUiLangNames[] = { L"Português", L"English", L"Español" };

enum UiStr {
    S_SETTINGS, S_RES, S_MODE, S_TEXTLANG, S_VOICELANG, S_GAMEDIR, S_SAVEDIR, S_VSYNC, S_SKIP, S_ADV, S_D3DDBG,
    S_VERBOSE, S_GAMEPLAY, S_PLAY, S_CLOSE, S_PREP, S_BTN_GAME, S_BTN_SAVES, S_SHOWLOG, S_HIDELOG, S_CLEAR, S_COPY,
    S_RUNNING, S_READY, S_NOTFOUND, S_VERSION, S_MODE_BORDERLESS, S_MODE_WINDOW, S_MODE_FULL, S_LANG_SYSTEM,
    S_LANG_SAME, S_PICK_GAME, S_BAD_DIR, S_NO_GAME, S_PICK_SAVE, S_PLAYER, S_PHOTO, S_PHOTO_PNG, S_PHOTO_JPG,
    S_UPD_CHECK, S_UPD_CHECKING, S_UPD_AVAIL, S_UPD_LATEST, S_UPD_FAIL, S_UPD_NONE, S_UPD_ASK,
    S_SETUP_ASK, S_SETUP_FAIL, S_SETUP_NOPORT, S_UPD_ASK_INSTALL, S_UPD_DL, S_UPD_GAMERUN, S_UPD_DLFAIL, S_COUNT
};
static const wchar_t *kUiStr[S_COUNT][3] = {
    { L"CONFIGURAÇÕES", L"SETTINGS", L"CONFIGURACIÓN" },
    { L"Resolução", L"Resolution", L"Resolución" },
    { L"Modo de tela", L"Display mode", L"Modo de pantalla" },
    { L"Idioma do texto", L"Text language", L"Idioma del texto" },
    { L"Idioma da voz", L"Voice language", L"Idioma de las voces" },
    { L"Pasta do jogo", L"Game folder", L"Carpeta del juego" },
    { L"Pasta de saves", L"Save folder", L"Carpeta de partidas" },
    { L"Sincronização vertical (VSync)", L"Vertical sync (VSync)", L"Sincronización vertical (VSync)" },
    { L"Pular introdução (tela legal e logos)", L"Skip intro (legal screen and logos)", L"Omitir introducción (aviso legal y logos)" },
    { L"Opções avançadas", L"Advanced options", L"Opciones avanzadas" },
    { L"Camada de depuração do Direct3D 12", L"Direct3D 12 debug layer", L"Capa de depuración de Direct3D 12" },
    { L"Capturas de quadros (depuração, BMP)", L"Frame captures (debug, BMP)", L"Capturas de fotogramas (depuración, BMP)" },
    { L"Ir direto para o gameplay (teste)", L"Go straight to gameplay (test)", L"Ir directo a la partida (prueba)" },
    { L"JOGAR", L"PLAY", L"JUGAR" },
    { L"FECHAR JOGO", L"CLOSE GAME", L"CERRAR JUEGO" },
    { L"PREPARANDO…", L"PREPARING…", L"PREPARANDO…" },
    { L"Pasta do jogo", L"Game folder", L"Carpeta" },
    { L"Saves", L"Saves", L"Partidas" },
    { L"Ver log", L"Show log", L"Ver registro" },
    { L"Ocultar log", L"Hide log", L"Ocultar registro" },
    { L"Limpar", L"Clear", L"Limpiar" },
    { L"Copiar", L"Copy", L"Copiar" },
    { L"Jogo em execução", L"Game running", L"Juego en ejecución" },
    { L"Pronto para jogar", L"Ready to play", L"Listo para jugar" },
    { L"Pasta do jogo não encontrada", L"Game folder not found", L"Carpeta del juego no encontrada" },
    { L"Versão 1.0.64980.34  ·  Windows x64 · Direct3D 12", L"Version 1.0.64980.34  ·  Windows x64 · Direct3D 12",
      L"Versión 1.0.64980.34  ·  Windows x64 · Direct3D 12" },
    { L"Sem bordas (recomendado)", L"Borderless (recommended)", L"Sin bordes (recomendado)" },
    { L"Janela", L"Windowed", L"Ventana" },
    { L"Tela cheia", L"Fullscreen", L"Pantalla completa" },
    { L"Automático (sistema)", L"Automatic (system)", L"Automático (sistema)" },
    { L"Igual ao texto", L"Same as text", L"Igual que el texto" },
    { L"Selecione a sua cópia do ONRUSH (a pasta do pacote extraído, com Mount e EmbeddedXvd)",
      L"Select your copy of ONRUSH (the extracted package folder, with Mount and EmbeddedXvd)",
      L"Selecciona tu copia de ONRUSH (la carpeta del paquete extraído, con Mount y EmbeddedXvd)" },
    { L"Essa pasta não tem o ONRUSH (game_release_xbox.exe, direto ou em Mount\\).\nEscolher outra pasta?",
      L"This folder does not contain ONRUSH (game_release_xbox.exe, directly or in Mount\\).\nChoose another folder?",
      L"Esta carpeta no contiene ONRUSH (game_release_xbox.exe, directo o en Mount\\).\n¿Elegir otra carpeta?" },
    { L"O jogo ainda não está preparado.\nSelecione a pasta da sua cópia do ONRUSH (o pacote extraído, com Mount e EmbeddedXvd).",
      L"The game is not set up yet.\nSelect the folder of your copy of ONRUSH (the extracted package, with Mount and EmbeddedXvd).",
      L"El juego aún no está preparado.\nSelecciona la carpeta de tu copia de ONRUSH (el paquete extraído, con Mount y EmbeddedXvd)." },
    { L"Selecione a pasta para os saves do ONRUSH", L"Select the folder for ONRUSH saves", L"Selecciona la carpeta para las partidas de ONRUSH" },
    { L"Jogador (save próprio)", L"Player (own save)", L"Jugador (partida propia)" },
    { L"Fotos do modo foto (F12)", L"Photo mode shots (F12)", L"Fotos del modo foto (F12)" },
    { L"PNG (maior qualidade)", L"PNG (best quality)", L"PNG (máxima calidad)" },
    { L"JPG (mais leve)", L"JPG (smaller files)", L"JPG (más ligero)" },
    { L"Verificar atualizações", L"Check for updates", L"Buscar actualizaciones" },
    { L"Verificando…", L"Checking…", L"Buscando…" },
    { L"Atualização: %ls", L"Update: %ls", L"Actualización: %ls" },
    { L"Você já está na versão mais recente do launcher (v%ls).", L"You already have the latest launcher version (v%ls).",
      L"Ya tienes la versión más reciente del launcher (v%ls)." },
    { L"Não foi possível verificar atualizações (%ls).\nConfira sua conexão ou abra a página de versões no GitHub.",
      L"Could not check for updates (%ls).\nCheck your connection or open the releases page on GitHub.",
      L"No se pudo buscar actualizaciones (%ls).\nRevisa tu conexión o abre la página de versiones en GitHub." },
    { L"Ainda não há versões publicadas no GitHub.", L"There are no releases on GitHub yet.", L"Todavía no hay versiones publicadas en GitHub." },
    { L"Nova versão do launcher disponível: %ls (atual: v%ls).\n\nAbrir a página de download no GitHub?",
      L"A new launcher version is available: %ls (current: v%ls).\n\nOpen the download page on GitHub?",
      L"Hay una nueva versión del launcher: %ls (actual: v%ls).\n\n¿Abrir la página de descarga en GitHub?" },
    { L"Encontrei o ONRUSH em:\n%ls\n\nPreparar o jogo na pasta do launcher?\n%ls\n\nOs arquivos do jogo são copiados (~60 MB), as pastas grandes (dat e EmbeddedXvd) viram atalhos para a sua cópia e é criado o onrush-pc.exe. A sua cópia não é alterada.",
      L"Found ONRUSH in:\n%ls\n\nSet the game up in the launcher folder?\n%ls\n\nThe game files are copied (~60 MB), the big folders (dat and EmbeddedXvd) become links to your copy and onrush-pc.exe is created. Your copy is not modified.",
      L"Se encontró ONRUSH en:\n%ls\n\n¿Preparar el juego en la carpeta del launcher?\n%ls\n\nLos archivos del juego se copian (~60 MB), las carpetas grandes (dat y EmbeddedXvd) se enlazan a tu copia y se crea onrush-pc.exe. Tu copia no se modifica." },
    { L"Não foi possível preparar o jogo: %ls.", L"Could not set the game up: %ls.", L"No se pudo preparar el juego: %ls." },
    { L"A pasta do launcher não tem os arquivos do port (d3d12_x.dll e outros).\nExtraia o zip da release inteiro e abra o launcher de dentro dele.",
      L"The launcher folder is missing the port files (d3d12_x.dll and others).\nExtract the whole release zip and run the launcher from it.",
      L"La carpeta del launcher no tiene los archivos del port (d3d12_x.dll y otros).\nExtrae el zip completo de la release y abre el launcher desde ahí." },
    { L"Nova versão do launcher disponível: %ls (atual: v%ls).\n\nBaixar e instalar agora? O launcher fecha, atualiza a pasta dele e abre de novo sozinho. Suas configurações, saves e shaders são mantidos.",
      L"A new launcher version is available: %ls (current: v%ls).\n\nDownload and install it now? The launcher closes, updates its folder and opens again by itself. Your settings, saves and shaders are kept.",
      L"Hay una nueva versión del launcher: %ls (actual: v%ls).\n\n¿Descargar e instalar ahora? El launcher se cierra, actualiza su carpeta y se abre de nuevo solo. Tu configuración, partidas guardadas y shaders se mantienen." },
    { L"Baixando… %d%%", L"Downloading… %d%%", L"Descargando… %d%%" },
    { L"Feche o jogo antes de atualizar o launcher.", L"Close the game before updating the launcher.", L"Cierra el juego antes de actualizar el launcher." },
    { L"Não foi possível baixar a atualização (%ls).\n\nAbrir a página da versão no GitHub?",
      L"Could not download the update (%ls).\n\nOpen the release page on GitHub?",
      L"No se pudo descargar la actualización (%ls).\n\n¿Abrir la página de la versión en GitHub?" },
};
static const wchar_t *T(UiStr s) { return kUiStr[s][g_uiLang]; }

// Game languages (names in their own language) and the locale passed to the game.
static const wchar_t *kGameLangNames[] = { L"English", L"Português (Brasil)", L"Español", L"Français", L"Deutsch", L"Italiano", L"Polski" };
static const wchar_t *kGameLangLocales[] = { L"en-US", L"pt-BR", L"es-ES", L"fr-FR", L"de-DE", L"it-IT", L"pl-PL" };
static const int kGameLangCount = 7;

static void FillGameLangCombo(HWND combo, const wchar_t *firstItem, const wchar_t *current)
{
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)firstItem);
    int sel = 0;
    for (int i = 0; i < kGameLangCount; ++i)
    {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)kGameLangNames[i]);
        if (current && current[0] && _wcsnicmp(current, kGameLangLocales[i], 2) == 0) sel = i + 1;
    }
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
}
static void ReadGameLangCombo(HWND combo, wchar_t *out, size_t n)
{
    int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (sel >= 1 && sel <= kGameLangCount) wcscpy_s(out, n, kGameLangLocales[sel - 1]);
    else out[0] = L'\0';
}

static void LoadHeroArt(const wchar_t *gameDir)
{
    if (g_heroArt) { delete g_heroArt; g_heroArt = nullptr; }
    const wchar_t *dirs[] = { gameDir, g_exeDir };
    const wchar_t *subs[] = { L"", L"\\Mount" };
    for (const wchar_t *d : dirs)
    {
        if (!d || !d[0]) continue;
        for (const wchar_t *s : subs)
        {
            wchar_t p[MAX_PATH];
            swprintf_s(p, MAX_PATH, L"%ls%ls\\SplashScreen.png", d, s);
            if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) continue;
            Gdiplus::Bitmap *b = Gdiplus::Bitmap::FromFile(p);
            if (b && b->GetLastStatus() == Gdiplus::Ok) { g_heroArt = b; return; }
            delete b;
        }
    }
}

static bool GameDirLooksValid(const wchar_t *dir)
{
    if (!dir || !dir[0]) return false;
    const wchar_t *exes[] = { L"onrush-pc.exe", L"game_release_xbox.exe" };
    for (const wchar_t *e : exes)
    {
        wchar_t p[MAX_PATH];
        swprintf_s(p, MAX_PATH, L"%ls\\%ls", dir, e);
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) return true;
    }
    return false;
}

static bool GameDirIsReady(const wchar_t *dir);
static void RefreshGameDirStatus(HWND hwnd)
{
    wchar_t edit[MAX_PATH] = {};
    if (g_hGamePathEdit) GetWindowTextW(g_hGamePathEdit, edit, MAX_PATH);
    LauncherConfig tmp = g_config;
    wcscpy_s(tmp.gamePath, MAX_PATH, edit);
    GetEffectiveGameDir(&tmp, g_gameDirShown, MAX_PATH);
    g_gameDirOk = GameDirIsReady(g_gameDirShown);
    if (!g_heroArt) LoadHeroArt(g_gameDirShown);
    RECT rc = { 0, 0, UI_HERO_W, UI_H };
    InvalidateRect(hwnd, &rc, FALSE);
}

static void SaveIni()
{
    wchar_t iniPath[MAX_PATH];
    swprintf_s(iniPath, MAX_PATH, L"%ls\\onrush.ini", g_exeDir);
    SaveConfigFile(iniPath, &g_config);
    WritePrivateProfileStringW(L"Discord", L"Enabled", g_discordOn ? L"1" : L"0", iniPath);
    wchar_t id[64]; MultiByteToWideChar(CP_UTF8, 0, g_discordAppId, -1, id, 64);
    WritePrivateProfileStringW(L"Discord", L"AppId", id, iniPath);
}

static void LoadDiscordIni()
{
    wchar_t iniPath[MAX_PATH], id[64] = {};
    swprintf_s(iniPath, MAX_PATH, L"%ls\\onrush.ini", g_exeDir);
    g_discordOn = GetPrivateProfileIntW(L"Discord", L"Enabled", 1, iniPath) != 0;
    GetPrivateProfileStringW(L"Discord", L"AppId", L"", id, 64, iniPath);
    if (id[0]) WideCharToMultiByte(CP_UTF8, 0, id, -1, g_discordAppId, 64, nullptr, nullptr);
    else strcpy_s(g_discordAppId, kDiscordDefaultAppId);
}

// Modern folder picker; returns false when cancelled.
static bool PickFolder(HWND hwnd, const wchar_t *title, wchar_t *out)
{
    IFileOpenDialog *dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opts = 0; dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(title);
    bool ok = false;
    if (SUCCEEDED(dlg->Show(hwnd)))
    {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)))
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { wcscpy_s(out, MAX_PATH, path); CoTaskMemFree(path); ok = true; }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

#include "game_setup.inc"
// Asks for the game folder until a valid one is chosen (or the user cancels). A copy of the game
// that is not set up yet (the extracted package) is set up in the launcher's folder.
static bool AskGameFolder(HWND hwnd)
{
    wchar_t dir[MAX_PATH], mount[MAX_PATH], root[MAX_PATH];
    while (PickFolder(hwnd, T(S_PICK_GAME), dir))
    {
        if (GameDirIsReady(dir)) { SetWindowTextW(g_hGamePathEdit, dir); return true; }
        if (FindGameCopy(dir, mount, root))
        {
            if (!OfferGameSetup(hwnd, dir)) return false;
            SetWindowTextW(g_hGamePathEdit, g_exeDir);
            return true;
        }
        if (MessageBoxW(hwnd, T(S_BAD_DIR), L"ONRUSH", MB_ICONWARNING | MB_YESNO) != IDYES) break;
    }
    return false;
}

static void FillRound(HDC hdc, RECT r, int rad, COLORREF fill, COLORREF border)
{
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(hdc, b), op = SelectObject(hdc, p);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(b); DeleteObject(p);
}

static void LayoutAdvanced(HWND hwnd)
{
    ShowWindow(g_hD3DDebugChk, g_showAdvanced ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hVerboseChk, g_showAdvanced ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hGameplayChk, g_showAdvanced ? SW_SHOW : SW_HIDE);
    InvalidateRect(g_hAdvBtn, nullptr, TRUE);
}

static void LayoutLog(HWND hwnd)
{
    int sw = g_showLog ? SW_SHOW : SW_HIDE;
    ShowWindow(g_hLogEdit, sw); ShowWindow(g_hClearLogBtn, sw); ShowWindow(g_hCopyLogBtn, sw);
    SetWindowTextW(g_hLogBtn, g_showLog ? T(S_HIDELOG) : T(S_SHOWLOG));
    RECT rc = { 0, 0, UI_HERO_W, UI_H };
    InvalidateRect(hwnd, &rc, FALSE);
}

#include "playlist_ui.inc"

static void DrawHero(HDC hdc)
{
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    Gdiplus::SolidBrush bg(Gdiplus::Color(255, 16, 18, 22));
    g.FillRectangle(&bg, 0, 0, UI_HERO_W, UI_H);
    if (g_heroArt)
    {
        // cover-fit the 16:9 art into the hero area, centred on the logo
        float iw = (float)g_heroArt->GetWidth(), ih = (float)g_heroArt->GetHeight();
        float sc = max((float)UI_HERO_W / iw, (float)UI_H / ih);
        float dw = iw * sc, dh = ih * sc;
        g.DrawImage(g_heroArt, Gdiplus::RectF(((float)UI_HERO_W - dw) / 2.f, ((float)UI_H - dh) / 2.f, dw, dh));
    }
    if (g_tab == 1) { PlDrawHero(g); return; }
    if (g_showLog)
    {
        Gdiplus::SolidBrush dim(Gdiplus::Color(235, 12, 13, 16));
        g.FillRectangle(&dim, 0, 0, UI_HERO_W, UI_H);
        return;
    }
    Gdiplus::LinearGradientBrush fade(Gdiplus::Point(0, UI_H - 230), Gdiplus::Point(0, UI_H),
                                      Gdiplus::Color(0, 16, 18, 22), Gdiplus::Color(245, 16, 18, 22));
    fade.SetWrapMode(Gdiplus::WrapModeTileFlipXY);  // default tiling paints a dark seam on the first row
    g.FillRectangle(&fade, 0, UI_H - 229, UI_HERO_W, 229);

    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font fTag(&ff, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font fStatus(&ff, 20, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font fSmall(&ff, 12, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush orange(Gdiplus::Color(255, 255, 92, 40)), white(Gdiplus::Color(255, 240, 243, 246)),
        muted(Gdiplus::Color(255, 150, 158, 170)), green(Gdiplus::Color(255, 60, 220, 130)), red(Gdiplus::Color(255, 240, 80, 70));
    g.DrawString(L"PC NATIVE PORT", -1, &fTag, Gdiplus::PointF(32, UI_H - 112), &orange);
    const wchar_t *status = g_gameRunning ? T(S_RUNNING) : (g_gameDirOk ? T(S_READY) : T(S_NOTFOUND));
    Gdiplus::SolidBrush *dot = g_gameRunning ? &orange : (g_gameDirOk ? &green : &red);
    g.FillEllipse(dot, 32, UI_H - 85, 10, 10);
    g.DrawString(status, -1, &fStatus, Gdiplus::PointF(48, UI_H - 94), &white);
    g.DrawString(T(S_VERSION), -1, &fSmall, Gdiplus::PointF(32, UI_H - 58), &muted);
    g.DrawString(L"Launcher v" OLAUNCHER_VERSION, -1, &fSmall, Gdiplus::PointF(32, UI_H - 40), &muted);
}

static void DrawPanel(HDC hdc)
{
    RECT rp = { UI_HERO_W, 0, UI_W, UI_H };
    FillRect(hdc, &rp, g_hBrushCard);
    RECT edge = { UI_HERO_W, 0, UI_HERO_W + 1, UI_H };
    HBRUSH eb = CreateSolidBrush(COLOR_BORDER); FillRect(hdc, &edge, eb); DeleteObject(eb);

    SetBkMode(hdc, TRANSPARENT);
    // tab row underline
    RECT tabLine = { UI_PANEL_X, 46, UI_PANEL_X + 184, 47 };
    HBRUSH tl = CreateSolidBrush(COLOR_BORDER); FillRect(hdc, &tabLine, tl); DeleteObject(tl);
    if (g_tab == 1) { PlDrawPanel(hdc); return; }

    SelectObject(hdc, g_hFontUI);
    SetTextColor(hdc, COLOR_TEXT_PRIMARY);
    const int half = (UI_PANEL_W - 12) / 2, X2 = UI_PANEL_X + half + 12;
    struct { int x, y; const wchar_t *t; } labels[] = {
        { UI_PANEL_X, 58, T(S_RES) }, { X2, 58, T(S_MODE) }, { UI_PANEL_X, 120, T(S_TEXTLANG) }, { X2, 120, T(S_VOICELANG) },
        { UI_PANEL_X, 256, T(S_PLAYER) }, { X2, 256, T(S_PHOTO) }, { UI_PANEL_X, 320, T(S_GAMEDIR) }, { UI_PANEL_X, 384, T(S_SAVEDIR) } };
    for (auto &l : labels) TextOutW(hdc, l.x, l.y, l.t, (int)wcslen(l.t));

    // input boxes behind the borderless edits
    RECT r0 = { UI_PANEL_X, 276, UI_PANEL_X + half, 308 };
    RECT r1 = { UI_PANEL_X, 340, UI_PANEL_X + UI_PANEL_W - 48, 372 };
    RECT r2 = { UI_PANEL_X, 404, UI_PANEL_X + UI_PANEL_W - 48, 436 };
    FillRound(hdc, r0, 8, COLOR_BG_INPUT, COLOR_BORDER);
    FillRound(hdc, r1, 8, COLOR_BG_INPUT, g_gameDirOk ? COLOR_BORDER : COLOR_ACCENT_RED);
    FillRound(hdc, r2, 8, COLOR_BG_INPUT, COLOR_BORDER);
}

static void DrawOwnerButton(const DRAWITEMSTRUCT *di)
{
    HDC hdc = di->hDC;
    RECT r = di->rcItem;
    int id = (int)di->CtlID;
    bool pressed = (di->itemState & ODS_SELECTED) != 0;
    bool disabled = (di->itemState & ODS_DISABLED) != 0;
    wchar_t text[128] = {};
    GetWindowTextW(di->hwndItem, text, 128);
    FillRect(hdc, &r, g_hBrushCard);
    SetBkMode(hdc, TRANSPARENT);

    if (id == ID_TAB_SETTINGS || id == ID_TAB_PLAYLIST)
    {
        bool active = (id == ID_TAB_PLAYLIST) == (g_tab == 1);
        SelectObject(hdc, g_hFontSmallCaps);
        SetTextColor(hdc, active ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);
        RECT rt = r; rt.bottom -= 2;
        DrawTextW(hdc, text, -1, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (active)
        {
            SIZE sz = {}; GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
            RECT u = { r.left, r.bottom - 2, r.left + sz.cx, r.bottom };
            HBRUSH ub = CreateSolidBrush(COLOR_ACCENT_ORANGE); FillRect(hdc, &u, ub); DeleteObject(ub);
        }
        return;
    }
    bool isToggle = (id == ID_PL_ENABLE || id == ID_PL_SHUFFLE || id == ID_VSYNC_CHK || id == ID_DISCORD_CHK || id == ID_D3DDEBUG_CHK || id == ID_VERBOSE_CHK || id == ID_GAMEPLAY_CHK || id == ID_SKIPINTRO_CHK || id == ID_BTN_ADVANCED);
    if (isToggle)
    {
        bool on = id == ID_PL_ENABLE ? g_config.musicEnabled != 0 : id == ID_PL_SHUFFLE ? g_config.musicShuffle != 0 :
                  id == ID_VSYNC_CHK ? g_vsyncOn : id == ID_DISCORD_CHK ? g_discordOn : id == ID_D3DDEBUG_CHK ? g_d3dDebugOn :
                  id == ID_VERBOSE_CHK ? g_verboseOn : id == ID_GAMEPLAY_CHK ? g_quickGameplayOn : id == ID_SKIPINTRO_CHK ? g_skipIntroOn : g_showAdvanced;
        if (id == ID_BTN_ADVANCED)
        {   // disclosure row
            SelectObject(hdc, g_hFontUI);
            SetTextColor(hdc, COLOR_TEXT_MUTED);
            wchar_t t[160]; swprintf_s(t, L"%ls  %ls", on ? L"▾" : L"▸", text);
            RECT rt = r; DrawTextW(hdc, t, -1, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            return;
        }
        // switch: track 36x20 on the right, label on the left
        RECT tr = { r.right - 38, r.top + (r.bottom - r.top - 20) / 2, r.right - 2, 0 };
        tr.bottom = tr.top + 20;
        FillRound(hdc, tr, 20, on ? COLOR_ACCENT_ORANGE : RGB(58, 64, 74), on ? COLOR_ACCENT_ORANGE : RGB(58, 64, 74));
        RECT knob = on ? RECT{ tr.right - 18, tr.top + 2, tr.right - 2, tr.bottom - 2 }
                       : RECT{ tr.left + 2, tr.top + 2, tr.left + 18, tr.bottom - 2 };
        FillRound(hdc, knob, 16, RGB(245, 245, 245), RGB(245, 245, 245));
        SelectObject(hdc, g_hFontUI);
        SetTextColor(hdc, COLOR_TEXT_PRIMARY);
        RECT rt = r; rt.right = tr.left - 8;
        DrawTextW(hdc, text, -1, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    bool primary = (id == ID_BTN_LAUNCH);
    COLORREF fill, border, fg = COLOR_TEXT_PRIMARY;
    if (primary)
    {
        fill = g_gameRunning ? COLOR_ACCENT_RED : COLOR_ACCENT_ORANGE;
        if (pressed) fill = g_gameRunning ? RGB(200, 45, 45) : RGB(220, 62, 16);
        if (disabled) { fill = RGB(70, 60, 56); fg = COLOR_TEXT_MUTED; }
        border = fill;
    }
    else
    {
        fill = pressed ? RGB(48, 54, 64) : RGB(38, 43, 51);
        border = COLOR_BORDER;
        if (disabled) fg = COLOR_TEXT_MUTED;
        if (id == ID_BTN_UPDATE && g_updateKnown && g_updateLast.status == UPD_NEWER) { border = COLOR_ACCENT_ORANGE; fg = RGB(255, 140, 90); }
    }
    FillRound(hdc, r, primary ? 12 : 8, fill, border);
    SelectObject(hdc, primary ? g_hFontPlay : g_hFontUI);
    SetTextColor(hdc, fg);
    DrawTextW(hdc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void DrawComboItem(const DRAWITEMSTRUCT *di)
{
    if (di->itemID == (UINT)-1) return;
    wchar_t text[128] = {};
    SendMessageW(di->hwndItem, CB_GETLBTEXT, di->itemID, (LPARAM)text);
    bool sel = (di->itemState & ODS_SELECTED) && !(di->itemState & ODS_COMBOBOXEDIT);
    HBRUSH b = CreateSolidBrush(sel ? RGB(60, 40, 32) : COLOR_BG_INPUT);
    FillRect(di->hDC, &di->rcItem, b); DeleteObject(b);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, sel ? RGB(255, 140, 90) : COLOR_TEXT_PRIMARY);
    SelectObject(di->hDC, g_hFontUI);
    RECT r = di->rcItem; r.left += 10;
    DrawTextW(di->hDC, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void SetLaunchButtonState()
{
    SetWindowTextW(g_hBtnLaunch, g_gameRunning ? T(S_CLOSE) : T(S_PLAY));
    InvalidateRect(g_hBtnLaunch, nullptr, TRUE);
    RECT rc = { 0, 0, UI_HERO_W, UI_H };
    InvalidateRect(g_hMainWnd, &rc, FALSE);
}

static void BrowseFolder(HWND hwnd, HWND edit, const wchar_t *title)
{
    BROWSEINFOW bi = {};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t selected[MAX_PATH];
    if (SHGetPathFromIDListW(pidl, selected)) SetWindowTextW(edit, selected);
    CoTaskMemFree(pidl);
}

static void ReadUiIntoConfig()
{
    int sel = (int)SendMessageW(g_hResCombo, CB_GETCURSEL, 0, 0);
    if (sel == 1) { g_config.width = 2560; g_config.height = 1440; }
    else if (sel == 2) { g_config.width = 3840; g_config.height = 2160; }
    else if (sel == 3) { g_config.width = 1280; g_config.height = 720; }
    else { g_config.width = 1920; g_config.height = 1080; }
    int modeSel = (int)SendMessageW(g_hModeCombo, CB_GETCURSEL, 0, 0);
    wcscpy_s(g_config.windowMode, 32, modeSel == 1 ? L"Windowed" : modeSel == 2 ? L"Fullscreen" : L"Borderless");
    g_config.vsync = g_vsyncOn ? 1 : 0;
    g_config.d3d12Debug = g_d3dDebugOn ? 1 : 0;
    g_config.verboseLog = g_verboseOn ? 1 : 0;
    g_config.quickGameplay = g_quickGameplayOn ? 1 : 0;
    g_config.skipIntro = g_skipIntroOn ? 1 : 0;
    wcscpy_s(g_config.photoFormat, 8, SendMessageW(g_hPhotoFmtCombo, CB_GETCURSEL, 0, 0) == 1 ? L"jpg" : L"png");
    wcscpy_s(g_config.uiLang, 8, kUiLangCodes[g_uiLang]);
    ReadGameLangCombo(g_hTextLangCombo, g_config.textLang, 16);
    ReadGameLangCombo(g_hVoiceLangCombo, g_config.voiceLang, 16);
    GetWindowTextW(g_hGamePathEdit, g_config.gamePath, MAX_PATH);
    GetWindowTextW(g_hSaveEdit, g_config.savePath, MAX_PATH);
    // player name: trimmed, no characters Windows can't use in a folder name
    wchar_t name[32] = {}, clean[32] = {};
    GetWindowTextW(g_hPlayerEdit, name, 32);
    size_t n = 0;
    for (const wchar_t *p = name; *p && n < 31; ++p)
        if (*p >= 32 && !wcschr(L"<>:\"/\\|?*", *p)) clean[n++] = *p;
    while (n > 0 && (clean[n - 1] == L' ' || clean[n - 1] == L'.')) clean[--n] = 0;
    const wchar_t *s = clean; while (*s == L' ') ++s;
    wcscpy_s(g_config.playerName, 32, *s ? s : L"Player 1");
}

// Puts every launcher text in the current language (g_uiLang); keeps the selections.
static void UpdateButtonText()
{
    if (!g_hBtnUpdate) return;
    wchar_t t[96];
    if (g_updateDownloading) swprintf_s(t, 96, T(S_UPD_DL), g_updatePercent);
    else if (g_updateChecking) wcscpy_s(t, T(S_UPD_CHECKING));
    else if (g_updateKnown && g_updateLast.status == UPD_NEWER) swprintf_s(t, 96, T(S_UPD_AVAIL), g_updateLast.tag);
    else wcscpy_s(t, T(S_UPD_CHECK));
    SetWindowTextW(g_hBtnUpdate, t);
    InvalidateRect(g_hBtnUpdate, nullptr, TRUE);
}

static void ApplyUiLanguage(HWND hwnd)
{
    UpdateButtonText();
    wchar_t textLang[16], voiceLang[16];
    ReadGameLangCombo(g_hTextLangCombo, textLang, 16);
    ReadGameLangCombo(g_hVoiceLangCombo, voiceLang, 16);
    if (SendMessageW(g_hTextLangCombo, CB_GETCOUNT, 0, 0) == 0) { wcscpy_s(textLang, g_config.textLang); wcscpy_s(voiceLang, g_config.voiceLang); }
    FillGameLangCombo(g_hTextLangCombo, T(S_LANG_SYSTEM), textLang);
    FillGameLangCombo(g_hVoiceLangCombo, T(S_LANG_SAME), voiceLang);

    int modeSel = (int)SendMessageW(g_hModeCombo, CB_GETCURSEL, 0, 0);
    if (modeSel < 0) modeSel = _wcsicmp(g_config.windowMode, L"Windowed") == 0 ? 1 : _wcsicmp(g_config.windowMode, L"Fullscreen") == 0 ? 2 : 0;
    SendMessageW(g_hModeCombo, CB_RESETCONTENT, 0, 0);
    const UiStr modes[] = { S_MODE_BORDERLESS, S_MODE_WINDOW, S_MODE_FULL };
    for (auto s : modes) SendMessageW(g_hModeCombo, CB_ADDSTRING, 0, (LPARAM)T(s));
    SendMessageW(g_hModeCombo, CB_SETCURSEL, modeSel, 0);

    int photoSel = (int)SendMessageW(g_hPhotoFmtCombo, CB_GETCURSEL, 0, 0);
    if (photoSel < 0) photoSel = _wcsicmp(g_config.photoFormat, L"jpg") == 0 ? 1 : 0;
    SendMessageW(g_hPhotoFmtCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_hPhotoFmtCombo, CB_ADDSTRING, 0, (LPARAM)T(S_PHOTO_PNG));
    SendMessageW(g_hPhotoFmtCombo, CB_ADDSTRING, 0, (LPARAM)T(S_PHOTO_JPG));
    SendMessageW(g_hPhotoFmtCombo, CB_SETCURSEL, photoSel, 0);

    SetWindowTextW(g_hVsyncChk, T(S_VSYNC));
    SetWindowTextW(g_hSkipIntroChk, T(S_SKIP));
    SetWindowTextW(g_hAdvBtn, T(S_ADV));
    SetWindowTextW(g_hD3DDebugChk, T(S_D3DDBG));
    SetWindowTextW(g_hVerboseChk, T(S_VERBOSE));
    SetWindowTextW(g_hGameplayChk, T(S_GAMEPLAY));
    SetWindowTextW(g_hBtnOpenGame, T(S_BTN_GAME));
    SetWindowTextW(g_hBtnOpenSave, T(S_BTN_SAVES));
    SetWindowTextW(g_hClearLogBtn, T(S_CLEAR));
    SetWindowTextW(g_hCopyLogBtn, T(S_COPY));
    SetWindowTextW(g_hLogBtn, g_showLog ? T(S_HIDELOG) : T(S_SHOWLOG));
    SetWindowTextW(g_hBtnLaunch, !IsWindowEnabled(g_hBtnLaunch) ? T(S_PREP) : g_gameRunning ? T(S_CLOSE) : T(S_PLAY));
    if (g_hTabSettings) PlApplyLanguage();
    InvalidateRect(hwnd, nullptr, TRUE);
}

#include "shader_prep.inc"

// ---- first run: the game records the shaders it uses only when it runs, and without translations it stops
// at the first screens. JOGAR on a new install therefore runs it a few times on its own ("warm-up"): each
// round lets it record shaders until nothing new shows up for a while, closes it, translates, and repeats.
#define TIMER_WARMUP      0x7701
#define WARMUP_MAX_ROUNDS 3
static int g_warmRound = 0;           // > 0 while warming up
static int g_warmDumpsBefore = 0, g_warmLastCount = 0;
static ULONGLONG g_warmStart = 0, g_warmLastChange = 0;
static bool g_warmKilled = false, g_warmNewDumps = false;
static bool g_prepLaunch = true;      // start the game when the preparation finishes

static void BeginWarmupRound(HWND hwnd)
{
    g_warmRound++;
    g_warmDumpsBefore = g_warmLastCount = PrepCountDumps(g_gameDirShown);
    g_warmStart = g_warmLastChange = GetTickCount64();
    g_warmKilled = false;
    LogPrintf(L"[Shaders] primeira execução: rodada %d de coleta de shaders (%d gravados até agora)\r\n", g_warmRound, g_warmDumpsBefore);
    StartShaderPrep(hwnd, g_gameDirShown, g_warmRound);
    g_prep->done = g_warmLastCount;
    g_warmupLaunch = true;
    g_hWorkerThread = CreateThread(nullptr, 0, GameSupervisorThread, &g_config, 0, nullptr);
    SetTimer(hwnd, TIMER_WARMUP, 500, nullptr);
}

// preparation (translate -> compile -> pipelines); WM_SHADER_PREP_DONE starts the game if launch is set
static void BeginShaderPrep(HWND hwnd, bool launch)
{
    g_prepLaunch = launch;
    EnableWindow(g_hBtnLaunch, FALSE);
    SetWindowTextW(g_hBtnLaunch, T(S_PREP));
    StartShaderPrep(hwnd, g_gameDirShown, 0, !launch);  // after the game closed there is time to optimize
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_hTabSettings) { LRESULT plr = 0; if (PlHandleMessage(hwnd, msg, wParam, lParam, &plr)) return plr; }
    switch (msg)
    {
    case WM_CREATE:
    {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        auto mk = [](int h, int w, const wchar_t *face) {
            return CreateFontW(-h, 0, 0, 0, w, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face); };
        g_hFontUI = mk(14, FW_NORMAL, L"Segoe UI");
        g_hFontUIBold = mk(14, FW_SEMIBOLD, L"Segoe UI");
        g_hFontSmallCaps = mk(11, FW_BOLD, L"Segoe UI");
        g_hFontPlay = mk(18, FW_BOLD, L"Segoe UI");
        g_hFontLog = mk(12, FW_NORMAL, L"Consolas");

        wchar_t initialGameDir[MAX_PATH];
        GetEffectiveGameDir(&g_config, initialGameDir, MAX_PATH);
        if (g_config.gamePath[0] == L'\0') wcscpy_s(g_config.gamePath, MAX_PATH, initialGameDir);
        LoadDiscordIni();
        g_vsyncOn = g_config.vsync != 0; g_d3dDebugOn = g_config.d3d12Debug != 0; g_verboseOn = g_config.verboseLog != 0; g_quickGameplayOn = g_config.quickGameplay != 0; g_skipIntroOn = g_config.skipIntro != 0;

        g_uiLang = 0;
        for (int i = 0; i < 3; ++i) if (_wcsicmp(g_config.uiLang, kUiLangCodes[i]) == 0) g_uiLang = i;

        const int X = UI_PANEL_X, W = UI_PANEL_W, HALF = (UI_PANEL_W - 12) / 2, X2 = UI_PANEL_X + HALF + 12;
        DWORD comboStyle = WS_VISIBLE | WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL;
        // launcher language, top right of the panel
        g_hUiLangCombo = CreateWindowW(WC_COMBOBOXW, L"", comboStyle, X + W - 124, 20, 124, 200, hwnd, (HMENU)ID_UILANG_COMBO, nullptr, nullptr);
        for (auto s : kUiLangNames) SendMessageW(g_hUiLangCombo, CB_ADDSTRING, 0, (LPARAM)s);
        SendMessageW(g_hUiLangCombo, CB_SETCURSEL, g_uiLang, 0);

        g_hResCombo = CreateWindowW(WC_COMBOBOXW, L"", comboStyle, X, 78, HALF, 300, hwnd, (HMENU)ID_RES_COMBO, nullptr, nullptr);
        const wchar_t *res[] = { L"1920 × 1080 (FHD)", L"2560 × 1440 (QHD)", L"3840 × 2160 (4K)", L"1280 × 720 (HD)" };
        for (auto s : res) SendMessageW(g_hResCombo, CB_ADDSTRING, 0, (LPARAM)s);
        SendMessageW(g_hResCombo, CB_SETCURSEL, g_config.width == 2560 ? 1 : g_config.width == 3840 ? 2 : g_config.width == 1280 ? 3 : 0, 0);

        g_hModeCombo = CreateWindowW(WC_COMBOBOXW, L"", comboStyle, X2, 78, HALF, 300, hwnd, (HMENU)ID_MODE_COMBO, nullptr, nullptr);
        // game text / voice language (filled by ApplyUiLanguage)
        g_hTextLangCombo = CreateWindowW(WC_COMBOBOXW, L"", comboStyle, X, 140, HALF, 300, hwnd, (HMENU)ID_TEXTLANG_COMBO, nullptr, nullptr);
        g_hVoiceLangCombo = CreateWindowW(WC_COMBOBOXW, L"", comboStyle, X2, 140, HALF, 300, hwnd, (HMENU)ID_VOICELANG_COMBO, nullptr, nullptr);
        g_hPhotoFmtCombo = CreateWindowW(WC_COMBOBOXW, L"", comboStyle, X2, 276, HALF, 200, hwnd, (HMENU)ID_PHOTOFMT_COMBO, nullptr, nullptr);
        HWND combos[] = { g_hUiLangCombo, g_hResCombo, g_hModeCombo, g_hTextLangCombo, g_hVoiceLangCombo, g_hPhotoFmtCombo };
        for (HWND c : combos) SetWindowTheme(c, L"DarkMode_CFD", nullptr);

        DWORD od = WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_OWNERDRAW;
        g_hVsyncChk = CreateWindowW(L"BUTTON", L"", od, X, 178, W, 24, hwnd, (HMENU)ID_VSYNC_CHK, nullptr, nullptr);
        g_hDiscordChk = CreateWindowW(L"BUTTON", L"Discord Rich Presence", od, X, 202, W, 24, hwnd, (HMENU)ID_DISCORD_CHK, nullptr, nullptr);
        g_hSkipIntroChk = CreateWindowW(L"BUTTON", L"", od, X, 226, W, 24, hwnd, (HMENU)ID_SKIPINTRO_CHK, nullptr, nullptr);

        DWORD es = WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL;
        g_hPlayerEdit = CreateWindowW(L"EDIT", g_config.playerName, es, X + 10, 283, HALF - 20, 20, hwnd, (HMENU)ID_PLAYER_EDIT, nullptr, nullptr);
        SendMessageW(g_hPlayerEdit, EM_LIMITTEXT, 20, 0);
        g_hGamePathEdit = CreateWindowW(L"EDIT", g_config.gamePath, es, X + 10, 347, W - 68, 20, hwnd, (HMENU)ID_GAME_PATH_EDIT, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"…", od, X + W - 40, 340, 40, 32, hwnd, (HMENU)ID_GAME_PATH_BROWSE, nullptr, nullptr);
        g_hSaveEdit = CreateWindowW(L"EDIT", g_config.savePath, es, X + 10, 411, W - 68, 20, hwnd, (HMENU)ID_SAVE_EDIT, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"…", od, X + W - 40, 404, 40, 32, hwnd, (HMENU)ID_SAVE_BROWSE, nullptr, nullptr);

        g_hAdvBtn = CreateWindowW(L"BUTTON", L"", od, X, 450, W, 24, hwnd, (HMENU)ID_BTN_ADVANCED, nullptr, nullptr);
        g_hD3DDebugChk = CreateWindowW(L"BUTTON", L"", od & ~WS_VISIBLE, X, 476, W, 24, hwnd, (HMENU)ID_D3DDEBUG_CHK, nullptr, nullptr);
        g_hVerboseChk = CreateWindowW(L"BUTTON", L"", od & ~WS_VISIBLE, X, 502, W, 24, hwnd, (HMENU)ID_VERBOSE_CHK, nullptr, nullptr);
        g_hGameplayChk = CreateWindowW(L"BUTTON", L"", od & ~WS_VISIBLE, X, 528, W, 22, hwnd, (HMENU)ID_GAMEPLAY_CHK, nullptr, nullptr);

        g_hBtnLaunch = CreateWindowW(L"BUTTON", L"", od, X, UI_H - 112, W, 56, hwnd, (HMENU)ID_BTN_LAUNCH, nullptr, nullptr);
        int bw = (W - 16) / 3;
        g_hBtnOpenGame = CreateWindowW(L"BUTTON", L"", od, X, UI_H - 46, bw, 30, hwnd, (HMENU)ID_BTN_OPEN_GAME, nullptr, nullptr);
        g_hBtnOpenSave = CreateWindowW(L"BUTTON", L"", od, X + bw + 8, UI_H - 46, bw, 30, hwnd, (HMENU)ID_BTN_OPEN_SAVE, nullptr, nullptr);
        g_hLogBtn = CreateWindowW(L"BUTTON", L"", od, X + 2 * (bw + 8), UI_H - 46, bw, 30, hwnd, (HMENU)ID_BTN_SHOW_LOG, nullptr, nullptr);

        // log overlay (over the art), hidden until "Ver log"
        g_hLogEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                                   24, 24, UI_HERO_W - 48, UI_H - 92, hwnd, (HMENU)ID_LOG_EDIT, nullptr, nullptr);
        SetWindowTheme(g_hLogEdit, L"DarkMode_Explorer", nullptr);
        g_hClearLogBtn = CreateWindowW(L"BUTTON", L"", od & ~WS_VISIBLE, 24, UI_H - 56, 90, 30, hwnd, (HMENU)ID_BTN_CLEAR_LOG, nullptr, nullptr);
        g_hCopyLogBtn = CreateWindowW(L"BUTTON", L"", od & ~WS_VISIBLE, 122, UI_H - 56, 90, 30, hwnd, (HMENU)ID_BTN_COPY_LOG, nullptr, nullptr);
        g_hBtnKill = nullptr;
        g_hBtnUpdate = CreateWindowW(L"BUTTON", L"", od, UI_HERO_W - 214, UI_H - 50, 190, 30, hwnd, (HMENU)ID_BTN_UPDATE, nullptr, nullptr);
        PlCreateControls(hwnd);

        EnumChildWindows(hwnd, [](HWND h, LPARAM) -> BOOL {
            SendMessageW(h, WM_SETFONT, (WPARAM)(GetDlgCtrlID(h) == ID_LOG_EDIT ? g_hFontLog : g_hFontUI), TRUE);
            return TRUE; }, 0);
        for (HWND c : combos) SendMessageW(c, CB_SETITEMHEIGHT, (WPARAM)-1, 24);
        ApplyUiLanguage(hwnd);
        PlUpdateVisibility(hwnd);

        // "OnRushLauncher.exe -setup <copy of the game>": set the game up in the launcher folder without asking
        {
            int argc = 0; LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            for (int i = 1; argv && i + 1 < argc; ++i)
            {
                if (_wcsicmp(argv[i], L"-setup") != 0) continue;
                wchar_t sm[MAX_PATH], sr[MAX_PATH]; std::wstring err;
                if (!FindGameCopy(argv[i + 1], sm, sr)) LogPrintf(L"[Setup] cópia do jogo não encontrada em %ls\r\n", argv[i + 1]);
                else if (!SetupGameFolder(sm, sr, g_exeDir, err)) LogPrintf(L"[Setup] falhou: %ls\r\n", err.c_str());
                else
                {
                    SetWindowTextW(g_hGamePathEdit, g_exeDir);
                    wcscpy_s(g_config.gamePath, MAX_PATH, g_exeDir); wcscpy_s(initialGameDir, MAX_PATH, g_exeDir);
                    SaveIni();
                }
                break;
            }
            if (argv) LocalFree(argv);
        }
        LoadHeroArt(initialGameDir);
        RefreshGameDirStatus(hwnd);

        SysSpecs specs = GetHostSpecs();
        LogPrintf(L"ONRUSH — PC Native Port (launcher)\r\n");
        LogPrintf(L"CPU: %u núcleos | RAM: %llu MB (%llu MB livres) | AVX2: %ls\r\n",
                  specs.cores, specs.ramTotalMB, specs.ramAvailMB, specs.hasAVX2 ? L"sim" : L"não");
        LogPrintf(L"Pasta do jogo: %ls\r\n\r\n", initialGameDir);
        for (const auto &p : CheckPrerequisites(initialGameDir))
            LogPrintf(L"  [%ls] %ls (%ls)\r\n", p.found ? L" OK " : L"FALTA", p.name, p.file);
        // "OnRushLauncher.exe -prepshaders": run only the shader/pipeline preparation and exit
        // "OnRushLauncher.exe -autolaunch": press JOGAR on start (automated tests of the real launcher path)
        if (wcsstr(GetCommandLineW(), L"-prepshaders") || wcsstr(GetCommandLineW(), L"-autolaunch"))
            PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_LAUNCH, 0), 0);
        else if (!wcsstr(GetCommandLineW(), L"-noupdatecheck"))
        {
            if (wcsstr(GetCommandLineW(), L"-updated"))
            {   // just installed by the updater: say so and remove its temp folder
                LogPrintf(L"[Atualização] launcher atualizado para v%ls\r\n", OLAUNCHER_VERSION);
                CloseHandle(CreateThread(nullptr, 0, UpdateCleanupThread, nullptr, 0, nullptr));
            }
            StartUpdateCheck(hwnd, false);  // silent: only highlights the button when a newer release exists
            UpdateButtonText();
        }
        return 0;
    }

    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)lParam;
        if (mi->CtlType == ODT_COMBOBOX) { mi->itemHeight = 24; return TRUE; }
        if (mi->CtlType == ODT_LISTBOX) { mi->itemHeight = 48; return TRUE; }
        break;
    }

    case WM_DRAWITEM:
    {
        const DRAWITEMSTRUCT *di = (const DRAWITEMSTRUCT *)lParam;
        if (di->CtlType == ODT_COMBOBOX) DrawComboItem(di);
        else if (di->CtlType == ODT_LISTBOX) PlDrawListItem(di);
        else if (di->CtlType == ODT_BUTTON) DrawOwnerButton(di);
        return TRUE;
    }

    case WM_APPEND_LOG:
    {
        wchar_t *text = (wchar_t *)lParam;
        if (text) { AppendLogText(text); free(text); }
        return 0;
    }

    case WM_GAME_STARTED:
        g_gameRunning = true;
        SetLaunchButtonState();
        if (g_warmRound) { EnableWindow(g_hBtnLaunch, FALSE); SetWindowTextW(g_hBtnLaunch, T(S_PREP)); return 0; }
        if (g_discordOn)
        {
            bool ok = g_discord.Connect(g_discordAppId) && g_discord.SetPlaying((long long)time(nullptr));
            LogPrintf(L"[Discord] Rich Presence: %ls\r\n", ok ? L"ativo" : L"Discord não encontrado (o app do Discord precisa estar aberto)");
        }
        return 0;

    case WM_SHADER_PREP_DONE:
        if (g_warmRound)
        {
            bool skipped = g_prep && g_prep->cancel;
            if (!skipped && g_warmNewDumps && g_warmRound < WARMUP_MAX_ROUNDS) { BeginWarmupRound(hwnd); return 0; }
            LogPrintf(L"[Shaders] primeira execução concluída (%d rodada(s))\r\n", g_warmRound);
            g_warmRound = 0;
        }
        EnableWindow(g_hBtnLaunch, TRUE);
        SetLaunchButtonState();
        if (wcsstr(GetCommandLineW(), L"-prepshaders")) { DestroyWindow(hwnd); return 0; }  // prepare only, don't start the game
        if (!g_prepLaunch) return 0;  // translation after the game closed: nothing else to do
        g_hWorkerThread = CreateThread(nullptr, 0, GameSupervisorThread, &g_config, 0, nullptr);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_WARMUP)
        {
            if (!g_warmRound) { KillTimer(hwnd, TIMER_WARMUP); return 0; }
            ULONGLONG now = GetTickCount64();
            int n = PrepCountDumps(g_gameDirShown);
            if (n != g_warmLastCount) { g_warmLastCount = n; g_warmLastChange = now; }
            if (g_prep && g_prep->warmup) g_prep->done = n;
            bool skip = g_prep && g_prep->warmup && g_prep->cancel;
            // enough: nothing new for 10 s (after at least 20 s), 2 minutes in total, or "Pular"
            if (g_hGameProcess && !g_warmKilled && (skip || (now - g_warmStart > 20000 && now - g_warmLastChange > 10000) || now - g_warmStart > 120000))
            {
                g_warmKilled = true;
                TerminateProcess(g_hGameProcess, 0);
            }
            return 0;
        }
        break;

    case WM_UPDATE_RESULT:
    {
        UpdateResult *r = (UpdateResult *)lParam;
        g_updateChecking = false;
        g_updateLast = *r; g_updateKnown = true;
        UpdateButtonText();
        wchar_t msgText[512];
        if (r->status != UPD_NEWER)
            LogPrintf(L"[Atualização] Launcher v%ls: %ls%ls\r\n", OLAUNCHER_VERSION,
                      r->status == UPD_LATEST ? L"versão mais recente" : r->status == UPD_NO_RELEASE ? L"nenhuma versão publicada no GitHub" : L"falha na verificação — ",
                      r->status == UPD_ERROR ? r->error : L"");
        if (r->status == UPD_NEWER)
            LogPrintf(L"[Atualização] Nova versão do launcher: %ls (atual v%ls) — %ls\r\n", r->tag, OLAUNCHER_VERSION, r->url);
        if (r->manual)
        {
            if (r->status == UPD_NEWER && r->asset[0] && r->sha256[0])
            {   // automatic update: download, close, install, reopen
                swprintf_s(msgText, 512, T(S_UPD_ASK_INSTALL), r->tag, OLAUNCHER_VERSION);
                if (MessageBoxW(hwnd, msgText, L"ONRUSH Launcher", MB_YESNO | MB_ICONINFORMATION) == IDYES)
                {
                    if (g_gameRunning || g_hPrepWnd || g_warmRound) MessageBoxW(hwnd, T(S_UPD_GAMERUN), L"ONRUSH Launcher", MB_ICONINFORMATION);
                    else if (StartUpdateDownload(hwnd, *r)) LogPrintf(L"[Atualização] baixando %ls…\r\n", r->asset);
                    UpdateButtonText();
                }
            }
            else if (r->status == UPD_NEWER)
            {
                swprintf_s(msgText, 512, T(S_UPD_ASK), r->tag, OLAUNCHER_VERSION);
                if (MessageBoxW(hwnd, msgText, L"ONRUSH Launcher", MB_YESNO | MB_ICONINFORMATION) == IDYES)
                    ShellExecuteW(hwnd, L"open", r->url, nullptr, nullptr, SW_SHOWNORMAL);
            }
            else if (r->status == UPD_LATEST) { swprintf_s(msgText, 512, T(S_UPD_LATEST), OLAUNCHER_VERSION); MessageBoxW(hwnd, msgText, L"ONRUSH Launcher", MB_ICONINFORMATION); }
            else if (r->status == UPD_NO_RELEASE) MessageBoxW(hwnd, T(S_UPD_NONE), L"ONRUSH Launcher", MB_ICONINFORMATION);
            else { swprintf_s(msgText, 512, T(S_UPD_FAIL), r->error); MessageBoxW(hwnd, msgText, L"ONRUSH Launcher", MB_ICONWARNING); }
        }
        delete r;
        return 0;
    }

    case WM_UPDATE_PROGRESS:
        g_updatePercent = (int)wParam;
        UpdateButtonText();
        return 0;

    case WM_UPDATE_READY:
    {
        UpdateJob *job = (UpdateJob *)lParam;
        g_updateDownloading = false;
        UpdateButtonText();
        wchar_t msgText[512], err[160] = {};
        if (job->ok && (g_gameRunning || g_hPrepWnd || g_warmRound))
            MessageBoxW(hwnd, T(S_UPD_GAMERUN), L"ONRUSH Launcher", MB_ICONINFORMATION);
        else if (job->ok && LaunchUpdateInstaller(job, err, 160))
        {
            LogPrintf(L"[Atualização] %ls baixada e conferida; o launcher fecha para instalar\r\n", job->tag);
            ReadUiIntoConfig();
            SaveIni();
            delete job;
            DestroyWindow(hwnd);
            return 0;
        }
        else
        {
            const wchar_t *why = job->ok ? err : job->error;
            LogPrintf(L"[Atualização] falhou: %ls\r\n", why);
            swprintf_s(msgText, 512, T(S_UPD_DLFAIL), why);
            if (MessageBoxW(hwnd, msgText, L"ONRUSH Launcher", MB_YESNO | MB_ICONWARNING) == IDYES)
                ShellExecuteW(hwnd, L"open", g_updateLast.url[0] ? g_updateLast.url : OLAUNCHER_RELEASES L"/latest", nullptr, nullptr, SW_SHOWNORMAL);
        }
        delete job;
        return 0;
    }

    case WM_GAME_EXITED:
        g_gameRunning = false;
        if (g_warmRound)
        {   // end of a warm-up round: translate what it recorded (the next round or the game starts afterwards)
            KillTimer(hwnd, TIMER_WARMUP);
            g_warmupLaunch = false;
            bool skipped = g_prep && g_prep->warmup && g_prep->cancel;
            int n = PrepCountDumps(g_gameDirShown);
            g_warmNewDumps = n > g_warmDumpsBefore;
            LogPrintf(L"[Shaders] rodada %d: %d shader(s) novo(s) gravado(s)%ls\r\n", g_warmRound, n - g_warmDumpsBefore, skipped ? L" (pulado)" : L"");
            if (g_hPrepWnd) DestroyWindow(g_hPrepWnd);
            if (skipped) g_warmNewDumps = false;  // no more rounds; still prepare what was recorded (can be skipped too)
            BeginShaderPrep(hwnd, true);
            return 0;
        }
        SetLaunchButtonState();
        g_discord.Clear();
        g_discord.Close();
        // shaders recorded during this session get translated now, so the next JOGAR starts right away
        {
            wchar_t tr[MAX_PATH];
            bool fresh = PrepTranslationNeeded(g_gameDirShown);
            if (!g_hPrepWnd && PrepTranslatorExe(tr) && (fresh || PrepHasUnoptimized(g_gameDirShown)) && !wcsstr(GetCommandLineW(), L"-autolaunch"))
            {
                LogPrintf(fresh ? L"[Shaders] o jogo gravou shaders novos: traduzindo para a próxima vez\r\n"
                                : L"[Shaders] otimizando shaders de computação (pode pular)\r\n");
                BeginShaderPrep(hwnd, false);
            }
        }
        return 0;

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        switch (id)
        {
        case ID_BTN_LAUNCH:
            if (g_gameRunning)
            {
                if (g_hGameProcess) TerminateProcess(g_hGameProcess, 0);
                return 0;
            }
            PlPreviewStop();
            ReadUiIntoConfig();
            SaveIni();
            if (!GameDirIsReady(g_gameDirShown))
            {
                // the configured folder may be the player's copy itself: offer to set it up first
                wchar_t gm[MAX_PATH], gr[MAX_PATH];
                if (FindGameCopy(g_gameDirShown, gm, gr) && OfferGameSetup(hwnd, g_gameDirShown)) SetWindowTextW(g_hGamePathEdit, g_exeDir);
                else
                {
                    MessageBoxW(hwnd, T(S_NO_GAME), L"ONRUSH", MB_ICONINFORMATION);
                    if (!AskGameFolder(hwnd)) return 0;
                }
                ReadUiIntoConfig(); SaveIni();
                if (g_heroArt) { delete g_heroArt; g_heroArt = nullptr; }
                RefreshGameDirStatus(hwnd);
            }
            if (!FileExistsIn(g_gameDirShown, L"onrush-pc.exe") && FileExistsIn(g_gameDirShown, L"game_release_xbox.exe") && BrandGameExe(g_gameDirShown))
                LogPrintf(L"[Setup] onrush-pc.exe criado a partir de game_release_xbox.exe\r\n");
            if (g_hPrepWnd || g_warmRound) return 0;  // already preparing
            {
                wchar_t tr[MAX_PATH];
                if (PrepIsFreshInstall(g_gameDirShown) && PrepTranslatorExe(tr) && !wcsstr(GetCommandLineW(), L"-prepshaders") &&
                    !wcsstr(GetCommandLineW(), L"-nowarmup"))
                {   // new install: let the game record its shaders first (popup + a few short runs)
                    EnableWindow(g_hBtnLaunch, FALSE);
                    SetWindowTextW(g_hBtnLaunch, T(S_PREP));
                    g_prepLaunch = true;
                    BeginWarmupRound(hwnd);
                    return 0;
                }
            }
            // shader/pipeline preparation first (popup with progress); the game starts on WM_SHADER_PREP_DONE
            BeginShaderPrep(hwnd, true);
            return 0;
        case ID_VSYNC_CHK: g_vsyncOn = !g_vsyncOn; InvalidateRect((HWND)lParam, nullptr, TRUE); return 0;
        case ID_DISCORD_CHK:
            g_discordOn = !g_discordOn; InvalidateRect((HWND)lParam, nullptr, TRUE); SaveIni();
            if (!g_discordOn) { g_discord.Clear(); g_discord.Close(); }
            else if (g_gameRunning && g_discord.Connect(g_discordAppId)) g_discord.SetPlaying((long long)time(nullptr));
            return 0;
        case ID_D3DDEBUG_CHK: g_d3dDebugOn = !g_d3dDebugOn; InvalidateRect((HWND)lParam, nullptr, TRUE); return 0;
        case ID_VERBOSE_CHK: g_verboseOn = !g_verboseOn; InvalidateRect((HWND)lParam, nullptr, TRUE); return 0;
        case ID_GAMEPLAY_CHK: g_quickGameplayOn = !g_quickGameplayOn; InvalidateRect((HWND)lParam, nullptr, TRUE); return 0;
        case ID_SKIPINTRO_CHK: g_skipIntroOn = !g_skipIntroOn; InvalidateRect((HWND)lParam, nullptr, TRUE); return 0;
        case ID_BTN_UPDATE:
            if (g_updateKnown && g_updateLast.status == UPD_NEWER && !g_updateChecking)
            {
                // a newer release is already known: go straight to the question
                UpdateResult *r = new UpdateResult(g_updateLast); r->manual = true;
                PostMessageW(hwnd, WM_UPDATE_RESULT, 0, (LPARAM)r);
                g_updateChecking = true;
                return 0;
            }
            StartUpdateCheck(hwnd, true);
            UpdateButtonText();
            return 0;
        case ID_BTN_ADVANCED: g_showAdvanced = !g_showAdvanced; LayoutAdvanced(hwnd); return 0;
        case ID_BTN_SHOW_LOG: if (g_tab != 0) PlSetTab(hwnd, 0); g_showLog = !g_showLog; LayoutLog(hwnd); return 0;
        case ID_BTN_CLEAR_LOG: SetWindowTextW(g_hLogEdit, L""); return 0;
        case ID_BTN_COPY_LOG:
        {
            int len = GetWindowTextLengthW(g_hLogEdit);
            if (len <= 0 || !OpenClipboard(hwnd)) return 0;
            EmptyClipboard();
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
            if (hg) { GetWindowTextW(g_hLogEdit, (wchar_t *)GlobalLock(hg), len + 1); GlobalUnlock(hg); SetClipboardData(CF_UNICODETEXT, hg); }
            CloseClipboard();
            return 0;
        }
        case ID_BTN_OPEN_GAME:
            RefreshGameDirStatus(hwnd);
            ShellExecuteW(hwnd, L"open", g_gameDirShown, nullptr, nullptr, SW_SHOW);
            return 0;
        case ID_BTN_OPEN_SAVE:
        {
            wchar_t savePath[MAX_PATH], full[MAX_PATH];
            GetWindowTextW(g_hSaveEdit, savePath, MAX_PATH);
            if (PathIsRelativeW(savePath)) { RefreshGameDirStatus(hwnd); PathCombineW(full, g_gameDirShown, savePath); }
            else wcscpy_s(full, MAX_PATH, savePath);
            CreateDirectoryW(full, nullptr);
            ShellExecuteW(hwnd, L"open", full, nullptr, nullptr, SW_SHOW);
            return 0;
        }
        case ID_GAME_PATH_BROWSE:
            AskGameFolder(hwnd);
            ReadUiIntoConfig(); SaveIni();
            if (g_heroArt) { delete g_heroArt; g_heroArt = nullptr; }
            RefreshGameDirStatus(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case ID_SAVE_BROWSE:
            { wchar_t d[MAX_PATH]; if (PickFolder(hwnd, T(S_PICK_SAVE), d)) SetWindowTextW(g_hSaveEdit, d); }
            ReadUiIntoConfig(); SaveIni();
            return 0;
        case ID_PLAYER_EDIT:
            if (code == EN_KILLFOCUS) { ReadUiIntoConfig(); SetWindowTextW(g_hPlayerEdit, g_config.playerName); SaveIni(); }
            return 0;
        case ID_GAME_PATH_EDIT:
            if (code == EN_CHANGE) { RefreshGameDirStatus(hwnd); InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        case ID_UILANG_COMBO:
            if (code == CBN_SELCHANGE)
            {
                int sel = (int)SendMessageW(g_hUiLangCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < 3 && sel != g_uiLang) { g_uiLang = sel; ApplyUiLanguage(hwnd); ReadUiIntoConfig(); SaveIni(); }
            }
            return 0;
        case ID_TEXTLANG_COMBO:
        case ID_VOICELANG_COMBO:
        case ID_PHOTOFMT_COMBO:
            if (code == CBN_SELCHANGE) { ReadUiIntoConfig(); SaveIni(); }
            return 0;
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        // double buffer the whole client area
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, UI_W, UI_H);
        HGDIOBJ old = SelectObject(mem, bmp);
        DrawHero(mem);
        DrawPanel(mem);
        BitBlt(hdc, 0, 0, UI_W, UI_H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    {
        HDC h = (HDC)wParam;
        if ((HWND)lParam == g_hLogEdit)
        {
            SetTextColor(h, COLOR_LOG_TEXT);
            SetBkColor(h, COLOR_BG_LOG);
            return (INT_PTR)g_hBrushLog;
        }
        SetTextColor(h, COLOR_TEXT_PRIMARY);
        SetBkColor(h, COLOR_BG_INPUT);
        return (INT_PTR)g_hBrushInput;
    }

    case WM_CTLCOLORLISTBOX:
    {
        HDC h = (HDC)wParam;
        SetTextColor(h, COLOR_TEXT_PRIMARY);
        SetBkColor(h, COLOR_BG_INPUT);
        return (INT_PTR)g_hBrushInput;
    }

    case WM_CTLCOLORBTN:
        return (INT_PTR)g_hBrushCard;

    case WM_DESTROY:
    {
        ReadUiIntoConfig();
        SaveIni();
        PlShutdown();
        if (g_heroArt) { delete g_heroArt; g_heroArt = nullptr; }
        HFONT fonts[] = { g_hFontUI, g_hFontUIBold, g_hFontSmallCaps, g_hFontPlay, g_hFontLog };
        for (HFONT f : fonts) if (f) DeleteObject(f);
        HBRUSH brushes[] = { g_hBrushMain, g_hBrushCard, g_hBrushInput, g_hBrushLog };
        for (HBRUSH b : brushes) if (b) DeleteObject(b);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Application Entrypoint
// ============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    if (wcsstr(GetCommandLineW(), L"-applyupdate")) return RunApplyUpdate();  // the updater copy (self_update.inc)
    GetModuleFileNameW(nullptr, g_exeDir, MAX_PATH);
    wchar_t *lastSlash = wcsrchr(g_exeDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';

    SetDefaultConfig(&g_config);
    wchar_t iniPath[MAX_PATH];
    swprintf_s(iniPath, MAX_PATH, L"%ls\\onrush.ini", g_exeDir);
    LoadConfigFile(iniPath, &g_config);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icex);
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gsi, nullptr);

    g_hBrushMain = CreateSolidBrush(COLOR_BG_MAIN);
    g_hBrushCard = CreateSolidBrush(COLOR_BG_CARD);
    g_hBrushInput = CreateSolidBrush(COLOR_BG_INPUT);
    g_hBrushLog = CreateSolidBrush(COLOR_BG_LOG);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"OnRushLauncherV2";
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT wr = { 0, 0, UI_W, UI_H };
    AdjustWindowRectEx(&wr, style, FALSE, WS_EX_APPWINDOW);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    g_hMainWnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"ONRUSH™ — Launcher", style,
                                 (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2,
                                 ww, wh, nullptr, nullptr, hInstance, nullptr);
    if (!g_hMainWnd) return 1;
    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (IsDialogMessageW(g_hMainWnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_discord.Clear();
    g_discord.Close();
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    CoUninitialize();
    return (int)msg.wParam;
}
