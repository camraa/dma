// firmware.cpp - OpenOCD (version CH347 de WCH) + bitstreams proxy bscan_spi
#include "firmware.h"
#include <thread>
#include <algorithm>

// IDCODE JTAG (sans les 4 bits de révision) -> modèle FPGA et bitstream proxy bscan_spi_<modèle>.bit
const FpgaModel FPGA_MODELS[] = {
    { 0x0362D093, "xc7a35t" }, { 0x0362C093, "xc7a50t" }, { 0x03632093, "xc7a75t" }, { 0x03631093, "xc7a100t" },
    { 0x03636093, "xc7a200t" }, { 0x0362E093, "xc7a15t" }, { 0x037C3093, "xc7a12t" }, { 0x037C2093, "xc7a25t" },
    { 0x037C4093, "xc7s25" }, { 0x0362F093, "xc7s50" }, { 0x03647093, "xc7k70t" }, { 0x0364C093, "xc7k160t" },
    { 0x03651093, "xc7k325t" }, { 0x03747093, "xc7k355t" }, { 0x03656093, "xc7k410t" }, { 0x03752093, "xc7k420t" },
    { 0x03751093, "xc7k480t" }, { 0x03671093, "xc7v585t" }, { 0x036B3093, "xc7v2000t" }, { 0x03667093, "xc7vx330t" },
    { 0x03682093, "xc7vx415t" }, { 0x03687093, "xc7vx485t" }, { 0x03692093, "xc7vx550t" }, { 0x03691093, "xc7vx690t" },
    { 0x03696093, "xc7vx980t" }, { 0x036D5093, "xc7vx1140t" }, { 0x036D9093, "xc7vh580t" }, { 0x036DB093, "xc7vh870t" },
    { 0x04000093, "xc6slx4" }, { 0x04001093, "xc6slx9" }, { 0x04002093, "xc6slx16" }, { 0x04004093, "xc6slx25" },
    { 0x04024093, "xc6slx25t" }, { 0x04008093, "xc6slx45" }, { 0x04028093, "xc6slx45t" }, { 0x0400E093, "xc6slx75" },
    { 0x0402E093, "xc6slx75t" }, { 0x04011093, "xc6slx100" }, { 0x04031093, "xc6slx100t" }, { 0x0401D093, "xc6slx150" },
    { 0x0403D093, "xc6slx150t" }, { 0x03822093, "xcku040" },
};
const int FPGA_MODEL_COUNT = (int)(sizeof FPGA_MODELS / sizeof FPGA_MODELS[0]);

const FpgaModel* FindModel(const char* name) {
    for (auto& m : FPGA_MODELS) if (_stricmp(m.name, name) == 0) return &m;
    return nullptr;
}

const FpgaModel* ModelById(uint32_t id) {
    for (auto& m : FPGA_MODELS) if ((m.idcode & 0x0FFFFFFF) == (id & 0x0FFFFFFF)) return &m;
    return nullptr;
}

