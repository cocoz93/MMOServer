@echo off
REM ============================================
REM   MMO Release Upload - Run 폴더 패키징 + GitHub Release 업로드
REM   사용법: .release_upload.bat [tag]
REM     (없음) : bin-latest (고정 태그, 다운로드 URL 항상 동일)
REM     v0.1   : 지정한 태그로 릴리스
REM   zip 이름: MMO_Run_<날짜>_<git해시>.zip  (예: MMO_Run_20260601_a1b2c3d.zip)
REM   자산 정책: 최신만 유지 (기존 릴리스 삭제 후 재생성, 태그는 보존)
REM   포함: 배치(*.bat), bin\*.exe(서버+클라 3종), bin\*.ini
REM   제외: *.pdb, LanServer 레거시 exe, logs/CrashDump/Results, .claude
REM   선행 조건: .build.bat 로 exe 산출 + gh auth login 완료
REM ============================================
setlocal

set "TAG=%~1"
if "%TAG%"=="" set "TAG=bin-latest"

set "RUN_DIR=%~dp0"
set "STAGE_ROOT=%TEMP%\MMO_release_stage"
set "STAGE=%STAGE_ROOT%\Run"

REM === 버전 문자열 생성 (날짜 + git 해시, 더티 표시) ===
set "BDATE=00000000"
for /f "delims=" %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd" 2^>nul') do set "BDATE=%%i"
set "GHASH=nogit"
for /f "delims=" %%i in ('git -C "%RUN_DIR%." describe --always --dirty 2^>nul') do set "GHASH=%%i"
set "VERSION=%BDATE%_%GHASH%"
set "ZIPNAME=MMO_Run_%VERSION%.zip"
set "ZIP=%TEMP%\%ZIPNAME%"

REM === gh 탐색 (PATH 우선, 없으면 기본 설치 경로) ===
where gh >nul 2>&1 && (set "GH=gh") || (set "GH=%ProgramFiles%\GitHub CLI\gh.exe")
if not exist "%GH%" if /I not "%GH%"=="gh" (
    echo [ERROR] gh CLI not found.  install: winget install GitHub.cli
    goto :ERROR
)

echo ============================================
echo   MMO Release Upload  -  tag: %TAG%
echo   version: %VERSION%
echo ============================================
echo.

REM === 스테이징 디렉터리 초기화 ===
if exist "%STAGE_ROOT%" rmdir /s /q "%STAGE_ROOT%"
mkdir "%STAGE%\bin"
if exist "%ZIP%" del /q "%ZIP%"

REM === 배치 파일 복사 ===
copy /y "%RUN_DIR%*.bat" "%STAGE%\" >nul

REM === exe 복사 (레거시/pdb 제외, 명시한 4개만) ===
for %%E in (IOCP_Server.exe MMOStressClient.exe EchoStressClient.exe GameClient.exe) do (
    if exist "%RUN_DIR%bin\%%E" (
        copy /y "%RUN_DIR%bin\%%E" "%STAGE%\bin\" >nul
    ) else (
        echo [WARN] missing: bin\%%E   ^(먼저 .build.bat 실행 필요?^)
    )
)

REM === ini 복사 ===
copy /y "%RUN_DIR%bin\*.ini" "%STAGE%\bin\" >nul

REM === zip 생성 ===
echo   - Compressing -^> %ZIP%
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%STAGE%' -DestinationPath '%ZIP%' -Force"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compress-Archive failed!
    goto :ERROR
)

REM === Release 교체 (최신만 유지): 기존 삭제 후 재생성 ===
"%GH%" release view %TAG% >nul 2>&1
if %ERRORLEVEL%==0 (
    echo   - Replacing release '%TAG%' ^(keep latest only^)
    "%GH%" release delete %TAG% --yes >nul 2>&1
)
echo   - Create release '%TAG%'   asset: %ZIPNAME%
"%GH%" release create %TAG% "%ZIP%" --title "MMO binaries (%TAG%)" --notes "Build %VERSION% - Run package (batch + exe + ini). Unzip and use the Run folder as-is."
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] gh release failed!
    goto :ERROR
)

echo.
echo ============================================
echo   [OK] Release '%TAG%' updated.   asset: %ZIPNAME%
"%GH%" release view %TAG% --json url -q ".url"
echo ============================================
pause
exit /b 0

:ERROR
echo.
echo ============================================
echo   [FAILED] Release error. Check log above.
echo ============================================
pause
exit /b 1
