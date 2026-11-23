// launcher.cpp - lance toujours la dernière version publiée de Camra's Tool
// À chaque démarrage : dernière version publiée sur GitHub -> téléchargement si besoin (HTTPS + SHA-256 publié)
// -> lancement. Sans réseau ou en cas de problème, la version déjà installée est lancée.
// Options : --dir=DOSSIER (installation ailleurs), --api=URL (tests), le reste est transmis à l'application.
#include "../src/app.h"
#include <shellapi.h>
#include <commctrl.h>
#include <thread>
#pragma comment(lib, "comctl32.lib")

bool HttpGet(const std::string& url, std::string& out, size_t maxSize, std::atomic<int>* pct);

static const wchar_t* APP_EXE = L"Camra's Tool.exe";
static std::atomic<int> g_pct{ -1 };
static std::atomic<bool> g_downloading{ false }, g_done{ false };
static std::wstring g_dir, g_args;
static std::string g_api = "https://api.github.com/repos/camraa/dma/releases/latest";
static HWND g_wnd, g_label, g_bar;
static std::string g_result = "ok";          // écrit dans lanceur.txt pour le dépannage

static std::string ExeVersion(const std::wstring& path) {
    DWORD h = 0, n = GetFileVersionInfoSizeW(path.c_str(), &h);
    std::vector<BYTE> b(n);
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!n || !GetFileVersionInfoW(path.c_str(), 0, n, b.data()) || !VerQueryValueW(b.data(), L"\\", (void**)&fi, &len) || !fi) return "0.0.0";
    return Fmt("%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS));
}

static bool AppRunning() {
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"CamrasTool_SingleInstance");
    if (m) CloseHandle(m);
    return m != nullptr;
}

