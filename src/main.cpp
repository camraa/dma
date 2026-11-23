// main.cpp - Camra's Tool : fenêtre, rendu DirectX 11, barre latérale, en-tête, démarrage
#include "ui.h"
#include "app.h"
#include "bench.h"
#include "diag.h"
#include "firmware.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <thread>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void PageSpeed();
void PageResults();
void PageHistory();
void PageDevice();
void PageFirmware();
void PageDiag();
void PageLog();
void PageSettings();
void PageHeaderButtons(int page, float rightEdge);

using namespace ui;

static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain* g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static UINT g_resizeW = 0, g_resizeH = 0;
static float g_scale = 1.0f, g_newScale = 0;
static bool g_appliedDark = true;

// ---------------------------------------------------------------------------
//  DirectX 11
// ---------------------------------------------------------------------------
static void CreateRT() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { g_dev->CreateRenderTargetView(back, nullptr, &g_rtv); back->Release(); }
}

static void CleanupRT() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

static bool CreateDevice(HWND h) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = h;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED)   // pas de GPU compatible : rendu logiciel WARP
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) return false;
    CreateRT();
    return true;
}

static void CleanupDevice() {
    CleanupRT();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

static LRESULT WINAPI WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT r;
    if (HandleNc(h, msg, wp, lp, &r)) return r;
    if (ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) { g_resizeW = LOWORD(lp); g_resizeH = HIWORD(lp); }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = (LONG)(1000 * g_scale);
        mm->ptMinTrackSize.y = (LONG)(660 * g_scale);
        return 0;
    }
    case WM_DPICHANGED: {
        g_newScale = HIWORD(wp) / 96.0f;
        RECT* rc = (RECT*)lp;
        SetWindowPos(h, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//  Barre latérale
// ---------------------------------------------------------------------------
static bool SidebarItem(const char* label, unsigned int icon, ImVec4 tint, bool selected, bool dot) {
    ImGui::PushID(label);
    float h = 36 * S, w = ImGui::GetContentRegionAvail().x - 12 * S;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##it", ImVec2(w, h));
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), U(P.accent), 8 * S);
    else if (hov) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), U(WithA(P.text, 0.06f)), 8 * S);
    float is = 24 * S;
    ImVec2 ip = ImVec2(p.x + 8 * S, p.y + (h - is) * 0.5f);
    dl->AddRectFilled(ip, ImVec2(ip.x + is, ip.y + is), U(tint), 6 * S);
    dl->AddRectFilledMultiColor(ip, ImVec2(ip.x + is, ip.y + is * 0.5f), IM_COL32(255, 255, 255, 40), IM_COL32(255, 255, 255, 40), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    Icon(dl, icon, ImVec2(ip.x + is * 0.5f, ip.y + is * 0.5f), 14 * S, IM_COL32(255, 255, 255, 255));
    float ty = p.y + (h - ImGui::GetFontSize()) * 0.5f;
    dl->AddText(ImVec2(ip.x + is + 10 * S, ty), U(selected ? ImVec4(1, 1, 1, 1) : P.text), label);
    if (dot) dl->AddCircleFilled(ImVec2(p.x + w - 16 * S, p.y + h * 0.5f), 4.5f * S, U(selected ? ImVec4(1, 1, 1, 1) : P.red), 16);
    ImGui::PopID();
    return pressed;
}

