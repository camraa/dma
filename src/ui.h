// ui.h - habillage de Camra's Tool pour Dear ImGui : thème clair/sombre façon macOS,
// fenêtre sans cadre à pastilles, barre latérale, cartes, boutons, icônes d'état, courbes.
#pragma once
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <algorithm>
#include <vector>
#include <initializer_list>
#include "imgui.h"

#ifndef IM_PI
#define IM_PI 3.14159265358979323846f
#endif

namespace ui {

inline float S = 1.0f;                       // échelle DPI
inline float TitleH = 58.0f;                 // bande du haut qui sert à déplacer la fenêtre
inline float SidebarW = 0.0f;
inline bool Dark = true;
inline ImFont* FReg = nullptr;
inline ImFont* FSemi = nullptr;
inline ImFont* FMono = nullptr;
inline ImFont* FIcon = nullptr;
inline float Base = 15.0f;                   // taille du texte courant

struct Pal {
    ImVec4 bg, side, card, card2, border, field, text, text2, text3;
    ImVec4 accent, red, orange, yellow, green, teal, blue, indigo, purple, pink, gray;
};
inline Pal P;

inline ImVec4 Rgb(int r, int g, int b, float a = 1) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }
inline ImVec4 WithA(ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); }
inline ImVec4 Mix(ImVec4 a, ImVec4 b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }
inline ImU32 U(ImVec4 c) { return ImGui::GetColorU32(c); }

// ---------------------------------------------------------------------------
//  Thème
// ---------------------------------------------------------------------------
inline void ApplyTheme(bool dark, float scale) {
    Dark = dark;
    S = scale;
    if (dark) {
        P.bg = Rgb(29, 29, 31); P.side = Rgb(36, 36, 39); P.card = Rgb(43, 43, 46); P.card2 = Rgb(52, 52, 56);
        P.border = Rgb(255, 255, 255, 0.075f); P.field = Rgb(255, 255, 255, 0.07f);
        P.text = Rgb(245, 245, 247); P.text2 = Rgb(235, 235, 245, 0.62f); P.text3 = Rgb(235, 235, 245, 0.36f);
        P.accent = Rgb(10, 132, 255); P.red = Rgb(255, 69, 58); P.orange = Rgb(255, 159, 10); P.yellow = Rgb(255, 214, 10);
        P.green = Rgb(48, 209, 88); P.teal = Rgb(64, 200, 224); P.blue = Rgb(10, 132, 255); P.indigo = Rgb(94, 92, 230);
        P.purple = Rgb(191, 90, 242); P.pink = Rgb(255, 55, 95); P.gray = Rgb(142, 142, 147);
    } else {
        P.bg = Rgb(242, 242, 247); P.side = Rgb(232, 232, 237); P.card = Rgb(255, 255, 255); P.card2 = Rgb(246, 246, 248);
        P.border = Rgb(0, 0, 0, 0.08f); P.field = Rgb(0, 0, 0, 0.05f);
        P.text = Rgb(28, 28, 30); P.text2 = Rgb(60, 60, 67, 0.72f); P.text3 = Rgb(60, 60, 67, 0.42f);
        P.accent = Rgb(0, 122, 255); P.red = Rgb(255, 59, 48); P.orange = Rgb(255, 149, 0); P.yellow = Rgb(255, 204, 0);
        P.green = Rgb(40, 180, 75); P.teal = Rgb(48, 176, 199); P.blue = Rgb(0, 122, 255); P.indigo = Rgb(88, 86, 214);
        P.purple = Rgb(175, 82, 222); P.pink = Rgb(255, 45, 85); P.gray = Rgb(142, 142, 147);
    }
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(16, 14); st.FramePadding = ImVec2(12, 7); st.ItemSpacing = ImVec2(10, 8);
    st.ItemInnerSpacing = ImVec2(8, 6); st.CellPadding = ImVec2(10, 7);
    st.WindowRounding = 0; st.ChildRounding = 12; st.FrameRounding = 8; st.PopupRounding = 10;
    st.GrabRounding = 99; st.ScrollbarRounding = 99; st.TabRounding = 8;
    st.ScrollbarSize = 10; st.GrabMinSize = 14;
    st.WindowBorderSize = 0; st.ChildBorderSize = 1; st.PopupBorderSize = 1; st.FrameBorderSize = 0;
    st.SelectableTextAlign = ImVec2(0, 0.5f);
    ImVec4* c = st.Colors;
    ImVec4 ink = dark ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1);
    c[ImGuiCol_Text] = P.text;
    c[ImGuiCol_TextDisabled] = P.text3;
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = P.card;
    c[ImGuiCol_PopupBg] = dark ? Rgb(44, 44, 48, 0.98f) : Rgb(255, 255, 255, 0.98f);
    c[ImGuiCol_Border] = P.border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = P.field;
    c[ImGuiCol_FrameBgHovered] = WithA(ink, dark ? 0.11f : 0.08f);
    c[ImGuiCol_FrameBgActive] = WithA(ink, dark ? 0.15f : 0.11f);
    c[ImGuiCol_Button] = WithA(ink, dark ? 0.10f : 0.06f);
    c[ImGuiCol_ButtonHovered] = WithA(ink, dark ? 0.16f : 0.10f);
    c[ImGuiCol_ButtonActive] = WithA(ink, dark ? 0.22f : 0.15f);
    c[ImGuiCol_Header] = WithA(P.accent, 0.85f);
    c[ImGuiCol_HeaderHovered] = WithA(ink, dark ? 0.08f : 0.06f);
    c[ImGuiCol_HeaderActive] = WithA(P.accent, 0.95f);
    c[ImGuiCol_SliderGrab] = P.accent;
    c[ImGuiCol_SliderGrabActive] = P.accent;
    c[ImGuiCol_CheckMark] = P.accent;
    c[ImGuiCol_Separator] = P.border;
    c[ImGuiCol_SeparatorHovered] = WithA(P.accent, 0.6f);
    c[ImGuiCol_SeparatorActive] = P.accent;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = WithA(ink, 0.16f);
    c[ImGuiCol_ScrollbarGrabHovered] = WithA(ink, 0.26f);
    c[ImGuiCol_ScrollbarGrabActive] = WithA(ink, 0.34f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = WithA(ink, 0.025f);
    c[ImGuiCol_TableBorderLight] = P.border;
    c[ImGuiCol_TableBorderStrong] = P.border;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);
    c[ImGuiCol_TextSelectedBg] = WithA(P.accent, 0.35f);
    c[ImGuiCol_NavCursor] = P.accent;
    c[ImGuiCol_PlotHistogram] = P.accent;
    st.ScaleAllSizes(scale);
    st.FontScaleDpi = scale;
    st.FontSizeBase = Base;
}

