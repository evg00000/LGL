// PadDriverFix.cpp — ставит драйвер Xbox 360 (xusb21) на XInput-совместимые
// геймпады сторонних производителей под Windows XP.
//
// Как работает:
//   1. Ищет подключённые USB-устройства с интерфейсом Xbox 360
//      (compatible ID USB\Class_FF&SubClass_5D&Prot_01) — так их опознаёт
//      и Windows 10, поэтому список моделей не нужен.
//   2. Находит установленный xusb21.inf (пакет Xbox 360 Accessories 1.2)
//      в C:\WINDOWS\inf\oem*.inf и делает его копию с добавленными ID падов.
//   3. Собирает рядом файлы драйвера из системы и ставит драйвер на пады.
//
// Сборка: VS2010, Win32 Console Application, Unicode, Release, /MT.
// Запуск: от администратора, пад подключён в режиме X-input.
// Windows предупредит, что драйвер не подписан, — нажмите «Всё равно продолжить».

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
#include <setupapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef _MSC_VER
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "user32.lib")
#endif

#ifndef INSTALLFLAG_FORCE
#define INSTALLFLAG_FORCE 0x00000001
#endif
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

// Язык сообщений — по языку Windows: 0 — русский, 1 — английский
static int g_lang = 1;
#define TR(ru, en) (g_lang ? (en) : (ru))

typedef BOOL (WINAPI *PFN_UpdateDriver)(HWND, LPCWSTR, LPCWSTR, DWORD, PBOOL);

static HANDLE g_out;

static void Out(const wchar_t* fmt, ...)
{
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 2047, fmt, ap);
    va_end(ap);
    buf[2047] = 0;
    DWORD w;
    if (!WriteConsoleW(g_out, buf, (DWORD)wcslen(buf), &w, NULL)) {
        // вывод перенаправлен в файл
        char a[4096];
        int n = WideCharToMultiByte(CP_OEMCP, 0, buf, -1, a, sizeof(a), NULL, NULL);
        if (n > 1) WriteFile(g_out, a, n - 1, &w, NULL);
    }
}

static void Pause()
{
    Out(TR(L"\nНажмите Enter, чтобы закрыть окно...", L"\nPress Enter to close this window..."));
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    FlushConsoleInputBuffer(in);
    wchar_t c[8];
    DWORD r;
    ReadConsoleW(in, c, 1, &r, NULL);
}

static bool FileExists(const wchar_t* p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void SysError(DWORD err, wchar_t* buf, int cap)
{
    buf[0] = 0;
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, buf, cap, NULL);
    size_t n = wcslen(buf);
    while (n > 0 && (buf[n - 1] == L'\n' || buf[n - 1] == L'\r' || buf[n - 1] == L' ')) buf[--n] = 0;
}

static void MakeDirs(const wchar_t* path)
{
    wchar_t tmp[MAX_PATH];
    wcsncpy(tmp, path, MAX_PATH - 1);
    tmp[MAX_PATH - 1] = 0;
    for (wchar_t* p = tmp + 3; *p; p++) {
        if (*p == L'\\') { *p = 0; CreateDirectoryW(tmp, NULL); *p = L'\\'; }
    }
    CreateDirectoryW(tmp, NULL);
}

// Текст inf: UTF-16LE с BOM или ANSI
static wchar_t* ReadText(const wchar_t* path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size > 2 * 1024 * 1024) { CloseHandle(f); return NULL; }
    unsigned char* raw = (unsigned char*)malloc(size + 2);
    DWORD rd = 0;
    if (!raw || !ReadFile(f, raw, size, &rd, NULL)) { CloseHandle(f); free(raw); return NULL; }
    CloseHandle(f);
    wchar_t* out;
    if (rd >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        int n = (int)((rd - 2) / 2);
        out = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
        memcpy(out, raw + 2, n * sizeof(wchar_t));
        out[n] = 0;
    } else {
        const char* p = (const char*)raw;
        int len = (int)rd;
        UINT cp = CP_ACP;
        if (rd >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) { p += 3; len -= 3; cp = CP_UTF8; }
        int n = len > 0 ? MultiByteToWideChar(cp, 0, p, len, NULL, 0) : 0;
        out = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
        if (n > 0) MultiByteToWideChar(cp, 0, p, len, out, n);
        out[n] = 0;
    }
    free(raw);
    return out;
}

