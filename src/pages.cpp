// pages.cpp - les 8 onglets de Camra's Tool
#include "ui.h"
#include "app.h"
#include "bench.h"
#include "diag.h"
#include "firmware.h"
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>

using namespace ui;

// ---------------------------------------------------------------------------
//  Outils communs
// ---------------------------------------------------------------------------
static float Avail() { return ImGui::GetContentRegionAvail().x; }

static bool OpenFileDlg(std::wstring& path, const wchar_t* filter, const char* title) {
    wchar_t buf[MAX_PATH * 2] = {};
    wcsncpy_s(buf, path.c_str(), _TRUNCATE);
    std::wstring dir;
    size_t k = path.find_last_of(L"\\/");
    if (k != std::wstring::npos) dir = path.substr(0, k);
    std::wstring t = U2W(title);
    OPENFILENAMEW of = { sizeof of };
    of.hwndOwner = g_hwnd;
    of.lpstrFilter = filter;
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH * 2;
    of.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    of.lpstrTitle = t.c_str();
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return false;
    path = buf;
    return true;
}

static void ExportFile(const char* ext, const std::string& content) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring name = U2W(Fmt("camras_tool_%04d%02d%02d_%02d%02d%02d.%s", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ext));
    wchar_t buf[MAX_PATH * 2] = {};
    wcsncpy_s(buf, name.c_str(), _TRUNCATE);
    bool json = strcmp(ext, "json") == 0;
    const wchar_t* filter = json ? L"JSON\0*.json\0" : L"CSV (Excel)\0*.csv\0";
    std::wstring title = U2W(T("Exporter les résultats", "Export results"));
    PWSTR docs = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) { dir = docs; CoTaskMemFree(docs); }
    OPENFILENAMEW of = { sizeof of };
    of.hwndOwner = g_hwnd;
    of.lpstrFilter = filter;
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH * 2;
    of.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    of.lpstrTitle = title.c_str();
    std::wstring defExt = U2W(ext);
    of.lpstrDefExt = defExt.c_str();
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&of)) return;
    std::string data = content;
    if (!json) data = "\xEF\xBB\xBF" + data;   // BOM : Excel lit les accents
    if (WriteFileAll(buf, data.data(), data.size())) {
        Toast(T("Résultats exportés.", "Results exported."));
        Log(L_OK, T("Export : %s", "Export: %s"), W2U(buf).c_str());
    } else {
        Toast(T("Impossible d'écrire le fichier.", "Cannot write the file."), true);
        Log(L_ERR, T("ÉCHEC export : %s", "Export FAILED: %s"), W2U(buf).c_str());
    }
}

static void CopyDiscord(const std::string& s) {
    if (!CopyToClipboard(s)) { Toast(T("Presse-papiers indisponible : réessayez.", "Clipboard unavailable: try again."), true); return; }
    int n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++;
    Toast(n > 2000 ? Fmt(T("Copié (%d caractères : Discord en accepte 2000).", "Copied (%d characters: Discord accepts 2000)."), n)
                   : Fmt(T("Copié : collez-le dans Discord (%d caractères).", "Copied: paste it into Discord (%d characters)."), n));
}

// Ligne « libellé ........ valeur »
static void KV(const char* k, const char* v, const char* help = nullptr) {
    ImGui::TextColored(P.text2, "%s", k);
    if (help) Help(help);
    float w = ImGui::CalcTextSize(v).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - w));
    ImGui::TextUnformatted(v);
}

// Bandeau coloré (simulation, connexion impossible…)
static bool Banner(ImVec4 col, const char* text, const char* button) {
    bool clicked = false;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, WithA(col, Dark ? 0.16f : 0.12f));
    ImGui::PushStyleColor(ImGuiCol_Border, WithA(col, 0.35f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * S, 10 * S));
    ImGui::BeginChild("##banner", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    float bw = button ? ImGui::CalcTextSize(button).x + 30 * S : 0;
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - bw - 10 * S);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    if (button) {
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw);
        clicked = Button(button, B_NEUTRAL, ImVec2(bw, 0));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    return clicked;
}

static ImVec4 RatingCol(int r) {
    return r == R_EXCELLENT ? P.green : r == R_GOOD ? P.teal : r == R_AVERAGE ? P.orange : r == R_POOR ? P.red : P.text3;
}

// Grande valeur avec unité (jauges, indicateurs)
static void BigValue(const char* value, const char* unit, ImVec4 col, float size = 40) {
    PushFontSz(FSemi, size);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (unit && *unit) {
        ImGui::SameLine(0, 6 * S);
        float off = (size - Base) * S * 0.62f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + off);
        ImGui::TextColored(P.text2, "%s", unit);
    }
}

static void EmptyState(const char* title, const char* text, const char* button, int goPage) {
    BeginCard("##empty");
    ImGui::Dummy(ImVec2(0, 18 * S));
    float w = Avail();
    PushFontSz(FSemi, 19);
    ImVec2 ts = ImGui::CalcTextSize(title);
    ImGui::SetCursorPosX((w - ts.x) * 0.5f + ImGui::GetStyle().WindowPadding.x);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
    ImVec2 t2 = ImGui::CalcTextSize(text, nullptr, false, w * 0.8f);
    ImGui::SetCursorPosX((w - t2.x) * 0.5f + ImGui::GetStyle().WindowPadding.x);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w * 0.8f);
    ImGui::TextColored(P.text2, "%s", text);
    ImGui::PopTextWrapPos();
    ImGui::PopTextWrapPos();
    if (button) {
        ImGui::Dummy(ImVec2(0, 6 * S));
        float bw = ImGui::CalcTextSize(button).x + 40 * S;
        ImGui::SetCursorPosX((w - bw) * 0.5f + ImGui::GetStyle().WindowPadding.x);
        if (Button(button, B_PRIMARY, ImVec2(bw, 0))) {
            if (goPage >= 0) g_page = goPage;
            else DiagStart();
        }
    }
    ImGui::Dummy(ImVec2(0, 18 * S));
    EndCard();
}

// ---------------------------------------------------------------------------
//  Boutons de l'en-tête (alignés à droite, avant le sélecteur FR | EN)
// ---------------------------------------------------------------------------
struct HBtn { const char* label; int kind; bool disabled; };
static int HeaderButtons(std::initializer_list<HBtn> btns, float rightEdge) {
    float total = 0, gap = 8 * S;
    for (auto& b : btns) total += ImGui::CalcTextSize(b.label).x + 28 * S + gap;
    float x = rightEdge - total;
    int clicked = -1, i = 0;
    float y = ImGui::GetCursorPosY();
    for (auto& b : btns) {
        float w = ImGui::CalcTextSize(b.label).x + 28 * S;
        ImGui::SetCursorPos(ImVec2(x, y));
        if (Button(b.label, b.kind, ImVec2(w, 0), b.disabled)) clicked = i;
        Hit();
        x += w + gap;
        i++;
    }
    return clicked;
}

void PageHeaderButtons(int page, float rightEdge) {
    if (page == PG_RESULTS) {
        BenchState b = BenchCopy();
        if (!b.hasResult || b.running) return;
        int c = HeaderButtons({ { T("Copier l'image", "Copy image"), B_PRIMARY, false },
                                { T("Copier pour Discord", "Copy for Discord"), B_NEUTRAL, false },
                                { T("Exporter CSV", "Export CSV"), B_NEUTRAL, false },
                                { T("Exporter JSON", "Export JSON"), B_NEUTRAL, false } }, rightEdge);
        if (c == 0) {
            if (ResultsImage(b)) Toast(T("Image copiée : collez-la dans Discord.", "Image copied: paste it into Discord."));
            else Toast(T("Presse-papiers indisponible : réessayez.", "Clipboard unavailable: try again."), true);
        }
        if (c == 1) CopyDiscord(ResultsDiscord(b));
        if (c == 2) ExportFile("csv", ResultsCsv(b));
        if (c == 3) ExportFile("json", ResultsJson(b));
    } else if (page == PG_DIAG) {
        DiagResult d = DiagCopy();
        bool has = d.done && !d.running;
        int c = HeaderButtons({ { T("Copier pour Discord", "Copy for Discord"), B_NEUTRAL, !has },
                                { T("Copier le rapport", "Copy report"), B_NEUTRAL, !has },
                                { d.running ? T("Diagnostic…", "Diagnosing…") : has ? T("Relancer le diagnostic", "Run diagnostics again") : T("Lancer le diagnostic", "Run diagnostics"),
                                  B_PRIMARY, d.running || JobRunning() } }, rightEdge);
        if (c == 0) CopyDiscord(DiagDiscord());
        if (c == 1) {
            if (CopyToClipboard(DiagReport())) Toast(T("Rapport de diagnostic copié dans le presse-papiers.", "Diagnostics report copied to the clipboard."));
            else Toast(T("Presse-papiers indisponible : réessayez.", "Clipboard unavailable: try again."), true);
        }
        if (c == 2) DiagStart();
    } else if (page == PG_LOG) {
        int c = HeaderButtons({ { T("Effacer", "Clear"), B_NEUTRAL, false }, { T("Copier", "Copy"), B_NEUTRAL, false }, { T("Signaler un problème", "Report a problem"), B_PRIMARY, false } }, rightEdge);
        if (c == 2) ReportProblem();
        if (c == 0) LogClear();
        if (c == 1) {
            std::string s;
            for (auto& l : LogCopy()) s += l.time + "  " + l.text + "\r\n";
            if (CopyToClipboard(s)) Toast(T("Journal copié dans le presse-papiers.", "Log copied to the clipboard."));
        }
    }
}

// ===========================================================================
//  Speedtest
// ===========================================================================
static const char* ModeName(int m) {
    switch (m) {
    case TM_FULL: return T("Test complet", "Full test");
    case TM_SPEED: return T("Vitesse", "Speed");
    case TM_THROUGHPUT: return T("Débit", "Throughput");
    case TM_STABILITY: return T("Stabilité", "Stability");
    }
    return T("Personnalisé", "Custom");
}

static const char* ModeDesc(int m) {
    switch (m) {
    case TM_FULL: return T("Recommandé · latence, vitesse, débit séquentiel et scatter, avec verdict", "Recommended · latency, speed, sequential and scatter throughput, with a verdict");
    case TM_SPEED: return T("Nombre de lectures par seconde et latence des petites lectures", "Reads per second and latency of small reads");
    case TM_THROUGHPUT: return T("Débit maximal en Mo/s : grosses lectures contiguës et scatter", "Maximum MB/s: large contiguous reads and scatter");
    case TM_STABILITY: return T("Lectures en continu pendant plusieurs minutes : chutes de débit, erreurs et coupures", "Continuous reads for several minutes: throughput dips, errors and dropouts");
    }
    return T("Tests choisis dans Réglages", "Tests selected in Settings");
}

static void ModeTiles(bool locked) {
    const int N = 5;
    float gap = 10 * S, w = (Avail() - gap * (N - 1)) / N;
    float h = 92 * S;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int m = 0; m < N; m++) {
        if (m) ImGui::SameLine(0, gap);
        ImGui::PushID(m);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (locked) ImGui::BeginDisabled();
        if (ImGui::InvisibleButton("##mode", ImVec2(w, h))) { g_set.testMode = m; SaveSettings(); }
        if (locked) ImGui::EndDisabled();
        bool sel = g_set.testMode == m, hov = ImGui::IsItemHovered();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), U(sel ? WithA(P.accent, Dark ? 0.16f : 0.10f) : hov ? P.card2 : P.card), 12 * S);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), U(sel ? P.accent : P.border), 12 * S, 0, sel ? 2.0f * S : 1.0f);
        PushFontSz(FSemi, Base + 1);
        dl->AddText(ImVec2(p.x + 14 * S, p.y + 12 * S), U(sel ? P.accent : P.text), ModeName(m));
        ImGui::PopFont();
        PushFontSz(FReg, Base - 2);
        dl->AddText(nullptr, 0, ImVec2(p.x + 14 * S, p.y + 36 * S), U(P.text2), ModeDesc(m), nullptr, w - 26 * S);
        ImGui::PopFont();
        ImGui::PopID();
    }
}

