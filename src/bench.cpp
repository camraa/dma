// bench.cpp - speedtest DMA : mesures, stabilité, verdicts, exports, historique
#include "bench.h"
#include <algorithm>
#include <random>
#include <thread>
#include <cmath>

const TestDef TESTS[8] = {
    { "lat8", "Latence", "Latency", "Lecture de 8 octets", "8-byte read",
      "Mesure le temps d'une toute petite lecture (8 octets). Plus c'est bas, plus la carte est réactive. Repère FT601 : 40 à 120 µs.",
      "Measures the time of a tiny read (8 bytes). Lower means a more responsive card. FT601 reference: 40 to 120 µs.", K_READ, 8, 0 },
    { "seq4k", "Séquentiel 4 Ko", "Sequential 4 KB", "Lecture contiguë, 4 Ko / appel", "Contiguous read, 4 KB / call",
      "Lit une page mémoire (4 Ko) à la fois : indique combien de pages par seconde la carte lit une par une.",
      "Reads one memory page (4 KB) at a time: shows how many pages per second the card reads one by one.", K_READ, 4096, 0 },
    { "seq64k", "Séquentiel 64 Ko", "Sequential 64 KB", "Lecture contiguë, 64 Ko / appel", "Contiguous read, 64 KB / call",
      "Lectures contiguës de 64 Ko : bon compromis entre latence et débit.",
      "64 KB contiguous reads: a good balance between latency and throughput.", K_READ, 65536, 0 },
    { "seq1m", "Séquentiel 1 Mo", "Sequential 1 MB", "Lecture contiguë, 1 Mo / appel", "Contiguous read, 1 MB / call",
      "Lectures contiguës de 1 Mo : mesure le débit soutenu de la liaison USB.",
      "1 MB contiguous reads: measures the sustained throughput of the USB link.", K_READ, 1u << 20, 0 },
    { "seq8m", "Séquentiel 8 Mo", "Sequential 8 MB", "Lecture contiguë, 8 Mo / appel", "Contiguous read, 8 MB / call",
      "Lectures de 8 Mo : débit maximal de la carte. Une carte FT601 en USB 3 atteint environ 150 à 200 Mo/s.",
      "8 MB reads: the card's maximum throughput. An FT601 card on USB 3 reaches about 150 to 200 MB/s.", K_READ, 8u << 20, 0 },
    { "sc64", "Scatter 64", "Scatter 64", "64 pages aléatoires / appel", "64 random pages / call",
      "Lit 64 pages éparpillées en un seul appel (lecture « scatter »), comme un logiciel DMA qui lit plusieurs objets à la fois.",
      "Reads 64 scattered pages in a single call (scatter read), like DMA software reading several objects at once.", K_SCATTER, 0, 64 },
    { "sc256", "Scatter 256", "Scatter 256", "256 pages aléatoires / appel", "256 random pages / call",
      "Scatter de 256 pages : charge typique d'un logiciel DMA qui lit beaucoup de données à chaque image.",
      "256-page scatter: typical load of DMA software reading lots of data every frame.", K_SCATTER, 0, 256 },
    { "sc1024", "Scatter 1024", "Scatter 1024", "1024 pages aléatoires / appel", "1024 random pages / call",
      "Scatter de 1024 pages : charge très lourde, révèle les limites de la carte et du firmware.",
      "1024-page scatter: very heavy load, reveals the limits of the card and firmware.", K_SCATTER, 0, 1024 },
};

static const double MB = 1048576.0;
static std::mutex g_bMx;
static BenchState g_b;
static std::atomic<bool> g_stop{ false };

BenchState BenchCopy() { std::lock_guard<std::mutex> l(g_bMx); return g_b; }

std::vector<int> TestsForMode(int mode) {
    switch (mode) {
    case TM_FULL: return { 0, 1, 2, 3, 4, 5, 6, 7 };
    case TM_SPEED: return { 0, 1, 5 };
    case TM_THROUGHPUT: return { 3, 4, 6, 7 };
    case TM_CUSTOM: {
        std::vector<int> v;
        for (int i = 0; i < 8; i++) if (g_set.tests[i]) v.push_back(i);
        return v;
    }
    }
    return {};
}

// ---------------------------------------------------------------------------
//  Adresses aléatoires dans la zone testée (lecture seule)
// ---------------------------------------------------------------------------
struct Picker {
    std::vector<Range> r;
    u64 total = 0;
    std::mt19937_64 rng{ 0xC0FFEE ^ (u64)GetTickCount64() };
    explicit Picker(const std::vector<Range>& ranges) : r(ranges) { for (auto& x : r) total += x.size; }
    u64 Pick(uint32_t cb) {
        u64 x = std::uniform_int_distribution<u64>(0, total - 1)(rng);
        for (auto& g : r) {
            if (x < g.size) {
                u64 span = g.size > cb ? g.size - cb : 1;
                u64 off = std::uniform_int_distribution<u64>(0, span - 1)(rng);
                off &= cb >= 0x1000 ? ~0xFFFULL : ~7ULL;
                return g.base + off;
            }
            x -= g.size;
        }
        return r[0].base;
    }
};