// Polices Windows : Segoe UI, Segoe UI Semibold, Consolas, icônes Segoe Fluent / MDL2
inline void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    wchar_t wd[MAX_PATH];
    GetWindowsDirectoryW(wd, MAX_PATH);
    char d[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, wd, -1, d, sizeof d, nullptr, nullptr);
    std::string dir = std::string(d) + "\\Fonts\\";
    auto ex = [](const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; };
    auto pick = [&](std::initializer_list<const char*> names) -> std::string {
        for (auto n : names) if (ex(dir + n)) return dir + n;
        return "";
    };
    std::string reg = pick({ "segoeui.ttf" }), semi = pick({ "seguisb.ttf", "segoeuib.ttf" }), sym = pick({ "seguisym.ttf" });
    std::string mono = pick({ "consola.ttf", "cour.ttf" }), ico = pick({ "SegoeIcons.ttf", "segmdl2.ttf" });
    auto add = [&](const std::string& p, bool merge) -> ImFont* {
        if (p.empty()) return nullptr;
        ImFontConfig c;
        c.MergeMode = merge;
        return io.Fonts->AddFontFromFileTTF(p.c_str(), Base, &c);
    };
    FReg = add(reg, false);
    if (FReg) add(sym, true);
    if (!FReg) FReg = io.Fonts->AddFontDefault();
    FSemi = add(semi, false);
    if (FSemi) add(sym, true); else FSemi = FReg;
    FMono = add(mono, false);
    if (!FMono) FMono = FReg;
    FIcon = add(ico, false);
    io.FontDefault = FReg;
}

// ---------------------------------------------------------------------------
//  Texte
// ---------------------------------------------------------------------------
inline void PushFontSz(ImFont* f, float size) { ImGui::PushFont(f ? f : FReg, size); }

inline void TextCol(ImVec4 col, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}

