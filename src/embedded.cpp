// embedded.cpp - fichiers intégrés à l'exe : DLL LeechCore et kit de flashage
#include "app.h"
#include <compressapi.h>
#include <thread>

// Ressources RCDATA (voir app.rc)
enum { RES_LEECHCORE = 101, RES_FTD3XX = 102, RES_VCRUNTIME = 103, RES_KIT = 104 };

static bool GetRes(int id, const uint8_t** data, DWORD* size) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    if (!g) return false;
    *data = (const uint8_t*)LockResource(g);
    *size = SizeofResource(nullptr, r);
    return *data && *size;
}

// Écrit le fichier seulement s'il est absent ou différent (une DLL chargée ailleurs reste en place)
static bool WriteIfDifferent(const std::wstring& path, const uint8_t* data, size_t n) {
    std::string cur;
    if (ReadFileAll(path, cur) && cur.size() == n && memcmp(cur.data(), data, n) == 0) return true;
    return WriteFileAll(path, data, n);
}

std::wstring BuiltinLibDir() { return LocalDir() + L"\\lib"; }

bool ExtractBuiltinLibs() {
    std::wstring dir = BuiltinLibDir();
    MakeDirs(dir);
    struct { int id; const wchar_t* name; } files[] = {
        { RES_LEECHCORE, L"leechcore.dll" }, { RES_FTD3XX, L"FTD3XX.dll" }, { RES_VCRUNTIME, L"vcruntime140.dll" } };
    bool ok = true;
    for (auto& f : files) {
        const uint8_t* d; DWORD n;
        if (!GetRes(f.id, &d, &n)) { ok = false; continue; }
        if (!WriteIfDifferent(dir + L"\\" + f.name, d, n)) {
            ok = false;
            Log(L_WARN, T("Écriture impossible : %s (erreur %lu).", "Cannot write %s (error %lu)."),
                W2U(dir + L"\\" + f.name).c_str(), GetLastError());
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
//  Kit de flashage : "CTPK", version, nombre, puis pour chaque fichier
//  (u16 longueur, nom UTF-16, u32 taille, u32 taille compressée), puis les données LZMS.
// ---------------------------------------------------------------------------
static std::atomic<bool> g_kitReady{ false };
static std::atomic<bool> g_kitBusy{ false };

std::wstring KitDir() { return LocalDir() + L"\\flash"; }
bool KitReady() { return g_kitReady; }

static bool ExtractKit() {
    const uint8_t* p; DWORD size;
    if (!GetRes(RES_KIT, &p, &size) || size < 12 || memcmp(p, "CTPK", 4) != 0) {
        Log(L_ERR, "%s", T("Kit de flashage absent de l'exécutable.", "Flashing kit missing from the executable."));
        return false;
    }
    uint32_t count = *(const uint32_t*)(p + 8);
    std::wstring dir = KitDir();
    std::string id = Fmt("%lu-%u", (unsigned long)size, count);

    struct Entry { std::wstring name; uint32_t raw, comp; };
    std::vector<Entry> ents;
    size_t o = 12;
    for (uint32_t i = 0; i < count; i++) {
        if (o + 2 > size) return false;
        uint16_t len = *(const uint16_t*)(p + o); o += 2;
        if (o + len * 2 + 8 > size) return false;
        Entry e;
        e.name.assign((const wchar_t*)(p + o), len); o += len * 2;
        e.raw = *(const uint32_t*)(p + o); e.comp = *(const uint32_t*)(p + o + 4); o += 8;
        ents.push_back(e);
    }

    // Déjà extrait (même kit, tous les fichiers présents avec la bonne taille) ?
    std::string cur;
    if (ReadFileAll(dir + L"\\pak.id", cur) && cur.substr(0, id.size()) == id) {
        bool all = true;
        for (auto& e : ents) {
            WIN32_FILE_ATTRIBUTE_DATA fa;
            if (!GetFileAttributesExW((dir + L"\\" + e.name).c_str(), GetFileExInfoStandard, &fa) || fa.nFileSizeLow != e.raw) { all = false; break; }
        }
        if (all) return true;
    }

    Log(L_INFO, T("Firmware : extraction d'OpenOCD et des bitstreams dans %s…", "Firmware: extracting OpenOCD and bitstreams to %s…"), W2U(dir).c_str());
    DECOMPRESSOR_HANDLE dh = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &dh)) {
        Log(L_ERR, "CreateDecompressor %lu", GetLastError());
        return false;
    }
    std::vector<uint8_t> buf;
    bool ok = true;
    for (auto& e : ents) {
        if (o + e.comp > size) { ok = false; break; }
        buf.resize(e.raw);
        SIZE_T outSz = 0;
        if (e.raw && !Decompress(dh, p + o, e.comp, buf.data(), e.raw, &outSz)) {
            Log(L_ERR, T("Décompression impossible : %s", "Cannot decompress: %s"), W2U(e.name).c_str());
            ok = false; break;
        }
        o += e.comp;
        std::wstring path = dir + L"\\" + e.name;
        MakeDirs(path.substr(0, path.find_last_of(L'\\')));
        if (!WriteIfDifferent(path, buf.data(), e.raw)) {
            Log(L_ERR, T("Écriture impossible : %s (erreur %lu).", "Cannot write %s (error %lu)."), W2U(path).c_str(), GetLastError());
            ok = false; break;
        }
    }
    CloseDecompressor(dh);
    if (!ok) return false;
    WriteFileAll(dir + L"\\pak.id", id.data(), id.size());
    Log(L_OK, T("Firmware : kit prêt (%d fichiers).", "Firmware: kit ready (%d files)."), (int)count);
    return true;
}

// Extrait de l'exe les fichiers dont le nom commence par prefix (sans passer par le disque de l'utilisateur)
bool ExtractKitPrefix(const std::wstring& prefix, const std::wstring& dir) {
    const uint8_t* p; DWORD size;
    if (!GetRes(RES_KIT, &p, &size) || size < 12 || memcmp(p, "CTPK", 4) != 0) return false;
    uint32_t count = *(const uint32_t*)(p + 8);
    struct Entry { std::wstring name; uint32_t raw, comp; };
    std::vector<Entry> ents;
    size_t o = 12;
    for (uint32_t i = 0; i < count; i++) {
        if (o + 2 > size) return false;
        uint16_t len = *(const uint16_t*)(p + o); o += 2;
        if (o + len * 2 + 8 > size) return false;
        Entry e;
        e.name.assign((const wchar_t*)(p + o), len); o += len * 2;
        e.raw = *(const uint32_t*)(p + o); e.comp = *(const uint32_t*)(p + o + 4); o += 8;
        ents.push_back(e);
    }
    DECOMPRESSOR_HANDLE dh = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &dh)) return false;
    bool ok = true, any = false;
    for (auto& e : ents) {
        if (o + e.comp > size) { ok = false; break; }
        if (e.name.rfind(prefix, 0) == 0) {
            std::vector<uint8_t> buf(e.raw);
            SIZE_T n = 0;
            std::wstring name = e.name.substr(prefix.size());
            if ((e.raw && !Decompress(dh, p + o, e.comp, buf.data(), e.raw, &n)) || !WriteFileAll(dir + L"\\" + name, buf.data(), e.raw)) { ok = false; break; }
            any = true;
        }
        o += e.comp;
    }
    CloseDecompressor(dh);
    return ok && any;
}

void KitEnsureAsync() {
    bool expected = false;
    if (g_kitReady || !g_kitBusy.compare_exchange_strong(expected, true)) return;
    std::thread([] {
        g_kitReady = ExtractKit();
        g_kitBusy = false;
    }).detach();
}