// ---------------------------------------------------------------------------
//  Une mesure
// ---------------------------------------------------------------------------
static void RunTest(int ti, Picker& pk, double dur) {
    const TestDef& t = TESTS[ti];
    std::vector<uint8_t> buf(t.kind == K_READ ? t.size : 8);
    std::vector<u64> pas((size_t)std::max(t.pages, 1));
    std::vector<float> lats;
    lats.reserve(1 << 16);
    u64 calls = 0, errs = 0, units = 0, okUnits = 0;
    double bytes = 0, busy = 0;
    double winBytes = 0, winBusy = 0, winLat = 0;
    int winCalls = 0;
    {
        std::lock_guard<std::mutex> l(g_bMx);
        g_b.current = ti;
        g_b.progress = 0;
        g_b.curve.clear();
        g_b.curveTest = ti;
        g_b.res[ti] = TestResult();
        g_b.res[ti].ran = true;
    }
    double t0 = NowSec(), lastSample = t0;
    while (!g_stop) {
        double now = NowSec();
        if (now - t0 >= dur) break;
        double us = 0;
        if (t.kind == K_READ) {
            bool ok = DevRead(pk.Pick(t.size), t.size, buf.data(), &us);
            units++;
            if (ok) { okUnits++; bytes += t.size; winBytes += t.size; } else errs++;
        } else {
            for (int i = 0; i < t.pages; i++) pas[(size_t)i] = pk.Pick(0x1000);
            int ok = DevReadScatter(t.pages, pas.data(), &us);
            units += (u64)t.pages;
            okUnits += (u64)ok;
            errs += (u64)(t.pages - ok);
            bytes += ok * 4096.0;
            winBytes += ok * 4096.0;
        }
        calls++;
        busy += us;
        winBusy += us;
        winLat += us;
        winCalls++;
        lats.push_back((float)us);
        if (now - lastSample >= 0.1 && winCalls > 0) {
            float v = ti == 0 ? (float)(winLat / winCalls) : (float)(winBusy > 0 ? winBytes / (winBusy / 1e6) / MB : 0);
            std::lock_guard<std::mutex> l(g_bMx);
            g_b.curve.push_back(v);
            g_b.progress = std::min(1.0, (now - t0) / dur);
            g_b.liveLat = winLat / winCalls;
            g_b.liveMbps = winBusy > 0 ? winBytes / (winBusy / 1e6) / MB : 0;
            TestResult& r = g_b.res[ti];
            r.calls = calls;
            r.mbps = busy > 0 ? bytes / (busy / 1e6) / MB : 0;
            r.latAvg = busy / calls;
            r.success = units ? 100.0 * okUnits / units : 100;
            winBytes = winBusy = winLat = 0;
            winCalls = 0;
            lastSample = now;
        }
    }
    TestResult r;
    r.ran = true;
    r.done = !g_stop;
    r.calls = calls;
    r.errors = errs;
    if (calls) {
        std::sort(lats.begin(), lats.end());
        r.latAvg = busy / calls;
        r.latMin = lats.front();
        r.latMax = lats.back();
        r.latP50 = lats[lats.size() / 2];
        r.latP99 = lats[std::min(lats.size() - 1, (size_t)(lats.size() * 0.99))];
        r.latP999 = lats[std::min(lats.size() - 1, (size_t)(lats.size() * 0.999))];
        for (float x : lats) if (x > 2 * r.latP50) r.spikes++;
        // Répartition entre le minimum et le 99,5e centile ; la dernière tranche regroupe tout le reste
        const int BINS = 40;
        double hi = lats[std::min(lats.size() - 1, (size_t)(lats.size() * 0.995))];
        r.histMin = r.latMin;
        r.histStep = std::max((hi - r.latMin) / (BINS - 1), 0.1);
        r.hist.assign(BINS, 0);
        for (float x : lats) r.hist[(size_t)std::min((double)(BINS - 1), (x - r.histMin) / r.histStep)] += 100.f / lats.size();
        r.mbps = busy > 0 ? bytes / (busy / 1e6) / MB : 0;
        r.callsPerSec = busy > 0 ? calls / (busy / 1e6) : 0;
        r.success = units ? 100.0 * okUnits / units : 0;
    }
    {
        std::lock_guard<std::mutex> l(g_bMx);
        g_b.res[ti] = r;
        g_b.progress = 1;
        if (r.done) g_b.doneCount++;
    }
    if (calls)
        Log(r.success < 99.5 ? L_WARN : L_INFO,
            T("%-18s %8.1f Mo/s  %9.0f appels/s  lat moy %6.1f µs  p99 %6.1f µs  succès %.1f %%",
              "%-18s %8.1f MB/s  %9.0f calls/s  avg lat %6.1f µs  p99 %6.1f µs  success %.1f %%"),
            g_en ? t.nameEn : t.nameFr, r.mbps, r.callsPerSec, r.latAvg, r.latP99, r.success);
}

