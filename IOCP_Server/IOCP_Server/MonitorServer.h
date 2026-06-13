// ==========================================================================
// CMonitorServer — Prometheus 메트릭 HTTP 엔드포인트
//
// [책임]
//  - 별도 스레드에서 경량 HTTP 서버 구동
//  - GET /metrics → CMonitorManager 지표를 Prometheus 텍스트 형식으로 노출
//  - IOCP 워커와 간섭 없이 읽기 전용으로 동작
//
// [사용법]
//  CMonitorServer monitorSvr(monitor, 9090);
//  monitorSvr.Start();   // HTTP 스레드 시작
//  monitorSvr.Stop();    // 종료 + join
// ==========================================================================
#pragma once

#include <WinSock2.h>
#include <Windows.h>
#include <thread>
#include <string>
#include <sstream>
#include <iomanip>
#include <memory>
#include <atomic>
#include <iostream>
#include <cstdio>
#include <cstdint>

#include "ThirdParty/httplib.h"
#include "MonitorManager.h"
#include "../../Shared/Common/ErrorLog.h"

class CMonitorServer
{
public:
    explicit CMonitorServer(CMonitorManager& monitor, int port = 9090)
        : _monitor(monitor), _port(port) {}

    ~CMonitorServer() { Stop(); }

    CMonitorServer(const CMonitorServer&) = delete;
    CMonitorServer& operator=(const CMonitorServer&) = delete;

    bool Start()
    {
        _svr = std::make_unique<httplib::Server>();

        _svr->Get("/metrics", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(BuildMetricsText(),
                            "text/plain; version=0.0.4; charset=utf-8");
        });

        _httpThread = std::thread([this]() { HttpThreadFunc(); });
        return true;
    }

    void Stop()
    {
        _stopFlag = true;
        if (_svr) _svr->stop();
        if (_httpThread.joinable()) _httpThread.join();
        _svr.reset();
    }