inline void Wrapped(ImVec4 col, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

inline void Small(ImVec4 col, const char* text, bool wrap = true) {
    PushFontSz(FReg, Base - 2);
    if (wrap) Wrapped(col, text);
    else { ImGui::PushStyleColor(ImGuiCol_Text, col); ImGui::TextUnformatted(text); ImGui::PopStyleColor(); }
    ImGui::PopFont();
}

inline void Title(const char* text, float size = 22) {
    PushFontSz(FSemi, size);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

inline float Avail0() { return ImGui::GetContentRegionAvail().x; }

// Titre de section (petit, gris)
inline void Section(const char* text) {
    PushFontSz(FSemi, Base - 1);
    ImGui::PushStyleColor(ImGuiCol_Text, P.text2);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// Infobulle « ? » sur l'élément précédent
inline void Help(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

inline void HelpMark(const char* text) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, P.text3);
    ImGui::TextUnformatted("(?)");
    ImGui::PopStyleColor();
    Help(text);
}

// ---------------------------------------------------------------------------
//  Icônes
// ---------------------------------------------------------------------------
inline void Icon(ImDrawList* dl, unsigned int cp, ImVec2 center, float size, ImU32 col) {
    if (!FIcon || !FIcon->IsGlyphInFont((ImWchar)cp)) return;
    char buf[5] = {};
    if (cp < 0x80) buf[0] = (char)cp;
    else if (cp < 0x800) { buf[0] = (char)(0xC0 | (cp >> 6)); buf[1] = (char)(0x80 | (cp & 0x3F)); }
    else { buf[0] = (char)(0xE0 | (cp >> 12)); buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[2] = (char)(0x80 | (cp & 0x3F)); }
    ImVec2 ts = FIcon->CalcTextSizeA(size, FLT_MAX, 0, buf);
    dl->AddText(FIcon, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), col, buf);
}

// Rond d'état : 0 OK (coche verte), 1 erreur (croix rouge), 2 avertissement (!), 3 info (i)
inline void StatusIcon(ImDrawList* dl, int lvl, ImVec2 c, float r) {
    ImVec4 col = lvl == 0 ? P.green : lvl == 1 ? P.red : lvl == 2 ? P.orange : P.blue;
    dl->AddCircleFilled(c, r, U(col), 32);
    ImU32 w = IM_COL32(255, 255, 255, 255);
    float t = r * 0.18f;
    if (lvl == 0) {
        dl->PathLineTo(ImVec2(c.x - r * 0.45f, c.y + r * 0.02f));
        dl->PathLineTo(ImVec2(c.x - r * 0.12f, c.y + r * 0.35f));
        dl->PathLineTo(ImVec2(c.x + r * 0.48f, c.y - r * 0.33f));
        dl->PathStroke(w, 0, t * 1.1f);
    } else if (lvl == 1) {
        float k = r * 0.36f;
        dl->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), w, t * 1.1f);
        dl->AddLine(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), w, t * 1.1f);
    } else if (lvl == 2) {
        dl->AddLine(ImVec2(c.x, c.y - r * 0.48f), ImVec2(c.x, c.y + r * 0.12f), w, t * 1.2f);
        dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.42f), t * 0.75f, w, 12);
    } else {
        dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.42f), t * 0.75f, w, 12);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.12f), ImVec2(c.x, c.y + r * 0.48f), w, t * 1.2f);
    }
}

inline void Dot(ImVec4 col, float r = 4.5f) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r * S, p.y + h * 0.5f), r * S, U(col), 16);
    ImGui::Dummy(ImVec2(r * 2 * S, h));
}

inline void Spinner(ImVec2 c, float r, ImVec4 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = (float)ImGui::GetTime() * 6.0f;
    dl->PathArcTo(c, r, t, t + 4.4f, 24);
    dl->PathStroke(U(col), 0, r * 0.28f);
}

// ---------------------------------------------------------------------------
//  Boutons
// ---------------------------------------------------------------------------
enum { B_NEUTRAL, B_PRIMARY, B_DANGER, B_GHOST };
inline bool Button(const char* label, int kind = B_NEUTRAL, ImVec2 size = ImVec2(0, 0), bool disabled = false) {
    if (size.y <= 0) size.y = ImGui::GetFrameHeight() + 2 * S;
    int n = 0;
    if (kind == B_PRIMARY || kind == B_DANGER) {
        ImVec4 c = kind == B_PRIMARY ? P.accent : P.red;
        ImGui::PushStyleColor(ImGuiCol_Button, c);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Mix(c, ImVec4(1, 1, 1, 1), 0.12f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Mix(c, ImVec4(0, 0, 0, 1), 0.15f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        n = 4;
    } else if (kind == B_GHOST) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, P.accent);
        n = 2;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8 * S);
    if (disabled) ImGui::BeginDisabled();
    bool r = ImGui::Button(label, size);
    if (disabled) ImGui::EndDisabled();
    ImGui::PopStyleVar();
    if (n) ImGui::PopStyleColor(n);
    return r;
}

