# Camra's Tool — DMA Speedtest

Speedtest, diagnostic et flash de cartes DMA (FT601 / LeechCore) pour Windows 10/11 64 bits. Interface FR/EN.

## Compiler
Visual Studio avec le développement C++ (MSVC x64 et SDK Windows), puis :
- `build.bat` : `out\Camra's Tool.exe`
- `make_dist.bat` : paquet client `dist\Camras-Tool-1.3.0.zip`

## Organisation
- `src/` : application (Dear ImGui + DirectX 11)
  - `leech.cpp` LeechCore et simulation · `bench.cpp` mesures, stabilité, historique · `diag.cpp` diagnostic
  - `sysinfo.cpp` USB et pilotes · `firmware.cpp` OpenOCD/CH347 · `pages.cpp` onglets · `ui.h` thème
- `resources/` : DLL et kit intégrés à l'exe (OpenOCD, bitstreams, pilotes), à ne pas modifier
- `tools/pack.cpp` : compression du kit
- `dist_files/` : textes livrés aux clients

## Options de lancement
`--sim` `--en` `--light` `--page=0..7` `--mode=0..4` `--autostart` `--diag` `--noconnect` `--nocard` `--algos` `--selftest`

## Licences tierces
Voir [LICENCES-TIERCES.txt](dist_files/LICENCES-TIERCES.txt) (LeechCore GPL-3, OpenOCD GPL-2, FTDI, WCH, Microsoft, ImGui MIT).