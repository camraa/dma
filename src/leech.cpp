// leech.cpp - carte DMA via LeechCore (chargée dynamiquement) + carte virtuelle de simulation
#include "app.h"
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <random>
#include <thread>
#include <algorithm>

// ---------------------------------------------------------------------------
//  API LeechCore 2.x (leechcore.h)
// ---------------------------------------------------------------------------
#define LC_CONFIG_VERSION           0xc0fd0002
#define MEM_SCATTER_VERSION         0xc0fe0002
#define LC_CONFIG_PRINTF_ENABLED    0x01
#define LC_CONFIG_PRINTF_V          0x02
#define LC_CONFIG_PRINTF_VV         0x04
#define LC_OPT_CORE_ADDR_MAX        0x1000000800000000ULL
#define LC_OPT_FPGA_DEVICE_ID       0x0300008000000000ULL
#define LC_OPT_FPGA_FPGA_ID         0x0300008100000000ULL
#define LC_OPT_FPGA_VERSION_MAJOR   0x0300008200000000ULL
#define LC_OPT_FPGA_VERSION_MINOR   0x0300008300000000ULL

typedef int (*PFN_PRINTF)(const char* fmt, ...);
typedef struct {
    DWORD dwVersion;
    DWORD dwPrintfVerbosity;
    CHAR szDevice[MAX_PATH];
    CHAR szRemote[MAX_PATH];
    PFN_PRINTF pfn_printf_opt;
    u64 paMax;
    BOOL fVolatile;
    BOOL fWritable;
    BOOL fRemote;
    BOOL fRemoteDisableCompress;
    CHAR szDeviceName[MAX_PATH];
} LC_CONFIG;

typedef struct {
    DWORD version;
    BOOL f;
    u64 qwA;
    union { PBYTE pb; u64 _Filler; };
    DWORD cb;
    DWORD iStack;
    u64 vStack[12];
} MEM_SCATTER, *PMEM_SCATTER, **PPMEM_SCATTER;

typedef HANDLE (*PFN_LcCreate)(LC_CONFIG*);
typedef VOID (*PFN_LcClose)(HANDLE);
typedef VOID (*PFN_LcMemFree)(PVOID);
typedef BOOL (*PFN_LcRead)(HANDLE, u64, DWORD, PBYTE);
typedef BOOL (*PFN_LcAllocScatter1)(DWORD, PPMEM_SCATTER*);
typedef VOID (*PFN_LcReadScatter)(HANDLE, DWORD, PPMEM_SCATTER);
typedef BOOL (*PFN_LcGetOption)(HANDLE, u64, u64*);

static struct {
    HMODULE mod = nullptr;
    std::wstring path;
    PFN_LcCreate Create; PFN_LcClose Close; PFN_LcMemFree MemFree; PFN_LcRead Read;
    PFN_LcAllocScatter1 AllocScatter1; PFN_LcReadScatter ReadScatter; PFN_LcGetOption GetOption;
} LC;

static std::mutex g_devMx;
static DeviceInfo g_dev;
static Tx g_devErr;
static HANDLE g_h = nullptr;
static PPMEM_SCATTER g_scatter = nullptr;      // 1024 pages allouées par LeechCore
static const int SCATTER_MAX = 1024;
std::atomic<int> g_connState{ CS_IDLE };
static std::atomic<bool> g_sim{ false };          // lu à chaque lecture : pas de verrou

DeviceInfo DevInfo() { std::lock_guard<std::mutex> l(g_devMx); return g_dev; }
Tx DevError() { std::lock_guard<std::mutex> l(g_devMx); return g_devErr; }
bool DevOpen() { return g_h != nullptr || g_sim; }

// ---------------------------------------------------------------------------
//  Messages LeechCore (journal + diagnostic)
// ---------------------------------------------------------------------------
static std::mutex g_lcMx;
static std::vector<std::string> g_lcMsgs;
static std::string g_lcPartial;

std::vector<std::string> LcMessages() { std::lock_guard<std::mutex> l(g_lcMx); return g_lcMsgs; }
void LcMessagesClear() { std::lock_guard<std::mutex> l(g_lcMx); g_lcMsgs.clear(); }

