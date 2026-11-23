// image.cpp - image des résultats, copiée dans le presse-papiers (à coller dans Discord)
#include "bench.h"
#include <objidl.h>
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

static void Txt(Graphics& g, const std::string& s, float x, float y, float size, Color c, bool bold = false, float right = 0) {
    Font f(L"Segoe UI", size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush b(c);
    std::wstring w = U2W(s);
    StringFormat fmt(StringFormat::GenericTypographic());
    if (right > 0) { fmt.SetAlignment(StringAlignmentFar); g.DrawString(w.c_str(), -1, &f, RectF(x, y, right - x, size * 2), &fmt, &b); }
    else g.DrawString(w.c_str(), -1, &f, PointF(x, y), &fmt, &b);
}

static Color RateColor(int r) {
    return r == R_EXCELLENT ? Color(48, 209, 88) : r == R_GOOD ? Color(64, 200, 224) : r == R_AVERAGE ? Color(255, 159, 10) : r == R_POOR ? Color(255, 69, 58) : Color(150, 150, 155);
}

bool ResultsImage(const BenchState& b, const wchar_t* png) {
    GdiplusStartupInput in;
    ULONG_PTR tok = 0;
    if (GdiplusStartup(&tok, &in, nullptr) != Ok) return false;
    bool ok = false;
    {
        int rows = 0;
        for (auto& r : b.res) if (r.ran) rows++;
        const int W = 900, H = b.stability ? 440 : 290 + rows * 38;
        Bitmap bmp(W, H, PixelFormat24bppRGB);
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.Clear(Color(29, 29, 31));
        Color white(245, 245, 247), grey(160, 160, 166), dim(110, 110, 116);
        SolidBrush card(Color(43, 43, 46));
        Txt(g, "Camra's Tool", 32, 22, 26, white, true);
        Txt(g, b.date, 32, 28, 16, grey, false, W - 32.f);
        Txt(g, Fmt("FPGA %s  ·  firmware %s  ·  %s", b.fpga.c(), b.fw.c(), b.usb.c()), 32, 60, 16, grey);
        if (b.simulated) Txt(g, T("SIMULATION : données fictives", "SIMULATION: fake data"), 32, 60, 16, Color(255, 159, 10), true, W - 32.f);
        struct K { std::string t, v, s; Color c; } k[3];
        if (b.stability) {
            int v = StabVerdict(b);
            k[0] = { T("Débit moyen", "Average throughput"), Num(b.sAvg, 1) + T(" Mo/s", " MB/s"), Fmt("min %s · max %s", Num(b.sMin, 1).c_str(), Num(b.sMax, 1).c_str()), RateColor(RateMbps(b.sAvg)) };
            k[1] = { T("Lectures réussies", "Successful reads"), Num(b.sSuccess, 2) + " %", Fmt("%d:%02d", (int)b.stabElapsed / 60, (int)b.stabElapsed % 60), b.sSuccess < 99.5 ? Color(255, 69, 58) : Color(48, 209, 88) };
            k[2] = { "Verdict", StabVerdictText(v), Fmt(T("%d coupure(s) · %d chute(s)", "%d dropout(s) · %d dip(s)"), b.sDrops, b.sDips), v == 0 ? Color(48, 209, 88) : v == 1 ? Color(255, 159, 10) : Color(255, 69, 58) };
        } else {
            double best = BestMbps(b), st = Stability(b);
            const TestResult& l = b.res[0];
            k[0] = { T("Débit maximal", "Max throughput"), best > 0 ? Num(best, 1) + T(" Mo/s", " MB/s") : "-", RatingText(RateMbps(best)), RateColor(RateMbps(best)) };
            k[1] = { T("Latence moyenne", "Average latency"), l.calls ? Num(l.latAvg, 1) + " µs" : "-", l.calls ? RatingText(RateLat(l.latAvg)) : "", RateColor(RateLat(l.latAvg)) };
            k[2] = { T("Stabilité", "Stability"), Num(st, 2) + " %", "", st < 99.5 ? Color(255, 69, 58) : Color(48, 209, 88) };
        }
        for (int i = 0; i < 3; i++) {
            float x = 32.f + i * 284;
            g.FillRectangle(&card, x, 98.f, 268.f, 118.f);
            Txt(g, k[i].t, x + 16, 110, 15, grey);
            Txt(g, k[i].v, x + 16, 134, 34, k[i].c, true);
            Txt(g, k[i].s, x + 16, 184, 15, grey);
        }
        if (b.stability) {
            g.FillRectangle(&card, 32.f, 236.f, W - 64.f, 170.f);
            float mx = 1;
            for (auto& s : b.stab) mx = std::max(mx, s.mbps);
            std::vector<PointF> pts;
            for (size_t i = 0; i < b.stab.size(); i++)
                pts.push_back(PointF(48.f + (W - 96.f) * i / std::max<size_t>(1, b.stab.size() - 1), 396.f - 140.f * b.stab[i].mbps / (mx * 1.1f)));
            Pen pen(Color(10, 132, 255), 2.5f);
            if (pts.size() > 1) g.DrawLines(&pen, pts.data(), (INT)pts.size());
        } else {
            float y = 236;
            Txt(g, "Test", 48, y, 14, dim);
            Txt(g, T("Débit", "Throughput"), 300, y, 14, dim, false, 480);
            Txt(g, T("Latence", "Latency"), 490, y, 14, dim, false, 660);
            Txt(g, T("Succès", "Success"), 680, y, 14, dim, false, W - 48.f);
            y += 24;
            for (int i = 0; i < 8; i++) {
                const TestResult& r = b.res[i];
                if (!r.ran) continue;
                g.FillRectangle(&card, 32.f, y, W - 64.f, 34.f);
                Txt(g, g_en ? TESTS[i].nameEn : TESTS[i].nameFr, 48, y + 6, 16, white);
                if (i) Txt(g, Num(r.mbps, 1) + T(" Mo/s", " MB/s"), 300, y + 6, 16, RateColor(RateMbps(r.mbps)), false, 480);
                Txt(g, Num(r.latAvg, 1) + " µs", 490, y + 6, 16, white, false, 660);
                Txt(g, Num(r.success, 2) + " %", 680, y + 6, 16, r.success < 99.5 ? Color(255, 69, 58) : white, false, W - 48.f);
                y += 38;
            }
        }
        if (png) {
            static const CLSID PNG = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
            ok = bmp.Save(png, &PNG, nullptr) == Ok;
        }
        HBITMAP hb = nullptr;
        if (!png && bmp.GetHBITMAP(Color(29, 29, 31), &hb) == Ok && hb) {
            DIBSECTION ds = {};
            if (GetObjectW(hb, sizeof ds, &ds) && ds.dsBm.bmBits) {
                size_t img = (size_t)ds.dsBm.bmWidthBytes * ds.dsBm.bmHeight;
                BITMAPINFOHEADER h = ds.dsBmih;
                h.biCompression = BI_RGB;
                h.biSizeImage = (DWORD)img;
                HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, sizeof h + img);
                if (m) {
                    BYTE* p = (BYTE*)GlobalLock(m);
                    memcpy(p, &h, sizeof h);
                    memcpy(p + sizeof h, ds.dsBm.bmBits, img);
                    GlobalUnlock(m);
                    for (int i = 0; i < 10 && !ok; i++) {
                        if (!OpenClipboard(g_hwnd)) { Sleep(20); continue; }
                        EmptyClipboard();
                        ok = SetClipboardData(CF_DIB, m) != nullptr;
                        CloseClipboard();
                        break;
                    }
                    if (!ok) GlobalFree(m);
                }
            }
            DeleteObject(hb);
        }
    }
    GdiplusShutdown(tok);
    return ok;
}
