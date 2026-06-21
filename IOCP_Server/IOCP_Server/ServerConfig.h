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
        SLOG_INFO("  Maps        : {}", maps.size());
    }
};