static int LcPrintf(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> l(g_lcMx);
    g_lcPartial += buf;
    size_t k;
    while ((k = g_lcPartial.find('\n')) != std::string::npos) {
        std::string line = g_lcPartial.substr(0, k);
        g_lcPartial.erase(0, k + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        if (g_lcMsgs.size() < 400) g_lcMsgs.push_back(line);
        if (g_set.journal > 0) Log(L_LC, "[LeechCore] %s", line.c_str());
    }
    return n;
}

// ---------------------------------------------------------------------------
//  Chemin de leechcore.dll
// ---------------------------------------------------------------------------
std::wstring ResolveDll() {
    if (!g_set.dll.empty() && FileExists(g_set.dll)) return g_set.dll;
    std::wstring nextTo = ExeDir() + L"\\leechcore.dll";
    if (FileExists(nextTo)) return nextTo;
    std::wstring builtin = BuiltinLibDir() + L"\\leechcore.dll";
    if (FileExists(builtin)) return builtin;
    return g_set.dll;
}

std::atomic<int> g_algoOverride{ 0 };

std::string DevDeviceString() {
    if (g_set.mode == MODE_CUSTOM) return g_set.custom;
    // Réglage : 0 = auto, 1..4 ; LeechCore : 0 async normal, 1 async tiny, 2 old normal, 3 old tiny
    int algo = g_algoOverride ? g_algoOverride.load() : g_set.algo;
    return algo ? Fmt("fpga://algo=%d", algo - 1) : std::string("fpga");
}

static bool LoadLc(const std::wstring& path, Tx& err) {
    if (LC.mod && LC.path == LowerW(path)) return true;
    if (path.empty() || !FileExists(path)) {
        err = TX("leechcore.dll introuvable :\n%s", "leechcore.dll not found:\n%s", W2U(path).c_str());
        return false;
    }
    HMODULE m = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) {
        DWORD e = GetLastError();
        err = TX("Impossible de charger leechcore.dll (erreur %lu).\nVérifiez qu'il s'agit d'une DLL 64 bits.",
                 "Cannot load leechcore.dll (error %lu).\nMake sure it is a 64-bit DLL.", e);
        return false;
    }
    struct { const char* name; void** fn; } fns[] = {
        { "LcCreate", (void**)&LC.Create }, { "LcClose", (void**)&LC.Close }, { "LcMemFree", (void**)&LC.MemFree },
        { "LcRead", (void**)&LC.Read }, { "LcAllocScatter1", (void**)&LC.AllocScatter1 },
        { "LcReadScatter", (void**)&LC.ReadScatter }, { "LcGetOption", (void**)&LC.GetOption } };
    for (auto& f : fns) {
        *f.fn = (void*)GetProcAddress(m, f.name);
        if (!*f.fn) {
            err = TX("Fonction %s absente de leechcore.dll.", "Function %s missing from leechcore.dll.", f.name);
            return false;
        }
    }
    LC.mod = m;
    LC.path = LowerW(path);
    return true;
}

static const char* FpgaName(u64 id) {
    static const char* names[] = { "SP605 / FT601", "PCIeScreamer R1", "AC701 / FT601", "PCIeScreamer R2", "ScreamerM2",
                                   "NeTV2 RawUDP", nullptr, nullptr, "FT2232H #1", "Enigma X1", "Enigma X1 (FutureUse)", "PCIeSquirrel" };
    return id < sizeof names / sizeof names[0] ? names[id] : nullptr;
}

Tx RamText(const DeviceInfo& d) {
    if (!d.ramBytes) return TX("inconnue", "unknown");
    double gb = d.ramBytes / 1073741824.0;
    return TX("%s Go", "%s GB", Fmt(gb < 10 ? "%.1f" : "%.0f", gb).c_str());
}

static Tx MaxText(u64 pa) {
    if (!pa) return TX("inconnue", "unknown");
    return Tx{ Fmt("0x%llX (%s Go)", (unsigned long long)pa, Fmt("%.1f", pa / 1073741824.0).c_str()),
               Fmt("0x%llX (%.1f GB)", (unsigned long long)pa, pa / 1073741824.0) };
}