// Logo : jauge blanche sur carré bleu
static void Logo(ImVec2 p, float sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(p, ImVec2(p.x + sz, p.y + sz), IM_COL32(64, 156, 255, 255), IM_COL32(64, 156, 255, 255), IM_COL32(20, 90, 230, 255), IM_COL32(20, 90, 230, 255));
    // coins arrondis : on recouvre les angles avec le fond de la barre latérale
    float r = sz * 0.24f;
    ImU32 bg = U(P.side);
    for (int k = 0; k < 4; k++) {
        ImVec2 c = ImVec2(k % 2 ? p.x + sz - r : p.x + r, k / 2 ? p.y + sz - r : p.y + r);
        ImVec2 o = ImVec2(k % 2 ? p.x + sz : p.x, k / 2 ? p.y + sz : p.y);
        float a0 = (k == 0) ? IM_PI : (k == 1) ? IM_PI * 1.5f : (k == 2) ? IM_PI * 0.5f : 0;
        dl->PathLineTo(o);
        dl->PathArcTo(c, r, a0, a0 + IM_PI * 0.5f, 8);
        dl->PathFillConvex(bg);
    }
    ImVec2 c = ImVec2(p.x + sz * 0.5f, p.y + sz * 0.6f);
    float gr = sz * 0.3f;
    dl->PathArcTo(c, gr, IM_PI * 1.0f, IM_PI * 2.0f, 24);
    dl->PathStroke(IM_COL32(255, 255, 255, 255), 0, sz * 0.08f);
    float a = IM_PI * 1.72f;
    dl->AddLine(c, ImVec2(c.x + cosf(a) * gr * 0.85f, c.y + sinf(a) * gr * 0.85f), IM_COL32(255, 255, 255, 255), sz * 0.07f);
    dl->AddCircleFilled(c, sz * 0.06f, IM_COL32(255, 255, 255, 255), 12);
}

static const char* PageTitle(int p) {
    switch (p) {
    case PG_SPEED: return "Speedtest";
    case PG_RESULTS: return T("Résultats", "Results");
    case PG_HISTORY: return T("Historique", "History");
    case PG_DEVICE: return T("Appareil", "Device");
    case PG_FIRMWARE: return "Firmware";
    case PG_DIAG: return T("Diagnostic", "Diagnostics");
    case PG_LOG: return T("Journal", "Log");
    }
    return T("Réglages", "Settings");
}

static void Sidebar(float w, float hgt) {
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::BeginChild("##side", ImVec2(w, hgt), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    TrafficLights(g_hwnd, ImVec2(22 * S, 22 * S));
    ImGui::SetCursorPos(ImVec2(18 * S, 52 * S));
    ImVec2 lp = ImGui::GetCursorScreenPos();
    Logo(lp, 38 * S);
    ImGui::SetCursorScreenPos(ImVec2(lp.x + 48 * S, lp.y + 1 * S));
    ImGui::BeginGroup();
    PushFontSz(FSemi, Base + 2);
    ImGui::TextUnformatted("Camra's Tool");
    ImGui::PopFont();
    Small(P.text2, "DMA Speedtest", false);
    ImGui::EndGroup();
    ImGui::SetCursorPos(ImVec2(12 * S, 112 * S));
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1 * S));
    ImGui::PushItemWidth(w - 24 * S);
    ImGui::Dummy(ImVec2(w - 24 * S, 0));
    DiagResult d = DiagCopy();
    struct It { int page; unsigned icon; ImVec4 tint; };
    const It items[] = { { PG_SPEED, 0xEC4A, P.blue }, { PG_RESULTS, 0xE9D2, P.green }, { PG_HISTORY, 0xE81C, P.teal }, { PG_DEVICE, 0xE950, P.orange },
                         { PG_FIRMWARE, 0xE945, P.purple }, { PG_DIAG, 0xE9D9, P.pink }, { PG_LOG, 0xE8FD, P.gray }, { PG_SETTINGS, 0xE713, P.gray } };
    for (auto& it : items) {
        bool dot = it.page == PG_DIAG && d.done && d.errors > 0;
        if (SidebarItem(PageTitle(it.page), it.icon, it.tint, g_page == it.page, dot)) g_page = it.page;
        ImGui::Dummy(ImVec2(0, 1 * S));
    }
    ImGui::PopItemWidth();
    ImGui::PopStyleVar();
    ImGui::EndGroup();

    // État de la carte (bas de la barre latérale)
    DeviceInfo di = DevInfo();
    int cs = g_connState;
    BenchState b = BenchCopy();
    ImVec4 col;
    const char* title;
    std::string sub;
    if (cs == CS_CONNECTING) { col = P.orange; title = T("Connexion…", "Connecting…"); sub = T("Ouverture du périphérique", "Opening the device"); }
    else if (di.connected && di.simulated) { col = P.orange; title = "Simulation"; sub = T("Données fictives, aucune carte", "Fake data, no card"); }
    else if (di.connected) { col = P.green; title = T("Connecté", "Connected"); sub = di.fpga.s(); }
    else if (di.released) { col = P.green; title = T("Prête", "Ready"); sub = di.fpga.s(); }
    else { col = P.red; title = T("Carte non connectée", "Card not connected"); sub = T("Voir le diagnostic", "View diagnostics"); }
    if (b.running) sub = b.stability ? T("Test de stabilité…", "Stability test…") : T("Test en cours…", "Test running…");
    float ch = 56 * S;
    ImGui::SetCursorPos(ImVec2(12 * S, hgt - ch - 14 * S));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float cw = w - 24 * S;
    if (ImGui::InvisibleButton("##status", ImVec2(cw, ch))) g_page = di.connected || di.released ? PG_DEVICE : PG_DIAG;
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + ch), U(hov ? P.card2 : P.card), 10 * S);
    dl->AddRect(p, ImVec2(p.x + cw, p.y + ch), U(P.border), 10 * S);
    dl->AddCircleFilled(ImVec2(p.x + 16 * S, p.y + 20 * S), 4.5f * S, U(col), 16);
    dl->AddText(ImVec2(p.x + 30 * S, p.y + 10 * S), U(P.text), title);
    PushFontSz(FReg, Base - 2);
    dl->PushClipRect(p, ImVec2(p.x + cw - 8 * S, p.y + ch), true);
    dl->AddText(ImVec2(p.x + 30 * S, p.y + 31 * S), U(P.text2), sub.c_str());
    dl->PopClipRect();
    ImGui::PopFont();
    ImGui::EndChild();
}

