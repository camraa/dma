@echo off
rem Compile puis cree dist\Camras-Tool-VERSION et le zip a donner aux clients
setlocal
cd /d "%~dp0"
set VER=1.2.0
call "%~dp0build.bat" || exit /b 1
set "D=dist\Camras-Tool-%VER%"
if exist "%D%" rmdir /s /q "%D%"
mkdir "%D%" || exit /b 1
copy /y "out\Camra's Tool.exe" "%D%\" >nul || exit /b 1
copy /y dist_files\*.txt "%D%\" >nul || exit /b 1
powershell -NoProfile -Command "Compress-Archive -Path '%D%\*' -DestinationPath 'dist\Camras-Tool-%VER%.zip' -Force" || exit /b 1
echo == OK : dist\Camras-Tool-%VER%.zip