// ---------------------------------------------------------------------------
//  Simulation : carte FT601 virtuelle (mesures fictives, temps réel respecté)
// ---------------------------------------------------------------------------
static std::mt19937_64 g_rng{ 12345 };
static double g_simDebtUs = 0;

static double SimLatency(double us) {
    std::uniform_real_distribution<double> j(0.92, 1.12);
    double v = us * j(g_rng);
    g_simDebtUs += v;
    if (g_simDebtUs >= 2000) {                 // rattrape le temps simulé par tranches de 2 ms
        double t0 = NowSec();
        Sleep((DWORD)(g_simDebtUs / 1000));
        g_simDebtUs -= (NowSec() - t0) * 1e6;
    }
    return v;
}

static bool SimFail() { return std::uniform_int_distribution<int>(0, 49999)(g_rng) == 0; }

// ---------------------------------------------------------------------------
//  Lectures
// ---------------------------------------------------------------------------
bool DevRead(u64 pa, uint32_t cb, uint8_t* pb, double* us) {
    if (g_sim) {
        *us = SimLatency(48 + cb / 182.0);
        return !SimFail();
    }
    if (!g_h) return false;
    double t0 = NowSec();
    BOOL ok = LC.Read(g_h, pa, cb, pb);
    *us = (NowSec() - t0) * 1e6;
    return ok != FALSE;
}

int DevReadScatter(int n, const u64* pas, double* us, uint8_t* copyTo) {
    n = std::min(n, SCATTER_MAX);
    if (g_sim) {
        *us = SimLatency(75 + n * 4096 / 168.0);
        int ok = n;
        for (int i = 0; i < n; i++) if (SimFail()) ok--;
        return ok;
    }
    if (!g_h || !g_scatter) return 0;
    for (int i = 0; i < n; i++) {
        g_scatter[i]->qwA = pas[i] & ~0xFFFULL;
        g_scatter[i]->cb = 0x1000;
        g_scatter[i]->f = FALSE;
    }
    double t0 = NowSec();
    LC.ReadScatter(g_h, (DWORD)n, g_scatter);
    *us = (NowSec() - t0) * 1e6;
    int ok = 0;
    for (int i = 0; i < n; i++) {
        if (g_scatter[i]->f) {
            ok++;
            if (copyTo) memcpy(copyTo + (size_t)i * 0x1000, g_scatter[i]->pb, 0x1000);
        }
    }
    return ok;
}

// Une adresse est lisible si au moins une des deux pages testées revient
static bool Readable(u64 pa) {
    u64 pas[2] = { pa, pa + 0x100000 };
    double us;
    return DevReadScatter(2, pas, &us) > 0;
}

// Cartographie [lo, hi) et garde les blocs lisibles. Une zone est exclue seulement si elle échoue 4 fois
// de suite : ce sont des zones protégées du PC cible, pas des erreurs du lien (qui restent comptées).
static void MapReadable(u64 lo, u64 hi, u64 step, DeviceInfo& d) {
    if (hi <= lo) return;
    std::vector<u64> todo;
    for (u64 a = lo; a < hi; a += step) todo.push_back(a);
    std::vector<char> ok(todo.size(), 0);
    std::vector<size_t> left(todo.size());
    for (size_t i = 0; i < left.size(); i++) left[i] = i;
    for (int pass = 0; pass < 4 && !left.empty(); pass++) {
        std::vector<size_t> next;
        for (size_t i = 0; i < left.size(); i += 256) {
            size_t n = std::min<size_t>(256, left.size() - i);
            u64 pas[256];
            double us;
            for (size_t k = 0; k < n; k++) pas[k] = todo[left[i + k]];
            DevReadScatter((int)n, pas, &us);
            for (size_t k = 0; k < n; k++) {
                if (g_scatter[k]->f) ok[left[i + k]] = 1;
                else next.push_back(left[i + k]);
            }
        }
        left.swap(next);
    }
    // Marge : un bloc voisin d'une zone illisible est écarté aussi
    size_t n = todo.size();
    u64 start = 0;
    bool open = false;
    auto close = [&](u64 end) {
        if (open && end - start >= (16ULL << 20)) d.ranges.push_back({ start, end - start });
        open = false;
    };
    for (size_t i = 0; i < n; i++) {
        bool good = ok[i] && (i == 0 || ok[i - 1]) && (i + 1 == n || ok[i + 1]);
        if (good && !open) { start = todo[i]; open = true; }
        if (!good) close(todo[i]);
    }
    close(hi);
}

