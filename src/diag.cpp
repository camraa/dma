// diag.cpp - diagnostic DMA : vérifie toute la chaîne et propose les solutions dans le bon ordre
#include "diag.h"
#include <shellapi.h>
#include <random>
#include <algorithm>
#include <thread>

static std::mutex g_dMx;
static DiagResult g_d;

DiagResult DiagCopy() { std::lock_guard<std::mutex> l(g_dMx); return g_d; }

static const char* URL_FTDI = "https://ftdichip.com/drivers/d3xx-drivers/";
static const char* URL_WCH = "https://www.wch-ic.com/downloads/CH341PAR_EXE.html";
static const char* URL_VC = "https://aka.ms/vs/17/release/vc_redist.x64.exe";

enum { CAT_NONE, CAT_NOCARD, CAT_FILES, CAT_BUSY, CAT_CONNECT, CAT_MEMREAD, CAT_OTHER };

struct Builder {
    DiagResult r;
    int cat = CAT_NONE;
    std::vector<Fix> sim, files, card, driver, busy, conn, mem, ch341;
    void Add(int lvl, Tx title, Tx detail, int category = CAT_OTHER) {
        r.checks.push_back({ lvl, std::move(title), std::move(detail) });
        if (lvl == CK_ERR) { r.errors++; if (cat == CAT_NONE) cat = category; }
        if (lvl == CK_WARN) r.warns++;
        std::lock_guard<std::mutex> l(g_dMx);
        g_d.checks = r.checks;                  // affichage au fil de l'eau
    }
};

static Fix F(Tx text, Tx button = {}, int action = ACT_NONE, std::string arg = {}) {
    return Fix{ std::move(text), std::move(button), action, std::move(arg) };
}

static std::wstring DirOf(const std::wstring& p) {
    size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"." : p.substr(0, k);
}

