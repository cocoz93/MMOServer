@echo off
REM ============================================
REM   MMO Build - Release x64 (vswhere 자동 탐색)
REM   사용법: .build.bat [target]
REM     (없음)     : 전체 (server + gameclient + echo + mmo)
REM     server     : IOCP_Server
REM     gameclient : GameClient
REM     echo       : EchoStressClient
REM     mmo        : MMOStressClient
REM   산출물: Run\bin\
REM ============================================
setlocal

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=all"

echo ============================================
echo   MMO Build (Release x64) - target: %TARGET%
echo ============================================
echo.

REM === MSBuild 탐색 (vswhere: VS 에디션/버전/설치경로 무관) ===
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found! ^(Visual Studio 2017+ required^)
    goto :ERROR
)
set "MSBUILD="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do if not defined MSBUILD set "MSBUILD=%%i"
if not defined MSBUILD (
    echo [ERROR] MSBuild not found via vswhere!
    goto :ERROR
)
echo [MSBuild] %MSBUILD%
echo.

REM === 솔루션 경로 ===
set "SLN_SERVER=%~dp0..\IOCP_Server\IOCP_Server.sln"
set "SLN_GAMECLIENT=%~dp0..\GameClient\GameClient.sln"
set "SLN_ECHO=%~dp0..\StressTest\2. Custom_echo_stress\EchoStressClient.sln"
set "SLN_MMO=%~dp0..\StressTest\3. MMO_stress\MMOStressClient.sln"

if /I "%TARGET%"=="all" (
    call :BUILD "IOCP_Server"      "%SLN_SERVER%"     || goto :ERROR
    call :BUILD "GameClient"       "%SLN_GAMECLIENT%" || goto :ERROR
    call :BUILD "EchoStressClient" "%SLN_ECHO%"       || goto :ERROR
    call :BUILD "MMOStressClient"  "%SLN_MMO%"        || goto :ERROR
) else if /I "%TARGET%"=="server" (
    call :BUILD "IOCP_Server" "%SLN_SERVER%" || goto :ERROR
) else if /I "%TARGET%"=="gameclient" (
    call :BUILD "GameClient" "%SLN_GAMECLIENT%" || goto :ERROR
) else if /I "%TARGET%"=="echo" (
    call :BUILD "EchoStressClient" "%SLN_ECHO%" || goto :ERROR
) else if /I "%TARGET%"=="mmo" (
    call :BUILD "MMOStressClient" "%SLN_MMO%" || goto :ERROR
) else (
    echo [ERROR] Unknown target: %TARGET%
    echo         Usage: .build.bat [all ^| server ^| gameclient ^| echo ^| mmo]
    goto :ERROR
)

echo.
echo ============================================
echo   [OK] Build finished.  Output: %~dp0bin
echo ============================================
pause
exit /b 0

REM === 빌드 서브루틴: %1=표시이름, %2=.sln 경로 ===
:BUILD
echo   - Building %~1 ...
"%MSBUILD%" "%~2" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] %~1 build failed!
    exit /b 1
)
echo   - %~1 OK
exit /b 0

:ERROR
echo.
echo ============================================
echo   [FAILED] Build error. Check log above.
echo ============================================
pause
exit /b 1