// ---------------------------------------------------------------------------
//  Test de stabilité : scatter 256 pages en continu, échantillons d'une seconde
// ---------------------------------------------------------------------------
static void RunStability(Picker& pk, double total) {
    const int pages = 256;
    std::vector<u64> pas(pages);
    double t0 = NowSec(), secStart = t0;
    double secBytes = 0, secBusy = 0, secLat = 0;
    int secCalls = 0, secOk = 0;
    uint32_t secFailed = 0;
    u64 okPages = 0, failed = 0;
    std::vector<float> normal;                  // débits sans coupure, pour la référence « habituelle »
    bool inDrop = false;
    int dips = 0, drops = 0;
    double sum = 0, mn = 1e30, mx = 0;
    int n = 0;
    while (!g_stop) {
        double now = NowSec();
        if (now - t0 >= total) break;
        for (int i = 0; i < pages; i++) pas[(size_t)i] = pk.Pick(0x1000);
        double us = 0;
        int ok = DevReadScatter(pages, pas.data(), &us);
        secBytes += ok * 4096.0;
        secBusy += us;
        secLat += us;
        secCalls++;
        secOk += ok;
        secFailed += (uint32_t)(pages - ok);
        okPages += (u64)ok;
        failed += (u64)(pages - ok);
        now = NowSec();
        if (now - secStart >= 1.0) {
            double el = now - secStart;
            StabSample s;
            s.mbps = (float)(secBytes / el / MB);
            s.lat = (float)(secCalls ? secLat / secCalls : 0);
            s.failed = secFailed;
            s.drop = secOk == 0;
            s.dip = 0;
            int m = (int)((now - t0) / 60), sec = (int)(now - t0) % 60;
            if (s.drop) {
                if (!inDrop) {
                    drops++;
                    Log(L_ERR, T("Stabilité : COUPURE à %d:%02d — plus aucune lecture ne réussit.", "Stability: DROPOUT at %d:%02d — no read succeeds any more."), m, sec);
                }
                inDrop = true;
            } else {
                if (inDrop) Log(L_OK, T("Stabilité : lien rétabli à %d:%02d.", "Stability: link restored at %d:%02d."), m, sec);
                inDrop = false;
                if (normal.size() >= 5) {
                    std::vector<float> w(normal.end() - std::min<size_t>(normal.size(), 30), normal.end());
                    std::nth_element(w.begin(), w.begin() + w.size() / 2, w.end());
                    float med = w[w.size() / 2];
                    if (s.mbps < 0.6f * med) {
                        s.dip = 1;
                        bool prevDip = false;
                        {
                            std::lock_guard<std::mutex> l(g_bMx);
                            prevDip = !g_b.stab.empty() && g_b.stab.back().dip;
                        }
                        if (!prevDip) {
                            dips++;
                            Log(L_WARN, T("Stabilité : chute de débit à %d:%02d (%.1f Mo/s).", "Stability: throughput dip at %d:%02d (%.1f MB/s)."), m, sec, s.mbps);
                        }
                    }
                }
                if (!s.dip) normal.push_back(s.mbps);
            }
            sum += s.mbps; n++;
            mn = std::min(mn, (double)s.mbps);
            mx = std::max(mx, (double)s.mbps);
            {
                std::lock_guard<std::mutex> l(g_bMx);
                g_b.stab.push_back(s);
                g_b.stabElapsed = now - t0;
                g_b.progress = std::min(1.0, (now - t0) / total);
                g_b.liveMbps = s.mbps;
                g_b.liveLat = s.lat;
                g_b.sAvg = sum / n;
                g_b.sMin = mn;
                g_b.sMax = mx;
                g_b.sFailed = failed;
                g_b.sPages = okPages + failed;
                g_b.sSuccess = okPages + failed ? 100.0 * okPages / (okPages + failed) : 100;
                g_b.sDips = dips;
                g_b.sDrops = drops;
            }
            secStart = now;
            secBytes = secBusy = secLat = 0;
            secCalls = secOk = 0;
            secFailed = 0;
        }
    }
    std::lock_guard<std::mutex> l(g_bMx);
    g_b.stabElapsed = NowSec() - t0;
}

// ---------------------------------------------------------------------------
//  Lancement
// ---------------------------------------------------------------------------
static void SaveToHistory(const BenchState& b);