static void DiagRun() {
    Builder b;
    b.r.running = true;
    b.r.date = NowStamp();
    { std::lock_guard<std::mutex> l(g_dMx); g_d = b.r; }
    const char* modeName = g_set.mode == MODE_SIM ? T("simulation", "simulation") : g_set.mode == MODE_CUSTOM ? T("personnalisé", "custom") : "FPGA";
    Log(L_INFO, T("Diagnostic démarré (%s).", "Diagnostics started (%s)."), modeName);

    // 0. Mode simulation
    if (g_set.mode == MODE_SIM) {
        b.Add(CK_WARN, TX("Mode simulation actif", "Simulation mode enabled"),
              TX("Aucune carte réelle n'est utilisée : les mesures affichées sont fictives.", "No real card is used: the displayed results are fake."));
        b.sim.push_back(F(TX("Dans Appareil, choisissez « FPGA » pour tester votre vraie carte DMA.", "In Device, choose \"FPGA\" to test your real DMA card."),
                          TX("Utiliser la carte FPGA", "Use the FPGA card"), ACT_USE_FPGA));
    }

    // 1. Fichiers LeechCore
    std::wstring dll = ResolveDll();
    std::wstring builtin = BuiltinLibDir() + L"\\leechcore.dll";
    bool filesOk = true;
    auto builtinFix = [&] {
        if (FileExists(builtin) && LowerW(dll) != LowerW(builtin))
            b.files.insert(b.files.begin(), F(TX("Utilisez la leechcore.dll intégrée à Camra's Tool (LeechCore 2.16, signée par Ulf Frisk, avec FTD3XX.dll et vcruntime140.dll).",
                                                 "Use the leechcore.dll built into Camra's Tool (LeechCore 2.16, signed by Ulf Frisk, with FTD3XX.dll and vcruntime140.dll)."),
                                              TX("Utiliser la DLL intégrée", "Use built-in DLL"), ACT_BUILTIN_DLL));
    };
    if (dll.empty()) {
        filesOk = false;
        b.Add(CK_ERR, TX("leechcore.dll introuvable", "leechcore.dll not found"),
              TX("Aucun chemin indiqué et aucune DLL trouvée à côté de Camra's Tool.exe.", "No path set and no DLL found next to Camra's Tool.exe."), CAT_FILES);
    } else if (!FileExists(dll)) {
        filesOk = false;
        b.Add(CK_ERR, TX("leechcore.dll introuvable", "leechcore.dll not found"),
              TX("Le fichier n'existe pas : %s", "The file does not exist: %s", W2U(dll).c_str()), CAT_FILES);
        b.files.push_back(F(TX("Corrigez le chemin de leechcore.dll dans Appareil (bouton « … »).", "Fix the leechcore.dll path in Device (\"…\" button)."),
                            TX("Appareil", "Device"), ACT_DEVICE));
    } else {
        int arch = PeArch(dll);
        if (arch == 32) {
            filesOk = false;
            b.Add(CK_ERR, TX("leechcore.dll en 32 bits", "leechcore.dll is 32-bit"),
                  TX("Camra's Tool est une application 64 bits : %s", "Camra's Tool is a 64-bit app: %s", W2U(dll).c_str()), CAT_FILES);
            b.files.push_back(F(TX("Utilisez la version 64 bits (x64) de leechcore.dll et de FTD3XX.dll.", "Use the 64-bit (x64) version of leechcore.dll and FTD3XX.dll.")));
        } else if (arch != 64) {
            filesOk = false;
            b.Add(CK_ERR, TX("leechcore.dll illisible", "leechcore.dll unreadable"),
                  TX("Le fichier n'est pas une DLL Windows valide : %s", "The file is not a valid Windows DLL: %s", W2U(dll).c_str()), CAT_FILES);
        } else {
            b.Add(CK_OK, TX("leechcore.dll trouvée", "leechcore.dll found"), Tx{ W2U(dll), W2U(dll) });
        }
    }
    std::wstring dir = DirOf(dll);
    if (filesOk) {
        std::wstring ftd = dir + L"\\FTD3XX.dll";
        int a = PeArch(ftd);
        if (a == 64) b.Add(CK_OK, TX("FTD3XX.dll présent", "FTD3XX.dll present"), TX("Pilote utilisateur FTDI à côté de leechcore.dll.", "FTDI user-mode driver next to leechcore.dll."));
        else if (a == 32 || a == 0) {
            filesOk = false;
            b.Add(CK_ERR, TX("FTD3XX.dll incompatible", "FTD3XX.dll incompatible"), TX("La DLL n'est pas en 64 bits : %s", "The DLL is not 64-bit: %s", W2U(ftd).c_str()), CAT_FILES);
            b.files.push_back(F(TX("Remplacez FTD3XX.dll par sa version x64.", "Replace FTD3XX.dll with its x64 version.")));
        } else {
            filesOk = false;
            b.Add(CK_ERR, TX("FTD3XX.dll manquant", "FTD3XX.dll missing"),
                  TX("Requis par LeechCore pour parler à la carte en USB. Attendu ici : %s", "Required by LeechCore to talk to the card over USB. Expected here: %s", W2U(ftd).c_str()), CAT_FILES);
            b.files.push_back(F(TX("Copiez FTD3XX.dll (x64) dans le même dossier que leechcore.dll : %s", "Copy FTD3XX.dll (x64) into the same folder as leechcore.dll: %s", W2U(dir).c_str())));
        }
        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        if (FileExists(dir + L"\\vcruntime140.dll") || FileExists(std::wstring(sys) + L"\\vcruntime140.dll"))
            b.Add(CK_OK, TX("Runtime Visual C++ présent", "Visual C++ runtime present"), TX("vcruntime140.dll disponible.", "vcruntime140.dll available."));
        else {
            filesOk = false;
            b.Add(CK_ERR, TX("Runtime Visual C++ absent", "Visual C++ runtime missing"),
                  TX("vcruntime140.dll est introuvable : leechcore.dll ne peut pas se charger.", "vcruntime140.dll not found: leechcore.dll cannot load."), CAT_FILES);
            b.files.push_back(F(TX("Installez « Visual C++ Redistributable 2015-2022 x64 » (ou copiez vcruntime140.dll à côté de leechcore.dll).",
                                   "Install \"Visual C++ Redistributable 2015-2022 x64\" (or copy vcruntime140.dll next to leechcore.dll)."),
                                TX("Télécharger VC++ x64", "Download VC++ x64"), ACT_URL, URL_VC));
        }
    }
    if (filesOk) {
        HMODULE m = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (m) b.Add(CK_OK, TX("leechcore.dll se charge", "leechcore.dll loads"), TX("Toutes ses dépendances sont présentes.", "All its dependencies are present."));
        else {
            DWORD e = GetLastError();
            filesOk = false;
            const char* sfr = e == 126 ? " : une dépendance manque (FTD3XX.dll, vcruntime140.dll…)." : e == 193 ? " : fichier corrompu ou mauvaise architecture." : ".";
            const char* sen = e == 126 ? ": a dependency is missing (FTD3XX.dll, vcruntime140.dll…)." : e == 193 ? ": corrupted file or wrong architecture." : ".";
            b.Add(CK_ERR, TX("leechcore.dll ne se charge pas", "leechcore.dll does not load"), Tx{ Fmt("Erreur Windows %lu%s", e, sfr), Fmt("Windows error %lu%s", e, sen) }, CAT_FILES);
            b.files.push_back(F(TX("Placez ensemble dans un même dossier : leechcore.dll, FTD3XX.dll et vcruntime140.dll (version x64).",
                                   "Put these together in one folder: leechcore.dll, FTD3XX.dll and vcruntime140.dll (x64).")));
        }
    }
    if (!filesOk) builtinFix();

    // 2. Pilote FTDI D3XX : actif, ou prêt dans le magasin de pilotes (s'active au branchement)
    DriverState fd = FtdiDriver();
    if (fd.active)
        b.Add(CK_OK, TX("Pilote FTDI D3XX installé", "FTDI D3XX driver installed"),
              TX("Pilote USB de la puce FT601 de la carte (FTDIBUS3%s%s).", "USB driver for the card's FT601 chip (FTDIBUS3%s%s).", fd.version.empty() ? "" : " v", fd.version.c_str()));
    else if (fd.staged)
        b.Add(CK_OK, TX("Pilote FTDI D3XX prêt", "FTDI D3XX driver ready"),
              TX("Déjà présent dans Windows (%s%s%s) : il s'active tout seul au premier branchement de la carte. Rien à installer.",
                 "Already in Windows (%s%s%s): it activates by itself the first time the card is plugged in. Nothing to install.",
                 fd.inf.c_str(), fd.version.empty() ? "" : " v", fd.version.c_str()));
    else {
        b.Add(CK_ERR, TX("Pilote FTDI D3XX non installé", "FTDI D3XX driver not installed"),
              TX("Sans ce pilote, Windows ne peut pas dialoguer avec la puce FT601 de la carte DMA.", "Without this driver, Windows cannot talk to the DMA card's FT601 chip."));
        b.driver.push_back(F(TX("Installez le pilote FTDI D3XX intégré à Camra's Tool (v1.3.0.8, signé Microsoft WHQL) : un clic, acceptez la demande administrateur, puis rebranchez la carte. Aucun téléchargement.",
                                "Install the FTDI D3XX driver built into Camra's Tool (v1.3.0.8, Microsoft WHQL-signed): one click, accept the administrator prompt, then replug the card. No download needed."),
                             TX("Installer le pilote FTDI", "Install the FTDI driver"), ACT_INSTALL_FTDI));
        b.driver.push_back(F(TX("Ou installez la dernière version du pilote FTDI D3XX (Windows x64) : téléchargez-la sur ftdichip.com, décompressez l'archive, puis clic droit sur « ftdibus3.inf » → Installer. Rebranchez ensuite la carte.",
                                "Or install the latest FTDI D3XX driver (Windows x64): download it from ftdichip.com, extract the archive, then right-click \"ftdibus3.inf\" → Install. Then replug the card."),
                             TX("Ouvrir la page FTDI D3XX", "Open the FTDI D3XX page"), ACT_URL, URL_FTDI));
    }

    // 3. Pilote WCH CH341/CH347 (flash du firmware uniquement)
    DriverState cd = Ch341Driver();
    if (cd.active)
        b.Add(CK_OK, TX("Pilote WCH CH341/CH347 installé", "WCH CH341/CH347 driver installed"),
              TX("Interface de flash du firmware (%s%s%s).", "Firmware flashing interface (%s%s%s).", cd.service.c_str(), cd.version.empty() ? "" : " v", cd.version.c_str()));
    else if (cd.staged)
        b.Add(CK_OK, TX("Pilote WCH CH341/CH347 prêt", "WCH CH341/CH347 driver ready"),
              TX("Déjà présent dans Windows (%s%s%s) : il s'active au branchement du port de flash (JTAG). Sert uniquement à flasher le firmware.",
                 "Already in Windows (%s%s%s): it activates when the flashing (JTAG) port is plugged in. Only used to flash the firmware.",
                 cd.inf.c_str(), cd.version.empty() ? "" : " v", cd.version.c_str()));
    else {
        b.Add(CK_WARN, TX("Pilote WCH CH341/CH347 non installé", "WCH CH341/CH347 driver not installed"),
              TX("Sert à flasher le firmware de la carte (puce CH347/CH341). Inutile pour le speedtest, indispensable pour un reflash.",
                 "Used to flash the card's firmware (CH347/CH341 chip). Not needed for the speedtest, required for a reflash."));
        b.ch341.push_back(F(TX("Installez le pilote WCH CH341/CH347 intégré à Camra's Tool (CH341WDM v2.4.2023.10, signé Microsoft WHQL) : un clic, puis acceptez la demande administrateur. Aucun téléchargement.",
                               "Install the WCH CH341/CH347 driver built into Camra's Tool (CH341WDM v2.4.2023.10, Microsoft WHQL-signed): one click, then accept the administrator prompt. No download needed."),
                            TX("Installer le pilote CH341", "Install CH341 driver"), ACT_INSTALL_CH341));
    }

    // 4. Port de flash CH347
    Ch347 ch = FindCh347();
    if (ch.present) {
        if (ch.problem) {
            b.Add(CK_WARN, TX("Puce de flash CH347 sans pilote", "CH347 flash chip without driver"),
                  TX("Une interface WCH CH341/CH347 est branchée mais Windows signale le code %lu.", "A WCH CH341/CH347 interface is plugged in but Windows reports code %lu.", ch.problem));
            if (b.ch341.empty())
                b.ch341.push_back(F(TX("Installez le pilote WCH CH341/CH347 intégré à Camra's Tool : un clic, puis acceptez la demande administrateur.",
                                       "Install the WCH CH341/CH347 driver built into Camra's Tool: one click, then accept the administrator prompt."),
                                    TX("Installer le pilote CH341", "Install CH341 driver"), ACT_INSTALL_CH341));
        } else
            b.Add(CK_INFO, TX("Interface de flash CH347 détectée", "CH347 flash interface detected"),
                  TX("Le port de programmation (JTAG/SPI) de la carte est branché et reconnu.", "The card's programming port (JTAG/SPI) is plugged in and recognized."));
    }

    // 5. Carte en USB (pont FTDI FT601/FT600)
    bool cardOk = false, cardProblem = false;
    std::vector<UsbDev> cards = FindFt60x(false);
    if (!cards.empty()) {
        std::sort(cards.begin(), cards.end(), [](const UsbDev& a, const UsbDev& c) { return a.problem < c.problem; });
        const UsbDev& u = cards[0];
        if (u.problem) {
            cardProblem = true;
            const char* sfr = u.problem == 28 ? " (aucun pilote installé)" : u.problem == 10 ? " (le périphérique ne démarre pas)" : "";
            const char* sen = u.problem == 28 ? " (no driver installed)" : u.problem == 10 ? " (device cannot start)" : "";
            b.Add(CK_ERR, TX("Pilote de la carte en erreur", "Card driver error"),
                  Tx{ Fmt("%s détectée, mais Windows signale le code %lu%s.", u.name.c_str(), u.problem, sfr),
                      Fmt("%s detected, but Windows reports code %lu%s.", u.name.c_str(), u.problem, sen) }, CAT_NOCARD);
            if (u.problem != 28 || fd.active || fd.staged)
                b.card.push_back(F(TX("Dans le Gestionnaire de périphériques, désinstallez « FTDI FT601 USB 3.0 Bridge Device » puis rebranchez la carte.",
                                      "In Device Manager, uninstall \"FTDI FT601 USB 3.0 Bridge Device\" and replug the card."),
                                   TX("Gestionnaire de périphériques", "Device Manager"), ACT_DEVMGMT));
            if (u.problem == 28 && !fd.active && b.driver.empty())
                b.driver.push_back(F(TX("Installez le pilote FTDI D3XX intégré à Camra's Tool : un clic, acceptez la demande administrateur, puis rebranchez la carte.",
                                        "Install the FTDI D3XX driver built into Camra's Tool: one click, accept the administrator prompt, then replug the card."),
                                     TX("Installer le pilote FTDI", "Install the FTDI driver"), ACT_INSTALL_FTDI));
        } else {
            cardOk = true;
            b.Add(CK_OK, TX("Carte détectée en USB", "Card detected over USB"),
                  TX("%s%s", "%s%s", u.name.c_str(), cards.size() > 1 ? T(" (plusieurs cartes branchées)", " (several cards plugged in)") : ""));
            if (u.speed == 3)
                b.Add(CK_OK, TX("Lien USB 3.0 (SuperSpeed)", "USB 3.0 link (SuperSpeed)"),
                      TX("La carte communique à 5 Gb/s (port %d de son hub) : le débit maximal est possible.", "The card talks at 5 Gb/s (port %d of its hub): full throughput is possible.", u.port));
            else if (u.speed >= 0) {
                Tx sp = UsbSpeedText(u.speed);
                b.Add(CK_WARN, TX("Carte branchée en USB 2.0", "Card connected over USB 2.0"),
                      Tx{ Fmt("La carte fonctionne en %s alors qu'elle sait faire de l'USB 3 : le débit est limité à environ 30 à 40 Mo/s.", sp.fr.c_str()),
                          Fmt("The card runs at %s although it supports USB 3: throughput is limited to about 30 to 40 MB/s.", sp.en.c_str()) });
                b.card.push_back(F(TX("Branchez la carte sur un port USB 3 (souvent bleu ou marqué « SS ») à l'arrière du PC de test, directement sur la carte mère.",
                                      "Plug the card into a USB 3 port (often blue or marked \"SS\") at the back of the test PC, directly on the motherboard.")));
                b.card.push_back(F(TX("Utilisez un câble USB-C compatible USB 3 (5 Gb/s) : beaucoup de câbles de charge sont limités à l'USB 2.0.",
                                      "Use a USB 3 capable USB-C cable (5 Gb/s): many charging cables are limited to USB 2.0.")));
                b.card.push_back(F(TX("Évitez les hubs, rallonges et ports en façade, puis relancez le diagnostic.", "Avoid hubs, extension cables and front-panel ports, then run the diagnostics again.")));
            }
        }
    } else {
        // Rien sur le port USB : c'est un problème de branchement ou d'alimentation, pas de pilote
        bool seen = !FindFt60x(true).empty();
        if (seen)
            b.Add(CK_ERR, TX("Carte DMA non connectée", "DMA card not connected"),
                  TX("Une carte FTDI FT60x a déjà été branchée sur ce PC, mais elle n'est pas visible actuellement.",
                     "An FTDI FT60x card has been plugged into this PC before, but it is not visible right now."), CAT_NOCARD);
        else
            b.Add(CK_ERR, TX("Aucune carte DMA détectée", "No DMA card detected"),
                  TX("Windows ne voit aucun pont USB FTDI FT601/FT600 (VID 0403) sur ce PC. Le plus souvent, c'est le branchement ou l'alimentation de la carte : même sans pilote, une carte branchée apparaîtrait ici.",
                     "Windows sees no FTDI FT601/FT600 USB bridge (VID 0403) on this PC. It is usually the card's connection or power: even without a driver, a plugged-in card would show up here."), CAT_NOCARD);
        if (ch.present)
            b.card.push_back(F(TX("Seul le port de flash (CH347) de la carte est branché : branchez aussi son port DATA (USB 3, puce FT601) sur ce PC.",
                                  "Only the card's flashing port (CH347) is plugged in: also plug its DATA port (USB 3, FT601 chip) into this PC.")));
        b.card.push_back(F(TX("Le PC cible doit être allumé : la carte est alimentée par son slot PCIe/M.2. Vérifiez que sa LED s'allume.",
                              "The target PC must be powered on: the card is powered by its PCIe/M.2 slot. Check that its LED lights up.")));
        b.card.push_back(F(TX("Vérifiez que le câble USB relie le port DATA de la carte DMA à CE PC (le PC de test), pas au PC cible ni au port de flash (JTAG).",
                              "Make sure the USB cable connects the DMA card's DATA port to THIS PC (the test PC), not the target PC nor the flashing (JTAG) port.")));
        b.card.push_back(F(TX("Utilisez un câble USB-C de données (certains câbles ne font que la charge) branché sur un port USB 3 à l'arrière de la carte mère, sans hub ni rallonge.",
                              "Use a USB-C data cable (some cables only charge) plugged into a USB 3 port on the back of the motherboard, without a hub or extension.")));
        b.card.push_back(F(TX("Ouvrez le Gestionnaire de périphériques : la carte doit apparaître comme « FTDI FT601 USB 3.0 Bridge Device ». Débranchez puis rebranchez le câble pour la voir apparaître.",
                              "Open Device Manager: the card should show up as \"FTDI FT601 USB 3.0 Bridge Device\". Unplug and replug the cable to see it appear."),
                           TX("Gestionnaire de périphériques", "Device Manager"), ACT_DEVMGMT));
    }

    // 6. Carte déjà utilisée par un autre logiciel
    bool busy = false;
    DeviceInfo dinfo = DevInfo();
    bool ownOpen = dinfo.connected && !dinfo.simulated;
    if (cardOk && !ownOpen) {
        std::vector<std::string> procs = ProcessesWithModule(L"FTD3XX.dll");
        if (!procs.empty()) {
            busy = true;
            std::string list;
            for (auto& p : procs) list += (list.empty() ? "" : ", ") + p;
            b.Add(CK_WARN, TX("Carte peut-être utilisée ailleurs", "Card may be in use elsewhere"),
                  TX("Ces programmes ont chargé FTD3XX.dll : %s", "These programs have loaded FTD3XX.dll: %s", list.c_str()));
            b.busy.push_back(F(TX("Fermez %s : une seule application à la fois peut utiliser la carte DMA.", "Close %s: only one app at a time can use the DMA card.", list.c_str())));
        }
    }

    // 7. Ouverture par LeechCore (journal très verbeux)
    if (cardOk && filesOk && g_set.mode != MODE_SIM) {
        bool ok = ownOpen;
        if (!ok) {
            std::string dev = DevDeviceString();
            Log(L_INFO, T("Diagnostic : ouverture de « %s » (journal très verbeux)…", "Diagnostics: opening \"%s\" (very verbose log)…"), dev.c_str());
            LcMessagesClear();
            ok = DevConnect(true);
            // Messages utiles seulement : les vidages de registres restent dans le Journal
            for (auto& m : LcMessages()) {
                std::string lo = Lower(m);
                if (lo.find("device:") != std::string::npos || lo.find("error") != std::string::npos || lo.find("fail") != std::string::npos) b.r.lc.push_back(m);
            }
        }
        dinfo = DevInfo();
        if (ok) {
            b.Add(CK_OK, TX("Carte ouverte par LeechCore", "Card opened by LeechCore"),
                  TX("FPGA %s · firmware %s · PCIe %s · mémoire max %s", "FPGA %s · firmware %s · PCIe %s · max memory %s",
                     dinfo.fpga.c(), dinfo.firmware.c(), dinfo.pcie.c(), dinfo.maxMem.c()));
        } else {
            std::string last;
            for (auto it = b.r.lc.rbegin(); it != b.r.lc.rend(); ++it)
                if (Lower(*it).find("fail") != std::string::npos || Lower(*it).find("error") != std::string::npos) { last = *it; break; }
            std::string dev = DevDeviceString();
            b.Add(CK_ERR, TX("Ouverture de la carte impossible", "Cannot open the card"),
                  Tx{ Fmt("LeechCore ne parvient pas à dialoguer avec la carte via « %s ».%s%s", dev.c_str(), last.empty() ? "" : " Message : ", last.c_str()),
                      Fmt("LeechCore cannot talk to the card via \"%s\".%s%s", dev.c_str(), last.empty() ? "" : " Message: ", last.c_str()) },
                  busy ? CAT_BUSY : CAT_CONNECT);
            b.conn.push_back(F(TX("Débranchez puis rebranchez le câble USB de la carte, attendez 5 secondes et relancez le diagnostic.",
                                  "Unplug and replug the card's USB cable, wait 5 seconds and run the diagnostics again.")));
            b.conn.push_back(F(TX("Dans Appareil, essayez un autre algorithme FPGA : « Async Tiny » puis « Old Tiny » pour les cartes capricieuses.",
                                  "In Device, try another FPGA algorithm: \"Async Tiny\" then \"Old Tiny\" for picky cards."),
                               TX("Appareil", "Device"), ACT_DEVICE));
            b.conn.push_back(F(TX("Redémarrez le PC cible avec la carte en place, puis relancez le diagnostic.", "Restart the target PC with the card installed, then run the diagnostics again.")));
            b.conn.push_back(F(TX("Si rien n'y fait, le firmware de la carte est peut-être corrompu ou inadapté : reflashez-le (onglet Firmware) avec le fichier fourni par le vendeur.",
                                  "If nothing helps, the card's firmware may be corrupted or unsuitable: reflash it (Firmware tab) with the file supplied by the vendor.")));
        }
    }

    // 8. Lecture de la RAM du PC cible
    dinfo = DevInfo();
    if (dinfo.connected && !dinfo.simulated) {
        Tx err;
        std::vector<Range> ranges = TestRanges(&err);
        if (ranges.empty()) {
            b.Add(CK_ERR, TX("Test de lecture impossible", "Read test not possible"), err, CAT_MEMREAD);
            b.mem.push_back(F(TX("Dans Réglages, remettez la zone par défaut : adresse 0x10000000, taille 1024 Mo.", "In Settings, restore the default range: address 0x10000000, size 1024 MB."),
                              TX("Réglages", "Settings"), ACT_SETTINGS));
        } else {
            std::mt19937_64 rng(GetTickCount64());
            u64 total = 0;
            for (auto& r : ranges) total += r.size;
            const int N = 64, ROUNDS = 4;
            int ok = 0;
            std::vector<u64> pas(N);
            for (int k = 0; k < ROUNDS; k++) {
                for (int i = 0; i < N; i++) {
                    u64 x = std::uniform_int_distribution<u64>(0, total - 1)(rng);
                    for (auto& r : ranges) { if (x < r.size) { pas[(size_t)i] = (r.base + x) & ~0xFFFULL; break; } x -= r.size; }
                }
                double us;
                ok += DevReadScatter(N, pas.data(), &us);
            }
            int tot = N * ROUNDS;
            if (ok == 0) {
                b.Add(CK_ERR, TX("Aucune lecture mémoire ne réussit", "No memory read succeeds"),
                      TX("La carte répond en USB mais ne lit pas la RAM du PC cible (0/%d pages).", "The card answers over USB but cannot read the target PC's RAM (0/%d pages).", tot), CAT_MEMREAD);
                b.mem.push_back(F(TX("Vérifiez que le PC cible est démarré sous Windows (pas en veille, ni bloqué dans le BIOS).", "Make sure the target PC is booted into Windows (not asleep, not stuck in the BIOS).")));
                b.mem.push_back(F(TX("Dans le BIOS du PC cible, désactivez VT-d / AMD-Vi (IOMMU) ; dans Windows, la protection DMA du noyau (Kernel DMA Protection) doit être inactive.",
                                     "In the target PC's BIOS, disable VT-d / AMD-Vi (IOMMU); in Windows, Kernel DMA Protection must be off.")));
                b.mem.push_back(F(TX("Réinsérez la carte dans un autre slot PCIe/M.2 du PC cible, de préférence relié au processeur.", "Reseat the card in another PCIe/M.2 slot of the target PC, preferably one wired to the CPU.")));
                b.mem.push_back(F(TX("Le firmware doit correspondre à votre carte mère : un firmware inadapté bloque les lectures.", "The firmware must match your motherboard: an unsuitable firmware blocks reads.")));
            } else if (ok < tot * 99 / 100) {
                b.Add(CK_WARN, TX("Lectures instables", "Unstable reads"), TX("%d/%d pages lues : le lien est fragile.", "%d/%d pages read: the link is fragile.", ok, tot));
                b.mem.push_back(F(TX("Changez de câble USB et de port USB 3, puis essayez l'algorithme FPGA « Async Tiny ».", "Change the USB cable and USB 3 port, then try the \"Async Tiny\" FPGA algorithm.")));
                b.mem.push_back(F(TX("Vérifiez que la carte est bien enfoncée dans son slot sur le PC cible.", "Make sure the card is fully seated in its slot on the target PC.")));
            } else
                b.Add(CK_OK, TX("Lecture mémoire OK", "Memory read OK"), TX("%d/%d pages lues dans la zone de test.", "%d/%d pages read in the test range.", ok, tot));
        }
    }

    DevRelease();                            // la carte redevient disponible pour les autres logiciels

    // Solutions dans l'ordre : ce qui bloque d'abord. Sans carte visible en USB, le branchement passe avant les pilotes.
    std::vector<Fix>& out = b.r.fixes;
    auto take = [&](std::vector<Fix>& v) { out.insert(out.end(), v.begin(), v.end()); };
    take(b.sim);
    take(b.files);
    if (cardProblem) { take(b.driver); take(b.card); }
    else { take(b.card); take(b.driver); }
    take(b.busy);
    take(b.conn);
    take(b.mem);
    take(b.ch341);

    int n = (int)b.r.checks.size();
    b.r.summary = Tx{ Fmt("%d vérifications · %d erreur%s · %d avertissement%s", n, b.r.errors, b.r.errors > 1 ? "s" : "", b.r.warns, b.r.warns > 1 ? "s" : ""),
                      Fmt("%d checks · %d error%s · %d warning%s", n, b.r.errors, b.r.errors != 1 ? "s" : "", b.r.warns, b.r.warns != 1 ? "s" : "") };
    if (b.r.errors) {
        switch (b.cat) {
        case CAT_NOCARD: b.r.headline = TX("Carte DMA non détectée", "DMA card not detected"); break;
        case CAT_FILES: b.r.headline = TX("Fichiers LeechCore manquants ou invalides", "LeechCore files missing or invalid"); break;
        case CAT_BUSY: b.r.headline = TX("Carte occupée par un autre logiciel", "Card busy in another program"); break;
        case CAT_CONNECT: b.r.headline = TX("Connexion à la carte impossible", "Cannot connect to the card"); break;
        case CAT_MEMREAD: b.r.headline = TX("La carte ne lit pas la mémoire", "The card cannot read memory"); break;
        default: b.r.headline = TX("Problème détecté", "Problem detected");
        }
    } else if (b.r.warns) b.r.headline = TX("Fonctionnel, avec des avertissements", "Working, with warnings");
    else b.r.headline = TX("Tout est opérationnel", "Everything is working");
    b.r.running = false;
    b.r.done = true;
    Log(b.r.errors ? L_ERR : b.r.warns ? L_WARN : L_OK, T("Diagnostic terminé : %s.", "Diagnostics finished: %s."), b.r.headline.c());
    std::lock_guard<std::mutex> l(g_dMx);
    g_d = b.r;
}

