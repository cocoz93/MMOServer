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
//   0: 기존 RingBuffer 경로 [권장] 
#define USE_LOCKFREE_SENDQ 0

// Send flush 시점 선택 (USE_LOCKFREE_SENDQ와 직교 — PostSend 공통 경로에 적용)
//   0: 기존 baseline — RequestSendMsg마다 즉시 PostSend
//   1: 틱 끝에 세션당 1회 flush (송신 coalescing → WSASend 시스콜 절감, WSABUF↑) [권장]
#define USE_SEND_COALESCING 1

// 커널 송신버퍼 크기 0 실험 (accept 소켓마다 SO_SNDBUF=0)
//   목적: WSASend 호출비용 중 "커널 송신버퍼 복사(③)"가 실제로 미미한지 실측 검증.
//   0: 기존 OS 기본값 [권장]
//   1: SO_SNDBUF=0 (zero-copy, 송신은 ACK까지 pending) 
//   주의: 세션당 _sending 직렬화와 겹쳐 flush_send가 호출수 감소로 줄 수 있음 →
//         mmo_wsa_send_calls_total(틱당 호출수)을 반드시 함께 봐야 해석 가능.
#define USE_ZERO_SNDBUF 0

// 송신 스레드 분리 실험 — A1: 단일 send 스레드 (offload 효과 격리)
//   목적: 틱의 74%(flush_send=WSASend 루프)를 게임루프 임계경로에서 떼어내
//         "오프로딩만으로 틱 천장이 오르나"를 격리 검증.
//   1: 게임루프는 FlushPendingSends에서 dirty 배치(sessionId)만 send 스레드에 넘기고,
//      전용 send 스레드 1개가 FindSession→PostSend로 WSASend 수행 (게임루프와 파이프라인)
//   0: 기존 — 게임루프가 직접 PostSend 루프 (baseline)

//   주의: tick p99 하락만 보지 말고 send 스레드 CPU·완료율(wsa_send_completions)·
//         _flushQueue 백로그를 함께 봐야 천장 이동 지점이 보임.
#define USE_SEND_THREAD 1

//   의존: USE_SEND_COALESCING=1 필요 (_dirtySessions 배치를 재사용). =0이면 dirty 배치가
//         안 쌓여 무의미하므로 함께 켤 것.
#if USE_SEND_THREAD && !USE_SEND_COALESCING
	#error "USE_SEND_THREAD requires USE_SEND_COALESCING=1 (dirty 배치 핸드오프 의존)"
#endif

// 섹터 단위 묶음 패킷 실험 — 이동/sync를 이벤트마다 즉시 브로드캐스트하지 않고,
// 틱 끝에 "섹터별 최종 상태(위치·방향·이동상태)"를 한 패킷(S2C_SECTOR_UPDATES)으로 묶어 그 섹터 주변 9섹터에만 전달.
// 목적: 수신자별 복사 횟수를 이벤트 수와 무관하게 (플레이어 × AOI섹터)로 상한 고정.
//   1: dirty 마킹 → 틱 끝 FlushSectorUpdates로 섹터 묶음 송신 [실험]
//   0: 기존 — 이벤트마다 즉시 BroadcastAroundSector (baseline)
//   주의: 틱 내 start→stop 전이 "연출"은 최종상태만 남아 손실(위치 정합은 유지).
#define USE_SECTOR_AGGREGATION 1

// DB 저장 파이프라인 실험 — dirty flag 기반 비동기 위치 저장 (존 서버 관점)
//   서버 메모리 = 진실, DB = 저장소. 바뀐 플레이어만 주기적으로 전용 워커 스레드가 MySQL에 UPSERT.
//   목적: 전량 저장 부담을 dirty flag로 줄이고, 저장 I/O를 게임 틱 임계경로에서 떼어낸다.
//   1: DB 워커 활성 (MySQL 필요, 지표 mmo_db_*) [실험]
//   0: DB 없이 기존 동작 그대로 (회귀 기준선) [기본]
#define USE_DB_WORKER 1
