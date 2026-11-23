// sysinfo.cpp - périphériques USB, vitesse du lien, pilotes, processus, installation des pilotes
#include "app.h"
#include <initguid.h>
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <usbiodef.h>
#include <usbioctl.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <sddl.h>
#include <objbase.h>
#include <thread>
#include <algorithm>

// ---------------------------------------------------------------------------
//  Vitesse du lien USB : demandée au hub parent (port de la carte)
// ---------------------------------------------------------------------------
static int UsbSpeed(DEVINST dev, int port) {
    DEVINST parent;
    if (port <= 0 || CM_Get_Parent(&parent, dev, 0) != CR_SUCCESS) return -1;
    wchar_t pid[MAX_DEVICE_ID_LEN];
    if (CM_Get_Device_IDW(parent, pid, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) return -1;
    ULONG len = 0;
    if (CM_Get_Device_Interface_List_SizeW(&len, (LPGUID)&GUID_DEVINTERFACE_USB_HUB, pid, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || len < 2) return -1;
    std::vector<wchar_t> list(len);
    if (CM_Get_Device_Interface_ListW((LPGUID)&GUID_DEVINTERFACE_USB_HUB, pid, list.data(), len, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS) return -1;
    HANDLE hub = CreateFileW(list.data(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hub == INVALID_HANDLE_VALUE) return -1;
    int speed = -1;
    DWORD br = 0;
    // La structure finit par un tableau de taille variable : on réserve un tampon plus grand
    union { USB_NODE_CONNECTION_INFORMATION_EX ci; BYTE raw[sizeof(USB_NODE_CONNECTION_INFORMATION_EX) + 30 * sizeof(USB_PIPE_INFO)]; } ub;
    memset(&ub, 0, sizeof ub);
    USB_NODE_CONNECTION_INFORMATION_EX& ci = ub.ci;
    ci.ConnectionIndex = (ULONG)port;
    if (DeviceIoControl(hub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, &ub, sizeof ub, &ub, sizeof ub, &br, nullptr))
        speed = ci.Speed;
    USB_NODE_CONNECTION_INFORMATION_EX_V2 v2 = {};
    v2.ConnectionIndex = (ULONG)port;
    v2.Length = sizeof v2;
    v2.SupportedUsbProtocols.Usb300 = 1;
    if (DeviceIoControl(hub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2, &v2, sizeof v2, &v2, sizeof v2, &br, nullptr)) {
        if (v2.Flags.DeviceIsOperatingAtSuperSpeedOrHigher || v2.Flags.DeviceIsOperatingAtSuperSpeedPlusOrHigher) speed = 3;
    }
    CloseHandle(hub);
    return speed;
}

Tx UsbSpeedText(int s) {
    switch (s) {
    case 3: return Tx{ "USB 3.0 · 5 Gb/s", "USB 3.0 · 5 Gb/s" };
    case 2: return Tx{ "USB 2.0 · 480 Mb/s", "USB 2.0 · 480 Mb/s" };
    case 1: return Tx{ "USB 1.1 · 12 Mb/s", "USB 1.1 · 12 Mb/s" };
    case 0: return Tx{ "USB 1.0 · 1,5 Mb/s", "USB 1.0 · 1.5 Mb/s" };
    }
    return Tx{ "USB ?", "USB ?" };
}

// ---------------------------------------------------------------------------
//  Énumération des périphériques USB (présents ou déjà vus sur ce PC)
// ---------------------------------------------------------------------------
std::vector<UsbDev> FindUsb(const wchar_t* vidpid, bool includeAbsent, bool interfaces) {
    std::vector<UsbDev> out;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | (includeAbsent ? 0 : DIGCF_PRESENT));
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVINFO_DATA dd = { sizeof dd };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dd); i++) {
        wchar_t id[MAX_DEVICE_ID_LEN];
        if (!SetupDiGetDeviceInstanceIdW(set, &dd, id, MAX_DEVICE_ID_LEN, nullptr)) continue;
        std::wstring up = id;
        for (auto& c : up) c = (wchar_t)towupper(c);
        if (up.find(vidpid) == std::wstring::npos) continue;
        bool isIf = up.find(L"&MI_") != std::wstring::npos;
        if (isIf != interfaces) continue;
        UsbDev u;
        u.id = id;
        size_t k = up.find(L"PID_");
        if (k != std::wstring::npos) u.pid = (int)wcstol(up.c_str() + k + 4, nullptr, 16);
        wchar_t name[256] = {};
        if (!SetupDiGetDeviceRegistryPropertyW(set, &dd, SPDRP_FRIENDLYNAME, nullptr, (PBYTE)name, sizeof name, nullptr))
            SetupDiGetDeviceRegistryPropertyW(set, &dd, SPDRP_DEVICEDESC, nullptr, (PBYTE)name, sizeof name, nullptr);
        u.name = W2U(name);
        ULONG status = 0, problem = 0;
        u.present = CM_Get_DevNode_Status(&status, &problem, dd.DevInst, 0) == CR_SUCCESS;
        if (u.present && (status & DN_HAS_PROBLEM)) u.problem = problem ? problem : 1;
        DWORD addr = 0;
        if (SetupDiGetDeviceRegistryPropertyW(set, &dd, SPDRP_ADDRESS, nullptr, (PBYTE)&addr, sizeof addr, nullptr)) u.port = (int)addr;
        if (u.present && !interfaces) u.speed = UsbSpeed(dd.DevInst, u.port);
        out.push_back(u);
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

// Pont USB de la carte DMA : FTDI FT601 (PID 601F) ou FT600 (PID 601E)
std::vector<UsbDev> FindFt60x(bool includeAbsent) {
    // --nocard : simule l'absence de carte pour vérifier le diagnostic
    if (wcsstr(GetCommandLineW(), L"--nocard")) return {};
    std::vector<UsbDev> r = FindUsb(L"VID_0403&PID_601F", includeAbsent, false);
    for (auto& u : FindUsb(L"VID_0403&PID_601E", includeAbsent, false)) r.push_back(u);
    for (auto& u : r) {
        std::string n = Lower(u.name);
        if (u.name.empty() || n.find("composite") != std::string::npos || n.find("usb device") != std::string::npos ||
            n.find("inconnu") != std::string::npos || n.find("unknown") != std::string::npos)
            u.name = u.pid == 0x601E ? "FTDI FT600" : "FTDI FT601";
    }
    return r;
}

// Interface JTAG du CH347 (port de flash de la carte) : PID 55DB/55DD (interface 2) ou 55DE (interface 4)
Ch347 FindCh347() {
    Ch347 best;
    const wchar_t* pids[] = { L"VID_1A86&PID_55DD", L"VID_1A86&PID_55DE", L"VID_1A86&PID_55DB" };
    for (auto pid : pids) {
        for (auto& u : FindUsb(pid, false, true)) {
            std::wstring up = u.id;
            for (auto& c : up) c = (wchar_t)towupper(c);
            bool jtag = (u.pid == 0x55DE) ? up.find(L"&MI_04") != std::wstring::npos : up.find(L"&MI_02") != std::wstring::npos;
            if (!jtag || !u.present) continue;
            Ch347 c;
            c.present = true;
            c.pid = u.pid;
            c.problem = u.problem;
            c.name = Fmt("WCH CH347%s (VID 1A86 / PID %04X)", u.pid == 0x55DE ? "F" : "T", u.pid);
            if (!best.present || (best.problem && !c.problem)) best = c;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
//  Pilotes : actif (service installé) ou prêt (présent dans le magasin de pilotes Windows)
//  Windows ne crée le service qu'au premier branchement de la carte : un pilote seulement
//  « prêt » est donc normal tant que la carte n'a jamais été branchée sur ce PC.
// ---------------------------------------------------------------------------
std::string FileVersion(const std::wstring& path) {
    DWORD h = 0, n = GetFileVersionInfoSizeW(path.c_str(), &h);
    if (!n) return "";
    std::vector<BYTE> b(n);
    if (!GetFileVersionInfoW(path.c_str(), 0, n, b.data())) return "";
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(b.data(), L"\\", (void**)&fi, &len) || !fi) return "";
    return Fmt("%u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
}

static std::wstring WinDir() {
    wchar_t w[MAX_PATH];
    GetWindowsDirectoryW(w, MAX_PATH);
    return w;
}

static std::string InfVersion(const std::wstring& inf) {
    std::string raw;
    if (!ReadFileAll(inf, raw)) return "";
    std::string txt;
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE)
        txt = W2U(std::wstring((const wchar_t*)(raw.data() + 2), (raw.size() - 2) / 2));
    else txt = raw;
    std::string low = Lower(txt);
    size_t k = low.find("driverver");
    if (k == std::string::npos) return "";
    size_t c = low.find(',', k), e = low.find_first_of("\r\n;", k);
    if (c == std::string::npos || (e != std::string::npos && c > e)) return "";
    std::string v = txt.substr(c + 1, (e == std::string::npos ? txt.size() : e) - c - 1);
    v.erase(0, v.find_first_not_of(" \t"));
    v.erase(v.find_last_not_of(" \t") + 1);
    return v;
}

static DriverState CheckDriver(std::initializer_list<const wchar_t*> services, std::initializer_list<const wchar_t*> infs) {
    DriverState d;
    for (auto svc : services) {
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, (std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\") + svc).c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
        wchar_t img[MAX_PATH * 2] = {};
        DWORD sz = sizeof img, type = 0;
        bool got = RegQueryValueExW(k, L"ImagePath", nullptr, &type, (BYTE*)img, &sz) == ERROR_SUCCESS;
        RegCloseKey(k);
        if (!got) continue;
        std::wstring p = img;
        std::wstring low = LowerW(p);
        if (low.rfind(L"\\??\\", 0) == 0) p = p.substr(4);
        else if (low.rfind(L"\\systemroot\\", 0) == 0) p = WinDir() + p.substr(11);
        else if (low.rfind(L"system32\\", 0) == 0) p = WinDir() + L"\\" + p;
        if (!FileExists(p)) continue;
        d.active = true;
        d.service = W2U(svc);
        d.version = FileVersion(p);
        return d;
    }
    for (auto inf : infs) {
        WIN32_FIND_DATAW fd;
        std::wstring repo = WinDir() + L"\\System32\\DriverStore\\FileRepository\\";
        HANDLE h = FindFirstFileW((repo + inf + L"_*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            d.staged = true;
            d.inf = W2U(inf);
            std::string v = InfVersion(repo + fd.cFileName + L"\\" + inf);
            if (!v.empty() && v > d.version) d.version = v;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        if (d.staged) return d;
    }
    return d;
}

DriverState FtdiDriver() { return CheckDriver({ L"FTDIBUS3" }, { L"ftdibus3.inf", L"ftd3xxwu.inf" }); }
DriverState Ch341Driver() { return CheckDriver({ L"CH341_A64", L"CH341_M64", L"CH341WDM", L"CH341" }, { L"ch341wdm.inf" }); }

// ---------------------------------------------------------------------------
//  Programmes qui ont chargé une DLL (ex. FTD3XX.dll : carte déjà utilisée ailleurs)
// ---------------------------------------------------------------------------
std::vector<std::string> ProcessesWithModule(const wchar_t* dll) {
    std::vector<std::string> out;
    HANDLE ps = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (ps == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe = { sizeof pe };
    DWORD self = GetCurrentProcessId();
    for (BOOL ok = Process32FirstW(ps, &pe); ok; ok = Process32NextW(ps, &pe)) {
        if (pe.th32ProcessID == self || pe.th32ProcessID <= 4) continue;
        HANDLE ms = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pe.th32ProcessID);
        if (ms == INVALID_HANDLE_VALUE) continue;
        MODULEENTRY32W me = { sizeof me };
        for (BOOL m = Module32FirstW(ms, &me); m; m = Module32NextW(ms, &me)) {
            if (_wcsicmp(me.szModule, dll) == 0) {
                std::string n = W2U(pe.szExeFile);
                if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
                break;
            }
        }
        CloseHandle(ms);
    }
    CloseHandle(ps);
    return out;
}

// ---------------------------------------------------------------------------
//  Architecture d'une DLL : 64, 32, 0 = pas un exécutable Windows, -1 = illisible
// ---------------------------------------------------------------------------
int PeArch(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return -1;
    BYTE b[1024] = {};
    DWORD rd = 0;
    ReadFile(h, b, sizeof b, &rd, nullptr);
    CloseHandle(h);
    if (rd < 0x40 || b[0] != 'M' || b[1] != 'Z') return 0;
    DWORD pe = *(DWORD*)(b + 0x3C);
    if (pe + 6 > rd || memcmp(b + pe, "PE\0\0", 4) != 0) return 0;
    WORD m = *(WORD*)(b + pe + 4);
    return m == 0x8664 || m == 0xAA64 ? 64 : (m == 0x14C ? 32 : 0);
}

// ---------------------------------------------------------------------------
//  Installation d'un pilote intégré. L'exe se relance en administrateur, extrait le pilote de ses
//  propres ressources dans un dossier que seuls les administrateurs peuvent modifier, puis lance
//  pnputil : aucun fichier modifiable par un autre programme n'est installé.
// ---------------------------------------------------------------------------
int RunDriverInstall(int which) {
    if (which < 0 || which > 1) return 87;
    GUID g;
    if (FAILED(CoCreateGuid(&g))) return 1;
    wchar_t gs[64];
    swprintf_s(gs, L"%08lX%04X%04X", g.Data1, g.Data2, g.Data3);
    std::wstring dir = WinDir() + L"\\Temp\\CamrasTool-" + gs;
    SECURITY_ATTRIBUTES sa = { sizeof sa };
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAD:P(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)", SDDL_REVISION_1, &sa.lpSecurityDescriptor, nullptr)) return (int)GetLastError();
    BOOL made = CreateDirectoryW(dir.c_str(), &sa);
    DWORD err = GetLastError();
    LocalFree(sa.lpSecurityDescriptor);
    if (!made) return (int)err;
    DWORD code = 1;
    if (ExtractKitPrefix(which == 0 ? L"drivers\\ftdi\\" : L"drivers\\ch341\\", dir)) {
        std::wstring exe = WinDir() + L"\\System32\\pnputil.exe";
        std::wstring cl = L"\"" + exe + L"\" /add-driver \"" + dir + (which == 0 ? L"\\ftdibus3.Inf" : L"\\CH341WDM.INF") + L"\" /install";
        STARTUPINFOW si = { sizeof si };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        std::vector<wchar_t> b(cl.begin(), cl.end());
        b.push_back(0);
        if (CreateProcessW(exe.c_str(), b.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 4 * 60 * 1000);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else code = GetLastError();
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + L"\\" + fd.cFileName).c_str()); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
    return (int)code;
}

static std::atomic<bool> g_drvBusy{ false };
bool DriverInstalling() { return g_drvBusy; }

void InstallDriverAsync(int which) {
    bool expected = false;
    if (!g_drvBusy.compare_exchange_strong(expected, true)) {
        Toast(T("Une installation est déjà en cours.", "An installation is already running."), true);
        return;
    }
    std::thread([which] {
        const char* name = which == 0 ? "FTDI D3XX" : "WCH CH341/CH347";
        auto done = [&](const std::string& msg, bool err) {
            Log(err ? L_ERR : L_OK, err ? T("Pilote : ÉCHEC — %s", "Driver: FAILED — %s") : T("Pilote : %s", "Driver: %s"), msg.c_str());
            Toast(msg, err);
            g_drvBusy = false;
            if (!err) {                      // le diagnostic se met à jour tout seul
                extern void DiagStart();
                for (int i = 0; i < 200 && JobRunning(); i++) Sleep(50);
                DiagStart();
            }
        };
        wchar_t self[MAX_PATH * 2];
        GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
        std::wstring params = L"--install-driver=" + std::to_wstring(which);
        Log(L_INFO, T("Pilote %s : installation (demande administrateur)…", "Driver %s: installing (administrator prompt)…"), name);
        Toast(T("Acceptez la demande administrateur pour installer le pilote.", "Accept the administrator prompt to install the driver."));
        SHELLEXECUTEINFOW sei = { sizeof sei };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.hwnd = g_hwnd;
        sei.lpVerb = L"runas";
        sei.lpFile = self;
        sei.lpParameters = params.c_str();
        sei.nShow = SW_HIDE;
        if (!ShellExecuteExW(&sei) || !sei.hProcess) {
            DWORD e = GetLastError();
            if (e == ERROR_CANCELLED)
                return done(Fmt(T("Installation du pilote %s annulée (autorisation administrateur refusée).", "%s driver installation cancelled (administrator permission denied)."), name), true);
            return done(Fmt(T("Impossible de lancer l'installation (erreur %lu).", "Could not start the installation (error %lu)."), e), true);
        }
        if (WaitForSingleObject(sei.hProcess, 5 * 60 * 1000) == WAIT_TIMEOUT) {
            CloseHandle(sei.hProcess);
            return done(Fmt(T("pnputil ne répond pas (plus de 5 minutes) : installation du pilote %s non confirmée.", "pnputil is not responding (over 5 minutes): %s driver installation not confirmed."), name), true);
        }
        DWORD code = 1;
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        Log(L_INFO, "[pnputil] code %lu", code);
        if (code == 0) return done(Fmt(T("Pilote %s installé. Débranchez puis rebranchez la carte.", "%s driver installed. Unplug and replug the card."), name), false);
        if (code == 3010) return done(Fmt(T("Pilote %s installé. Redémarrez le PC pour terminer.", "%s driver installed. Restart the PC to finish."), name), false);
        if (code == 259) return done(Fmt(T("Pilote %s ajouté à Windows : il s'activera au branchement de la carte.", "%s driver added to Windows: it will activate when the card is plugged in."), name), false);
        done(Fmt(T("pnputil a échoué (code %lu).", "pnputil failed (code %lu)."), code), true);
    }).detach();
}
