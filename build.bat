@echo off
rem Compile Camra's Tool avec Visual Studio (MSVC x64) : out\Camra's Tool.exe
setlocal enabledelayedexpansion
cd /d "%~dp0"
where cl >nul 2>nul && goto :build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" echo [ERREUR] Visual Studio avec le C++ est requis. & exit /b 1
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || goto :err

:build
if not exist obj mkdir obj
if not exist out mkdir out
cl /nologo /O2 /EHsc /std:c++17 /utf-8 /MT tools\pack.cpp /Fo:obj\pack.obj /Fe:obj\pack.exe cabinet.lib || goto :err
.\obj\pack.exe resources\kit obj\kit.pak || goto :err
rc /nologo /c65001 /fo obj\app.res src\app.rc || goto :err
if not exist obj\app mkdir obj\app
cl /nologo /MP /O2 /MT /EHsc /std:c++17 /utf-8 /W3 /DNOMINMAX /DUNICODE /D_UNICODE /DNDEBUG /Ithird_party\imgui /Ithird_party\imgui\backends /Isrc /Fo:obj\app\ /c src\*.cpp third_party\imgui\*.cpp third_party\imgui\backends\*.cpp || goto :err
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:"out\Camra's Tool.exe" obj\app\*.obj obj\app.res d3d11.lib d3dcompiler.lib dxgi.lib dwmapi.lib setupapi.lib cfgmgr32.lib cabinet.lib version.lib shell32.lib ole32.lib comdlg32.lib advapi32.lib user32.lib gdi32.lib imm32.lib || goto :err
rem Lanceur : ouvre toujours la derniere version publiee
if not exist obj\launcher mkdir obj\launcher
rc /nologo /c65001 /fo obj\launcher.res launcher\launcher.rc || goto :err
cl /nologo /O2 /MT /EHsc /std:c++17 /utf-8 /W3 /DNOMINMAX /DUNICODE /D_UNICODE /DNDEBUG /Fo:obj\launcher\ /c launcher\launcher.cpp src\update.cpp src\util.cpp || goto :err
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:"out\Camras-Tool-Lanceur.exe" obj\launcher\*.obj obj\launcher.res version.lib shell32.lib ole32.lib advapi32.lib user32.lib gdi32.lib || goto :err
echo == OK : out\Camra's Tool.exe
exit /b 0

:err
echo [ERREUR] La compilation a echoue.
exit /b 1