bool BenchStart(int mode) {
    if (JobRunning()) { Toast(T("Un test est en cours.", "A test is running."), true); return false; }
    std::vector<int> order = TestsForMode(mode);
    if (mode != TM_STABILITY && order.empty()) {
        Toast(T("Aucun test sélectionné (Réglages → Tests personnalisés).", "No test selected (Settings → Custom tests)."), true);
        return false;
    }
    g_stop = false;
    return StartJob([mode, order] {
        extern void DiagStart();
        if (!DevOpen() && !DevConnect(false)) {
            Toast(Fmt(T("%s — la carte DMA ne répond pas. Consultez le diagnostic et les solutions à tester.",
                        "%s — the DMA card is not responding. Check the diagnostics and the fixes to try."),
                      T("Connexion impossible", "Connection failed")), true);
            g_page = PG_DIAG;
            std::thread([] { while (JobRunning()) Sleep(20); DiagStart(); }).detach();
            return;
        }
        Tx err;
        std::vector<Range> ranges = TestRanges(&err);
        if (ranges.empty()) { Toast(err.s(), true); Log(L_ERR, "%s", err.c()); DevRelease(); return; }
        DeviceInfo d = DevInfo();
        Picker pk(ranges);
        bool stab = mode == TM_STABILITY;
        {
            std::lock_guard<std::mutex> l(g_bMx);
            BenchState b;
            b.running = true;
            b.stability = stab;
            b.mode = mode;
            b.order = order;
            b.date = NowStamp();
            b.simulated = d.simulated;
            b.devString = d.device;
            b.fpga = d.fpga; b.fw = d.firmware; b.pcie = d.pcie; b.maxMem = d.maxMem; b.usb = d.usbLink; b.ram = RamText(d);
            b.zone = ZoneText(ranges);
            b.zoneBase = ranges[0].base;
            u64 tot = 0;
            for (auto& r : ranges) tot += r.size;
            b.zoneMb = tot >> 20;
            b.duration = stab ? g_set.stabMin * 60.0 : (double)g_set.duration;
            b.stabTotal = stab ? g_set.stabMin * 60.0 : 0;
            g_b = b;
        }
        BenchState snap = BenchCopy();
        if (stab) {
            Log(L_INFO, T("Test de stabilité : %.0f min, scatter %d pages en continu.", "Stability test: %.0f min, continuous %d-page scatter."), g_set.stabMin * 1.0, 256);
            Log(L_INFO, T("Démarrage : zone 0x%llX + %llu Mo.", "Starting: range 0x%llX + %llu MB."), (unsigned long long)snap.zoneBase, (unsigned long long)snap.zoneMb);
            RunStability(pk, snap.stabTotal);
        } else {
            Log(L_INFO, T("Démarrage : zone 0x%llX + %llu Mo, %.0f s par test.", "Starting: range 0x%llX + %llu MB, %.0f s per test."),
                (unsigned long long)snap.zoneBase, (unsigned long long)snap.zoneMb, snap.duration);
            for (int ti : order) {
                if (g_stop) break;
                RunTest(ti, pk, snap.duration);
            }
        }
        DevRelease();
        BenchState fin;
        {
            std::lock_guard<std::mutex> l(g_bMx);
            g_b.running = false;
            g_b.stopped = g_stop;
            g_b.finished = !g_stop;
            g_b.current = -1;
            g_b.hasResult = true;
            fin = g_b;
        }
        if (stab) {
            int v = StabVerdict(fin);
            Log(v == 2 ? L_WARN : L_OK, T("Stabilité %s : %.1f Mo/s en moyenne (min %.1f, max %.1f), %llu pages en échec, %d chute(s), %d coupure(s).",
                                          "Stability %s: %.1f MB/s average (min %.1f, max %.1f), %llu failed pages, %d dip(s), %d dropout(s)."),
                fin.stopped ? T("interrompue", "stopped") : T("terminée", "finished"), fin.sAvg, fin.sMin, fin.sMax,
                (unsigned long long)fin.sFailed, fin.sDips, fin.sDrops);
            Toast(Fmt(T("Stabilité terminée · %s Mo/s en moyenne · lien %s", "Stability finished · %s MB/s average · %s link"),
                      Num(fin.sAvg, 1).c_str(), StabVerdictText(v)));
            if (fin.finished || fin.stabElapsed >= 60) SaveToHistory(fin);
        } else if (fin.stopped) {
            Log(L_WARN, "%s", T("Test interrompu.", "Test stopped."));
            Toast(T("Test interrompu.", "Test stopped."));
        } else {
            Log(L_OK, "%s", T("Test terminé.", "Test finished."));
            double best = BestMbps(fin);
            Toast(best > 0 ? Fmt(T("Test terminé · débit max %s Mo/s", "Test finished · max throughput %s MB/s"), Num(best, 1).c_str())
                           : std::string(T("Test terminé", "Test finished")));
            SaveToHistory(fin);
            g_page = PG_RESULTS;
        }
    });
}

// ---------------------------------------------------------------------------
//  Comparaison des 4 algorithmes FPGA : mêmes adresses, mêmes durées, réglage d'origine restauré
// ---------------------------------------------------------------------------
static std::mutex g_aMx;
static AlgoState g_a;
AlgoState AlgoCopy() { std::lock_guard<std::mutex> l(g_aMx); return g_a; }

