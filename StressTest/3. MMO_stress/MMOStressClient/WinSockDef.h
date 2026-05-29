#pragma once

// select()에서 8192개 소켓까지 처리 가능하도록 FD_SETSIZE 재정의
// 반드시 WinSock2.h include 전에 위치해야 함
// 모든 소스 파일은 WinSock2.h 직접 include 대신 이 헤더를 사용할 것
#ifndef FD_SETSIZE
#define FD_SETSIZE 8192
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>