// Sonde la RAM du PC cible en lecture seule, en évitant la zone des périphériques entre 2 et 4 Go
static void ProbeRam(DeviceInfo& d) {
    const u64 MB16 = 16ULL << 20, G2 = 0x80000000ULL, G4 = 0x100000000ULL;
    const u64 base = 0x10000000ULL;
    d.lowTop = d.highTop = d.ramBytes = 0;
    if (!Readable(base)) return;
    u64 lo = base, hi = G2;
    if (Readable(G2 - MB16 * 2)) lo = hi;
    while (hi - lo > MB16) {
        u64 mid = ((lo + hi) / 2) & ~(MB16 - 1);
        if (Readable(mid)) lo = mid; else hi = mid;
    }
    d.lowTop = hi;
    u64 limit = d.paMax ? std::min<u64>(d.paMax + 1, 1ULL << 40) : (1ULL << 37);
    if (limit > G4 + MB16 && Readable(G4 + MB16)) {
        lo = G4 + MB16; hi = limit & ~(MB16 - 1);
        if (Readable(hi - MB16 * 2)) lo = hi;
        while (hi - lo > MB16) {
            u64 mid = ((lo + hi) / 2) & ~(MB16 - 1);
            if (Readable(mid)) lo = mid; else hi = mid;
        }
        d.highTop = hi;
    }
    // Zones lisibles : pas de 512 Ko, plus large au-delà de 32 Go pour rester rapide
    u64 span = (d.lowTop - base) + (d.highTop > G4 ? d.highTop - G4 : 0);
    u64 step = 512ULL << 10;
    while (span / step > 65536) step *= 2;
    MapReadable(base, d.lowTop, step, d);
    if (d.highTop > G4) MapReadable(G4, d.highTop, step, d);
    u64 kept = 0;
    for (auto& r : d.ranges) kept += r.size;
    d.excluded = span - kept;
    // Estimation de la RAM installée : sous 4 Go environ 3,25 Go sont utilisables
    double gb;
    if (d.highTop > G4) gb = (d.highTop - G4) / 1073741824.0 + 3.25;
    else gb = d.lowTop / 1073741824.0;
    static const double sizes[] = { 1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024 };
    double best = sizes[0];
    for (double s : sizes) if (std::fabs(s - gb) < std::fabs(best - gb)) best = s;
    d.ramBytes = (u64)(best * 1073741824.0);
    d.ramProbed = true;
    Log(L_INFO, T("RAM du PC cible : %s (vérifiée jusqu'à 0x%llX sous 2 Go, 0x%llX au-dessus de 4 Go).",
                  "Target PC RAM: %s (verified up to 0x%llX below 2 GB, 0x%llX above 4 GB)."),
        RamText(d).c(), (unsigned long long)d.lowTop, (unsigned long long)d.highTop);
    if (d.excluded)
        Log(L_INFO, T("Zones illisibles du PC cible exclues du test : %s Mo (mémoire protégée, sans rapport avec le lien).",
                      "Unreadable target PC areas excluded from the test: %s MB (protected memory, unrelated to the link)."),
            Num((double)(d.excluded >> 20), 0).c_str());
}