static void AlgoMeasure(AlgoRes& r, const std::vector<Range>& ranges) {
    Picker pk(ranges);
    pk.rng.seed(42);
    std::vector<uint8_t> buf(8u << 20);
    std::vector<u64> pas(256);
    u64 units = 0, okU = 0;
    for (int k = 0; k < 3 && !g_stop; k++) {
        double busy = 0, bytes = 0, us = 0, t0 = NowSec();
        u64 calls = 0;
        while (!g_stop && NowSec() - t0 < 2.0) {
            if (k == 2) {
                for (auto& a : pas) a = pk.Pick(0x1000);
                int ok = DevReadScatter(256, pas.data(), &us);
                units += 256; okU += (u64)ok; bytes += ok * 4096.0;
            } else {
                uint32_t n = k == 0 ? 8 : 8u << 20;
                bool ok = DevRead(pk.Pick(n), n, buf.data(), &us);
                units++; if (ok) { okU++; bytes += n; }
            }
            busy += us;
            calls++;
        }
        if (!calls || busy <= 0) continue;
        if (k == 0) r.lat = busy / calls;
        else (k == 1 ? r.mbps : r.scMbps) = bytes / (busy / 1e6) / MB;
    }
    r.success = units ? 100.0 * okU / units : 0;
    r.ran = !g_stop;
}

bool AlgoStart() {
    if (g_set.mode != MODE_FPGA) return false;
    if (JobRunning()) { Toast(T("Un test est en cours.", "A test is running."), true); return false; }
    g_stop = false;
    return StartJob([] {
        { std::lock_guard<std::mutex> l(g_aMx); g_a = AlgoState(); g_a.running = true; }
        std::vector<Range> ranges;
        AlgoState s = AlgoCopy();
        for (int a = 1; a <= 4 && !g_stop; a++) {
            { std::lock_guard<std::mutex> l(g_aMx); g_a.current = a; }
            g_algoOverride = a;
            AlgoRes r;
            r.algo = a;
            if (DevConnect(false)) {
                r.opened = true;
                if (ranges.empty()) ranges = TestRanges(nullptr);
                if (!ranges.empty()) AlgoMeasure(r, ranges);
            }
            s.r[a] = r;
            std::lock_guard<std::mutex> l(g_aMx);
            g_a.r[a] = r;
        }
        g_algoOverride = 0;
        // Score : débit et réactivité rapportés au meilleur ; les erreurs éliminent
        double bl = 1e30, bm = 0, bs = 0;
        for (auto& r : s.r) if (r.ran && r.lat > 0) { bl = std::min(bl, r.lat); bm = std::max(bm, r.mbps); bs = std::max(bs, r.scMbps); }
        int best = 0;
        for (auto& r : s.r) {
            if (!r.ran || r.lat <= 0 || bm <= 0 || bs <= 0) continue;
            r.score = 100 * (0.4 * bl / r.lat + 0.2 * r.mbps / bm + 0.4 * r.scMbps / bs);
            if (r.success < 99.9) r.score *= std::max(0.0, (r.success - 95) / 5) * 0.5;
            if (!best || r.score > s.r[best].score) best = r.algo;
        }
        static const char* N[5] = { "Auto", "Async Normal", "Async Tiny", "Old Normal", "Old Tiny" };
        for (auto& r : s.r)
            if (r.ran) Log(L_INFO, T("Algorithme %-13s lat %6.1f µs · %6.1f Mo/s · scatter %6.1f Mo/s · succès %.2f %%", "Algorithm %-13s lat %6.1f µs · %6.1f MB/s · scatter %6.1f MB/s · success %.2f %%"),
                           N[r.algo], r.lat, r.mbps, r.scMbps, r.success);
        // Rouvre avec le réglage d'origine pour afficher les bonnes informations, puis libère la carte
        DevConnect(false);
        DevRelease();
        s.running = false;
        s.done = best > 0 && !g_stop;
        s.best = best;
        if (s.done) Toast(Fmt(T("Meilleur algorithme : %s", "Best algorithm: %s"), N[best]));
        std::lock_guard<std::mutex> l(g_aMx);
        g_a = s;
    });
}

void BenchStop() {
    g_stop = true;
    std::lock_guard<std::mutex> l(g_bMx);
    g_b.stopping = g_b.running;
}

// ---------------------------------------------------------------------------
//  Verdicts
// ---------------------------------------------------------------------------
int RateMbps(double v) { return v <= 0 ? R_NONE : v < 30 ? R_POOR : v < 80 ? R_AVERAGE : v < 150 ? R_GOOD : R_EXCELLENT; }
int RateLat(double us) { return us <= 0 ? R_NONE : us < 60 ? R_EXCELLENT : us < 120 ? R_GOOD : us < 250 ? R_AVERAGE : R_POOR; }

const char* RatingText(int r) {
    switch (r) {
    case R_EXCELLENT: return "Excellent";
    case R_GOOD: return T("Bon", "Good");
    case R_AVERAGE: return T("Moyen", "Average");
    case R_POOR: return T("Faible", "Poor");
    }
    return "-";
}

