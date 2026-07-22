#pragma once

// ==========================================================================
// ServerConfig — INI 파일 기반 서버 설정 로더
//
// [사용법]
//  ServerConfig config;
//  config.Load();  // 실행 파일 옆의 IOCP_ServerConfig.ini 로드
//
// [INI 포맷]
//  [Server]
//  Mode=GameServer
//  Port=6000
//  MaxClients=1000
//  MonitorPort=9090
//  MapCount=1
//
//  [MapDefault]
//  Width=120
//  Height=120
//  SectorSize=20
//  MaxPlayersPerChannel=100
//
//  [Map1]              ← 개별 오버라이드 (없으면 MapDefault 적용)
//  Width=600
// ==========================================================================

#include <string>
#include <vector>
#include <iostream>
#include <cstdlib>      // wcstol (ServerCores 범위 파싱)
#include <Windows.h>

#include "Common.h"
#include "MapManager.h"
#include "../../Shared/Common/ErrorLog.h"

struct ServerConfig
{
    ServerMode  mode         = ServerMode::GameServer;
    int         port         = 6000;
    int         maxClients   = 1000;
    int         monitorPort  = 9090;
    bool        monitorEnabled = false;
    unsigned long long affinityMask = 0;   // 프로세스를 묶을 CPU 코어 마스크 (0=미적용)
    int         workerThreads = 0;         // IOCP 워커 스레드 수 (0=서버 affinity 코어 수로 자동 산정)
    int         sendWorkers   = 0;         // 전용 송신 워커 수 (0/1=단일, 2+=sessionId%K 워커 풀; A/B 실험용)
    int         rioWorkers    = 0;         // RIO 전송 워커 수 (USE_RIO_TRANSPORT=1 빌드 전용, 0=자동 2)

    // 게임스레드 코어 격리 (실험용) — GameCore INI에서 도출. 빈값/부적합이면 gameCore=-1, 마스크 0(=off).
    int                gameCore     = -1;  // 게임루프 전용 물리코어 (-1=격리 off). ServerCores 안의 코어여야 함.
    unsigned long long gameCoreMask = 0;   // 게임코어 HT 논리쌍 마스크
    unsigned long long ioCoreMask   = 0;   // ServerCores에서 게임코어를 뺀 나머지(=I/O 스레드용)

    // [DB] 저장 파이프라인 설정 (값은 USE_DB_WORKER 토글과 무관하게 항상 로드)
    std::string dbHost              = "127.0.0.1";
    int         dbPort              = 3306;
    std::string dbUser              = "root";
    std::string dbPassword;
    std::string dbDatabase          = "gamedb";
    int         dbWorkers           = 1;    // 1단계는 단일 워커
    int         dbSavePeriodSec     = 10;   // dirty 저장 주기(초)
    int         dbConnectTimeoutSec = 3;
    int         dbRwTimeoutSec      = 5;    // 읽기/쓰기 소켓 타임아웃(초) — DB 무응답 시 재연결 판단
    int         dbQueueMax          = 20000; // 백프레셔: 워커당 큐 상한(초과분 드롭)

    std::vector<MapConfig> maps;

    // 실행 파일 경로 기준으로 IOCP_ServerConfig.ini 로드
    bool Load()
    {
        // 실행 파일 디렉토리에서 INI 경로 구성
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);

        std::wstring iniPath(exePath);
        size_t pos = iniPath.find_last_of(L"\\/");
        iniPath = iniPath.substr(0, pos + 1) + L"IOCP_ServerConfig.ini";

        // 파일 존재 확인
        DWORD attr = GetFileAttributesW(iniPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES)
        {
            SLOG_INFO("[ServerConfig] IOCP_ServerConfig.ini not found. Using defaults.");
            SetDefaultMaps();
            return false;
        }

        const wchar_t* path = iniPath.c_str();

        // [Server] 섹션
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Server", L"Mode", L"GameServer", buf, 256, path);
        mode = ParseServerMode(buf);

        port        = GetPrivateProfileIntW(L"Server", L"Port", 6000, path);
        maxClients  = GetPrivateProfileIntW(L"Server", L"MaxClients", 1000, path);
        monitorPort = GetPrivateProfileIntW(L"Server", L"MonitorPort", 9090, path);
        monitorEnabled = (GetPrivateProfileIntW(L"Server", L"MonitorEnabled", 0, path) != 0);

        // ServerCores: 물리코어 범위("0-5")를 받아 논리코어 비트마스크로 변환
        // (HT 형제 자동 포함 — 한 물리코어를 서버/클라가 쪼개 쓰는 격리 깨짐을 방지)
        wchar_t coresBuf[64];
        GetPrivateProfileStringW(L"Server", L"ServerCores", L"", coresBuf, 64, path);
        affinityMask = ParsePhysicalCoreMask(coresBuf);

