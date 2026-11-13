// LGL — Local Games Launcher: лончер локальных игр для Windows XP с управлением геймпадом
// Сборка: VS2010, Release, Character Set = Unicode, Runtime Library = /MT.
// Библиотеки: user32, gdi32, gdiplus, winmm, advapi32 (подключаются через #pragma).
// XInput грузится динамически — DirectX SDK не нужен.
//
// Файлы рядом с exe:
//   games.ini  — настройки и ручные правки (UTF-8 или ANSI). Если файла нет,
//                создаётся образец. scan=<папка> — каждая подпапка = игра,
//                главный exe определяется автоматически.
//   state.ini  — пишет сам лончер: избранное, последний запуск, время в игре,
//                настройки, изменённые из меню (они перекрывают games.ini).
//   covers\    — обложки: <id>.jpg / .png / .bmp / .gif
//
//   profiles.ini       — общие профили маппинга (правится вручную)
//   profiles_games.ini — свои раскладки игр (пишет редактор раскладки)
//   sounds\            — move.wav, select.wav, back.wav (необязательно)
//   backgrounds\       — фоновые картинки (jpg/png/bmp), выбираются в меню Start
//
// Управление (пад):
//   крестовина/левый стик — навигация, A — запуск/выбор, B/Back — назад,
//   X — избранное, Y — настройки игры, LB/RB — разделы, LT/RT — по буквам,
//   Start — меню лончера (сортировка, звуки, автозапуск, выход, выключение).
//   В игре: удерживать Back+Start 1.5 с — закрыть игру (повторно — принудительно).
// Клавиатура: стрелки, Enter, Backspace, Space (=Y), Tab (=X), PgUp/PgDn (=LB/RB),
//   Home/End (=LT/RT), буквы — прыжок к игре, Esc — меню, F5 — пересканировать,
//   F11 — полный экран.

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WINVER       0x0501
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <math.h>
#include <mmsystem.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "advapi32.lib")
#endif

#ifndef CLEARTYPE_QUALITY
#define CLEARTYPE_QUALITY 5
#endif
#ifndef SHTDN_REASON_MAJOR_OTHER
#define SHTDN_REASON_MAJOR_OTHER 0x00000000
#endif
#ifndef SHTDN_REASON_FLAG_PLANNED
#define SHTDN_REASON_FLAG_PLANNED 0x80000000
#endif
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

typedef unsigned __int64 u64;

// Язык интерфейса: 0 — русский, 1 — английский. Строки — парами прямо в коде.
static int g_lang = 0;
#define TR(ru, en) (g_lang ? (en) : (ru))

// ---------------------------------------------------------------------------
// XInput без SDK
// ---------------------------------------------------------------------------
struct XI_GAMEPAD {
    WORD  wButtons;
    BYTE  bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};
struct XI_STATE {
    DWORD      dwPacketNumber;
    XI_GAMEPAD Gamepad;
};
struct XI_VIBRATION {
    WORD wLeftMotorSpeed, wRightMotorSpeed;
};
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD, XI_STATE*);
typedef DWORD (WINAPI *PFN_XInputSetState)(DWORD, XI_VIBRATION*);

#define XI_DPAD_UP    0x0001
#define XI_DPAD_DOWN  0x0002
#define XI_DPAD_LEFT  0x0004
#define XI_DPAD_RIGHT 0x0008
#define XI_START      0x0010
#define XI_BACK       0x0020
#define XI_LB         0x0100
#define XI_RB         0x0200
#define XI_A          0x1000
#define XI_B          0x2000
#define XI_X          0x4000
#define XI_Y          0x8000
#define STICK_THRESHOLD 16000

enum {
    VB_UP = 1, VB_DOWN = 2, VB_LEFT = 4, VB_RIGHT = 8,
    VB_A = 16, VB_B = 32, VB_X = 64, VB_Y = 128,
    VB_LB = 256, VB_RB = 512, VB_START = 1024, VB_BACK = 2048,
    VB_LT = 4096, VB_RT = 8192
};

enum Action {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_OK, ACT_BACK, ACT_MENU, ACT_FAV, ACT_PREVTAB, ACT_NEXTTAB,
    ACT_START, ACT_PREVLETTER, ACT_NEXTLETTER
};

// ---------------------------------------------------------------------------
// Цвета
// ---------------------------------------------------------------------------
struct Theme {
    const wchar_t* id;
    const wchar_t* nameRu;
    const wchar_t* nameEn;
    COLORREF bg, panel, panel2, sel, selDim, text, dim, accent, ok, bad;
    BYTE     panelAlpha;   // непрозрачность панелей поверх фото, 0..255
};
static const Theme kThemes[] = {
    { L"night", L"Ночь", L"Night",
      RGB(14, 18, 26),   RGB(24, 31, 44),   RGB(32, 41, 58),   RGB(46, 108, 214), RGB(44, 62, 92),
      RGB(232, 236, 242), RGB(138, 147, 166), RGB(255, 196, 64), RGB(90, 200, 110), RGB(220, 90, 80), 200 },
    { L"graphite", L"Графит", L"Graphite",
      RGB(22, 22, 24),   RGB(36, 36, 40),   RGB(50, 50, 56),   RGB(206, 104, 36), RGB(88, 58, 42),
      RGB(236, 236, 238), RGB(150, 150, 158), RGB(255, 170, 70), RGB(110, 200, 110), RGB(226, 86, 76), 205 },
    { L"green", L"Зелёная", L"Green",
      RGB(14, 18, 14),   RGB(28, 34, 28),   RGB(42, 50, 42),   RGB(74, 160, 30),  RGB(48, 74, 40),
      RGB(236, 242, 232), RGB(146, 158, 140), RGB(176, 226, 96), RGB(110, 210, 90), RGB(222, 90, 80), 205 },
    { L"deepblue", L"Глубокий синий", L"Deep blue",
      RGB(4, 6, 20),     RGB(12, 18, 48),   RGB(22, 32, 78),   RGB(40, 92, 204),  RGB(30, 48, 110),
      RGB(226, 232, 255), RGB(132, 142, 186), RGB(120, 200, 255), RGB(90, 210, 140), RGB(230, 90, 100), 190 },
    { L"light", L"Светлая", L"Light",
      RGB(220, 224, 230), RGB(246, 247, 249), RGB(226, 230, 236), RGB(158, 194, 244), RGB(200, 212, 232),
      RGB(26, 30, 38),   RGB(104, 110, 124), RGB(204, 112, 16), RGB(40, 150, 60),  RGB(200, 50, 40), 215 }
};
static const int kThemeCount = (int)(sizeof(kThemes) / sizeof(kThemes[0]));
static int g_themeIdx = 0;

#define C_BG      (kThemes[g_themeIdx].bg)
#define C_PANEL   (kThemes[g_themeIdx].panel)
#define C_PANEL2  (kThemes[g_themeIdx].panel2)
#define C_SEL     (kThemes[g_themeIdx].sel)
#define C_SEL_DIM (kThemes[g_themeIdx].selDim)
#define C_TEXT    (kThemes[g_themeIdx].text)
#define C_DIM     (kThemes[g_themeIdx].dim)
#define C_ACCENT  (kThemes[g_themeIdx].accent)
#define C_OK      (kThemes[g_themeIdx].ok)
#define C_BAD     (kThemes[g_themeIdx].bad)
#define C_BTN_A   RGB(96, 184, 64)
#define C_BTN_B   RGB(214, 64, 56)
#define C_BTN_X   RGB(52, 120, 214)
#define C_BTN_Y   RGB(232, 184, 40)
#define C_BTN_G   RGB(90, 98, 112)

// ---------------------------------------------------------------------------
// Игры
// ---------------------------------------------------------------------------
struct Game {
    wchar_t id[64];
    wchar_t name[128];
    wchar_t exe[MAX_PATH];
    wchar_t args[512];
    wchar_t dir[MAX_PATH];
    wchar_t genre[64];
    wchar_t cover[MAX_PATH];
    wchar_t root[MAX_PATH];    // папка игры (для найденных сканированием)
    wchar_t folder[128];       // имя этой папки
    int     year;
    wchar_t profile[32];       // id профиля из profiles.ini
    int     input, mouse;
    bool    ovr;        // настройки изменены из меню (лежат в state.ini)
    bool    exeUser;    // exe выбран в меню (state.ini: exe)
    bool    exeIni;     // exe указан в games.ini
    bool    hidden;
    bool    fav;
    u64     last;       // FILETIME (UTC), 0 = не запускалась
    int     count;
    UINT    playSec;
};

static const int kMaxGames = 512;
static Game g_games[kMaxGames];
static int  g_gameCount = 0;
static int  g_order[kMaxGames];

static const wchar_t* kInputIds[]   = { L"map", L"dinput", L"xinput" };
static const wchar_t* kInputRu[]    = { L"Маппинг на клавиатуру", L"DirectInput как есть", L"XInput как есть" };
static const wchar_t* kInputEn[]    = { L"Map to keyboard", L"DirectInput as is", L"XInput as is" };
#define kInput TR(kInputRu, kInputEn)
static const wchar_t* kMouseIds[]   = { L"profile", L"right", L"left", L"off" };
static const wchar_t* kMouseRu[]    = { L"Как в профиле", L"Правый стик", L"Левый стик", L"Выкл" };
static const wchar_t* kMouseEn[]    = { L"As in profile", L"Right stick", L"Left stick", L"Off" };
#define kMouse TR(kMouseRu, kMouseEn)
static const wchar_t* kTabsRu[]     = { L"Все", L"Избранное", L"Недавние", L"Скрытые" };
static const wchar_t* kTabsEn[]     = { L"All", L"Favorites", L"Recent", L"Hidden" };
#define kTabs TR(kTabsRu, kTabsEn)

static const COLORREF kCoverColors[] = {
    RGB(120, 48, 48), RGB(48, 96, 140), RGB(60, 110, 70), RGB(120, 90, 40),
    RGB(90, 56, 120), RGB(40, 110, 110), RGB(130, 70, 100), RGB(80, 80, 96)
};

// ---------------------------------------------------------------------------
// Состояние
// ---------------------------------------------------------------------------
enum Mode { MODE_LIST, MODE_SETTINGS, MODE_LAUNCH, MODE_RUNNING, MODE_MESSAGE,
            MODE_EDITOR, MODE_KEYPICK, MODE_MENU };

static wchar_t g_baseDir[MAX_PATH], g_gamesIni[MAX_PATH], g_stateIni[MAX_PATH], g_coversDir[MAX_PATH];
static bool    g_cfgFullscreen = true;
static bool    g_sortByName = true;
static wchar_t g_defProfile[32] = L"none";
static void    LoadProfiles();
static const int kMaxRoots = 16;
static wchar_t g_scanRoots[kMaxRoots][MAX_PATH];
static int     g_scanCount = 0;

// Кандидаты на главный exe
struct ExeCand {
    wchar_t path[MAX_PATH];
    int     score;
};
static const int kMaxCand = 32;
static ExeCand g_cand[kMaxCand];
static int     g_candCount = 0, g_candIdx = 0;

static HWND    g_hWnd = NULL;
static int     g_W = 1024, g_H = 768;
static HDC     g_memDC = NULL;
static HBITMAP g_memBmp = NULL, g_oldBmp = NULL;
static HFONT   g_fTitle, g_fTab, g_fRow, g_fSmall, g_fBig, g_fCover, g_fBtn;
static int     g_pxSmall, g_pxBig, g_glyphD;
static bool    g_fullscreen = false;
static RECT    g_winRect;

static Mode  g_mode = MODE_LIST;
static int   g_tab = 0;
static int   g_filtered[kMaxGames];
static int   g_nf = 0, g_sel = 0, g_top = 0;
static int   g_setSel = 0;
enum { SET_PROFILE, SET_INPUT, SET_MOUSE, SET_LAYOUT, SET_EXE, SET_ARGS, SET_DIR, SET_HIDE, SET_LAUNCH };
static const int kSetCount = 9;
static int   g_launchGame = -1;
static DWORD g_launchTick = 0;
static int   g_launchPhase = -1;
static int   g_lastMinute = -1;
static DWORD g_lastNav = 0;
static int   g_fgRetries = 0;
static UINT  g_timerMs = 0;

static wchar_t g_msgTitle[128], g_msgText[1024];

// Пад
static PFN_XInputGetState g_XGetState = NULL;
static PFN_XInputSetState g_XSetState = NULL;
static wchar_t g_xiName[32] = L"";
static volatile int g_padIdx = -1;
static DWORD g_lastScan = 0;
static int   g_padState = -1;     // 0 = нет XInput, 1 = нет пада, 2 = подключён
static DWORD g_prevMask = 0, g_repBtn = 0, g_repNext = 0;
static DWORD g_rumbleUntil = 0;

// Запущенная игра
static HANDLE   g_job = NULL, g_proc = NULL;
static DWORD    g_gamePid = 0, g_gameStart = 0;
static int      g_runGame = -1;
static bool     g_hidden = false;
static DEVMODEW g_dmBefore;
static bool     g_dmValid = false;
static int      g_closeStage = 0;   // 0 — нет, 1 — послан WM_CLOSE, 2 — убита
static DWORD    g_closeTick = 0, g_comboStart = 0, g_lastJobCheck = 0;
static bool     g_comboHeld = false, g_comboFired = false;
static bool     g_mapActive = false;

// Обложки
struct CoverEntry {
    int     game;      // -1 = пусто
    int     boxW, boxH;
    HBITMAP bmp;       // NULL = обложки нет
    int     w, h;
    DWORD   used;
};
static const int kCoverCache = 24;
static CoverEntry g_cov[kCoverCache];
static int        g_covPendGame = -1, g_covPendW = 0, g_covPendH = 0;
static ULONG_PTR  g_gdipToken = 0;
static bool       g_gdipOk = false;

struct Layout {
    RECT header, footer, list, info;
    int  rowH, pad, visRows, margin;
};

// ---------------------------------------------------------------------------
// Строки, файлы, время
// ---------------------------------------------------------------------------
static void CopyStr(wchar_t* dst, int cap, const wchar_t* src)
{
    wcsncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}
#define COPY(dst, src) CopyStr((dst), (int)(sizeof(dst) / sizeof((dst)[0])), (src))

static void Trim(wchar_t* s)
{
    wchar_t* p = s;
    while (*p == L' ' || *p == L'\t') p++;
    if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));
    size_t n = wcslen(s);
    while (n > 0 && (s[n - 1] == L' ' || s[n - 1] == L'\t' || s[n - 1] == L'\r')) s[--n] = 0;
}

static void Unquote(wchar_t* s)
{
    size_t n = wcslen(s);
    if (n >= 2 && s[0] == L'"' && s[n - 1] == L'"') {
        memmove(s, s + 1, (n - 2) * sizeof(wchar_t));
        s[n - 2] = 0;
    }
}