int StabVerdict(const BenchState& b) {
    double minutes = b.stabElapsed / 60.0;
    if (b.sDrops == 0 && b.sSuccess >= 99.9 && b.sDips <= std::max(2, (int)(minutes / 5))) return 0;
    if (b.sDrops == 0 && b.sSuccess >= 99.5) return 1;
    return 2;
}

const char* StabVerdictText(int v) {
    return v == 0 ? "stable" : v == 1 ? T("moyen", "fair") : T("instable", "unstable");
}

double BestMbps(const BenchState& b, int* which) {
    double best = 0;
    for (int i = 1; i < 8; i++)
        if (b.res[i].ran && b.res[i].mbps > best) { best = b.res[i].mbps; if (which) *which = i; }
    return best;
}

double Stability(const BenchState& b, u64* failed) {
    double ok = 0, tot = 0;
    u64 f = 0;
    for (int i = 0; i < 8; i++) {
        const TestResult& r = b.res[i];
        if (!r.ran || !r.calls) continue;
        double units = TESTS[i].kind == K_SCATTER ? (double)r.calls * TESTS[i].pages : (double)r.calls;
        tot += units;
        ok += units - (double)r.errors;
        f += r.errors;
    }
    if (failed) *failed = f;
    return tot > 0 ? 100.0 * ok / tot : 100;
}

// ---------------------------------------------------------------------------
//  Exports
// ---------------------------------------------------------------------------
static std::string JsonEsc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) o += Fmt("\\u%04x", (unsigned char)c);
        else o += c;
    }
    return o;
}

std::string ResultsJson(const BenchState& b) {
    std::string s = "{\n  \"application\": \"Camra's Tool\",\n";
    s += Fmt("  \"version\": \"%s\",\n", APP_VERSION);
    s += Fmt("  \"date\": \"%s\",\n", b.date.c_str());
    s += Fmt("  \"simulation\": %s,\n", b.simulated ? "true" : "false");
    s += "  \"peripherique\": { \"chaine\": \"" + JsonEsc(b.devString) + "\", ";
    s += Fmt("\"fpga\": \"%s\", \"firmware\": \"%s\", \"pcie\": \"%s\", \"adresse_max\": \"%s\", \"usb\": \"%s\" },\n",
             JsonEsc(b.fpga.fr).c_str(), JsonEsc(b.fw.fr).c_str(), JsonEsc(b.pcie.fr).c_str(), JsonEsc(b.maxMem.en).c_str(), JsonEsc(b.usb.en).c_str());
    s += Fmt("  \"parametres\": { \"base\": \"0x%llX\", \"plage_Mo\": %llu, \"duree_s\": %.1f }",
             (unsigned long long)b.zoneBase, (unsigned long long)b.zoneMb, b.duration);
    if (!b.stability) {
        s += ",\n  \"resultats\": [\n";
        bool first = true;
        for (int i = 0; i < 8; i++) {
            const TestResult& r = b.res[i];
            if (!r.ran) continue;
            s += Fmt("%s    { \"test\": \"%s\", \"cle\": \"%s\", \"debit_MBps\": %.2f, \"appels_par_s\": %.1f, \"latence_moy_us\": %.1f, "
                     "\"latence_min_us\": %.1f, \"latence_p50_us\": %.1f, \"latence_p99_us\": %.1f, \"latence_max_us\": %.1f, "
                     "\"succes_pct\": %.2f, \"appels\": %llu, \"erreurs\": %llu }",
                     first ? "" : ",\n", TESTS[i].nameFr, TESTS[i].key, r.mbps, r.callsPerSec, r.latAvg, r.latMin, r.latP50, r.latP99, r.latMax,
                     r.success, (unsigned long long)r.calls, (unsigned long long)r.errors);
            first = false;
        }
        s += "\n  ]";
    } else {
        s += Fmt(",\n  \"stabilite\": { \"duree_s\": %.0f, \"debit_moy_MBps\": %.2f, \"debit_min_MBps\": %.2f, \"debit_max_MBps\": %.2f, "
                 "\"succes_pct\": %.3f, \"pages_en_echec\": %llu, \"chutes\": %d, \"coupures\": %d }",
                 b.stabElapsed, b.sAvg, b.sMin, b.sMax, b.sSuccess, (unsigned long long)b.sFailed, b.sDips, b.sDrops);
    }
    s += "\n}\n";
    return s;
}

