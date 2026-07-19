#pragma once
// ==========================================================================
// NetIoModel — 네트워크 I/O 모델 (플랫폼이 결정)
//
//   게임로직은 이 별칭(NetIoModel)만 알고, 실제 구현은 OS가 고른다.
//     Windows -> IOCP   /   Linux -> epoll (Phase 2, 아직 없음)
//   지금 도는 모델은 서버 부팅 로그 "Network I/O model: ..."로 바로 확인.
// ==========================================================================
#ifdef _WIN32
    #include "IOCPServer.h"
    using NetIoModel = CIOCPServer;
    inline constexpr const char* kNetIoModelName = "IOCP";
#else
    #include "EpollServer.h"      // (Phase 2) 리눅스 백엔드 — 아직 없음
    using NetIoModel = CEpollServer;
    inline constexpr const char* kNetIoModelName = "epoll";
#endif