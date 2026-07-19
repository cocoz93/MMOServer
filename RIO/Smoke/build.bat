@echo off
setlocal
rem RIO Phase 0 smoke build (finds VS via vswhere, falls back to VS2022 default paths)
rem note: no ( ) blocks and no for-backquote here -- both mis-parse with "%ProgramFiles(x86)%"
set "VSDIR="
set "VSINST=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSINST%\vswhere.exe" goto :fallback
"%VSINST%\vswhere.exe" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\rio_vsdir.txt" 2>nul
set /p VSDIR=<"%TEMP%\rio_vsdir.txt"
rem (temp file is overwritten on every run; intentionally not deleted)
:fallback
if not defined VSDIR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VSDIR=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not defined VSDIR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VSDIR=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
if not defined VSDIR echo [build] Visual Studio with C++ tools not found
if not defined VSDIR exit /b 1
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 echo [build] vcvars64 failed
if errorlevel 1 exit /b 1
pushd "%~dp0"
cl /nologo /W4 /O2 /EHsc /std:c++17 main.cpp /Fe:RioSmoke.exe ws2_32.lib
set ERR=%ERRORLEVEL%
popd
exit /b %ERR%
