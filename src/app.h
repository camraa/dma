// app.h - déclarations communes de Camra's Tool
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>

#define APP_NAME    "Camra's Tool"
#ifndef APP_VERSION
#define APP_VERSION "1.4.2"
#endif
#define APP_AUTHOR  "1sfk"

typedef uint64_t u64;

// ---------------------------------------------------------------------------
//  Langue : chaque texte existe en français et en anglais
// ---------------------------------------------------------------------------
extern bool g_en;
inline const char* T(const char* fr, const char* en) { return g_en ? en : fr; }

// Texte bilingue conservé tel quel (diagnostic, résultats) : suit la langue choisie
struct Tx {
    std::string fr, en;
    const char* c() const { return g_en ? en.c_str() : fr.c_str(); }
    const std::string& s() const { return g_en ? en : fr; }
};
std::string Fmt(const char* fmt, ...);
#define TX(fr, en, ...) Tx{ Fmt(fr, ##__VA_ARGS__), Fmt(en, ##__VA_ARGS__) }

// ---------------------------------------------------------------------------
//  Texte
// ---------------------------------------------------------------------------
std::string W2U(const std::wstring& w);
std::wstring U2W(const std::string& s);
std::string Num(double v, int dec);          // 1 234,5 (FR) / 1,234.5 (EN)
std::string Pl(long long n);                 // "s" si pluriel
std::string Lower(std::string s);
std::wstring LowerW(std::wstring s);
std::string FileName(const std::wstring& path);
std::string Hex(u64 v);                      // 0x10000000

// ---------------------------------------------------------------------------
//  Chemins
// ---------------------------------------------------------------------------
std::wstring ExeDir();
std::wstring LocalDir();                     // %LOCALAPPDATA%\CamrasTool
std::wstring DataDir();                      // réglages + historique (à côté de l'exe, sinon LocalDir)
bool FileExists(const std::wstring& p);
bool DirExists(const std::wstring& p);
bool MakeDirs(const std::wstring& p);
bool ReadFileAll(const std::wstring& p, std::string& out);
bool WriteFileAll(const std::wstring& p, const void* data, size_t n);
std::string NowStamp();                      // 2026-11-22 19:45:01
double NowSec();                             // horloge haute précision (s)

// ---------------------------------------------------------------------------
//  Journal (onglet Journal)
// ---------------------------------------------------------------------------
enum { L_INFO, L_OK, L_WARN, L_ERR, L_LC };
struct LogLine { std::string time, text; int lvl; };
void Log(int lvl, const char* fmt, ...);
std::vector<LogLine> LogCopy();
void LogClear();

// ---------------------------------------------------------------------------
//  Notifications et presse-papiers
// ---------------------------------------------------------------------------
void Toast(const std::string& msg, bool err = false);
struct ToastMsg { std::string text; bool err; double t; };
ToastMsg ToastGet();
bool CopyToClipboard(const std::string& utf8);
void OpenUrl(const char* url);

// ---------------------------------------------------------------------------
//  Réglages (CamrasTool.ini, mêmes clés que la version 1.1)
// ---------------------------------------------------------------------------
enum { MODE_FPGA, MODE_SIM, MODE_CUSTOM };
enum { TM_FULL, TM_SPEED, TM_THROUGHPUT, TM_STABILITY, TM_CUSTOM };
struct Settings {
    std::wstring dll;
    int  mode = MODE_FPGA;
    char custom[260] = "fpga://algo=0";
    int  algo = 0;
    int  journal = 1;
    char base[32] = "0x10000000";
    int  sizeMb = 1024;
    int  duration = 3;
    int  testMode = TM_FULL;
    int  stabMin = 10;
    bool zoneAuto = true;
    bool release = true;                     // libère la carte après chaque opération
    bool updates = true;                     // cherche une nouvelle version au démarrage
    int  runs = 1;                           // passages par test (médiane gardée)
    bool notify = true;                      // son et clignotement à la fin d'un test
    bool guideDone = false;                  // assistant déjà vu
    bool fwBackup = true;                    // sauvegarde la flash avant de l'écrire
    bool tests[8] = { true, true, true, true, true, true, true, true };
    bool dark = true;
    bool english = false;
    char fwModel[32] = "xc7a35t";
    int  fwType = 1;                         // 0 = BIT (RAM), 1 = BIN (flash)
    char fwClock[32] = "10000000";
    std::wstring fwFile;
};
extern Settings g_set;
void LoadSettings();
void SaveSettings();

// ---------------------------------------------------------------------------
//  Navigation
// ---------------------------------------------------------------------------
enum { PG_SPEED, PG_RESULTS, PG_HISTORY, PG_DEVICE, PG_FIRMWARE, PG_DIAG, PG_LOG, PG_SETTINGS, PG_COUNT };
extern int  g_page;
extern HWND g_hwnd;

// Mise à jour : dernière version publiée sur GitHub (vide si à jour ou inconnue)
void UpdateCheckAsync();                     // au démarrage, puis toutes les 2 heures
void UpdateCheckNow();                       // bouton des Réglages
std::string UpdateNotes();                   // nouveautés de la version disponible
std::string UpdateAvailable();
bool UpdateBusy();
int UpdatePct();                              // avancement du téléchargement, -1 si inconnu
void UpdateInstallAsync();
void UpdateCleanup();
bool IsNewer(const std::string& a, const std::string& b);
#define APP_ASSET      "Camras-Tool.exe"
#define LAUNCHER_ASSET "Camras-Tool-Lanceur.exe"
bool ParseRelease(const std::string& body, std::string& ver, std::string& url, std::string& sha, const char* asset = APP_ASSET);
bool FetchRelease(std::string& body);
std::string JsonText(const std::string& b, const char* key);
int SwapExe(const std::wstring& path, const std::string& data);   // 0 ok, 1 écriture impossible, 2 remplacement impossible
std::string Sha256(const std::string& d);