void DiagStart() {
    if (!StartJob(DiagRun)) Toast(T("Un test est en cours.", "A test is running."), true);
}

void DiagAction(const Fix& f) {
    switch (f.action) {
    case ACT_INSTALL_FTDI: InstallDriverAsync(0); break;
    case ACT_INSTALL_CH341: InstallDriverAsync(1); break;
    case ACT_URL: OpenUrl(f.arg.c_str()); break;
    case ACT_DEVMGMT: ShellExecuteW(nullptr, L"open", L"devmgmt.msc", nullptr, nullptr, SW_SHOWNORMAL); break;
    case ACT_BUILTIN_DLL:
        g_set.dll = BuiltinLibDir() + L"\\leechcore.dll";
        SaveSettings();
        Toast(T("Dossier LeechCore sélectionné.", "LeechCore folder selected."));
        DiagStart();
        break;
    case ACT_USE_FPGA:
        g_set.mode = MODE_FPGA;
        SaveSettings();
        StartJob([] { DevDisconnect(); });
        std::thread([] { while (JobRunning()) Sleep(20); DiagStart(); }).detach();
        break;
    case ACT_SETTINGS: g_page = PG_SETTINGS; break;
    case ACT_DEVICE: g_page = PG_DEVICE; break;
    }
}

// Masque le nom d'utilisateur dans les chemins (C:\Users\xxx\ -> C:\Users\…\)
static std::string Anon(std::string s) {
    std::string low = Lower(s);
    size_t p = 0;
    while ((p = low.find("\\users\\", p)) != std::string::npos) {
        size_t a = p + 7, e = low.find('\\', a);
        if (e == std::string::npos) break;
        s.replace(a, e - a, "…");
        low.replace(a, e - a, "...");
        p = a + 3;
    }
    return s;
}