// Bandeau « mise à jour disponible » en haut de toutes les pages ; renvoie sa hauteur (0 s'il n'y a rien)
static float UpdateBanner(float x, float y, float w) {
    static std::string later;                // version remise à plus tard, jusqu'au prochain lancement
    std::string v = UpdateAvailable();
    bool busy = UpdateBusy();
    if (v.empty() || (v == later && !busy)) return 0;
    float h = 58 * S;
    ImGui::SetCursorPos(ImVec2(x, y));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), U(WithA(P.accent, Dark ? 0.22f : 0.13f)), 12 * S);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), U(WithA(P.accent, 0.55f)), 12 * S);
    ImVec2 ic = ImVec2(p.x + 29 * S, p.y + h * 0.5f);
    dl->AddCircleFilled(ic, 15 * S, U(P.accent), 32);
    Icon(dl, 0xE896, ic, 15 * S, IM_COL32(255, 255, 255, 255));
    bool job = JobRunning() || FwCopy().running;
    std::string title, sub;
    if (busy) {
        int pc = UpdatePct();
        title = pc < 0 ? std::string(T("Téléchargement de la mise à jour…", "Downloading the update…")) : Fmt(T("Téléchargement de la mise à jour… %d %%", "Downloading the update… %d%%"), pc);
        sub = T("L'application va redémarrer toute seule.", "The app will restart by itself.");
    } else {
        title = Fmt(T("Mise à jour disponible : version %s", "Update available: version %s"), v.c_str());
        sub = job ? T("Attendez la fin de l'opération en cours pour la télécharger.", "Wait for the current operation to finish before downloading.")
                  : T("Veuillez la télécharger : l'application redémarre ensuite toute seule.", "Please download it: the app then restarts by itself.");
    }
    dl->AddText(FSemi, ImGui::GetFontSize(), ImVec2(p.x + 56 * S, p.y + 10 * S), U(P.text), title.c_str());
    dl->AddText(FReg, (Base - 2) * S, ImVec2(p.x + 56 * S, p.y + 31 * S), U(P.text2), sub.c_str());
    if (!busy) {
        float bw = 124 * S, lw = 96 * S, bh = 34 * S, by = y + (h - bh) * 0.5f;
        ImGui::SetCursorPos(ImVec2(x + w - 12 * S - bw - 6 * S - lw, by));
        if (Button(T("Plus tard", "Later"), B_GHOST, ImVec2(lw, bh))) later = v;
        ImGui::SetCursorPos(ImVec2(x + w - 12 * S - bw, by));
        if (Button(T("Télécharger", "Download"), B_PRIMARY, ImVec2(bw, bh), job)) UpdateInstallAsync();
    }
    return h + 10 * S;
}