// Un firmware Xilinx série 7 / UltraScale contient le mot de synchro AA995566 puis l'écriture du
// registre IDCODE (30018001) suivie du modèle visé.
uint32_t FirmwareIdcode(const std::wstring& file) {
    std::string d;
    if (!ReadFileAll(file, d)) return 0;
    const unsigned char sync[4] = { 0xAA, 0x99, 0x55, 0x66 }, idw[4] = { 0x30, 0x01, 0x80, 0x01 };
    size_t k = d.find(std::string((const char*)sync, 4));
    if (k == std::string::npos) return 0;
    size_t i = d.find(std::string((const char*)idw, 4), k);
    if (i == std::string::npos || i - k > 4096 || i + 8 > d.size()) return 0;
    const unsigned char* p = (const unsigned char*)d.data() + i + 4;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// Script OpenOCD de la famille : (fichier cfg, tap, procédure de redémarrage)
static void Family(const char* model, const char** cfg, const char** tap, const char** prog) {
    if (_strnicmp(model, "xc6s", 4) == 0) { *cfg = "xilinx-xc6s.cfg"; *tap = "xc6s.tap"; *prog = "xc6s_program"; }
    else if (_strnicmp(model, "xcku", 4) == 0) { *cfg = "xilinx-xcu.cfg"; *tap = "xcu.tap"; *prog = "xcu_program"; }
    else { *cfg = "xilinx-xc7.cfg"; *tap = "xc7.tap"; *prog = "xc7_program"; }
}

static std::mutex g_fMx;
static FwState g_f;

FwState FwCopy() { std::lock_guard<std::mutex> l(g_fMx); return g_f; }
void FwClearConsole() { std::lock_guard<std::mutex> l(g_fMx); g_f.console.clear(); }

static void Console(const std::string& line) {
    std::lock_guard<std::mutex> l(g_fMx);
    g_f.console.push_back(line);
    if (g_f.console.size() > 3000) g_f.console.erase(g_f.console.begin(), g_f.console.begin() + 500);
}

static void Status(const Tx& t) { std::lock_guard<std::mutex> l(g_fMx); g_f.status = t; }

// ---------------------------------------------------------------------------
//  CH347 : état rafraîchi en arrière-plan (le branchement est détecté en direct)
// ---------------------------------------------------------------------------
static std::mutex g_chMx;
static Ch347 g_ch;
static double g_chT = -10;
static std::atomic<bool> g_chBusy{ false };

Ch347 Ch347Cached() {
    double now = NowSec();
    bool expected = false;
    if (now - g_chT > 1.5 && g_chBusy.compare_exchange_strong(expected, true)) {
        g_chT = now;
        std::thread([] {
            Ch347 c = FindCh347();
            { std::lock_guard<std::mutex> l(g_chMx); g_ch = c; }
            g_chBusy = false;
        }).detach();
    }
    std::lock_guard<std::mutex> l(g_chMx);
    return g_ch;
}

// ---------------------------------------------------------------------------
//  Chemins pour Tcl : barres obliques, et nom court 8.3 si le chemin contient des accents
// ---------------------------------------------------------------------------
static bool Ascii(const std::wstring& s) {
    for (wchar_t c : s) if (c > 126) return false;
    return true;
}

static std::wstring Safe(const std::wstring& p) {
    if (Ascii(p)) return p;
    wchar_t s[MAX_PATH * 2];
    DWORD n = GetShortPathNameW(p.c_str(), s, MAX_PATH * 2);
    return n && n < MAX_PATH * 2 && Ascii(std::wstring(s, n)) ? std::wstring(s, n) : p;
}

static std::string Tcl(const std::wstring& p) {
    std::string s = W2U(Safe(p));
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

// Copie le firmware choisi dans le dossier du kit si son chemin n'est pas lisible par OpenOCD
static std::wstring StageFile(const std::wstring& f) {
    if (Ascii(f) || Ascii(Safe(f))) return f;
    std::wstring dst = KitDir() + L"\\work";
    MakeDirs(dst);
    size_t dot = f.find_last_of(L'.');
    dst += L"\\firmware" + (dot == std::wstring::npos ? L"" : f.substr(dot));
    CopyFileW(f.c_str(), dst.c_str(), FALSE);
    return dst;
}

// ---------------------------------------------------------------------------
//  Exécution d'OpenOCD avec lecture de sa sortie ligne par ligne
// ---------------------------------------------------------------------------
static bool RunOpenOcd(const std::string& cmd, const std::function<void(const std::string&)>& onLine, DWORD* code) {
    std::wstring bin = KitDir() + L"\\openocd\\bin";
    std::wstring scripts = KitDir() + L"\\openocd\\scripts";
    std::wstring cl = L"\"" + Safe(bin) + L"\\openocd.exe\" -s \"" + Safe(scripts) + L"\" -c \"" + U2W(cmd) + L"\"";
    Console(Fmt("> openocd -c \"%s\"", cmd.c_str()));
    SECURITY_ATTRIBUTES sa = { sizeof sa, nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { Console("CreatePipe"); return false; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = { sizeof si };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cl.begin(), cl.end());
    buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, bin.c_str(), &si, &pi)) {
        DWORD e = GetLastError();
        CloseHandle(rd); CloseHandle(wr);
        Console(Fmt(T("Impossible de lancer OpenOCD (erreur %lu).", "Cannot start OpenOCD (error %lu)."), e));
        *code = e;
        return false;
    }
    CloseHandle(wr);
    std::string pending;
    char chunk[4096];
    DWORD n = 0;
    while (ReadFile(rd, chunk, sizeof chunk, &n, nullptr) && n) {
        pending.append(chunk, n);
        size_t k;
        while ((k = pending.find_first_of("\r\n")) != std::string::npos) {
            std::string line = pending.substr(0, k);
            pending.erase(0, k + 1);
            if (!line.empty()) { Console(line); onLine(line); }
        }
    }
    if (!pending.empty()) { Console(pending); onLine(pending); }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

// Traduit une erreur d'OpenOCD en message compréhensible
static Tx Explain(const std::string& allLower, const std::string& lastErr, DWORD code) {
    auto has = [&](const char* s) { return allLower.find(s) != std::string::npos; };
    if (has("couldn't open") || has("no such file") || has("can't open") || has("failed to open"))
        return TX("Le fichier choisi est introuvable ou illisible.", "The selected file is missing or unreadable.");
    if (has("ch347") && (has("fail") || has("not found") || has("no device")))
        return TX("Le CH347 n'est pas accessible : branchez le câble USB du port de flash (JTAG) de la carte, installez le pilote WCH CH341PAR et fermez les autres outils de flashage.",
                  "The CH347 is not reachable: plug the USB cable into the card's flashing (JTAG) port, install the WCH CH341PAR driver and close other flashing tools.");
    if (has("all ones") || has("all zeroes") || has("interrogation failed") || has("no taps"))
        return TX("Aucun FPGA ne répond sur la chaîne JTAG : le PC cible doit être allumé (la carte est alimentée par son slot PCIe/M.2) et le câble JTAG bien branché. Essayez l'horloge 10 MHz.",
                  "No FPGA answers on the JTAG chain: the target PC must be on (the card is powered by its PCIe/M.2 slot) and the JTAG cable properly plugged in. Try the 10 MHz clock.");
    if (has("unexpected") && has("expected"))
        return TX("Le FPGA présent ne correspond pas au modèle choisi : utilisez « Infos appareil » pour détecter le bon modèle.",
                  "The FPGA found does not match the selected model: use \"Device info\" to detect the right model.");
    if (has("unknown flash") || has("flash id") || (has("probe") && has("fail")))
        return TX("La flash SPI n'est pas reconnue : vérifiez le modèle FPGA (bitstream proxy) et réessayez à 10 MHz.",
                  "The SPI flash is not recognized: check the FPGA model (proxy bitstream) and retry at 10 MHz.");
    if (has("verif") || has("differ") || has("mismatch"))
        return TX("La vérification de la flash a échoué : relancez le flashage avec une horloge plus basse (10 MHz).",
                  "Flash verification failed: flash again with a lower clock (10 MHz).");
    if (!lastErr.empty()) return Tx{ lastErr, lastErr };
    return TX("OpenOCD s'est arrêté avec le code %lu.", "OpenOCD exited with code %lu.", code);
}

// ---------------------------------------------------------------------------
//  Lancement d'une opération
// ---------------------------------------------------------------------------
bool FwStart(int op) {
    {
        std::lock_guard<std::mutex> l(g_fMx);
        if (g_f.running) { Toast(T("Une opération est déjà en cours.", "An operation is already running."), true); return false; }
    }
    if (!KitReady()) {
        KitEnsureAsync();
        Toast(T("Préparation du kit de flashage… réessayez dans quelques secondes.", "Preparing the flashing kit… try again in a few seconds."));
        return false;
    }
    Ch347 ch = FindCh347();
    if (!ch.present) {
        Toast(T("Aucun CH347 détecté. Branchez le câble USB sur le port de flash (JTAG) de la carte DMA et installez le pilote WCH CH341PAR (Diagnostic).",
                "No CH347 detected. Plug the USB cable into the DMA card's flashing (JTAG) port and install the WCH CH341PAR driver (Diagnostics)."), true);
        return false;
    }
    std::string model = g_set.fwModel;
    const FpgaModel* fm = FindModel(model.c_str());
    if (op != FW_DETECT && !fm) {
        Toast(Fmt(T("Modèle FPGA inconnu : %s", "Unknown FPGA model: %s"), model.c_str()), true);
        return false;
    }
    std::wstring file = g_set.fwFile;
    bool needFile = op == FW_PROGRAM || op == FW_VERIFY;
    if (needFile && (file.empty() || !FileExists(file))) {
        Toast(T("Le fichier choisi est introuvable ou illisible.", "The selected file is missing or unreadable."), true);
        return false;
    }
    if (needFile) {
        uint32_t fid = FirmwareIdcode(file);
        if (fid && (fid & 0x0FFFFFFF) != (fm->idcode & 0x0FFFFFFF)) {
            const FpgaModel* m = ModelById(fid);
            Toast(Fmt(T("Refusé : ce fichier est prévu pour %s, pas pour %s.", "Refused: this file is made for %s, not %s."),
                      m ? m->name : Fmt("IDCODE 0x%08X", fid).c_str(), fm->name), true);
            return false;
        }
    }
    bool backup = op == FW_BACKUP || (((op == FW_PROGRAM && g_set.fwType == 1) || op == FW_ERASE) && g_set.fwBackup);
    unsigned long hz = strtoul(g_set.fwClock, nullptr, 10);
    if (hz < 1000) hz = 10000000;
    unsigned khz = (unsigned)(hz / 1000);
    bool bit = g_set.fwType == 0;

    {
        std::lock_guard<std::mutex> l(g_fMx);
        g_f.running = true;
        g_f.op = op;
        g_f.t0 = NowSec();
        g_f.hasResult = false;
        g_f.status = TX("Connexion au CH347…", "Connecting to CH347…");
    }
    std::thread([=] {
        const char* cfg; const char* tap; const char* prog;
        Family(model.c_str(), &cfg, &tap, &prog);
        std::string pre = "proc ct {m r} {echo [concat $m [join [split $r \\n] { }]]}; ";
        pre += Fmt("adapter driver ch347; ch347 vid_pid 0x1a86 0x%04x; adapter speed %u; ", ch.pid, khz);
        std::string f = needFile ? Tcl(StageFile(file)) : "";
        std::string proxy = Tcl(KitDir() + L"\\FPGABit\\bscan_spi_" + U2W(model) + L".bit");
        std::wstring bakTmp = KitDir() + L"\\work\\backup.bin", bakFinal;
        if (backup) { MakeDirs(KitDir() + L"\\work"); DeleteFileW(bakTmp.c_str()); }
        std::string bak = backup ? Tcl(KitDir() + L"\\work") + "/backup.bin" : "";
        std::string cmd;
        if (op == FW_DETECT) cmd = pre + "init; exit";
        else if (op == FW_PROGRAM && bit)
            cmd = Fmt("%ssource [find cpld/%s]; source [find cpld/jtagspi.cfg]; init; if {[catch {pld load 0 {%s}} r]} {ct CT_FAIL $r} else {ct CT_LOADED $r}; exit",
                      pre.c_str(), cfg, f.c_str());
        else {
            std::string act;
            if (op == FW_BACKUP) act = Fmt("if {[catch {flash read_bank 0 {%s}} r]} {ct CT_FAIL $r} else {ct CT_BACKUP $r}", bak.c_str());
            else if (op == FW_VERIFY) act = Fmt("if {[catch {flash verify_bank 0 {%s} 0} r]} {ct CT_FAIL $r} else {ct CT_VERIFY $r}", f.c_str());
            else if (op == FW_ERASE) act = "if {[catch {flash erase_sector 0 0 last} r]} {ct CT_FAIL $r} else {ct CT_ERASED $r}";
            else act = Fmt("if {[catch {flash write_image erase {%s} 0} r]} {ct CT_FAIL $r} else {ct CT_WROTE $r; if {[catch {flash verify_bank 0 {%s} 0} r]} {ct CT_FAIL $r} else {ct CT_VERIFY $r}}",
                           f.c_str(), f.c_str());
            // Sauvegarde d'abord : si elle échoue, rien n'est écrit
            if (op != FW_BACKUP && backup)
                act = Fmt("if {[catch {flash read_bank 0 {%s}} r]} {ct CT_FAIL $r} else {ct CT_BACKUP $r; %s}", bak.c_str(), act.c_str());
            cmd = Fmt("%ssource [find cpld/%s]; source [find cpld/jtagspi.cfg]; init; if {[catch {jtagspi_init 0 {%s}} r]} {ct CT_FAIL $r} else {echo CT_PROXY; %s}; if {[catch {%s %s} r]} {ct CT_NOREBOOT $r} else {echo CT_REBOOT}; exit",
                      pre.c_str(), cfg, proxy.c_str(), act.c_str(), prog, tap);
        }
        const char* opName = op == FW_DETECT ? "detect" : op == FW_VERIFY ? "verify" : op == FW_ERASE ? "erase" : op == FW_BACKUP ? "backup" : bit ? "load" : "write";
        Log(L_INFO, T("Firmware : %s", "Firmware: %s"), Fmt("%s · %s · %u kHz%s%s", opName, model.c_str(), khz, needFile ? " · " : "", needFile ? FileName(file).c_str() : "").c_str());

        uint32_t idcode = 0;
        bool saved = false, loaded = false, verified = false, erased = false, rebooted = false, noreboot = false, failed = false, tooBig = false, mismatch = false;
        std::string failMsg, lastErr, all;
        auto onLine = [&](const std::string& line) {
            std::string low = Lower(line);
            all += low + "\n";
            size_t k = low.find("tap/device found: 0x");
            if (k != std::string::npos && !idcode) {
                idcode = (uint32_t)strtoul(line.c_str() + k + 18, nullptr, 16);
                Status(TX("FPGA détecté sur la chaîne JTAG", "FPGA found on the JTAG chain"));
            }
            if (line.find("CT_LOADED") != std::string::npos) { loaded = true; Status(TX("Bitstream chargé dans le FPGA", "Bitstream loaded into the FPGA")); }
            if (line.find("CT_PROXY") != std::string::npos) Status(TX("Proxy SPI chargé", "SPI proxy loaded"));
            if (low.find("found flash device") != std::string::npos) {
                Status(op == FW_VERIFY ? TX("Lecture et comparaison de la flash…", "Reading and comparing the flash…")
                       : backup ? TX("Sauvegarde de la flash… (peut prendre plusieurs minutes)", "Backing up the flash… (may take several minutes)")
                       : op == FW_ERASE ? TX("Effacement de la flash…", "Erasing the flash…")
                                        : TX("Effacement et écriture de la flash…", "Erasing and writing the flash…"));
            }
            if (line.find("CT_BACKUP") != std::string::npos) {
                saved = true;
                if (op == FW_ERASE) Status(TX("Effacement de la flash…", "Erasing the flash…"));
                else if (op == FW_PROGRAM) Status(TX("Effacement et écriture de la flash…", "Erasing and writing the flash…"));
            }
            if (line.find("CT_WROTE") != std::string::npos) Status(TX("Vérification de la flash…", "Verifying the flash…"));
            if (line.find("CT_VERIFY") != std::string::npos) { verified = true; Status(TX("Redémarrage du FPGA…", "Restarting the FPGA…")); }
            if (line.find("CT_ERASED") != std::string::npos) { erased = true; Status(TX("Redémarrage du FPGA…", "Restarting the FPGA…")); }
            if (line.find("CT_NOREBOOT") != std::string::npos) noreboot = true;
            else if (line.find("CT_REBOOT") != std::string::npos) rebooted = true;
            size_t f2 = line.find("CT_FAIL");
            if (f2 != std::string::npos) {
                failed = true;
                failMsg = line.substr(f2 + 7);
                failMsg.erase(0, failMsg.find_first_not_of(' '));
                std::string fl = Lower(failMsg);
                if (fl.find("exceeds flash bank size") != std::string::npos) tooBig = true;
                if (fl.find("mismatch") != std::string::npos || fl.find("differ") != std::string::npos || fl.find("verif") != std::string::npos) mismatch = true;
            }
            if (low.rfind("error", 0) == 0) lastErr = line;
        };
        DWORD code = 0;
        bool started = true, mismatchId = false;
        // Avant toute action : le FPGA branché doit être celui choisi
        if (op != FW_DETECT) {
            started = RunOpenOcd(pre + "init; exit", onLine, &code);
            mismatchId = idcode && (idcode & 0x0FFFFFFF) != (fm->idcode & 0x0FFFFFFF);
        }
        if (started && (op == FW_DETECT || (idcode && !mismatchId))) started = RunOpenOcd(cmd, onLine, &code);
        // La sauvegarde est gardée même si la suite échoue
        if (saved) {
            WIN32_FILE_ATTRIBUTE_DATA fa;
            saved = GetFileAttributesExW(bakTmp.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 0;
            if (saved) {
                std::wstring bd = DataDir() + L"\\sauvegardes";
                MakeDirs(bd);
                SYSTEMTIME st;
                GetLocalTime(&st);
                bakFinal = bd + L"\\" + U2W(Fmt("%s_%04d%02d%02d_%02d%02d%02d.bin", model.c_str(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond));
                saved = MoveFileExW(bakTmp.c_str(), bakFinal.c_str(), MOVEFILE_COPY_ALLOWED) != 0;
                if (saved) Log(L_OK, T("Firmware : sauvegarde %s", "Firmware: backup %s"), W2U(bakFinal).c_str());
            }
        }

        Tx res;
        bool ok = false;
        if (!started) res = TX("Impossible de lancer OpenOCD (erreur %lu).", "Cannot start OpenOCD (error %lu).", code);
        else if (op == FW_DETECT) {
            if (idcode) {
                const FpgaModel* m = ModelById(idcode);
                res = Tx{ Fmt("FPGA détecté : %s (IDCODE 0x%08X)", m ? m->name : "modèle inconnu", idcode),
                          Fmt("FPGA detected: %s (IDCODE 0x%08X)", m ? m->name : "unknown model", idcode) };
                ok = true;
                if (m) {
                    strncpy_s(g_set.fwModel, m->name, _TRUNCATE);
                    SaveSettings();
                }
            } else res = TX("Aucun FPGA détecté sur la chaîne JTAG.", "No FPGA detected on the JTAG chain.");
        } else if (mismatchId) {
            const FpgaModel* m = ModelById(idcode);
            res = TX("Refusé, rien n'a été écrit : le FPGA branché est %s (IDCODE 0x%08X), pas %s.", "Refused, nothing was written: the connected FPGA is %s (IDCODE 0x%08X), not %s.",
                     m ? m->name : "?", idcode, model.c_str());
        } else if (!idcode) {
            res = all.empty() ? TX("Aucun FPGA détecté sur la chaîne JTAG.", "No FPGA detected on the JTAG chain.") : Explain(all, lastErr, code);
        } else if (op == FW_BACKUP) {
            ok = saved && !failed;
            res = ok ? Tx{ "Sauvegarde enregistrée : " + W2U(bakFinal), "Backup saved: " + W2U(bakFinal) } : Explain(all, failMsg.empty() ? lastErr : failMsg, code);
        } else if (op == FW_PROGRAM && bit) {
            ok = loaded && !failed;
            res = ok ? TX("Bitstream chargé en RAM : actif jusqu'à la prochaine coupure de courant.", "Bitstream loaded into RAM: active until the next power cycle.")
                     : Explain(all, failMsg.empty() ? lastErr : failMsg, code);
        } else if (op == FW_VERIFY) {
            if (verified && !failed) { ok = true; res = TX("Identique : la flash contient exactement ce firmware.", "Identical: the flash contains exactly this firmware."); }
            else if (tooBig) res = TX("Différent : le fichier est plus grand que la flash. Ce .bin ne correspond pas à cette carte : vérifiez le modèle FPGA et le fichier.",
                                      "Different: the file is larger than the flash. This .bin does not match this card: check the FPGA model and the file.");
            else if (mismatch) res = TX("Différent : la flash ne contient pas ce firmware. Le firmware installé n'est pas ce fichier .bin (ou il est abîmé). Flashez-le pour l'installer.",
                                        "Different: the flash does not contain this firmware. The installed firmware is not this .bin file (or it is damaged). Flash it to install it.");
            else res = Explain(all, failMsg.empty() ? lastErr : failMsg, code);
        } else if (op == FW_ERASE) {
            ok = erased && !failed;
            res = ok ? TX("Flash effacée. La carte n'a plus de firmware : flashez-en un avant de l'utiliser.", "Flash erased. The card has no firmware any more: flash one before using it.")
                     : Explain(all, failMsg.empty() ? lastErr : failMsg, code);
        } else {
            ok = verified && !failed;
            if (ok && rebooted && !noreboot) res = TX("Firmware écrit dans la flash et vérifié. Le FPGA a redémarré sur le nouveau firmware.", "Firmware written to flash and verified. The FPGA restarted on the new firmware.");
            else if (ok) res = TX("Opération réussie, mais le FPGA n'a pas redémarré : éteignez puis rallumez le PC cible.", "Operation succeeded, but the FPGA did not restart: power the target PC off and on again.");
            else if (tooBig) res = TX("Ce .bin ne correspond pas à cette carte : il est plus grand que la flash. Vérifiez le modèle FPGA et le fichier.",
                                      "This .bin does not match this card: it is larger than the flash. Check the FPGA model and the file.");
            else res = Explain(all, failMsg.empty() ? lastErr : failMsg, code);
        }
        Log(ok ? L_OK : L_ERR, ok ? T("Firmware : %s", "Firmware: %s") : T("Firmware : ÉCHEC — %s", "Firmware: FAILED — %s"), res.c());
        Toast(res.s(), !ok);
        std::lock_guard<std::mutex> l(g_fMx);
        g_f.running = false;
        g_f.hasResult = true;
        g_f.ok = ok;
        g_f.result = res;
        g_f.idcode = idcode;
        g_f.status = ok ? TX("Terminé", "Done") : TX("Échec.", "Failed.");
    }).detach();
    return true;
}