// ---------------------------------------------------------------------------
//  Connexion
// ---------------------------------------------------------------------------
bool DevConnect(bool veryVerbose) {
    DevDisconnect();
    g_connState = CS_CONNECTING;
    DeviceInfo d;
    if (g_set.mode == MODE_SIM) {
        d.connected = d.simulated = true;
        d.device = "simulation";
        d.fpga = TX("Carte virtuelle (FT601)", "Virtual card (FT601)");
        d.firmware = Tx{ "v4.14", "v4.14" };
        d.pcie = Tx{ "04:00.0", "04:00.0" };
        d.paMax = 0x47FFFFFFFULL;
        d.maxMem = MaxText(d.paMax);
        d.usbLink = UsbSpeedText(3);
        d.pcieLink = "gen2 x1";
        d.lowTop = 0x80000000ULL; d.highTop = 0x480000000ULL; d.ramBytes = 16ULL << 30; d.ramProbed = true;
        { std::lock_guard<std::mutex> l(g_devMx); g_dev = d; g_devErr = {}; }
        g_sim = true;
        Log(L_WARN, "%s", T("Mode simulation : aucune carte réelle.", "Simulation mode: no real card."));
        g_connState = CS_CONNECTED;
        return true;
    }

    std::string dev = DevDeviceString();
    Tx err;
    std::wstring dll = ResolveDll();
    auto fail = [&](const Tx& e) {
        std::lock_guard<std::mutex> l(g_devMx);
        g_dev = DeviceInfo();
        g_devErr = e;
        g_connState = CS_FAILED;
        Log(L_ERR, T("ÉCHEC : %s", "FAILED: %s"), e.c());
        return false;
    };
    Log(L_INFO, T("Connexion à « %s »…", "Connecting to \"%s\"…"), dev.c_str());
    if (!LoadLc(dll, err)) return fail(err);
    Log(L_INFO, T("Bibliothèque : %s", "Library: %s"), W2U(dll).c_str());

    LC_CONFIG cfg = {};
    cfg.dwVersion = LC_CONFIG_VERSION;
    cfg.dwPrintfVerbosity = LC_CONFIG_PRINTF_ENABLED | LC_CONFIG_PRINTF_V;   // V : donne le lien PCIe (le Journal filtre)
    if (g_set.journal >= 2 || veryVerbose) cfg.dwPrintfVerbosity |= LC_CONFIG_PRINTF_VV;
    strncpy_s(cfg.szDevice, dev.c_str(), _TRUNCATE);
    cfg.pfn_printf_opt = LcPrintf;
    HANDLE h = LC.Create(&cfg);
    if (!h) {
        return fail(TX("Impossible d'ouvrir le périphérique « %s ».\nVérifiez que la carte est branchée sur un port USB 3, que le PC cible est allumé et que FTD3XX.dll se trouve à côté de leechcore.dll.",
                       "Cannot open device \"%s\".\nMake sure the card is plugged into a USB 3 port, the target PC is on and FTD3XX.dll is next to leechcore.dll.",
                       dev.c_str()));
    }
    PPMEM_SCATTER sc = nullptr;
    if (!LC.AllocScatter1(SCATTER_MAX, &sc)) {
        LC.Close(h);
        return fail(TX("Mémoire insuffisante.", "Out of memory."));
    }
    for (int i = 0; i < SCATTER_MAX; i++) sc[i]->version = MEM_SCATTER_VERSION;
    g_h = h;
    g_scatter = sc;

    d.connected = true;
    d.device = dev;
    u64 v = 0, maj = 0, min = 0, id = 0;
    bool isFpga = Lower(cfg.szDeviceName).find("fpga") != std::string::npos || Lower(dev).rfind("fpga", 0) == 0;
    if (isFpga && LC.GetOption(h, LC_OPT_FPGA_FPGA_ID, &v)) {
        d.fpgaId = v;
        const char* n = FpgaName(v);
        d.fpga = n ? Tx{ Fmt("%s (id %llu)", n, v), Fmt("%s (id %llu)", n, v) }
                   : TX("inconnue (id %llu)", "unknown (id %llu)", v);
    } else d.fpga = Tx{ cfg.szDeviceName, cfg.szDeviceName };
    if (isFpga && LC.GetOption(h, LC_OPT_FPGA_VERSION_MAJOR, &maj) && LC.GetOption(h, LC_OPT_FPGA_VERSION_MINOR, &min))
        d.firmware = Tx{ Fmt("v%llu.%llu", maj, min), Fmt("v%llu.%llu", maj, min) };
    else d.firmware = TX("inconnue", "unknown");
    if (isFpga && LC.GetOption(h, LC_OPT_FPGA_DEVICE_ID, &id) && id)
        d.pcie = Tx{ Fmt("%02llX:%02llX.%llu", (id >> 8) & 0xFF, (id >> 3) & 0x1F, id & 7),
                     Fmt("%02llX:%02llX.%llu", (id >> 8) & 0xFF, (id >> 3) & 0x1F, id & 7) };
    else d.pcie = TX("inconnue", "unknown");
    d.paMax = cfg.paMax;
    if (!d.paMax) LC.GetOption(h, LC_OPT_CORE_ADDR_MAX, &d.paMax);
    d.maxMem = MaxText(d.paMax);

    for (auto& m : LcMessages()) {
        std::string l = ParsePcieLink(m);
        if (!l.empty()) d.pcieLink = l;
    }

    // Lien USB de la carte (FT601/FT600)
    for (auto& u : FindFt60x(false)) {
        if (u.speed >= 0) { d.usbLink = UsbSpeedText(u.speed); d.usb2 = u.speed < 3; break; }
    }
    if (d.usbLink.fr.empty()) d.usbLink = Tx{ "USB ?", "USB ?" };

    { std::lock_guard<std::mutex> l(g_devMx); g_dev = d; g_devErr = {}; }
    if (g_set.zoneAuto && g_algoOverride <= 1) {   // la comparaison garde la première cartographie
        ProbeRam(d);
        std::lock_guard<std::mutex> l(g_devMx);
        g_dev = d;
    }
    g_connState = CS_CONNECTED;
    Log(L_OK, T("Connecté : %s · FPGA %s · firmware %s · PCIe %s · mémoire max %s",
                "Connected: %s · FPGA %s · firmware %s · PCIe %s · max memory %s"),
        dev.c_str(), d.fpga.c(), d.firmware.c(), d.pcie.c(), d.maxMem.c());
    if (d.usb2)
        Log(L_WARN, T("Carte branchée en %s : débit limité à environ 30 à 40 Mo/s. Utilisez un port et un câble USB 3.",
                      "Card connected at %s: throughput limited to about 30 to 40 MB/s. Use a USB 3 port and cable."), d.usbLink.c());
    return true;
}