// ---------------------------------------------------------------------------
//  Interface complète
// ---------------------------------------------------------------------------
static void DrawUI() {
    if (g_set.dark != g_appliedDark) {
        ApplyTheme(g_set.dark, g_scale);
        SetupWindow(g_hwnd, g_set.dark);
        g_appliedDark = g_set.dark;
    }
    g_en = g_set.english;
    HitRects.clear();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    float sw = 232 * S;
    SidebarW = sw;
    bg->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), U(P.bg));
    bg->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + sw, vp->Pos.y + vp->Size.y), U(P.side));
    bg->AddLine(ImVec2(vp->Pos.x + sw, vp->Pos.y), ImVec2(vp->Pos.x + sw, vp->Pos.y + vp->Size.y), U(P.border), 1);

    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    Sidebar(sw, vp->Size.y);

    // En-tête : titre de la page, boutons, FR | EN
    float cx = sw + 26 * S, right = vp->Size.x - 20 * S;
    ImGui::SetCursorPos(ImVec2(cx, 14 * S));
    Title(PageTitle(g_page), 24);
    int lang = g_set.english ? 1 : 0;
    float segW = 44 * S;
    float segX = right - segW * 2 - 4 * S;
    ImGui::SetCursorPos(ImVec2(segX, 16 * S));
    if (Segmented("lang", &lang, { "FR", "EN" }, segW)) { g_set.english = lang == 1; g_en = g_set.english; SaveSettings(); }
    Hit();
    ImGui::SetCursorPos(ImVec2(0, 16 * S));
    PageHeaderButtons(g_page, segX - 12 * S);

    // Contenu de la page (défilant)
    float top = TitleH * S + 6 * S;
    top += UpdateBanner(cx, top, right - 16 * S - cx);   // aligné sur les cartes
    ImGui::SetCursorPos(ImVec2(sw, top));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26 * S, 4 * S));
    ImGui::BeginChild("##content", ImVec2(vp->Size.x - sw, vp->Size.y - top), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    switch (g_page) {
    case PG_SPEED: PageSpeed(); break;
    case PG_RESULTS: PageResults(); break;
    case PG_HISTORY: PageHistory(); break;
    case PG_DEVICE: PageDevice(); break;
    case PG_FIRMWARE: PageFirmware(); break;
    case PG_DIAG: PageDiag(); break;
    case PG_LOG: PageLog(); break;
    default: PageSettings(); break;
    }
    ImGui::Dummy(ImVec2(0, 16 * S));
    ImGui::EndChild();
    ImGui::End();

    DrawModals();

    // Notification en bas de la fenêtre
    ToastMsg t = ToastGet();
    double age = NowSec() - t.t;
    if (!t.text.empty() && age < 4.5) {
        float a = (float)std::min(1.0, std::min(age * 6, (4.5 - age) * 2));
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        float maxW = std::min(vp->Size.x - sw - 80 * S, 720 * S);
        ImVec2 ts = ImGui::CalcTextSize(t.text.c_str(), nullptr, false, maxW);
        float pw = ts.x + 36 * S, ph = ts.y + 20 * S;
        ImVec2 p = ImVec2(vp->Pos.x + sw + (vp->Size.x - sw - pw) * 0.5f, vp->Pos.y + vp->Size.y - ph - 22 * S);
        ImVec4 bgc = t.err ? Mix(P.red, ImVec4(0, 0, 0, 1), 0.25f) : (Dark ? Rgb(70, 70, 76) : Rgb(40, 40, 44));
        fg->AddRectFilled(ImVec2(p.x, p.y + 3 * S), ImVec2(p.x + pw, p.y + ph + 3 * S), IM_COL32(0, 0, 0, (int)(50 * a)), ph * 0.5f);
        fg->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), U(WithA(bgc, 0.96f * a)), std::min(ph * 0.5f, 18 * S));
        fg->AddText(nullptr, 0, ImVec2(p.x + 18 * S, p.y + 10 * S), IM_COL32(255, 255, 255, (int)(255 * a)), t.text.c_str(), nullptr, maxW);
    }
}

