// util.cpp - texte, chemins, journal, réglages, presse-papiers
#include "app.h"
#include <shlobj.h>
#include <shellapi.h>
#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <thread>

bool g_en = false;
Settings g_set;
int g_page = PG_SPEED;
HWND g_hwnd = nullptr;

// ---------------------------------------------------------------------------
//  Texte
// ---------------------------------------------------------------------------
std::string Fmt(const char* fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    std::string s;
    if (n > 0) {
        s.resize((size_t)n);
        vsnprintf(&s[0], (size_t)n + 1, fmt, ap2);
    }
    va_end(ap2);
    return s;
}

std::string W2U(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring U2W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string Num(double v, int dec) {
    if (v != v) return "-";
    std::string s = Fmt("%.*f", dec, v < 0 ? -v : v);
    size_t dot = s.find('.');
    std::string ip = dot == std::string::npos ? s : s.substr(0, dot);
    std::string fp = dot == std::string::npos ? "" : s.substr(dot + 1);
    std::string out;
    int cnt = 0;
    for (int i = (int)ip.size() - 1; i >= 0; i--) {
        out.insert(out.begin(), ip[(size_t)i]);
        if (++cnt % 3 == 0 && i > 0) out.insert(0, g_en ? "," : " ");
    }
    if (!fp.empty()) out += (g_en ? "." : ",") + fp;
    return (v < 0 ? "-" : "") + out;
}

std::string Pl(long long n) { return (g_en ? (n != 1) : (n > 1)) ? "s" : ""; }

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::wstring LowerW(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::string FileName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return W2U(p == std::wstring::npos ? path : path.substr(p + 1));
}

std::string Hex(u64 v) { return Fmt("0x%llX", (unsigned long long)v); }

// ---------------------------------------------------------------------------
//  Chemins
// ---------------------------------------------------------------------------
std::wstring ExeDir() {
    wchar_t p[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, p, (DWORD)(sizeof p / sizeof p[0]));
    std::wstring s(p, n);
    size_t k = s.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"." : s.substr(0, k);
}

std::wstring LocalDir() {
    static std::wstring dir;
    if (dir.empty()) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)) && p) {
            dir = std::wstring(p) + L"\\CamrasTool";
            CoTaskMemFree(p);
        } else {
            wchar_t buf[MAX_PATH];
            DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
            dir = (n ? std::wstring(buf, n) : ExeDir()) + L"\\CamrasTool";
        }
        MakeDirs(dir);
    }
    return dir;
}