static bool FileExists(const wchar_t* p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool IsAbsPath(const wchar_t* p)
{
    return (p[0] && p[1] == L':') || (p[0] == L'\\' && p[1] == L'\\');
}

static void JoinPath(wchar_t* out, int cap, const wchar_t* dir, const wchar_t* name)
{
    if (IsAbsPath(name)) { CopyStr(out, cap, name); return; }
    _snwprintf(out, cap, L"%ls\\%ls", dir, name);
    out[cap - 1] = 0;
}

static void WorkDir(const wchar_t* exe, wchar_t* buf, int cap)
{
    CopyStr(buf, cap, exe);
    wchar_t* p = wcsrchr(buf, L'\\');
    if (p) *p = 0;
}

static int FindId(const wchar_t** ids, int n, const wchar_t* v, int def)
{
    for (int i = 0; i < n; i++)
        if (_wcsicmp(ids[i], v) == 0) return i;
    return def;
}

static u64 NowFT()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((u64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static u64 ToLocalFT(u64 t)
{
    FILETIME a, b;
    a.dwLowDateTime = (DWORD)t;
    a.dwHighDateTime = (DWORD)(t >> 32);
    if (!FileTimeToLocalFileTime(&a, &b)) return t;
    return ((u64)b.dwHighDateTime << 32) | b.dwLowDateTime;
}

static bool IsUtf8(const unsigned char* s, DWORD n)
{
    DWORD i = 0;
    while (i < n) {
        unsigned char c = s[i];
        DWORD extra;
        if (c < 0x80) { i++; continue; }
        else if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;
        if (i + extra >= n) return false;
        for (DWORD k = 1; k <= extra; k++)
            if ((s[i + k] & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}

// Читает текстовый файл: UTF-16LE (BOM), UTF-8 (с BOM или без) или ANSI.
static wchar_t* ReadTextFile(const wchar_t* path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size > 4 * 1024 * 1024) { CloseHandle(f); return NULL; }
    unsigned char* raw = (unsigned char*)malloc(size + 2);
    if (!raw) { CloseHandle(f); return NULL; }
    DWORD rd = 0;
    ReadFile(f, raw, size, &rd, NULL);
    CloseHandle(f);
    raw[rd] = 0; raw[rd + 1] = 0;

    wchar_t* out = NULL;
    if (rd >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        int n = (int)((rd - 2) / 2);
        out = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
        if (out) { memcpy(out, raw + 2, n * sizeof(wchar_t)); out[n] = 0; }
    } else {
        const char* p = (const char*)raw;
        int len = (int)rd;
        UINT cp = CP_ACP;
        if (rd >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) { p += 3; len -= 3; cp = CP_UTF8; }
        else if (IsUtf8(raw, rd)) cp = CP_UTF8;
        int n = len > 0 ? MultiByteToWideChar(cp, 0, p, len, NULL, 0) : 0;
        out = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
        if (out) {
            if (n > 0) MultiByteToWideChar(cp, 0, p, len, out, n);
            out[n] = 0;
        }
    }
    free(raw);
    return out;
}

static void WriteUtf8File(const wchar_t* path, const wchar_t* text, bool overwrite = false)
{
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, overwrite ? CREATE_ALWAYS : CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    char* buf = (char*)malloc(n + 3);
    if (buf) {
        buf[0] = (char)0xEF; buf[1] = (char)0xBB; buf[2] = (char)0xBF;
        WideCharToMultiByte(CP_UTF8, 0, text, -1, buf + 3, n, NULL, NULL);
        DWORD wr;
        WriteFile(f, buf, (DWORD)(n - 1 + 3), &wr, NULL);
        free(buf);
    }
    CloseHandle(f);
}

// Язык по умолчанию: из state.ini, иначе — по языку Windows
static void LoadLanguage()
{
    wchar_t buf[8];
    GetPrivateProfileStringW(L"@launcher", L"lang", L"", buf, 8, g_stateIni);
    if (!_wcsicmp(buf, L"ru"))      g_lang = 0;
    else if (!_wcsicmp(buf, L"en")) g_lang = 1;
    else {
        WORD pl = PRIMARYLANGID(GetUserDefaultUILanguage());
        g_lang = (pl == LANG_RUSSIAN || pl == LANG_UKRAINIAN || pl == LANG_BELARUSIAN) ? 0 : 1;
    }
}

static const wchar_t* kSampleIniEn =
    L"; Launcher settings. Encoding: UTF-8 or ANSI.\r\n"
    L"; After editing, press F5 in the launcher to rescan the folders.\r\n"
    L";\r\n"
    L"; [launcher] - general settings:\r\n"
    L";   scan = D:\\Games    games folder: every subfolder is a separate game,\r\n"
    L";                       the main exe is detected automatically. You can add several scan= lines.\r\n"
    L";   fullscreen = 1      start in full screen (F11 toggles)\r\n"
    L";   sort = name         name - alphabetical, file - in the order found\r\n"
    L";   covers = covers     covers folder (relative to the launcher or a full path)\r\n"
    L";   profile = none      default mapping profile for new games\r\n"
    L";\r\n"
    L"; Covers: covers\\<game folder name>.jpg/.png/.bmp/.gif,\r\n"
    L"; or cover.jpg / folder.jpg inside the game folder.\r\n"
    L";\r\n"
    L"; Per-game overrides (optional) - a section named after the game folder.\r\n"
    L"; Games outside the scan folders can be added the same way with exe=:\r\n"
    L";   name    = title\r\n"
    L";   exe     = path to the exe (full or relative to the game folder)\r\n"
    L";   args    = command line arguments\r\n"
    L";   dir     = working folder (default - the exe folder)\r\n"
    L";   genre   = genre\r\n"
    L";   year    = year\r\n"
    L";   cover   = custom cover file (name in the covers folder or a full path)\r\n"
    L";   profile = profile id from profiles.ini (shooter, racing, strategy, none ...)\r\n"
    L";   input   = map | dinput | xinput\r\n"
    L";   mouse   = profile | right | left | off   (stick as mouse)\r\n"
    L";\r\n"
    L"; Comments must take a whole line, starting with ; or #\r\n"
    L"\r\n"
    L"[launcher]\r\n"
    L";scan=D:\\Games\r\n"
    L"fullscreen=1\r\n"
    L"sort=name\r\n"
    L"covers=covers\r\n"
    L"\r\n"
    L";[Half-Life]\r\n"
    L";genre=Shooter\r\n"
    L";year=1998\r\n"
    L";profile=shooter\r\n"
    L";mouse=right\r\n";

static const wchar_t* kSampleProfilesEn =
    L"; Gamepad-to-keyboard/mouse mapping profiles. Encoding: UTF-8 or ANSI.\r\n"
    L"; Pick a game's profile in the launcher: Y -> Mapping profile.\r\n"
    L"; Changes apply from the next game launch (F5 rereads the file).\r\n"
    L";\r\n"
    L"; Pad buttons: A B X Y LB RB LT RT BACK START LS RS (stick clicks)\r\n"
    L";   DPAD_UP DPAD_DOWN DPAD_LEFT DPAD_RIGHT\r\n"
    L";   LS_UP LS_DOWN LS_LEFT LS_RIGHT, RS_UP ... - stick directions in keys mode\r\n"
    L"; Value: a key or a combo of up to three joined with +, e.g. CTRL+S\r\n"
    L"; Keys: A..Z 0..9 F1..F12 ESC TAB ENTER SPACE BACKSPACE SHIFT CTRL ALT\r\n"
    L";   RSHIFT RCTRL RALT CAPSLOCK UP DOWN LEFT RIGHT HOME END PGUP PGDN INSERT DELETE\r\n"
    L";   MINUS EQUALS LBRACKET RBRACKET SEMICOLON QUOTE TILDE BACKSLASH COMMA PERIOD SLASH\r\n"
    L";   NUM0..NUM9 NUMPLUS NUMMINUS NUMMUL NUMDIV NUMDOT NUMENTER\r\n"
    L";   MOUSE_LEFT MOUSE_RIGHT MOUSE_MIDDLE WHEEL_UP WHEEL_DOWN\r\n"
    L";\r\n"
    L"; lstick / rstick = wasd | arrows | keys | mouse | none\r\n"
    L";   keys - directions are taken from LS_UP.. / RS_UP..\r\n"
    L"; mouse_speed  - cursor speed (pixels/s at full deflection)\r\n"
    L"; mouse_curve  - 1 is linear, 2 is more precise at small deflection\r\n"
    L"; deadzone     - stick dead zone, 0..32767\r\n"
    L"; layer = LB   - modifier button: while held, buttons use the ^ values\r\n"
    L";   (^A=1 - A with LB held sends 1). The layer button itself sends nothing.\r\n"
    L"; passthrough = 1 - mapping is off\r\n"
    L"; name / name_en / name_ru - profile name (name_xx is used for that UI language)\r\n"
    L"\r\n"
    L"[shooter]\r\n"
    L"name=Shooter: WASD + mouse\r\n"
    L"name_ru=Шутер: WASD + мышь\r\n"
    L"lstick=wasd\r\n"
    L"rstick=mouse\r\n"
    L"mouse_speed=900\r\n"
    L"mouse_curve=2\r\n"
    L"RT=MOUSE_LEFT\r\n"
    L"LT=MOUSE_RIGHT\r\n"
    L"A=SPACE\r\n"
    L"B=C\r\n"
    L"X=R\r\n"
    L"Y=E\r\n"
    L"LB=WHEEL_UP\r\n"
    L"RB=WHEEL_DOWN\r\n"
    L"LS=SHIFT\r\n"
    L"RS=V\r\n"
    L"DPAD_UP=1\r\n"
    L"DPAD_RIGHT=2\r\n"
    L"DPAD_DOWN=3\r\n"
    L"DPAD_LEFT=4\r\n"
    L"START=ESC\r\n"
    L"BACK=TAB\r\n"
    L"\r\n"
    L"[racing]\r\n"
    L"name=Racing: arrows\r\n"
    L"name_ru=Гонки: стрелки\r\n"
    L"lstick=keys\r\n"
    L"LS_LEFT=LEFT\r\n"
    L"LS_RIGHT=RIGHT\r\n"
    L"stick_threshold=12000\r\n"
    L"rstick=none\r\n"
    L"RT=UP\r\n"
    L"LT=DOWN\r\n"
    L"A=ENTER\r\n"
    L"X=SPACE\r\n"
    L"B=BACKSPACE\r\n"
    L"Y=C\r\n"
    L"LB=SHIFT\r\n"
    L"RB=N\r\n"
    L"DPAD_UP=UP\r\n"
    L"DPAD_DOWN=DOWN\r\n"
    L"DPAD_LEFT=LEFT\r\n"
    L"DPAD_RIGHT=RIGHT\r\n"
    L"START=ESC\r\n"
    L"BACK=R\r\n"
    L"\r\n"
    L"[strategy]\r\n"
    L"name=Strategy: mouse + LB layer\r\n"
    L"name_ru=Стратегия: мышь + слой LB\r\n"
    L"lstick=mouse\r\n"
    L"rstick=arrows\r\n"
    L"mouse_speed=700\r\n"
    L"mouse_curve=2\r\n"
    L"A=MOUSE_LEFT\r\n"
    L"B=MOUSE_RIGHT\r\n"
    L"X=SHIFT\r\n"
    L"Y=H\r\n"
    L"RT=CTRL\r\n"
    L"LT=SHIFT\r\n"
    L"RB=SPACE\r\n"
    L"LS=MOUSE_MIDDLE\r\n"
    L"DPAD_UP=WHEEL_UP\r\n"
    L"DPAD_DOWN=WHEEL_DOWN\r\n"
    L"DPAD_LEFT=PGUP\r\n"
    L"DPAD_RIGHT=PGDN\r\n"
    L"START=ESC\r\n"
    L"BACK=F10\r\n"
    L"layer=LB\r\n"
    L"^A=1\r\n"
    L"^B=2\r\n"
    L"^X=3\r\n"
    L"^Y=4\r\n"
    L"^DPAD_UP=5\r\n"
    L"^DPAD_RIGHT=6\r\n"
    L"^DPAD_DOWN=7\r\n"
    L"^DPAD_LEFT=8\r\n"
    L"^START=ENTER\r\n"
    L"\r\n"
    L"[none]\r\n"
    L"name=No mapping\r\n"
    L"name_ru=Без маппинга\r\n"
    L"passthrough=1\r\n";

static const wchar_t* kSampleIni =
    L"; Настройки лончера. Кодировка: UTF-8 или ANSI.\r\n"
    L"; После правки нажмите F5 в лончере — папки пересканируются.\r\n"
    L";\r\n"
    L"; [launcher] — общие настройки:\r\n"
    L";   scan = D:\\Games    папка с играми: каждая подпапка — отдельная игра,\r\n"
    L";                       главный exe находится сам. Строк scan= может быть несколько.\r\n"
    L";   fullscreen = 1      запускаться во весь экран (F11 переключает)\r\n"
    L";   sort = name         name — по алфавиту, file — в порядке нахождения\r\n"
    L";   covers = covers     папка обложек (относительно лончера или полный путь)\r\n"
    L";   profile = none      профиль маппинга по умолчанию для новых игр\r\n"
    L";\r\n"
    L"; Обложки: covers\\<имя папки игры>.jpg/.png/.bmp/.gif,\r\n"
    L"; либо cover.jpg / folder.jpg внутри папки игры.\r\n"
    L";\r\n"
    L"; Правки для отдельной игры (необязательно) — секция с именем папки игры.\r\n"
    L"; Игры не из scan-папок тоже можно добавить так, указав exe:\r\n"
    L";   name    = название\r\n"
    L";   exe     = путь к exe (полный или относительно папки игры)\r\n"
    L";   args    = аргументы командной строки\r\n"
    L";   dir     = рабочая папка (по умолчанию — папка exe)\r\n"
    L";   genre   = жанр\r\n"
    L";   year    = год\r\n"
    L";   cover   = свой файл обложки (имя в папке covers или полный путь)\r\n"
    L";   profile = id профиля из profiles.ini (shooter, racing, strategy, none ...)\r\n"
    L";   input   = map | dinput | xinput\r\n"
    L";   mouse   = profile | right | left | off   (мышь со стика)\r\n"
    L";\r\n"
    L"; Комментарий — только целой строкой, начиная с ; или #\r\n"
    L"\r\n"
    L"[launcher]\r\n"
    L";scan=D:\\Games\r\n"
    L"fullscreen=1\r\n"
    L"sort=name\r\n"
    L"covers=covers\r\n"
    L"\r\n"
    L";[Half-Life]\r\n"
    L";genre=Шутер\r\n"
    L";year=1998\r\n"
    L";profile=shooter\r\n"
    L";mouse=right\r\n";

// ---------------------------------------------------------------------------
// Поиск главного exe в папке игры
// ---------------------------------------------------------------------------
static void LowerAlnum(const wchar_t* s, wchar_t* out, int cap)
{
    int n = 0;
    for (; *s && n < cap - 1; s++)
        if (iswalnum(*s)) out[n++] = (wchar_t)towlower(*s);
    out[n] = 0;
}

// "Need for Speed Underground 2" -> "nfsu2", "Half-Life" -> "hl"
static void WordInitials(const wchar_t* s, wchar_t* out, int cap)
{
    int n = 0;
    bool start = true;
    for (; *s && n < cap - 1; s++) {
        if (iswalnum(*s)) {
            if (start) { out[n++] = (wchar_t)towlower(*s); start = false; }
        } else {
            start = true;
        }
    }
    out[n] = 0;
}

static const wchar_t* kExeBad[] = {
    L"setup", L"install", L"uninst", L"unins0", L"unwise", L"config", L"setting", L"patch",
    L"update", L"regist", L"crashrep", L"bugreport", L"report", L"dxsetup", L"vcredist", L"dotnet",
    L"redist", L"readme", L"autorun", L"editor", L"isuninst", L"_isdel", L"unarc", L"winrar",
    L"directx", L"activat", L"benchmark", L"server", L"dedicated", L"keygen", L"helper"
};
static const wchar_t* kDirBad[] = {
    L"redist", L"_commonredist", L"directx", L"dx", L"dx9", L"support", L"setup", L"install",
    L"__installer", L"installer", L"vcredist", L"dotnet", L"manual", L"manuals", L"doc", L"docs",
    L"music", L"movies", L"movie", L"video", L"videos", L"sound", L"sounds", L"maps",
    L"save", L"saves", L"savegames", L"screenshots", L"logs", L"tools", L"sdk", L"extras"
};
static const wchar_t* kDirGood[] = {
    L"bin", L"system", L"game", L"binaries", L"exe", L"win32", L"x86", L"release"
};

static bool InList(const wchar_t** list, int n, const wchar_t* s)
{
    for (int i = 0; i < n; i++) if (!_wcsicmp(list[i], s)) return true;
    return false;
}
#define LISTN(a) ((int)(sizeof(a) / sizeof((a)[0])))

struct ScanCtx {
    wchar_t fnorm[128];   // имя папки: только буквы/цифры, нижний регистр
    wchar_t finit[32];    // инициалы слов имени папки
};

static int ScoreExe(const ScanCtx& c, const wchar_t* fname, u64 size, int depth, const wchar_t* subdir)
{
    wchar_t low[MAX_PATH];
    int i = 0;
    for (; fname[i] && i < MAX_PATH - 1; i++) low[i] = (wchar_t)towlower(fname[i]);
    low[i] = 0;
    if (i > 4) low[i - 4] = 0;   // без ".exe"
    for (int k = 0; k < LISTN(kExeBad); k++)
        if (wcsstr(low, kExeBad[k])) return -1000;

    wchar_t n[128];
    LowerAlnum(low, n, 128);
    size_t ln = wcslen(n), lf = wcslen(c.fnorm), li = wcslen(c.finit);
    int s = 0;
    if (ln == 0) return -1000;

    if (lf && !wcscmp(n, c.fnorm))                                              s += 40;
    else if (ln >= 3 && lf >= 3 && (!wcsncmp(c.fnorm, n, ln) || !wcsncmp(n, c.fnorm, lf))) s += 25;
    else if (li >= 2 && !wcsncmp(n, c.finit, li) && ln <= li + 2)               s += 22;
    else if (ln >= 3 && wcsstr(c.fnorm, n))                                     s += 15;

    if (!wcscmp(n, L"game") || !wcscmp(n, L"main")) s += 8;
    if (wcsstr(n, L"launch")) s -= 12;

    s += depth == 0 ? 10 : 4;
    if (depth > 0 && InList(kDirGood, LISTN(kDirGood), subdir)) s += 6;

    if (size > 4 * 1024 * 1024)      s += 15;
    else if (size > 1024 * 1024)     s += 10;
    else if (size > 256 * 1024)      s += 5;
    else if (size < 64 * 1024)       s -= 5;
    return s;
}

static void AddCand(const wchar_t* dir, const wchar_t* fname, int score)
{
    int slot = -1;
    if (g_candCount < kMaxCand) slot = g_candCount++;
    else {
        int lo = 0;
        for (int i = 1; i < kMaxCand; i++) if (g_cand[i].score < g_cand[lo].score) lo = i;
        if (g_cand[lo].score >= score) return;
        slot = lo;
    }
    JoinPath(g_cand[slot].path, MAX_PATH, dir, fname);
    g_cand[slot].score = score;
}

static void CollectExes(const ScanCtx& c, const wchar_t* dir, int depth, const wchar_t* subdir)
{
    wchar_t mask[MAX_PATH];
    _snwprintf(mask, MAX_PATH, L"%ls\\*", dir);
    mask[MAX_PATH - 1] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    static const int kMaxSub = 16;
    wchar_t subs[kMaxSub][64];
    int ns = 0;
    do {
        if (fd.cFileName[0] == L'.') continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth == 0 && ns < kMaxSub && wcslen(fd.cFileName) < 64
                && !InList(kDirBad, LISTN(kDirBad), fd.cFileName))
                { COPY(subs[ns], fd.cFileName); ns++; }
            continue;
        }
        size_t L = wcslen(fd.cFileName);
        if (L < 5 || _wcsicmp(fd.cFileName + L - 4, L".exe")) continue;
        u64 size = ((u64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        int sc = ScoreExe(c, fd.cFileName, size, depth, subdir);
        if (sc > -1000) AddCand(dir, fd.cFileName, sc);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    for (int i = 0; i < ns; i++) {
        wchar_t sub[MAX_PATH];
        JoinPath(sub, MAX_PATH, dir, subs[i]);
        CollectExes(c, sub, depth + 1, subs[i]);
    }
}

// Заполняет g_cand кандидатами, лучший — первый
static void BuildCandidates(const wchar_t* root, const wchar_t* folder)
{
    g_candCount = 0;
    ScanCtx c;
    LowerAlnum(folder, c.fnorm, 128);
    WordInitials(folder, c.finit, 32);
    CollectExes(c, root, 0, L"");
    for (int i = 1; i < g_candCount; i++) {
        ExeCand v = g_cand[i];
        int j = i - 1;
        while (j >= 0 && g_cand[j].score < v.score) { g_cand[j + 1] = g_cand[j]; j--; }
        g_cand[j + 1] = v;
    }
}

// ---------------------------------------------------------------------------
// Загрузка списка и state.ini
// ---------------------------------------------------------------------------
static void EnsureStateFile()
{
    if (FileExists(g_stateIni)) return;
    HANDLE f = CreateFileW(g_stateIni, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    static const unsigned char bom[2] = { 0xFF, 0xFE };   // UTF-16LE: так WritePrivateProfileStringW пишет Unicode
    DWORD wr;
    WriteFile(f, bom, 2, &wr, NULL);
    CloseHandle(f);
}

static void SaveGameState(const Game& g)
{
    EnsureStateFile();
    wchar_t buf[32];
    WritePrivateProfileStringW(g.id, L"fav", g.fav ? L"1" : L"0", g_stateIni);
    WritePrivateProfileStringW(g.id, L"hidden", g.hidden ? L"1" : L"0", g_stateIni);
    _snwprintf(buf, 32, L"%I64u", g.last); buf[31] = 0;
    WritePrivateProfileStringW(g.id, L"last", buf, g_stateIni);
    _snwprintf(buf, 32, L"%d", g.count); buf[31] = 0;
    WritePrivateProfileStringW(g.id, L"count", buf, g_stateIni);
    _snwprintf(buf, 32, L"%u", g.playSec); buf[31] = 0;
    WritePrivateProfileStringW(g.id, L"time", buf, g_stateIni);
    if (g.ovr) {
        WritePrivateProfileStringW(g.id, L"profile", g.profile, g_stateIni);
        WritePrivateProfileStringW(g.id, L"input",   kInputIds[g.input],     g_stateIni);
        WritePrivateProfileStringW(g.id, L"mouse",   kMouseIds[g.mouse],     g_stateIni);
    }
    if (g.exeUser) WritePrivateProfileStringW(g.id, L"exe", g.exe, g_stateIni);
}

static void LoadState()
{
    if (!FileExists(g_stateIni)) return;
    wchar_t buf[MAX_PATH];
    for (int i = 0; i < g_gameCount; i++) {
        Game& g = g_games[i];
        g.fav    = GetPrivateProfileIntW(g.id, L"fav", 0, g_stateIni) != 0;
        g.hidden = GetPrivateProfileIntW(g.id, L"hidden", 0, g_stateIni) != 0;
        GetPrivateProfileStringW(g.id, L"last", L"0", buf, MAX_PATH, g_stateIni);
        g.last = _wcstoui64(buf, NULL, 10);
        g.count = (int)GetPrivateProfileIntW(g.id, L"count", 0, g_stateIni);
        g.playSec = GetPrivateProfileIntW(g.id, L"time", 0, g_stateIni);
        GetPrivateProfileStringW(g.id, L"profile", L"", buf, MAX_PATH, g_stateIni);
        if (buf[0]) { COPY(g.profile, buf); g.ovr = true; }
        GetPrivateProfileStringW(g.id, L"input", L"", buf, MAX_PATH, g_stateIni);
        if (buf[0]) { g.input = FindId(kInputIds, 3, buf, g.input); g.ovr = true; }
        GetPrivateProfileStringW(g.id, L"mouse", L"", buf, MAX_PATH, g_stateIni);
        if (buf[0]) { g.mouse = FindId(kMouseIds, 4, buf, g.mouse); g.ovr = true; }
        GetPrivateProfileStringW(g.id, L"exe", L"", buf, MAX_PATH, g_stateIni);
        if (buf[0] && FileExists(buf)) { COPY(g.exe, buf); g.exeUser = true; }
    }
}

// Имя секции в state.ini: без символов, ломающих ini
static void SanitizeId(const wchar_t* src, wchar_t* out, int cap)
{
    CopyStr(out, cap, src);
    for (wchar_t* p = out; *p; p++)
        if (*p == L'[' || *p == L']' || *p == L'=' || *p == L';') *p = L'_';
    Trim(out);
}

static Game* FindGameById(const wchar_t* id)
{
    for (int i = 0; i < g_gameCount; i++)
        if (!_wcsicmp(g_games[i].id, id)) return &g_games[i];
    return NULL;
}

static Game* NewGame(const wchar_t* id)
{
    if (g_gameCount >= kMaxGames) return NULL;
    Game* g = &g_games[g_gameCount++];
    ZeroMemory(g, sizeof(*g));
    COPY(g->id, id);
    COPY(g->name, id);
    COPY(g->profile, g_defProfile);
    return g;
}

static void ScanRoot(const wchar_t* root)
{
    wchar_t mask[MAX_PATH];
    _snwprintf(mask, MAX_PATH, L"%ls\\*", root);
    mask[MAX_PATH - 1] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        if (fd.cFileName[0] == L'.') continue;

        wchar_t base[64], id[64];
        SanitizeId(fd.cFileName, base, 60);
        if (!base[0]) continue;
        COPY(id, base);
        for (int k = 2; FindGameById(id) && k < 100; k++) {
            _snwprintf(id, 64, L"%ls~%d", base, k);
            id[63] = 0;
        }
        Game* g = NewGame(id);
        if (!g) break;
        JoinPath(g->root, MAX_PATH, root, fd.cFileName);
        COPY(g->folder, fd.cFileName);
        COPY(g->name, fd.cFileName);
        for (wchar_t* p = g->name; *p; p++) if (*p == L'_') *p = L' ';
        Trim(g->name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void ApplyGameKey(Game* g, const wchar_t* key, const wchar_t* val)
{
    if      (!_wcsicmp(key, L"name"))    { if (val[0]) COPY(g->name, val); }
    else if (!_wcsicmp(key, L"exe")) {
        if (val[0] && !IsAbsPath(val) && g->root[0]) JoinPath(g->exe, MAX_PATH, g->root, val);
        else COPY(g->exe, val);
        g->exeIni = g->exe[0] != 0;
    }
    else if (!_wcsicmp(key, L"args"))    COPY(g->args, val);
    else if (!_wcsicmp(key, L"dir"))     COPY(g->dir, val);
    else if (!_wcsicmp(key, L"genre"))   COPY(g->genre, val);
    else if (!_wcsicmp(key, L"cover"))   COPY(g->cover, val);
    else if (!_wcsicmp(key, L"year"))    g->year = _wtoi(val);
    else if (!_wcsicmp(key, L"profile")) { if (val[0]) COPY(g->profile, val); }
    else if (!_wcsicmp(key, L"input"))   g->input = FindId(kInputIds, 3, val, g->input);
    else if (!_wcsicmp(key, L"mouse"))   g->mouse = FindId(kMouseIds, 4, val, g->mouse);
}

struct IniEntry {
    wchar_t* sec;
    wchar_t* key;   // NULL — строка-заголовок секции
    wchar_t* val;
};

static int TokenizeIni(wchar_t* text, IniEntry* out, int cap)
{
    int n = 0;
    wchar_t* sec = NULL;
    wchar_t* p = text;
    while (*p && n < cap) {
        wchar_t* line = p;
        while (*p && *p != L'\n') p++;
        if (*p) *p++ = 0;
        Trim(line);
        if (!line[0] || line[0] == L';' || line[0] == L'#') continue;

        if (line[0] == L'[') {
            wchar_t* e = wcschr(line, L']');
            if (!e) continue;
            *e = 0;
            sec = line + 1;
            Trim(sec);
            out[n].sec = sec; out[n].key = NULL; out[n].val = NULL;
            n++;
            continue;
        }
        wchar_t* eq = wcschr(line, L'=');
        if (!eq || !sec) continue;
        *eq = 0;
        wchar_t* key = line;
        wchar_t* val = eq + 1;
        Trim(key); Trim(val); Unquote(val);
        out[n].sec = sec; out[n].key = key; out[n].val = val;
        n++;
    }
    return n;
}

// Главный exe для найденных сканированием игр: кэш в state.ini (autoexe),
// иначе — поиск по папке.
static void ResolveExes()
{
    wchar_t buf[MAX_PATH];
    for (int i = 0; i < g_gameCount; i++) {
        Game& g = g_games[i];
        if (g.exeUser || g.exeIni || !g.root[0]) continue;
        GetPrivateProfileStringW(g.id, L"autoexe", L"", buf, MAX_PATH, g_stateIni);
        if (buf[0] && FileExists(buf)) { COPY(g.exe, buf); continue; }
        BuildCandidates(g.root, g.folder);
        if (g_candCount > 0) {
            COPY(g.exe, g_cand[0].path);
            EnsureStateFile();
            WritePrivateProfileStringW(g.id, L"autoexe", g.exe, g_stateIni);
        }
    }
    g_candCount = 0;
}

static void LoadGames()
{
    g_gameCount = 0;
    g_scanCount = 0;
    g_sortByName = true;
    COPY(g_defProfile, L"none");
    JoinPath(g_coversDir, MAX_PATH, g_baseDir, L"covers");
    LoadProfiles();

    wchar_t* text = ReadTextFile(g_gamesIni);
    if (!text) {
        WriteUtf8File(g_gamesIni, TR(kSampleIni, kSampleIniEn));
        text = ReadTextFile(g_gamesIni);
        if (!text) return;
    }
    int cap = 1;
    for (const wchar_t* q = text; *q; q++) if (*q == L'\n') cap++;
    IniEntry* ent = (IniEntry*)malloc(cap * sizeof(IniEntry));
    if (!ent) { free(text); return; }
    int ne = TokenizeIni(text, ent, cap);

    // 1. Общие настройки
    for (int i = 0; i < ne; i++) {
        if (!ent[i].key || _wcsicmp(ent[i].sec, L"launcher")) continue;
        const wchar_t* key = ent[i].key;
        wchar_t* val = ent[i].val;
        if      (!_wcsicmp(key, L"fullscreen")) g_cfgFullscreen = _wtoi(val) != 0;
        else if (!_wcsicmp(key, L"sort"))       g_sortByName = _wcsicmp(val, L"file") != 0;
        else if (!_wcsicmp(key, L"covers") && val[0]) JoinPath(g_coversDir, MAX_PATH, g_baseDir, val);
        else if (!_wcsicmp(key, L"profile") && val[0]) COPY(g_defProfile, val);
        else if (!_wcsicmp(key, L"scan") && val[0] && g_scanCount < kMaxRoots) {
            wchar_t* r = g_scanRoots[g_scanCount];
            JoinPath(r, MAX_PATH, g_baseDir, val);
            size_t n = wcslen(r);
            while (n > 0 && r[n - 1] == L'\\') r[--n] = 0;
            if (n > 0) g_scanCount++;
        }
    }

    // 2. Сканирование папок
    for (int i = 0; i < g_scanCount; i++) ScanRoot(g_scanRoots[i]);

    // 3. Ручные правки и игры вне scan-папок
    Game* cur = NULL;
    for (int i = 0; i < ne; i++) {
        if (!ent[i].key) {
            cur = NULL;
            if (!_wcsicmp(ent[i].sec, L"launcher")) continue;
            wchar_t id[64];
            SanitizeId(ent[i].sec, id, 60);   // как в ScanRoot
            if (!id[0]) continue;
            cur = FindGameById(id);
            if (!cur) cur = NewGame(id);
            continue;
        }
        if (cur) ApplyGameKey(cur, ent[i].key, ent[i].val);
    }
    free(ent);
    free(text);

    LoadState();
    ResolveExes();

    // Игры без exe пропускаем
    int n = 0;
    for (int i = 0; i < g_gameCount; i++) {
        if (!g_games[i].exe[0]) continue;
        if (n != i) g_games[n] = g_games[i];
        n++;
    }
    g_gameCount = n;
}

// ---------------------------------------------------------------------------
// Профили маппинга (profiles.ini)
// ---------------------------------------------------------------------------
enum PadInput {
    PI_A, PI_B, PI_X, PI_Y, PI_LB, PI_RB, PI_LT, PI_RT, PI_BACK, PI_START, PI_LS, PI_RS,
    PI_DUP, PI_DDOWN, PI_DLEFT, PI_DRIGHT,
    PI_LS_UP, PI_LS_DOWN, PI_LS_LEFT, PI_LS_RIGHT,
    PI_RS_UP, PI_RS_DOWN, PI_RS_LEFT, PI_RS_RIGHT,
    PI_COUNT
};
static const wchar_t* kPadNames[PI_COUNT] = {
    L"A", L"B", L"X", L"Y", L"LB", L"RB", L"LT", L"RT", L"BACK", L"START", L"LS", L"RS",
    L"DPAD_UP", L"DPAD_DOWN", L"DPAD_LEFT", L"DPAD_RIGHT",
    L"LS_UP", L"LS_DOWN", L"LS_LEFT", L"LS_RIGHT",
    L"RS_UP", L"RS_DOWN", L"RS_LEFT", L"RS_RIGHT"
};

enum { STICK_NONE, STICK_KEYS, STICK_MOUSE };

// Выходные коды: 0..255 — сканкод (бит 0x80 = расширенная клавиша E0),
// 256+ — мышь
enum {
    OUT_MOUSE_L = 256, OUT_MOUSE_R, OUT_MOUSE_M, OUT_WHEEL_UP, OUT_WHEEL_DOWN,
    OUT_COUNT
};

struct Binding {
    WORD out[3];
    int  n;
};

struct Profile {
    wchar_t id[32];
    wchar_t name[64];
    wchar_t nameRu[64];        // name_ru / name_en — название для языка интерфейса
    wchar_t nameEn[64];
    wchar_t baseName[64];      // для своих раскладок — из какого профиля сделана
    bool    own;               // из profiles_games.ini
    bool    passthrough;
    int     layerBtn;          // -1 — нет
    int     lstick, rstick;
    int     mouseSpeed;        // пикселей в секунду при полном отклонении
    float   mouseCurve;
    int     deadzone;          // 0..32767
    int     stickThreshold;    // порог "нажатия" стика в режиме клавиш
    int     triggerThreshold;  // 0..255
    Binding base[PI_COUNT];
    Binding layer[PI_COUNT];
};

static const int kMaxProfiles = 32;
static Profile g_prof[kMaxProfiles];
static int     g_profCount = 0;
static wchar_t g_profilesIni[MAX_PATH];
static wchar_t g_ownProfilesIni[MAX_PATH];

struct KeyName { const wchar_t* name; WORD code; };
static const KeyName kKeys[] = {
    { L"ESC", 0x01 }, { L"1", 0x02 }, { L"2", 0x03 }, { L"3", 0x04 }, { L"4", 0x05 },
    { L"5", 0x06 }, { L"6", 0x07 }, { L"7", 0x08 }, { L"8", 0x09 }, { L"9", 0x0A },
    { L"0", 0x0B }, { L"MINUS", 0x0C }, { L"EQUALS", 0x0D }, { L"BACKSPACE", 0x0E },
    { L"TAB", 0x0F }, { L"Q", 0x10 }, { L"W", 0x11 }, { L"E", 0x12 }, { L"R", 0x13 },
    { L"T", 0x14 }, { L"Y", 0x15 }, { L"U", 0x16 }, { L"I", 0x17 }, { L"O", 0x18 },
    { L"P", 0x19 }, { L"LBRACKET", 0x1A }, { L"RBRACKET", 0x1B }, { L"ENTER", 0x1C },
    { L"CTRL", 0x1D }, { L"LCTRL", 0x1D }, { L"A", 0x1E }, { L"S", 0x1F }, { L"D", 0x20 },
    { L"F", 0x21 }, { L"G", 0x22 }, { L"H", 0x23 }, { L"J", 0x24 }, { L"K", 0x25 },
    { L"L", 0x26 }, { L"SEMICOLON", 0x27 }, { L"QUOTE", 0x28 }, { L"TILDE", 0x29 },
    { L"SHIFT", 0x2A }, { L"LSHIFT", 0x2A }, { L"BACKSLASH", 0x2B }, { L"Z", 0x2C },
    { L"X", 0x2D }, { L"C", 0x2E }, { L"V", 0x2F }, { L"B", 0x30 }, { L"N", 0x31 },
    { L"M", 0x32 }, { L"COMMA", 0x33 }, { L"PERIOD", 0x34 }, { L"SLASH", 0x35 },
    { L"RSHIFT", 0x36 }, { L"NUMMUL", 0x37 }, { L"ALT", 0x38 }, { L"LALT", 0x38 },
    { L"SPACE", 0x39 }, { L"CAPSLOCK", 0x3A },
    { L"F1", 0x3B }, { L"F2", 0x3C }, { L"F3", 0x3D }, { L"F4", 0x3E }, { L"F5", 0x3F },
    { L"F6", 0x40 }, { L"F7", 0x41 }, { L"F8", 0x42 }, { L"F9", 0x43 }, { L"F10", 0x44 },
    { L"F11", 0x57 }, { L"F12", 0x58 },
    { L"NUM7", 0x47 }, { L"NUM8", 0x48 }, { L"NUM9", 0x49 }, { L"NUMMINUS", 0x4A },
    { L"NUM4", 0x4B }, { L"NUM5", 0x4C }, { L"NUM6", 0x4D }, { L"NUMPLUS", 0x4E },
    { L"NUM1", 0x4F }, { L"NUM2", 0x50 }, { L"NUM3", 0x51 }, { L"NUM0", 0x52 }, { L"NUMDOT", 0x53 },
    { L"NUMENTER", 0x9C }, { L"RCTRL", 0x9D }, { L"NUMDIV", 0xB5 }, { L"RALT", 0xB8 },
    { L"HOME", 0xC7 }, { L"UP", 0xC8 }, { L"PGUP", 0xC9 }, { L"LEFT", 0xCB },
    { L"RIGHT", 0xCD }, { L"END", 0xCF }, { L"DOWN", 0xD0 }, { L"PGDN", 0xD1 },
    { L"INSERT", 0xD2 }, { L"DELETE", 0xD3 },
    { L"MOUSE_LEFT", OUT_MOUSE_L }, { L"MOUSE_RIGHT", OUT_MOUSE_R }, { L"MOUSE_MIDDLE", OUT_MOUSE_M },
    { L"WHEEL_UP", OUT_WHEEL_UP }, { L"WHEEL_DOWN", OUT_WHEEL_DOWN }
};

static bool IsModifier(int code)
{
    return code == 0x1D || code == 0x2A || code == 0x36 || code == 0x38 || code == 0x9D || code == 0xB8;
}

static int KeyCode(const wchar_t* name)
{
    for (int i = 0; i < LISTN(kKeys); i++)
        if (!_wcsicmp(kKeys[i].name, name)) return kKeys[i].code;
    return -1;
}

// "CTRL+S" -> до трёх выходов
static void ParseBinding(const wchar_t* val, Binding& b)
{
    b.n = 0;
    wchar_t buf[128];
    COPY(buf, val);
    wchar_t* p = buf;
    while (*p && b.n < 3) {
        wchar_t* e = wcschr(p, L'+');
        if (e) *e = 0;
        Trim(p);
        int code = KeyCode(p);
        if (code >= 0) b.out[b.n++] = (WORD)code;
        if (!e) break;
        p = e + 1;
    }
}

static int PadInputIndex(const wchar_t* name)
{
    for (int i = 0; i < PI_COUNT; i++)
        if (!_wcsicmp(kPadNames[i], name)) return i;
    return -1;
}

static void InitProfile(Profile& p, const wchar_t* id)
{
    ZeroMemory(&p, sizeof(p));
    COPY(p.id, id);
    COPY(p.name, id);
    p.layerBtn = -1;
    p.mouseSpeed = 900;
    p.mouseCurve = 2.0f;
    p.deadzone = 7800;
    p.stickThreshold = 16000;
    p.triggerThreshold = 64;
}

// Пресет стика: WASD / ARROWS заполняют только незанятые направления
static int ApplyStickPreset(Profile& p, const wchar_t* mode, int firstDir)
{
    if (!_wcsicmp(mode, L"mouse")) return STICK_MOUSE;
    if (!_wcsicmp(mode, L"none") || !mode[0]) return STICK_NONE;
    const wchar_t* keys[4] = { NULL, NULL, NULL, NULL };
    if (!_wcsicmp(mode, L"wasd"))   { keys[0] = L"W";  keys[1] = L"S";    keys[2] = L"A";    keys[3] = L"D"; }
    if (!_wcsicmp(mode, L"arrows")) { keys[0] = L"UP"; keys[1] = L"DOWN"; keys[2] = L"LEFT"; keys[3] = L"RIGHT"; }
    for (int i = 0; i < 4; i++)
        if (keys[i] && p.base[firstDir + i].n == 0) ParseBinding(keys[i], p.base[firstDir + i]);
    return STICK_KEYS;
}

static const wchar_t* kSampleProfiles =
    L"; Профили маппинга геймпада на клавиатуру и мышь. Кодировка: UTF-8 или ANSI.\r\n"
    L"; Профиль игры выбирается в лончере: Y -> «Профиль маппинга».\r\n"
    L"; Изменения применяются со следующего запуска игры (F5 перечитывает файл).\r\n"
    L";\r\n"
    L"; Кнопки пада: A B X Y LB RB LT RT BACK START LS RS (нажатие стиков)\r\n"
    L";   DPAD_UP DPAD_DOWN DPAD_LEFT DPAD_RIGHT\r\n"
    L";   LS_UP LS_DOWN LS_LEFT LS_RIGHT, RS_UP ... — направления стиков в режиме клавиш\r\n"
    L"; Значение: клавиша или сочетание до трёх через +, например CTRL+S\r\n"
    L"; Клавиши: A..Z 0..9 F1..F12 ESC TAB ENTER SPACE BACKSPACE SHIFT CTRL ALT\r\n"
    L";   RSHIFT RCTRL RALT CAPSLOCK UP DOWN LEFT RIGHT HOME END PGUP PGDN INSERT DELETE\r\n"
    L";   MINUS EQUALS LBRACKET RBRACKET SEMICOLON QUOTE TILDE BACKSLASH COMMA PERIOD SLASH\r\n"
    L";   NUM0..NUM9 NUMPLUS NUMMINUS NUMMUL NUMDIV NUMDOT NUMENTER\r\n"
    L";   MOUSE_LEFT MOUSE_RIGHT MOUSE_MIDDLE WHEEL_UP WHEEL_DOWN\r\n"
    L";\r\n"
    L"; lstick / rstick = wasd | arrows | keys | mouse | none\r\n"
    L";   keys — направления берутся из LS_UP.. / RS_UP..\r\n"
    L"; mouse_speed  — скорость курсора (пикселей/с при полном отклонении)\r\n"
    L"; mouse_curve  — 1 линейно, 2 — точнее при малом отклонении\r\n"
    L"; deadzone     — мёртвая зона стика, 0..32767\r\n"
    L"; layer = LB   — кнопка-модификатор: пока зажата, кнопки берут значения с ^\r\n"
    L";   (^A=1 — A при зажатой LB даёт 1). Сама кнопка слоя ничего не нажимает.\r\n"
    L"; passthrough = 1 — маппинг выключен\r\n"
    L"; name / name_ru / name_en — название профиля (name_xx — для этого языка интерфейса)\r\n"
    L"\r\n"
    L"[shooter]\r\n"
    L"name=Шутер: WASD + мышь\r\n"
    L"name_en=Shooter: WASD + mouse\r\n"
    L"lstick=wasd\r\n"
    L"rstick=mouse\r\n"
    L"mouse_speed=900\r\n"
    L"mouse_curve=2\r\n"
    L"RT=MOUSE_LEFT\r\n"
    L"LT=MOUSE_RIGHT\r\n"
    L"A=SPACE\r\n"
    L"B=C\r\n"
    L"X=R\r\n"
    L"Y=E\r\n"
    L"LB=WHEEL_UP\r\n"
    L"RB=WHEEL_DOWN\r\n"
    L"LS=SHIFT\r\n"
    L"RS=V\r\n"
    L"DPAD_UP=1\r\n"
    L"DPAD_RIGHT=2\r\n"
    L"DPAD_DOWN=3\r\n"
    L"DPAD_LEFT=4\r\n"
    L"START=ESC\r\n"
    L"BACK=TAB\r\n"
    L"\r\n"
    L"[racing]\r\n"
    L"name=Гонки: стрелки\r\n"
    L"name_en=Racing: arrows\r\n"
    L"lstick=keys\r\n"
    L"LS_LEFT=LEFT\r\n"
    L"LS_RIGHT=RIGHT\r\n"
    L"stick_threshold=12000\r\n"
    L"rstick=none\r\n"
    L"RT=UP\r\n"
    L"LT=DOWN\r\n"
    L"A=ENTER\r\n"
    L"X=SPACE\r\n"
    L"B=BACKSPACE\r\n"
    L"Y=C\r\n"
    L"LB=SHIFT\r\n"
    L"RB=N\r\n"
    L"DPAD_UP=UP\r\n"
    L"DPAD_DOWN=DOWN\r\n"
    L"DPAD_LEFT=LEFT\r\n"
    L"DPAD_RIGHT=RIGHT\r\n"
    L"START=ESC\r\n"
    L"BACK=R\r\n"
    L"\r\n"
    L"[strategy]\r\n"
    L"name=Стратегия: мышь + слой LB\r\n"
    L"name_en=Strategy: mouse + LB layer\r\n"
    L"lstick=mouse\r\n"
    L"rstick=arrows\r\n"
    L"mouse_speed=700\r\n"
    L"mouse_curve=2\r\n"
    L"A=MOUSE_LEFT\r\n"
    L"B=MOUSE_RIGHT\r\n"
    L"X=SHIFT\r\n"
    L"Y=H\r\n"
    L"RT=CTRL\r\n"
    L"LT=SHIFT\r\n"
    L"RB=SPACE\r\n"
    L"LS=MOUSE_MIDDLE\r\n"
    L"DPAD_UP=WHEEL_UP\r\n"
    L"DPAD_DOWN=WHEEL_DOWN\r\n"
    L"DPAD_LEFT=PGUP\r\n"
    L"DPAD_RIGHT=PGDN\r\n"
    L"START=ESC\r\n"
    L"BACK=F10\r\n"
    L"layer=LB\r\n"
    L"^A=1\r\n"
    L"^B=2\r\n"
    L"^X=3\r\n"
    L"^Y=4\r\n"
    L"^DPAD_UP=5\r\n"
    L"^DPAD_RIGHT=6\r\n"
    L"^DPAD_DOWN=7\r\n"
    L"^DPAD_LEFT=8\r\n"
    L"^START=ENTER\r\n"
    L"\r\n"
    L"[none]\r\n"
    L"name=Без маппинга\r\n"
    L"name_en=No mapping\r\n"
    L"passthrough=1\r\n";

static void LoadProfileFile(const wchar_t* path, bool own, const wchar_t* sample)
{
    wchar_t* text = ReadTextFile(path);
    if (!text) {
        if (!sample) return;
        WriteUtf8File(path, sample);
        text = ReadTextFile(path);
        if (!text) return;
    }
    int cap = 1;
    for (const wchar_t* q = text; *q; q++) if (*q == L'\n') cap++;
    IniEntry* ent = (IniEntry*)malloc(cap * sizeof(IniEntry));
    if (!ent) { free(text); return; }
    int ne = TokenizeIni(text, ent, cap);

    Profile* cur = NULL;
    wchar_t lmode[16] = L"", rmode[16] = L"";
    for (int i = 0; i <= ne; i++) {
        // Конец секции: применяем пресеты стиков
        if (cur && (i == ne || !ent[i].key)) {
            cur->lstick = ApplyStickPreset(*cur, lmode, PI_LS_UP);
            cur->rstick = ApplyStickPreset(*cur, rmode, PI_RS_UP);
            cur = NULL;
        }
        if (i == ne) break;
        if (!ent[i].key) {
            if (g_profCount >= kMaxProfiles) continue;
            cur = &g_prof[g_profCount++];
            wchar_t id[32];
            SanitizeId(ent[i].sec, id, 32);
            InitProfile(*cur, id);
            cur->own = own;
            lmode[0] = rmode[0] = 0;
            continue;
        }
        if (!cur) continue;
        const wchar_t* key = ent[i].key;
        const wchar_t* val = ent[i].val;
        if      (!_wcsicmp(key, L"name"))              { if (val[0]) COPY(cur->name, val); }
        else if (!_wcsicmp(key, L"base"))              COPY(cur->baseName, val);
        else if (!_wcsicmp(key, L"name_ru"))           COPY(cur->nameRu, val);
        else if (!_wcsicmp(key, L"name_en"))           COPY(cur->nameEn, val);
        else if (!_wcsicmp(key, L"passthrough"))       cur->passthrough = _wtoi(val) != 0;
        else if (!_wcsicmp(key, L"layer"))             cur->layerBtn = PadInputIndex(val);
        else if (!_wcsicmp(key, L"lstick"))            COPY(lmode, val);
        else if (!_wcsicmp(key, L"rstick"))            COPY(rmode, val);
        else if (!_wcsicmp(key, L"mouse_speed"))       cur->mouseSpeed = _wtoi(val);
        else if (!_wcsicmp(key, L"mouse_curve"))       cur->mouseCurve = (float)_wtof(val);
        else if (!_wcsicmp(key, L"deadzone"))          cur->deadzone = _wtoi(val);
        else if (!_wcsicmp(key, L"stick_threshold"))   cur->stickThreshold = _wtoi(val);
        else if (!_wcsicmp(key, L"trigger_threshold")) cur->triggerThreshold = _wtoi(val);
        else {
            bool isLayer = key[0] == L'^';
            int pi = PadInputIndex(isLayer ? key + 1 : key);
            if (pi >= 0) ParseBinding(val, isLayer ? cur->layer[pi] : cur->base[pi]);
        }
    }
    free(ent);
    free(text);

    for (int i = 0; i < g_profCount; i++) {
        Profile& p = g_prof[i];
        if (p.mouseSpeed < 10) p.mouseSpeed = 10;
        if (p.mouseCurve < 0.5f || p.mouseCurve > 5.0f) p.mouseCurve = 2.0f;
        if (p.deadzone < 0 || p.deadzone > 30000) p.deadzone = 7800;
        if (p.stickThreshold < 1000 || p.stickThreshold > 32000) p.stickThreshold = 16000;
        if (p.triggerThreshold < 1 || p.triggerThreshold > 254) p.triggerThreshold = 64;
    }
}

static void LoadProfiles()
{
    g_profCount = 0;
    LoadProfileFile(g_profilesIni, false, TR(kSampleProfiles, kSampleProfilesEn));
    LoadProfileFile(g_ownProfilesIni, true, NULL);
}

static int ProfileIndex(const wchar_t* id)
{
    for (int i = 0; i < g_profCount; i++)
        if (!_wcsicmp(g_prof[i].id, id)) return i;
    return -1;
}

static const wchar_t* ProfileName(const wchar_t* id)
{
    int i = ProfileIndex(id);
    if (i < 0) return id[0] ? id : L"\x2014";
    const Profile& p = g_prof[i];
    if (p.own) {   // "Своя (Шутер...)" — на текущем языке
        static wchar_t buf[128];
        _snwprintf(buf, 128, TR(L"Своя (%ls)", L"Custom (%ls)"), p.baseName[0] ? p.baseName : TR(L"пустая", L"empty"));
        buf[127] = 0;
        return buf;
    }
    if (g_lang == 1 && p.nameEn[0]) return p.nameEn;
    if (g_lang == 0 && p.nameRu[0]) return p.nameRu;
    return p.name;
}

// ---------------------------------------------------------------------------
// Движок маппинга: отдельный поток, 125 Гц
// ---------------------------------------------------------------------------
static Profile          g_mapProf;            // копия профиля для потока
static HANDLE           g_mapThread = NULL;
static volatile LONG    g_mapStop = 0;
static CRITICAL_SECTION g_pidLock;
static DWORD            g_mapPids[64];
static int              g_mapPidCount = 0;

static void SendKey(int code, bool up)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    if (code >= 256) {
        in.type = INPUT_MOUSE;
        switch (code) {
        case OUT_MOUSE_L: in.mi.dwFlags = up ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN; break;
        case OUT_MOUSE_R: in.mi.dwFlags = up ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN; break;
        case OUT_MOUSE_M: in.mi.dwFlags = up ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_MIDDLEDOWN; break;
        case OUT_WHEEL_UP:
        case OUT_WHEEL_DOWN:
            if (up) return;
            in.mi.dwFlags = MOUSEEVENTF_WHEEL;
            in.mi.mouseData = (DWORD)(code == OUT_WHEEL_UP ? WHEEL_DELTA : -WHEEL_DELTA);
            break;
        default: return;
        }
    } else {
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = (WORD)(code & 0x7F);
        in.ki.dwFlags = KEYEVENTF_SCANCODE;
        if (code & 0x80) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        if (up) in.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    SendInput(1, &in, sizeof(INPUT));
}

static void SendMouseMove(int dx, int dy)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));
}

static bool GameHasFocus()
{
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    bool ok = false;
    EnterCriticalSection(&g_pidLock);
    for (int i = 0; i < g_mapPidCount; i++)
        if (g_mapPids[i] == pid) { ok = true; break; }
    LeaveCriticalSection(&g_pidLock);
    return ok;
}

static void StickToMouse(SHORT sx, SHORT sy, const Profile& p, float dt, float& fx, float& fy)
{
    float x = sx / 32767.0f, y = sy / 32767.0f;
    float mag = sqrtf(x * x + y * y);
    float dz = p.deadzone / 32767.0f;
    if (mag <= dz || mag < 0.0001f) return;
    float m = (mag - dz) / (1.0f - dz);
    if (m > 1.0f) m = 1.0f;
    float sp = p.mouseSpeed * powf(m, p.mouseCurve) * dt;
    fx += x / mag * sp;
    fy -= y / mag * sp;     // у стика "вверх" — плюс, у экрана — минус
}

static DWORD WINAPI MapThread(LPVOID)
{
    const Profile& p = g_mapProf;
    const DWORD kMinHold = 40;      // мс: старые игры пропускают слишком короткие нажатия
    const DWORD kWheelRepeat = 120;
    static bool  actual[OUT_COUNT];
    static DWORD downAt[OUT_COUNT];
    static DWORD wheelNext[OUT_COUNT];
    bool desired[OUT_COUNT];
    ZeroMemory(actual, sizeof(actual));
    ZeroMemory(wheelNext, sizeof(wheelNext));
    float fx = 0, fy = 0;
    DWORD last = GetTickCount();

    timeBeginPeriod(1);
    for (;;) {
        bool stop = g_mapStop != 0;
        DWORD now = GetTickCount();
        float dt = (now - last) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        last = now;

        ZeroMemory(desired, sizeof(desired));
        XI_STATE st;
        ZeroMemory(&st, sizeof(st));
        int idx = g_padIdx;
        bool active = !stop && idx >= 0 && g_XGetState && GameHasFocus()
                      && g_XGetState((DWORD)idx, &st) == ERROR_SUCCESS;

        if (active) {
            const XI_GAMEPAD& gp = st.Gamepad;
            bool in[PI_COUNT];
            ZeroMemory(in, sizeof(in));
            WORD b = gp.wButtons;
            in[PI_A] = (b & XI_A) != 0;          in[PI_B] = (b & XI_B) != 0;
            in[PI_X] = (b & XI_X) != 0;          in[PI_Y] = (b & XI_Y) != 0;
            in[PI_LB] = (b & XI_LB) != 0;        in[PI_RB] = (b & XI_RB) != 0;
            in[PI_BACK] = (b & XI_BACK) != 0;    in[PI_START] = (b & XI_START) != 0;
            in[PI_LS] = (b & 0x0040) != 0;       in[PI_RS] = (b & 0x0080) != 0;
            in[PI_DUP] = (b & XI_DPAD_UP) != 0;  in[PI_DDOWN] = (b & XI_DPAD_DOWN) != 0;
            in[PI_DLEFT] = (b & XI_DPAD_LEFT) != 0; in[PI_DRIGHT] = (b & XI_DPAD_RIGHT) != 0;
            if (in[PI_BACK] && in[PI_START]) in[PI_BACK] = in[PI_START] = false;   // это комбинация закрытия
            in[PI_LT] = gp.bLeftTrigger  > p.triggerThreshold;
            in[PI_RT] = gp.bRightTrigger > p.triggerThreshold;
            int t = p.stickThreshold;
            if (p.lstick == STICK_KEYS) {
                in[PI_LS_UP] = gp.sThumbLY > t;    in[PI_LS_DOWN] = gp.sThumbLY < -t;
                in[PI_LS_LEFT] = gp.sThumbLX < -t; in[PI_LS_RIGHT] = gp.sThumbLX > t;
            }
            if (p.rstick == STICK_KEYS) {
                in[PI_RS_UP] = gp.sThumbRY > t;    in[PI_RS_DOWN] = gp.sThumbRY < -t;
                in[PI_RS_LEFT] = gp.sThumbRX < -t; in[PI_RS_RIGHT] = gp.sThumbRX > t;
            }

            bool layer = p.layerBtn >= 0 && in[p.layerBtn];
            for (int i = 0; i < PI_COUNT; i++) {
                if (!in[i] || i == p.layerBtn) continue;
                const Binding& bd = (layer && p.layer[i].n) ? p.layer[i] : p.base[i];
                for (int k = 0; k < bd.n; k++) desired[bd.out[k]] = true;
            }

            if (p.lstick == STICK_MOUSE) StickToMouse(gp.sThumbLX, gp.sThumbLY, p, dt, fx, fy);
            if (p.rstick == STICK_MOUSE) StickToMouse(gp.sThumbRX, gp.sThumbRY, p, dt, fx, fy);
            int ix = (int)fx, iy = (int)fy;
            if (ix || iy) { SendMouseMove(ix, iy); fx -= ix; fy -= iy; }
        } else {
            fx = fy = 0;
        }

        // Нажатия: сначала модификаторы, потом остальное
        for (int pass = 0; pass < 2; pass++)
            for (int o = 0; o < OUT_WHEEL_UP; o++)
                if (desired[o] && !actual[o] && IsModifier(o) == (pass == 0)) {
                    SendKey(o, false);
                    actual[o] = true;
                    downAt[o] = now;
                }
        // Отпускания: сначала обычные, потом модификаторы; при остановке — сразу
        for (int pass = 0; pass < 2; pass++)
            for (int o = 0; o < OUT_WHEEL_UP; o++)
                if (!desired[o] && actual[o] && IsModifier(o) == (pass == 1)
                    && (stop || now - downAt[o] >= kMinHold)) {
                    SendKey(o, true);
                    actual[o] = false;
                }
        // Колесо — импульсами, пока кнопка зажата
        for (int o = OUT_WHEEL_UP; o <= OUT_WHEEL_DOWN; o++) {
            if (desired[o]) {
                if (!wheelNext[o] || (LONG)(now - wheelNext[o]) >= 0) {
                    SendKey(o, false);
                    wheelNext[o] = now + kWheelRepeat;
                }
            } else {
                wheelNext[o] = 0;
            }
        }

        if (stop) break;
        Sleep(8);
    }
    timeEndPeriod(1);
    return 0;
}

static void MapSetPids(const DWORD* pids, int n)
{
    EnterCriticalSection(&g_pidLock);
    if (n > 64) n = 64;
    memcpy(g_mapPids, pids, n * sizeof(DWORD));
    g_mapPidCount = n;
    LeaveCriticalSection(&g_pidLock);
}

static void MapStop()
{
    if (!g_mapThread) return;
    InterlockedExchange(&g_mapStop, 1);
    WaitForSingleObject(g_mapThread, 2000);
    CloseHandle(g_mapThread);
    g_mapThread = NULL;
    MapSetPids(NULL, 0);
}

// Запуск маппинга для игры; возвращает true, если он включён
static bool MapStart(const Game& g, DWORD pid)
{
    MapStop();
    if (g.input != 0) return false;           // игра читает пад сама
    int pi = ProfileIndex(g.profile);
    if (pi < 0 || g_prof[pi].passthrough) return false;

    g_mapProf = g_prof[pi];
    // Переопределение мыши из настроек игры: 0 — как в профиле
    if (g.mouse == 1) g_mapProf.rstick = STICK_MOUSE;
    if (g.mouse == 2) g_mapProf.lstick = STICK_MOUSE;
    if (g.mouse == 3) {
        if (g_mapProf.lstick == STICK_MOUSE) g_mapProf.lstick = STICK_NONE;
        if (g_mapProf.rstick == STICK_MOUSE) g_mapProf.rstick = STICK_NONE;
    }

    MapSetPids(&pid, 1);
    InterlockedExchange(&g_mapStop, 0);
    DWORD tid;
    g_mapThread = CreateThread(NULL, 0, MapThread, NULL, 0, &tid);
    if (!g_mapThread) return false;
    SetThreadPriority(g_mapThread, THREAD_PRIORITY_ABOVE_NORMAL);
    return true;
}

// ---------------------------------------------------------------------------
// Подписи клавиш, свои раскладки игр (profiles_games.ini)
// ---------------------------------------------------------------------------
static void ShowMessage(const wchar_t* title, const wchar_t* text);


static void Cat(wchar_t* buf, int cap, const wchar_t* s)
{
    size_t n = wcslen(buf);
    if ((int)n >= cap - 1) return;
    wcsncpy(buf + n, s, cap - 1 - n);
    buf[cap - 1] = 0;
}

static const wchar_t* KeyIniName(int code)
{
    for (int i = 0; i < LISTN(kKeys); i++)
        if (kKeys[i].code == code) return kKeys[i].name;
    return NULL;
}

// Короткая подпись для экрана
static const wchar_t* KeyLabel(int code)
{
    switch (code) {
    case OUT_MOUSE_L:    return TR(L"Мышь Л", L"Mouse L");
    case OUT_MOUSE_R:    return TR(L"Мышь П", L"Mouse R");
    case OUT_MOUSE_M:    return TR(L"Мышь ср.", L"Mouse M");
    case OUT_WHEEL_UP:   return TR(L"Колесо \x2191", L"Wheel \x2191");
    case OUT_WHEEL_DOWN: return TR(L"Колесо \x2193", L"Wheel \x2193");
    case 0x01: return L"Esc";
    case 0x0E: return L"BkSp";
    case 0x0F: return L"Tab";
    case 0x1C: return L"Enter";
    case 0x39: return TR(L"Пробел", L"Space");
    case 0x3A: return L"Caps";
    case 0x0C: return L"-";
    case 0x0D: return L"=";
    case 0x1A: return L"[";
    case 0x1B: return L"]";
    case 0x27: return L";";
    case 0x28: return L"'";
    case 0x29: return L"`";
    case 0x2B: return L"\\";
    case 0x33: return L",";
    case 0x34: return L".";
    case 0x35: return L"/";
    case 0x1D: return L"Ctrl";
    case 0x2A: return L"Shift";
    case 0x38: return L"Alt";
    case 0x36: return L"R.Shift";
    case 0x9D: return L"R.Ctrl";
    case 0xB8: return L"R.Alt";
    case 0x37: return L"Num *";
    case 0x4A: return L"Num -";
    case 0x4E: return L"Num +";
    case 0x53: return L"Num .";
    case 0x9C: return L"Num Ent";
    case 0xB5: return L"Num /";
    case 0xC8: return L"\x2191";
    case 0xD0: return L"\x2193";
    case 0xCB: return L"\x2190";
    case 0xCD: return L"\x2192";
    case 0xC7: return L"Home";
    case 0xCF: return L"End";
    case 0xC9: return L"PgUp";
    case 0xD1: return L"PgDn";
    case 0xD2: return L"Ins";
    case 0xD3: return L"Del";
    }
    static const WORD numCodes[10] = { 0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49 };
    static const wchar_t* numLabels[10] = { L"Num 0", L"Num 1", L"Num 2", L"Num 3", L"Num 4",
                                            L"Num 5", L"Num 6", L"Num 7", L"Num 8", L"Num 9" };
    for (int i = 0; i < 10; i++) if (code == numCodes[i]) return numLabels[i];
    const wchar_t* n = KeyIniName(code);
    return n ? n : L"?";
}

static void BindingText(const Binding& b, wchar_t* buf, int cap, bool pretty)
{
    buf[0] = 0;
    for (int k = 0; k < b.n; k++) {
        const wchar_t* nm = pretty ? KeyLabel(b.out[k]) : KeyIniName(b.out[k]);
        if (!nm) continue;
        if (buf[0]) Cat(buf, cap, L"+");
        Cat(buf, cap, nm);
    }
}

static void OwnProfileId(const Game& g, wchar_t* out)
{
    _snwprintf(out, 32, L"g_%ls", g.id);
    out[31] = 0;
}

static void SaveOwnProfiles()
{
    const int cap = 64 * 1024;
    wchar_t* t = (wchar_t*)malloc(cap * sizeof(wchar_t));
    if (!t) return;
    t[0] = 0;
    Cat(t, cap, TR(L"; Раскладки отдельных игр. Файл пишет редактор раскладки в лончере.\r\n", L"; Per-game layouts. This file is written by the launcher's layout editor.\r\n"));
    static const wchar_t* sm[3] = { L"none", L"keys", L"mouse" };
    for (int i = 0; i < g_profCount; i++) {
        const Profile& p = g_prof[i];
        if (!p.own) continue;
        wchar_t line[512];
        _snwprintf(line, 512, L"\r\n[%ls]\r\nname=%ls\r\nbase=%ls\r\nlstick=%ls\r\nrstick=%ls\r\n"
                              L"mouse_speed=%d\r\nmouse_curve=%.2f\r\ndeadzone=%d\r\n"
                              L"stick_threshold=%d\r\ntrigger_threshold=%d\r\n",
                   p.id, p.name, p.baseName, sm[p.lstick], sm[p.rstick], p.mouseSpeed, (double)p.mouseCurve,
                   p.deadzone, p.stickThreshold, p.triggerThreshold);
        line[511] = 0;
        Cat(t, cap, line);
        if (p.layerBtn >= 0) {
            _snwprintf(line, 512, L"layer=%ls\r\n", kPadNames[p.layerBtn]);
            line[511] = 0;
            Cat(t, cap, line);
        }
        for (int lay = 0; lay < 2; lay++)
            for (int k = 0; k < PI_COUNT; k++) {
                const Binding& b = lay ? p.layer[k] : p.base[k];
                if (!b.n) continue;
                wchar_t v[96];
                BindingText(b, v, 96, false);
                _snwprintf(line, 512, L"%ls%ls=%ls\r\n", lay ? L"^" : L"", kPadNames[k], v);
                line[511] = 0;
                Cat(t, cap, line);
            }
    }
    WriteUtf8File(g_ownProfilesIni, t, true);
    free(t);
}

// ---------------------------------------------------------------------------
// Редактор раскладки: данные
// ---------------------------------------------------------------------------
enum { ER_LSTICK, ER_RSTICK, ER_SPEED, ER_CURVE, ER_LAYERBTN, ER_BIND };
struct EdRow { int kind; int pad; };
static EdRow g_edRows[48];
static int   g_edCount = 0, g_edSel = 0, g_edTop = 0;
static int   g_edProf = -1, g_edGame = -1;
static bool  g_edLayer = false;

static const int kLayerChoices[] = { -1, PI_LB, PI_RB, PI_LT, PI_RT, PI_LS, PI_RS, PI_BACK };
static const wchar_t* kStickModesRu[5] = { L"Не используется", L"WASD", L"Стрелки", L"Свои клавиши", L"Мышь" };
static const wchar_t* kStickModesEn[5] = { L"Not used", L"WASD", L"Arrows", L"Custom keys", L"Mouse" };
#define kStickModes TR(kStickModesRu, kStickModesEn)
static const wchar_t* kPadLabelsRu[PI_COUNT] = {
    L"A", L"B", L"X", L"Y", L"LB", L"RB", L"LT (курок)", L"RT (курок)", L"Back", L"Start",
    L"Нажатие левого стика", L"Нажатие правого стика",
    L"Крестовина вверх", L"Крестовина вниз", L"Крестовина влево", L"Крестовина вправо",
    L"Левый стик вверх", L"Левый стик вниз", L"Левый стик влево", L"Левый стик вправо",
    L"Правый стик вверх", L"Правый стик вниз", L"Правый стик влево", L"Правый стик вправо"
};
static const wchar_t* kPadLabelsEn[PI_COUNT] = {
    L"A", L"B", L"X", L"Y", L"LB", L"RB", L"LT (trigger)", L"RT (trigger)", L"Back", L"Start",
    L"Left stick click", L"Right stick click",
    L"D-pad up", L"D-pad down", L"D-pad left", L"D-pad right",
    L"Left stick up", L"Left stick down", L"Left stick left", L"Left stick right",
    L"Right stick up", L"Right stick down", L"Right stick left", L"Right stick right"
};
#define kPadLabels TR(kPadLabelsRu, kPadLabelsEn)

static bool BindIs(const Binding& b, const wchar_t* key)
{
    return b.n == 1 && (int)b.out[0] == KeyCode(key);
}

static int StickModeIdx(const Profile& p, bool left)
{
    int mode = left ? p.lstick : p.rstick;
    if (mode == STICK_NONE)  return 0;
    if (mode == STICK_MOUSE) return 4;
    int f = left ? PI_LS_UP : PI_RS_UP;
    if (BindIs(p.base[f], L"W") && BindIs(p.base[f + 1], L"S") &&
        BindIs(p.base[f + 2], L"A") && BindIs(p.base[f + 3], L"D")) return 1;
    if (BindIs(p.base[f], L"UP") && BindIs(p.base[f + 1], L"DOWN") &&
        BindIs(p.base[f + 2], L"LEFT") && BindIs(p.base[f + 3], L"RIGHT")) return 2;
    return 3;
}

static void SetStickMode(Profile& p, bool left, int idx)
{
    int f = left ? PI_LS_UP : PI_RS_UP;
    int cur = StickModeIdx(p, left);
    int mode = STICK_KEYS;
    if (idx == 0) mode = STICK_NONE;
    else if (idx == 4) mode = STICK_MOUSE;
    else if (idx == 1 || idx == 2) {
        static const wchar_t* k1[4] = { L"W", L"S", L"A", L"D" };
        static const wchar_t* k2[4] = { L"UP", L"DOWN", L"LEFT", L"RIGHT" };
        for (int i = 0; i < 4; i++) ParseBinding(idx == 1 ? k1[i] : k2[i], p.base[f + i]);
    } else if (idx == 3 && (cur == 1 || cur == 2)) {
        for (int i = 0; i < 4; i++) p.base[f + i].n = 0;   // свои — начинаем с пустых
    }
    if (left) p.lstick = mode; else p.rstick = mode;
}

static void EdAddRow(int kind, int pad)
{
    if (g_edCount >= 48) return;
    g_edRows[g_edCount].kind = kind;
    g_edRows[g_edCount].pad = pad;
    g_edCount++;
}

static void BuildEdRows()
{
    g_edCount = 0;
    if (g_edProf < 0) return;
    const Profile& p = g_prof[g_edProf];
    EdAddRow(ER_LSTICK, 0);
    EdAddRow(ER_RSTICK, 0);
    if (p.lstick == STICK_MOUSE || p.rstick == STICK_MOUSE) {
        EdAddRow(ER_SPEED, 0);
        EdAddRow(ER_CURVE, 0);
    }
    EdAddRow(ER_LAYERBTN, 0);
    for (int i = 0; i <= PI_DRIGHT; i++) if (i != p.layerBtn) EdAddRow(ER_BIND, i);
    if (p.lstick == STICK_KEYS) for (int i = PI_LS_UP; i <= PI_LS_RIGHT; i++) EdAddRow(ER_BIND, i);
    if (p.rstick == STICK_KEYS) for (int i = PI_RS_UP; i <= PI_RS_RIGHT; i++) EdAddRow(ER_BIND, i);
    if (g_edSel >= g_edCount) g_edSel = g_edCount - 1;
    if (g_edSel < 0) g_edSel = 0;
    if (p.layerBtn < 0) g_edLayer = false;
}

// Перед первой правкой: общий профиль копируется в "свой" для этой игры
static bool EnsureOwnProfile()
{
    if (g_edGame < 0) return false;
    Game& g = g_games[g_edGame];
    wchar_t own[32];
    OwnProfileId(g, own);
    if (g_edProf >= 0 && !_wcsicmp(g_prof[g_edProf].id, own)) return true;

    int dst = ProfileIndex(own);
    if (dst < 0) {
        if (g_profCount >= kMaxProfiles) {
            ShowMessage(TR(L"Слишком много профилей", L"Too many profiles"),
                        TR(L"Удалите лишние профили из profiles.ini или profiles_games.ini.",
                           L"Remove unused profiles from profiles.ini or profiles_games.ini."));
            return false;
        }
        dst = g_profCount++;
    }
    Profile np;
    if (g_edProf >= 0) np = g_prof[g_edProf];
    else InitProfile(np, own);
    wchar_t baseName[64];
    COPY(baseName, g_edProf >= 0 ? ProfileName(g_prof[g_edProf].id) : TR(L"пустая", L"empty"));
    COPY(np.id, own);
    _snwprintf(np.name, 64, TR(L"Своя (%ls)", L"Custom (%ls)"), baseName);
    np.name[63] = 0;
    COPY(np.baseName, baseName);
    np.own = true;
    np.passthrough = false;
    g_prof[dst] = np;
    g_edProf = dst;

    COPY(g.profile, own);
    g.input = 0;          // раз настраиваем раскладку — значит, нужен маппинг
    g.ovr = true;
    SaveGameState(g);
    SaveOwnProfiles();
    return true;
}

// ---------------------------------------------------------------------------
// Выбор клавиши: сетка
// ---------------------------------------------------------------------------
static const wchar_t* kPickNames[] = {
    L"+CTRL", L"+SHIFT", L"+ALT", L"ESC", L"TAB", L"ENTER", L"SPACE", L"BACKSPACE", L"CAPSLOCK", L"TILDE",
    L"UP", L"DOWN", L"LEFT", L"RIGHT", L"HOME", L"END", L"PGUP", L"PGDN", L"INSERT", L"DELETE",
    L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"0",
    L"Q", L"W", L"E", L"R", L"T", L"Y", L"U", L"I", L"O", L"P",
    L"A", L"S", L"D", L"F", L"G", L"H", L"J", L"K", L"L", L"SEMICOLON",
    L"Z", L"X", L"C", L"V", L"B", L"N", L"M", L"COMMA", L"PERIOD", L"SLASH",
    L"F1", L"F2", L"F3", L"F4", L"F5", L"F6", L"F7", L"F8", L"F9", L"F10",
    L"F11", L"F12", L"SHIFT", L"CTRL", L"ALT", L"RSHIFT", L"RCTRL", L"RALT", L"MINUS", L"EQUALS",
    L"LBRACKET", L"RBRACKET", L"QUOTE", L"BACKSLASH", L"NUMPLUS", L"NUMMINUS", L"NUMMUL", L"NUMDIV", L"NUMDOT", L"NUMENTER",
    L"NUM0", L"NUM1", L"NUM2", L"NUM3", L"NUM4", L"NUM5", L"NUM6", L"NUM7", L"NUM8", L"NUM9",
    L"MOUSE_LEFT", L"MOUSE_RIGHT", L"MOUSE_MIDDLE", L"WHEEL_UP", L"WHEEL_DOWN"
};
static const int kPickCols = 10;
static int  g_kpCodes[LISTN(kPickNames)];   // -10/-11/-12 — переключатели Ctrl/Shift/Alt
static int  g_kpN = 0, g_kpSel = 0, g_kpTop = 0, g_kpMods = 0, g_kpPad = 0;
static bool g_kpLayer = false;

static void InitPicker()
{
    g_kpN = LISTN(kPickNames);
    for (int i = 0; i < g_kpN; i++) {
        if      (!wcscmp(kPickNames[i], L"+CTRL"))  g_kpCodes[i] = -10;
        else if (!wcscmp(kPickNames[i], L"+SHIFT")) g_kpCodes[i] = -11;
        else if (!wcscmp(kPickNames[i], L"+ALT"))   g_kpCodes[i] = -12;
        else g_kpCodes[i] = KeyCode(kPickNames[i]);
    }
}

static void OpenPicker(int pad)
{
    if (g_edProf < 0) return;
    const Profile& p = g_prof[g_edProf];
    const Binding& b = g_edLayer ? p.layer[pad] : p.base[pad];
    g_kpPad = pad;
    g_kpLayer = g_edLayer;
    g_kpMods = 0;
    int key = -1;
    if (b.n == 1) key = b.out[0];
    else {
        for (int k = 0; k < b.n; k++) {
            int c = b.out[k];
            bool last = (k == b.n - 1);
            if (!last && c == 0x1D) g_kpMods |= 1;
            else if (!last && c == 0x2A) g_kpMods |= 2;
            else if (!last && c == 0x38) g_kpMods |= 4;
            else key = c;
        }
    }
    g_kpSel = 3;
    for (int i = 0; i < g_kpN; i++)
        if (g_kpCodes[i] == key) { g_kpSel = i; break; }
    g_kpTop = 0;
    g_mode = MODE_KEYPICK;
}

static void PickerSet(int code)   // code < 0 — очистить
{
    if (!EnsureOwnProfile()) return;
    Profile& p = g_prof[g_edProf];
    Binding& b = g_kpLayer ? p.layer[g_kpPad] : p.base[g_kpPad];
    b.n = 0;
    if (code >= 0) {
        if ((g_kpMods & 1) && code != 0x1D) b.out[b.n++] = 0x1D;
        if ((g_kpMods & 2) && code != 0x2A) b.out[b.n++] = 0x2A;
        if ((g_kpMods & 4) && code != 0x38) b.out[b.n++] = 0x38;
        if (b.n > 2) b.n = 2;
        b.out[b.n++] = (WORD)code;
    }
    SaveOwnProfiles();
    g_mode = MODE_EDITOR;
    BuildEdRows();
}

// Нажатие на настоящей клавиатуре в окне выбора
static void PickerPhysicalKey(LPARAM lp)
{
    if (lp & (1 << 30)) return;   // автоповтор
    int scan = (int)((lp >> 16) & 0xFF);
    if (scan == 0 || scan >= 0x80) return;
    int code = scan | ((lp & (1 << 24)) ? 0x80 : 0);
    if (code == 0x1D || code == 0x9D) { g_kpMods ^= 1; return; }
    if (code == 0x2A || code == 0x36) { g_kpMods ^= 2; return; }
    if (code == 0x38 || code == 0xB8) { g_kpMods ^= 4; return; }
    if (!KeyIniName(code)) return;
    PickerSet(code);
}

// ---------------------------------------------------------------------------
// Меню лончера, сортировка, звуки, автозапуск, питание
// ---------------------------------------------------------------------------
enum { MI_SORT, MI_THEME, MI_BG, MI_VEIL, MI_LANG, MI_SOUND, MI_AUTORUN, MI_RESCAN, MI_EXIT, MI_REBOOT, MI_POWEROFF, MI_COUNT };

// Фон: картинки из папки backgrounds, затемнение — полупрозрачная заливка цветом темы
static wchar_t g_bgName[MAX_PATH] = L"";
static int     g_veil = 1;
static const BYTE kVeil[4] = { 70, 120, 170, 210 };
static const wchar_t* kVeilNamesRu[4] = { L"Слабое", L"Среднее", L"Сильное", L"Максимальное" };
static const wchar_t* kVeilNamesEn[4] = { L"Light", L"Medium", L"Strong", L"Maximum" };
#define kVeilNames TR(kVeilNamesRu, kVeilNamesEn)
static wchar_t g_bgList[48][MAX_PATH];
static int     g_bgCount = 0;
static int  g_menuSel = 0, g_menuArmed = -1;
static bool g_autorun = false;
static int  g_sortMode = -1;     // -1 — как в games.ini
static bool g_sound = true;
static const wchar_t* kSortNamesRu[4] = { L"По названию", L"По времени в игре", L"По году выхода", L"Как найдены" };
static const wchar_t* kSortNamesEn[4] = { L"By name", L"By play time", L"By release year", L"As found" };
#define kSortNames TR(kSortNamesRu, kSortNamesEn)

static int SortMode() { return g_sortMode >= 0 ? g_sortMode : (g_sortByName ? 0 : 3); }

static void SaveLauncherState()
{
    EnsureStateFile();
    wchar_t buf[16];
    if (g_sortMode >= 0) {
        _snwprintf(buf, 16, L"%d", g_sortMode); buf[15] = 0;
        WritePrivateProfileStringW(L"@launcher", L"sort", buf, g_stateIni);
    }
    WritePrivateProfileStringW(L"@launcher", L"sound", g_sound ? L"1" : L"0", g_stateIni);
    WritePrivateProfileStringW(L"@launcher", L"theme", kThemes[g_themeIdx].id, g_stateIni);
    WritePrivateProfileStringW(L"@launcher", L"background", g_bgName[0] ? g_bgName : L"-", g_stateIni);
    _snwprintf(buf, 16, L"%d", g_veil); buf[15] = 0;
    WritePrivateProfileStringW(L"@launcher", L"veil", buf, g_stateIni);
    WritePrivateProfileStringW(L"@launcher", L"lang", g_lang ? L"en" : L"ru", g_stateIni);
}

static void LoadLauncherState()
{
    g_sortMode = (int)GetPrivateProfileIntW(L"@launcher", L"sort", (UINT)-1, g_stateIni);
    if (g_sortMode < -1 || g_sortMode > 3) g_sortMode = -1;
    g_sound = GetPrivateProfileIntW(L"@launcher", L"sound", 1, g_stateIni) != 0;
    wchar_t buf[MAX_PATH];
    GetPrivateProfileStringW(L"@launcher", L"theme", L"", buf, MAX_PATH, g_stateIni);
    g_themeIdx = 0;
    for (int i = 0; i < kThemeCount; i++) if (!_wcsicmp(kThemes[i].id, buf)) g_themeIdx = i;
    GetPrivateProfileStringW(L"@launcher", L"background", L"-", buf, MAX_PATH, g_stateIni);
    if (wcscmp(buf, L"-")) COPY(g_bgName, buf); else g_bgName[0] = 0;
    g_veil = (int)GetPrivateProfileIntW(L"@launcher", L"veil", 1, g_stateIni);
    if (g_veil < 0 || g_veil > 3) g_veil = 1;
}

static void ListBackgrounds()
{
    g_bgCount = 0;
    wchar_t mask[MAX_PATH];
    _snwprintf(mask, MAX_PATH, L"%ls\\backgrounds\\*", g_baseDir);
    mask[MAX_PATH - 1] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    static const wchar_t* ext[] = { L".jpg", L".jpeg", L".png", L".bmp", L".gif" };
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const wchar_t* dot = wcsrchr(fd.cFileName, L'.');
        if (!dot) continue;
        bool ok = false;
        for (int i = 0; i < 5; i++) if (!_wcsicmp(dot, ext[i])) ok = true;
        if (ok && g_bgCount < 48) { COPY(g_bgList[g_bgCount], fd.cFileName); g_bgCount++; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (int i = 1; i < g_bgCount; i++)   // по алфавиту
        for (int j = i; j > 0 && lstrcmpiW(g_bgList[j - 1], g_bgList[j]) > 0; j--) {
            wchar_t t[MAX_PATH];
            COPY(t, g_bgList[j]); COPY(g_bgList[j], g_bgList[j - 1]); COPY(g_bgList[j - 1], t);
        }
}

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static bool AutorunGet()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    LONG r = RegQueryValueExW(k, L"LGL", NULL, &type, NULL, &size);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static void AutorunSet(bool on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    RegDeleteValueW(k, L"RetroLauncher");   // имя из ранних версий
    if (on) {
        wchar_t exe[MAX_PATH], val[MAX_PATH + 4];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        exe[MAX_PATH - 1] = 0;
        _snwprintf(val, MAX_PATH + 4, L"\"%ls\"", exe);
        val[MAX_PATH + 3] = 0;
        RegSetValueExW(k, L"LGL", 0, REG_SZ, (const BYTE*)val, (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"LGL");
    }
    RegCloseKey(k);
}

static void SystemPower(bool reboot)
{
    HANDLE tok;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid))
            AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
        CloseHandle(tok);
    }
    ExitWindowsEx(reboot ? EWX_REBOOT : EWX_POWEROFF, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
}

// Звуки: sounds\move.wav, select.wav, back.wav — грузятся в память один раз
static BYTE* g_snd[3] = { NULL, NULL, NULL };

static void LoadSounds()
{
    static const wchar_t* names[3] = { L"move.wav", L"select.wav", L"back.wav" };
    for (int i = 0; i < 3; i++) {
        wchar_t path[MAX_PATH];
        _snwprintf(path, MAX_PATH, L"%ls\\sounds\\%ls", g_baseDir, names[i]);
        path[MAX_PATH - 1] = 0;
        HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        DWORD size = GetFileSize(f, NULL);
        if (size != INVALID_FILE_SIZE && size > 44 && size < 2 * 1024 * 1024) {
            BYTE* buf = (BYTE*)malloc(size);
            DWORD rd = 0;
            if (buf && ReadFile(f, buf, size, &rd, NULL) && rd == size) g_snd[i] = buf;
            else free(buf);
        }
        CloseHandle(f);
    }
}

static void Snd(int i)
{
    if (g_sound && i >= 0 && i < 3 && g_snd[i])
        PlaySoundW((LPCWSTR)g_snd[i], NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// Прыжки по первой букве
static wchar_t g_letter = 0;
static DWORD   g_letterTick = 0;

static wchar_t FirstLetter(const wchar_t* s)
{
    for (; *s; s++) {
        if (iswdigit(*s)) return L'#';
        if (iswalpha(*s)) return (wchar_t)towupper(*s);
    }
    return L'#';
}

static int CmpByName(const void* a, const void* b)
{
    return lstrcmpiW(g_games[*(const int*)a].name, g_games[*(const int*)b].name);
}

static int CmpByTime(const void* a, const void* b)
{
    const Game& x = g_games[*(const int*)a];
    const Game& y = g_games[*(const int*)b];
    if (x.playSec != y.playSec) return x.playSec > y.playSec ? -1 : 1;
    return lstrcmpiW(x.name, y.name);
}

static int CmpByYear(const void* a, const void* b)
{
    const Game& x = g_games[*(const int*)a];
    const Game& y = g_games[*(const int*)b];
    int yx = x.year > 0 ? x.year : 99999, yy = y.year > 0 ? y.year : 99999;
    if (yx != yy) return yx < yy ? -1 : 1;
    return lstrcmpiW(x.name, y.name);
}

static void BuildOrder()
{
    for (int i = 0; i < g_gameCount; i++) g_order[i] = i;
    if (g_gameCount < 2) return;
    switch (SortMode()) {
    case 0: qsort(g_order, g_gameCount, sizeof(int), CmpByName); break;
    case 1: qsort(g_order, g_gameCount, sizeof(int), CmpByTime); break;
    case 2: qsort(g_order, g_gameCount, sizeof(int), CmpByYear); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Вспомогательное рисование
// ---------------------------------------------------------------------------
static void Fill(HDC dc, const RECT& r, COLORREF c)
{
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void RFill(HDC dc, const RECT& r, int rad, COLORREF c)
{
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    SetDCBrushColor(dc, c);
    RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
}

static void Circle(HDC dc, int cx, int cy, int d, COLORREF c)
{
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    SetDCBrushColor(dc, c);
    int x = cx - d / 2, y = cy - d / 2;
    Ellipse(dc, x, y, x + d + 1, y + d + 1);
    SelectObject(dc, ob);
    SelectObject(dc, op);
}

static void Txt(HDC dc, const wchar_t* s, RECT r, HFONT f, COLORREF c, UINT fl)
{
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fl | DT_NOPREFIX);
}

static int TxtW(HDC dc, const wchar_t* s, HFONT f)
{
    SIZE sz;
    SelectObject(dc, f);
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    return sz.cx;
}

static int GlyphW(HDC dc, const wchar_t* lbl)
{
    if (wcslen(lbl) <= 1) return g_glyphD;
    int w = TxtW(dc, lbl, g_fBtn) + g_glyphD * 2 / 3;
    return w < g_glyphD ? g_glyphD : w;
}

static int Glyph(HDC dc, int x, int cy, const wchar_t* lbl, COLORREF c)
{
    int d = g_glyphD;
    int w = GlyphW(dc, lbl);
    RECT r = { x, cy - d / 2, x + w, cy - d / 2 + d };
    if (w == d) Circle(dc, x + d / 2, cy, d, c);
    else        RFill(dc, r, d, c);
    COLORREF tc = (c == C_BTN_Y) ? RGB(30, 30, 30) : RGB(255, 255, 255);
    Txt(dc, lbl, r, g_fBtn, tc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return w;
}

static int Hint(HDC dc, int x, int cy, const wchar_t* g, COLORREF c, const wchar_t* text)
{
    x += Glyph(dc, x, cy, g, c);
    x += g_glyphD / 3;
    int tw = TxtW(dc, text, g_fSmall);
    RECT r = { x, cy - g_glyphD, x + tw + 2, cy + g_glyphD };
    Txt(dc, text, r, g_fSmall, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    return x + tw + g_glyphD;
}

// ---------------------------------------------------------------------------
// Шрифты и буфер
// ---------------------------------------------------------------------------
static HFONT MakeFont(int px, int weight)
{
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, L"Tahoma");
}

static void FreeFonts()
{
    HFONT* a[] = { &g_fTitle, &g_fTab, &g_fRow, &g_fSmall, &g_fBig, &g_fCover, &g_fBtn };
    for (int i = 0; i < (int)(sizeof(a) / sizeof(a[0])); i++)
        if (*a[i]) { DeleteObject(*a[i]); *a[i] = NULL; }
}

static void CreateFonts()
{
    FreeFonts();
    int h = g_H < 300 ? 300 : g_H;
    g_pxSmall = h / 42;
    g_pxBig   = h / 20;
    g_glyphD  = h / 26;
    g_fTitle  = MakeFont(h / 22, FW_BOLD);
    g_fTab    = MakeFont(h / 36, FW_BOLD);
    g_fRow    = MakeFont(h / 28, FW_NORMAL);
    g_fSmall  = MakeFont(g_pxSmall, FW_NORMAL);
    g_fBig    = MakeFont(g_pxBig, FW_BOLD);
    g_fCover  = MakeFont(h / 7, FW_BOLD);
    g_fBtn    = MakeFont(g_glyphD * 6 / 10, FW_BOLD);
}

static void ResizeBuffer(HWND hWnd)
{
    HDC dc = GetDC(hWnd);
    if (g_memDC) {
        SelectObject(g_memDC, g_oldBmp);
        DeleteObject(g_memBmp);
    } else {
        g_memDC = CreateCompatibleDC(dc);
    }
    g_memBmp = CreateCompatibleBitmap(dc, g_W > 0 ? g_W : 1, g_H > 0 ? g_H : 1);
    g_oldBmp = (HBITMAP)SelectObject(g_memDC, g_memBmp);
    ReleaseDC(hWnd, dc);
}

// ---------------------------------------------------------------------------
// Обложки: GDI+ декодирует один раз, уменьшенная копия хранится в кэше
// ---------------------------------------------------------------------------
static void CoverClear()
{
    for (int i = 0; i < kCoverCache; i++) {
        if (g_cov[i].bmp) DeleteObject(g_cov[i].bmp);
        g_cov[i].bmp = NULL;
        g_cov[i].game = -1;
    }
    g_covPendGame = -1;
}

static CoverEntry* CoverFind(int game, int w, int h)
{
    for (int i = 0; i < kCoverCache; i++)
        if (g_cov[i].game == game && g_cov[i].boxW == w && g_cov[i].boxH == h) return &g_cov[i];
    return NULL;
}

static bool ResolveCover(const Game& g, wchar_t* out)
{
    if (g.cover[0]) {
        JoinPath(out, MAX_PATH, g_coversDir, g.cover);
        if (FileExists(out)) return true;
        if (g.root[0] && !IsAbsPath(g.cover)) {
            JoinPath(out, MAX_PATH, g.root, g.cover);
            if (FileExists(out)) return true;
        }
        return false;
    }
    static const wchar_t* ext[] = { L".jpg", L".jpeg", L".png", L".bmp", L".gif" };
    // covers\<папка>.*, covers\<id>.*
    const wchar_t* names[2] = { g.folder[0] ? g.folder : g.id, g.id };
    for (int k = 0; k < 2; k++) {
        if (k == 1 && names[1] == names[0]) break;
        for (int i = 0; i < 5; i++) {
            _snwprintf(out, MAX_PATH, L"%ls\\%ls%ls", g_coversDir, names[k], ext[i]);
            out[MAX_PATH - 1] = 0;
            if (FileExists(out)) return true;
        }
    }
    // cover.* / folder.* внутри папки игры
    if (g.root[0]) {
        static const wchar_t* local[] = { L"cover", L"folder" };
        for (int k = 0; k < 2; k++)
            for (int i = 0; i < 5; i++) {
                _snwprintf(out, MAX_PATH, L"%ls\\%ls%ls", g.root, local[k], ext[i]);
                out[MAX_PATH - 1] = 0;
                if (FileExists(out)) return true;
            }
    }
    return false;
}

static HBITMAP LoadCoverBitmap(const wchar_t* path, int boxW, int boxH, int* outW, int* outH)
{
    if (!g_gdipOk || boxW <= 0 || boxH <= 0) return NULL;
    Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromFile(path, FALSE);
    if (!src) return NULL;
    if (src->GetLastStatus() != Gdiplus::Ok) { delete src; return NULL; }
    int iw = (int)src->GetWidth(), ih = (int)src->GetHeight();
    if (iw <= 0 || ih <= 0) { delete src; return NULL; }

    // Вписываем с сохранением пропорций
    int dw = boxW;
    int dh = (int)((__int64)ih * boxW / iw);
    if (dh > boxH) { dh = boxH; dw = (int)((__int64)iw * boxH / ih); }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = dw;
    bi.bmiHeader.biHeight      = -dh;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib) { delete src; return NULL; }

    HDC mdc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(mdc, dib);
    RECT r = { 0, 0, dw, dh };
    Fill(mdc, r, C_PANEL);   // фон под прозрачные PNG
    {
        Gdiplus::Graphics gr(mdc);
        gr.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        gr.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        Gdiplus::ImageAttributes ia;
        ia.SetWrapMode(Gdiplus::WrapModeTileFlipXY);   // без тёмной каймы по краям
        gr.DrawImage(src, Gdiplus::Rect(0, 0, dw, dh), 0, 0, iw, ih, Gdiplus::UnitPixel, &ia);
    }
    SelectObject(mdc, old);
    DeleteDC(mdc);
    delete src;

    *outW = dw;
    *outH = dh;
    return dib;
}

static void CoverLoad(int game, int boxW, int boxH)
{
    if (game < 0 || game >= g_gameCount || CoverFind(game, boxW, boxH)) return;
    int slot = 0;
    for (int i = 0; i < kCoverCache; i++) {
        if (g_cov[i].game < 0) { slot = i; break; }
        if (g_cov[i].used < g_cov[slot].used) slot = i;
    }
    CoverEntry& e = g_cov[slot];
    if (e.bmp) DeleteObject(e.bmp);
    e.bmp = NULL;
    e.game = game;
    e.boxW = boxW;
    e.boxH = boxH;
    e.w = e.h = 0;
    e.used = GetTickCount();

    wchar_t path[MAX_PATH];
    if (ResolveCover(g_games[game], path))
        e.bmp = LoadCoverBitmap(path, boxW, boxH, &e.w, &e.h);
}

// ---------------------------------------------------------------------------
// Раскладка, список, фильтр
// ---------------------------------------------------------------------------
static void CalcLayout(Layout& L)
{
    int hh = g_H * 9 / 100;
    int fh = g_H * 8 / 100;
    L.margin = g_W / 40;
    int lw = g_W * 46 / 100;
    SetRect(&L.header, 0, 0, g_W, hh);
    SetRect(&L.footer, 0, g_H - fh, g_W, g_H);
    SetRect(&L.list, L.margin, hh + L.margin / 2, lw, g_H - fh - L.margin / 2);
    SetRect(&L.info, lw + L.margin, hh + L.margin / 2, g_W - L.margin, g_H - fh - L.margin / 2);
    L.rowH = g_H * 65 / 1000;
    if (L.rowH < 16) L.rowH = 16;
    L.pad = L.rowH / 3;
    L.visRows = (L.list.bottom - L.list.top - L.pad) / L.rowH;
    if (L.visRows < 1) L.visRows = 1;
}

static Game& Cur() { return g_games[g_filtered[g_sel]]; }

static bool InTab(const Game& g, int tab)
{
    if (tab == 3) return g.hidden;
    if (g.hidden) return false;
    return tab == 0 || (tab == 1 && g.fav) || (tab == 2 && g.last != 0);
}

static int CountTab(int tab)
{
    int n = 0;
    for (int i = 0; i < g_gameCount; i++) if (InTab(g_games[i], tab)) n++;
    return n;
}

static int TabCount() { return CountTab(3) > 0 ? 4 : 3; }

static void EnsureVisible()
{
    Layout L; CalcLayout(L);
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + L.visRows) g_top = g_sel - L.visRows + 1;
    int maxTop = g_nf - L.visRows;
    if (maxTop < 0) maxTop = 0;
    if (g_top > maxTop) g_top = maxTop;
    if (g_top < 0) g_top = 0;
}

static void Refilter(bool keep)
{
    int keepGame = (keep && g_nf > 0) ? g_filtered[g_sel] : -1;
    int oldSel = g_sel;
    g_nf = 0;
    for (int k = 0; k < g_gameCount; k++) {
        int i = g_order[k];
        if (InTab(g_games[i], g_tab)) g_filtered[g_nf++] = i;
    }

    if (g_tab == 2) {   // недавние — сначала свежие
        for (int i = 1; i < g_nf; i++) {
            int v = g_filtered[i], j = i - 1;
            while (j >= 0 && g_games[g_filtered[j]].last < g_games[v].last) {
                g_filtered[j + 1] = g_filtered[j];
                j--;
            }
            g_filtered[j + 1] = v;
        }
    }

    g_sel = keep ? oldSel : 0;
    if (keepGame >= 0)
        for (int i = 0; i < g_nf; i++)
            if (g_filtered[i] == keepGame) { g_sel = i; break; }
    if (g_sel >= g_nf) g_sel = g_nf > 0 ? g_nf - 1 : 0;
    if (g_sel < 0) g_sel = 0;
    EnsureVisible();
}

static void ReloadGames()
{
    wchar_t keepId[64] = L"";
    if (g_nf > 0) COPY(keepId, Cur().id);
    CoverClear();
    LoadGames();
    BuildOrder();
    if (g_tab >= TabCount()) g_tab = 0;
    Refilter(false);
    if (keepId[0])
        for (int i = 0; i < g_nf; i++)
            if (!_wcsicmp(g_games[g_filtered[i]].id, keepId)) { g_sel = i; break; }
    EnsureVisible();
}

static void MoveSel(int d)
{
    if (g_nf == 0) return;
    g_sel += d;
    if (g_sel < 0) g_sel = 0;
    if (g_sel >= g_nf) g_sel = g_nf - 1;
    EnsureVisible();
}

static void Initials(const wchar_t* name, wchar_t* out)
{
    const wchar_t* s = name;
    int n = 0;
    bool start = true;
    for (; *s && n < 2; s++) {
        wchar_t c = *s;
        if (start && (iswupper(c) || iswdigit(c))) { out[n++] = c; start = false; }
        else if (c == L' ' || c == L':' || c == L'-') start = true;
        else start = false;
    }
    if (n == 0 && name[0]) out[n++] = (wchar_t)towupper(name[0]);
    out[n] = 0;
}

static void LastPlayedText(u64 last, wchar_t* buf, int cap)
{
    if (!last) { CopyStr(buf, cap, TR(L"никогда", L"never")); return; }
    const u64 DAY = 864000000000ULL;
    u64 a = ToLocalFT(last), b = ToLocalFT(NowFT());
    u64 da = a / DAY, db = b / DAY;
    int d = db >= da ? (int)(db - da) : 0;
    if (d == 0)      CopyStr(buf, cap, TR(L"сегодня", L"today"));
    else if (d == 1) CopyStr(buf, cap, TR(L"вчера", L"yesterday"));
    else if (d < 60) { _snwprintf(buf, cap, TR(L"%d дн. назад", L"%d days ago"), d); buf[cap - 1] = 0; }
    else {
        FILETIME ft; SYSTEMTIME st;
        ft.dwLowDateTime = (DWORD)a;
        ft.dwHighDateTime = (DWORD)(a >> 32);
        FileTimeToSystemTime(&ft, &st);
        _snwprintf(buf, cap, L"%02d.%02d.%04d", st.wDay, st.wMonth, st.wYear);
        buf[cap - 1] = 0;
    }
}

static void PlayTimeText(UINT sec, int count, wchar_t* buf, int cap)
{
    if (sec == 0 && count == 0) CopyStr(buf, cap, L"\x2014");
    else if (sec < 60)          CopyStr(buf, cap, TR(L"меньше минуты", L"under a minute"));
    else if (sec < 3600)        _snwprintf(buf, cap, TR(L"%u мин", L"%u min"), sec / 60);
    else                        _snwprintf(buf, cap, TR(L"%u ч %02u мин", L"%u h %02u min"), sec / 3600, (sec % 3600) / 60);
    buf[cap - 1] = 0;
}

// ---------------------------------------------------------------------------
// Отрисовка частей
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Подложка: фон (цвет или фото) + панели. Собирается один раз при смене
// размера, темы или фона; кадр просто копирует её.
// ---------------------------------------------------------------------------
static HBITMAP           g_baseBmp = NULL;
static int               g_baseW = 0, g_baseH = 0;
static Gdiplus::Bitmap*  g_bgSrc = NULL;

static void LoadBackgroundSource()
{
    if (g_bgSrc) { delete g_bgSrc; g_bgSrc = NULL; }
    if (!g_bgName[0] || !g_gdipOk) return;
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%ls\\backgrounds\\%ls", g_baseDir, g_bgName);
    path[MAX_PATH - 1] = 0;
    if (!FileExists(path)) return;
    Gdiplus::Bitmap* b = Gdiplus::Bitmap::FromFile(path, FALSE);
    if (b && b->GetLastStatus() == Gdiplus::Ok && b->GetWidth() > 0 && b->GetHeight() > 0) g_bgSrc = b;
    else delete b;
}

static void AddRoundRect(Gdiplus::GraphicsPath& p, const RECT& r, int rad)
{
    int x = r.left, y = r.top, w = r.right - r.left, h = r.bottom - r.top;
    int d = rad * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    if (d < 2) { p.AddRectangle(Gdiplus::Rect(x, y, w, h)); return; }
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

static Gdiplus::Color GpColor(BYTE a, COLORREF c)
{
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

// force = false: пересобрать, только если изменился размер окна
static void BuildBaseLayer(bool force = true)
{
    if (!g_hWnd || g_W <= 0 || g_H <= 0) return;
    if (!force && g_baseBmp && g_baseW == g_W && g_baseH == g_H) return;
    if (g_baseBmp) { DeleteObject(g_baseBmp); g_baseBmp = NULL; }
    g_baseW = g_W;
    g_baseH = g_H;
    HDC wdc = GetDC(g_hWnd);
    g_baseBmp = CreateCompatibleBitmap(wdc, g_W, g_H);
    ReleaseDC(g_hWnd, wdc);
    if (!g_baseBmp) return;

    HDC dc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(dc, g_baseBmp);
    Layout L; CalcLayout(L);
    RECT all = { 0, 0, g_W, g_H };
    Fill(dc, all, C_BG);

    if (!g_gdipOk) {
        Fill(dc, L.header, C_PANEL);
        Fill(dc, L.footer, C_PANEL);
        RFill(dc, L.list, L.rowH / 2, C_PANEL);
        RFill(dc, L.info, L.rowH / 2, C_PANEL);
    } else {
        Gdiplus::Graphics gr(dc);
        bool photo = (g_bgSrc != NULL);
        if (photo) {
            // Заполнить экран с сохранением пропорций, лишнее обрезать
            int iw = (int)g_bgSrc->GetWidth(), ih = (int)g_bgSrc->GetHeight();
            double sx = (double)g_W / iw, sy = (double)g_H / ih;
            double sc = sx > sy ? sx : sy;
            int dw = (int)(iw * sc + 0.5), dh = (int)(ih * sc + 0.5);
            gr.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
            gr.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            gr.DrawImage(g_bgSrc, (g_W - dw) / 2, (g_H - dh) / 2, dw, dh);
            Gdiplus::SolidBrush veil(GpColor(kVeil[g_veil], C_BG));
            gr.FillRectangle(&veil, 0, 0, g_W, g_H);
        }
        BYTE a = photo ? kThemes[g_themeIdx].panelAlpha : 255;
        Gdiplus::SolidBrush panel(GpColor(a, C_PANEL));
        Gdiplus::SolidBrush line(GpColor(a, C_PANEL2));
        gr.FillRectangle(&panel, 0, L.header.top, g_W, L.header.bottom - L.header.top - 2);
        gr.FillRectangle(&line, 0, L.header.bottom - 2, g_W, 2);
        gr.FillRectangle(&line, 0, L.footer.top, g_W, 2);
        gr.FillRectangle(&panel, 0, L.footer.top + 2, g_W, L.footer.bottom - L.footer.top - 2);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath p1, p2;
        AddRoundRect(p1, L.list, L.rowH / 4);
        AddRoundRect(p2, L.info, L.rowH / 4);
        gr.FillPath(&panel, &p1);
        gr.FillPath(&panel, &p2);
    }
    SelectObject(dc, old);
    DeleteDC(dc);
}

static void DrawHeader(HDC dc, const Layout& L)
{
    int cy = (L.header.top + L.header.bottom) / 2;

    RECT tr = { L.margin, 0, g_W / 3, L.header.bottom };
    Txt(dc, TR(L"ИГРЫ", L"GAMES"), tr, g_fTitle, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int nt = TabCount();
    wchar_t buf[4][40];
    int tw[4], total = 0, gap = g_glyphD;
    for (int t = 0; t < nt; t++) {
        _snwprintf(buf[t], 40, L"%ls  %d", kTabs[t], CountTab(t)); buf[t][39] = 0;
        tw[t] = TxtW(dc, buf[t], g_fTab);
        total += tw[t];
    }
    total += GlyphW(dc, L"LB") + GlyphW(dc, L"RB") + gap * (nt + 1);
    int x = (g_W - total) / 2;
    x += Glyph(dc, x, cy, L"LB", C_BTN_G) + gap;
    for (int t = 0; t < nt; t++) {
        RECT r = { x, L.header.top, x + tw[t], L.header.bottom };
        Txt(dc, buf[t], r, g_fTab, t == g_tab ? C_TEXT : C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (t == g_tab) {
            RECT u = { x, L.header.bottom - 6, x + tw[t], L.header.bottom - 2 };
            Fill(dc, u, C_ACCENT);
        }
        x += tw[t] + gap;
    }
    Glyph(dc, x, cy, L"RB", C_BTN_G);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t tbuf[16]; _snwprintf(tbuf, 16, L"%02d:%02d", st.wHour, st.wMinute); tbuf[15] = 0;
    int tmw = TxtW(dc, tbuf, g_fTab);
    RECT rt = { g_W - L.margin - tmw, 0, g_W - L.margin, L.header.bottom };
    Txt(dc, tbuf, rt, g_fTab, C_TEXT, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    const wchar_t* ps = g_padState == 2 ? TR(L"Геймпад", L"Gamepad") : (g_padState == 1 ? TR(L"Нет геймпада", L"No gamepad") : TR(L"Нет XInput", L"No XInput"));
    COLORREF pc = g_padState == 2 ? C_OK : C_BAD;
    int pw = TxtW(dc, ps, g_fSmall);
    int px = rt.left - g_glyphD - pw;
    RECT rp = { px, 0, px + pw, L.header.bottom };
    Txt(dc, ps, rp, g_fSmall, C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Circle(dc, px - g_glyphD / 3, cy, g_glyphD / 3, pc);
}

static void DrawList(HDC dc, const Layout& L)
{
    if (g_nf == 0) {
        RECT r = L.list;
        InflateRect(&r, -L.rowH / 2, 0);
        const wchar_t* s = g_gameCount == 0
            ? TR(L"Список игр пуст.\nУкажите папку с играми (scan=)\nв games.ini и нажмите F5.", L"The game list is empty.\nSet your games folder (scan=)\nin games.ini and press F5.")
            : TR(L"Здесь пока пусто", L"Nothing here yet");
        RECT calc = r;
        SelectObject(dc, g_fRow);
        DrawTextW(dc, s, -1, &calc, DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        int h = calc.bottom - calc.top;
        r.top = (L.list.top + L.list.bottom - h) / 2;
        r.bottom = r.top + h;
        Txt(dc, s, r, g_fRow, C_DIM, DT_CENTER | DT_WORDBREAK);
        return;
    }
    bool scroll = g_nf > L.visRows;
    int sbW = scroll ? L.pad : 0;
    bool active = (g_mode == MODE_LIST);

    for (int i = 0; i < L.visRows && g_top + i < g_nf; i++) {
        const Game& g = g_games[g_filtered[g_top + i]];
        RECT row = { L.list.left + L.pad / 2, L.list.top + L.pad / 2 + i * L.rowH,
                     L.list.right - L.pad / 2 - sbW, L.list.top + L.pad / 2 + (i + 1) * L.rowH };
        bool sel = (g_top + i == g_sel);
        if (sel) RFill(dc, row, L.rowH / 2, active ? C_SEL : C_SEL_DIM);

        int cy = (row.top + row.bottom) / 2;
        if (g.fav) Circle(dc, row.left + L.rowH / 3, cy, L.rowH / 5, C_ACCENT);

        RECT tr = row;
        tr.left  = row.left + L.rowH * 2 / 3;
        tr.right = row.right - L.rowH / 3;
        if (g.year > 0) {
            wchar_t yr[8]; _snwprintf(yr, 8, L"%d", g.year); yr[7] = 0;
            int yw = TxtW(dc, yr, g_fSmall);
            RECT yrR = tr; yrR.left = tr.right - yw;
            Txt(dc, yr, yrR, g_fSmall, sel ? C_TEXT : C_DIM, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            tr.right = yrR.left - L.rowH / 3;
        }
        Txt(dc, g.name, tr, g_fRow, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (scroll) {
        RECT track = { L.list.right - L.pad, L.list.top + L.pad, L.list.right - L.pad / 2, L.list.bottom - L.pad };
        Fill(dc, track, C_PANEL2);
        int th = track.bottom - track.top;
        int thumbH = th * L.visRows / g_nf;
        if (thumbH < L.pad) thumbH = L.pad;
        int maxTop = g_nf - L.visRows;
        int ty = track.top + (maxTop > 0 ? (th - thumbH) * g_top / maxTop : 0);
        RECT thumb = { track.left, ty, track.right, ty + thumbH };
        Fill(dc, thumb, C_DIM);
    }
}

static void DrawPlaceholder(HDC dc, const RECT& r, const Game& g)
{
    unsigned h = 0;
    for (const wchar_t* p = g.name; *p; p++) h = h * 31 + *p;
    COLORREF c = kCoverColors[h % (sizeof(kCoverColors) / sizeof(kCoverColors[0]))];
    COLORREF lite = RGB(GetRValue(c) + (255 - GetRValue(c)) / 3,
                        GetGValue(c) + (255 - GetGValue(c)) / 3,
                        GetBValue(c) + (255 - GetBValue(c)) / 3);
    Fill(dc, r, lite);
    RECT in = r; InflateRect(&in, -3, -3);
    Fill(dc, in, c);

    RECT band = in; band.top = in.bottom - (in.bottom - in.top) * 18 / 100;
    Fill(dc, band, RGB(GetRValue(c) * 2 / 3, GetGValue(c) * 2 / 3, GetBValue(c) * 2 / 3));
    Txt(dc, TR(L"нет обложки", L"no cover"), band, g_fSmall, C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    wchar_t ini[4]; Initials(g.name, ini);
    RECT top = in; top.bottom = band.top;
    Txt(dc, ini, top, g_fCover, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void DrawCover(HDC dc, const RECT& r, int gi)
{
    int bw = r.right - r.left, bh = r.bottom - r.top;
    CoverEntry* e = CoverFind(gi, bw, bh);
    if (!e) {
        // Грузим, когда выбор "успокоится" — см. OnTimer
        g_covPendGame = gi; g_covPendW = bw; g_covPendH = bh;
        RECT in = r; InflateRect(&in, -bw / 8, -bh / 8);
        RFill(dc, in, g_glyphD, C_PANEL2);
        return;
    }
    e->used = GetTickCount();
    if (!e->bmp) { DrawPlaceholder(dc, r, g_games[gi]); return; }

    int x = r.left + (bw - e->w) / 2;
    int y = r.top + (bh - e->h) / 2;
    HDC sdc = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(sdc, e->bmp);
    BitBlt(dc, x, y, e->w, e->h, sdc, 0, 0, SRCCOPY);
    SelectObject(sdc, old);
    DeleteDC(sdc);
}

static void DrawInfo(HDC dc, const Layout& L)
{
    if (g_nf == 0) return;
    int gi = g_filtered[g_sel];
    const Game& g = g_games[gi];

    RECT in = L.info; InflateRect(&in, -L.rowH / 2, -L.rowH / 2);
    int coverH = (in.bottom - in.top) * 46 / 100;
    int coverW = coverH * 3 / 4;
    RECT cover = { in.left + ((in.right - in.left) - coverW) / 2, in.top, 0, in.top + coverH };
    cover.right = cover.left + coverW;
    DrawCover(dc, cover, gi);

    int y = cover.bottom + L.rowH / 3;
    RECT nr = { in.left, y, in.right, y + g_pxBig * 3 / 2 };
    Txt(dc, g.name, nr, g_fBig, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    y = nr.bottom + L.rowH / 4;

    wchar_t year[8], last[32], played[32];
    if (g.year > 0) { _snwprintf(year, 8, L"%d", g.year); year[7] = 0; }
    else CopyStr(year, 8, L"\x2014");
    LastPlayedText(g.last, last, 32);
    PlayTimeText(g.playSec, g.count, played, 32);
    const wchar_t* labels[] = { TR(L"Жанр", L"Genre"), TR(L"Год", L"Year"), TR(L"Профиль", L"Profile"), TR(L"Ввод", L"Input"), TR(L"Мышь со стика", L"Stick as mouse"),
                                TR(L"Последний запуск", L"Last played"), TR(L"Наиграно", L"Play time") };
    const wchar_t* values[] = { g.genre[0] ? g.genre : L"\x2014", year, ProfileName(g.profile),
                                kInput[g.input], kMouse[g.mouse], last, played };

    int lineH = g_pxSmall * 17 / 10;
    int split = in.left + (in.right - in.left) * 42 / 100;
    for (int i = 0; i < 7 && y + lineH <= in.bottom; i++) {
        RECT lr = { in.left, y, split - L.rowH / 4, y + lineH };
        RECT vr = { split, y, in.right, y + lineH };
        Txt(dc, labels[i], lr, g_fSmall, C_DIM, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        Txt(dc, values[i], vr, g_fSmall, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += lineH;
    }
}

static void DrawFooter(HDC dc, const Layout& L)
{
    int cy = (L.footer.top + L.footer.bottom) / 2;
    int x = L.margin;

    switch (g_mode) {
    case MODE_LIST:
        x = Hint(dc, x, cy, L"A", C_BTN_A, TR(L"Запуск", L"Play"));
        x = Hint(dc, x, cy, L"Y", C_BTN_Y, TR(L"Настройки", L"Settings"));
        x = Hint(dc, x, cy, L"X", C_BTN_X, TR(L"Избранное", L"Favorite"));
        x += Glyph(dc, x, cy, L"LB", C_BTN_G) + g_glyphD / 4;
        x = Hint(dc, x, cy, L"RB", C_BTN_G, TR(L"Раздел", L"Section"));
        x += Glyph(dc, x, cy, L"LT", C_BTN_G) + g_glyphD / 4;
        x = Hint(dc, x, cy, L"RT", C_BTN_G, TR(L"Буква", L"Letter"));
        x = Hint(dc, x, cy, L"Start", C_BTN_G, TR(L"Меню", L"Menu"));
        break;
    case MODE_EDITOR:
        x = Hint(dc, x, cy, L"A", C_BTN_A, TR(L"Назначить", L"Assign"));
        x = Hint(dc, x, cy, L"Y", C_BTN_Y, TR(L"Очистить", L"Clear"));
        if (g_edProf >= 0 && g_prof[g_edProf].layerBtn >= 0) {
            x += Glyph(dc, x, cy, L"LB", C_BTN_G) + g_glyphD / 4;
            x = Hint(dc, x, cy, L"RB", C_BTN_G, TR(L"Слой", L"Layer"));
        }
        x = Hint(dc, x, cy, L"B", C_BTN_B, TR(L"Готово", L"Done"));
        break;
    case MODE_KEYPICK:
        x = Hint(dc, x, cy, L"A", C_BTN_A, TR(L"Выбрать", L"Select"));
        x = Hint(dc, x, cy, L"Y", C_BTN_Y, TR(L"Очистить", L"Clear"));
        x = Hint(dc, x, cy, L"B", C_BTN_B, TR(L"Отмена", L"Cancel"));
        break;
    case MODE_MENU:
        x = Hint(dc, x, cy, L"A", C_BTN_A, TR(L"Выбрать", L"Select"));
        x = Hint(dc, x, cy, L"B", C_BTN_B, TR(L"Закрыть", L"Close"));
        break;
    case MODE_SETTINGS:
        x = Hint(dc, x, cy, L"A", C_BTN_A, TR(L"Выбрать", L"Select"));
        x = Hint(dc, x, cy, L"B", C_BTN_B, TR(L"Назад", L"Back"));
        x = Hint(dc, x, cy, L"\x2039 \x203A", C_BTN_G, TR(L"Изменить значение", L"Change value"));
        break;
    case MODE_LAUNCH:
    case MODE_RUNNING:
        x += Glyph(dc, x, cy, L"Back", C_BTN_G) + g_glyphD / 4;
        x = Hint(dc, x, cy, L"Start", C_BTN_G, TR(L"Удерживать \x2014 закрыть игру", L"Hold \x2014 close game"));
        break;
    case MODE_MESSAGE:
        x = Hint(dc, x, cy, L"A", C_BTN_A, L"OK");
        break;
    }

    wchar_t st[96];
    _snwprintf(st, 96, TR(L"%ls  \x00B7  игр: %d", L"%ls  \x00B7  games: %d"), g_XGetState ? g_xiName : TR(L"XInput не найден", L"XInput not found"), g_gameCount);
    st[95] = 0;
    RECT r = { x, L.footer.top, g_W - L.margin, L.footer.bottom };
    Txt(dc, st, r, g_fSmall, C_DIM, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static RECT DrawDialog(HDC dc, int wPct, int hPct)
{
    int w = g_W * wPct / 100, h = g_H * hPct / 100;
    RECT o = { (g_W - w) / 2, (g_H - h) / 2, (g_W - w) / 2 + w, (g_H - h) / 2 + h };
    int rad = g_H / 30;
    RFill(dc, o, rad, C_SEL);
    RECT in = o; InflateRect(&in, -3, -3);
    RFill(dc, in, rad, C_PANEL2);
    return in;
}

// Путь exe для показа: относительно папки игры, если возможно
static const wchar_t* ExeShort(const Game& g)
{
    size_t n = wcslen(g.root);
    if (n && !_wcsnicmp(g.exe, g.root, n) && g.exe[n] == L'\\') return g.exe + n + 1;
    const wchar_t* p = wcsrchr(g.exe, L'\\');
    return p ? p + 1 : g.exe;
}

// Строка "подпись — значение" в диалогах
static void DrawRow(HDC dc, RECT row, bool sel, const wchar_t* label, const wchar_t* val,
                    bool cyc, COLORREF vc, int valPct)
{
    int h = row.bottom - row.top;
    if (sel) RFill(dc, row, h / 2, C_SEL);
    RECT lr = row; lr.left += h / 2; lr.right -= h / 2;
    Txt(dc, label, lr, g_fRow, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    wchar_t v[MAX_PATH + 32];
    if (cyc && sel) _snwprintf(v, MAX_PATH + 32, L"\x2039  %ls  \x203A", val);
    else            _snwprintf(v, MAX_PATH + 32, L"%ls", val);
    v[MAX_PATH + 31] = 0;
    RECT vr = lr; vr.left = lr.left + (lr.right - lr.left) * valPct / 100;
    Txt(dc, v, vr, g_fSmall, sel ? C_TEXT : vc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
}

static void DrawActionRow(HDC dc, RECT row, bool sel, const wchar_t* label, COLORREF c)
{
    int h = row.bottom - row.top;
    if (sel) RFill(dc, row, h / 2, C_SEL);
    Txt(dc, label, row, g_fRow, sel ? C_TEXT : c, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static int DialogTitle(HDC dc, RECT in, const wchar_t* title, const wchar_t* sub)
{
    RECT t = { in.left, in.top, in.right, in.top + g_pxBig * 3 / 2 };
    Txt(dc, title, t, g_fBig, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (!sub) return t.bottom;
    RECT s = { in.left, t.bottom, in.right, t.bottom + g_pxSmall * 2 };
    Txt(dc, sub, s, g_fSmall, C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    return s.bottom;
}

static void DrawSettings(HDC dc, const Layout& L)
{
    const Game& g = Cur();
    RECT in = DrawDialog(dc, 66, 84);
    int pad = L.rowH / 2;
    InflateRect(&in, -pad, -pad);
    int y = DialogTitle(dc, in, TR(L"Настройки игры", L"Game settings"), g.name) + pad / 2;

    wchar_t wd[MAX_PATH];
    if (g.dir[0]) COPY(wd, g.dir); else WorkDir(g.exe, wd, MAX_PATH);
    wchar_t exe[MAX_PATH + 16];
    if (g_candCount > 1) _snwprintf(exe, MAX_PATH + 16, L"%ls  (%d/%d)", ExeShort(g), g_candIdx + 1, g_candCount);
    else                 _snwprintf(exe, MAX_PATH + 16, L"%ls", ExeShort(g));
    exe[MAX_PATH + 15] = 0;

    int rowH = L.rowH * 11 / 10;
    if (rowH * kSetCount > in.bottom - y) rowH = (in.bottom - y) / kSetCount;
    for (int i = 0; i < kSetCount; i++) {
        RECT row = { in.left, y, in.right, y + rowH };
        bool sel = (i == g_setSel);
        switch (i) {
        case SET_PROFILE: DrawRow(dc, row, sel, TR(L"Профиль маппинга", L"Mapping profile"), ProfileName(g.profile), true, C_TEXT, 36); break;
        case SET_INPUT:   DrawRow(dc, row, sel, TR(L"Режим ввода", L"Input mode"), kInput[g.input], true, C_TEXT, 36); break;
        case SET_MOUSE:   DrawRow(dc, row, sel, TR(L"Мышь со стика", L"Stick as mouse"), kMouse[g.mouse], true, C_TEXT, 36); break;
        case SET_LAYOUT:  DrawRow(dc, row, sel, TR(L"Раскладка кнопок", L"Button layout"), TR(L"Изменить \x203A", L"Edit \x203A"), false, C_ACCENT, 36); break;
        case SET_EXE:     DrawRow(dc, row, sel, TR(L"Исполняемый файл", L"Executable"), exe, g_candCount > 1, g_candCount > 1 ? C_TEXT : C_DIM, 36); break;
        case SET_ARGS:    DrawRow(dc, row, sel, TR(L"Аргументы", L"Arguments"), g.args[0] ? g.args : L"\x2014", false, C_DIM, 36); break;
        case SET_DIR:     DrawRow(dc, row, sel, TR(L"Рабочая папка", L"Working folder"), wd, false, C_DIM, 36); break;
        case SET_HIDE:    DrawActionRow(dc, row, sel, g.hidden ? TR(L"Вернуть в список", L"Show in list") : TR(L"Скрыть игру", L"Hide game"), C_DIM); break;
        case SET_LAUNCH:  DrawActionRow(dc, row, sel, TR(L"Запустить игру", L"Launch game"), C_ACCENT); break;
        }
        y += rowH;
    }
}

static void DrawEditor(HDC dc, const Layout& L)
{
    if (g_edProf < 0 || g_edGame < 0) return;
    const Profile& p = g_prof[g_edProf];
    const Game& g = g_games[g_edGame];
    RECT in = DrawDialog(dc, 80, 88);
    int pad = L.rowH / 2;
    InflateRect(&in, -pad, -pad);

    wchar_t sub[256];
    _snwprintf(sub, 256, L"%ls  \x00B7  %ls", g.name,
               p.own ? ProfileName(p.id) : TR(L"общий профиль \x2014 при изменении создастся своя копия для этой игры", L"shared profile \x2014 editing creates a custom copy for this game"));
    sub[255] = 0;
    int y = DialogTitle(dc, in, TR(L"Раскладка кнопок", L"Button layout"), sub) + pad / 2;

    // Вкладки слоёв
    if (p.layerBtn >= 0) {
        wchar_t lt[64];
        _snwprintf(lt, 64, TR(L"Слой %ls", L"Layer %ls"), kPadNames[p.layerBtn]); lt[63] = 0;
        int th = g_pxBig * 3 / 2;
        int w2 = TxtW(dc, lt, g_fTab), w1 = TxtW(dc, TR(L"Основной", L"Base"), g_fTab);
        int x2 = in.right - w2, x1 = x2 - g_glyphD - w1;
        RECT r1 = { x1, in.top, x1 + w1, in.top + th }, r2 = { x2, in.top, in.right, in.top + th };
        Txt(dc, TR(L"Основной", L"Base"), r1, g_fTab, g_edLayer ? C_DIM : C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Txt(dc, lt, r2, g_fTab, g_edLayer ? C_TEXT : C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT u = g_edLayer ? r2 : r1;
        u.top = u.bottom - 4; u.bottom -= 1;
        Fill(dc, u, C_ACCENT);
    }

    int rowH = L.rowH;
    int vis = (in.bottom - y) / rowH;
    if (vis < 1) vis = 1;
    if (g_edSel < g_edTop) g_edTop = g_edSel;
    if (g_edSel >= g_edTop + vis) g_edTop = g_edSel - vis + 1;

    for (int k = 0; k < vis && g_edTop + k < g_edCount; k++) {
        int i = g_edTop + k;
        const EdRow& r = g_edRows[i];
        RECT row = { in.left, y, in.right, y + rowH };
        bool sel = (i == g_edSel);
        wchar_t val[128];
        val[0] = 0;
        switch (r.kind) {
        case ER_LSTICK:
            DrawRow(dc, row, sel, TR(L"Левый стик", L"Left stick"), kStickModes[StickModeIdx(p, true)], true, C_TEXT, 45);
            break;
        case ER_RSTICK:
            DrawRow(dc, row, sel, TR(L"Правый стик", L"Right stick"), kStickModes[StickModeIdx(p, false)], true, C_TEXT, 45);
            break;
        case ER_SPEED:
            _snwprintf(val, 128, L"%d", p.mouseSpeed); val[127] = 0;
            DrawRow(dc, row, sel, TR(L"Скорость мыши", L"Mouse speed"), val, true, C_TEXT, 45);
            break;
        case ER_CURVE:
            _snwprintf(val, 128, L"%.2f", (double)p.mouseCurve); val[127] = 0;
            DrawRow(dc, row, sel, TR(L"Точность мыши (кривая)", L"Mouse precision (curve)"), val, true, C_TEXT, 45);
            break;
        case ER_LAYERBTN:
            DrawRow(dc, row, sel, TR(L"Кнопка слоя", L"Layer button"), p.layerBtn >= 0 ? kPadNames[p.layerBtn] : TR(L"нет", L"none"), true, C_TEXT, 45);
            break;
        case ER_BIND: {
            const Binding& b = g_edLayer ? p.layer[r.pad] : p.base[r.pad];
            COLORREF vc = C_TEXT;
            if (b.n) BindingText(b, val, 128, true);
            else if (g_edLayer && p.base[r.pad].n) {
                wchar_t bt[96];
                BindingText(p.base[r.pad], bt, 96, true);
                _snwprintf(val, 128, TR(L"как в основном: %ls", L"as in base: %ls"), bt); val[127] = 0;
                vc = C_DIM;
            } else {
                COPY(val, L"\x2014");
                vc = C_DIM;
            }
            DrawRow(dc, row, sel, kPadLabels[r.pad], val, false, vc, 45);
            break;
        }
        }
        y += rowH;
    }
}

static void DrawPicker(HDC dc, const Layout& L)
{
    RECT in = DrawDialog(dc, 92, 90);
    int pad = L.rowH / 2;
    InflateRect(&in, -pad, -pad);
    wchar_t title[128];
    _snwprintf(title, 128, TR(L"Клавиша: %ls%ls", L"Key: %ls%ls"), kPadLabels[g_kpPad], g_kpLayer ? TR(L" (слой)", L" (layer)") : L"");
    title[127] = 0;
    int y = DialogTitle(dc, in, title,
                        TR(L"Выберите клавишу или нажмите её на клавиатуре. Ctrl+ / Shift+ / Alt+ добавляют модификатор.", L"Pick a key or press it on the keyboard. Ctrl+ / Shift+ / Alt+ add a modifier."))
            + pad / 2;

    int rows = (g_kpN + kPickCols - 1) / kPickCols;
    int cellW = (in.right - in.left) / kPickCols;
    int cellH = L.rowH;
    int vis = (in.bottom - y) / cellH;
    if (vis < 1) vis = 1;
    int selRow = g_kpSel / kPickCols;
    if (selRow < g_kpTop) g_kpTop = selRow;
    if (selRow >= g_kpTop + vis) g_kpTop = selRow - vis + 1;

    for (int r = g_kpTop; r < rows && r < g_kpTop + vis; r++) {
        for (int c = 0; c < kPickCols; c++) {
            int i = r * kPickCols + c;
            if (i >= g_kpN) break;
            int top = y + (r - g_kpTop) * cellH;
            RECT cell = { in.left + c * cellW + 2, top + 2, in.left + (c + 1) * cellW - 2, top + cellH - 2 };
            int code = g_kpCodes[i];
            bool sel = (i == g_kpSel);
            bool on = code <= -10 && (g_kpMods & (1 << (-code - 10)));
            RFill(dc, cell, cellH / 3, sel ? C_SEL : (on ? C_ACCENT : C_PANEL));
            const wchar_t* lab = code == -10 ? L"Ctrl +" : code == -11 ? L"Shift +" : code == -12 ? L"Alt +"
                               : (code >= 0 ? KeyLabel(code) : L"?");
            Txt(dc, lab, cell, g_fSmall, (on && !sel) ? RGB(30, 30, 30) : C_TEXT,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
}

static void DrawMenu(HDC dc, const Layout& L)
{
    RECT in = DrawDialog(dc, 58, 86);
    int pad = L.rowH / 2;
    InflateRect(&in, -pad, -pad);
    int y = DialogTitle(dc, in, TR(L"Меню", L"Menu"), NULL) + pad / 2;
    int rowH = L.rowH * 11 / 10;
    if (rowH * MI_COUNT > in.bottom - y) rowH = (in.bottom - y) / MI_COUNT;

    for (int i = 0; i < MI_COUNT; i++) {
        RECT row = { in.left, y, in.right, y + rowH };
        bool sel = (i == g_menuSel);
        bool armed = (i == g_menuArmed);
        switch (i) {
        case MI_SORT:    DrawRow(dc, row, sel, TR(L"Сортировка", L"Sort"), kSortNames[SortMode()], true, C_TEXT, 40); break;
        case MI_THEME:   DrawRow(dc, row, sel, TR(L"Тема", L"Theme"), TR(kThemes[g_themeIdx].nameRu, kThemes[g_themeIdx].nameEn), true, C_TEXT, 40); break;
        case MI_BG:      DrawRow(dc, row, sel, TR(L"Фон", L"Background"), g_bgName[0] ? g_bgName : TR(L"Без фона", L"None"), true, C_TEXT, 30); break;
        case MI_LANG:    DrawRow(dc, row, sel, L"Язык / Language", g_lang ? L"English" : L"Русский", true, C_TEXT, 50); break;
        case MI_VEIL:    DrawRow(dc, row, sel, TR(L"Затемнение фона", L"Background dimming"), kVeilNames[g_veil], true,
                                 g_bgName[0] ? C_TEXT : C_DIM, 50); break;
        case MI_SOUND:   DrawRow(dc, row, sel, TR(L"Звуки", L"Sounds"), g_sound ? TR(L"Вкл", L"On") : TR(L"Выкл", L"Off"), true, C_TEXT, 40); break;
        case MI_AUTORUN: DrawRow(dc, row, sel, TR(L"Запуск вместе с Windows", L"Start with Windows"), g_autorun ? TR(L"Вкл", L"On") : TR(L"Выкл", L"Off"), true, C_TEXT, 60); break;
        case MI_RESCAN:  DrawActionRow(dc, row, sel, TR(L"Пересканировать игры", L"Rescan games"), C_TEXT); break;
        case MI_EXIT:
            DrawActionRow(dc, row, sel, armed ? TR(L"Выйти? Нажмите A ещё раз", L"Exit? Press A again") : TR(L"Выход в Windows", L"Exit to Windows"), armed ? C_ACCENT : C_DIM);
            break;
        case MI_REBOOT:
            DrawActionRow(dc, row, sel, armed ? TR(L"Перезагрузить? Нажмите A ещё раз", L"Restart? Press A again") : TR(L"Перезагрузка", L"Restart"), armed ? C_ACCENT : C_DIM);
            break;
        case MI_POWEROFF:
            DrawActionRow(dc, row, sel, armed ? TR(L"Выключить? Нажмите A ещё раз", L"Shut down? Press A again") : TR(L"Выключение", L"Shut down"), armed ? C_ACCENT : C_DIM);
            break;
        }
        y += rowH;
    }
}

static void DrawLetter(HDC dc)
{
    if (!g_letter) return;
    int s = g_H / 4;
    RECT o = { (g_W - s) / 2, (g_H - s) / 2, (g_W + s) / 2, (g_H + s) / 2 };
    RFill(dc, o, s / 5, C_SEL);
    RECT in = o; InflateRect(&in, -3, -3);
    RFill(dc, in, s / 5, C_PANEL2);
    wchar_t t[2] = { g_letter, 0 };
    Txt(dc, t, in, g_fCover, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void DrawLaunch(HDC dc, const Layout& L)
{
    if (g_launchGame < 0 || g_launchGame >= g_gameCount) return;
    const Game& g = g_games[g_launchGame];
    RECT in = DrawDialog(dc, 58, 40);
    int pad = L.rowH / 2;
    InflateRect(&in, -pad, -pad);

    RECT t = { in.left, in.top, in.right, in.top + g_pxBig * 3 / 2 };
    Txt(dc, g.name, t, g_fBig, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    int y = t.bottom + pad;
    RECT r = { in.left, y, in.right, y + g_pxBig * 3 / 2 };
    if (g_mode == MODE_LAUNCH) {
        const wchar_t* dots[] = { TR(L"Запуск", L"Starting"), TR(L"Запуск.", L"Starting."),
                                  TR(L"Запуск..", L"Starting.."), TR(L"Запуск...", L"Starting...") };
        DWORD el = GetTickCount() - g_launchTick;
        Txt(dc, dots[(el / 150) % 4], r, g_fRow, C_ACCENT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        Txt(dc, g_closeStage ? TR(L"Закрываем игру...", L"Closing the game...") : TR(L"Игра запущена", L"Game is running"), r, g_fRow, C_ACCENT,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT h = { in.left, r.bottom + pad / 2, in.right, r.bottom + pad / 2 + g_pxSmall * 2 };
        wchar_t mp[128];
        if (g_mapActive) _snwprintf(mp, 128, TR(L"Маппинг: %ls", L"Mapping: %ls"), ProfileName(g.profile));
        else             _snwprintf(mp, 128, TR(L"Маппинг выключен", L"Mapping is off"));
        mp[127] = 0;
        Txt(dc, mp, h, g_fSmall, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        OffsetRect(&h, 0, g_pxSmall * 2);
        Txt(dc, TR(L"Удерживайте Back + Start, чтобы закрыть игру", L"Hold Back + Start to close the game"), h, g_fSmall, C_DIM,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

static void DrawMessage(HDC dc, const Layout& L)
{
    RECT in = DrawDialog(dc, 60, 40);
    int pad = L.rowH / 2;
    InflateRect(&in, -pad, -pad);
    RECT t = { in.left, in.top, in.right, in.top + g_pxBig * 3 / 2 };
    Txt(dc, g_msgTitle, t, g_fBig, C_ACCENT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT b = { in.left, t.bottom + pad / 2, in.right, in.bottom };
    Txt(dc, g_msgText, b, g_fSmall, C_TEXT, DT_CENTER | DT_WORDBREAK | DT_EDITCONTROL);
}

static void Paint(HDC dc)
{
    Layout L; CalcLayout(L);
    SetBkMode(dc, TRANSPARENT);
    RECT all = { 0, 0, g_W, g_H };
    if (g_baseBmp) {
        HDC bdc = CreateCompatibleDC(dc);
        HGDIOBJ ob = SelectObject(bdc, g_baseBmp);
        BitBlt(dc, 0, 0, g_W, g_H, bdc, 0, 0, SRCCOPY);
        SelectObject(bdc, ob);
        DeleteDC(bdc);
    } else {
        Fill(dc, all, C_BG);
    }
    DrawHeader(dc, L);
    DrawList(dc, L);
    DrawInfo(dc, L);
    DrawFooter(dc, L);
    if (g_mode == MODE_SETTINGS && g_nf > 0)               DrawSettings(dc, L);
    if (g_mode == MODE_LAUNCH || g_mode == MODE_RUNNING)   DrawLaunch(dc, L);
    if (g_mode == MODE_EDITOR)                             DrawEditor(dc, L);
    if (g_mode == MODE_KEYPICK)                            DrawPicker(dc, L);
    if (g_mode == MODE_MENU)                               DrawMenu(dc, L);
    if (g_mode == MODE_MESSAGE)                            DrawMessage(dc, L);
    if (g_mode == MODE_LIST)                               DrawLetter(dc);
}

// ---------------------------------------------------------------------------
// Окно: полный экран, фокус
// ---------------------------------------------------------------------------
static void Redraw() { InvalidateRect(g_hWnd, NULL, FALSE); }

static void SetTimerMs(UINT ms)
{
    if (ms != g_timerMs) { SetTimer(g_hWnd, 1, ms, NULL); g_timerMs = ms; }
}

static void ApplyFullscreenGeometry()
{
    MONITORINFO mi; mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowPos(g_hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED);
}

static void SetFullscreen(bool fs)
{
    if (fs == g_fullscreen) return;
    g_fullscreen = fs;
    if (fs) {
        GetWindowRect(g_hWnd, &g_winRect);
        SetWindowLongW(g_hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        ApplyFullscreenGeometry();
    } else {
        SetWindowLongW(g_hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(g_hWnd, HWND_NOTOPMOST, g_winRect.left, g_winRect.top,
                     g_winRect.right - g_winRect.left, g_winRect.bottom - g_winRect.top,
                     SWP_FRAMECHANGED);
    }
}

// На XP SetForegroundWindow часто молча отказывает — подцепляемся к потоку
// текущего активного окна, а если не помогло — эмулируем нажатие Alt.
static void ForceForeground()
{
    HWND fg = GetForegroundWindow();
    if (fg == g_hWnd) return;
    DWORD me = GetCurrentThreadId();
    DWORD fgT = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    BOOL att = FALSE;
    if (fgT && fgT != me) att = AttachThreadInput(me, fgT, TRUE);
    BringWindowToTop(g_hWnd);
    SetForegroundWindow(g_hWnd);
    SetFocus(g_hWnd);
    if (att) AttachThreadInput(me, fgT, FALSE);
    if (GetForegroundWindow() != g_hWnd) {
        keybd_event(VK_MENU, 0, KEYEVENTF_EXTENDEDKEY, 0);
        SetForegroundWindow(g_hWnd);
        keybd_event(VK_MENU, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
    }
}

// ---------------------------------------------------------------------------
// Пад
// ---------------------------------------------------------------------------
static void LoadXInput()
{
    const wchar_t* names[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    for (int i = 0; i < 3; i++) {
        HMODULE h = LoadLibraryW(names[i]);
        if (!h) continue;
        g_XGetState = (PFN_XInputGetState)GetProcAddress(h, "XInputGetState");
        if (g_XGetState) {
            g_XSetState = (PFN_XInputSetState)GetProcAddress(h, "XInputSetState");
            COPY(g_xiName, names[i]);
            return;
        }
        FreeLibrary(h);
    }
}

static void SetPadState(int s)
{
    if (s != g_padState) { g_padState = s; Redraw(); }
}

static void Rumble(WORD power, DWORD ms)
{
    if (!g_XSetState || g_padIdx < 0) return;
    XI_VIBRATION v; v.wLeftMotorSpeed = power; v.wRightMotorSpeed = power;
    g_XSetState((DWORD)g_padIdx, &v);
    g_rumbleUntil = GetTickCount() + ms;
    if (!g_rumbleUntil) g_rumbleUntil = 1;
}

static void StopRumble()
{
    if (g_XSetState && g_padIdx >= 0) {
        XI_VIBRATION v; v.wLeftMotorSpeed = 0; v.wRightMotorSpeed = 0;
        g_XSetState((DWORD)g_padIdx, &v);
    }
    g_rumbleUntil = 0;
}

// Пока пад не найден — ищем раз в секунду: опрос пустого слота на P3 дорогой.
static DWORD PollPad(DWORD now)
{
    if (!g_XGetState) { SetPadState(0); return 0; }
    XI_STATE st;

    if (g_padIdx < 0) {
        if (g_lastScan != 0 && now - g_lastScan < 1000) return 0;
        g_lastScan = now;
        for (DWORD i = 0; i < 4; i++) {
            ZeroMemory(&st, sizeof(st));
            if (g_XGetState(i, &st) == ERROR_SUCCESS) { g_padIdx = (int)i; break; }
        }
        if (g_padIdx < 0) { SetPadState(1); return 0; }
    }

    ZeroMemory(&st, sizeof(st));
    if (g_XGetState((DWORD)g_padIdx, &st) != ERROR_SUCCESS) {
        g_padIdx = -1; g_lastScan = now;
        SetPadState(1);
        return 0;
    }
    SetPadState(2);

    WORD b = st.Gamepad.wButtons;
    DWORD m = 0;
    if ((b & XI_DPAD_UP)    || st.Gamepad.sThumbLY >  STICK_THRESHOLD) m |= VB_UP;
    if ((b & XI_DPAD_DOWN)  || st.Gamepad.sThumbLY < -STICK_THRESHOLD) m |= VB_DOWN;
    if ((b & XI_DPAD_LEFT)  || st.Gamepad.sThumbLX < -STICK_THRESHOLD) m |= VB_LEFT;
    if ((b & XI_DPAD_RIGHT) || st.Gamepad.sThumbLX >  STICK_THRESHOLD) m |= VB_RIGHT;
    if (b & XI_A)     m |= VB_A;
    if (b & XI_B)     m |= VB_B;
    if (b & XI_X)     m |= VB_X;
    if (b & XI_Y)     m |= VB_Y;
    if (b & XI_LB)    m |= VB_LB;
    if (b & XI_RB)    m |= VB_RB;
    if (b & XI_START) m |= VB_START;
    if (b & XI_BACK)  m |= VB_BACK;
    if (st.Gamepad.bLeftTrigger  > 100) m |= VB_LT;
    if (st.Gamepad.bRightTrigger > 100) m |= VB_RT;
    return m;
}

// ---------------------------------------------------------------------------
// Запуск игры и слежение за ней
// ---------------------------------------------------------------------------
static void ShowMessage(const wchar_t* title, const wchar_t* text)
{
    COPY(g_msgTitle, title);
    COPY(g_msgText, text);
    g_mode = MODE_MESSAGE;
    Redraw();
}

static void SysErrorText(DWORD err, wchar_t* buf, int cap)
{
    buf[0] = 0;
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, buf, cap, NULL);
    Trim(buf);
    size_t n = wcslen(buf);
    while (n > 0 && (buf[n - 1] == L'\n' || buf[n - 1] == L'\r' || buf[n - 1] == L'.')) buf[--n] = 0;
}

static int GetGamePids(DWORD* out, int cap)
{
    if (g_job) {
        struct { JOBOBJECT_BASIC_PROCESS_ID_LIST h; ULONG_PTR extra[63]; } lst;
        ZeroMemory(&lst, sizeof(lst));
        if (QueryInformationJobObject(g_job, JobObjectBasicProcessIdList, &lst, sizeof(lst), NULL)
            || GetLastError() == ERROR_MORE_DATA) {
            int n = (int)lst.h.NumberOfProcessIdsInList;
            if (n > cap) n = cap;
            if (n > 64) n = 64;
            for (int i = 0; i < n; i++) out[i] = (DWORD)lst.h.ProcessIdList[i];
            if (n > 0) return n;
        }
    }
    if (g_gamePid && cap > 0) { out[0] = g_gamePid; return 1; }
    return 0;
}

struct EnumCtx {
    DWORD pids[64];
    int   n;
    int   found;
    bool  close;
};

static BOOL CALLBACK EnumGameWnd(HWND h, LPARAM lp)
{
    EnumCtx* c = (EnumCtx*)lp;
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    for (int i = 0; i < c->n; i++) {
        if (c->pids[i] == pid) {
            c->found++;
            if (c->close) PostMessageW(h, WM_CLOSE, 0, 0);
            break;
        }
    }
    return TRUE;
}

static int GameWindows(bool close)
{
    EnumCtx c;
    ZeroMemory(&c, sizeof(c));
    c.n = GetGamePids(c.pids, 64);
    c.close = close;
    if (c.n > 0) EnumWindows(EnumGameWnd, (LPARAM)&c);
    return c.found;
}

static void StartLaunch()
{
    if (g_nf == 0) return;
    g_launchGame = g_filtered[g_sel];
    g_mode = MODE_LAUNCH;
    g_launchTick = GetTickCount();
    g_launchPhase = -1;
}

static void DoLaunch()
{
    Game& g = g_games[g_launchGame];
    wchar_t text[1024];

    if (!FileExists(g.exe)) {
        _snwprintf(text, 1024, TR(L"Файл не найден:\n%ls", L"File not found:\n%ls"), g.exe); text[1023] = 0;
        ShowMessage(TR(L"Не удалось запустить", L"Could not launch"), text);
        return;
    }

    wchar_t dir[MAX_PATH];
    if (g.dir[0]) COPY(dir, g.dir); else WorkDir(g.exe, dir, MAX_PATH);

    static wchar_t cmd[MAX_PATH + 600];
    _snwprintf(cmd, MAX_PATH + 600, L"\"%ls\"%ls%ls", g.exe, g.args[0] ? L" " : L"", g.args);
    cmd[MAX_PATH + 599] = 0;

    ZeroMemory(&g_dmBefore, sizeof(g_dmBefore));
    g_dmBefore.dmSize = sizeof(g_dmBefore);
    g_dmValid = EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &g_dmBefore) != FALSE;

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    g_job = CreateJobObjectW(NULL, NULL);
    if (!CreateProcessW(g.exe, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, dir, &si, &pi)) {
        DWORD err = GetLastError();
        if (g_job) { CloseHandle(g_job); g_job = NULL; }
        wchar_t es[512];
        SysErrorText(err, es, 512);
        _snwprintf(text, 1024, TR(L"%ls\n(код %lu)\n\n%ls", L"%ls\n(code %lu)\n\n%ls"), es, err, g.exe); text[1023] = 0;
        ShowMessage(TR(L"Не удалось запустить", L"Could not launch"), text);
        return;
    }
    // Job отслеживает и дочерние процессы (игры со своим лончером).
    // Если не вышло (процесс уже в чужом job) — следим только за самим процессом.
    if (g_job && !AssignProcessToJobObject(g_job, pi.hProcess)) {
        CloseHandle(g_job);
        g_job = NULL;
    }
    AllowSetForegroundWindow(pi.dwProcessId);
    g_mapActive = MapStart(g, pi.dwProcessId);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    g_proc = pi.hProcess;
    g_gamePid = pi.dwProcessId;
    g_gameStart = GetTickCount();
    g_runGame = g_launchGame;
    g_hidden = false;
    g_closeStage = 0;
    g_comboHeld = g_comboFired = false;
    g_lastJobCheck = g_gameStart;

    g.count++;
    g.last = NowFT();
    SaveGameState(g);

    g_mode = MODE_RUNNING;
    SetTimerMs(50);
    Redraw();
}

static void RequestCloseGame(DWORD now)
{
    Rumble(40000, 250);
    if (g_closeStage == 0) {
        GameWindows(true);
        g_closeStage = 1;
        g_closeTick = now;
    } else if (g_closeStage == 1) {
        if (g_job) TerminateJobObject(g_job, 1);
        else if (g_proc) TerminateProcess(g_proc, 1);
        g_closeStage = 2;
    }
    Redraw();
}

static void OnGameEnded()
{
    MapStop();
    g_mapActive = false;
    DWORD el = GetTickCount() - g_gameStart;
    DWORD code = 0;
    if (g_proc) GetExitCodeProcess(g_proc, &code);
    int closeStage = g_closeStage;

    if (g_runGame >= 0 && g_runGame < g_gameCount) {
        Game& g = g_games[g_runGame];
        g.playSec += el / 1000;
        SaveGameState(g);
    }
    if (g_proc) { CloseHandle(g_proc); g_proc = NULL; }
    if (g_job)  { CloseHandle(g_job);  g_job = NULL; }
    g_gamePid = 0;
    g_runGame = -1;
    g_closeStage = 0;
    StopRumble();

    // Игра могла оставить чужое разрешение (например, после вылета)
    if (g_dmValid) {
        DEVMODEW cur;
        ZeroMemory(&cur, sizeof(cur));
        cur.dmSize = sizeof(cur);
        if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &cur) &&
            (cur.dmPelsWidth != g_dmBefore.dmPelsWidth || cur.dmPelsHeight != g_dmBefore.dmPelsHeight ||
             cur.dmBitsPerPel != g_dmBefore.dmBitsPerPel)) {
            g_dmBefore.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
            ChangeDisplaySettingsW(&g_dmBefore, 0);
        }
    }

    g_mode = MODE_LIST;
    SetTimerMs(16);
    ShowWindow(g_hWnd, SW_SHOW);
    if (g_fullscreen) ApplyFullscreenGeometry();
    g_hidden = false;
    ForceForeground();
    g_fgRetries = 20;
    Refilter(true);

    if (el < 3000 && code != 0 && closeStage == 0) {
        wchar_t text[256];
        _snwprintf(text, 256, TR(L"Процесс завершился через %lu мс с кодом 0x%08lX.\n"
                                 L"Проверьте путь, рабочую папку и аргументы.",
                                 L"The process exited after %lu ms with code 0x%08lX.\n"
                                 L"Check the path, working folder and arguments."), el, code);
        text[255] = 0;
        ShowMessage(TR(L"Игра сразу закрылась", L"The game closed right away"), text);
    }
    Redraw();
}

static void CheckGame(DWORD now)
{
    bool alive;
    if (g_job) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION ai;
        ZeroMemory(&ai, sizeof(ai));
        if (QueryInformationJobObject(g_job, JobObjectBasicAccountingInformation, &ai, sizeof(ai), NULL))
            alive = ai.ActiveProcesses > 0;
        else
            alive = WaitForSingleObject(g_proc, 0) == WAIT_TIMEOUT;
    } else {
        alive = WaitForSingleObject(g_proc, 0) == WAIT_TIMEOUT;
    }
    if (!alive) { OnGameEnded(); return; }

    // Маппингу — актуальный список процессов игры (для проверки фокуса)
    if (g_mapActive) {
        DWORD pids[64];
        int n = GetGamePids(pids, 64);
        MapSetPids(pids, n);
    }

    // Прячемся, когда у игры появилось окно (до этого лончер держит фокус,
    // чтобы окно игры гарантированно стало активным)
    if (!g_hidden && (GameWindows(false) > 0 || now - g_gameStart > 8000)) {
        ShowWindow(g_hWnd, SW_HIDE);
        g_hidden = true;
    }
    // Игра не отреагировала на WM_CLOSE — добиваем
    if (g_closeStage == 1 && now - g_closeTick > 5000) RequestCloseGame(now);
}

// ---------------------------------------------------------------------------
// Логика меню
// ---------------------------------------------------------------------------
static void OpenSettings()
{
    if (g_nf == 0) return;
    Game& g = Cur();
    g_mode = MODE_SETTINGS;
    g_setSel = 0;
    g_candCount = 0;
    g_candIdx = 0;
    if (g.root[0]) BuildCandidates(g.root, g.folder);   // для игр из scan-папок
    int idx = -1;
    for (int i = 0; i < g_candCount; i++)
        if (!_wcsicmp(g_cand[i].path, g.exe)) { idx = i; break; }
    if (idx < 0) {   // текущий exe — первым в списке
        if (g_candCount == kMaxCand) g_candCount--;
        memmove(&g_cand[1], &g_cand[0], g_candCount * sizeof(ExeCand));
        COPY(g_cand[0].path, g.exe);
        g_cand[0].score = 0;
        g_candCount++;
        idx = 0;
    }
    g_candIdx = idx;
}

static void ChangeSetting(int dir)
{
    Game& g = Cur();
    switch (g_setSel) {
    case SET_PROFILE: {
        if (g_profCount == 0) return;
        wchar_t own[32];
        OwnProfileId(g, own);
        int i = ProfileIndex(g.profile);
        // Свои раскладки других игр в перебор не попадают
        for (int step = 0; step < g_profCount; step++) {
            i = i < 0 ? 0 : (i + dir + g_profCount) % g_profCount;
            if (!g_prof[i].own || !_wcsicmp(g_prof[i].id, own)) break;
        }
        COPY(g.profile, g_prof[i].id);
        g.ovr = true;
        break;
    }
    case SET_INPUT: g.input = (g.input + dir + 3) % 3; g.ovr = true; break;
    case SET_MOUSE: g.mouse = (g.mouse + dir + 4) % 4; g.ovr = true; break;
    case SET_EXE:
        if (g_candCount < 2) return;
        g_candIdx = (g_candIdx + dir + g_candCount) % g_candCount;
        COPY(g.exe, g_cand[g_candIdx].path);
        g.dir[0] = 0;
        g.exeUser = true;
        break;
    default: return;
    }
    SaveGameState(g);
}

static void ToggleHidden()
{
    Game& g = Cur();
    g.hidden = !g.hidden;
    SaveGameState(g);
    g_mode = MODE_LIST;
    if (g_tab == 3 && CountTab(3) == 0) g_tab = 0;
    Refilter(true);
}

// --- Редактор раскладки ---
static void OpenEditor()
{
    if (g_nf == 0) return;
    g_edGame = g_filtered[g_sel];
    int idx = ProfileIndex(g_games[g_edGame].profile);
    if (idx >= 0 && g_prof[idx].passthrough) idx = -1;
    g_edProf = idx;
    if (idx < 0 && !EnsureOwnProfile()) return;
    g_edLayer = false;
    g_edSel = 0;
    g_edTop = 0;
    BuildEdRows();
    g_mode = MODE_EDITOR;
}

static void EdChange(int dir)
{
    if (g_edSel < 0 || g_edSel >= g_edCount) return;
    EdRow r = g_edRows[g_edSel];
    if (r.kind == ER_BIND) return;
    if (!EnsureOwnProfile()) return;
    Profile& p = g_prof[g_edProf];
    switch (r.kind) {
    case ER_LSTICK:
    case ER_RSTICK: {
        bool left = (r.kind == ER_LSTICK);
        int idx = StickModeIdx(p, left);
        SetStickMode(p, left, (idx + dir + 5) % 5);
        break;
    }
    case ER_SPEED:
        p.mouseSpeed += dir * 50;
        if (p.mouseSpeed < 50) p.mouseSpeed = 50;
        if (p.mouseSpeed > 5000) p.mouseSpeed = 5000;
        break;
    case ER_CURVE:
        p.mouseCurve += dir * 0.25f;
        if (p.mouseCurve < 0.5f) p.mouseCurve = 0.5f;
        if (p.mouseCurve > 4.0f) p.mouseCurve = 4.0f;
        break;
    case ER_LAYERBTN: {
        int n = LISTN(kLayerChoices), i = 0;
        while (i < n && kLayerChoices[i] != p.layerBtn) i++;
        if (i >= n) i = 0;
        p.layerBtn = kLayerChoices[(i + dir + n) % n];
        break;
    }
    }
    SaveOwnProfiles();
    BuildEdRows();
}

static void EdOk()
{
    if (g_edSel < 0 || g_edSel >= g_edCount) return;
    if (g_edRows[g_edSel].kind == ER_BIND) OpenPicker(g_edRows[g_edSel].pad);
    else EdChange(+1);
}

static void EdClear()
{
    if (g_edSel < 0 || g_edSel >= g_edCount || g_edRows[g_edSel].kind != ER_BIND) return;
    int pad = g_edRows[g_edSel].pad;
    if (!EnsureOwnProfile()) return;
    Profile& p = g_prof[g_edProf];
    (g_edLayer ? p.layer[pad] : p.base[pad]).n = 0;
    SaveOwnProfiles();
}

// --- Меню лончера ---
static void OpenMenu()
{
    g_mode = MODE_MENU;
    g_menuSel = 0;
    g_menuArmed = -1;
    g_autorun = AutorunGet();
}

static void MenuChange(int dir)
{
    switch (g_menuSel) {
    case MI_SORT:
        g_sortMode = (SortMode() + dir + 4) % 4;
        SaveLauncherState();
        BuildOrder();
        Refilter(true);
        break;
    case MI_SOUND:
        g_sound = !g_sound;
        SaveLauncherState();
        break;
    case MI_THEME:
        g_themeIdx = (g_themeIdx + dir + kThemeCount) % kThemeCount;
        SaveLauncherState();
        CoverClear();          // под прозрачными обложками — цвет темы
        BuildBaseLayer();
        break;
    case MI_BG: {
        ListBackgrounds();
        int i = -1;   // -1 — без фона
        for (int k = 0; k < g_bgCount; k++) if (!_wcsicmp(g_bgList[k], g_bgName)) i = k;
        int n = g_bgCount + 1;
        i = ((i + 1) + dir + n) % n - 1;
        if (i < 0) g_bgName[0] = 0; else COPY(g_bgName, g_bgList[i]);
        LoadBackgroundSource();
        BuildBaseLayer();
        SaveLauncherState();
        break;
    }
    case MI_LANG:
        g_lang = !g_lang;
        SaveLauncherState();
        SetWindowTextW(g_hWnd, L"LGL");
        break;
    case MI_VEIL:
        g_veil = (g_veil + dir + 4) % 4;
        BuildBaseLayer();
        SaveLauncherState();
        break;
    case MI_AUTORUN:
        AutorunSet(!g_autorun);
        g_autorun = AutorunGet();
        break;
    }
}

static void MenuOk()
{
    switch (g_menuSel) {
    case MI_SORT: case MI_SOUND: case MI_AUTORUN: case MI_THEME: case MI_BG: case MI_VEIL: case MI_LANG:
        MenuChange(+1);
        break;
    case MI_RESCAN:
        g_mode = MODE_LIST;
        ReloadGames();
        break;
    case MI_EXIT: case MI_REBOOT: case MI_POWEROFF:
        if (g_menuArmed != g_menuSel) { g_menuArmed = g_menuSel; break; }   // второе нажатие подтверждает
        if (g_menuSel == MI_EXIT) PostMessage(g_hWnd, WM_CLOSE, 0, 0);
        else SystemPower(g_menuSel == MI_REBOOT);
        break;
    }
}

// --- Прыжки по буквам ---
static void ShowLetter()
{
    if (g_nf == 0) return;
    g_letter = FirstLetter(Cur().name);
    g_letterTick = GetTickCount();
}

static void JumpLetter(int dir)
{
    if (g_nf == 0) return;
    wchar_t cur = FirstLetter(Cur().name);
    int i = g_sel;
    if (dir > 0) {
        while (i < g_nf && FirstLetter(g_games[g_filtered[i]].name) == cur) i++;
        if (i >= g_nf) i = g_nf - 1;
    } else {
        int s = i;
        while (s > 0 && FirstLetter(g_games[g_filtered[s - 1]].name) == cur) s--;
        if (s < i) i = s;            // в начало текущей буквы
        else if (s > 0) {            // в начало предыдущей
            wchar_t pl = FirstLetter(g_games[g_filtered[s - 1]].name);
            i = s - 1;
            while (i > 0 && FirstLetter(g_games[g_filtered[i - 1]].name) == pl) i--;
        }
    }
    g_sel = i;
    EnsureVisible();
    ShowLetter();
}

static void JumpToChar(wchar_t ch)
{
    if (g_nf == 0) return;
    wchar_t c = iswdigit(ch) ? L'#' : (wchar_t)towupper(ch);
    for (int k = 1; k <= g_nf; k++) {
        int i = (g_sel + k) % g_nf;
        if (FirstLetter(g_games[g_filtered[i]].name) == c) {
            g_sel = i;
            EnsureVisible();
            ShowLetter();
            g_lastNav = GetTickCount();
            Snd(0);
            return;
        }
    }
}

static void HandleAction(Action a)
{
    if (g_mode == MODE_LAUNCH || g_mode == MODE_RUNNING) return;
    Layout L; CalcLayout(L);
    g_lastNav = GetTickCount();

    switch (a) {
    case ACT_OK: case ACT_MENU: case ACT_FAV: case ACT_START: Snd(1); break;
    case ACT_BACK: Snd(2); break;
    default: Snd(0); break;
    }

    switch (g_mode) {
    case MODE_LIST:
        switch (a) {
        case ACT_UP:    MoveSel(-1); break;
        case ACT_DOWN:  MoveSel(+1); break;
        case ACT_LEFT:  MoveSel(-L.visRows); break;
        case ACT_RIGHT: MoveSel(+L.visRows); break;
        case ACT_OK:    StartLaunch(); break;
        case ACT_MENU:  OpenSettings(); break;
        case ACT_START: OpenMenu(); break;
        case ACT_FAV:
            if (g_nf > 0) { Cur().fav = !Cur().fav; SaveGameState(Cur()); Refilter(true); }
            break;
        case ACT_PREVTAB: { int nt = TabCount(); g_tab = (g_tab + nt - 1) % nt; Refilter(false); } break;
        case ACT_NEXTTAB: { int nt = TabCount(); g_tab = (g_tab + 1) % nt; Refilter(false); } break;
        case ACT_PREVLETTER: JumpLetter(-1); break;
        case ACT_NEXTLETTER: JumpLetter(+1); break;
        default: break;
        }
        break;

    case MODE_SETTINGS:
        switch (a) {
        case ACT_UP:    g_setSel = (g_setSel + kSetCount - 1) % kSetCount; break;
        case ACT_DOWN:  g_setSel = (g_setSel + 1) % kSetCount; break;
        case ACT_LEFT:  ChangeSetting(-1); break;
        case ACT_RIGHT: ChangeSetting(+1); break;
        case ACT_OK:
            if      (g_setSel == SET_LAUNCH) StartLaunch();
            else if (g_setSel == SET_HIDE)   ToggleHidden();
            else if (g_setSel == SET_LAYOUT) OpenEditor();
            else ChangeSetting(+1);
            break;
        case ACT_BACK: case ACT_START: g_mode = MODE_LIST; break;
        default: break;
        }
        break;

    case MODE_EDITOR:
        switch (a) {
        case ACT_UP:   if (g_edSel > 0) g_edSel--; break;
        case ACT_DOWN: if (g_edSel < g_edCount - 1) g_edSel++; break;
        case ACT_PREVLETTER: g_edSel -= 8; if (g_edSel < 0) g_edSel = 0; break;
        case ACT_NEXTLETTER: g_edSel += 8; if (g_edSel > g_edCount - 1) g_edSel = g_edCount - 1; break;
        case ACT_LEFT:  EdChange(-1); break;
        case ACT_RIGHT: EdChange(+1); break;
        case ACT_OK:    EdOk(); break;
        case ACT_MENU:  EdClear(); break;
        case ACT_PREVTAB: case ACT_NEXTTAB:
            if (g_edProf >= 0 && g_prof[g_edProf].layerBtn >= 0) g_edLayer = !g_edLayer;
            break;
        case ACT_BACK: case ACT_START: g_mode = MODE_SETTINGS; break;
        default: break;
        }
        break;

    case MODE_KEYPICK:
        switch (a) {
        case ACT_UP:    if (g_kpSel >= kPickCols) g_kpSel -= kPickCols; break;
        case ACT_DOWN:
            if (g_kpSel + kPickCols < g_kpN) g_kpSel += kPickCols;
            else if (g_kpSel / kPickCols < (g_kpN - 1) / kPickCols) g_kpSel = g_kpN - 1;
            break;
        case ACT_LEFT:  if (g_kpSel > 0) g_kpSel--; break;
        case ACT_RIGHT: if (g_kpSel < g_kpN - 1) g_kpSel++; break;
        case ACT_OK: {
            int code = g_kpCodes[g_kpSel];
            if (code <= -10) g_kpMods ^= 1 << (-code - 10);
            else if (code >= 0) PickerSet(code);
            break;
        }
        case ACT_MENU: PickerSet(-1); break;
        case ACT_BACK: case ACT_START: g_mode = MODE_EDITOR; break;
        default: break;
        }
        break;

    case MODE_MENU:
        switch (a) {
        case ACT_UP:    g_menuSel = (g_menuSel + MI_COUNT - 1) % MI_COUNT; g_menuArmed = -1; break;
        case ACT_DOWN:  g_menuSel = (g_menuSel + 1) % MI_COUNT; g_menuArmed = -1; break;
        case ACT_LEFT:  MenuChange(-1); break;
        case ACT_RIGHT: MenuChange(+1); break;
        case ACT_OK:    MenuOk(); break;
        case ACT_BACK: case ACT_START: g_mode = MODE_LIST; break;
        default: break;
        }
        break;

    case MODE_MESSAGE:
        if (a == ACT_OK || a == ACT_BACK) g_mode = MODE_LIST;
        break;

    default:
        break;
    }
    Redraw();
}

static void FireButton(DWORD bit)
{
    switch (bit) {
    case VB_UP:    HandleAction(ACT_UP); break;
    case VB_DOWN:  HandleAction(ACT_DOWN); break;
    case VB_LEFT:  HandleAction(ACT_LEFT); break;
    case VB_RIGHT: HandleAction(ACT_RIGHT); break;
    case VB_A:     HandleAction(ACT_OK); break;
    case VB_B:     HandleAction(ACT_BACK); break;
    case VB_X:     HandleAction(ACT_FAV); break;
    case VB_Y:     HandleAction(ACT_MENU); break;
    case VB_LB:    HandleAction(ACT_PREVTAB); break;
    case VB_RB:    HandleAction(ACT_NEXTTAB); break;
    case VB_LT:    HandleAction(ACT_PREVLETTER); break;
    case VB_RT:    HandleAction(ACT_NEXTLETTER); break;
    case VB_START: HandleAction(ACT_START); break;
    case VB_BACK:  HandleAction(ACT_BACK); break;
    }
}

static void OnTimer()
{
    DWORD now = GetTickCount();
    DWORD m = PollPad(now);
    DWORD pressed = m & ~g_prevMask;

    if (g_rumbleUntil && (LONG)(now - g_rumbleUntil) >= 0) StopRumble();

    // --- Игра запущена: слушаем только комбинацию закрытия ---
    if (g_mode == MODE_RUNNING) {
        bool combo = (m & (VB_START | VB_BACK)) == (VB_START | VB_BACK);
        if (combo) {
            if (!g_comboHeld) { g_comboHeld = true; g_comboFired = false; g_comboStart = now; }
            else if (!g_comboFired && now - g_comboStart >= 1500) { g_comboFired = true; RequestCloseGame(now); }
        } else {
            g_comboHeld = false;
        }
        g_prevMask = m;
        if (now - g_lastJobCheck >= 250) { g_lastJobCheck = now; CheckGame(now); }
        return;
    }

    // --- Меню ---
    const DWORD kRep = VB_UP | VB_DOWN | VB_LEFT | VB_RIGHT | VB_LB | VB_RB | VB_LT | VB_RT;
    for (DWORD bit = 1; bit <= VB_RT; bit <<= 1) {
        if (pressed & bit) {
            FireButton(bit);
            if (bit & kRep) { g_repBtn = bit; g_repNext = now + 400; }
        }
    }
    if (g_repBtn) {
        if (m & g_repBtn) {
            if ((LONG)(now - g_repNext) >= 0) {
                FireButton(g_repBtn);
                g_repNext = now + ((g_repBtn & (VB_LT | VB_RT)) ? 250 : 70);
            }
        } else {
            g_repBtn = 0;
        }
    }
    g_prevMask = m;

    if (g_mode == MODE_LAUNCH) {
        DWORD el = now - g_launchTick;
        int phase = (int)(el / 150);
        if (phase != g_launchPhase) { g_launchPhase = phase; Redraw(); }
        if (el >= 450) { PlaySoundW(NULL, NULL, 0); DoLaunch(); }
    }

    // Обложка: грузим, когда выбор не менялся 150 мс
    if (g_covPendGame >= 0 && now - g_lastNav >= 150 && g_mode != MODE_LAUNCH) {
        int gi = g_covPendGame;
        g_covPendGame = -1;
        CoverLoad(gi, g_covPendW, g_covPendH);
        Redraw();
    }

    if (g_letter && now - g_letterTick >= 700) { g_letter = 0; Redraw(); }

    if (g_fgRetries > 0) {
        if (GetForegroundWindow() != g_hWnd) ForceForeground();
        else g_fgRetries = 0;
        if (g_fgRetries > 0) g_fgRetries--;
    }

    SYSTEMTIME st; GetLocalTime(&st);
    if (st.wMinute != g_lastMinute) { g_lastMinute = st.wMinute; Redraw(); }
}

// ---------------------------------------------------------------------------
// Оконная процедура
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_hWnd = hWnd;
        return 0;

    case WM_SIZE:
        g_W = LOWORD(lp); g_H = HIWORD(lp);
        if (g_W == 0 || g_H == 0) return 0;
        CreateFonts();
        ResizeBuffer(hWnd);
        BuildBaseLayer(false);
        EnsureVisible();
        Redraw();
        return 0;

    case WM_DISPLAYCHANGE:
        if (g_fullscreen && g_mode != MODE_RUNNING) ApplyFullscreenGeometry();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SETCURSOR:
        if (g_fullscreen && LOWORD(lp) == HTCLIENT) { SetCursor(NULL); return TRUE; }
        break;

    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) return 0;   // Alt не открывает меню окна
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        if (g_memDC) {
            Paint(g_memDC);
            BitBlt(dc, 0, 0, g_W, g_H, g_memDC, 0, 0, SRCCOPY);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_TIMER:
        OnTimer();
        return 0;

    case WM_SYSKEYDOWN:
        if (g_mode == MODE_KEYPICK) { PickerPhysicalKey(lp); Redraw(); return 0; }
        break;

    case WM_SYSCHAR:
        if (g_mode == MODE_KEYPICK) return 0;   // без системного писка на Alt+клавиша
        break;

    case WM_KEYDOWN:
        if (g_mode == MODE_LAUNCH || g_mode == MODE_RUNNING) return 0;
        if (g_mode == MODE_KEYPICK) { PickerPhysicalKey(lp); Redraw(); return 0; }
        switch (wp) {
        case VK_UP:     HandleAction(ACT_UP); break;
        case VK_DOWN:   HandleAction(ACT_DOWN); break;
        case VK_LEFT:   HandleAction(ACT_LEFT); break;
        case VK_RIGHT:  HandleAction(ACT_RIGHT); break;
        case VK_RETURN: HandleAction(ACT_OK); break;
        case VK_BACK:   HandleAction(ACT_BACK); break;
        case VK_SPACE:  HandleAction(ACT_MENU); break;
        case VK_TAB:    HandleAction(ACT_FAV); break;
        case VK_PRIOR:  HandleAction(ACT_PREVTAB); break;
        case VK_NEXT:   HandleAction(ACT_NEXTTAB); break;
        case VK_HOME:   HandleAction(ACT_PREVLETTER); break;
        case VK_END:    HandleAction(ACT_NEXTLETTER); break;
        case VK_F5:
            if (g_mode == MODE_LIST) { ReloadGames(); Redraw(); }
            break;
        case VK_F11:    SetFullscreen(!g_fullscreen); break;
        case VK_ESCAPE:
            if (g_mode == MODE_LIST) HandleAction(ACT_START);   // меню
            else HandleAction(ACT_BACK);
            break;
        }
        return 0;

    case WM_CHAR:
        // Буква на клавиатуре — прыжок к игре на эту букву
        if (g_mode == MODE_LIST && iswalnum((wchar_t)wp)) { JumpToChar((wchar_t)wp); Redraw(); }
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        MapStop();
        StopRumble();
        if (g_proc) { CloseHandle(g_proc); g_proc = NULL; }
        if (g_job)  { CloseHandle(g_job);  g_job = NULL; }
        CoverClear();
        FreeFonts();
        if (g_memDC) {
            SelectObject(g_memDC, g_oldBmp);
            DeleteObject(g_memBmp);
            DeleteDC(g_memDC);
            g_memDC = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow)
{
    // Пути относительно exe
    GetModuleFileNameW(NULL, g_baseDir, MAX_PATH);
    g_baseDir[MAX_PATH - 1] = 0;
    wchar_t* slash = wcsrchr(g_baseDir, L'\\');
    if (slash) *slash = 0;
    JoinPath(g_gamesIni, MAX_PATH, g_baseDir, L"games.ini");
    JoinPath(g_stateIni, MAX_PATH, g_baseDir, L"state.ini");
    JoinPath(g_profilesIni, MAX_PATH, g_baseDir, L"profiles.ini");
    JoinPath(g_ownProfilesIni, MAX_PATH, g_baseDir, L"profiles_games.ini");
    InitializeCriticalSection(&g_pidLock);

    for (int i = 0; i < kCoverCache; i++) { g_cov[i].game = -1; g_cov[i].bmp = NULL; g_cov[i].used = 0; }

    Gdiplus::GdiplusStartupInput gsi;
    g_gdipOk = Gdiplus::GdiplusStartup(&g_gdipToken, &gsi, NULL) == Gdiplus::Ok;

    LoadXInput();
    LoadLanguage();
    LoadGames();
    LoadLauncherState();
    InitPicker();
    LoadSounds();
    LoadBackgroundSource();
    BuildOrder();
    CreateDirectoryW(g_coversDir, NULL);
    {
        wchar_t bgDir[MAX_PATH];
        JoinPath(bgDir, MAX_PATH, g_baseDir, L"backgrounds");
        CreateDirectoryW(bgDir, NULL);
    }

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = L"LGL_Launcher";
    RegisterClassW(&wc);

    RECT r = { 0, 0, 1024, 768 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    CreateWindowW(wc.lpszClassName, L"LGL", WS_OVERLAPPEDWINDOW,
                  CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                  NULL, NULL, hInst, NULL);
    if (!g_hWnd) return 1;

    Refilter(false);
    ShowWindow(g_hWnd, nShow);
    if (g_cfgFullscreen) SetFullscreen(true);
    UpdateWindow(g_hWnd);
    SetTimerMs(16);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoverClear();
    if (g_bgSrc) { delete g_bgSrc; g_bgSrc = NULL; }
    if (g_baseBmp) { DeleteObject(g_baseBmp); g_baseBmp = NULL; }
    if (g_gdipOk) Gdiplus::GdiplusShutdown(g_gdipToken);
    return 0;
}