// Interrupteur façon macOS
inline bool Switch(const char* label, bool* v) {
    ImGui::PushID(label);
    ImGui::BeginGroup();
    float fh = ImGui::GetFrameHeight();
    float h = fh * 0.8f, w = h * 1.7f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##sw", ImVec2(w, fh));
    ImGuiID id = ImGui::GetItemID();
    const char* end = strstr(label, "##");
    size_t len = end ? (size_t)(end - label) : strlen(label);
    if (len) {
        ImGui::SameLine(0, 10 * S);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label, label + len);
        if (ImGui::IsItemClicked()) pressed = true;
    }
    if (pressed) *v = !*v;
    ImGuiStorage* stg = ImGui::GetStateStorage();
    float target = *v ? 1.f : 0.f;
    float t = stg->GetFloat(id, target);
    float k = ImGui::GetIO().DeltaTime * 14.f;
    t += (target - t) * (k > 1 ? 1 : k);
    stg->SetFloat(id, t);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = p.y + (fh - h) * 0.5f;
    ImVec4 off = Dark ? Rgb(255, 255, 255, 0.16f) : Rgb(0, 0, 0, 0.12f);
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + h), U(Mix(off, P.green, t)), h * 0.5f);
    float r = h * 0.5f - 2 * S;
    float cx = p.x + h * 0.5f + t * (w - h);
    dl->AddCircleFilled(ImVec2(cx, y + h * 0.5f + 1 * S), r, IM_COL32(0, 0, 0, 50), 32);
    dl->AddCircleFilled(ImVec2(cx, y + h * 0.5f), r, IM_COL32(255, 255, 255, 255), 32);
    ImGui::EndGroup();
    ImGui::PopID();
    return pressed;
}

// Contrôle segmenté (FR | EN, Clair | Sombre, BIT | BIN…)
inline bool Segmented(const char* id, int* v, std::initializer_list<const char*> items, float itemW = 0) {
    ImGui::PushID(id);
    bool changed = false;
    float h = ImGui::GetFrameHeight();
    int n = (int)items.size();
    float w = itemW;
    if (w <= 0) for (auto s : items) w = std::max(w, ImGui::CalcTextSize(s).x + 26 * S);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w * n + 4 * S, p.y + h + 4 * S), U(P.field), 9 * S);
    int i = 0;
    for (auto s : items) {
        ImVec2 a = ImVec2(p.x + 2 * S + w * i, p.y + 2 * S);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", ImVec2(w, h))) { if (*v != i) changed = true; *v = i; }
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (*v == i) {
            dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), U(Dark ? Rgb(99, 99, 102) : Rgb(255, 255, 255)), 7 * S);
            if (!Dark) dl->AddRect(a, ImVec2(a.x + w, a.y + h), U(P.border), 7 * S);
        } else if (hov) dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), U(WithA(P.text, 0.05f)), 7 * S);
        ImVec2 ts = ImGui::CalcTextSize(s);
        dl->AddText(ImVec2(a.x + (w - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f), U(*v == i ? P.text : P.text2), s);
        i++;
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w * n + 4 * S, h + 4 * S));
    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------------------
//  Cartes (panneaux arrondis)
// ---------------------------------------------------------------------------
inline bool BeginCard(const char* id, float width = 0, float height = 0) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, P.card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * S, 14 * S));
    ImGuiChildFlags f = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (height <= 0) f |= ImGuiChildFlags_AutoResizeY;
    bool r = ImGui::BeginChild(id, ImVec2(width, height), f, height > 0 ? 0 : ImGuiWindowFlags_NoScrollbar);
    return r;
}