void PageSpeed() {
    BenchState b = BenchCopy();
    DeviceInfo d = DevInfo();
    int cs = g_connState;

    if (d.simulated) {
        ImGui::PushID("sim");
        if (Banner(P.orange, T("Mode simulation : les mesures sont fictives, aucune carte n'est testée.", "Simulation mode: results are fake, no card is being tested."),
                   T("Utiliser la carte FPGA", "Use the FPGA card"))) {
            g_set.mode = MODE_FPGA;
            SaveSettings();
            StartJob([] { DevDisconnect(); });
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 4 * S));
    } else if (cs == CS_FAILED && !b.running) {
        ImGui::PushID("fail");
        if (Banner(P.red, Fmt(T("%s — la carte DMA ne répond pas. Consultez le diagnostic et les solutions à tester.", "%s — the DMA card is not responding. Check the diagnostics and the fixes to try."),
                              T("Connexion impossible", "Connection failed")).c_str(), T("Voir le diagnostic", "View diagnostics"))) {
            g_page = PG_DIAG;
            if (!DiagCopy().done) DiagStart();
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 4 * S));
    }

    ModeTiles(b.running);
    ImGui::Dummy(ImVec2(0, 4 * S));

    // Jauges
    bool stabView = b.running ? b.stability : g_set.testMode == TM_STABILITY;
    double lat = 0, mbps = 0;
    // La jauge de latence ne concerne que les petites lectures (8 octets)
    if (b.running) { lat = b.stability ? 0 : b.current == 0 ? b.liveLat : b.res[0].latAvg; mbps = b.liveMbps; }
    else if (b.hasResult && !b.stability) { lat = b.res[0].ran ? b.res[0].latAvg : 0; mbps = BestMbps(b); }
    else if (b.hasResult && b.stability) { mbps = b.sAvg; }
    float gap = 10 * S, gw = (Avail() - gap) * 0.5f;
    BeginCard("##g1", gw);
    Section(T("LATENCE", "LATENCY"));
    BigValue(lat > 0 ? Num(lat, 1).c_str() : "—", T("µs / lecture", "µs / read"), lat > 0 ? RatingCol(RateLat(lat)) : P.text3);
    EndCard();
    ImGui::SameLine(0, gap);
    BeginCard("##g2", gw);
    Section(T("DÉBIT", "THROUGHPUT"));
    BigValue(mbps > 0 ? Num(mbps, 1).c_str() : "—", T("Mo/s", "MB/s"), mbps > 0 ? RatingCol(RateMbps(mbps)) : P.text3);
    EndCard();
    ImGui::Dummy(ImVec2(0, 4 * S));

    // Lancer / arrêter
    float bw = 260 * S;
    if (b.running) {
        if (Button(b.stopping ? T("Arrêt…", "Stopping…") : T("Arrêter", "Stop"), B_DANGER, ImVec2(bw, 40 * S))) BenchStop();
        ImGui::SameLine(0, 14 * S);
        ImGui::BeginGroup();
        if (b.stability) {
            int e = (int)b.stabElapsed, t = (int)b.stabTotal;
            ImGui::Text("%s · %d:%02d / %d:%02d", T("Test de stabilité", "Stability test"), e / 60, e % 60, t / 60, t % 60);
            Progress((float)b.progress, Avail(), P.accent);
        } else {
            int idx = (int)(std::find(b.order.begin(), b.order.end(), b.current) - b.order.begin());
            if (b.runs > 1) { ImGui::TextColored(P.text2, T("Passage %d / %d", "Run %d / %d"), b.run + 1, b.runs); ImGui::SameLine(); }
            ImGui::Text("%s · %d / %d", b.current >= 0 ? (g_en ? TESTS[b.current].nameEn : TESTS[b.current].nameFr) : "", idx + 1, (int)b.order.size());
            float tot = (float)b.order.size();
            Progress(tot > 0 ? (float)(idx + b.progress) / tot : 0, Avail(), P.accent);
        }
        ImGui::EndGroup();
    } else {
        const char* label = g_set.testMode == TM_FULL ? T("Lancer le test complet", "Run full test")
                          : g_set.testMode == TM_STABILITY ? T("Lancer le test de stabilité", "Start the stability test")
                                                           : T("Lancer le test", "Run test");
        bool busy = JobRunning();
        if (Button(cs == CS_CONNECTING ? T("Connexion…", "Connecting…") : label, B_PRIMARY, ImVec2(bw, 40 * S), busy)) BenchStart(g_set.testMode);
        ImGui::SameLine(0, 14 * S);
        ImGui::BeginGroup();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 * S);
        if (g_set.testMode == TM_STABILITY)
            ImGui::TextColored(P.text2, "%s", Fmt(T("Débit en continu · %d min · scatter 256 pages aléatoires, sans pause", "Continuous throughput · %d min · 256 random-page scatter, non-stop"), g_set.stabMin).c_str());
        else {
            int n = (int)TestsForMode(g_set.testMode).size();
            ImGui::TextColored(P.text2, "%s", Fmt(T("%d tests · ~%d s", "%d tests · ~%d s"), n, n * g_set.duration * g_set.runs).c_str());
        }
        Tx err;
        std::vector<Range> r = TestRanges(&err);
        Small(P.text3, r.empty() ? err.c() : Fmt(T("Zone %s · lecture seule", "Range %s · read-only"), ZoneText(r).c()).c_str(), false);
        ImGui::EndGroup();
    }
    ImGui::Dummy(ImVec2(0, 6 * S));

    if (stabView) {
        // Stabilité : courbe du débit seconde par seconde + compteurs
        BeginCard("##stab");
        Section(T("Débit au fil du temps", "Throughput over time"));
        std::vector<float> v;
        std::vector<unsigned char> dips, drops;
        for (auto& s : b.stab) { v.push_back(s.mbps); dips.push_back(s.dip); drops.push_back(s.drop); }
        int cap = b.stability ? std::max((int)b.stabTotal, 10) : g_set.stabMin * 60;
        if (v.empty()) {
            ImGui::Dummy(ImVec2(0, 6 * S));
            ImGui::TextColored(P.text3, "%s", b.running ? T("Mesure en cours…", "Measuring…") : T("La courbe s'affiche pendant le test", "The chart appears during the test"));
            ImGui::Dummy(ImVec2(0, 150 * S));
        } else Chart("stabc", v.data(), (int)v.size(), cap, ImVec2(Avail(), 190 * S), P.accent, T("Mo/s", "MB/s"), dips.data(), drops.data());
        if (b.stability && !b.stab.empty()) {
            ImGui::Dummy(ImVec2(0, 4 * S));
            if (ImGui::BeginTable("##st", 3, ImGuiTableFlags_SizingStretchSame)) {
                auto cell = [&](const char* k, const std::string& val, ImVec4 col, const char* help) {
                    ImGui::TableNextColumn();
                    ImGui::TextColored(P.text2, "%s", k);
                    if (help) Help(help);
                    PushFontSz(FSemi, Base + 3);
                    ImGui::TextColored(col, "%s", val.c_str());
                    ImGui::PopFont();
                };
                cell(T("Débit moyen", "Average throughput"), Num(b.sAvg, 1) + T(" Mo/s", " MB/s"), P.text, nullptr);
                cell(T("Min · max", "Min · max"), Fmt(T("min %s · max %s Mo/s", "min %s · max %s MB/s"), Num(b.sMin, 1).c_str(), Num(b.sMax, 1).c_str()), P.text, nullptr);
                cell(T("Lectures réussies", "Successful reads"), Num(b.sSuccess, 2) + " %", b.sSuccess < 99.5 ? P.red : P.text, nullptr);
                cell(T("Pages en échec", "Failed pages"), Num((double)b.sFailed, 0), b.sFailed ? P.orange : P.text, nullptr);
                cell(T("Chutes de débit", "Throughput dips"), std::to_string(b.sDips), b.sDips ? P.orange : P.text,
                     T("Chute : une seconde où le débit tombe sous 60 % du débit habituel. Quelques chutes isolées sont normales ; des chutes répétées indiquent un câble, un port USB ou une carte qui chauffe.",
                       "Dip: a second where throughput falls below 60% of the usual rate. A few isolated dips are normal; repeated dips point to a cable, a USB port or an overheating card."));
                cell(T("Coupures", "Dropouts"), std::to_string(b.sDrops), b.sDrops ? P.red : P.text,
                     T("Coupure : plus aucune lecture ne réussit pendant au moins une seconde (carte déconnectée, câble qui bouge, PC cible en veille). Une seule coupure suffit à rendre le lien instable.",
                       "Dropout: no read succeeds for at least one second (card disconnected, loose cable, target PC asleep). A single dropout makes the link unstable."));
                ImGui::EndTable();
            }
            int v2 = StabVerdict(b);
            ImGui::Dummy(ImVec2(0, 4 * S));
            ImVec4 vc = v2 == 0 ? P.green : v2 == 1 ? P.orange : P.red;
            const char* vt = v2 == 0 ? T("Lien stable : aucune coupure, débit régulier.", "Stable link: no dropout, steady throughput.")
                           : v2 == 1 ? T("Lien correct, avec des chutes ou des erreurs : vérifiez le câble et le port USB 3.", "Decent link, with dips or errors: check the cable and the USB 3 port.")
                                     : T("Lien instable : coupures ou nombreuses erreurs. Voir le diagnostic.", "Unstable link: dropouts or many errors. See the diagnostics.");
            ImGui::TextColored(vc, "%s%s", b.running ? T("Verdict provisoire : ", "Provisional verdict: ") : "", vt);
        }
        double drift = StabDrift(b.stab);
        if (drift < -8)
            ImGui::TextColored(P.orange, "%s", Fmt(T("Le débit baisse lentement : %s %% depuis le début. La carte chauffe peut-être.", "Throughput is slowly dropping: %s%% since the start. The card may be heating up."), Num(drift, 0).c_str()).c_str());
        if (b.running) Small(P.text3, T("Laissez le PC cible allumé et ne touchez pas au câble pendant le test.", "Keep the target PC on and do not touch the cable during the test."));
        EndCard();
        return;
    }

    // Étapes + courbe
    std::vector<int> order = b.running || (b.hasResult && !b.stability) ? b.order : TestsForMode(g_set.testMode);
    float lw = Avail() * 0.42f;
    BeginCard("##steps", lw);
    Section(T("Étapes", "Steps"));
    if (b.running) Small(P.text3, Fmt(T("%d tests · ~%d s", "%d tests · ~%d s"), (int)order.size(), (int)(order.size() * b.duration * b.runs)).c_str(), false);
    else if (b.hasResult && !b.stability)
        Small(P.text3, b.stopped ? Fmt(T("Interrompu · %d / %d", "Stopped · %d / %d"), b.doneCount, (int)order.size()).c_str()
                                 : Fmt(T("Terminé · %d tests", "Done · %d tests"), (int)order.size()).c_str(), false);
    ImGui::Dummy(ImVec2(0, 2 * S));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int ti : order) {
        const TestDef& t = TESTS[ti];
        const TestResult& r = b.res[ti];
        bool cur = b.running && b.current == ti;
        ImVec2 p = ImGui::GetCursorScreenPos();
        float rowH = 42 * S;
        ImGui::Dummy(ImVec2(Avail(), rowH));
        ImVec2 c = ImVec2(p.x + 10 * S, p.y + rowH * 0.5f);
        if (cur) Spinner(c, 8 * S, P.accent);
        else if (r.done && r.calls) StatusIcon(dl, r.success < 99.5 ? 2 : 0, c, 9 * S);
        else dl->AddCircle(c, 8 * S, U(P.text3), 24, 1.5f * S);
        dl->AddText(ImVec2(p.x + 28 * S, p.y + 5 * S), U(cur ? P.accent : P.text), g_en ? t.nameEn : t.nameFr);
        PushFontSz(FReg, Base - 2);
        dl->AddText(ImVec2(p.x + 28 * S, p.y + 23 * S), U(P.text3), g_en ? t.descEn : t.descFr);
        ImGui::PopFont();
        std::string val;
        if (r.calls) val = ti == 0 ? Num(r.latAvg, 1) + " µs" : Num(r.mbps, 1) + T(" Mo/s", " MB/s");
        else val = cur ? "" : T("En attente", "Pending");
        ImVec2 vs = ImGui::CalcTextSize(val.c_str());
        dl->AddText(ImVec2(p.x + Avail() - vs.x, p.y + (rowH - vs.y) * 0.5f), U(r.calls ? P.text : P.text3), val.c_str());
        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton(t.key, ImVec2(Avail(), rowH));
        Help(g_en ? t.helpEn : t.helpFr);
    }
    EndCard();
    ImGui::SameLine(0, 10 * S);
    BeginCard("##curve");
    int ct = b.curveTest >= 0 ? b.curveTest : -1;
    Section(ct >= 0 ? Fmt(T("Courbe · %s", "Chart · %s"), g_en ? TESTS[ct].nameEn : TESTS[ct].nameFr).c_str() : T("Courbe", "Chart"));
    if (b.curve.size() < 2) {
        ImGui::Dummy(ImVec2(0, 8 * S));
        ImGui::TextColored(P.text3, "%s", b.running ? T("Mesure en cours…", "Measuring…") : T("La courbe s'affiche pendant le test", "The chart appears during the test"));
        ImGui::Dummy(ImVec2(0, std::max(60.f * S, order.size() * 42 * S - 40 * S)));
    } else {
        int cap = std::max((int)(b.duration * 10), (int)b.curve.size());
        Chart("curve", b.curve.data(), (int)b.curve.size(), cap, ImVec2(Avail(), std::max(160.f * S, order.size() * 42 * S - 10 * S)),
              ct == 0 ? P.orange : P.accent, ct == 0 ? T("µs par lecture", "µs per read") : T("Mo/s", "MB/s"));
    }
    EndCard();
}