void DevDisconnect() {
    bool was = false;
    {
        std::lock_guard<std::mutex> l(g_devMx);
        was = g_dev.connected;
        g_dev = DeviceInfo();
    }
    g_sim = false;
    if (g_h) {
        if (g_scatter) LC.MemFree(g_scatter);
        LC.Close(g_h);
        g_h = nullptr;
        g_scatter = nullptr;
    }
    g_connState = CS_IDLE;
    if (was) Log(L_INFO, "%s", T("Déconnecté.", "Disconnected."));
}

// Rend la carte aux autres logiciels en gardant les informations affichées
void DevRelease() {
    if (!g_set.release || !g_h) return;
    LC.MemFree(g_scatter);
    LC.Close(g_h);
    g_h = nullptr;
    g_scatter = nullptr;
    std::lock_guard<std::mutex> l(g_devMx);
    g_dev.connected = false;
    g_dev.released = true;
    g_connState = CS_RELEASED;
}

// « ... PCIe gen2 x1 ... » -> « gen2 x1 »
std::string ParsePcieLink(const std::string& line) {
    size_t k = line.find("PCIe gen");
    if (k == std::string::npos) return "";
    int g = 0, w = 0;
    if (sscanf_s(line.c_str() + k, "PCIe gen%d x%d", &g, &w) != 2) return "";
    return Fmt("gen%d x%d", g, w);
}

static std::mutex g_usbMx;
static UsbDev g_usb;
UsbDev CardUsb() { std::lock_guard<std::mutex> l(g_usbMx); return g_usb; }

static bool CardPresent() {
    UsbDev best;
    for (auto& u : FindFt60x(false)) if (!best.present || (best.problem && !u.problem)) best = u;
    std::lock_guard<std::mutex> l(g_usbMx);
    g_usb = best;
    return best.present && !best.problem;
}

