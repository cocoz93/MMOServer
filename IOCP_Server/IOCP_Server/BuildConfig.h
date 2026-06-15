#pragma once

// ==========================================================================
// 빌드 토글 모음 (프로젝트 전역 컴파일 스위치)
//
// 주의: #if 로 이 토글을 분기하는 모든 TU에서 "가장 먼저" include 해야 한다.
//       include 순서가 어긋나 정의가 보이지 않으면 매크로는 조용히 0으로
//       평가되어 A/B 빌드가 어긋날 수 있다.
// ==========================================================================

// SendQ 구현 경로 선택
//   1: LockFreeQueue 경로
//   0: 기존 RingBuffer 경로
#define USE_LOCKFREE_SENDQ 0

// Send flush 시점 선택 (USE_LOCKFREE_SENDQ와 직교 — PostSend 공통 경로에 적용)
//   1: 틱 끝에 세션당 1회 flush (송신 coalescing → WSASend 시스콜 절감, WSABUF↑)
//   0: 기존 baseline — RequestSendMsg마다 즉시 PostSend
#define USE_SEND_COALESCING 1

// 커널 송신버퍼 크기 0 실험 (accept 소켓마다 SO_SNDBUF=0)
//   목적: WSASend 호출비용 중 "커널 송신버퍼 복사(③)"가 실제로 미미한지 실측 검증.
//   1: SO_SNDBUF=0 (zero-copy, 송신은 ACK까지 pending) / 0: 기존 OS 기본값
//   주의: 세션당 _sending 직렬화와 겹쳐 flush_send가 호출수 감소로 줄 수 있음 →
//         mmo_wsa_send_calls_total(틱당 호출수)을 반드시 함께 봐야 해석 가능.
#define USE_ZERO_SNDBUF 0