private:
    void HttpThreadFunc()
    {
        static const int RETRY_INTERVAL_SEC = 5;

        while (!_stopFlag)
        {
            SLOG_INFO("[MonitorServer] Listening on port {}", _port);
            bool ok = _svr->listen("0.0.0.0", _port);

            if (_stopFlag) break;

            if (!ok)
            {
                SLOG_ERROR("[MonitorServer] listen failed on port {}. Retrying in {}s...", _port, RETRY_INTERVAL_SEC);

                for (int i = 0; i < RETRY_INTERVAL_SEC * 10 && !_stopFlag; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        SLOG_INFO("[MonitorServer] Stopped");
    }

    // ══════════════════════════════════════════════════════════════
    // Prometheus exposition format 생성
    // ══════════════════════════════════════════════════════════════

    std::string BuildMetricsText()
    {
        std::ostringstream ss;

        // ── 카운터 ──
        WriteCounter(ss, "mmo_recv_packets_total",
                     "Total received packets", _monitor._recvPackets);
        WriteCounter(ss, "mmo_send_packets_total",
                     "Total sent packets", _monitor._sendPackets);
        WriteCounter(ss, "mmo_recv_bytes_total",
                     "Total received bytes", _monitor._recvBytes);
        WriteCounter(ss, "mmo_send_bytes_total",
                     "Total sent bytes", _monitor._sendBytes);
        WriteCounter(ss, "mmo_session_created_total",
                     "Total sessions created", _monitor._sessionCreated);
        WriteCounter(ss, "mmo_session_destroyed_total",
                     "Total sessions destroyed", _monitor._sessionDestroyed);
        WriteCounter(ss, "mmo_accept_failed_total",
                     "Total accept failures", _monitor._acceptFailed);
        WriteCounter(ss, "mmo_session_timed_out_total",
                     "Total session timeouts", _monitor._sessionTimedOut);
        WriteCounter(ss, "mmo_cheat_detected_total",
                     "Total cheat detections", _monitor._gameLoop._cheatDetected);
        WriteCounter(ss, "mmo_packet_errors_total",
                     "Total packet errors", _monitor._packetErrors);
        WriteCounter(ss, "mmo_send_queue_overflow_total",
                     "Total send queue overflows", _monitor._sendQueueOverflow);
        WriteCounter(ss, "mmo_partial_send_total",
                     "Total partial sends (success but fewer bytes than requested)", _monitor._partialSend);
        WriteCounter(ss, "mmo_recv_buffer_overflow_total",
                     "Total recv buffer overflows", _monitor._recvBufferOverflow);
        WriteCounter(ss, "mmo_zone_change_total",
                     "Total zone changes", _monitor._gameLoop._zoneChangeCount);
        WriteCounter(ss, "mmo_send_contention_total",
                     "Total PostSend contentions (skipped due to sending flag)", _monitor._sendContention);
        WriteCounter(ss, "mmo_wsa_recv_calls_total",
                     "Total WSARecv system calls", _monitor._wsaRecvCalls);
        WriteCounter(ss, "mmo_wsa_send_calls_total",
                     "Total WSASend system calls", _monitor._wsaSendCalls);
        WriteCounter(ss, "mmo_wsa_send_completions_total",
                     "Total WSASend IOCP completions", _monitor._wsaSendCompletions);
        WriteCounter(ss, "mmo_send_enqueued_bytes_total",
                     "Total bytes enqueued to SendQ", _monitor._sendEnqueuedBytes);
        WriteCounter(ss, "mmo_send_discarded_bytes_total",
                     "Total bytes discarded from SendQ on disconnect", _monitor._sendDiscardedBytes);
        WriteCounter(ss, "mmo_broadcast_calls_total",
                     "Total broadcast invocations", _monitor._gameLoop._broadcastCalls);
        WriteCounter(ss, "mmo_broadcast_targets_total",
                     "Total broadcast target players", _monitor._gameLoop._broadcastTargets);
        WriteCounter(ss, "mmo_membership_sends_total",
                     "Uncounted membership-change copies (sector-change/enter/leave); compare vs broadcast_targets",
                     _monitor._gameLoop._membershipSends);

        // ── 구간별 시간 (초 단위 counter) ──
        WritePhaseCounters(ss);

        // ── 게이지 ──
        ss << "# HELP mmo_session_count Current active sessions\n";
        ss << "# TYPE mmo_session_count gauge\n";
        ss << "mmo_session_count " << _monitor._sessionCount << "\n\n";

        ss << "# HELP mmo_event_queue_size Network event queue size before dispatch\n";
        ss << "# TYPE mmo_event_queue_size gauge\n";
        ss << "mmo_event_queue_size " << _monitor._gameLoop._eventQueueSize << "\n\n";

        // ── 히스토그램 (비누적 → 누적 변환) ──
        WriteTickHistogram(ss);
        WriteHandleLatencyHistogram(ss);

        // ── 워커 스레드 카운터 ──
        WriteWorkerCounters(ss);

        // ── 스레드별 CPU 점유율 (외부 관측: 게임루프 동결 사각지대 보강) ──
        WriteThreadCpu(ss);

        return ss.str();
    }

    static void WriteCounter(std::ostringstream& ss,
                              const char* name, const char* help,
                              volatile LONG64& value)
    {
        ss << "# HELP " << name << " " << help << "\n";
        ss << "# TYPE " << name << " counter\n";
        ss << name << " " << value << "\n\n";
    }

    void WritePhaseCounters(std::ostringstream& ss)
    {
        // 마이크로초 → 초 변환하여 counter로 노출
        // rate(mmo_tick_phase_seconds_total) / rate(mmo_tick_duration_seconds_count) → 틱당 평균 구간 시간
        ss << "# HELP mmo_tick_phase_seconds_total Cumulative time spent in each game loop phase\n";
        ss << "# TYPE mmo_tick_phase_seconds_total counter\n";

        ss << std::fixed << std::setprecision(6);
        ss << "mmo_tick_phase_seconds_total{phase=\"network_dispatch\"} "
           << (static_cast<double>(_monitor._gameLoop._phaseNetworkUs) / 1000000.0) << "\n";
        ss << "mmo_tick_phase_seconds_total{phase=\"game_logic\"} "
           << (static_cast<double>(_monitor._gameLoop._phaseGameLogicUs) / 1000000.0) << "\n";
        ss << "mmo_tick_phase_seconds_total{phase=\"broadcast_sync\"} "
           << (static_cast<double>(_monitor._gameLoop._phaseBroadcastSyncUs) / 1000000.0) << "\n";
        ss << std::defaultfloat;
        ss << "\n";

        // 비용종류별 계측 (단계 경계와 무관한 축) — gather/enqueue(복사) vs flush_send(WSASend) 비교용.
        // 복사 vs 송신: rate(enqueue) vs rate(flush_send). USE_LOCKFREE_SENDQ A/B로 enqueue 변화 관찰.
        ss << "# HELP mmo_broadcast_cost_seconds_total Cumulative broadcast cost by type (1-stage: BroadcastAroundSector hot path)\n";
        ss << "# TYPE mmo_broadcast_cost_seconds_total counter\n";
        ss << std::fixed << std::setprecision(6);
        ss << "mmo_broadcast_cost_seconds_total{type=\"gather\"} "
           << (static_cast<double>(_monitor._gameLoop._broadcastGatherUs) / 1000000.0) << "\n";
        ss << "mmo_broadcast_cost_seconds_total{type=\"enqueue\"} "
           << (static_cast<double>(_monitor._gameLoop._broadcastEnqueueUs) / 1000000.0) << "\n";
        ss << "mmo_broadcast_cost_seconds_total{type=\"flush_send\"} "
           << (static_cast<double>(_monitor._gameLoop._flushSendUs) / 1000000.0) << "\n";
        ss << std::defaultfloat;
        ss << "\n";
    }

    void WriteTickHistogram(std::ostringstream& ss)
    {
        // 비누적 버킷 스냅샷
        using GLC = CMonitorManager::GameLoopCounters;

        LONG64 raw[GLC::TICK_BUCKET_COUNT];
        for (int i = 0; i < GLC::TICK_BUCKET_COUNT; ++i)
            raw[i] = _monitor._gameLoop._tickBuckets[i];

        // 누적 변환
        LONG64 cum[GLC::TICK_BUCKET_COUNT];
        cum[0] = raw[0];
        for (int i = 1; i < GLC::TICK_BUCKET_COUNT; ++i)
            cum[i] = cum[i - 1] + raw[i];

        LONG64 tickCount = _monitor._gameLoop._tickCount;
        LONG64 tickSumUs = _monitor._gameLoop._tickSumUs;

        // 버킷 경계 (밀리초 → 초)
        static const char* leBounds[] = {
            "0.001", "0.005", "0.01", "0.02", "0.04",
            "0.06", "0.08", "0.1", "0.2"
        };

        ss << "# HELP mmo_tick_duration_seconds Game loop tick duration\n";
        ss << "# TYPE mmo_tick_duration_seconds histogram\n";

        for (int i = 0; i < GLC::TICK_BUCKET_COUNT - 1; ++i)
        {
            ss << "mmo_tick_duration_seconds_bucket{le=\""
               << leBounds[i] << "\"} " << cum[i] << "\n";
        }
        ss << "mmo_tick_duration_seconds_bucket{le=\"+Inf\"} "
           << cum[GLC::TICK_BUCKET_COUNT - 1] << "\n";

        ss << std::fixed << std::setprecision(6);
        ss << "mmo_tick_duration_seconds_sum "
           << (static_cast<double>(tickSumUs) / 1000000.0) << "\n";
        ss << std::defaultfloat;

        ss << "mmo_tick_duration_seconds_count " << tickCount << "\n\n";
    }

    void WriteHandleLatencyHistogram(std::ostringstream& ss)
    {
        // 서버 handle-latency: recv enqueue → 처리완료(응답 송신) 시간.
        // 클라 mmo_dummy_rtt(왕복)의 분해용 대조군. 비누적 버킷 스냅샷 → 누적 변환.
        using GLC = CMonitorManager::GameLoopCounters;

        LONG64 raw[GLC::HANDLE_BUCKET_COUNT];
        for (int i = 0; i < GLC::HANDLE_BUCKET_COUNT; ++i)
            raw[i] = _monitor._gameLoop._handleBuckets[i];

        LONG64 cum[GLC::HANDLE_BUCKET_COUNT];
        cum[0] = raw[0];
        for (int i = 1; i < GLC::HANDLE_BUCKET_COUNT; ++i)
            cum[i] = cum[i - 1] + raw[i];

        LONG64 handleCount = _monitor._gameLoop._handleCount;
        LONG64 handleSumUs = _monitor._gameLoop._handleSumUs;

        // 버킷 경계 (밀리초 → 초), HANDLE_BUCKET_BOUNDS와 일치
        static const char* leBounds[] = {
            "0.001", "0.005", "0.01", "0.02", "0.04",
            "0.06", "0.08", "0.1", "0.2"
        };

        ss << "# HELP mmo_handle_latency_seconds Server handle latency: recv enqueue to processed (response sent)\n";
        ss << "# TYPE mmo_handle_latency_seconds histogram\n";

        for (int i = 0; i < GLC::HANDLE_BUCKET_COUNT - 1; ++i)
        {
            ss << "mmo_handle_latency_seconds_bucket{le=\""
               << leBounds[i] << "\"} " << cum[i] << "\n";
        }
        ss << "mmo_handle_latency_seconds_bucket{le=\"+Inf\"} "
           << cum[GLC::HANDLE_BUCKET_COUNT - 1] << "\n";

        ss << std::fixed << std::setprecision(6);
        ss << "mmo_handle_latency_seconds_sum "
           << (static_cast<double>(handleSumUs) / 1000000.0) << "\n";
        ss << std::defaultfloat;

        ss << "mmo_handle_latency_seconds_count " << handleCount << "\n\n";
    }

    void WriteWorkerCounters(std::ostringstream& ss)
    {
        LONG workerCount = _monitor._workerThreadCount;
        if (workerCount <= 0) return;

        ss << "# HELP mmo_worker_completions_total IOCP completions per worker\n";
        ss << "# TYPE mmo_worker_completions_total counter\n";

        for (int i = 0; i < workerCount && i < CMonitorManager::MAX_WORKER_THREADS; ++i)
        {
            ss << "mmo_worker_completions_total{worker=\"" << i << "\"} "
               << _monitor._workerCounters[i].completionCount << "\n";
        }
        ss << "\n";
    }

    // ══════════════════════════════════════════════════════════════
    // 스레드별 CPU 점유율 (gauge, 1.0 = 코어 1개 풀)
    //
    // HTTP 스레드(외부 관측자)가 각 스레드 핸들에 GetThreadTimes를 호출.
    // 두 스크레이프 사이의 ΔCPU시간 / Δ벽시계시간 = 그 구간 평균 점유율.
    // 게임루프가 드레인 루프에 갇혀도 외부에서 읽으니 동결되지 않음(진단정리 6 보강).
    // GetThreadTimes/벽시계 모두 100ns 단위 → 무차원 비율.
    // ══════════════════════════════════════════════════════════════
    struct CpuSample
    {
        uint64_t lastCpu100ns = 0;
        uint64_t lastWall100ns = 0;
        bool primed = false;
    };

    static uint64_t FileTimeToU64(const FILETIME& ft)
    {
        return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    void SampleThreadCpu(std::ostringstream& ss, const char* label,
                         HANDLE h, CpuSample& s, uint64_t wallNow)
    {
        if (h == nullptr) return;   // 아직 미등록(예: 에코 모드엔 게임루프 없음)

        FILETIME ftCreate, ftExit, ftKernel, ftUser;
        if (!GetThreadTimes(h, &ftCreate, &ftExit, &ftKernel, &ftUser))
            return;

        uint64_t cpuNow = FileTimeToU64(ftKernel) + FileTimeToU64(ftUser);

        double ratio = 0.0;
        if (s.primed && wallNow > s.lastWall100ns)
        {
            uint64_t dCpu = cpuNow - s.lastCpu100ns;
            uint64_t dWall = wallNow - s.lastWall100ns;
            ratio = static_cast<double>(dCpu) / static_cast<double>(dWall);
        }
        s.lastCpu100ns = cpuNow;
        s.lastWall100ns = wallNow;
        s.primed = true;

        ss << "mmo_thread_cpu_ratio{thread=\"" << label << "\"} " << ratio << "\n";
    }

    void WriteThreadCpu(std::ostringstream& ss)
    {
        FILETIME ftNow;
        GetSystemTimeAsFileTime(&ftNow);   // 100ns 단위 벽시계 (GetThreadTimes와 동일 단위)
        uint64_t wallNow = FileTimeToU64(ftNow);

        ss << "# HELP mmo_thread_cpu_ratio Per-thread CPU utilization (1.0 = one full core)\n";
        ss << "# TYPE mmo_thread_cpu_ratio gauge\n";
        ss << std::fixed << std::setprecision(4);

        // 게임루프 (진단정리 4-🔴 capacity-bound 판정 핵심 지표)
        SampleThreadCpu(ss, "gameloop",
                        _monitor._gameLoopThreadHandle, _cpuGameLoop, wallNow);

        // IOCP 워커 ("게임루프만 타고 워커는 노나" 대조용)
        LONG workerCount = _monitor._workerThreadCount;
        for (int i = 0; i < workerCount && i < CMonitorManager::MAX_WORKER_THREADS; ++i)
        {
            char label[24];
            std::snprintf(label, sizeof(label), "worker-%d", i);
            SampleThreadCpu(ss, label,
                            _monitor._workerCounters[i].threadHandle, _cpuWorker[i], wallNow);
        }

        ss << std::defaultfloat << "\n";
    }

private:
    CMonitorManager& _monitor;
    int _port;
    std::atomic<bool> _stopFlag{false};
    std::unique_ptr<httplib::Server> _svr;
    std::thread _httpThread;

    // CPU 점유율 직전 샘플 상태 (HTTP 스레드 단독 접근 → 락 불필요)
    CpuSample _cpuGameLoop;
    CpuSample _cpuWorker[CMonitorManager::MAX_WORKER_THREADS];
};