inline void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
//  Barre de progression fine
// ---------------------------------------------------------------------------
inline void Progress(float frac, float width, ImVec4 col, float h = 6) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    h *= S;
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), U(P.field), h);
    if (frac < 0) {                          // indéterminée
        float t = (float)fmod(ImGui::GetTime() * 0.8, 1.0);
        float a = p.x + (width * 1.3f) * t - width * 0.3f;
        float x0 = std::max(p.x, a), x1 = std::min(p.x + width, a + width * 0.3f);
        if (x1 > x0) dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + h), U(col), h);
    } else if (frac > 0) {
        dl->AddRectFilled(p, ImVec2(p.x + width * std::min(frac, 1.f), p.y + h), U(col), h);
    }
    ImGui::Dummy(ImVec2(width, h));
}

// ---------------------------------------------------------------------------
//  Courbe (aire remplie + ligne) avec repère max et marqueurs facultatifs
// ---------------------------------------------------------------------------
inline void Chart(const char* id, const float* v, int n, int capacity, ImVec2 size, ImVec4 col, const char* unit,
                  const unsigned char* markA = nullptr, const unsigned char* markB = nullptr) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##chart", size);
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float maxv = 0;
    for (int i = 0; i < n; i++) maxv = std::max(maxv, v[i]);
    if (maxv <= 0) maxv = 1;
    float nice = powf(10.f, floorf(log10f(maxv)));
    float top = ceilf(maxv * 1.15f / nice) * nice;
    float padL = 0, padB = 0;
    ImVec2 a = ImVec2(p.x + padL, p.y), b = ImVec2(p.x + size.x, p.y + size.y - padB);
    for (int g = 0; g <= 4; g++) {
        float y = b.y - (b.y - a.y) * g / 4.f;
        dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), U(WithA(P.text, Dark ? 0.06f : 0.07f)), 1);
    }
    char lbl[64];
    snprintf(lbl, sizeof lbl, "%.0f %s", top, unit);
    PushFontSz(FReg, Base - 3);
    dl->AddText(ImVec2(a.x + 4 * S, a.y + 2 * S), U(P.text3), lbl);
    ImGui::PopFont();
    int cap = std::max(capacity, std::max(n, 2));
    auto X = [&](int i) { return a.x + (b.x - a.x) * (float)i / (float)(cap - 1); };
    auto Y = [&](float val) { return b.y - (b.y - a.y) * std::min(val / top, 1.f); };
    if (n >= 2) {
        for (int i = 0; i < n - 1; i++) {
            ImVec2 q0 = ImVec2(X(i), Y(v[i])), q1 = ImVec2(X(i + 1), Y(v[i + 1]));
            dl->AddQuadFilled(q0, q1, ImVec2(q1.x, b.y), ImVec2(q0.x, b.y), U(WithA(col, 0.16f)));
        }
        for (int i = 0; i < n; i++) dl->PathLineTo(ImVec2(X(i), Y(v[i])));
        dl->PathStroke(U(col), 0, 2.0f * S);
    }
    for (int i = 0; i < n; i++) {
        if (markB && markB[i]) dl->AddRectFilled(ImVec2(X(i) - 1.5f * S, a.y), ImVec2(X(i) + 1.5f * S, b.y), U(WithA(P.red, 0.55f)));
        else if (markA && markA[i]) dl->AddCircleFilled(ImVec2(X(i), Y(v[i])), 3 * S, U(P.orange), 12);
    }
    if (hov && n > 0) {
        float mx = ImGui::GetIO().MousePos.x;
        int i = (int)roundf((mx - a.x) / (b.x - a.x) * (cap - 1));
        if (i >= 0 && i < n) {
            dl->AddLine(ImVec2(X(i), a.y), ImVec2(X(i), b.y), U(WithA(P.text, 0.25f)), 1);
            dl->AddCircleFilled(ImVec2(X(i), Y(v[i])), 4 * S, U(col), 12);
            ImGui::SetTooltip("%.1f %s", v[i], unit);
        }
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
//  Fenêtre sans cadre : pastilles rouge/jaune/vert, bords redimensionnables
// ---------------------------------------------------------------------------
struct Rect { float x0, y0, x1, y1; };
inline std::vector<Rect> HitRects;           // zones cliquables dans la bande du haut

inline void Hit() {
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    HitRects.push_back({ a.x, a.y, b.x, b.y });
}

inline void TrafficLights(HWND hwnd, ImVec2 pos) {
    float r = 6.5f * S, gap = 20 * S;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 old = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(pos.x - r - 2 * S, pos.y - r - 2 * S));
    ImGui::InvisibleButton("##lights", ImVec2(gap * 2 + r * 2 + 4 * S, r * 2 + 4 * S));
    Hit();
    bool hov = ImGui::IsItemHovered();
    bool active = GetForegroundWindow() == hwnd;
    const ImVec4 cols[3] = { Rgb(255, 95, 87), Rgb(254, 188, 46), Rgb(40, 200, 64) };
    ImVec2 m = ImGui::GetIO().MousePos;
    int clicked = -1;
    for (int i = 0; i < 3; i++) {
        ImVec2 c = ImVec2(pos.x + gap * i, pos.y);
        dl->AddCircleFilled(c, r, (active || hov) ? U(cols[i]) : U(WithA(P.text, 0.18f)), 24);
        dl->AddCircle(c, r, IM_COL32(0, 0, 0, 40), 24, 1.0f);
        if (hov) {
            ImU32 g = IM_COL32(0, 0, 0, 150);
            float s = r * 0.45f, th = 1.2f * S;
            if (i == 0) { dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), g, th); dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), g, th); }
            if (i == 1) dl->AddLine(ImVec2(c.x - s * 1.2f, c.y), ImVec2(c.x + s * 1.2f, c.y), g, th);
            if (i == 2) { dl->AddLine(ImVec2(c.x - s * 1.2f, c.y), ImVec2(c.x + s * 1.2f, c.y), g, th); dl->AddLine(ImVec2(c.x, c.y - s * 1.2f), ImVec2(c.x, c.y + s * 1.2f), g, th); }
            float dx = m.x - c.x, dy = m.y - c.y;
            if (dx * dx + dy * dy <= (r + 3 * S) * (r + 3 * S) && ImGui::IsMouseReleased(0)) clicked = i;
        }
    }
    ImGui::SetCursorScreenPos(old);
    if (clicked == 0) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    if (clicked == 1) ShowWindow(hwnd, SW_MINIMIZE);
    if (clicked == 2) ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
}

