// pack.cpp - assemble le kit de flashage (resources\kit) en un seul fichier compressé
// Usage : pack.exe <dossier> <sortie.pak>
// Format : "CTPK", u32 version, u32 nombre de fichiers, puis pour chaque fichier
//          (u16 longueur du nom, nom UTF-16 relatif, u32 taille, u32 taille compressée),
//          puis les données compressées (LZMS, API de compression Windows) dans le même ordre.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <compressapi.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

static void Walk(const std::wstring& root, const std::wstring& rel, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\" + rel + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) Walk(root, rel + n + L"\\", out);
        else out.push_back(rel + n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static bool ReadAll(const std::wstring& p, std::vector<BYTE>& v) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    v.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    bool ok = v.empty() || (ReadFile(h, v.data(), (DWORD)v.size(), &rd, nullptr) && rd == v.size());
    CloseHandle(h);
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { fwprintf(stderr, L"usage: pack <dossier> <sortie.pak>\n"); return 2; }
    std::wstring root = argv[1];
    std::vector<std::wstring> files;
    Walk(root, L"", files);
    std::sort(files.begin(), files.end());

    COMPRESSOR_HANDLE ch = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &ch)) { fwprintf(stderr, L"CreateCompressor %lu\n", GetLastError()); return 1; }

    std::vector<BYTE> header, data;
    auto put32 = [&](uint32_t v) { header.insert(header.end(), (BYTE*)&v, (BYTE*)&v + 4); };
    header.insert(header.end(), { 'C', 'T', 'P', 'K' });
    put32(2);
    put32((uint32_t)files.size());
    unsigned long long rawTotal = 0;
    for (auto& f : files) {
        std::vector<BYTE> in, out;
        if (!ReadAll(root + L"\\" + f, in)) { fwprintf(stderr, L"lecture impossible : %s\n", f.c_str()); return 1; }
        SIZE_T need = 0;
        if (!in.empty()) {
            Compress(ch, in.data(), in.size(), nullptr, 0, &need);
            out.resize(need);
            if (!Compress(ch, in.data(), in.size(), out.data(), out.size(), &need)) { fwprintf(stderr, L"compression impossible : %s\n", f.c_str()); return 1; }
            out.resize(need);
        }
        uint16_t len = (uint16_t)f.size();
        header.insert(header.end(), (BYTE*)&len, (BYTE*)&len + 2);
        header.insert(header.end(), (BYTE*)f.c_str(), (BYTE*)(f.c_str() + f.size()));
        put32((uint32_t)in.size());
        put32((uint32_t)out.size());
        data.insert(data.end(), out.begin(), out.end());
        rawTotal += in.size();
    }
    CloseCompressor(ch);

    HANDLE h = CreateFileW(argv[2], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { fwprintf(stderr, L"écriture impossible : %s\n", argv[2]); return 1; }
    DWORD w;
    WriteFile(h, header.data(), (DWORD)header.size(), &w, nullptr);
    WriteFile(h, data.data(), (DWORD)data.size(), &w, nullptr);
    CloseHandle(h);
    wprintf(L"kit : %zu fichiers, %.1f Mo -> %.1f Mo\n", files.size(), rawTotal / 1048576.0, (header.size() + data.size()) / 1048576.0);
    return 0;
}
