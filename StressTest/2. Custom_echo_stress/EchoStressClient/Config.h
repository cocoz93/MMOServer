#pragma once
#include <string>
#include <Windows.h>
#include <cstdio>

struct Config
{
    // ── 설정 항목 ───────────────────────────────────────────────────
    std::string serverIp        = "127.0.0.1";  // 접속할 서버 IP 주소
    int         port            = 6000;          // 서버 포트 번호
    int         clientCount     = 1000;          // 동시 접속 더미 클라이언트 수
    bool        disconnectTest  = true;          // true 시 무작위 간격으로 끊었다 재접속 반복
    int         overSendCount   = 100;           // 에코 응답을 받지 못한 상태에서 추가로 보낼 수 있는 최대 패킷 수 (미응답 허용 윈도우)
    int         loopDelayMs     = 1;             // 각 클라이언트 루프 1회당 sleep 시간(ms). 낮을수록 송신 빈도 증가
    int         reconnectIntervalMs = 1000;      // 재접속 최소 대기 시간(ms). disconnectTest=true 시 사용. 실제 대기는 이 값 ~ 5배 사이 랜덤
    int         echoTimeoutMs   = 500;           // 에코 미응답 판정 기준 시간(ms). 이 시간 내 응답 없으면 EchoNotRecv 카운터 증가
    int         testDurationSec = 0;             // 테스트 지속 시간(초). 0이면 수동 종료까지 무한 실행
    int         monitorPort     = 9092;          // Prometheus 메트릭 HTTP 포트
    int         minPacketSize   = 12;            // 에코 패킷 최소 크기(B). 하한 12 (헤더4+echoValue8)
    int         maxPacketSize   = 256;           // 에코 패킷 최대 크기(B). 상한 4096 (서버 MAX_PACKET_SIZE)
    int         rampUpIntervalMs = 0;            // 점진 접속 간격(ms). 0이면 전체 동시 접속, >0이면 해당 간격마다 1명씩 추가
    int         attackMode       = 0;            // 0=정상 에코, 1=비정상 패킷 크기, 2=패킷 폭주, 3=idle(타임아웃), 4=sendQ 압박
    int         attackClientCount = 0;           // 0=전원 공격, N=앞에서 N명만 공격 (나머지 정상 에코)

    // 실행 파일 경로 기준으로 ini 로드 (인자 없으면 StressConfig.ini)
    bool Load(const char* iniFileName = nullptr)
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);

        std::wstring iniPath(exePath);
        size_t pos = iniPath.find_last_of(L"\\/");

        if (iniFileName)
        {
            wchar_t wBuf[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, iniFileName, -1, wBuf, MAX_PATH);
            iniPath = iniPath.substr(0, pos + 1) + wBuf;
        }
        else
        {
            iniPath = iniPath.substr(0, pos + 1) + L"StressConfig.ini";
        }

        // 파일 존재 확인
        DWORD attr = GetFileAttributesW(iniPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES)
        {
            wprintf(L"[Config] StressConfig.ini not found. Using defaults.\n");
            PrintConfig();
            return false;
        }

        const wchar_t* path = iniPath.c_str();

        // [Stress] 섹션
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Stress", L"ServerIp", L"127.0.0.1", buf, 256, path);
        // wchar_t → std::string 변환
        char mbBuf[256];
        WideCharToMultiByte(CP_ACP, 0, buf, -1, mbBuf, 256, nullptr, nullptr);
        serverIp = mbBuf;

        port                = GetPrivateProfileIntW(L"Stress", L"Port", 6000, path);
        clientCount         = GetPrivateProfileIntW(L"Stress", L"ClientCount", 1000, path);
        disconnectTest      = (GetPrivateProfileIntW(L"Stress", L"DisconnectTest", 1, path) != 0);
        overSendCount       = GetPrivateProfileIntW(L"Stress", L"OverSendCount", 100, path);
        loopDelayMs         = GetPrivateProfileIntW(L"Stress", L"LoopDelayMs", 1, path);
        reconnectIntervalMs = GetPrivateProfileIntW(L"Stress", L"ReconnectIntervalMs", 1000, path);
        echoTimeoutMs       = GetPrivateProfileIntW(L"Stress", L"EchoTimeoutMs", 500, path);
        testDurationSec     = GetPrivateProfileIntW(L"Stress", L"TestDurationSec", 0, path);
        monitorPort         = GetPrivateProfileIntW(L"Stress", L"MonitorPort", 9092, path);
        minPacketSize       = GetPrivateProfileIntW(L"Stress", L"MinPacketSize", 12, path);
        maxPacketSize       = GetPrivateProfileIntW(L"Stress", L"MaxPacketSize", 256, path);
        rampUpIntervalMs    = GetPrivateProfileIntW(L"Stress", L"RampUpIntervalMs", 0, path);
        attackMode          = GetPrivateProfileIntW(L"Stress", L"AttackMode", 0, path);
        attackClientCount   = GetPrivateProfileIntW(L"Stress", L"AttackClientCount", 0, path);

        // 유효성 보정
        if (minPacketSize < 12)   minPacketSize = 12;
        if (maxPacketSize > 4096) maxPacketSize = 4096;
        if (maxPacketSize < 12)   maxPacketSize = 12;
        if (minPacketSize > maxPacketSize) minPacketSize = maxPacketSize;
        if (attackMode < 0 || attackMode > 4) attackMode = 0;
        if (attackClientCount < 0) attackClientCount = 0;
        if (attackClientCount > clientCount) attackClientCount = clientCount;

        wprintf(L"[Config] Loaded from StressConfig.ini\n");
        PrintConfig();
        return true;
    }

private:
    void PrintConfig() const
    {
        wprintf(L"  ServerIp       : %hs\n", serverIp.c_str());
        wprintf(L"  Port           : %d\n", port);
        wprintf(L"  ClientCount    : %d\n", clientCount);
        wprintf(L"  DisconnectTest : %d\n", disconnectTest ? 1 : 0);
        wprintf(L"  OverSendCount  : %d\n", overSendCount);
        wprintf(L"  LoopDelayMs    : %d\n", loopDelayMs);
        wprintf(L"  ReconnectMs    : %d\n", reconnectIntervalMs);
        wprintf(L"  EchoTimeoutMs  : %d\n", echoTimeoutMs);
        wprintf(L"  TestDurationSec: %d\n", testDurationSec);
        wprintf(L"  MonitorPort    : %d\n", monitorPort);
        wprintf(L"  MinPacketSize  : %d\n", minPacketSize);
        wprintf(L"  MaxPacketSize  : %d\n", maxPacketSize);
        wprintf(L"  RampUpInterval : %d ms\n", rampUpIntervalMs);
        wprintf(L"  AttackMode     : %d\n", attackMode);
        wprintf(L"  AttackClients  : %d\n", attackClientCount);
    }
};
