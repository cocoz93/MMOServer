@echo off
setlocal

echo ============================================
echo   Build and Run - Server / Client
echo ============================================
echo.

REM === 1. Kill running processes ===
echo [1/3] Killing running processes...
taskkill /F /IM IOCP_MMOServer.exe 2>nul && echo   - Server killed || echo   - Server not running
taskkill /F /IM IOCP_MMOClient.exe 2>nul && echo   - Client killed || echo   - Client not running
echo.

REM === 2. MSBuild path ===
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo [ERROR] MSBuild not found!
    pause
    exit /b 1
)

REM === 3. Build ===
echo [2/3] Building...
echo   - Building Server...
"%MSBUILD%" "%~dp0IOCP_MMOServer\IOCP_MMOServer.sln" /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Server build failed!
    pause
    exit /b 1
)
echo   - Server build OK
echo.

echo   - Building Client...
"%MSBUILD%" "%~dp0IOCP_MMOClient\IOCP_MMOClient.sln" /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Client build failed!
    pause
    exit /b 1
)
echo   - Client build OK
echo.

REM === 4. Run ===
echo [3/3] Starting...
start "" "%~dp0IOCP_MMOServer\x64\Debug\IOCP_MMOServer.exe"
echo   - Server started

timeout /t 2 /nobreak >nul

start "" "%~dp0IOCP_MMOClient\x64\Debug\IOCP_MMOClient.exe"
echo   - Client started
echo.

echo ============================================
echo   Done! Server and Client are running.
echo ============================================
pause