std::string ResultsCsv(const BenchState& b) {
    std::string s;
    if (b.stability) {
        s = "Temps (s);Debit (Mo/s);Latence moy (us);Pages en echec;Chute;Coupure\r\n";
        for (size_t i = 0; i < b.stab.size(); i++) {
            const StabSample& x = b.stab[i];
            s += Fmt("%.0f;%.2f;%.1f;%u;%u;%u\r\n", (double)(i + 1), x.mbps, x.lat, x.failed, (unsigned)x.dip, (unsigned)x.drop);
        }
        return s;
    }
    s = "Test;Debit (Mo/s);Appels/s;Latence moy (us);Latence min (us);p50 (us);p99 (us);max (us);Succes (%);Appels;Erreurs\r\n";
    for (int i = 0; i < 8; i++) {
        const TestResult& r = b.res[i];
        if (!r.ran) continue;
        s += Fmt("%s;%.2f;%.1f;%.1f;%.1f;%.1f;%.1f;%.1f;%.2f;%llu;%llu\r\n", TESTS[i].nameFr, r.mbps, r.callsPerSec, r.latAvg, r.latMin,
                 r.latP50, r.latP99, r.latMax, r.success, (unsigned long long)r.calls, (unsigned long long)r.errors);
    }
    return s;
}

std::string ResultsDiscord(const BenchState& b) {
    std::string date = b.date.size() >= 16 ? b.date.substr(0, 16) : b.date;
    std::string s = Fmt("**Camra's Tool — %s** · %s\n", b.stability ? T("Test de stabilité DMA", "DMA stability test") : T("Résultats DMA", "DMA results"), date.c_str());
    s += Fmt(T("FPGA %s · firmware %s · %s · LeechCore « %s »\n", "FPGA %s · firmware %s · %s · LeechCore \"%s\"\n"),
             b.fpga.c(), b.fw.c(), b.usb.c(), b.devString.c_str());
    if (b.simulated) s += T("⚠️ Simulation : données fictives, aucune carte réelle.\n", "⚠️ Simulation: fake data, no real card.\n");
    if (b.stability) {
        int m = (int)(b.stabElapsed / 60), sec = (int)b.stabElapsed % 60;
        s += Fmt(T("⏱️ %d:%02d · **Débit moyen** %s Mo/s (min %s · max %s)\n", "⏱️ %d:%02d · **Average throughput** %s MB/s (min %s · max %s)\n"),
                 m, sec, Num(b.sAvg, 1).c_str(), Num(b.sMin, 1).c_str(), Num(b.sMax, 1).c_str());
        int v = StabVerdict(b);
        s += Fmt(T("%s Lectures réussies %s %% · chutes %d · coupures %d → **%s**\n", "%s Successful reads %s %% · dips %d · dropouts %d → **%s**\n"),
                 v == 0 ? "✅" : v == 1 ? "⚠️" : "❌", Num(b.sSuccess, 2).c_str(), b.sDips, b.sDrops, StabVerdictText(v));
        return s;
    }
    int bi = -1;
    double best = BestMbps(b, &bi);
    const TestResult& lat = b.res[0];
    u64 failed = 0;
    double st = Stability(b, &failed);
    if (best > 0) s += Fmt(T("📊 **Débit max** %s Mo/s (%s) · ", "📊 **Max throughput** %s MB/s (%s) · "), Num(best, 1).c_str(), RatingText(RateMbps(best)));
    if (lat.ran && lat.calls) s += Fmt(T("⚡ **Latence** %s µs (%s) · ", "⚡ **Latency** %s µs (%s) · "), Num(lat.latAvg, 1).c_str(), RatingText(RateLat(lat.latAvg)));
    s += Fmt(T("✅ **Stabilité** %s %%\n", "✅ **Stability** %s %%\n"), Num(st, 2).c_str());
    s += "```\n";
    s += Fmt("%-18s %9s %9s %9s %8s\n", "Test", T("Mo/s", "MB/s"), T("Appels/s", "Calls/s"), T("Lat. µs", "Lat. µs"), T("Succès", "Success"));
    for (int i = 0; i < 8; i++) {
        const TestResult& r = b.res[i];
        if (!r.ran) continue;
        std::string name = g_en ? TESTS[i].nameEn : TESTS[i].nameFr;
        // Aligne sur le nombre de caractères affichés (les accents prennent 2 octets en UTF-8)
        int extra = 0;
        for (unsigned char c : name) if ((c & 0xC0) == 0x80) extra++;
        s += Fmt("%-*s %9s %9s %9s %7s%%\n", 18 + extra, name.c_str(), Num(r.mbps, 1).c_str(), Num(r.callsPerSec, 0).c_str(),
                 Num(r.latAvg, 1).c_str(), Num(r.success, 1).c_str());
    }
    s += "```\n";
    return s;
}

// ---------------------------------------------------------------------------
//  Historique : une ligne par test terminé (CamrasTool_history.txt, champs séparés par des tabulations)
// ---------------------------------------------------------------------------
static std::mutex g_hMx;
static std::vector<HistEntry> g_hist;

static std::wstring HistPath() { return DataDir() + L"\\CamrasTool_history.txt"; }

static std::string Clean(std::string s) {
    for (auto& c : s) if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    return s;
}