static bool WriteUtf16(const wchar_t* path, const wchar_t* text)
{
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    static const unsigned char bom[2] = { 0xFF, 0xFE };
    DWORD w;
    WriteFile(f, bom, 2, &w, NULL);
    BOOL ok = WriteFile(f, text, (DWORD)(wcslen(text) * sizeof(wchar_t)), &w, NULL);
    CloseHandle(f);
    return ok != FALSE;
}

static wchar_t* WcsIStr(const wchar_t* hay, const wchar_t* needle)
{
    size_t n = wcslen(needle);
    for (; *hay; hay++)
        if (!_wcsnicmp(hay, needle, n)) return (wchar_t*)hay;
    return NULL;
}

// ---------------------------------------------------------------------------
// Поиск падов
// ---------------------------------------------------------------------------
struct Pad {
    wchar_t hwid[256];
    wchar_t name[256];
    wchar_t service[64];
};
static Pad g_pads[16];
static int g_padCount = 0;

static const wchar_t* kXusbClass = L"USB\\Class_FF&SubClass_5D&Prot_01";

static void FindPads()
{
    HDEVINFO h = SetupDiGetClassDevsW(NULL, L"USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (h == INVALID_HANDLE_VALUE) return;
    SP_DEVINFO_DATA d;
    d.cbSize = sizeof(d);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(h, i, &d) && g_padCount < 16; i++) {
        wchar_t buf[2048];
        DWORD type = 0, size = 0;
        ZeroMemory(buf, sizeof(buf));
        if (!SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_COMPATIBLEIDS, &type, (BYTE*)buf, sizeof(buf) - 4, &size))
            continue;
        bool match = false;
        for (const wchar_t* s = buf; *s; s += wcslen(s) + 1)
            if (!_wcsicmp(s, kXusbClass)) { match = true; break; }
        if (!match) continue;

        Pad& p = g_pads[g_padCount];
        ZeroMemory(&p, sizeof(p));
        // Аппаратный ID без ревизии: USB\VID_xxxx&PID_yyyy[&MI_zz]
        ZeroMemory(buf, sizeof(buf));
        if (!SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_HARDWAREID, &type, (BYTE*)buf, sizeof(buf) - 4, &size))
            continue;
        const wchar_t* pick = buf;
        for (const wchar_t* s = buf; *s; s += wcslen(s) + 1)
            if (!WcsIStr(s, L"REV_")) { pick = s; break; }
        wcsncpy(p.hwid, pick, 255);

        if (!SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_DEVICEDESC, &type, (BYTE*)p.name, sizeof(p.name) - 2, &size))
            wcscpy(p.name, TR(L"(без названия)", L"(no name)"));
        if (!SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_SERVICE, &type, (BYTE*)p.service, sizeof(p.service) - 2, &size))
            p.service[0] = 0;
        g_padCount++;
    }
    SetupDiDestroyDeviceInfoList(h);
}