// ===========================================================================
//  Résultats
// ===========================================================================
void PageResults() {
    BenchState b = BenchCopy();
    if (!b.hasResult) {
        EmptyState(T("Aucun résultat", "No results"), T("Lancez un test depuis l'onglet Speedtest pour voir les mesures ici.", "Run a test from the Speedtest tab to see results here."),
                   T("Aller au Speedtest", "Go to Speedtest"), PG_SPEED);
        return;
    }
    if (b.simulated) {
        Banner(P.orange, T("Mode simulation : les mesures sont fictives, aucune carte n'est testée.", "Simulation mode: results are fake, no card is being tested."), nullptr);
        ImGui::Dummy(ImVec2(0, 4 * S));
    }
    ImGui::TextColored(P.text2, "%s", Fmt(T("FPGA %s · firmware %s · %s · LeechCore « %s »", "FPGA %s · firmware %s · %s · LeechCore \"%s\""), b.fpga.c(), b.fw.c(), b.usb.c(), b.devString.c_str()).c_str());
    if (b.stability)
        ImGui::TextColored(P.text3, "%s", Fmt(T("Zone %s · scatter 256 pages · %s", "Range %s · 256-page scatter · %s"), b.zone.c(), b.date.c_str()).c_str());
    else
        ImGui::TextColored(P.text3, "%s", Fmt(T("Zone %s · %.0f s par test · %s", "Range %s · %.0f s per test · %s"), b.zone.c(), b.duration, b.date.c_str()).c_str());
    ImGui::Dummy(ImVec2(0, 4 * S));

    if (b.stability) {
        int v = StabVerdict(b);
        float gap = 10 * S, w = (Avail() - gap * 2) / 3;
        BeginCard("##k1", w);
        Section(T("Débit moyen", "Average throughput"));
        BigValue(Num(b.sAvg, 1).c_str(), T("Mo/s", "MB/s"), RatingCol(RateMbps(b.sAvg)), 34);
        Small(P.text3, Fmt(T("min %s · max %s Mo/s", "min %s · max %s MB/s"), Num(b.sMin, 1).c_str(), Num(b.sMax, 1).c_str()).c_str(), false);
        EndCard();
        ImGui::SameLine(0, gap);
        BeginCard("##k2", w);
        Section(T("Lectures réussies", "Successful reads"));
        BigValue(Num(b.sSuccess, 2).c_str(), "%", b.sSuccess < 99.5 ? P.red : P.green, 34);
        Small(P.text3, Fmt(T("%s page%s en échec", "%s failed page%s"), Num((double)b.sFailed, 0).c_str(), Pl((long long)b.sFailed).c_str()).c_str(), false);
        EndCard();
        ImGui::SameLine(0, gap);
        BeginCard("##k3", w);
        Section(T("Verdict", "Verdict"));
        PushFontSz(FSemi, 34);
        ImGui::TextColored(v == 0 ? P.green : v == 1 ? P.orange : P.red, "%s", v == 0 ? "Stable" : v == 1 ? T("Moyen", "Fair") : T("Instable", "Unstable"));
        ImGui::PopFont();
        Small(P.text3, Fmt(T("%d coupure%s · %d chute%s de débit", "%d dropout%s · %d throughput dip%s"), b.sDrops, Pl(b.sDrops).c_str(), b.sDips, Pl(b.sDips).c_str()).c_str(), false);
        EndCard();
        ImGui::Dummy(ImVec2(0, 4 * S));
        BeginCard("##sc");
        int e = (int)b.stabElapsed;
        Section(Fmt(T("Test de stabilité · %d:%02d", "Stability test · %d:%02d"), e / 60, e % 60).c_str());
        std::vector<float> vv;
        std::vector<unsigned char> dips, drops;
        for (auto& s : b.stab) { vv.push_back(s.mbps); dips.push_back(s.dip); drops.push_back(s.drop); }
        if (!vv.empty()) Chart("sr", vv.data(), (int)vv.size(), (int)vv.size(), ImVec2(Avail(), 200 * S), P.accent, T("Mo/s", "MB/s"), dips.data(), drops.data());
        EndCard();
        return;
    }

    // Indicateurs
    int bi = -1;
    double best = BestMbps(b, &bi);
    u64 failed = 0;
    double st = Stability(b, &failed);
    float gap = 10 * S, w = (Avail() - gap * 2) / 3;
    BeginCard("##k1", w);
    Section(T("Débit maximal", "Max throughput"));
    Help(T("Meilleur débit mesuré sur les tests séquentiels et scatter. Repères : moins de 30 Mo/s faible, 30 à 80 moyen, 80 à 150 bon, plus de 150 excellent.",
           "Best throughput measured on sequential and scatter tests. Reference: under 30 MB/s poor, 30 to 80 average, 80 to 150 good, over 150 excellent."));
    BigValue(best > 0 ? Num(best, 1).c_str() : "—", T("Mo/s", "MB/s"), RatingCol(RateMbps(best)), 34);
    Small(RatingCol(RateMbps(best)), best > 0 ? Fmt("%s · %s", RatingText(RateMbps(best)), bi >= 0 ? (g_en ? TESTS[bi].nameEn : TESTS[bi].nameFr) : "").c_str() : "-", false);
    EndCard();
    ImGui::SameLine(0, gap);
    BeginCard("##k2", w);
    Section(T("Latence moyenne", "Average latency"));
    Help(T("Temps moyen d'une petite lecture. Repères : moins de 60 µs excellent, moins de 120 bon, moins de 250 moyen, au-delà lent.",
           "Average time of a small read. Reference: under 60 µs excellent, under 120 good, under 250 average, slow beyond."));
    const TestResult& L = b.res[0];
    if (L.ran && L.calls) {
        BigValue(Num(L.latAvg, 1).c_str(), "µs", RatingCol(RateLat(L.latAvg)), 34);
        Small(RatingCol(RateLat(L.latAvg)), Fmt("%s · %s", RatingText(RateLat(L.latAvg)), Fmt(T("%s lectures/s", "%s reads/s"), Num(L.callsPerSec, 0).c_str()).c_str()).c_str(), false);
    } else {
        BigValue("—", "µs", P.text3, 34);
        Small(P.text3, T("Test de latence non exécuté", "Latency test not run"), false);
    }
    EndCard();
    ImGui::SameLine(0, gap);
    BeginCard("##k3", w);
    Section(T("Stabilité", "Stability"));
    Help(T("Pourcentage de lectures réussies. En dessous de 99,5 %, le lien est instable : câble, port USB, firmware ou algorithme FPGA.",
           "Percentage of successful reads. Below 99.5%, the link is unstable: cable, USB port, firmware or FPGA algorithm."));
    BigValue(Num(st, 2).c_str(), "%", st < 99.5 ? P.red : P.green, 34);
    Small(P.text3, Fmt(T("%s lecture%s en échec", "%s failed read%s"), Num((double)failed, 0).c_str(), Pl((long long)failed).c_str()).c_str(), false);
    EndCard();
    ImGui::Dummy(ImVec2(0, 4 * S));

    // Ce qui limite le débit
    if (best > 0 && (b.res[3].ran || b.res[4].ran || b.res[6].ran || b.res[7].ran)) {
        Tx lim = Bottleneck(best, b.usbSpeed, b.pcieLink);
        Wrapped(best >= 150 ? P.text2 : P.orange, lim.c());
        ImGui::Dummy(ImVec2(0, 2 * S));
    }

    // Référence : cartes du même modèle déjà mesurées
    {
        int lvl = 0;
        Tx ref = CardReference(b.fpga.fr, best, L.ran && L.calls ? L.latAvg : 0, b.usbSpeed, b.pcieLink, &lvl);
        if (!b.simulated && !ref.fr.empty()) {
            Wrapped(lvl == 0 ? P.green : lvl == 1 ? P.orange : P.text3, ref.c());
            ImGui::Dummy(ImVec2(0, 2 * S));
        }
    }

    // Repère : meilleurs résultats déjà obtenus avec cette carte sur ce PC
    {
        double refM = 0, refL = 0;
        int n = 0;
        for (auto& e : HistoryCopy()) {
            if (e.kind != 0 || e.sim != b.simulated || e.fpga != b.fpga.s() || b.date.compare(0, 16, e.date) == 0) continue;
            n++;
            refM = std::max(refM, e.maxMbps);
            if (e.latAvg > 0 && (refL == 0 || e.latAvg < refL)) refL = e.latAvg;
        }
        if (n && refM > 0 && best > 0) {
            double pm = best / refM * 100;
            std::string s = Fmt(T("Par rapport à vos %d test%s avec cette carte : débit à %s %% du record (%s Mo/s)", "Compared with your %d test%s with this card: throughput at %s%% of the record (%s MB/s)"),
                                n, Pl(n).c_str(), Num(pm, 0).c_str(), Num(refM, 1).c_str());
            if (refL > 0 && L.calls) s += Fmt(T(" · latence %s µs, record %s µs", " · latency %s µs, record %s µs"), Num(L.latAvg, 1).c_str(), Num(refL, 1).c_str());
            ImGui::TextColored(pm < 80 ? P.orange : P.text2, "%s", s.c_str());
            ImGui::Dummy(ImVec2(0, 2 * S));
        }
    }

    // Répartition de la latence
    if (L.calls && !L.hist.empty()) {
        BeginCard("##hist");
        Section(T("Répartition de la latence", "Latency distribution"));
        Help(T("Part des lectures pour chaque durée. Un pic étroit à gauche = carte régulière. La dernière barre regroupe les lectures les plus lentes.",
               "Share of reads for each duration. A narrow peak on the left = steady card. The last bar gathers the slowest reads."));
        ImVec2 hp = ImGui::GetCursorScreenPos();
        ImVec2 hs = ImVec2(Avail(), 90 * S);
        ImGui::InvisibleButton("##h", hs);
        bool hov = ImGui::IsItemHovered();
        ImDrawList* hd = ImGui::GetWindowDrawList();
        float mx = *std::max_element(L.hist.begin(), L.hist.end());
        float bw = hs.x / L.hist.size();
        for (size_t i = 0; i < L.hist.size(); i++) {
            float bh = mx > 0 ? L.hist[i] / mx * hs.y : 0;
            if (L.hist[i] > 0) bh = std::max(bh, 2 * S);
            hd->AddRectFilled(ImVec2(hp.x + bw * i + 1, hp.y + hs.y - bh), ImVec2(hp.x + bw * (i + 1) - 1, hp.y + hs.y), U(i + 1 == L.hist.size() ? P.orange : P.accent), 2 * S);
        }
        if (hov) {
            int i = (int)((ImGui::GetIO().MousePos.x - hp.x) / bw);
            if (i >= 0 && i < (int)L.hist.size()) {
                double lo = L.histMin + i * L.histStep;
                if (i + 1 == (int)L.hist.size()) ImGui::SetTooltip("> %.0f µs : %.2f %%", lo, L.hist[(size_t)i]);
                else ImGui::SetTooltip("%.0f – %.0f µs : %.2f %%", lo, lo + L.histStep, L.hist[(size_t)i]);
            }
        }
        Small(P.text2, Fmt(T("min %s · médiane %s · p99 %s · p99,9 %s · max %s µs · %s pics (plus de 2 fois la médiane)", "min %s · median %s · p99 %s · p99.9 %s · max %s µs · %s spikes (over twice the median)"),
                           Num(L.latMin, 0).c_str(), Num(L.latP50, 0).c_str(), Num(L.latP99, 0).c_str(), Num(L.latP999, 0).c_str(), Num(L.latMax, 0).c_str(), Num((double)L.spikes, 0).c_str()).c_str());
        EndCard();
        ImGui::Dummy(ImVec2(0, 4 * S));
    }

    // Tableau
    BeginCard("##tbl");
    if (ImGui::BeginTable("##res", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn(T("Débit", "Throughput"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(T("Appels/s", "Calls/s"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(T("Lat. moy.", "Avg lat."), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("p99", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(T("Succès", "Success"), ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int c = 0; c < 6; c++) { ImGui::TableSetColumnIndex(c); ImGui::TextColored(P.text2, "%s", ImGui::TableGetColumnName(c)); }
        for (int i = 0; i < 8; i++) {
            const TestResult& r = b.res[i];
            if (!r.ran) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(g_en ? TESTS[i].nameEn : TESTS[i].nameFr);
            Help(g_en ? TESTS[i].helpEn : TESTS[i].helpFr);
            Small(P.text3, g_en ? TESTS[i].descEn : TESTS[i].descFr, false);
            ImGui::TableNextColumn();
            if (i == 0) ImGui::TextColored(P.text3, "—"); else ImGui::TextColored(RatingCol(RateMbps(r.mbps)), "%s %s", Num(r.mbps, 1).c_str(), T("Mo/s", "MB/s"));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Num(r.callsPerSec, 0).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted((Num(r.latAvg, 1) + " µs").c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted((Num(r.latP99, 1) + " µs").c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(r.success < 99.5 ? P.red : P.text, "%s %%", Num(r.success, 2).c_str());
        }
        ImGui::EndTable();
    }
    EndCard();
}

// ===========================================================================
//  Historique
// ===========================================================================
static std::string HistSummary(const HistEntry& e) {
    if (e.kind == 1)
        return Fmt(T("%d min · %s Mo/s moy. · %d coupure%s", "%d min · %s MB/s avg · %d dropout%s"), (int)(e.stabMin + 0.5), Num(e.sAvg, 1).c_str(), e.sDrops, Pl(e.sDrops).c_str());
    if (e.latAvg > 0)
        return Fmt(T("%s Mo/s · %s µs · %s %%", "%s MB/s · %s µs · %s %%"), Num(e.maxMbps, 1).c_str(), Num(e.latAvg, 1).c_str(), Num(e.success, 2).c_str());
    return Fmt(T("%s Mo/s · %s %%", "%s MB/s · %s %%"), Num(e.maxMbps, 1).c_str(), Num(e.success, 2).c_str());
}

void PageHistory() {
    std::vector<HistEntry> h = HistoryCopy();
    static int selA = -1, selB = -1;
    static char note[160] = "";
    static int noteFor = -2;
    if (selA >= (int)h.size()) selA = -1;
    if (selB >= (int)h.size()) selB = -1;
    if (h.empty()) {
        EmptyState(T("Aucun test enregistré", "No saved test"),
                   T("Chaque test terminé s'enregistre ici automatiquement, pour comparer câbles, ports et firmwares.", "Every finished test is saved here automatically, to compare cables, ports and firmwares."),
                   T("Aller au Speedtest", "Go to Speedtest"), PG_SPEED);
        return;
    }
    Wrapped(P.text2, T("Chaque test terminé est enregistré ici avec la date, la carte, le lien USB et une note libre (ex. « nouveau câble »). Cliquez sur un test pour le mettre en A, sur « B » pour le comparer.",
                       "Every finished test is saved here with the date, the card, the USB link and a free note (e.g. \"new cable\"). Click a test to make it A, click \"B\" to compare."));
    ImGui::Dummy(ImVec2(0, 4 * S));
    float lw = Avail() * 0.5f;
    BeginCard("##hl", lw);
    int del = -1;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < (int)h.size(); i++) {
        const HistEntry& e = h[(size_t)i];
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float rowH = 50 * S, w = Avail();
        float btnW = 34 * S;
        if (ImGui::InvisibleButton("##row", ImVec2(w - btnW * 2 - 12 * S, rowH))) { selA = i; if (selB == i) selB = -1; }
        bool hov = ImGui::IsItemHovered();
        if (selA == i) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), U(WithA(P.accent, Dark ? 0.18f : 0.10f)), 8 * S);
        else if (selB == i) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), U(WithA(P.purple, Dark ? 0.18f : 0.10f)), 8 * S);
        else if (hov) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), U(P.card2), 8 * S);
        float x = p.x + 8 * S;
        if (selA == i || selB == i) {
            const char* tag = selA == i ? "A" : "B";
            ImVec4 tc = selA == i ? P.accent : P.purple;
            dl->AddRectFilled(ImVec2(x, p.y + 15 * S), ImVec2(x + 20 * S, p.y + 35 * S), U(tc), 6 * S);
            ImVec2 ts = ImGui::CalcTextSize(tag);
            dl->AddText(ImVec2(x + 10 * S - ts.x * 0.5f, p.y + 25 * S - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), tag);
            x += 28 * S;
        }
        std::string head = e.date + " · " + (e.kind == 1 ? T("Stabilité", "Stability") : ModeName(e.mode)) + (e.sim ? T(" · simulation", " · simulation") : "");
        dl->AddText(ImVec2(x, p.y + 6 * S), U(P.text), head.c_str());
        PushFontSz(FReg, Base - 2);
        std::string sub = HistSummary(e) + (e.note.empty() ? "" : "  ·  " + e.note);
        dl->PushClipRect(ImVec2(x, p.y), ImVec2(p.x + w - btnW * 2 - 14 * S, p.y + rowH), true);
        dl->AddText(ImVec2(x, p.y + 27 * S), U(P.text2), sub.c_str());
        dl->PopClipRect();
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - btnW * 2 - 6 * S, p.y + (rowH - ImGui::GetFrameHeight()) * 0.5f));
        if (Button("B", selB == i ? B_PRIMARY : B_NEUTRAL, ImVec2(btnW, 0), selA == i)) selB = selB == i ? -1 : i;
        ImGui::SameLine(0, 6 * S);
        if (Button("×", B_NEUTRAL, ImVec2(btnW, 0))) del = i;
        Help(T("Supprimer", "Delete"));
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH + 2 * S));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }
    EndCard();
    if (del >= 0) {
        std::wstring q = U2W(Fmt(T("Supprimer le test du %s de l'historique ?", "Delete the %s test from the history?"), h[(size_t)del].date.c_str()));
        if (MessageBoxW(g_hwnd, q.c_str(), L"Camra's Tool", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            HistoryDelete(del);
            Toast(T("Test supprimé de l'historique.", "Test deleted from the history."));
            selA = selB = -1;
            noteFor = -2;
        }
        return;
    }
    ImGui::SameLine(0, 10 * S);
    ImGui::BeginGroup();
    if (selA < 0) {
        BeginCard("##hint");
        ImGui::TextColored(P.text2, "%s", T("Cliquez sur un test pour voir le détail.", "Click a test to see its details."));
        EndCard();
    } else {
        const HistEntry& a = h[(size_t)selA];
        BeginCard("##ha");
        Section(T("Test A", "Test A"));
        ImGui::TextUnformatted((a.date + " · " + (a.kind == 1 ? T("Stabilité", "Stability") : ModeName(a.mode))).c_str());
        Small(P.text2, Fmt("FPGA %s · firmware %s · %s · %s %s", a.fpga.c_str(), a.fw.c_str(), a.usb.c_str(), T("RAM", "RAM"), a.ram.c_str()).c_str());
        Small(P.text3, Fmt(T("LeechCore « %s »", "LeechCore \"%s\""), a.device.c_str()).c_str(), false);
        ImGui::Dummy(ImVec2(0, 2 * S));
        if (noteFor != selA) { strncpy_s(note, a.note.c_str(), _TRUNCATE); noteFor = selA; }
        ImGui::TextColored(P.text2, "%s", T("Note", "Note"));
        ImGui::SetNextItemWidth(Avail());
        ImGui::InputTextWithHint("##note", T("ex. nouveau câble, port arrière", "e.g. new cable, rear port"), note, sizeof note);
        if (ImGui::IsItemDeactivatedAfterEdit()) HistorySetNote(selA, note);
        EndCard();
        ImGui::Dummy(ImVec2(0, 4 * S));
        BeginCard("##cmp");
        Section(T("Comparaison", "Comparison"));
        if (selB < 0) {
            ImGui::TextColored(P.text3, "%s", T("Cliquez sur « B » à côté d'un autre test pour le comparer à A.", "Click \"B\" next to another test to compare it with A."));
        } else {
            const HistEntry& bb = h[(size_t)selB];
            if (bb.kind != a.kind) ImGui::TextColored(P.orange, "%s", T("A et B ne sont pas du même type de test : comparaison impossible.", "A and B are not the same kind of test: they cannot be compared."));
            else if (ImGui::BeginTable("##c", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                const char* hd[4] = { T("Mesure", "Metric"), "A", "B", T("Écart", "Difference") };
                for (int c = 0; c < 4; c++) { ImGui::TableSetColumnIndex(c); ImGui::TextColored(P.text2, "%s", hd[c]); }
                // higherBetter : true si une valeur plus haute est meilleure
                auto row = [&](const char* name, double va, double vb, int dec, const char* unit, bool higherBetter) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
                    ImGui::TableNextColumn(); ImGui::Text("%s%s", Num(va, dec).c_str(), unit);
                    ImGui::TableNextColumn(); ImGui::Text("%s%s", Num(vb, dec).c_str(), unit);
                    ImGui::TableNextColumn();
                    double d = vb - va;
                    if (va != 0 && std::fabs(d) > 1e-9) {
                        double pct = d / std::fabs(va) * 100;
                        bool better = higherBetter ? d > 0 : d < 0;
                        ImGui::TextColored(better ? P.green : P.red, "%s%s %%", d > 0 ? "+" : "", Num(pct, 1).c_str());
                    } else ImGui::TextColored(P.text3, "=");
                };
                if (a.kind == 1) {
                    row(T("Débit moyen", "Average throughput"), a.sAvg, bb.sAvg, 1, T(" Mo/s", " MB/s"), true);
                    row(T("Débit min", "Min throughput"), a.sMin, bb.sMin, 1, T(" Mo/s", " MB/s"), true);
                    row(T("Débit max", "Max throughput"), a.sMax, bb.sMax, 1, T(" Mo/s", " MB/s"), true);
                    row(T("Lectures réussies", "Successful reads"), a.sSucc, bb.sSucc, 2, " %", true);
                    row(T("Chutes de débit", "Throughput dips"), a.sDips, bb.sDips, 0, "", false);
                    row(T("Coupures", "Dropouts"), a.sDrops, bb.sDrops, 0, "", false);
                    row(T("Durée (min)", "Duration (min)"), a.stabMin, bb.stabMin, 1, "", true);
                } else {
                    row(T("Débit maximal", "Max throughput"), a.maxMbps, bb.maxMbps, 1, T(" Mo/s", " MB/s"), true);
                    if (a.latAvg > 0 && bb.latAvg > 0) row(T("Latence moyenne", "Average latency"), a.latAvg, bb.latAvg, 1, " µs", false);
                    row(T("Stabilité", "Stability"), a.success, bb.success, 2, " %", true);
                    for (int i = 1; i < 8; i++)
                        if (a.ran[i] && bb.ran[i]) row(g_en ? TESTS[i].nameEn : TESTS[i].nameFr, a.mbps[i], bb.mbps[i], 1, T(" Mo/s", " MB/s"), true);
                }
                ImGui::EndTable();
                Small(P.text3, Fmt("B : %s · %s", bb.date.c_str(), bb.note.empty() ? bb.usb.c_str() : bb.note.c_str()).c_str(), false);
            }
        }
        EndCard();
    }
    ImGui::EndGroup();
}

