// update.cpp - cherche une nouvelle version sur GitHub (lecture seule, aucune donnée envoyée)
#include "app.h"
#include <winhttp.h>
#include <thread>
#pragma comment(lib, "winhttp.lib")

static std::mutex g_uMx;
static std::string g_new;
std::string UpdateAvailable() { std::lock_guard<std::mutex> l(g_uMx); return g_new; }

static bool Parse(const std::string& s, int v[3]) { return sscanf_s(s.c_str(), "%d.%d.%d", &v[0], &v[1], &v[2]) == 3; }

void UpdateCheckAsync() {
    std::thread([] {
        std::string body;
        HINTERNET s = WinHttpOpen(L"CamrasTool", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
        if (!s) return;
        WinHttpSetTimeouts(s, 5000, 5000, 5000, 5000);
        HINTERNET c = WinHttpConnect(s, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", L"/repos/camraa/dma/releases/latest", nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE) : nullptr;
        DWORD code = 0, n = sizeof code;
        if (r && WinHttpSendRequest(r, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr) &&
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &n, nullptr) && code == 200) {
            char buf[4096];
            DWORD got = 0;
            while (body.size() < 262144 && WinHttpReadData(r, buf, sizeof buf, &got) && got) body.append(buf, got);
        }
        if (r) WinHttpCloseHandle(r);
        if (c) WinHttpCloseHandle(c);
        WinHttpCloseHandle(s);
        size_t k = body.find("\"tag_name\"");
        if (k == std::string::npos) return;
        size_t a = body.find('"', body.find(':', k)), e = a == std::string::npos ? a : body.find('"', a + 1);
        if (e == std::string::npos) return;
        std::string tag = body.substr(a + 1, e - a - 1);
        if (!tag.empty() && tag[0] == 'v') tag.erase(0, 1);
        int x[3], y[3];
        if (!Parse(tag, x) || !Parse(APP_VERSION, y)) return;
        bool newer = x[0] != y[0] ? x[0] > y[0] : x[1] != y[1] ? x[1] > y[1] : x[2] > y[2];
        if (!newer) return;
        std::string clean = Fmt("%d.%d.%d", x[0], x[1], x[2]);
        Log(L_INFO, T("Nouvelle version disponible : %s", "New version available: %s"), clean.c_str());
        std::lock_guard<std::mutex> l(g_uMx);
        g_new = clean;
    }).detach();
}
