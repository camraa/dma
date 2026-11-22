// bench.h - speedtest : 8 mesures, test de stabilité, résultats, historique
#pragma once
#include "app.h"

enum { K_READ, K_SCATTER };
struct TestDef {
    const char* key;
    const char* nameFr; const char* nameEn;
    const char* descFr; const char* descEn;
    const char* helpFr; const char* helpEn;
    int kind; uint32_t size; int pages;
};
extern const TestDef TESTS[8];

struct TestResult {
    bool ran = false, done = false;
    double mbps = 0, callsPerSec = 0, latAvg = 0, latMin = 0, latP50 = 0, latP99 = 0, latMax = 0, success = 100;
    u64 calls = 0, errors = 0;
};

// Une seconde du test de stabilité
struct StabSample { float mbps; float lat; uint32_t failed; uint8_t dip, drop; };

struct BenchState {
    bool running = false, stopping = false, stability = false, finished = false, stopped = false;
    int mode = TM_FULL;
    std::vector<int> order;                  // tests à exécuter
    int current = -1;                        // index dans TESTS
    int doneCount = 0;
    double progress = 0;                     // 0..1 du test en cours
    double liveLat = 0, liveMbps = 0;
    TestResult res[8];
    std::vector<float> curve;                // courbe du test en cours
    int curveTest = -1;
    // Stabilité
    double stabTotal = 0, stabElapsed = 0;
    std::vector<StabSample> stab;
    double sAvg = 0, sMin = 0, sMax = 0, sSuccess = 100;
    u64 sFailed = 0, sPages = 0;
    int sDips = 0, sDrops = 0;
    // Contexte du dernier test
    bool hasResult = false, simulated = false;
    std::string date, devString;
    Tx zone, fpga, fw, pcie, maxMem, usb, ram;
    u64 zoneBase = 0, zoneMb = 0;
    double duration = 0;
};

BenchState BenchCopy();
std::vector<int> TestsForMode(int mode);
bool BenchStart(int mode);
void BenchStop();

// Verdicts
enum { R_EXCELLENT, R_GOOD, R_AVERAGE, R_POOR, R_NONE };
int RateMbps(double mbps);
int RateLat(double us);
const char* RatingText(int r);
int StabVerdict(const BenchState& b);         // 0 stable, 1 moyen, 2 instable
const char* StabVerdictText(int v);
double BestMbps(const BenchState& b, int* which = nullptr);
double Stability(const BenchState& b, u64* failed = nullptr);

// Exports
std::string ResultsJson(const BenchState& b);
std::string ResultsCsv(const BenchState& b);
std::string ResultsDiscord(const BenchState& b);

// Historique (CamrasTool_history.txt)
struct HistEntry {
    std::string date;
    int kind = 0;                            // 0 = speedtest, 1 = stabilité
    int mode = 0;
    bool sim = false;
    std::string fpga, fw, usb, ram, device, note;
    double maxMbps = 0, latAvg = 0, success = 100;
    bool ran[8] = {};
    double mbps[8] = {}, lat[8] = {}, succ[8] = {};
    double stabMin = 0, sAvg = 0, sMin = 0, sMax = 0, sSucc = 100;
    u64 sFailed = 0;
    int sDips = 0, sDrops = 0;
};
std::vector<HistEntry> HistoryCopy();
void HistoryLoad();
void HistoryAdd(const HistEntry& e);
void HistoryDelete(int i);
void HistorySetNote(int i, const std::string& note);
