// guide.cpp - fenêtres d'aide : assistant de branchement et comparaison de câbles/ports
#include "ui.h"
#include "app.h"
#include "bench.h"
#include "diag.h"

using namespace ui;

bool g_openGuide = false, g_openCable = false;

static float ModalW() { return std::min(620 * S, ImGui::GetMainViewport()->Size.x - 80 * S); }

static bool BeginModal(const char* id, const char* title, bool* open) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (*open && !ImGui::IsPopupOpen(id)) { ImGui::OpenPopup(id); *open = false; }
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ModalW(), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * S, 20 * S));
    bool r = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (r) { Title(title, 20); ImGui::Dummy(ImVec2(0, 2 * S)); }
    return r;
}

// Une étape : coche verte, en attente, et un bouton facultatif
static bool Step(int n, bool ok, bool active, const char* title, const char* help, const char* button, bool disabled) {
    ImGui::PushID(n);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c = ImVec2(p.x + 11 * S, p.y + ImGui::GetTextLineHeight() * 0.5f + 1 * S);
    if (ok) StatusIcon(dl, 0, c, 11 * S);
    else {
        dl->AddCircle(c, 10 * S, U(active ? P.accent : P.text3), 24, 1.6f * S);
        std::string s = std::to_string(n);
        ImVec2 ts = ImGui::CalcTextSize(s.c_str());
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), U(active ? P.accent : P.text3), s.c_str());
    }
    ImGui::Indent(34 * S);
    PushFontSz(FSemi, Base);
    ImGui::TextColored(ok || active ? P.text : P.text3, "%s", title);
    ImGui::PopFont();
    bool clicked = false;
    if (active && !ok) {
        Small(P.text2, help);
        if (button) clicked = Button(button, B_PRIMARY, ImVec2(0, 0), disabled);
    }
    ImGui::Unindent(34 * S);
    ImGui::Dummy(ImVec2(0, 3 * S));
    ImGui::PopID();
    return clicked;
}

