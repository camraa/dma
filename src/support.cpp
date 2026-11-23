// support.cpp - rapport de plantage local et zip « Signaler un problème » (rien n'est envoyé tout seul)
#include "diag.h"
#include "bench.h"
#include <dbghelp.h>
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "dbghelp.lib")

std::wstring ReportDir() {
    std::wstring d = DataDir() + L"\\rapports";
    MakeDirs(d);
    return d;
}

// ---------------------------------------------------------------------------
//  Plantage : un fichier texte (joint au signalement) et un .dmp qui reste sur le PC
// ---------------------------------------------------------------------------
static wchar_t g_crashTxt[MAX_PATH], g_crashDmp[MAX_PATH];

static LONG WINAPI OnCrash(EXCEPTION_POINTERS* ep) {
    static LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_EXECUTE_HANDLER;
    // Pas d'allocation ici : le tas peut être corrompu
    char buf[1024];
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &mod);
    char path[MAX_PATH] = "?";
    if (mod) GetModuleFileNameA(mod, path, MAX_PATH);
    const char* name = strrchr(path, '\\');
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = wsprintfA(buf, "Camra's Tool %s\r\n%04d-%02d-%02d %02d:%02d:%02d\r\nException 0x%08X\r\nModule %s + 0x%X\r\n", APP_VERSION, t.wYear, t.wMonth, t.wDay,
                      t.wHour, t.wMinute, t.wSecond, ep->ExceptionRecord->ExceptionCode, name ? name + 1 : path, (unsigned)((ULONG_PTR)addr - (ULONG_PTR)mod));
    DWORD w;
    HANDLE h = CreateFileW(g_crashTxt, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, buf, (DWORD)n, &w, nullptr); CloseHandle(h); }
    h = CreateFileW(g_crashDmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mi = { GetCurrentThreadId(), ep, FALSE };
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, MiniDumpNormal, &mi, nullptr, nullptr);
        CloseHandle(h);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void CrashInit() {
    std::wstring d = ReportDir();
    wcsncpy_s(g_crashTxt, (d + L"\\plantage.txt").c_str(), _TRUNCATE);
    wcsncpy_s(g_crashDmp, (d + L"\\plantage.dmp").c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(OnCrash);
}

// ---------------------------------------------------------------------------
//  Zip minimal sans compression
// ---------------------------------------------------------------------------
uint32_t Crc32(const std::string& s) {
    static uint32_t tab[256];
    static bool init = [] {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
        return true;
    }();
    (void)init;
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : s) c = tab[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string MakeZip(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, dir;
    auto p16 = [](std::string& s, unsigned v) { s += (char)(v & 0xFF); s += (char)(v >> 8 & 0xFF); };
    auto p32 = [&](std::string& s, uint32_t v) { p16(s, v & 0xFFFF); p16(s, v >> 16); };
    SYSTEMTIME t;
    GetLocalTime(&t);
    unsigned tm = t.wHour << 11 | t.wMinute << 5 | t.wSecond / 2, dt = (t.wYear - 1980) << 9 | t.wMonth << 5 | t.wDay;
    for (auto& f : files) {
        uint32_t off = (uint32_t)out.size(), crc = Crc32(f.second), n = (uint32_t)f.second.size();
        std::string h;
        p16(h, 20); p16(h, 0x800); p16(h, 0); p16(h, tm); p16(h, dt); p32(h, crc); p32(h, n); p32(h, n); p16(h, (unsigned)f.first.size()); p16(h, 0);
        out += std::string("PK\x03\x04", 4) + h + f.first + f.second;
        dir += std::string("PK\x01\x02", 4);
        p16(dir, 20);
        dir += h;
        p16(dir, 0); p16(dir, 0); p16(dir, 0); p32(dir, 0); p32(dir, off);
        dir += f.first;
    }
    uint32_t start = (uint32_t)out.size();
    out += dir + std::string("PK\x05\x06", 4);
    p16(out, 0); p16(out, 0); p16(out, (unsigned)files.size()); p16(out, (unsigned)files.size());
    p32(out, (uint32_t)dir.size()); p32(out, start); p16(out, 0);
    return out;
}

// Les vidages de registres LeechCore (octets en hexadécimal) identifient la carte : ils sont retirés
bool IsDumpLine(const std::string& text) {
    std::string s = text;
    const char* tag = "[LeechCore] ";
    size_t k = s.find(tag);
    if (k != std::string::npos) s = s.substr(k + strlen(tag));
    if (s.rfind("-----", 0) == 0) return true;
    if (s.size() < 8) return false;
    for (int i = 0; i < 4; i++) if (!isxdigit((unsigned char)s[(size_t)i])) return false;
    return s[4] == ' ' && s[5] == ' ';
}

std::string SupportZip() {
    typedef LONG(WINAPI * RtlVer)(OSVERSIONINFOW*);
    OSVERSIONINFOW os = { sizeof os };
    if (auto f = (RtlVer)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) f(&os);
    std::string sys = Fmt("Camra's Tool %s\r\nWindows %lu.%lu.%lu\r\n%s\r\n", APP_VERSION, os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber, NowStamp().c_str());
    DeviceInfo d = DevInfo();
    sys += Fmt("Mode %d, algorithme %d\r\nFPGA %s, firmware %s, %s, %s\r\n", g_set.mode, g_set.algo, d.fpga.c(), d.firmware.c(), d.usbLink.c(), d.pcieLink.c_str());
    std::string log;
    for (auto& l : LogCopy()) if (!IsDumpLine(l.text)) log += l.time + "  " + l.text + "\r\n";
    std::vector<std::pair<std::string, std::string>> files = { { "systeme.txt", sys }, { "journal.txt", Anon(log) } };
    if (DiagCopy().done) files.push_back({ "diagnostic.txt", DiagReport() });
    BenchState b = BenchCopy();
    if (b.hasResult) files.push_back({ "resultats.json", ResultsJson(b) });
    std::string s;
    if (ReadFileAll(ReportDir() + L"\\plantage.txt", s)) files.push_back({ "plantage.txt", s });
    return MakeZip(files);
}

void ReportProblem() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::wstring p = ReportDir() + L"\\" + U2W(Fmt("camras_tool_rapport_%04d%02d%02d_%02d%02d%02d.zip", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond));
    std::string z = SupportZip();
    if (!WriteFileAll(p, z.data(), z.size())) { Toast(T("Impossible d'écrire le fichier.", "Cannot write the file."), true); return; }
    Toast(T("Rapport créé : envoyez ce zip sur Discord. Il ne contient pas votre nom d'utilisateur.", "Report created: send this zip on Discord. It does not contain your user name."));
    ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + p + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}