inline void SetupWindow(HWND h, bool dark) {
    BOOL d = dark ? TRUE : FALSE;
    if (DwmSetWindowAttribute(h, 20, &d, sizeof d) != S_OK) DwmSetWindowAttribute(h, 19, &d, sizeof d);
    int corner = 2;                          // coins arrondis (Windows 11)
    DwmSetWindowAttribute(h, 33, &corner, sizeof corner);
    MARGINS m = { 0, 0, 1, 0 };
    DwmExtendFrameIntoClientArea(h, &m);
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

inline bool HandleNc(HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT* out) {
    if (msg == WM_NCCALCSIZE && wp) {
        if (IsZoomed(h)) {
            auto* p = (NCCALCSIZE_PARAMS*)lp;
            int fx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
            int fy = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
            p->rgrc[0].left += fx; p->rgrc[0].right -= fx; p->rgrc[0].top += fy; p->rgrc[0].bottom -= fy;
        }
        *out = 0;
        return true;
    }
    if (msg == WM_NCACTIVATE) { *out = DefWindowProcW(h, msg, wp, -1); return true; }
    if (msg == WM_NCHITTEST) {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &pt);
        RECT rc;
        GetClientRect(h, &rc);
        if (!IsZoomed(h)) {
            int b = (int)(6 * S);
            bool L = pt.x < b, R = pt.x >= rc.right - b, T = pt.y < b, B = pt.y >= rc.bottom - b;
            if (T && L) { *out = HTTOPLEFT; return true; }
            if (T && R) { *out = HTTOPRIGHT; return true; }
            if (B && L) { *out = HTBOTTOMLEFT; return true; }
            if (B && R) { *out = HTBOTTOMRIGHT; return true; }
            if (L) { *out = HTLEFT; return true; }
            if (R) { *out = HTRIGHT; return true; }
            if (T) { *out = HTTOP; return true; }
            if (B) { *out = HTBOTTOM; return true; }
        }
        if (pt.y < TitleH * S) {
            for (auto& r : HitRects)
                if (pt.x >= r.x0 && pt.x < r.x1 && pt.y >= r.y0 && pt.y < r.y1) { *out = HTCLIENT; return true; }
            *out = HTCAPTION;
            return true;
        }
        *out = HTCLIENT;
        return true;
    }
    return false;
}

} // namespace ui
