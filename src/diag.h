// diag.h - diagnostic de la chaîne DMA : fichiers, pilotes, USB, connexion, lecture mémoire
#pragma once
#include "app.h"

enum { CK_OK, CK_ERR, CK_WARN, CK_INFO };
enum { ACT_NONE, ACT_INSTALL_FTDI, ACT_INSTALL_CH341, ACT_URL, ACT_DEVMGMT, ACT_BUILTIN_DLL, ACT_USE_FPGA, ACT_SETTINGS, ACT_DEVICE };

struct Check { int lvl; Tx title, detail; };
struct Fix { Tx text, button; int action = ACT_NONE; std::string arg; };

struct DiagResult {
    bool running = false, done = false;
    std::string date;
    int errors = 0, warns = 0;
    Tx headline, summary;
    std::vector<Check> checks;
    std::vector<Fix> fixes;
    std::vector<std::string> lc;             // messages LeechCore de la tentative de connexion
};

DiagResult DiagCopy();
void DiagStart();
void DiagAction(const Fix& f);
std::string DiagReport();
std::string DiagDiscord();