// ===========================================================================
//  Appareil
// ===========================================================================
void PageDevice() {
    DeviceInfo d = DevInfo();
    int cs = g_connState;
    bool busy = JobRunning();
    float gap = 10 * S, lw = (Avail() - gap) * 0.55f;
    ImGui::BeginGroup();
    BeginCard("##conn", lw);
    Section(T("Connexion", "Connection"));
    const char* modes[3] = { "FPGA", "Simulation", T("Personnalisé", "Custom") };
    ImGui::TextColored(P.text2, "%s", T("Périphérique", "Device"));
    Help(T("FPGA : votre carte DMA réelle (USB FT601). Simulation : carte virtuelle pour essayer l'application sans matériel, les mesures sont fictives. Personnalisé : chaîne LeechCore libre (ex. fpga://algo=2, file://dump.raw).",
           "FPGA: your real DMA card (USB FT601). Simulation: a virtual card to try the app without hardware, results are fake. Custom: any LeechCore string (e.g. fpga://algo=2, file://dump.raw)."));
    bool changed = false;
    ImGui::SetNextItemWidth(Avail());
    if (ImGui::Combo("##mode", &g_set.mode, modes, 3)) changed = true;
    if (g_set.mode == MODE_CUSTOM) {
        ImGui::TextColored(P.text2, "%s", T("Chaîne", "String"));
        Help(T("Chaîne de périphérique transmise telle quelle à LeechCore. Exemples : fpga://algo=0, fpga://deviceindex=1, file://C:\\dump.raw.",
               "Device string passed as-is to LeechCore. Examples: fpga://algo=0, fpga://deviceindex=1, file://C:\\dump.raw."));
        ImGui::SetNextItemWidth(Avail());
        ImGui::InputTextWithHint("##custom", "fpga://algo=0  ou  file://C:\\dump.raw", g_set.custom, sizeof g_set.custom);
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
    }
    ImGui::TextColored(P.text2, "%s", T("Chemin de leechcore.dll", "Path to leechcore.dll"));
    Help(T("Bibliothèque LeechCore (x64) qui pilote la carte. FTD3XX.dll et vcruntime140.dll doivent se trouver dans le même dossier.",
           "LeechCore library (x64) that drives the card. FTD3XX.dll and vcruntime140.dll must be in the same folder."));
    static char dllBuf[MAX_PATH * 2];
    static std::wstring shown = L"\x1";
    std::wstring cur = ResolveDll();
    if (shown != cur && !ImGui::IsAnyItemActive()) { strncpy_s(dllBuf, W2U(cur).c_str(), _TRUNCATE); shown = cur; }
    ImGui::SetNextItemWidth(Avail() - 44 * S);
    ImGui::InputText("##dll", dllBuf, sizeof dllBuf);
    if (ImGui::IsItemDeactivatedAfterEdit()) { g_set.dll = U2W(dllBuf); shown = L"\x1"; changed = true; }
    ImGui::SameLine(0, 6 * S);
    if (Button("…", B_NEUTRAL, ImVec2(38 * S, 0))) {
        std::wstring p = cur;
        if (OpenFileDlg(p, L"Bibliothèques (*.dll)\0*.dll\0", T("Sélectionner leechcore.dll", "Select leechcore.dll"))) { g_set.dll = p; shown = L"\x1"; changed = true; }
    }
    std::wstring builtin = BuiltinLibDir() + L"\\leechcore.dll";
    if (LowerW(cur) != LowerW(builtin) && FileExists(builtin)) {
        if (Button(T("Utiliser la DLL intégrée", "Use built-in DLL"), B_GHOST)) { g_set.dll.clear(); shown = L"\x1"; changed = true; }
    } else Small(P.text3, Fmt(T("leechcore.dll intégrée : %s", "Built-in leechcore.dll: %s"), FileVersion(builtin).c_str()).c_str(), false);
    if (changed) {
        SaveSettings();
        if (d.connected) { StartJob([] { DevDisconnect(); }); Toast(T("Réglages modifiés · reconnexion au prochain test", "Settings changed · reconnects on next test")); }
    }
    if (Switch(T("Libérer la carte après chaque opération", "Release the card after each operation"), &g_set.release)) SaveSettings();
    Help(T("La carte n'est ouverte que pendant une connexion, un test ou un diagnostic. Les autres logiciels DMA peuvent l'utiliser le reste du temps.",
           "The card is only opened during a connection, test or diagnostic. Other DMA software can use it the rest of the time."));
    ImGui::Separator();
    ImVec4 dc = cs == CS_RELEASED ? P.green : cs == CS_CONNECTED ? (d.simulated ? P.orange : P.green) : cs == CS_CONNECTING ? P.orange : cs == CS_FAILED ? P.red : P.text3;
    Dot(dc);
    ImGui::SameLine();
    const char* st = cs == CS_CONNECTED ? T("Connecté", "Connected") : cs == CS_RELEASED ? T("Prête · carte libérée", "Ready · card released") : cs == CS_CONNECTING ? T("Connexion…", "Connecting…") : cs == CS_FAILED ? T("Échec", "Failed") : T("Déconnecté", "Disconnected");
    ImGui::Text("%s%s%s", st, cs == CS_CONNECTED ? " · " : "", cs == CS_CONNECTED ? d.device.c_str() : "");
    if (cs == CS_FAILED) Small(P.red, DevError().c());
    if (d.connected) {
        if (Button(T("Déconnecter", "Disconnect"), B_NEUTRAL, ImVec2(0, 0), busy)) StartJob([] { DevDisconnect(); });
    } else if (Button(T("Connecter", "Connect"), B_PRIMARY, ImVec2(0, 0), busy)) ConnectAsync(false);
    EndCard();
    ImGui::Dummy(ImVec2(0, 4 * S));

    BeginCard("##algo", lw);
    Section(T("Algorithme FPGA", "FPGA algorithm"));
    const char* algos[5] = { T("Auto (défaut)", "Auto (default)"), "Async Normal", "Async Tiny", "Old Normal", "Old Tiny" };
    ImGui::SetNextItemWidth(Avail());
    if (ImGui::Combo("##algo", &g_set.algo, algos, 5)) {
        SaveSettings();
        if (d.connected && !d.simulated) { StartJob([] { DevDisconnect(); }); Toast(T("Réglages modifiés · reconnexion au prochain test", "Settings changed · reconnects on next test")); }
    }
    static const char* descFr[5] = { "Laisse LeechCore choisir. À utiliser par défaut ; ne changez que si la carte est instable.",
        "Envoie plusieurs requêtes à la fois avec de grands paquets PCIe (TLP). Le plus rapide si la carte et la carte mère le supportent.",
        "Requêtes asynchrones avec de petits paquets de 128 octets. À essayer si des lectures échouent ou si la carte se déconnecte.",
        "Ancien mode synchrone : une requête à la fois. Plus lent, mais compatible avec d'anciens firmwares.",
        "Le mode le plus prudent : synchrone et petits paquets. Le plus lent, mais fonctionne quand tout le reste échoue." };
    static const char* descEn[5] = { "Lets LeechCore decide. Use by default; only change it if the card is unstable.",
        "Sends several requests at once with large PCIe packets (TLPs). Fastest if the card and motherboard support it.",
        "Asynchronous requests with small 128-byte packets. Try it if reads fail or the card disconnects.",
        "Legacy synchronous mode: one request at a time. Slower, but compatible with older firmware.",
        "The safest mode: synchronous with small packets. Slowest, but works when everything else fails." };
    Wrapped(P.text2, g_en ? descEn[g_set.algo] : descFr[g_set.algo]);
    Small(P.text3, T("S'applique uniquement au mode FPGA.", "Only applies to FPGA mode."), false);
    AlgoState as = AlgoCopy();
    if (as.running) {
        ImGui::Text("%s  %d / 4 · %s", T("Comparaison…", "Comparing…"), as.current, algos[as.current]);
        Progress((as.current - 1) / 4.f, Avail(), P.accent);
        if (Button(T("Arrêter", "Stop"), B_DANGER)) BenchStop();
    } else if (Button(T("Comparer les 4 algorithmes (~40 s)", "Compare the 4 algorithms (~40 s)"), B_NEUTRAL, ImVec2(0, 0), busy || g_set.mode != MODE_FPGA)) AlgoStart();
    if (as.done && ImGui::BeginTable("##al", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        const char* hd[5] = { T("Algorithme", "Algorithm"), T("Latence", "Latency"), T("Séq. 8 Mo", "Seq. 8 MB"), "Scatter 256", T("Succès", "Success") };
        for (int c = 0; c < 5; c++) { ImGui::TableSetColumnIndex(c); ImGui::TextColored(P.text2, "%s", hd[c]); }
        for (int a = 1; a <= 4; a++) {
            const AlgoRes& r = as.r[a];
            ImVec4 c = a == as.best ? P.green : P.text;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextColored(c, "%s", algos[a]);
            if (!r.ran) { ImGui::TableNextColumn(); ImGui::TextColored(P.red, "%s", T("échec", "failed")); continue; }
            ImGui::TableNextColumn(); ImGui::TextColored(c, "%s µs", Num(r.lat, 1).c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(c, "%s", Num(r.mbps, 1).c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(c, "%s", Num(r.scMbps, 1).c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(r.success < 99.5 ? P.red : c, "%s %%", Num(r.success, 2).c_str());
        }
        ImGui::EndTable();
        if (as.best != g_set.algo && Button(Fmt(T("Utiliser %s", "Use %s"), algos[as.best]).c_str(), B_PRIMARY, ImVec2(0, 0), busy)) {
            g_set.algo = as.best;
            SaveSettings();
            StartJob([] { DevDisconnect(); });
        }
    }
    EndCard();
    ImGui::Dummy(ImVec2(0, 4 * S));

    BeginCard("##log", lw);
    Section(T("Journalisation", "Logging"));
    const char* lv[3] = { T("Aucun", "None"), T("Verbeux", "Verbose"), T("Très verbeux", "Very verbose") };
    ImGui::TextColored(P.text2, "%s", T("Niveau", "Level"));
    Help(T("Quantité de messages LeechCore affichés dans l'onglet Journal. « Très verbeux » aide à comprendre un échec de connexion.",
           "Amount of LeechCore messages shown in the Log tab. \"Very verbose\" helps understand a connection failure."));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(Avail());
    if (ImGui::Combo("##lv", &g_set.journal, lv, 3)) SaveSettings();
    Small(P.text3, T("Messages LeechCore affichés dans l'onglet Journal.", "LeechCore messages are shown in the Log tab."), false);
    EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    BeginCard("##info");
    Section(T("Informations", "Information"));
    if (!d.connected && !d.released) {
        ImGui::TextColored(P.text3, "%s", T("Aucune carte ouverte", "No card opened"));
    } else {
        KV(T("Modèle FPGA", "FPGA model"), d.fpga.c());
        KV("Firmware", d.firmware.c());
        KV(T("ID PCIe", "PCIe ID"), d.pcie.c());
        KV(T("Mémoire max", "Max memory"), d.maxMem.c());
        KV(T("Lien USB", "USB link"), d.usbLink.c(), T("Vitesse du lien USB entre la carte et ce PC. Une carte FT601 doit être en USB 3.0 (5 Gb/s) ; en USB 2.0 le débit plafonne vers 30 à 40 Mo/s.",
                                                      "Speed of the USB link between the card and this PC. An FT601 card should run at USB 3.0 (5 Gb/s); at USB 2.0 throughput caps around 30 to 40 MB/s."));
        KV(T("RAM du PC cible", "Target PC RAM"), RamText(d).c(), T("RAM du PC cible estimée par sondage en lecture seule à la connexion. Sert à la zone mémoire automatique.",
                                                                  "Target PC RAM, estimated by a read-only probe on connection. Used for the automatic memory range."));
    }
    EndCard();
    ImGui::Dummy(ImVec2(0, 4 * S));
    BeginCard("##tip");
    Section(T("Aide", "Help"));
    Wrapped(P.text2, T("Camra's Tool contient déjà leechcore.dll, FTD3XX.dll et vcruntime140.dll : rien à installer. Branchez la carte sur un port USB 3 du PC de test.",
                       "Camra's Tool already contains leechcore.dll, FTD3XX.dll and vcruntime140.dll: nothing to install. Plug the card into a USB 3 port of the test PC."));
    if (Button(T("Assistant de branchement", "Connection assistant"), B_PRIMARY)) g_openGuide = true;
    if (Button(T("Comparer des câbles et des ports", "Compare cables and ports"))) g_openCable = true;
    if (Button(T("Signaler un problème", "Report a problem"))) ReportProblem();
    Help(T("Crée un zip avec le journal et le diagnostic, sans votre nom d'utilisateur. Rien n'est envoyé automatiquement.", "Creates a zip with the log and diagnostics, without your user name. Nothing is sent automatically."));
    EndCard();
    ImGui::EndGroup();
}

// ===========================================================================
//  Firmware (CH347 + OpenOCD)
// ===========================================================================
void PageFirmware() {
    FwState f = FwCopy();
    Ch347 ch = Ch347Cached();
    float gap = 10 * S, lw = (Avail() - gap) * 0.52f;
    ImGui::BeginGroup();
    BeginCard("##ch", lw);
    Section(T("Appareil CH347", "CH347 device"));
    Help(T("Le CH347 est la puce USB de programmation (JTAG) de la carte DMA. Son branchement est détecté en direct. « Infos appareil » lit l'IDCODE du FPGA pour identifier son modèle.",
           "The CH347 is the DMA card's USB programming (JTAG) chip. Plugging it in is detected live. \"Device info\" reads the FPGA IDCODE to identify its model."));
    Dot(ch.present ? (ch.problem ? P.orange : P.green) : P.text3);
    ImGui::SameLine();
    ImGui::TextUnformatted(ch.present ? (ch.problem ? T("CH347 détecté, pilote en erreur", "CH347 detected, driver error") : T("CH347 connecté", "CH347 connected"))
                                      : T("Aucun appareil CH347 connecté", "No CH347 device connected"));
    if (ch.present && ch.problem) Small(P.orange, Fmt(T("%s · code %lu : installez le pilote WCH CH341PAR (Diagnostic).", "%s · code %lu: install the WCH CH341PAR driver (Diagnostics)."), ch.name.c_str(), ch.problem).c_str());
    else if (ch.present) Small(P.text2, ch.name.c_str(), false);
    else Small(P.text3, T("Branchez le câble USB du port de flash (JTAG) de la carte.", "Plug the USB cable into the card's flashing (JTAG) port."));
    if (f.idcode) {
        const char* m = nullptr;
        for (int i = 0; i < FPGA_MODEL_COUNT; i++) if ((FPGA_MODELS[i].idcode & 0x0FFFFFFF) == (f.idcode & 0x0FFFFFFF)) m = FPGA_MODELS[i].name;
        ImGui::Text(T("FPGA : %s · IDCODE 0x%08X", "FPGA: %s · IDCODE 0x%08X"), m ? m : T("modèle inconnu", "unknown model"), f.idcode);
    } else ImGui::TextColored(P.text3, "%s", T("FPGA : non détecté", "FPGA: not detected"));
    if (Button(T("Infos appareil", "Device info"), B_NEUTRAL, ImVec2(0, 0), f.running)) FwStart(FW_DETECT);
    EndCard();
    ImGui::Dummy(ImVec2(0, 4 * S));

    BeginCard("##prog", lw);
    Section(T("Programmation", "Programming"));
    // Modèle FPGA (liste filtrable)
    ImGui::TextColored(P.text2, "%s", T("Modèle FPGA", "FPGA model"));
    Help(T("Modèle du FPGA de votre carte DMA (ex. xc7a35t pour une carte 35T, xc7a75t, xc7a100t, xc7k325t). Il détermine le bitstream proxy utilisé pour écrire la flash. Tapez pour filtrer la liste, ou utilisez « Infos appareil » pour le détecter.",
           "Your DMA card's FPGA model (e.g. xc7a35t for a 35T card, xc7a75t, xc7a100t, xc7k325t). It selects the proxy bitstream used to write the flash. Type to filter the list, or use \"Device info\" to detect it."));
    ImGui::SetNextItemWidth(Avail());
    if (ImGui::BeginCombo("##model", g_set.fwModel, ImGuiComboFlags_HeightLarge)) {
        static char filter[32] = "";
        if (ImGui::IsWindowAppearing()) { filter[0] = 0; ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##flt", "xc7a…", filter, sizeof filter);
        for (int i = 0; i < FPGA_MODEL_COUNT; i++) {
            const char* n = FPGA_MODELS[i].name;
            if (filter[0] && !strstr(n, Lower(filter).c_str())) continue;
            if (ImGui::Selectable(n, _stricmp(n, g_set.fwModel) == 0)) { strncpy_s(g_set.fwModel, n, _TRUNCATE); SaveSettings(); }
        }
        ImGui::EndCombo();
    }
    // Type de fichier
    ImGui::TextColored(P.text2, "%s", T("Type de fichier", "File type"));
    Help(T("BIT : charge le fichier dans la RAM du FPGA. C'est temporaire, perdu à la coupure de courant : idéal pour tester un firmware. BIN : écrit le firmware dans la flash SPI de la carte. Il est conservé et le FPGA redémarre dessus automatiquement.",
           "BIT: loads the file into the FPGA's RAM. It is temporary and lost on power off: ideal to test a firmware. BIN: writes the firmware to the card's SPI flash. It is kept and the FPGA restarts on it automatically."));
    if (Segmented("type", &g_set.fwType, { "BIT · RAM", "BIN · Flash" })) SaveSettings();
    // Horloge
    ImGui::TextColored(P.text2, "%s", T("Horloge JTAG (TCK)", "JTAG clock (TCK)"));
    Help(T("Fréquence de l'horloge JTAG (TCK) du CH347. 10 MHz est le plus fiable ; 15 ou 30 MHz vont plus vite. Vous pouvez saisir une autre valeur en Hz.",
           "CH347 JTAG clock frequency (TCK). 10 MHz is the most reliable; 15 or 30 MHz are faster. You can type another value in Hz."));
    ImGui::SetNextItemWidth(Avail() * 0.5f);
    if (ImGui::BeginCombo("##clk", g_set.fwClock)) {
        for (const char* c : { "10000000", "15000000", "30000000" })
            if (ImGui::Selectable(c, strcmp(c, g_set.fwClock) == 0)) { strncpy_s(g_set.fwClock, c, _TRUNCATE); SaveSettings(); }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(Avail() - 34 * S);
    ImGui::InputText("Hz##clkv", g_set.fwClock, sizeof g_set.fwClock, ImGuiInputTextFlags_CharsDecimal);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveSettings();
    // Fichier
    ImGui::TextColored(P.text2, "%s", T("Fichier", "File"));
    Help(T("Firmware fourni par le vendeur de la carte : .bit pour le mode BIT (RAM), .bin pour le mode BIN (flash). Le dernier dossier utilisé est mémorisé.",
           "Firmware supplied by the card vendor: .bit for BIT mode (RAM), .bin for BIN mode (flash). The last folder used is remembered."));
    std::string fn = g_set.fwFile.empty() ? T("Aucun fichier choisi", "No file selected") : FileName(g_set.fwFile);
    ImGui::SetNextItemWidth(Avail() - 44 * S);
    char fbuf[MAX_PATH * 2];
    strncpy_s(fbuf, fn.c_str(), _TRUNCATE);
    ImGui::InputText("##file", fbuf, sizeof fbuf, ImGuiInputTextFlags_ReadOnly);
    if (!g_set.fwFile.empty()) Help(W2U(g_set.fwFile).c_str());
    ImGui::SameLine(0, 6 * S);
    if (Button("…##f", B_NEUTRAL, ImVec2(38 * S, 0))) {
        std::wstring p = g_set.fwFile;
        const wchar_t* flt = g_set.fwType == 0 ? L"Bitstream (*.bit)\0*.bit\0Firmware (*.bin)\0*.bin\0" : L"Firmware (*.bin)\0*.bin\0Bitstream (*.bit)\0*.bit\0";
        if (OpenFileDlg(p, flt, T("Choisir le fichier de firmware", "Choose the firmware file"))) { g_set.fwFile = p; SaveSettings(); }
    }
    ImGui::Dummy(ImVec2(0, 4 * S));
    // Actions
    if (!g_set.fwFile.empty()) {
        static std::wstring idFor;
        static uint32_t fid = 0;
        if (idFor != g_set.fwFile) { idFor = g_set.fwFile; fid = FirmwareIdcode(idFor); }
        const FpgaModel* m = fid ? ModelById(fid) : nullptr;
        if (m) Small(_stricmp(m->name, g_set.fwModel) == 0 ? P.green : P.red, Fmt(T("Fichier prévu pour : %s", "File made for: %s"), m->name).c_str(), false);
        else Small(P.orange, T("Modèle visé par ce fichier inconnu : vérifiez qu'il est bien fait pour votre carte.", "Target model of this file unknown: make sure it is made for your card."));
    }
    bool bit = g_set.fwType == 0;
    if (!bit) {
        if (Switch(T("Sauvegarder la flash avant d'écrire ou d'effacer", "Back up the flash before writing or erasing"), &g_set.fwBackup)) SaveSettings();
        Help(T("Lit le firmware actuel de la carte dans un fichier avant d'écrire le nouveau. Pour revenir en arrière, flashez ce fichier. Si la sauvegarde échoue, rien n'est écrit.",
               "Reads the card's current firmware to a file before writing the new one. To go back, flash that file. If the backup fails, nothing is written."));
    }
    const char* main = bit ? T("Charger en RAM (BIT)", "Load into RAM (BIT)") : T("Flasher le firmware (BIN → flash)", "Flash firmware (BIN → flash)");
    if (Button(main, B_PRIMARY, ImVec2(0, 36 * S), f.running)) {
        std::string ext = Lower(W2U(g_set.fwFile));
        bool match = ext.size() > 4 && ext.substr(ext.size() - 4) == (bit ? ".bit" : ".bin");
        bool go = true;
        if (!g_set.fwFile.empty() && !match) {
            std::wstring q = U2W(Fmt(T("Le fichier « %s » ne correspond pas au type choisi (%s).\n\nContinuer quand même ?", "The file \"%s\" does not match the selected type (%s).\n\nContinue anyway?"),
                                     FileName(g_set.fwFile).c_str(), bit ? "BIT" : "BIN"));
            go = MessageBoxW(g_hwnd, q.c_str(), L"Camra's Tool", MB_YESNO | MB_ICONWARNING) == IDYES;
        }
        if (go && !bit && !g_set.fwFile.empty()) {
            std::wstring q = U2W(Fmt(T("Écrire « %s » dans la flash du FPGA %s ?\n\nL'ancien firmware sera effacé. Ne débranchez pas la carte et n'éteignez pas le PC cible pendant l'opération.",
                                       "Write \"%s\" to the flash of FPGA %s?\n\nThe old firmware will be erased. Do not unplug the card or turn off the target PC during the operation."),
                                     FileName(g_set.fwFile).c_str(), g_set.fwModel));
            go = MessageBoxW(g_hwnd, q.c_str(), L"Camra's Tool", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
        }
        if (go) FwStart(FW_PROGRAM);
    }
    if (!bit) {
        ImGui::SameLine();
        if (Button(T("Vérifier la flash", "Verify flash"), B_NEUTRAL, ImVec2(0, 36 * S), f.running)) {
            std::wstring q = U2W(Fmt(T("Comparer « %s » au contenu de la flash ?\n\nRien n'est écrit. Le FPGA redémarre à la fin : la carte DMA est indisponible quelques secondes.",
                                       "Compare \"%s\" with the flash contents?\n\nNothing is written. The FPGA restarts at the end: the DMA card is unavailable for a few seconds."),
                                     g_set.fwFile.empty() ? "?" : FileName(g_set.fwFile).c_str()));
            if (MessageBoxW(g_hwnd, q.c_str(), L"Camra's Tool", MB_YESNO | MB_ICONQUESTION) == IDYES) FwStart(FW_VERIFY);
        }
        Help(T("Compare le fichier .bin choisi au contenu de la flash, sans rien écrire. Indique si ce firmware est bien celui installé sur la carte.",
               "Compares the chosen .bin file with the flash contents, without writing anything. Tells whether this firmware is the one installed on the card."));
        ImGui::SameLine();
        if (Button(T("Sauvegarder", "Back up"), B_NEUTRAL, ImVec2(0, 36 * S), f.running)) FwStart(FW_BACKUP);
        Help(T("Lit toute la flash de la carte dans un fichier .bin, sans rien écrire. Le FPGA redémarre à la fin.", "Reads the card's whole flash to a .bin file, without writing anything. The FPGA restarts at the end."));
        ImGui::SameLine();
        if (Button(T("Effacer la flash", "Erase flash"), B_DANGER, ImVec2(0, 36 * S), f.running)) {
            std::wstring q1 = U2W(Fmt(T("Effacer entièrement la flash du FPGA %s ?\n\nLa carte n'aura plus de firmware et ne fonctionnera plus jusqu'au prochain flashage.",
                                        "Erase the whole flash of the %s FPGA?\n\nThe card will have no firmware and will not work until it is flashed again."), g_set.fwModel));
            std::wstring q2 = U2W(T("Dernière confirmation : la flash va être effacée. Cette opération est irréversible.\n\nVoulez-vous vraiment continuer ?",
                                    "Last confirmation: the flash is about to be erased. This cannot be undone.\n\nDo you really want to continue?"));
            if (MessageBoxW(g_hwnd, q1.c_str(), L"Camra's Tool", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES &&
                MessageBoxW(g_hwnd, q2.c_str(), L"Camra's Tool", MB_YESNO | MB_ICONSTOP | MB_DEFBUTTON2) == IDYES)
                FwStart(FW_ERASE);
        }
        Help(T("Efface entièrement la flash SPI. La carte n'aura plus de firmware et ne fonctionnera plus jusqu'au prochain flashage. Rarement utile : à réserver à un flash corrompu qui refuse d'être réécrit.",
               "Erases the whole SPI flash. The card will have no firmware and will not work until it is flashed again. Rarely useful: keep it for a corrupted flash that refuses to be rewritten."));
    }
    ImGui::Dummy(ImVec2(0, 4 * S));
    if (f.running) {
        int e = (int)(NowSec() - f.t0);
        ImGui::Text("%s  ·  %d:%02d", f.status.c(), e / 60, e % 60);
        Progress(-1, Avail(), P.accent);
    } else if (f.hasResult) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        StatusIcon(ImGui::GetWindowDrawList(), f.ok ? 0 : 1, ImVec2(p.x + 9 * S, p.y + ImGui::GetTextLineHeight() * 0.5f), 9 * S);
        ImGui::Indent(26 * S);
        Wrapped(f.ok ? P.text : P.red, f.result.c());
        ImGui::Unindent(26 * S);
    } else ImGui::TextColored(P.text3, "%s", KitReady() ? T("Prêt", "Ready") : T("Préparation…", "Preparing…"));
    EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    BeginCard("##console", 0, std::max(320.f * S, ImGui::GetContentRegionAvail().y - 4 * S));
    Section(T("Console OpenOCD", "OpenOCD console"));
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(T("Effacer", "Clear")).x - 24 * S);
    if (Button(T("Effacer", "Clear"), B_GHOST, ImVec2(0, 0))) FwClearConsole();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Dark ? Rgb(0, 0, 0, 0.25f) : Rgb(0, 0, 0, 0.04f));
    ImGui::BeginChild("##out", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing() * 3.2f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    PushFontSz(FMono, Base - 2);
    if (f.console.empty()) ImGui::TextColored(P.text3, "%s", T("La sortie d'OpenOCD s'affichera ici.", "OpenOCD output will appear here."));
    for (auto& l : f.console) {
        std::string low = Lower(l);
        ImVec4 c = l.rfind("> ", 0) == 0 ? P.accent : low.rfind("error", 0) == 0 || l.find("CT_FAIL") != std::string::npos ? P.red
                 : low.rfind("warn", 0) == 0 ? P.orange : P.text2;
        ImGui::TextColored(c, "%s", l.c_str());
    }
    static size_t lastN = 0;
    if (f.console.size() != lastN) { ImGui::SetScrollHereY(1.0f); lastN = f.console.size(); }
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    Small(P.text3, T("Reprend toutes les options de CH347FPGADownloader v1.1 (WCH) : modèle FPGA, BIT/BIN, horloge TCK, fichier, téléchargement, infos appareil et console. Basé sur OpenOCD (CH347) et les bitstreams proxy bscan_spi, intégrés à Camra's Tool.",
                     "Includes every option of CH347FPGADownloader v1.1 (WCH): FPGA model, BIT/BIN, TCK clock, file, download, device info and console. Based on OpenOCD (CH347) and the bscan_spi proxy bitstreams, built into Camra's Tool."));
    EndCard();
    ImGui::EndGroup();
}

// ===========================================================================
//  Diagnostic
// ===========================================================================
void PageDiag() {
    DiagResult d = DiagCopy();
    if (!d.done && !d.running) {
        EmptyState(T("Aucun diagnostic pour l'instant", "No diagnostics yet"),
                   T("Vérifie toute la chaîne : fichiers LeechCore, pilote FTDI, carte USB, connexion et lecture mémoire.", "Checks the whole chain: LeechCore files, FTDI driver, USB card, connection and memory reads."),
                   T("Lancer le diagnostic", "Run diagnostics"), -1);
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    BeginCard("##head");
    dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float r = 18 * S;
    if (d.running) Spinner(ImVec2(p.x + r, p.y + r + 2 * S), r * 0.8f, P.accent);
    else StatusIcon(dl, d.errors ? 1 : d.warns ? 2 : 0, ImVec2(p.x + r, p.y + r + 2 * S), r);
    ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + 14 * S, p.y));
    ImGui::BeginGroup();
    Title(d.running ? T("Diagnostic en cours…", "Running diagnostics…") : d.headline.c(), 20);
    if (d.running) Small(P.text2, T("Vérification des fichiers LeechCore, du pilote, de la carte USB, puis test de connexion et de lecture.",
                                    "Checking LeechCore files, drivers and the USB card, then testing connection and reads."));
    else Small(P.text2, d.summary.c(), false);
    ImGui::EndGroup();
    EndCard();
    ImGui::Dummy(ImVec2(0, 6 * S));

    float gap = 12 * S, lw = (Avail() - gap) * 0.5f;
    ImGui::BeginGroup();
    Section(T("Vérifications", "Checks"));
    BeginCard("##checks", lw);
    dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < d.checks.size(); i++) {
        const Check& c = d.checks[i];
        if (i) { ImGui::Dummy(ImVec2(0, 2 * S)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, 2 * S)); }
        ImVec2 q = ImGui::GetCursorScreenPos();
        int icon = c.lvl == CK_OK ? 0 : c.lvl == CK_ERR ? 1 : c.lvl == CK_WARN ? 2 : 3;
        StatusIcon(dl, icon, ImVec2(q.x + 10 * S, q.y + ImGui::GetTextLineHeight() * 0.5f + 1 * S), 10 * S);
        ImGui::Indent(30 * S);
        PushFontSz(FSemi, Base);
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(c.lvl == CK_ERR ? P.red : P.text, "%s", c.title.c());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        if (!c.detail.fr.empty()) Small(P.text2, c.detail.c());
        ImGui::Unindent(30 * S);
    }
    if (d.running) { ImGui::Dummy(ImVec2(0, 4 * S)); Progress(-1, Avail(), P.accent); }
    EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    if (!d.fixes.empty()) {
        Section(T("Solutions à tester, dans l'ordre", "Fixes to try, in order"));
        BeginCard("##fixes");
        dl = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < d.fixes.size(); i++) {
            const Fix& fx = d.fixes[i];
            ImGui::PushID((int)i);
            if (i) { ImGui::Dummy(ImVec2(0, 2 * S)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, 2 * S)); }
            ImVec2 q = ImGui::GetCursorScreenPos();
            ImVec2 c = ImVec2(q.x + 10 * S, q.y + ImGui::GetTextLineHeight() * 0.5f + 1 * S);
            dl->AddCircleFilled(c, 10 * S, U(WithA(P.accent, 0.18f)), 24);
            char num[8];
            snprintf(num, sizeof num, "%d", (int)i + 1);
            PushFontSz(FSemi, Base - 2);
            ImVec2 ns = ImGui::CalcTextSize(num);
            dl->AddText(ImVec2(c.x - ns.x * 0.5f, c.y - ns.y * 0.5f), U(P.accent), num);
            ImGui::PopFont();
            ImGui::Indent(30 * S);
            Wrapped(P.text, fx.text.c());
            if (fx.action != ACT_NONE && !fx.button.fr.empty()) {
                bool inst = (fx.action == ACT_INSTALL_FTDI || fx.action == ACT_INSTALL_CH341) && DriverInstalling();
                if (Button(inst ? T("Installation…", "Installing…") : fx.button.c(), B_NEUTRAL, ImVec2(0, 0), inst || (d.running && fx.action != ACT_URL && fx.action != ACT_DEVMGMT))) DiagAction(fx);
            }
            ImGui::Unindent(30 * S);
            ImGui::PopID();
        }
        EndCard();
    } else if (d.done) {
        Section(T("Conseil", "Advice"));
        BeginCard("##advice");
        Wrapped(P.text2, T("Rien à corriger : lancez un test complet depuis l'onglet Speedtest.", "Nothing to fix: run a full test from the Speedtest tab."));
        if (Button(T("Aller au Speedtest", "Go to Speedtest"), B_PRIMARY)) g_page = PG_SPEED;
        EndCard();
    }
    if (d.done && d.errors) {
        ImGui::Dummy(ImVec2(0, 6 * S));
        if (Button(T("Assistant de branchement", "Connection assistant"))) g_openGuide = true;
        ImGui::SameLine();
        if (Button(T("Signaler un problème", "Report a problem"))) ReportProblem();
    }
    if (!d.lc.empty()) {
        ImGui::Dummy(ImVec2(0, 6 * S));
        Section(T("Messages LeechCore", "LeechCore messages"));
        BeginCard("##lc");
        PushFontSz(FMono, Base - 3);
        for (auto& m : d.lc) Wrapped(P.text2, m.c_str());
        ImGui::PopFont();
        EndCard();
    }
    ImGui::EndGroup();
}

// ===========================================================================
//  Journal
// ===========================================================================
void PageLog() {
    std::vector<LogLine> lines = LogCopy();
    BeginCard("##logc", 0, std::max(200.f * S, ImGui::GetContentRegionAvail().y - 4 * S));
    if (lines.empty()) {
        ImGui::TextColored(P.text3, "%s", T("Le journal est vide. Les connexions, tests et messages LeechCore apparaîtront ici.", "The log is empty. Connections, tests and LeechCore messages will appear here."));
    } else {
        ImGui::BeginChild("##logscroll", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        PushFontSz(FMono, Base - 2);
        ImGuiListClipper clip;
        clip.Begin((int)lines.size());
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                const LogLine& l = lines[(size_t)i];
                ImVec4 c = l.lvl == L_ERR ? P.red : l.lvl == L_WARN ? P.orange : l.lvl == L_OK ? P.green : l.lvl == L_LC ? P.text3 : P.text;
                ImGui::TextColored(P.text3, "%s", l.time.c_str());
                ImGui::SameLine();
                ImGui::TextColored(c, "%s", l.text.c_str());
            }
        }
        static size_t lastN = 0;
        if (lines.size() != lastN) { ImGui::SetScrollHereY(1.0f); lastN = lines.size(); }
        ImGui::PopFont();
        ImGui::EndChild();
    }
    EndCard();
}