static void Update() {
    std::wstring exe = g_dir + L"\\" + APP_EXE;
    bool have = FileExists(exe);
    if (AppRunning() && have) { g_result = "application deja ouverte"; return; }
    std::string body, ver, url, sha;
    if (!HttpGet(g_api, body, 1 << 20, nullptr) || !ParseRelease(body, ver, url, sha)) { g_result = "GitHub injoignable"; return; }
    if (url.empty() || sha.empty()) { g_result = "version publiee sans exe verifiable"; return; }
    sha = Lower(sha);
    std::string cur;
    if (have) {
        if (ReadFileAll(exe, cur) && Sha256(cur) == sha) { g_result = "a jour " + ver; return; }
        if (IsNewer(ExeVersion(exe), ver)) { g_result = "version installee plus recente"; return; }   // jamais de retour en arrière
    }
    g_downloading = true;
    std::string data;
    if (!HttpGet(url, data, 64u << 20, &g_pct)) { g_result = "telechargement impossible"; return; }
    if (Sha256(data) != sha) { g_result = "empreinte incorrecte : mise a jour refusee"; return; }
    g_result = WriteFileAll(exe, data.data(), data.size()) ? "installe " + ver : "ecriture impossible";
}

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TIMER:
        if (g_done) { DestroyWindow(h); return 0; }
        // La fenêtre n'apparaît que si l'attente dure
        if (w == 1 && !IsWindowVisible(h) && (g_downloading || GetTickCount64() - (ULONGLONG)GetWindowLongPtrW(h, GWLP_USERDATA) > 800)) ShowWindow(h, SW_SHOW);
        if (g_downloading && g_pct >= 0) {
            SetWindowTextW(g_label, U2W(Fmt(T("Téléchargement de la mise à jour… %d %%", "Downloading the update… %d%%"), g_pct.load())).c_str());
            SetWindowLongPtrW(g_bar, GWL_STYLE, GetWindowLongPtrW(g_bar, GWL_STYLE) & ~PBS_MARQUEE);
            SendMessageW(g_bar, PBM_SETPOS, (WPARAM)g_pct.load(), 0);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE: return 0;                 // on ne coupe pas une installation
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"CamrasTool_Launcher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    g_en = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_FRENCH;
    g_dir = LocalDir() + L"\\app";
    int n = 0;
    LPWSTR* av = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 1; av && i < n; i++) {
        std::wstring a = av[i];
        if (a.rfind(L"--dir=", 0) == 0) g_dir = a.substr(6);
        else if (a.rfind(L"--api=", 0) == 0) g_api = W2U(a.substr(6));
        else g_args += L" \"" + a + L"\"";
    }
    LocalFree(av);
    MakeDirs(g_dir);
    // Première fois : reprend les réglages et l'historique posés à côté du lanceur
    for (const wchar_t* f : { L"\\CamrasTool.ini", L"\\CamrasTool_history.txt" })
        if (!FileExists(g_dir + f) && FileExists(ExeDir() + f)) CopyFileW((ExeDir() + f).c_str(), (g_dir + f).c_str(), TRUE);

    INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&ic);
    SetProcessDPIAware();
    HDC dc = GetDC(nullptr);
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    auto px = [dpi](int v) { return MulDiv(v, dpi, 96); };
    WNDCLASSEXW wc = { sizeof wc, 0, Proc, 0, 0, hi, LoadIconW(hi, MAKEINTRESOURCEW(1)), LoadCursorW(nullptr, IDC_ARROW), GetSysColorBrush(COLOR_WINDOW), nullptr, L"CamrasLauncher", LoadIconW(hi, MAKEINTRESOURCEW(1)) };
    RegisterClassExW(&wc);
    int W = px(380), H = px(120);
    g_wnd = CreateWindowExW(0, wc.lpszClassName, L"Camra's Tool", WS_CAPTION | WS_POPUP, (GetSystemMetrics(SM_CXSCREEN) - W) / 2, (GetSystemMetrics(SM_CYSCREEN) - H) / 2, W, H, nullptr, nullptr, hi, nullptr);
    SetWindowLongPtrW(g_wnd, GWLP_USERDATA, (LONG_PTR)GetTickCount64());
    HFONT font = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_label = CreateWindowExW(0, L"STATIC", U2W(T("Recherche de la dernière version…", "Checking for the latest version…")).c_str(), WS_CHILD | WS_VISIBLE, px(20), px(14), px(330), px(22), g_wnd, nullptr, hi, nullptr);
    SendMessageW(g_label, WM_SETFONT, (WPARAM)font, TRUE);
    g_bar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_MARQUEE, px(20), px(44), px(325), px(14), g_wnd, nullptr, hi, nullptr);
    SendMessageW(g_bar, PBM_SETMARQUEE, TRUE, 30);
    SetTimer(g_wnd, 1, 60, nullptr);

    std::thread th([] { Update(); g_done = true; });
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    th.join();
    std::string line = NowStamp() + "  " + g_result + "\r\n";
    WriteFileAll(g_dir + L"\\lanceur.txt", line.data(), line.size());

    std::wstring exe = g_dir + L"\\" + APP_EXE;
    if (!FileExists(exe)) {
        std::wstring m = U2W(Fmt(T("Impossible de télécharger Camra's Tool (%s).\n\nVérifiez la connexion Internet puis relancez.", "Cannot download Camra's Tool (%s).\n\nCheck the Internet connection and try again."), g_result.c_str()));
        MessageBoxW(nullptr, m.c_str(), L"Camra's Tool", MB_ICONERROR);
        return 1;
    }
    if (g_result.rfind("empreinte", 0) == 0)
        MessageBoxW(nullptr, U2W(T("La mise à jour téléchargée ne correspond pas à l'empreinte publiée : elle a été refusée. La version installée va s'ouvrir.",
                                   "The downloaded update does not match the published hash: it was refused. The installed version will open.")).c_str(), L"Camra's Tool", MB_ICONWARNING);
    ShellExecuteW(nullptr, L"open", exe.c_str(), g_args.c_str(), g_dir.c_str(), SW_SHOWNORMAL);
    if (mtx) CloseHandle(mtx);
    return 0;
}
