#pragma once
#include <string>

struct Config
{
    // ── 사용자 입력 항목 ─────────────────────────────────────────
    std::string serverIp        = "127.0.0.1";  // 접속할 서버 IP 주소
    int         port            = 6000;          // 서버 포트 번호
    int         clientCount     = 100;           // 동시 접속 더미 클라이언트 수
    bool        disconnectTest  = true;         // true 시 무작위 간격으로 끊었다 재접속 반복
    int         overSendCount   = 100;           // 에코 응답을 받지 못한 상태에서 추가로 보낼 수 있는 최대 패킷 수 (미응답 허용 윈도우)
    int         loopDelayMs     = 1;           // 각 클라이언트 루프 1회당 sleep 시간(ms). 낮을수록 송신 빈도 증가

    // ── 고정 디폴트 항목 (변경 불필요) ──────────────────────────
    int         reconnectIntervalMs = 1000;      // 재접속 최소 대기 시간(ms). disconnectTest=true 시 사용. 실제 대기는 이 값 ~ 5배 사이 랜덤
    int         echoTimeoutMs   = 500;           // 에코 미응답 판정 기준 시간(ms). 이 시간 내 응답 없으면 EchoNotRecv 카운터 증가
    int         testDurationSec = 0;             // 테스트 지속 시간(초). 0이면 수동 종료까지 무한 실행
};