// Surveille le branchement USB de la carte : débranchée, on ferme ; rebranchée, on reconnecte
void DevMonitorStart() {
    std::thread([] {
        bool was = CardPresent();
        for (;;) {
            Sleep(1500);
            bool now = CardPresent();
            if (g_set.mode != MODE_FPGA) { was = now; continue; }
            if (was && !now) {
                extern void BenchStop();
                Log(L_WARN, "%s", T("Carte débranchée.", "Card unplugged."));
                Toast(T("Carte débranchée.", "Card unplugged."), true);
                BenchStop();
                for (int i = 0; i < 200 && JobRunning(); i++) Sleep(50);
                StartJob([] {
                    DevDisconnect();
                    std::lock_guard<std::mutex> l(g_devMx);
                    g_devErr = TX("Carte débranchée.", "Card unplugged.");
                    g_connState = CS_FAILED;
                });
            } else if (!was && now) {
                Log(L_INFO, "%s", T("Carte branchée : reconnexion…", "Card plugged in: reconnecting…"));
                Sleep(2000);                 // laisse Windows activer le pilote
                for (int i = 0; i < 200 && JobRunning(); i++) Sleep(50);
                ConnectAsync(false);
            }
            was = now;
        }
    }).detach();
}

void ConnectAsync(bool diagOnFail) {
    extern void DiagStart();
    StartJob([diagOnFail] {
        LcMessagesClear();
        bool ok = DevConnect(false);
        if (ok) {
            DeviceInfo d = DevInfo();
            DevRelease();
            Toast(d.simulated ? T("Mode simulation : aucune carte réelle.", "Simulation mode: no real card.")
                              : Fmt(T("Carte connectée : %s", "Card connected: %s"), d.fpga.c()));
        } else if (diagOnFail) {
            Toast(T("Carte DMA introuvable — diagnostic en cours.", "DMA card not found — running diagnostics."), true);
            g_page = PG_DIAG;
            std::thread([] { while (JobRunning()) Sleep(20); DiagStart(); }).detach();
        }
    });
}

// ---------------------------------------------------------------------------
//  Zone mémoire testée
// ---------------------------------------------------------------------------
bool ParseHex(const char* s, u64* v) {
    while (*s == ' ') s++;
    char* end = nullptr;
    unsigned long long x = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? strtoull(s + 2, &end, 16) : strtoull(s, &end, 16);
    if (!end || end == s) return false;
    while (*end == ' ') end++;
    if (*end) return false;
    *v = x;
    return true;
}

std::vector<Range> TestRanges(Tx* err) {
    DeviceInfo d = DevInfo();
    std::vector<Range> r;
    if (g_set.zoneAuto && !d.ranges.empty()) return d.ranges;
    if (g_set.zoneAuto && d.ramProbed && d.lowTop > 0x10000000ULL) {
        r.push_back({ 0x10000000ULL, d.lowTop - 0x10000000ULL });
        if (d.highTop > 0x100000000ULL + (16ULL << 20)) r.push_back({ 0x100000000ULL, d.highTop - 0x100000000ULL });
        return r;
    }
    u64 base = 0;
    if (!ParseHex(g_set.base, &base)) {
        if (err) *err = TX("Adresse de base invalide (ex. 0x10000000).", "Invalid base address (e.g. 0x10000000).");
        return {};
    }
    if (g_set.sizeMb < 16) {
        if (err) *err = TX("La taille de la zone doit être d'au moins 16 Mo.", "The range size must be at least 16 MB.");
        return {};
    }
    u64 size = (u64)g_set.sizeMb << 20;
    if (d.connected && d.paMax && base + size > d.paMax + 1) {
        if (err) *err = TX("La zone 0x%llX + %llu Mo dépasse la mémoire du PC cible.", "Range 0x%llX + %llu MB exceeds the target PC's memory.",
                           (unsigned long long)base, (unsigned long long)g_set.sizeMb);
        return {};
    }
    r.push_back({ base & ~0xFFFULL, size });
    return r;
}

Tx ZoneText(const std::vector<Range>& r) {
    if (r.empty()) return Tx{ "-", "-" };
    u64 total = 0;
    for (auto& x : r) total += x.size;
    return TX("0x%llX + %llu Mo", "0x%llX + %llu MB", (unsigned long long)r[0].base, (unsigned long long)(total >> 20));
}
