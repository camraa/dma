// firmware.h - flash du firmware de la carte DMA via le CH347 (OpenOCD intégré)
#pragma once
#include "app.h"

enum { FW_DETECT, FW_PROGRAM, FW_VERIFY, FW_ERASE, FW_BACKUP };

struct FpgaModel { uint32_t idcode; const char* name; };
extern const FpgaModel FPGA_MODELS[];
extern const int FPGA_MODEL_COUNT;
uint32_t FirmwareIdcode(const std::wstring& file);   // IDCODE inscrit dans un .bit/.bin Xilinx, 0 si absent
const FpgaModel* ModelById(uint32_t id);
const FpgaModel* FindModel(const char* name);

struct FwState {
    bool running = false;
    int op = -1;
    double t0 = 0;
    Tx status;
    bool hasResult = false, ok = false;
    Tx result;
    std::vector<std::string> console;
    uint32_t idcode = 0;
    std::string detected;
};

FwState FwCopy();
bool FwStart(int op);                        // vérifie les prérequis puis lance OpenOCD en arrière-plan
void FwClearConsole();
Ch347 Ch347Cached();                         // état du CH347, rafraîchi en arrière-plan
