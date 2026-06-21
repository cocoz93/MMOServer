#pragma once
#include <string>
#include <Windows.h>
#include <cstdio>
#include <cstdlib>      // wcstol (ClientCores 범위 파싱)

struct MMOStressConfig
{
    // ── [Connection] ────────────────────────────────────────────
    std::string serverIp            = "127.0.0.1";
    int         port                = 6000;
    int         clientCount         = 100;
    int         clientsPerThread    = 2000;
    int         reconnectIntervalMs = 1000;
    int         connectTimeoutMs    = 5000;
    unsigned long long affinityMask = 0;   // 클라 프로세스 코어 핀 (0=미적용)

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
        // 기본값도 멤버 현재값(구조체 기본값)에서 가져온다 — 정수 키와 동일하게
        // 기본값 정의처를 구조체 한 곳으로 단일화 (string이라 wide 변환 1단계 경유)
        wchar_t defIp[256];
        MultiByteToWideChar(CP_ACP, 0, serverIp.c_str(), -1, defIp, 256);
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Connection", L"ServerIp", defIp, buf, 256, path);
        char mbBuf[256];
        WideCharToMultiByte(CP_ACP, 0, buf, -1, mbBuf, 256, nullptr, nullptr);
        serverIp = mbBuf;

        // 기본값(nDefault)은 멤버의 현재값(= 구조체 기본값)을 그대로 전달한다.
        // → 기본값 정의처를 구조체 한 곳으로 단일화: "파일 없음"과 "키 누락"이
        //   항상 동일한 값을 가지며, 양쪽 기본값이 어긋나는 일을 원천 차단한다.
        port                = GetPrivateProfileIntW(L"Connection", L"Port", port, path);
        clientCount         = GetPrivateProfileIntW(L"Connection", L"ClientCount", clientCount, path);
        clientsPerThread    = GetPrivateProfileIntW(L"Connection", L"ClientsPerThread", clientsPerThread, path);
        reconnectIntervalMs = GetPrivateProfileIntW(L"Connection", L"ReconnectIntervalMs", reconnectIntervalMs, path);
        connectTimeoutMs    = GetPrivateProfileIntW(L"Connection", L"ConnectTimeoutMs", connectTimeoutMs, path);

        // ClientCores: 물리코어 범위("6-9") → 논리코어 비트마스크 (HT 형제 자동 포함)
        // 서버와 코어가 겹치지 않게 잡아 한 PC 동시 측정 시 격리한다.
        wchar_t coresBuf[64];
        GetPrivateProfileStringW(L"Connection", L"ClientCores", L"", coresBuf, 64, path);
        affinityMask = ParsePhysicalCoreMask(coresBuf);

        // [Timing]
        loopDelayMs         = GetPrivateProfileIntW(L"Timing", L"LoopDelayMs", loopDelayMs, path);
        tickIntervalMs      = GetPrivateProfileIntW(L"Timing", L"TickIntervalMs", tickIntervalMs, path);

        // [Scenario]
        moveProbability         = GetPrivateProfileIntW(L"Scenario", L"MoveProbability", moveProbability, path);
        stopProbability         = GetPrivateProfileIntW(L"Scenario", L"StopProbability", stopProbability, path);
        chatProbability         = GetPrivateProfileIntW(L"Scenario", L"ChatProbability", chatProbability, path);
        zoneChangeProbability   = GetPrivateProfileIntW(L"Scenario", L"ZoneChangeProbability", zoneChangeProbability, path);
        heartbeatIntervalSec    = GetPrivateProfileIntW(L"Scenario", L"HeartbeatIntervalSec", heartbeatIntervalSec, path);
        targetMapId             = GetPrivateProfileIntW(L"Scenario", L"TargetMapId", targetMapId, path);

        // [Monitor]
        monitorPort         = GetPrivateProfileIntW(L"Monitor", L"MonitorPort", monitorPort, path);

        // [Test]
        testDurationSec     = GetPrivateProfileIntW(L"Test", L"TestDurationSec", testDurationSec, path);
        rampUpIntervalMs    = GetPrivateProfileIntW(L"Test", L"RampUpIntervalMs", rampUpIntervalMs, path);

        wprintf(L"[Config] Loaded from MMOStressConfig.ini\n");
        PrintConfig();
        return true;
    }

private:
    // "6-9" 또는 "3" 형태의 물리코어 범위 → 논리코어 비트마스크.
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

    void PrintConfig() const
    {
        wprintf(L"  ── Connection ──\n");
        wprintf(L"  ServerIp          : %hs\n", serverIp.c_str());
        wprintf(L"  Port              : %d\n", port);
        wprintf(L"  ClientCount       : %d\n", clientCount);
        wprintf(L"  ClientsPerThread  : %d\n", clientsPerThread);
        wprintf(L"  ReconnectMs       : %d\n", reconnectIntervalMs);
        wprintf(L"  ConnectTimeoutMs  : %d\n", connectTimeoutMs);
        wprintf(L"  AffinityMask      : 0x%llX\n", affinityMask);
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
        wprintf(L"  ── Monitor ──\n");
        wprintf(L"  MonitorPort       : %d\n", monitorPort);
        wprintf(L"  ── Test ──\n");
        wprintf(L"  TestDurationSec   : %d\n", testDurationSec);
        wprintf(L"  RampUpIntervalMs  : %d\n", rampUpIntervalMs);
    }
};
