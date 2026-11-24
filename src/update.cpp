// update.cpp - cherche une nouvelle version sur GitHub et l'installe si l'utilisateur le demande.
// Rien n'est envoyé. Le fichier vient de GitHub en HTTPS et son SHA-256 est comparé à celui publié.
#include "app.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <thread>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

// Fichier joint à chaque version : pas de limite de requêtes, contrairement à l'API (60 par heure et par adresse)
static const char* VERSION_URL = "https://github.com/camraa/dma/releases/latest/download/version.json";
static const char* API_URL = "https://api.github.com/repos/camraa/dma/releases/latest";
static const char* DL_PREFIX = "https://github.com/camraa/dma/releases/download/";

static std::mutex g_uMx;
static std::string g_new, g_url, g_sha, g_notesFr, g_notesEn;
static std::atomic<bool> g_updBusy{ false };
static std::atomic<int> g_updPct{ -1 };
std::string UpdateAvailable() { std::lock_guard<std::mutex> l(g_uMx); return g_new; }
std::string UpdateNotes() { std::lock_guard<std::mutex> l(g_uMx); return g_en ? g_notesEn : g_notesFr; }
bool UpdateBusy() { return g_updBusy; }
int UpdatePct() { return g_updPct; }

static bool Parse(const std::string& s, int v[3]) { return sscanf_s(s.c_str(), "%d.%d.%d", &v[0], &v[1], &v[2]) == 3; }

bool IsNewer(const std::string& a, const std::string& b) {
    int x[3], y[3];
    if (!Parse(a, x) || !Parse(b, y)) return false;
    return x[0] != y[0] ? x[0] > y[0] : x[1] != y[1] ? x[1] > y[1] : x[2] > y[2];
}

bool HttpGet(const std::string& url, std::string& out, size_t maxSize, std::atomic<int>* pct = nullptr) {
    std::wstring w = U2W(url);
    URL_COMPONENTS uc = { sizeof uc };
    wchar_t host[256], path[2048];
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
    HINTERNET s = WinHttpOpen(L"CamrasTool", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 8000, 8000, 8000, 30000);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE) : nullptr;
    DWORD code = 0, n = sizeof code;
    bool ok = false;
    if (r && WinHttpSendRequest(r, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr) &&
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &n, nullptr) && code == 200) {
        std::vector<char> buf(65536);
        DWORD got = 0, total = 0, tn = sizeof total;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &total, &tn, nullptr);
        ok = true;
        for (;;) {
            if (!WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &got)) { ok = false; break; }
            if (!got) break;
            out.append(buf.data(), got);
            if (pct && total) *pct = (int)(out.size() * 100ull / total);
            if (out.size() > maxSize) { ok = false; break; }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

std::string Sha256(const std::string& d) {
    unsigned char h[32];
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)d.data(), (ULONG)d.size(), h, 32) != 0) return "";
    std::string s;
    for (unsigned char x : h) s += Fmt("%02x", x);
    return s;
}

static std::string JsonStr(const std::string& b, const char* key, size_t from, size_t to) {
    size_t k = b.find(std::string("\"") + key + "\"", from);
    if (k == std::string::npos || k > to) return "";
    size_t a = b.find(':', k);
    if (a == std::string::npos) return "";
    a = b.find_first_not_of(" \t\r\n", a + 1);
    if (a == std::string::npos || b[a] != '"') return "";
    size_t e = b.find('"', a + 1);
    return e == std::string::npos ? "" : b.substr(a + 1, e - a - 1);
}

// Valeur texte avec ses échappements (\\n, \\", \\\\)
std::string JsonText(const std::string& b, const char* key) {
    size_t k = b.find(std::string("\"") + key + "\"");
    if (k == std::string::npos || (k = b.find(':', k)) == std::string::npos) return "";
    k = b.find_first_not_of(" \t\r\n", k + 1);
    if (k == std::string::npos || b[k] != '"') return "";
    std::string s;
    for (k++; k < b.size() && b[k] != '"'; k++) {
        if (b[k] == '\\' && k + 1 < b.size()) { k++; s += b[k] == 'n' ? '\n' : b[k]; }
        else s += b[k];
    }
    return s.substr(0, 2000);
}

// Dernière version publiée : version.json, sinon l'API (versions publiées avant la 1.4.2)
bool FetchRelease(std::string& body) {
    body.clear();
#ifdef CAMRAS_TEST_FEED                       // essais : faux version.json local
    wchar_t f[MAX_PATH];
    if (GetEnvironmentVariableW(L"CAMRAS_FEED", f, MAX_PATH)) return ReadFileAll(f, body);
#endif
    if (HttpGet(VERSION_URL, body, 1 << 20) && body.find("\"tag_name\"") != std::string::npos) return true;
    body.clear();
    return HttpGet(API_URL, body, 1 << 20);
}