// ---------------------------------------------------------------------------
//  Point d'entrée
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // Relance en administrateur pour installer un pilote : pas de fenêtre
    const wchar_t* drv = wcsstr(GetCommandLineW(), L"--install-driver=");
    if (drv) return RunDriverInstall(_wtoi(drv + 17));

    if (wcsstr(GetCommandLineW(), L"--test")) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ExtractBuiltinLibs();
        return RunTests();
    }
    CrashInit();
    bool afterUpdate = wcsstr(GetCommandLineW(), L"--after-update") != nullptr;

    // Une seule instance : une seule application à la fois peut utiliser la carte DMA
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"CamrasTool_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS && afterUpdate) {
        WaitForSingleObject(mtx, 15000);     // l'ancienne version se ferme
    } else if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"CamrasToolWindow", nullptr);
        if (other) { ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); }
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    LoadSettings();
    std::wstring cmd = GetCommandLineW();
    auto flag = [&](const wchar_t* f) { return cmd.find(f) != std::wstring::npos; };
    if (flag(L"--sim")) g_set.mode = MODE_SIM;
    if (flag(L"--en")) g_set.english = true;
    if (flag(L"--light")) g_set.dark = false;
    size_t pg = cmd.find(L"--page=");
    if (pg != std::wstring::npos) g_page = std::clamp(_wtoi(cmd.c_str() + pg + 7), 0, PG_COUNT - 1);
    size_t md = cmd.find(L"--mode=");
    if (md != std::wstring::npos) g_set.testMode = std::clamp(_wtoi(cmd.c_str() + md + 7), 0, 4);
    g_en = g_set.english;

    Log(L_INFO, T("%s %s démarré.", "%s %s started."), APP_NAME, APP_VERSION);
    UpdateCleanup();
    ExtractBuiltinLibs();
    if (g_set.dll.empty() || !FileExists(g_set.dll)) {
        if (!FileExists(ExeDir() + L"\\leechcore.dll") && !FileExists(BuiltinLibDir() + L"\\leechcore.dll"))
            Log(L_WARN, "%s", T("leechcore.dll introuvable à côté de l'exécutable — indiquez-le dans Appareil.", "leechcore.dll not found next to the executable — set it in Device."));
    }
    Log(L_INFO, T("leechcore.dll : %s", "leechcore.dll: %s"), W2U(ResolveDll()).c_str());
    KitEnsureAsync();
    HistoryLoad();

    ImGui_ImplWin32_EnableDpiAwareness();
    g_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
    if (g_scale < 1.0f) g_scale = 1.0f;

    WNDCLASSEXW wc = { sizeof wc, CS_CLASSDC, WndProc, 0, 0, hInst, LoadIconW(hInst, MAKEINTRESOURCEW(1)), LoadCursorW(nullptr, IDC_ARROW),
                       nullptr, nullptr, L"CamrasToolWindow", LoadIconW(hInst, MAKEINTRESOURCEW(1)) };
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int W = std::min((int)(1240 * g_scale), (int)(wa.right - wa.left));
    int H = std::min((int)(820 * g_scale), (int)(wa.bottom - wa.top));
    g_hwnd = CreateWindowW(wc.lpszClassName, L"Camra's Tool", WS_OVERLAPPEDWINDOW, wa.left + ((wa.right - wa.left) - W) / 2,
                           wa.top + ((wa.bottom - wa.top) - H) / 2, W, H, nullptr, nullptr, hInst, nullptr);
    SetupWindow(g_hwnd, g_set.dark);
    if (!CreateDevice(g_hwnd)) {
        CleanupDevice();
        MessageBoxW(nullptr, L"Impossible d'initialiser DirectX 11.", L"Camra's Tool", MB_ICONERROR);
        return 1;
    }
    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    ApplyTheme(g_set.dark, g_scale);
    g_appliedDark = g_set.dark;
    ImGui::GetStyle().FontScaleDpi = g_scale;
    LoadFonts();
    ImGui::GetStyle().FontSizeBase = Base;
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    if (!g_set.guideDone && g_set.mode == MODE_FPGA && !flag(L"--")) g_openGuide = true;   // premier lancement
    if (flag(L"--guide")) g_openGuide = true;
    if (flag(L"--cable")) g_openCable = true;
    if (flag(L"--crash")) std::thread([] { Sleep(1500); *(volatile int*)nullptr = 1; }).detach();
    if (flag(L"--update")) std::thread([] { for (int i = 0; i < 100 && UpdateAvailable().empty(); i++) Sleep(100); UpdateInstallAsync(); }).detach();

    // Connexion automatique ; en cas d'échec, le diagnostic s'ouvre avec les solutions
    if (flag(L"--diag")) { g_page = PG_DIAG; DiagStart(); }
    else if (flag(L"--algos")) { g_page = PG_DEVICE; AlgoStart(); }
    else if (flag(L"--autostart")) BenchStart(g_set.testMode);
    else if (!flag(L"--noconnect")) ConnectAsync(g_set.mode != MODE_SIM && !g_openGuide);

    // --selftest : après le test, écrit les exports à côté de l'exe et copie l'image
    if (flag(L"--selftest"))
        std::thread([] {
            for (int i = 0; i < 1200 && !BenchCopy().hasResult; i++) Sleep(100);
            BenchState b = BenchCopy();
            std::string j = ResultsJson(b), c = ResultsCsv(b), d = ResultsDiscord(b);
            WriteFileAll(DataDir() + L"\\selftest.json", j.data(), j.size());
            WriteFileAll(DataDir() + L"\\selftest.csv", c.data(), c.size());
            WriteFileAll(DataDir() + L"\\selftest.txt", d.data(), d.size());
            ResultsImage(b, (DataDir() + L"\\selftest.png").c_str());
            ResultsImage(b);
        }).detach();
    DevMonitorStart();
    if (g_set.updates) UpdateCheckAsync();

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (IsIconic(g_hwnd)) { MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT); continue; }
        if (g_newScale > 0) {
            g_scale = g_newScale;
            g_newScale = 0;
            ApplyTheme(g_set.dark, g_scale);
            ImGui::GetStyle().FontScaleDpi = g_scale;
        }
        if (g_resizeW && g_resizeH) {
            CleanupRT();
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateRT();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawUI();
        ImGui::Render();
        float c[4] = { P.bg.x, P.bg.y, P.bg.z, 1 };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, c);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
        // En arrière-plan et sans test en cours : on ralentit l'interface
        if (GetForegroundWindow() != g_hwnd && !JobRunning()) MsgWaitForMultipleObjects(0, nullptr, FALSE, 60, QS_ALLINPUT);
    }

    // Arrêt propre : on stoppe le test en cours avant de fermer la carte
    BenchStop();
    for (int i = 0; i < 150 && JobRunning(); i++) Sleep(20);
    if (!JobRunning()) DevDisconnect();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (mtx) CloseHandle(mtx);
    return 0;
}