// Support : plantage, signalement, tests
std::wstring ReportDir();
void CrashInit();
void ReportProblem();
std::string SupportZip();
uint32_t Crc32(const std::string& s);
std::string MakeZip(const std::vector<std::pair<std::string, std::string>>& files);
bool IsDumpLine(const std::string& text);
int RunTests();
void DrawModals();
extern bool g_openGuide, g_openCable;
#define RELEASES_URL "https://github.com/camraa/dma/releases/latest"

// Une seule opération sur la carte à la fois (connexion, test, diagnostic)
bool StartJob(std::function<void()> fn);
bool JobRunning();

// ---------------------------------------------------------------------------
//  Ressources intégrées (lib + kit de flashage)
// ---------------------------------------------------------------------------
std::wstring BuiltinLibDir();                // %LOCALAPPDATA%\CamrasTool\lib
bool ExtractBuiltinLibs();                   // leechcore.dll, FTD3XX.dll, vcruntime140.dll
void KitEnsureAsync();                       // OpenOCD, bitstreams, pilotes -> ...\flash
bool ExtractKitPrefix(const std::wstring& prefix, const std::wstring& dir);
bool KitReady();
std::wstring KitDir();                       // %LOCALAPPDATA%\CamrasTool\flash

// ---------------------------------------------------------------------------
//  Carte DMA (LeechCore)
// ---------------------------------------------------------------------------
struct Range { u64 base, size; };
struct DeviceInfo {
    bool connected = false, simulated = false;
    bool released = false;                   // infos connues, mais carte rendue aux autres logiciels
    std::string device;                      // chaîne LeechCore
    Tx fpga, firmware, pcie, maxMem;
    u64 fpgaId = 0, paMax = 0;
    Tx usbLink;                              // USB 3.0 · 5 Gb/s
    bool usb2 = false;
    std::string pcieLink;                    // gen2 x1
    bool ramProbed = false;
    u64 lowTop = 0, highTop = 0, ramBytes = 0;
    std::vector<Range> ranges;               // zones lisibles (zones protégées exclues)
    u64 excluded = 0;                        // octets exclus car illisibles en permanence
};
enum { CS_IDLE, CS_CONNECTING, CS_CONNECTED, CS_FAILED, CS_RELEASED };
extern std::atomic<int> g_connState;
DeviceInfo DevInfo();
Tx DevError();
std::wstring ResolveDll();                   // réglage, sinon à côté de l'exe, sinon DLL intégrée
std::string DevDeviceString();
extern std::atomic<int> g_algoOverride;      // comparaison des algorithmes : 1..4, 0 = réglage
bool DevConnect(bool veryVerbose);           // bloquant : à appeler dans un job
void DevDisconnect();
void DevRelease();                           // ferme la carte si « Libérer la carte » est actif
void DevMonitorStart();                      // détecte le débranchement et le rebranchement
bool DevOpen();
bool DevRead(u64 pa, uint32_t cb, uint8_t* pb, double* us);
int  DevReadScatter(int n, const u64* pas, double* us, uint8_t* copyTo = nullptr);
std::vector<Range> TestRanges(Tx* err);      // zone auto ou manuelle
bool ParseHex(const char* s, u64* v);
std::vector<std::string> LcMessages();
void LcMessagesClear();
Tx RamText(const DeviceInfo& d);             // 16 Go
Tx ZoneText(const std::vector<Range>& r);    // 0x10000000 + 1024 Mo
void ConnectAsync(bool diagOnFail);

// ---------------------------------------------------------------------------
//  Système : USB, pilotes, processus
// ---------------------------------------------------------------------------
struct UsbDev {
    std::wstring id;
    std::string name;
    bool present = false;
    ULONG problem = 0;
    int speed = -1;                          // 0 low, 1 full, 2 high, 3 super
    int port = 0;
    int pid = 0;
};
std::vector<UsbDev> FindUsb(const wchar_t* vidpid, bool includeAbsent, bool interfaces);
std::vector<UsbDev> FindFt60x(bool includeAbsent);
UsbDev CardUsb();                            // dernier état vu par la surveillance
std::string ParsePcieLink(const std::string& line);
Tx UsbSpeedText(int speed);
struct DriverState { bool active = false, staged = false; std::string version, service, inf; };
DriverState FtdiDriver();
DriverState Ch341Driver();
std::vector<std::string> ProcessesWithModule(const wchar_t* dll);
struct Ch347 { bool present = false; int pid = 0; ULONG problem = 0; std::string name; };
Ch347 FindCh347();
int  PeArch(const std::wstring& path);       // 64, 32, 0 = pas une DLL valide, -1 = illisible
std::string FileVersion(const std::wstring& path);
void InstallDriverAsync(int which);          // 0 = FTDI D3XX, 1 = WCH CH341/CH347
bool DriverInstalling();
int  RunDriverInstall(int which);             // processus élevé : --install-driver=N
