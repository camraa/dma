// tests.cpp - tests automatiques sans carte : Camra's Tool.exe --test --data=DOSSIER (code de sortie = nombre d'échecs)
#include "bench.h"
#include "diag.h"
#include "firmware.h"
#include <cmath>

static int g_fail = 0, g_count = 0;
static std::string g_out;

static void Check_(bool ok, const char* what) {
    g_count++;
    if (!ok) g_fail++;
    g_out += std::string(ok ? "OK    " : "ECHEC ") + what + "\r\n";
}
#define CHECK(x) Check_((x), #x)

static void WaitJob() { for (int i = 0; i < 1200 && JobRunning(); i++) Sleep(50); }

static std::string Le(uint32_t v) { return std::string((const char*)&v, 4); }

int RunTests() {
    // Nombres et textes
    g_en = false;
    CHECK(Num(1234567.891, 1) == "1 234 567,9");
    CHECK(Num(-12.5, 1) == "-12,5");
    g_en = true;
    CHECK(Num(1234.5, 1) == "1,234.5");
    g_en = false;
    u64 v = 0;
    CHECK(ParseHex("0x10000000", &v) && v == 0x10000000);
    CHECK(ParseHex("10000000", &v) && v == 0x10000000);
    CHECK(!ParseHex("0xZZ", &v));
    CHECK(U2W(W2U(L"câble é µ")) == L"câble é µ");

    // Algorithmes FPGA : réglage 1..4 -> LeechCore 0..3
    g_set.mode = MODE_FPGA;
    const char* want[5] = { "fpga", "fpga://algo=0", "fpga://algo=1", "fpga://algo=2", "fpga://algo=3" };
    for (int a = 0; a < 5; a++) { g_set.algo = a; CHECK(DevDeviceString() == want[a]); }
    g_set.algo = 0;

    // Firmware : IDCODE lu dans le fichier
    std::string head(64, '\xFF');
    std::string fw = head + std::string("\xAA\x99\x55\x66", 4) + std::string(16, '\0') + std::string("\x30\x01\x80\x01", 4) + std::string("\x03\x62\xD0\x93", 4) + std::string(64, '\0');
    std::wstring f = DataDir() + L"\\fw.bin";
    WriteFileAll(f, fw.data(), fw.size());
    CHECK(FirmwareIdcode(f) == 0x0362D093);
    CHECK(ModelById(0x1362D093) && std::string(ModelById(0x1362D093)->name) == "xc7a35t");
    WriteFileAll(f, head.data(), head.size());
    CHECK(FirmwareIdcode(f) == 0);
    DeleteFileW(f.c_str());
    for (int i = 0; i < FPGA_MODEL_COUNT; i++)
        for (int k = i + 1; k < FPGA_MODEL_COUNT; k++)
            if ((FPGA_MODELS[i].idcode & 0x0FFFFFFF) == (FPGA_MODELS[k].idcode & 0x0FFFFFFF)) CHECK(!"IDCODE en double");

    // Kit intégré
    std::wstring kd = DataDir() + L"\\drv";
    MakeDirs(kd);
    CHECK(ExtractKitPrefix(L"drivers\\ftdi\\", kd));
    CHECK(FileExists(kd + L"\\ftdibus3.Inf") && FileExists(kd + L"\\FTDIBUS3.sys") && FileExists(kd + L"\\FTDIBUS3.cat"));

    // Mises à jour
    CHECK(IsNewer("1.10.0", "1.9.9"));
    CHECK(!IsNewer("1.4.0", "1.4.0"));
    CHECK(!IsNewer("abc", "1.4.0"));
    std::string ver, url, sha, h64(64, 'a');
    std::string js = "{\"tag_name\": \"v9.8.7\", \"name\": \"x\", \"assets\": [{\"name\": \"a.zip\", \"digest\": \"sha256:" + std::string(64, 'b') +
                     "\", \"browser_download_url\": \"https://github.com/camraa/dma/releases/download/v9.8.7/a.zip\"},{\"name\":\"Camras-Tool.exe\",\"digest\":\"sha256:" + h64 +
                     "\",\"browser_download_url\":\"https://github.com/camraa/dma/releases/download/v9.8.7/Camras-Tool.exe\"}]}";
    CHECK(ParseRelease(js, ver, url, sha) && ver == "9.8.7" && sha == h64 && url.find("Camras-Tool.exe") != std::string::npos);
    std::string evil = js;
    evil.replace(evil.rfind("https://github.com"), 18, "https://evil.xx.com");
    CHECK(ParseRelease(evil, ver, url, sha) && url.empty());
    CHECK(Sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // Signalement : anonymisation, vidages retirés, zip
    CHECK(Anon("C:\\Users\\jean\\x.dll").find("jean") == std::string::npos);
    CHECK(IsDumpLine("[LeechCore] 0010    97 cf ad 50 3e"));
    CHECK(!IsDumpLine("[LeechCore] DEVICE: FPGA: Enigma X1"));
    CHECK(Crc32("123456789") == 0xCBF43926);
    std::string z = MakeZip({ { "a.txt", "abc" } });
    CHECK(z.rfind(std::string("PK\x03\x04", 4), 0) == 0 && z.find(Le(Crc32("abc"))) != std::string::npos);

    // Lien PCIe et cause du débit
    CHECK(ParsePcieLink("DEVICE: FPGA: Enigma X1 PCIe gen2 x1 [250,10,500]") == "gen2 x1");
    CHECK(ParsePcieLink("rien") == "");
    CHECK(Bottleneck(35, 2, "gen2 x1").fr.find("USB 2") != std::string::npos);
    CHECK(Bottleneck(190, 3, "gen2 x1").fr != Bottleneck(60, 3, "gen2 x1").fr);

    // Dérive de débit
    std::vector<StabSample> st;
    for (int i = 0; i < 300; i++) st.push_back({ 180.f, 0, 0, 0, 0 });
    CHECK(std::fabs(StabDrift(st)) < 0.1);
    for (int i = 0; i < 300; i++) st[(size_t)i].mbps = 180.f - i * 0.1f;
    CHECK(StabDrift(st) < -8);
    st.resize(60);
    CHECK(StabDrift(st) == 0);

    // Simulation : test complet, médiane de 3 passages, historique, exports
    g_set.mode = MODE_SIM;
    g_set.duration = 1;
    g_set.runs = 3;
    g_set.notify = false;
    HistoryLoad();
    size_t h0 = HistoryCopy().size();
    CHECK(BenchStart(TM_SPEED, "câble : essai", true));
    WaitJob();
    BenchState b = BenchCopy();
    CHECK(b.hasResult && b.finished && b.doneCount == 3);
    CHECK(b.res[0].calls > 0 && b.res[0].latAvg > 30 && b.res[0].latAvg < 80);
    double sum = 0;
    for (float x : b.res[0].hist) sum += x;
    CHECK(std::fabs(sum - 100) < 0.5);
    CHECK(HistoryCopy().size() == h0 + 1);
    HistoryLoad();
    CHECK(HistoryCopy().size() == h0 + 1 && HistoryCopy()[0].note == "câble : essai" && HistoryCopy()[0].kind == 0);
    CHECK(ResultsJson(b).find("\"simulation\": true") != std::string::npos);
    CHECK(ResultsCsv(b).find("Latence;") != std::string::npos);
    CHECK(ResultsDiscord(b).size() < 2000);
    std::wstring png = DataDir() + L"\\t.png";
    std::string data;
    CHECK(ResultsImage(b, png.c_str()) && ReadFileAll(png, data) && data.rfind("\x89PNG", 0) == 0);
    g_set.runs = 1;

    // Diagnostic en simulation
    DiagStart();
    WaitJob();
    DiagResult d = DiagCopy();
    CHECK(d.done && !d.checks.empty());
    CHECK(DiagReport().find("\\Users\\") == std::string::npos || DiagReport().find("…") != std::string::npos);
    CHECK(SupportZip().size() > 500);
    DevDisconnect();

    g_out += Fmt("%d tests, %d echec(s)\r\n", g_count, g_fail);
    WriteFileAll(DataDir() + L"\\tests.txt", g_out.data(), g_out.size());
    return g_fail;
}
