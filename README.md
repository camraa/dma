# Camra's Tool — DMA Speedtest

Speedtest, diagnostic et flash de cartes DMA (FT601 / LeechCore) pour Windows 10/11 64 bits. Interface FR/EN.

## Compiler
Visual Studio avec le développement C++ (MSVC x64 et SDK Windows), puis :
- `build.bat` : `out\Camra's Tool.exe`
- `make_dist.bat` : paquet client `dist\Camras-Tool-1.4.2.zip`

## Tests
`Camra's Tool.exe --test --data=DOSSIER` : tests sans carte (simulation), résultat dans `DOSSIER\tests.txt`, code de sortie = nombre d'échecs. Lancés par GitHub à chaque modification.

## Lanceur
`Camras-Tool-Lanceur.exe` lance Camra's Tool ; si une version plus récente est publiée, il affiche « Mise à jour disponible » et propose de la télécharger (empreinte SHA-256 vérifiée, jamais de retour à une version plus ancienne). Il se met à jour lui-même et crée les raccourcis au premier lancement. `version.json`, joint à chaque version par `tools/version.ps1`, contient la version, les empreintes et les nouveautés lues dans `NOTES.md`. L'application, les réglages et l'historique sont dans `%LOCALAPPDATA%\CamrasTool\app`.

## Organisation
- `src/` : application (Dear ImGui + DirectX 11)
  - `leech.cpp` LeechCore et simulation · `bench.cpp` mesures, stabilité, historique · `diag.cpp` diagnostic
  - `sysinfo.cpp` USB et pilotes · `firmware.cpp` OpenOCD/CH347 · `pages.cpp` onglets · `ui.h` thème
- `resources/` : DLL et kit intégrés à l'exe (OpenOCD, bitstreams, pilotes), à ne pas modifier
- `tools/pack.cpp` : compression du kit
- `tools/sauvegarde.ps1 -Dest DOSSIER` : copie du code sur un autre disque
- `docs/` : page de téléchargement (GitHub Pages)
- `dist_files/` : textes livrés aux clients

## Options de lancement
`--sim` `--en` `--light` `--page=0..7` `--mode=0..4` `--autostart` `--diag` `--noconnect` `--nocard` `--algos` `--selftest` `--guide` `--cable` `--data=DOSSIER`

## Licence
Copyright (C) 2026 1sfk. Le code de Camra's Tool (`src/`, `tools/`, scripts) est sous [GNU GPL version 3](LICENSE).

La GPL ne s'applique pas aux composants tiers de `resources/` et `third_party/` : chacun garde sa propre licence. Les fichiers FTDI, WCH et Microsoft sont propriétaires. Voir [LICENCES-TIERCES.txt](dist_files/LICENCES-TIERCES.txt).