// Lit la version, l'adresse et l'empreinte du fichier publié
bool ParseRelease(const std::string& body, std::string& ver, std::string& url, std::string& sha, const char* asset) {
    ver = JsonStr(body, "tag_name", 0, body.size());
    if (!ver.empty() && ver[0] == 'v') ver.erase(0, 1);
    int x[3];
    if (!Parse(ver, x)) return false;
    ver = Fmt("%d.%d.%d", x[0], x[1], x[2]);
    url.clear();
    sha.clear();
    size_t as = body.find("\"assets\"");
    size_t k = as;
    while (k != std::string::npos && (k = body.find("\"name\"", k)) != std::string::npos) {
        if (JsonStr(body, "name", k, body.size()) == asset) {
            size_t next = body.find("\"name\"", k + 6);
            if (next == std::string::npos) next = body.size();
            std::string dg = JsonStr(body, "digest", k, next), u = JsonStr(body, "browser_download_url", k, next);
            if (dg.rfind("sha256:", 0) == 0 && dg.size() == 71 && u.rfind(DL_PREFIX, 0) == 0) { sha = dg.substr(7); url = u; }
            break;
        }
        k += 6;
    }
    return true;
}

// 1 nouvelle version, 0 à jour, -1 GitHub injoignable
static int CheckOnce() {
    std::string body, ver, url, sha;
    if (!FetchRelease(body) || !ParseRelease(body, ver, url, sha)) return -1;
    if (!IsNewer(ver, APP_VERSION)) return 0;
    std::lock_guard<std::mutex> l(g_uMx);
    if (g_new != ver) Log(L_INFO, T("Nouvelle version disponible : %s", "New version available: %s"), ver.c_str());
    g_new = ver; g_url = url; g_sha = sha;
    g_notesFr = JsonText(body, "notes_fr");
    g_notesEn = JsonText(body, "notes_en");
    return 1;
}

void UpdateCheckAsync() {
    std::thread([] {
        for (;;) {
            if (g_set.updates && !g_updBusy) CheckOnce();
            Sleep(2 * 3600 * 1000);
        }
    }).detach();
}

void UpdateCheckNow() {
    std::thread([] {
        int r = CheckOnce();
        if (r == 0) Toast(Fmt(T("Vous avez la dernière version (%s).", "You have the latest version (%s)."), APP_VERSION));
        if (r < 0) Toast(T("GitHub est injoignable : vérifiez la connexion Internet.", "GitHub cannot be reached: check the Internet connection."), true);
        if (r > 0) Toast(T("Mise à jour disponible : voir le bandeau en haut.", "Update available: see the banner at the top."));
    }).detach();
}

int SwapExe(const std::wstring& path, const std::string& data) {
    std::wstring neu = path + L".new", old = path + L".old";
    if (!WriteFileAll(neu, data.data(), data.size())) { DeleteFileW(neu.c_str()); return 1; }
    DeleteFileW(old.c_str());
    if (FileExists(path) && !MoveFileExW(path.c_str(), old.c_str(), 0)) { DeleteFileW(neu.c_str()); return 2; }
    if (!MoveFileExW(neu.c_str(), path.c_str(), 0)) { MoveFileExW(old.c_str(), path.c_str(), 0); return 2; }
    DeleteFileW(old.c_str());                // reste en place si l'exe est en cours d'utilisation
    return 0;
}

static std::wstring SelfPath() {
    wchar_t p[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH * 2);
    return std::wstring(p, n);
}

void UpdateCleanup() { DeleteFileW((SelfPath() + L".old").c_str()); }

// Télécharge, vérifie, remplace l'exe (l'ancien est gardé en .old) puis relance
void UpdateInstallAsync() {
    std::string url, sha;
    { std::lock_guard<std::mutex> l(g_uMx); url = g_url; sha = g_sha; }
    if (url.empty() || sha.empty()) { OpenUrl(RELEASES_URL); return; }
    bool expected = false;
    if (!g_updBusy.compare_exchange_strong(expected, true)) return;
    std::thread([url, sha] {
        auto fail = [](const char* m) { Log(L_ERR, "%s", m); Toast(m, true); g_updBusy = false; };
        std::string exe;
        g_updPct = -1;
        if (!HttpGet(url, exe, 64u << 20, &g_updPct)) return fail(T("Téléchargement de la mise à jour impossible.", "Cannot download the update."));
        if (Sha256(exe) != Lower(sha)) return fail(T("Mise à jour refusée : le fichier téléchargé ne correspond pas à l'empreinte publiée.", "Update refused: the downloaded file does not match the published hash."));
        std::wstring self = SelfPath();
        int r = SwapExe(self, exe);
        if (r == 1) {
            fail(T("Dossier protégé : téléchargez la mise à jour depuis la page.", "Protected folder: download the update from the page."));
            OpenUrl(RELEASES_URL);
            return;
        }
        if (r) return fail(T("Remplacement de l'exe impossible.", "Cannot replace the exe."));
        ShellExecuteW(nullptr, L"open", self.c_str(), L"--after-update", nullptr, SW_SHOWNORMAL);
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    }).detach();
}
