#pragma once
#include <string>
#include <Windows.h>
#include <cstdio>

struct MMOStressConfig
{
    // ── [Connection] ────────────────────────────────────────────
    std::string serverIp            = "127.0.0.1";
    int         port                = 6000;
    int         clientCount         = 100;
    int         clientsPerThread    = 2000;
    int         reconnectIntervalMs = 1000;

    // ── [Timing] ────────────────────────────────────────────────
    int         loopDelayMs         = 1;
    int         tickIntervalMs      = 40;

    // ── [Scenario] ──────────────────────────────────────────────
    int         moveProbability         = 75;
    int         stopProbability         = 20;
    int         chatProbability         = 20;
    int         zoneChangeProbability   = 5;
    int         heartbeatIntervalSec    = 20;
    int         targetMapId             = 1;

    // ── [Map] ───────────────────────────────────────────────────
    int         mapWidth            = 400;
    int         mapHeight           = 400;

    // ── [Monitor] ───────────────────────────────────────────────
    int         monitorPort         = 9101;

    // ── [Test] ──────────────────────────────────────────────────
    int         testDurationSec     = 0;
    int         rampUpIntervalMs    = 0;

    // 실행 파일 경로 기준으로 MMOStressConfig.ini 로드
    bool Load()
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);

        std::wstring iniPath(exePath);
        size_t pos = iniPath.find_last_of(L"\\/");
        iniPath = iniPath.substr(0, pos + 1) + L"MMOStressConfig.ini";

        // 파일 존재 확인
        DWORD attr = GetFileAttributesW(iniPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES)
        {
            wprintf(L"[Config] MMOStressConfig.ini not found. Using defaults.\n");
            PrintConfig();
            return false;
        }

        const wchar_t* path = iniPath.c_str();

        // [Connection]
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Connection", L"ServerIp", L"127.0.0.1", buf, 256, path);
        char mbBuf[256];
        WideCharToMultiByte(CP_ACP, 0, buf, -1, mbBuf, 256, nullptr, nullptr);
        serverIp = mbBuf;

        port                = GetPrivateProfileIntW(L"Connection", L"Port", 6000, path);
        clientCount         = GetPrivateProfileIntW(L"Connection", L"ClientCount", 100, path);
        clientsPerThread    = GetPrivateProfileIntW(L"Connection", L"ClientsPerThread", 2000, path);
        reconnectIntervalMs = GetPrivateProfileIntW(L"Connection", L"ReconnectIntervalMs", 1000, path);

        // [Timing]
        loopDelayMs         = GetPrivateProfileIntW(L"Timing", L"LoopDelayMs", 1, path);
        tickIntervalMs      = GetPrivateProfileIntW(L"Timing", L"TickIntervalMs", 40, path);

        // [Scenario]
        moveProbability         = GetPrivateProfileIntW(L"Scenario", L"MoveProbability", 40, path);
        stopProbability         = GetPrivateProfileIntW(L"Scenario", L"StopProbability", 30, path);
        chatProbability         = GetPrivateProfileIntW(L"Scenario", L"ChatProbability", 5, path);
        zoneChangeProbability   = GetPrivateProfileIntW(L"Scenario", L"ZoneChangeProbability", 1, path);
        heartbeatIntervalSec    = GetPrivateProfileIntW(L"Scenario", L"HeartbeatIntervalSec", 20, path);
        targetMapId             = GetPrivateProfileIntW(L"Scenario", L"TargetMapId", 1, path);

        // [Map]
        mapWidth            = GetPrivateProfileIntW(L"Map", L"MapWidth", 400, path);
        mapHeight           = GetPrivateProfileIntW(L"Map", L"MapHeight", 400, path);

        // [Monitor]
        monitorPort         = GetPrivateProfileIntW(L"Monitor", L"MonitorPort", 9101, path);

        // [Test]
        testDurationSec     = GetPrivateProfileIntW(L"Test", L"TestDurationSec", 0, path);
        rampUpIntervalMs    = GetPrivateProfileIntW(L"Test", L"RampUpIntervalMs", 0, path);

        wprintf(L"[Config] Loaded from MMOStressConfig.ini\n");
        PrintConfig();
        return true;
    }

private:
    void PrintConfig() const
    {
        wprintf(L"  ── Connection ──\n");
        wprintf(L"  ServerIp          : %hs\n", serverIp.c_str());
        wprintf(L"  Port              : %d\n", port);
        wprintf(L"  ClientCount       : %d\n", clientCount);
        wprintf(L"  ClientsPerThread  : %d\n", clientsPerThread);
        wprintf(L"  ReconnectMs       : %d\n", reconnectIntervalMs);
        wprintf(L"  ── Timing ──\n");
        wprintf(L"  LoopDelayMs       : %d\n", loopDelayMs);
        wprintf(L"  TickIntervalMs    : %d\n", tickIntervalMs);
        wprintf(L"  ── Scenario ──\n");
        wprintf(L"  MoveProbability   : %d\n", moveProbability);
        wprintf(L"  StopProbability   : %d\n", stopProbability);
        wprintf(L"  ChatProbability   : %d\n", chatProbability);
        wprintf(L"  ZoneChangeProbab  : %d\n", zoneChangeProbability);
        wprintf(L"  HeartbeatSec      : %d\n", heartbeatIntervalSec);
        wprintf(L"  TargetMapId       : %d\n", targetMapId);
        wprintf(L"  ── Map ──\n");
        wprintf(L"  MapWidth          : %d\n", mapWidth);
        wprintf(L"  MapHeight         : %d\n", mapHeight);
        wprintf(L"  ── Monitor ──\n");
        wprintf(L"  MonitorPort       : %d\n", monitorPort);
        wprintf(L"  ── Test ──\n");
        wprintf(L"  TestDurationSec   : %d\n", testDurationSec);
        wprintf(L"  RampUpIntervalMs  : %d\n", rampUpIntervalMs);
    }
};