// Mode portable : réglages et historique à côté de l'exe, sauf si ce dossier est protégé (Program Files)
std::wstring DataDir() {
    static std::wstring dir;
    if (dir.empty()) {                       // --data=DOSSIER : tests automatiques
        std::wstring cmd = GetCommandLineW();
        size_t k = cmd.find(L"--data=");
        if (k != std::wstring::npos) {
            size_t e = cmd.find(k && cmd[k - 1] == L'"' ? L'"' : L' ', k);
            dir = cmd.substr(k + 7, e == std::wstring::npos ? e : e - k - 7);
            MakeDirs(dir);
        }
    }
    if (dir.empty()) {
        std::wstring exe = ExeDir();
        std::wstring probe = exe + L"\\.camras_write_test";
        HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); dir = exe; }
        else dir = LocalDir();
    }
    return dir;
}

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool MakeDirs(const std::wstring& p) {
    if (p.empty() || DirExists(p)) return true;
    size_t k = p.find_last_of(L"\\/");
    if (k != std::wstring::npos && k > 2) MakeDirs(p.substr(0, k));
    return CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ReadFileAll(const std::wstring& p, std::string& out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    bool ok = sz.QuadPart == 0 || (ReadFile(h, &out[0], (DWORD)sz.QuadPart, &rd, nullptr) && rd == (DWORD)sz.QuadPart);
    CloseHandle(h);
    return ok;
}

bool WriteFileAll(const std::wstring& p, const void* data, size_t n) {
    std::wstring tmp = p + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = n == 0 || (WriteFile(h, data, (DWORD)n, &wr, nullptr) && wr == n);
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}

std::string NowStamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return Fmt("%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

double NowSec() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

// ---------------------------------------------------------------------------
//  Journal
// ---------------------------------------------------------------------------
static std::mutex g_logMx;
static std::vector<LogLine> g_log;

void Log(int lvl, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::lock_guard<std::mutex> l(g_logMx);
    // Copie sur disque (2 Mo, un fichier précédent gardé)
    static HANDLE file = INVALID_HANDLE_VALUE;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::wstring p = DataDir() + L"\\CamrasTool.log";
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > (2u << 20)) MoveFileExW(p.c_str(), (p + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
        file = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    }
    if (file != INVALID_HANDLE_VALUE) {
        std::string line = Fmt("%04d-%02d-%02d %02d:%02d:%02d  ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond) + buf + "\r\n";
        DWORD w;
        WriteFile(file, line.data(), (DWORD)line.size(), &w, nullptr);
    }
    g_log.push_back({ Fmt("%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond), buf, lvl });
    if (g_log.size() > 5000) g_log.erase(g_log.begin(), g_log.begin() + 1000);
}

std::vector<LogLine> LogCopy() {
    std::lock_guard<std::mutex> l(g_logMx);
    return g_log;
}

void LogClear() {
    std::lock_guard<std::mutex> l(g_logMx);
    g_log.clear();
}

// ---------------------------------------------------------------------------
//  Notifications, presse-papiers, liens
// ---------------------------------------------------------------------------
static std::mutex g_toastMx;
static ToastMsg g_toast{ "", false, -100 };

void Toast(const std::string& msg, bool err) {
    std::lock_guard<std::mutex> l(g_toastMx);
    g_toast = { msg, err, NowSec() };
}

ToastMsg ToastGet() {
    std::lock_guard<std::mutex> l(g_toastMx);
    return g_toast;
}

bool CopyToClipboard(const std::string& utf8) {
    std::wstring w = U2W(utf8);
    bool open = false;
    for (int i = 0; i < 10 && !open; i++) {
        open = OpenClipboard(g_hwnd) != 0;
        if (!open) Sleep(20);
    }
    if (!open) return false;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    bool ok = false;
    if (h) {
        memcpy(GlobalLock(h), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
        if (!ok) GlobalFree(h);
    }
    CloseClipboard();
    return ok;
}

void OpenUrl(const char* url) {
    ShellExecuteW(nullptr, L"open", U2W(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
//  Une seule opération sur la carte à la fois
// ---------------------------------------------------------------------------
static std::atomic<bool> g_job{ false };

bool StartJob(std::function<void()> fn) {
    bool expected = false;
    if (!g_job.compare_exchange_strong(expected, true)) return false;
    std::thread([fn] { fn(); g_job = false; }).detach();
    return true;
}

bool JobRunning() { return g_job; }

// ---------------------------------------------------------------------------
//  Réglages : CamrasTool.ini (mêmes sections et clés que la version 1.1)
// ---------------------------------------------------------------------------
static const char* TEST_KEYS[8] = { "lat8", "seq4k", "seq64k", "seq1m", "seq8m", "sc64", "sc256", "sc1024" };

static std::wstring IniPath() { return DataDir() + L"\\CamrasTool.ini"; }

static std::wstring IniGet(const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[1024];
    GetPrivateProfileStringW(sec, key, def, buf, 1024, IniPath().c_str());
    return buf;
}

static int IniInt(const wchar_t* sec, const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(sec, key, def, IniPath().c_str());
}

static void IniGetA(const wchar_t* sec, const wchar_t* key, char* dst, size_t n) {
    std::string v = W2U(IniGet(sec, key, U2W(dst).c_str()));
    strncpy_s(dst, n, v.c_str(), _TRUNCATE);
}

void LoadSettings() {
    Settings& s = g_set;
    s.dll = IniGet(L"connexion", L"dll", L"");
    s.mode = std::clamp(IniInt(L"connexion", L"mode", s.mode), 0, 2);
    IniGetA(L"connexion", L"personnalise", s.custom, sizeof s.custom);
    s.algo = std::clamp(IniInt(L"connexion", L"algo", s.algo), 0, 4);
    s.journal = std::clamp(IniInt(L"connexion", L"journal", s.journal), 0, 2);
    IniGetA(L"test", L"base", s.base, sizeof s.base);
    s.sizeMb = std::clamp(IniInt(L"test", L"taille_mo", s.sizeMb), 16, 1 << 20);
    s.duration = std::clamp(IniInt(L"test", L"duree", s.duration), 1, 60);
    s.testMode = std::clamp(IniInt(L"test", L"mode", s.testMode), 0, 4);
    s.stabMin = std::clamp(IniInt(L"test", L"stabilite_min", s.stabMin), 1, 600);
    s.zoneAuto = IniInt(L"test", L"zone_auto", s.zoneAuto) != 0;
    s.runs = IniInt(L"test", L"passages", s.runs) >= 3 ? 3 : 1;
    s.notify = IniInt(L"apparence", L"notification", s.notify) != 0;
    s.guideDone = IniInt(L"apparence", L"assistant_vu", s.guideDone) != 0;
    s.release = IniInt(L"connexion", L"liberer", s.release) != 0;
    s.updates = IniInt(L"apparence", L"maj", s.updates) != 0;
    s.fwBackup = IniInt(L"firmware", L"sauvegarde", s.fwBackup) != 0;
    for (int i = 0; i < 8; i++)
        s.tests[i] = IniInt(L"tests", U2W(std::string("t_") + TEST_KEYS[i]).c_str(), s.tests[i]) != 0;
    s.dark = IniInt(L"apparence", L"sombre", s.dark) != 0;
    s.english = IniInt(L"apparence", L"anglais", s.english) != 0;
    IniGetA(L"firmware", L"modele", s.fwModel, sizeof s.fwModel);
    s.fwType = std::clamp(IniInt(L"firmware", L"type", s.fwType), 0, 1);
    IniGetA(L"firmware", L"horloge", s.fwClock, sizeof s.fwClock);
    s.fwFile = IniGet(L"firmware", L"fichier", L"");
    g_en = s.english;
}

void SaveSettings() {
    std::wstring p = IniPath();
    if (!FileExists(p)) {
        // Fichier en UTF-16 : les chemins avec accents sont conservés
        const unsigned char bom[2] = { 0xFF, 0xFE };
        HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, bom, 2, &w, nullptr); CloseHandle(h); }
    }
    const Settings& s = g_set;
    auto put = [&](const wchar_t* sec, const wchar_t* key, const std::wstring& v) {
        WritePrivateProfileStringW(sec, key, v.c_str(), p.c_str());
    };
    auto puti = [&](const wchar_t* sec, const wchar_t* key, int v) { put(sec, key, std::to_wstring(v)); };
    put(L"connexion", L"dll", s.dll);
    puti(L"connexion", L"mode", s.mode);
    put(L"connexion", L"personnalise", U2W(s.custom));
    puti(L"connexion", L"algo", s.algo);
    puti(L"connexion", L"journal", s.journal);
    put(L"test", L"base", U2W(s.base));
    puti(L"test", L"taille_mo", s.sizeMb);
    puti(L"test", L"duree", s.duration);
    puti(L"test", L"mode", s.testMode);
    puti(L"test", L"stabilite_min", s.stabMin);
    puti(L"test", L"zone_auto", s.zoneAuto);
    puti(L"test", L"passages", s.runs);
    puti(L"apparence", L"notification", s.notify);
    puti(L"apparence", L"assistant_vu", s.guideDone);
    puti(L"connexion", L"liberer", s.release);
    puti(L"apparence", L"maj", s.updates);
    puti(L"firmware", L"sauvegarde", s.fwBackup);
    for (int i = 0; i < 8; i++) puti(L"tests", U2W(std::string("t_") + TEST_KEYS[i]).c_str(), s.tests[i]);
    puti(L"apparence", L"sombre", s.dark);
    puti(L"apparence", L"anglais", s.english);
    put(L"firmware", L"modele", U2W(s.fwModel));
    puti(L"firmware", L"type", s.fwType);
    put(L"firmware", L"horloge", U2W(s.fwClock));
    put(L"firmware", L"fichier", s.fwFile);
}