std::string DiagReport() {
    DiagResult d = DiagCopy();
    std::string s = Fmt(T("Camra's Tool %s — Diagnostic DMA\r\n%s\r\n%s\r\n\r\nVérifications :\r\n", "Camra's Tool %s — DMA diagnostics\r\n%s\r\n%s\r\n\r\nChecks:\r\n"),
                        APP_VERSION, d.headline.c(), d.summary.c());
    for (auto& c : d.checks) {
        const char* lv = c.lvl == CK_OK ? "OK" : c.lvl == CK_ERR ? T("ERREUR", "ERROR") : c.lvl == CK_WARN ? T("ATTENTION", "WARNING") : "INFO";
        s += Fmt("  [%s] %s — %s\r\n", lv, c.title.c(), c.detail.c());
    }
    if (!d.fixes.empty()) {
        s += T("\r\nSolutions à tester :\r\n", "\r\nFixes to try:\r\n");
        for (size_t i = 0; i < d.fixes.size(); i++) s += Fmt("  %d. %s\r\n", (int)i + 1, d.fixes[i].text.c());
    }
    if (!d.lc.empty()) {
        s += T("\r\nMessages LeechCore :\r\n", "\r\nLeechCore messages:\r\n");
        for (auto& m : d.lc) s += "  " + m + "\r\n";
    }
    return Anon(s);
}