static void HistorySaveLocked() {
    std::string out;
    for (auto& e : g_hist) {
        out += Fmt("H2\t%s\t%d\t%d\t%d\t", Clean(e.date).c_str(), e.kind, e.mode, e.sim ? 1 : 0);
        out += Clean(e.device) + "\t" + Clean(e.fpga) + "\t" + Clean(e.fw) + "\t" + Clean(e.usb) + "\t" + Clean(e.ram) + "\t";
        out += Fmt("%.3f\t%.3f\t%.4f", e.maxMbps, e.latAvg, e.success);
        for (int i = 0; i < 8; i++) out += Fmt("\t%d\t%.3f\t%.3f\t%.3f", e.ran[i] ? 1 : 0, e.mbps[i], e.lat[i], e.succ[i]);
        out += Fmt("\t%.1f\t%.3f\t%.3f\t%.3f\t%.4f\t%llu\t%d\t%d\t", e.stabMin, e.sAvg, e.sMin, e.sMax, e.sSucc, (unsigned long long)e.sFailed, e.sDips, e.sDrops);
        out += Clean(e.note) + "\n";
    }
    if (!WriteFileAll(HistPath(), out.data(), out.size()))
        Toast(T("Historique non enregistré : le dossier de Camra's Tool est en lecture seule.", "History not saved: the Camra's Tool folder is read-only."), true);
}

void HistoryLoad() {
    std::string raw;
    if (!ReadFileAll(HistPath(), raw)) return;
    std::lock_guard<std::mutex> l(g_hMx);
    g_hist.clear();
    size_t p = 0;
    while (p < raw.size()) {
        size_t e = raw.find('\n', p);
        std::string line = raw.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? raw.size() : e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        size_t a = 0;
        while (true) {
            size_t t = line.find('\t', a);
            f.push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a));
            if (t == std::string::npos) break;
            a = t + 1;
        }
        if (f.size() < 54 || f[0] != "H2") continue;   // H2 + 52 valeurs + note
        HistEntry h;
        size_t k = 1;
        auto S = [&]() { return f[k++]; };
        auto I = [&]() { return atoi(f[k++].c_str()); };
        auto D = [&]() { return atof(f[k++].c_str()); };
        h.date = S(); h.kind = I(); h.mode = I(); h.sim = I() != 0;
        h.device = S(); h.fpga = S(); h.fw = S(); h.usb = S(); h.ram = S();
        h.maxMbps = D(); h.latAvg = D(); h.success = D();
        for (int i = 0; i < 8; i++) { h.ran[i] = I() != 0; h.mbps[i] = D(); h.lat[i] = D(); h.succ[i] = D(); }
        h.stabMin = D(); h.sAvg = D(); h.sMin = D(); h.sMax = D(); h.sSucc = D();
        h.sFailed = (u64)_strtoui64(f[k++].c_str(), nullptr, 10);
        h.sDips = I(); h.sDrops = I();
        h.note = k < f.size() ? f[k] : "";
        g_hist.push_back(h);
    }
}

std::vector<HistEntry> HistoryCopy() { std::lock_guard<std::mutex> l(g_hMx); return g_hist; }

void HistoryAdd(const HistEntry& e) {
    std::lock_guard<std::mutex> l(g_hMx);
    g_hist.insert(g_hist.begin(), e);
    if (g_hist.size() > 500) g_hist.resize(500);
    HistorySaveLocked();
}

void HistoryDelete(int i) {
    std::lock_guard<std::mutex> l(g_hMx);
    if (i < 0 || i >= (int)g_hist.size()) return;
    g_hist.erase(g_hist.begin() + i);
    HistorySaveLocked();
}

void HistorySetNote(int i, const std::string& note) {
    std::lock_guard<std::mutex> l(g_hMx);
    if (i < 0 || i >= (int)g_hist.size()) return;
    g_hist[(size_t)i].note = note;
    HistorySaveLocked();
}

static void SaveToHistory(const BenchState& b) {
    HistEntry e;
    e.date = b.date.substr(0, 16);
    e.kind = b.stability ? 1 : 0;
    e.mode = b.mode;
    e.sim = b.simulated;
    e.device = b.devString;
    e.fpga = b.fpga.s();
    e.fw = b.fw.s();
    e.usb = b.usb.s();
    e.ram = b.ram.s();
    if (b.stability) {
        e.stabMin = b.stabElapsed / 60.0;
        e.sAvg = b.sAvg; e.sMin = b.sMin; e.sMax = b.sMax; e.sSucc = b.sSuccess;
        e.sFailed = b.sFailed; e.sDips = b.sDips; e.sDrops = b.sDrops;
        e.maxMbps = b.sMax;
        e.success = b.sSuccess;
    } else {
        e.maxMbps = BestMbps(b);
        e.latAvg = b.res[0].ran ? b.res[0].latAvg : 0;
        e.success = Stability(b);
        for (int i = 0; i < 8; i++) {
            e.ran[i] = b.res[i].ran && b.res[i].calls;
            e.mbps[i] = b.res[i].mbps;
            e.lat[i] = b.res[i].latAvg;
            e.succ[i] = b.res[i].success;
        }
    }
    HistoryAdd(e);
}