// ===========================================================================
//  Réglages
// ===========================================================================
void PageSettings() {
    DeviceInfo d = DevInfo();
    float gap = 10 * S, lw = (Avail() - gap) * 0.5f;
    ImGui::BeginGroup();
    BeginCard("##zone", lw);
    Section(T("Zone mémoire testée", "Tested memory range"));
    if (Switch(T("Zone automatique", "Automatic range"), &g_set.zoneAuto)) SaveSettings();
    Help(T("À la connexion, Camra's Tool sonde la RAM du PC cible (lecture seule, en évitant la zone réservée aux périphériques entre 2 et 4 Go) et teste toute la RAM trouvée. Désactivez pour choisir l'adresse et la taille vous-même.",
           "On connection, Camra's Tool probes the target PC's RAM (read-only, avoiding the device area between 2 and 4 GB) and tests all the RAM found. Turn off to choose the address and size yourself."));
    if (g_set.zoneAuto) {
        if (d.ramProbed) {
            std::vector<Range> r = TestRanges(nullptr);
            u64 tot = 0;
            for (auto& x : r) tot += x.size;
            Small(P.text2, Fmt(T("Zone détectée : 0x%llX + %s Mo (RAM du PC cible %s). Lecture seule.", "Detected range: 0x%llX + %s MB (target PC RAM %s). Read-only."),
                               (unsigned long long)(r.empty() ? 0 : r[0].base), Num((double)(tot >> 20), 0).c_str(), RamText(d).c()).c_str());
        } else
            Small(P.text2, T("La RAM du PC cible est sondée à la connexion ; en attendant, la zone ci-dessous sert de repli. Lecture seule.",
                             "The target PC's RAM is probed on connection; until then, the range below is used as a fallback. Read-only."));
    }
    ImGui::TextColored(P.text2, "%s", T("Adresse de base", "Base address"));
    Help(T("Adresse physique où commencent les lectures dans la RAM du PC cible. 0x10000000 (256 Mo) évite les zones réservées du début de la mémoire.",
           "Physical address where reads start in the target PC's RAM. 0x10000000 (256 MB) avoids the reserved areas at the start of memory."));
    ImGui::SetNextItemWidth(Avail());
    ImGui::InputText("##base", g_set.base, sizeof g_set.base);
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveSettings();
    u64 bv;
    if (!ParseHex(g_set.base, &bv)) Small(P.red, T("Adresse de base invalide (ex. 0x10000000).", "Invalid base address (e.g. 0x10000000)."), false);
    ImGui::TextColored(P.text2, "%s", T("Taille (Mo)", "Size (MB)"));
    Help(T("Taille de la zone lue pendant les tests. Elle doit rester dans la RAM installée du PC cible ; 1024 Mo convient à presque tous les PC.",
           "Size of the range read during tests. It must fit in the target PC's installed RAM; 1024 MB suits almost every PC."));
    ImGui::SetNextItemWidth(Avail());
    if (ImGui::InputInt("##size", &g_set.sizeMb, 256, 1024)) { g_set.sizeMb = std::clamp(g_set.sizeMb, 16, 1 << 20); SaveSettings(); }
    Small(P.text3, T("Lecture seule. Restez dans la RAM du PC cible : par défaut 256 Mo → 1,25 Go, valable sur toute machine de 2 Go et plus.",
                     "Read-only. Stay within the target PC's RAM: default 256 MB → 1.25 GB, valid on any machine with 2 GB or more."));
    EndCard();
    ImGui::Dummy(ImVec2(0, 4 * S));

    BeginCard("##dur", lw);
    Section(T("Durée", "Duration"));
    ImGui::TextColored(P.text2, "%s", T("Durée de chaque test", "Duration of each test"));
    Help(T("Temps passé sur chaque test. Plus long = mesure plus stable : 3 s suffisent en général, 10 s pour un test de stabilité.",
           "Time spent on each test. Longer = steadier measurement: 3 s is usually enough, 10 s for a stability test."));
    ImGui::SetNextItemWidth(Avail());
    if (ImGui::SliderInt("##d", &g_set.duration, 1, 30, "%d s")) SaveSettings();
    ImGui::TextColored(P.text2, "%s", T("Passages par test", "Runs per test"));
    Help(T("3 passages : le test complet est répété 3 fois et la médiane est gardée. Plus long, mais chiffres plus réguliers.", "3 runs: the full test is repeated 3 times and the median is kept. Longer, but steadier figures."));
    int rn = g_set.runs == 3 ? 1 : 0;
    if (Segmented("runs", &rn, { T("1 passage", "1 run"), T("3 (médiane)", "3 (median)") })) { g_set.runs = rn ? 3 : 1; SaveSettings(); }
    if (Switch(T("Son à la fin d'un test", "Sound when a test ends"), &g_set.notify)) SaveSettings();
    Help(T("Un son et l'icône qui clignote si la fenêtre n'est pas au premier plan.", "A sound and a flashing icon if the window is not in front."));
    ImGui::TextColored(P.text2, "%s", T("Durée du test de stabilité", "Stability test duration"));
    Help(T("Durée du test de stabilité. 5 à 10 minutes suffisent pour un câble ou un port douteux ; 30 à 60 minutes révèlent une chauffe de la carte ou des coupures rares.",
           "Stability test duration. 5 to 10 minutes are enough for a doubtful cable or port; 30 to 60 minutes reveal card overheating or rare dropouts."));
    static const int mins[4] = { 5, 10, 30, 60 };
    int si = 1;
    for (int i = 0; i < 4; i++) if (g_set.stabMin == mins[i]) si = i;
    if (Segmented("stab", &si, { "5 min", "10 min", "30 min", "60 min" })) { g_set.stabMin = mins[si]; SaveSettings(); }
    EndCard();
    ImGui::Dummy(ImVec2(0, 4 * S));

    BeginCard("##look", lw);
    Section(T("Apparence", "Appearance"));
    ImGui::TextColored(P.text2, "%s", T("Thème", "Theme"));
    Help(T("Apparence claire ou sombre de l'application.", "Light or dark appearance of the app."));
    int th = g_set.dark ? 1 : 0;
    if (Segmented("theme", &th, { T("Clair", "Light"), T("Sombre", "Dark") })) { g_set.dark = th == 1; SaveSettings(); }
    ImGui::TextColored(P.text2, "%s", T("Langue", "Language"));
    Help(T("Langue de l'interface : français ou anglais.", "Interface language: French or English."));
    int lg = g_set.english ? 1 : 0;
    if (Segmented("lang", &lg, { "Français", "English" })) { g_set.english = lg == 1; g_en = g_set.english; SaveSettings(); }
    EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    BeginCard("##custom");
    Section(T("Tests personnalisés", "Custom tests"));
    Small(P.text2, T("Utilisés lorsque le mode « Personnalisé » est choisi dans Speedtest.", "Used when the \"Custom\" mode is selected in Speedtest."));
    for (int i = 0; i < 8; i++) {
        ImGui::PushID(i);
        if (ImGui::Checkbox(g_en ? TESTS[i].nameEn : TESTS[i].nameFr, &g_set.tests[i])) SaveSettings();
        Help(g_en ? TESTS[i].helpEn : TESTS[i].helpFr);
        ImGui::SameLine();
        ImGui::TextColored(P.text3, "%s", g_en ? TESTS[i].descEn : TESTS[i].descFr);
        ImGui::PopID();
    }
    if (Button(T("Tout activer", "Enable all"))) { for (bool& t : g_set.tests) t = true; SaveSettings(); }
    ImGui::SameLine();
    if (Button(T("Tout désactiver", "Disable all"))) { for (bool& t : g_set.tests) t = false; SaveSettings(); }
    ImGui::SameLine();
    if (Button(T("Utiliser ce mode", "Use this mode"), B_PRIMARY)) { g_set.testMode = TM_CUSTOM; SaveSettings(); g_page = PG_SPEED; }
    EndCard();
    ImGui::Dummy(ImVec2(0, 4 * S));

    BeginCard("##about");
    Section(T("À propos", "About"));
    PushFontSz(FSemi, Base + 2);
    ImGui::TextUnformatted("Camra's Tool");
    ImGui::PopFont();
    ImGui::TextColored(P.text2, "%s", Fmt(T("Version %s · Benchmark DMA basé sur LeechCore", "Version %s · DMA benchmark based on LeechCore"), APP_VERSION).c_str());
    ImGui::TextColored(P.text2, "%s %s %s", T("Développé par", "Developed by"), APP_AUTHOR, T("sur Discord", "on Discord"));
    if (Switch(T("Chercher les mises à jour", "Check for updates"), &g_set.updates)) SaveSettings();
    Help(T("Au démarrage puis toutes les 2 heures : interroge la page GitHub du projet. Aucune donnée n'est envoyée.", "At startup then every 2 hours: queries the project's GitHub page. No data is sent."));
    if (Button(T("Vérifier maintenant", "Check now"), B_NEUTRAL, ImVec2(0, 0), UpdateBusy())) UpdateCheckNow();
    Small(P.text3, Fmt(T("Réglages et historique : %s", "Settings and history: %s"), W2U(DataDir()).c_str()).c_str());
    if (Button(T("Ouvrir le dossier", "Open folder"), B_GHOST)) ShellExecuteW(nullptr, L"open", DataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    Small(P.text3, T("Logiciel libre sous GNU GPL version 3 · github.com/camraa/dma", "Free software under GNU GPL version 3 · github.com/camraa/dma"), false);
    Small(P.text3, T("Composants tiers : LeechCore (GPL-3), OpenOCD (GPL-2), bitstreams bscan_spi, pilotes FTDI et WCH. Voir LICENCES-TIERCES.txt.",
                     "Third-party components: LeechCore (GPL-3), OpenOCD (GPL-2), bscan_spi bitstreams, FTDI and WCH drivers. See LICENCES-TIERCES.txt."));
    EndCard();
    ImGui::EndGroup();
}