std::string DiagDiscord() {
    DiagResult d = DiagCopy();
    const char* icon = d.errors ? "❌" : d.warns ? "⚠️" : "✅";
    std::string s = Fmt("**Camra's Tool — %s**\n%s %s\n_%s_\n", T("Diagnostic DMA", "DMA diagnostics"), icon, d.headline.c(), d.summary.c());
    for (auto& c : d.checks) {
        const char* e = c.lvl == CK_OK ? "✅" : c.lvl == CK_ERR ? "❌" : c.lvl == CK_WARN ? "⚠️" : "ℹ️";
        s += c.detail.fr.empty() ? Fmt("%s **%s**\n", e, c.title.c()) : Fmt("%s **%s** — %s\n", e, c.title.c(), c.detail.c());
    }
    if (!d.fixes.empty()) {
        s += T("\n**Solutions à tester :**\n", "\n**Fixes to try:**\n");
        for (size_t i = 0; i < d.fixes.size() && i < 6; i++) s += Fmt("%d. %s\n", (int)i + 1, d.fixes[i].text.c());
    }
    s = Anon(s);
    if (s.size() > 1990) {
        size_t n = 1985;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;   // ne coupe pas un caractère UTF-8
        s = s.substr(0, n) + "…";
    }
    return s;
}