static void Guide() {
    if (!BeginModal("##guide", T("Assistant de branchement", "Connection assistant"), &g_openGuide)) return;
    static DriverState drv;
    static double drvT = -10;
    if (NowSec() - drvT > 2) { drv = FtdiDriver(); drvT = NowSec(); }
    UsbDev usb = CardUsb();
    DeviceInfo d = DevInfo();
    BenchState b = BenchCopy();
    bool busy = JobRunning(), fpga = g_set.mode == MODE_FPGA;
    bool s1 = drv.active || drv.staged, s2 = usb.present && !usb.problem, s3 = s2 && usb.speed == 3;
    bool s4 = s2 && !d.simulated && (d.connected || d.released) && (d.ramProbed || !g_set.zoneAuto);
    bool s5 = s4 && b.hasResult && !b.running && !b.simulated;
    if (!fpga) {
        Wrapped(P.orange, T("Le mode FPGA n'est pas choisi : l'assistant ne concerne que la vraie carte.", "FPGA mode is not selected: the assistant only applies to the real card."));
        if (Button(T("Utiliser la carte FPGA", "Use the FPGA card"))) { g_set.mode = MODE_FPGA; SaveSettings(); StartJob([] { DevDisconnect(); }); }
        ImGui::Dummy(ImVec2(0, 4 * S));
    }
    if (Step(1, s1, true, T("Pilote de la carte", "Card driver"), T("Le pilote FTDI manque. Cliquez, puis acceptez la demande administrateur.", "The FTDI driver is missing. Click, then accept the administrator prompt."),
             T("Installer le pilote FTDI", "Install the FTDI driver"), DriverInstalling())) InstallDriverAsync(0);
    Step(2, s2, s1, T("Carte visible en USB", "Card visible over USB"),
         T("Allumez le PC cible (la carte est alimentée par son slot), puis reliez le port DATA de la carte à CE PC avec un câble de données. Détection automatique.",
           "Power on the target PC (the card is powered by its slot), then connect the card's DATA port to THIS PC with a data cable. Detected automatically."), nullptr, false);
    Step(3, s3, s2, T("Liaison USB 3", "USB 3 link"),
         T("La carte est en USB 2 : le débit sera limité. Essayez un port USB 3 (bleu ou « SS »), sans hub, et un câble USB 3.",
           "The card is on USB 2: throughput will be limited. Try a USB 3 port (blue or \"SS\"), without a hub, and a USB 3 cable."), nullptr, false);
    if (Step(4, s4, s2, T("Le PC cible répond", "The target PC answers"),
             T("Fermez les autres logiciels DMA, vérifiez que Windows est démarré sur le PC cible, puis vérifiez la connexion.",
               "Close other DMA software, make sure Windows is running on the target PC, then check the connection."),
             busy ? T("Vérification…", "Checking…") : T("Vérifier la connexion", "Check the connection"), busy || !fpga)) ConnectAsync(false);
    if (g_connState == CS_FAILED && s2 && !s4 && !busy) {
        ImGui::Indent(34 * S);
        Small(P.red, DevError().c());
        ImGui::Unindent(34 * S);
    }
    if (Step(5, s5, s4, T("Test rapide", "Quick test"), T("Mesure la réactivité de la carte en quelques secondes.", "Measures the card's responsiveness in a few seconds."),
             b.running ? T("Test en cours…", "Test running…") : T("Lancer le test rapide", "Run the quick test"), busy)) BenchStart(TM_SPEED, "", true);
    if (s5) ImGui::TextColored(P.green, "%s", T("Tout est prêt. Votre carte fonctionne.", "All set. Your card works."));
    ImGui::Dummy(ImVec2(0, 4 * S));
    if (Button(s5 ? T("Terminer", "Finish") : T("Fermer", "Close"), s5 ? B_PRIMARY : B_NEUTRAL)) {
        g_set.guideDone = true;
        SaveSettings();
        ImGui::CloseCurrentPopup();
    }
    if (!s5) {
        ImGui::SameLine();
        if (Button(T("Diagnostic complet", "Full diagnostics"), B_GHOST, ImVec2(0, 0), busy)) {
            g_set.guideDone = true;
            SaveSettings();
            g_page = PG_DIAG;
            DiagStart();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

// Le préfixe de note identifie les essais de câble dans l'historique (les deux langues sont reconnues)
static bool CableName(const std::string& note, std::string& name) {
    for (const char* p : { "câble : ", "cable: " })
        if (note.rfind(p, 0) == 0) { name = note.substr(strlen(p)); return true; }
    return false;
}

static void Cable() {
    if (!BeginModal("##cable", T("Comparer des câbles et des ports", "Compare cables and ports"), &g_openCable)) return;
    static char name[64] = "";
    BenchState b = BenchCopy();
    bool busy = JobRunning();
    Wrapped(P.text2, T("Nommez le branchement actuel, testez-le, puis changez de câble ou de port et recommencez. Chaque essai dure environ 15 secondes.",
                       "Name the current connection, test it, then change the cable or port and start again. Each try takes about 15 seconds."));
    ImGui::SetNextItemWidth(Avail0() - 190 * S);
    ImGui::InputTextWithHint("##n", T("ex. câble noir, port arrière", "e.g. black cable, rear port"), name, sizeof name);
    ImGui::SameLine();
    if (b.running) {
        if (Button(T("Arrêter", "Stop"), B_DANGER, ImVec2(Avail0(), 0))) BenchStop();
        Progress((float)(b.order.empty() ? 0 : (b.doneCount + b.progress) / b.order.size()), Avail0(), P.accent);
    } else if (Button(T("Tester ce branchement", "Test this connection"), B_PRIMARY, ImVec2(Avail0(), 0), busy || !name[0]))
        BenchStart(TM_THROUGHPUT, std::string(T("câble : ", "cable: ")) + name, true);

    struct Row { std::string name; HistEntry e; };
    std::vector<Row> rows;
    for (auto& e : HistoryCopy()) {
        std::string n;
        if (CableName(e.note, n) && rows.size() < 12) rows.push_back({ n, e });
    }
    int best = -1;
    for (int i = 0; i < (int)rows.size(); i++) {
        auto& r = rows[(size_t)i].e;
        if (r.success < 99.9) continue;
        if (best < 0 || r.maxMbps > rows[(size_t)best].e.maxMbps) best = i;
    }
    if (!rows.empty() && ImGui::BeginTable("##t", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        const char* hd[4] = { T("Branchement", "Connection"), T("Débit", "Throughput"), T("Succès", "Success"), T("Lien", "Link") };
        for (int c = 0; c < 4; c++) { ImGui::TableSetColumnIndex(c); ImGui::TextColored(P.text2, "%s", hd[c]); }
        for (int i = 0; i < (int)rows.size(); i++) {
            auto& r = rows[(size_t)i];
            ImVec4 c = i == best ? P.green : P.text;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextColored(c, "%s%s", r.name.c_str(), r.e.sim ? T(" (simulation)", " (simulation)") : "");
            ImGui::TableNextColumn(); ImGui::TextColored(c, "%s %s", Num(r.e.maxMbps, 1).c_str(), T("Mo/s", "MB/s"));
            ImGui::TableNextColumn(); ImGui::TextColored(r.e.success < 99.9 ? P.red : c, "%s %%", Num(r.e.success, 2).c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(c, "%s", r.e.usb.c_str());
        }
        ImGui::EndTable();
        if (best >= 0 && rows.size() > 1) ImGui::TextColored(P.green, "%s", Fmt(T("Meilleur branchement : %s", "Best connection: %s"), rows[(size_t)best].name.c_str()).c_str());
        else if (best < 0) ImGui::TextColored(P.orange, "%s", T("Aucun branchement sans erreur pour l'instant.", "No error-free connection yet."));
    }
    ImGui::Dummy(ImVec2(0, 4 * S));
    if (Button(T("Fermer", "Close"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void DrawModals() {
    Guide();
    Cable();
}