        // GameCore: 게임루프를 고정할 물리코어 (빈값=격리 off). ServerCores 안의 코어여야 하며,
        //   나머지 코어로 I/O 스레드를 몰아 게임스레드 L2 캐시 간섭을 차단한다. (마스크는 아래서 도출)
        wchar_t gameCoreBuf[16];
        GetPrivateProfileStringW(L"Server", L"GameCore", L"", gameCoreBuf, 16, path);
        DeriveGameCoreIsolation(gameCoreBuf);

        // WorkerThreads: IOCP 워커 스레드 수 (0=서버 affinity 코어 수로 자동)
        workerThreads = GetPrivateProfileIntW(L"Server", L"WorkerThreads", 0, path);

        // SendWorkers: 전용 송신 워커 수 (0/1=단일 스레드, 2+=sessionId%K 워커 풀)
        sendWorkers = GetPrivateProfileIntW(L"Server", L"SendWorkers", 0, path);

        // RioWorkers: RIO 전송 워커 수 (USE_RIO_TRANSPORT=1 빌드에서만 사용, 0=자동 2)
        rioWorkers = GetPrivateProfileIntW(L"Server", L"RioWorkers", 0, path);

        // [DB] 섹션 — DB 저장 파이프라인 (문자열은 기존 WtoA로 std::string 변환)
        wchar_t dbBuf[256];
        GetPrivateProfileStringW(L"DB", L"Host", L"127.0.0.1", dbBuf, 256, path);
        dbHost = WtoA(dbBuf);
        dbPort = GetPrivateProfileIntW(L"DB", L"Port", 3306, path);
        GetPrivateProfileStringW(L"DB", L"User", L"root", dbBuf, 256, path);
        dbUser = WtoA(dbBuf);
        GetPrivateProfileStringW(L"DB", L"Password", L"", dbBuf, 256, path);
        dbPassword = WtoA(dbBuf);
        GetPrivateProfileStringW(L"DB", L"Database", L"gamedb", dbBuf, 256, path);
        dbDatabase = WtoA(dbBuf);
        dbWorkers           = GetPrivateProfileIntW(L"DB", L"Workers", 1, path);
        dbSavePeriodSec     = GetPrivateProfileIntW(L"DB", L"SavePeriodSec", 10, path);
        dbConnectTimeoutSec = GetPrivateProfileIntW(L"DB", L"ConnectTimeoutSec", 3, path);
        dbRwTimeoutSec      = GetPrivateProfileIntW(L"DB", L"RwTimeoutSec", 5, path);
        dbQueueMax          = GetPrivateProfileIntW(L"DB", L"QueueMax", 20000, path);

        int mapCount = GetPrivateProfileIntW(L"Server", L"MapCount", 3, path);

        // [MapDefault] 섹션 로드
        MapConfig defaultMap = {};
        defaultMap.mapWidth            = GetPrivateProfileIntW(L"MapDefault", L"Width", 120, path);
        defaultMap.mapHeight           = GetPrivateProfileIntW(L"MapDefault", L"Height", 120, path);
        defaultMap.sectorSize          = GetPrivateProfileIntW(L"MapDefault", L"SectorSize", 20, path);
        defaultMap.maxPlayersPerChannel = GetPrivateProfileIntW(L"MapDefault", L"MaxPlayersPerChannel", 100, path);

        // [Map0] ~ [MapN-1] 섹션 순회 (없으면 디폴트 적용)
        maps.clear();
        for (int i = 0; i < mapCount; ++i)
        {
            wchar_t section[16];
            swprintf_s(section, L"Map%d", i);

            MapConfig mc;
            mc.mapId               = i;
            mc.mapWidth            = GetPrivateProfileIntW(section, L"Width", defaultMap.mapWidth, path);
            mc.mapHeight           = GetPrivateProfileIntW(section, L"Height", defaultMap.mapHeight, path);
            mc.sectorSize          = GetPrivateProfileIntW(section, L"SectorSize", defaultMap.sectorSize, path);
            mc.maxPlayersPerChannel = GetPrivateProfileIntW(section, L"MaxPlayersPerChannel", defaultMap.maxPlayersPerChannel, path);
            maps.push_back(mc);
        }

        PrintConfig();
        return true;
    }

