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

#include "ThirdParty/httplib.h"
#include "MonitorManager.h"

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
            std::cout << "[MonitorServer] Listening on port " << _port << std::endl;
            bool ok = _svr->listen("0.0.0.0", _port);

            if (_stopFlag) break;

            if (!ok)
            {
                std::cerr << "[MonitorServer] listen failed on port " << _port
                          << ". Retrying in " << RETRY_INTERVAL_SEC << "s..." << std::endl;

                for (int i = 0; i < RETRY_INTERVAL_SEC * 10 && !_stopFlag; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        std::cout << "[MonitorServer] Stopped" << std::endl;
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
                     "Total cheat detections", _monitor._cheatDetected);
        WriteCounter(ss, "mmo_packet_errors_total",
                     "Total packet errors", _monitor._packetErrors);
        WriteCounter(ss, "mmo_send_queue_overflow_total",
                     "Total send queue overflows", _monitor._sendQueueOverflow);
        WriteCounter(ss, "mmo_recv_buffer_overflow_total",
                     "Total recv buffer overflows", _monitor._recvBufferOverflow);
        WriteCounter(ss, "mmo_zone_change_total",
                     "Total zone changes", _monitor._zoneChangeCount);
        WriteCounter(ss, "mmo_send_contention_total",
                     "Total PostSend contentions (skipped due to sending flag)", _monitor._sendContention);
        WriteCounter(ss, "mmo_wsa_recv_calls_total",
                     "Total WSARecv system calls", _monitor._wsaRecvCalls);
        WriteCounter(ss, "mmo_wsa_send_calls_total",
                     "Total WSASend system calls", _monitor._wsaSendCalls);
        WriteCounter(ss, "mmo_send_enqueued_bytes_total",
                     "Total bytes enqueued to SendQ", _monitor._sendEnqueuedBytes);

        // ── 게이지 ──
        ss << "# HELP mmo_session_count Current active sessions\n";
        ss << "# TYPE mmo_session_count gauge\n";
        ss << "mmo_session_count " << _monitor._sessionCount << "\n\n";

        // ── 히스토그램 (비누적 → 누적 변환) ──
        WriteTickHistogram(ss);

        // ── 워커 스레드 카운터 ──
        WriteWorkerCounters(ss);

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

    void WriteTickHistogram(std::ostringstream& ss)
    {
        // 비누적 버킷 스냅샷
        LONG64 raw[CMonitorManager::TICK_BUCKET_COUNT];
        for (int i = 0; i < CMonitorManager::TICK_BUCKET_COUNT; ++i)
            raw[i] = _monitor._tickBuckets[i];

        // 누적 변환
        LONG64 cum[CMonitorManager::TICK_BUCKET_COUNT];
        cum[0] = raw[0];
        for (int i = 1; i < CMonitorManager::TICK_BUCKET_COUNT; ++i)
            cum[i] = cum[i - 1] + raw[i];

        LONG64 tickCount = _monitor._tickCount;
        LONG64 tickSumUs = _monitor._tickSumUs;

        // 버킷 경계 (밀리초 → 초)
        static const char* leBounds[] = {
            "0.001", "0.005", "0.01", "0.02", "0.04",
            "0.06", "0.08", "0.1", "0.2"
        };

        ss << "# HELP mmo_tick_duration_seconds Game loop tick duration\n";
        ss << "# TYPE mmo_tick_duration_seconds histogram\n";

        for (int i = 0; i < CMonitorManager::TICK_BUCKET_COUNT - 1; ++i)
        {
            ss << "mmo_tick_duration_seconds_bucket{le=\""
               << leBounds[i] << "\"} " << cum[i] << "\n";
        }
        ss << "mmo_tick_duration_seconds_bucket{le=\"+Inf\"} "
           << cum[CMonitorManager::TICK_BUCKET_COUNT - 1] << "\n";

        ss << std::fixed << std::setprecision(6);
        ss << "mmo_tick_duration_seconds_sum "
           << (static_cast<double>(tickSumUs) / 1000000.0) << "\n";
        ss << std::defaultfloat;

        ss << "mmo_tick_duration_seconds_count " << tickCount << "\n\n";
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

private:
    CMonitorManager& _monitor;
    int _port;
    std::atomic<bool> _stopFlag{false};
    std::unique_ptr<httplib::Server> _svr;
    std::thread _httpThread;
};
