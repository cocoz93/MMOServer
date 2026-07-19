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
#include <cstdlib>      // strtol (ServerCores 범위 파싱)

#include "IniFile.h"              // 플랫폼 독립 INI 리더 (GetPrivateProfile* 대체)
#include "Platform/Platform.h"    // Platform::GetExecutableDir
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
        // 실행 파일 디렉토리 기준 INI 경로 (플랫폼 독립)
        std::string iniPath = Platform::GetExecutableDir() + "IOCP_ServerConfig.ini";

        CIniFile ini;
        if (!ini.Load(iniPath))
        {
            SLOG_INFO("[ServerConfig] IOCP_ServerConfig.ini not found. Using defaults.");
            SetDefaultMaps();
            return false;
        }

        // [Server] 섹션
        mode        = ParseServerMode(ini.GetString("Server", "Mode", "GameServer"));
        port        = ini.GetInt("Server", "Port", 6000);
        maxClients  = ini.GetInt("Server", "MaxClients", 1000);
        monitorPort = ini.GetInt("Server", "MonitorPort", 9090);
        monitorEnabled = (ini.GetInt("Server", "MonitorEnabled", 0) != 0);

        // ServerCores: 물리코어 범위("0-5")를 받아 논리코어 비트마스크로 변환
        // (HT 형제 자동 포함 — 한 물리코어를 서버/클라가 쪼개 쓰는 격리 깨짐을 방지)
        affinityMask = ParsePhysicalCoreMask(ini.GetString("Server", "ServerCores", ""));

        // WorkerThreads: IOCP 워커 스레드 수 (0=서버 affinity 코어 수로 자동)
        workerThreads = ini.GetInt("Server", "WorkerThreads", 0);

        // SendWorkers: 전용 송신 워커 수 (0/1=단일 스레드, 2+=sessionId%K 워커 풀)
        sendWorkers = ini.GetInt("Server", "SendWorkers", 0);

        // [DB] 섹션 — DB 저장 파이프라인 (값은 파서가 narrow std::string으로 반환)
        dbHost              = ini.GetString("DB", "Host", "127.0.0.1");
        dbPort              = ini.GetInt("DB", "Port", 3306);
        dbUser              = ini.GetString("DB", "User", "root");
        dbPassword          = ini.GetString("DB", "Password", "");
        dbDatabase          = ini.GetString("DB", "Database", "gamedb");
        dbWorkers           = ini.GetInt("DB", "Workers", 1);
        dbSavePeriodSec     = ini.GetInt("DB", "SavePeriodSec", 10);
        dbConnectTimeoutSec = ini.GetInt("DB", "ConnectTimeoutSec", 3);
        dbRwTimeoutSec      = ini.GetInt("DB", "RwTimeoutSec", 5);
        dbQueueMax          = ini.GetInt("DB", "QueueMax", 20000);

        int mapCount = ini.GetInt("Server", "MapCount", 3);

        // [MapDefault] 섹션 로드
        MapConfig defaultMap = {};
        defaultMap.mapWidth             = ini.GetInt("MapDefault", "Width", 120);
        defaultMap.mapHeight            = ini.GetInt("MapDefault", "Height", 120);
        defaultMap.sectorSize           = ini.GetInt("MapDefault", "SectorSize", 20);
        defaultMap.maxPlayersPerChannel = ini.GetInt("MapDefault", "MaxPlayersPerChannel", 100);

        // [Map0] ~ [MapN-1] 섹션 순회 (없으면 디폴트 적용)
        maps.clear();
        for (int i = 0; i < mapCount; ++i)
        {
            std::string section = "Map" + std::to_string(i);

            MapConfig mc;
            mc.mapId                = i;
            mc.mapWidth             = ini.GetInt(section, "Width", defaultMap.mapWidth);
            mc.mapHeight            = ini.GetInt(section, "Height", defaultMap.mapHeight);
            mc.sectorSize           = ini.GetInt(section, "SectorSize", defaultMap.sectorSize);
            mc.maxPlayersPerChannel = ini.GetInt(section, "MaxPlayersPerChannel", defaultMap.maxPlayersPerChannel);
            maps.push_back(mc);
        }

        PrintConfig();
        return true;
    }

private:
    // "0-5" 또는 "3" 형태의 물리코어 범위 → 논리코어 비트마스크.
    // 가정: 물리코어 k = 논리코어 2k, 2k+1 (Intel HT 표준 매핑).
    // 빈 문자열·숫자 아님 → 0(미적용).
    static unsigned long long ParsePhysicalCoreMask(const std::string& str)
    {
        const char* s = str.c_str();
        char* end = nullptr;
        long first = std::strtol(s, &end, 10);
        if (end == s) return 0;                          // 숫자로 시작 안 함
        long last = first;
        if (*end == '-') last = std::strtol(end + 1, &end, 10);
        if (first < 0 || last < first) return 0;
        if (last > 31) last = 31;                        // 64비트 마스크 상한 (물리코어 0~31)
        unsigned long long mask = 0;
        for (long k = first; k <= last; ++k)
            mask |= (0x3ull << (2 * k));                 // HT 형제 2논리코어
        return mask;
    }

    void SetDefaultMaps()
    {
        maps = 
        {
            { 0, 120, 120, 20, 100 },
        };
    }

    static ServerMode ParseServerMode(const std::string& s)
    {
        if (s == "GameCodiEchoTest")    return ServerMode::GameCodiEchoTest;
        if (s == "NetWorkLib_EchoTest") return ServerMode::NetWorkLib_EchoTest;
        if (s == "GameServer")          return ServerMode::GameServer;

        SLOG_WARN("[ServerConfig] Unknown Mode '{}'. Defaulting to GameServer.", s);
        return ServerMode::GameServer;
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
        SLOG_INFO("  DB          : {}:{} db={} user={} workers={} save={}s qmax={}",
                  dbHost, dbPort, dbDatabase, dbUser, dbWorkers, dbSavePeriodSec, dbQueueMax);
        SLOG_INFO("  Maps        : {}", maps.size());
    }
};