private:
    // "0-5" 또는 "3" 형태의 물리코어 범위 → 논리코어 비트마스크.
    // 가정: 물리코어 k = 논리코어 2k, 2k+1 (Intel HT 표준 매핑).
    // 빈 문자열·숫자 아님 → 0(미적용).
    static unsigned long long ParsePhysicalCoreMask(const wchar_t* s)
    {
        if (!s) return 0;
        wchar_t* end = nullptr;
        long first = wcstol(s, &end, 10);
        if (end == s) return 0;                          // 숫자로 시작 안 함
        long last = first;
        if (*end == L'-') last = wcstol(end + 1, &end, 10);
        if (first < 0 || last < first) return 0;
        if (last > 31) last = 31;                        // 64비트 마스크 상한 (물리코어 0~31)
        unsigned long long mask = 0;
        for (long k = first; k <= last; ++k)
            mask |= (0x3ull << (2 * k));                 // HT 형제 2논리코어
        return mask;
    }

    // GameCore 문자열 → gameCore/gameCoreMask/ioCoreMask 도출. affinityMask(ServerCores)가 선행돼야 함.
    // 빈값·비숫자·범위밖·ServerCores밖·남는코어없음 중 하나라도면 격리 off(전부 0/-1)로 두고 WARN.
    void DeriveGameCoreIsolation(const wchar_t* s)
    {
        gameCore = -1; gameCoreMask = 0; ioCoreMask = 0;
        if (!s || s[0] == L'\0')
            return;                                       // 빈값 = 격리 off (기본)

        wchar_t* end = nullptr;
        long core = wcstol(s, &end, 10);
        if (end == s || core < 0 || core > 31)
        {
            SLOG_WARN("[ServerConfig] GameCore '{}' 무시 — 물리코어 숫자(0~31)가 아님", WtoA(s));
            return;
        }
        if (affinityMask == 0)
        {
            SLOG_WARN("[ServerConfig] GameCore={} 무시 — ServerCores 미설정(프로세스 미고정이라 격리 불가)", core);
            return;
        }
        const unsigned long long gm = (0x3ull << (2 * core));   // 게임코어 HT 논리쌍
        if ((gm & affinityMask) != gm)
        {
            SLOG_WARN("[ServerConfig] GameCore={} 무시 — ServerCores(0x{:X}) 밖의 코어", core, affinityMask);
            return;
        }
        const unsigned long long io = affinityMask & ~gm;       // 나머지 코어(=I/O)
        if (io == 0)
        {
            SLOG_WARN("[ServerConfig] GameCore={} 무시 — I/O에 남는 코어 없음(ServerCores 물리코어 2개 이상 필요)", core);
            return;
        }
        gameCore = static_cast<int>(core);
        gameCoreMask = gm;
        ioCoreMask   = io;
    }

    void SetDefaultMaps()
    {
        maps = 
        {
            { 0, 120, 120, 20, 100 },
        };
    }

    static ServerMode ParseServerMode(const wchar_t* str)
    {
        std::wstring s(str);
        if (s == L"GameCodiEchoTest")    return ServerMode::GameCodiEchoTest;
        if (s == L"NetWorkLib_EchoTest") return ServerMode::NetWorkLib_EchoTest;
        if (s == L"GameServer")          return ServerMode::GameServer;

        SLOG_WARN("[ServerConfig] Unknown Mode '{}'. Defaulting to GameServer.", WtoA(str));
        return ServerMode::GameServer;
    }

    static std::string WtoA(const wchar_t* wstr)
    {
        int len = WideCharToMultiByte(CP_ACP, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        std::string result(len - 1, '\0');
        WideCharToMultiByte(CP_ACP, 0, wstr, -1, &result[0], len, nullptr, nullptr);
        return result;
    }

    void PrintConfig() const
    {
        const char* modeName = "Unknown";
        switch (mode)
        {
        case ServerMode::GameCodiEchoTest:    modeName = "GameCodiEchoTest";    break;
        case ServerMode::NetWorkLib_EchoTest: modeName = "NetWorkLib_EchoTest"; break;
        case ServerMode::GameServer:          modeName = "GameServer";          break;
        }

        SLOG_INFO("[ServerConfig] Loaded from INI");
        SLOG_INFO("  Mode        : {}", modeName);
        SLOG_INFO("  Port        : {}", port);
        SLOG_INFO("  MaxClients  : {}", maxClients);
        SLOG_INFO("  MonitorPort : {}", monitorPort);
        SLOG_INFO("  MonitorOn   : {}", monitorEnabled ? "true" : "false");
        SLOG_INFO("  Affinity    : 0x{:X}", affinityMask);
        SLOG_INFO("  WorkerThr   : {} (0=auto)", workerThreads);
        SLOG_INFO("  SendWkr     : {} (0/1=single)", sendWorkers);
        SLOG_INFO("  RioWkr      : {} (0=auto 2, RIO build only)", rioWorkers);
        if (gameCore >= 0)
            SLOG_INFO("  CoreIso     : ON GameCore={} game=0x{:X} io=0x{:X}", gameCore, gameCoreMask, ioCoreMask);
        else
            SLOG_INFO("  CoreIso     : off (GameCore 미설정)");
        SLOG_INFO("  DB          : {}:{} db={} user={} workers={} save={}s qmax={}",
                  dbHost, dbPort, dbDatabase, dbUser, dbWorkers, dbSavePeriodSec, dbQueueMax);
        SLOG_INFO("  Maps        : {}", maps.size());
    }
};