// ---------------------------------------------------------------------------
// Поиск установленного xusb21.inf
// ---------------------------------------------------------------------------
static bool FindXusbInf(wchar_t* out)
{
    wchar_t win[MAX_PATH], mask[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    _snwprintf(mask, MAX_PATH, L"%ls\\inf\\oem*.inf", win);
    mask[MAX_PATH - 1] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(mask, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        wchar_t path[MAX_PATH];
        _snwprintf(path, MAX_PATH, L"%ls\\inf\\%ls", win, fd.cFileName);
        path[MAX_PATH - 1] = 0;
        wchar_t* t = ReadText(path);
        if (!t) continue;
        if (WcsIStr(t, L"xusb21.sys") && WcsIStr(t, L"Vid_045E&Pid_028E")) {
            wcscpy(out, path);
            found = true;
        }
        free(t);
    } while (!found && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

// ---------------------------------------------------------------------------
// Патч inf и сбор файлов драйвера
// ---------------------------------------------------------------------------
struct DiskEntry { wchar_t id[16]; wchar_t path[MAX_PATH]; };
struct SrcFile   { wchar_t name[MAX_PATH]; wchar_t disk[16]; wchar_t sub[MAX_PATH]; };
static DiskEntry g_disks[16];
static int       g_diskCount = 0;
static SrcFile   g_files[64];
static int       g_fileCount = 0;

static void TrimW(wchar_t* s)
{
    wchar_t* p = s;
    while (*p == L' ' || *p == L'\t') p++;
    if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));
    size_t n = wcslen(s);
    while (n > 0 && (s[n - 1] == L' ' || s[n - 1] == L'\t' || s[n - 1] == L'\r' || s[n - 1] == L'"')) s[--n] = 0;
    if (s[0] == L'"') memmove(s, s + 1, wcslen(s) * sizeof(wchar_t));
}

// Поле номер idx из списка через запятую
static void Field(const wchar_t* val, int idx, wchar_t* out, int cap)
{
    out[0] = 0;
    int i = 0;
    const wchar_t* p = val;
    while (i < idx && *p) { if (*p == L',') i++; p++; }
    if (i < idx) return;
    int n = 0;
    while (*p && *p != L',' && *p != L';' && n < cap - 1) out[n++] = *p++;
    out[n] = 0;
    TrimW(out);
}

// Строит текст нового inf. Возвращает число добавленных строк.
static int BuildPatchedInf(const wchar_t* src, wchar_t* dst, int cap, const Pad* pads, const bool* need, int n)
{
    dst[0] = 0;
    int added = 0;
    wchar_t section[128] = L"";
    const wchar_t* p = src;
    while (*p) {
        const wchar_t* e = wcschr(p, L'\n');
        size_t len = e ? (size_t)(e - p + 1) : wcslen(p);
        wchar_t line[1024];
        size_t l = len < 1023 ? len : 1023;
        wcsncpy(line, p, l);
        line[l] = 0;
        p += len;

        wchar_t t[1024];
        wcscpy(t, line);
        TrimW(t);

        if (t[0] == L'[') {
            const wchar_t* q = wcschr(t, L']');
            size_t sl = q ? (size_t)(q - t - 1) : 0;
            if (sl > 127) sl = 127;
            wcsncpy(section, t + 1, sl);
            section[sl] = 0;
        }

        // Секции с исходными файлами — запоминаем
        if (t[0] && t[0] != L';' && t[0] != L'[') {
            const wchar_t* eq = wcschr(t, L'=');
            if (eq) {
                wchar_t key[MAX_PATH], val[1024];
                size_t kl = (size_t)(eq - t);
                if (kl > MAX_PATH - 1) kl = MAX_PATH - 1;
                wcsncpy(key, t, kl); key[kl] = 0; TrimW(key);
                wcscpy(val, eq + 1);
                if (!_wcsnicmp(section, L"SourceDisksNames", 16) && g_diskCount < 16) {
                    wcsncpy(g_disks[g_diskCount].id, key, 15);
                    g_disks[g_diskCount].id[15] = 0;
                    Field(val, 3, g_disks[g_diskCount].path, MAX_PATH);
                    g_diskCount++;
                } else if (!_wcsnicmp(section, L"SourceDisksFiles", 16) && g_fileCount < 64) {
                    SrcFile& f = g_files[g_fileCount];
                    wcscpy(f.name, key);
                    Field(val, 0, f.disk, 16);
                    Field(val, 1, f.sub, MAX_PATH);
                    bool dup = false;
                    for (int k = 0; k < g_fileCount; k++)
                        if (!_wcsicmp(g_files[k].name, f.name)) dup = true;
                    if (!dup) g_fileCount++;
                }
            }
        }

        // Подпись каталога больше не совпадёт — убираем
        if (!_wcsnicmp(t, L"CatalogFile", 11)) {
            wcsncat(dst, L";", cap - wcslen(dst) - 1);
        }
        wcsncat(dst, line, cap - wcslen(dst) - 1);
        if (!e && line[0]) wcsncat(dst, L"\r\n", cap - wcslen(dst) - 1);

        // Строка модели проводного пада Microsoft — дублируем для наших ID
        wchar_t* hit = WcsIStr(line, L"USB\\Vid_045E&Pid_028E");
        if (hit && t[0] != L';' && _wcsnicmp(section, L"SourceDisks", 11)) {
            size_t pre = (size_t)(hit - line);
            const wchar_t* post = hit + wcslen(L"USB\\Vid_045E&Pid_028E");
            for (int i = 0; i < n; i++) {
                if (!need[i] || WcsIStr(src, pads[i].hwid)) continue;
                wchar_t nl[1024];
                wcsncpy(nl, line, pre);
                nl[pre] = 0;
                wcsncat(nl, pads[i].hwid, 1023 - wcslen(nl));
                wcsncat(nl, post, 1023 - wcslen(nl));
                size_t nlen = wcslen(nl);
                if (nlen == 0 || nl[nlen - 1] != L'\n') wcsncat(nl, L"\r\n", 1023 - wcslen(nl));
                wcsncat(dst, nl, cap - wcslen(dst) - 1);
                added++;
            }
        }
    }
    return added;
}

static bool FindDriverFile(const wchar_t* name, wchar_t* found)
{
    wchar_t sys[MAX_PATH], win[MAX_PATH], pf[MAX_PATH], self[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    GetWindowsDirectoryW(win, MAX_PATH);
    if (!GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH)) wcscpy(pf, L"C:\\Program Files");
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wchar_t* sl = wcsrchr(self, L'\\');
    if (sl) *sl = 0;

    const wchar_t* dirs[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
    wchar_t d0[MAX_PATH], d1[MAX_PATH], d2[MAX_PATH], d3[MAX_PATH];
    _snwprintf(d0, MAX_PATH, L"%ls\\drivers", sys);
    _snwprintf(d1, MAX_PATH, L"%ls\\Microsoft Xbox 360 Accessories", pf);
    _snwprintf(d2, MAX_PATH, L"%ls\\system32\\DRVSTORE", win);
    _snwprintf(d3, MAX_PATH, L"%ls\\xusb_files", self);
    dirs[0] = self; dirs[1] = d3; dirs[2] = d0; dirs[3] = sys; dirs[4] = d1; dirs[5] = d2;
    for (int i = 0; i < 6; i++) {
        _snwprintf(found, MAX_PATH, L"%ls\\%ls", dirs[i], name);
        found[MAX_PATH - 1] = 0;
        if (FileExists(found)) return true;
    }
    return false;
}

// Раскладывает файлы драйвера в папку пакета. Возвращает число ненайденных.
static int CollectFiles(const wchar_t* pkg)
{
    int missing = 0;
    for (int i = 0; i < g_fileCount; i++) {
        const SrcFile& f = g_files[i];
        wchar_t rel[MAX_PATH] = L"";
        for (int k = 0; k < g_diskCount; k++)
            if (!_wcsicmp(g_disks[k].id, f.disk)) wcscpy(rel, g_disks[k].path);
        if (f.sub[0]) {
            if (rel[0]) wcsncat(rel, L"\\", MAX_PATH - wcslen(rel) - 1);
            wcsncat(rel, f.sub, MAX_PATH - wcslen(rel) - 1);
        }
        const wchar_t* r = rel;
        while (*r == L'\\') r++;

        wchar_t dir[MAX_PATH], dst[MAX_PATH], srcPath[MAX_PATH];
        if (*r) _snwprintf(dir, MAX_PATH, L"%ls\\%ls", pkg, r);
        else    _snwprintf(dir, MAX_PATH, L"%ls", pkg);
        dir[MAX_PATH - 1] = 0;
        MakeDirs(dir);
        _snwprintf(dst, MAX_PATH, L"%ls\\%ls", dir, f.name);
        dst[MAX_PATH - 1] = 0;

        if (FileExists(dst)) { Out(TR(L"   %-28ls уже на месте\n", L"   %-28ls already in place\n"), f.name); continue; }
        if (FindDriverFile(f.name, srcPath) && CopyFileW(srcPath, dst, FALSE)) {
            Out(TR(L"   %-28ls взят из %ls\n", L"   %-28ls taken from %ls\n"), f.name, srcPath);
        } else {
            Out(TR(L"   %-28ls НЕ НАЙДЕН\n", L"   %-28ls NOT FOUND\n"), f.name);
            missing++;
        }
    }
    return missing;
}

// ---------------------------------------------------------------------------
int wmain()
{
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    {
        WORD pl = PRIMARYLANGID(GetUserDefaultUILanguage());
        g_lang = (pl == LANG_RUSSIAN || pl == LANG_UKRAINIAN || pl == LANG_BELARUSIAN) ? 0 : 1;
    }
    SetConsoleTitleW(L"PadDriverFix");
    Out(TR(L"PadDriverFix — драйвер Xbox 360 для совместимых геймпадов\n\n", L"PadDriverFix - Xbox 360 driver for compatible gamepads\n\n"));

    // 1. Пады
    FindPads();
    if (g_padCount == 0) {
        Out(TR(L"Не найдено ни одного геймпада с интерфейсом Xbox 360.\n"
               L"Подключите пад в режиме X-input (у 8BitDo — зажать X при подключении)\n"
               L"и запустите программу снова.\n",
               L"No gamepad with an Xbox 360 interface was found.\n"
               L"Connect the pad in X-input mode (8BitDo: hold X while plugging in)\n"
               L"and run the program again.\n"));
        Pause();
        return 1;
    }
    bool need[16];
    int needCount = 0;
    Out(TR(L"Найдены геймпады:\n", L"Gamepads found:\n"));
    for (int i = 0; i < g_padCount; i++) {
        need[i] = _wcsicmp(g_pads[i].service, L"xusb21") != 0;
        if (need[i]) needCount++;
        Out(L"  %ls\n    %ls  \x2014  %ls\n", g_pads[i].name, g_pads[i].hwid,
            need[i] ? TR(L"нужен драйвер", L"needs the driver") : TR(L"драйвер уже стоит", L"driver already installed"));
    }
    if (needCount == 0) {
        Out(TR(L"\nВсе найденные пады уже работают с драйвером Xbox 360. Делать нечего.\n", L"\nAll pads found already use the Xbox 360 driver. Nothing to do.\n"));
        Pause();
        return 0;
    }

    // 2. inf
    wchar_t infPath[MAX_PATH];
    if (!FindXusbInf(infPath)) {
        Out(TR(L"\nНе найден установленный драйвер Xbox 360 (xusb21.inf).\n"
               L"Сначала установите Xbox 360 Accessories Software 1.2 для XP (Xbox360_32Eng.exe).\n",
               L"\nThe Xbox 360 driver (xusb21.inf) is not installed.\n"
               L"Install Xbox 360 Accessories Software 1.2 for XP (Xbox360_32Eng.exe) first.\n"));
        Pause();
        return 1;
    }
    Out(TR(L"\nДрайвер Xbox 360: %ls\n", L"\nXbox 360 driver: %ls\n"), infPath);

    wchar_t* src = ReadText(infPath);
    if (!src) { Out(TR(L"Не удалось прочитать файл.\n", L"Could not read the file.\n")); Pause(); return 1; }
    int cap = (int)wcslen(src) + 64 * 1024;
    wchar_t* dst = (wchar_t*)malloc(cap * sizeof(wchar_t));
    int added = BuildPatchedInf(src, dst, cap, g_pads, need, g_padCount);
    free(src);
    if (added == 0) {
        Out(TR(L"Не удалось найти в inf строку модели проводного пада. Пришлите этот inf разработчику.\n", L"Could not find the wired controller model line in the inf. Please send this inf to the developer.\n"));
        free(dst);
        Pause();
        return 1;
    }

    // 3. Пакет: папка xusb_patched рядом с программой
    wchar_t pkg[MAX_PATH], self[MAX_PATH], newInf[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wchar_t* sl = wcsrchr(self, L'\\');
    if (sl) *sl = 0;
    _snwprintf(pkg, MAX_PATH, L"%ls\\xusb_patched", self);
    pkg[MAX_PATH - 1] = 0;
    MakeDirs(pkg);
    _snwprintf(newInf, MAX_PATH, L"%ls\\xusb21_patched.inf", pkg);
    newInf[MAX_PATH - 1] = 0;
    if (!WriteUtf16(newInf, dst)) {
        Out(TR(L"Не удалось записать %ls\n", L"Could not write %ls\n"), newInf);
        free(dst);
        Pause();
        return 1;
    }
    free(dst);
    Out(TR(L"Создан %ls (добавлено строк: %d)\n\nФайлы драйвера:\n", L"Created %ls (lines added: %d)\n\nDriver files:\n"), newInf, added);

    int missing = CollectFiles(pkg);
    if (missing > 0) {
        Out(TR(L"\nНе хватает файлов драйвера (%d). Распакуйте Xbox360_32Eng.exe\n"
               L"(например, 7-Zip'ом) и положите недостающие файлы в папку\n  %ls\\xusb_files\n"
               L"рядом с программой, затем запустите её снова.\n",
               L"\n%d driver file(s) are missing. Extract Xbox360_32Eng.exe\n"
               L"(for example with 7-Zip) and put the missing files into\n  %ls\\xusb_files\n"
               L"next to the program, then run it again.\n"), missing, self);
        Pause();
        return 1;
    }

    // 4. Установка
    HMODULE nd = LoadLibraryW(L"newdev.dll");
    PFN_UpdateDriver upd = nd ? (PFN_UpdateDriver)GetProcAddress(nd, "UpdateDriverForPlugAndPlayDevicesW") : NULL;
    if (!upd) { Out(TR(L"\nНе найдена функция установки драйверов (newdev.dll).\n", L"\nThe driver install function (newdev.dll) was not found.\n")); Pause(); return 1; }

    Out(TR(L"\nУстановка. Если Windows предупредит о неподписанном драйвере —\n"
           L"нажмите «Всё равно продолжить».\n\n",
           L"\nInstalling. If Windows warns about an unsigned driver,\n"
           L"click \"Continue Anyway\".\n\n"));
    bool anyReboot = false;
    int ok = 0;
    for (int i = 0; i < g_padCount; i++) {
        if (!need[i]) continue;
        BOOL reboot = FALSE;
        Out(L"  %ls ... ", g_pads[i].hwid);
        if (upd(GetConsoleWindow(), g_pads[i].hwid, newInf, INSTALLFLAG_FORCE, &reboot)) {
            Out(TR(L"готово\n", L"done\n"));
            ok++;
            if (reboot) anyReboot = true;
        } else {
            DWORD err = GetLastError();
            wchar_t es[512];
            SysError(err, es, 512);
            Out(TR(L"ошибка 0x%08lX: %ls\n", L"error 0x%08lX: %ls\n"), err, es);
            if (err == ERROR_ACCESS_DENIED) Out(TR(L"    Запустите программу от имени администратора.\n", L"    Run the program as administrator.\n"));
        }
    }
    Out(TR(L"\nУстановлено: %d из %d.\n", L"\nInstalled: %d of %d.\n"), ok, needCount);
    if (anyReboot) Out(TR(L"Нужна перезагрузка, чтобы драйвер заработал.\n", L"A restart is needed for the driver to work.\n"));
    if (ok > 0) Out(TR(L"Проверьте пад в Панели управления \x2192 Игровые устройства.\n", L"Check the pad in Control Panel \x2192 Game Controllers.\n"));
    Pause();
    return ok == needCount ? 0 : 1;
}
