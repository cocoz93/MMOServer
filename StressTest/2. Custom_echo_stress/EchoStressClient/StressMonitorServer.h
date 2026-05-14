// ==========================================================================
// StressMonitorServer — Prometheus 메트릭 HTTP 엔드포인트 (스트레스 클라이언트)
//
// [책임]
//  - 별도 스레드에서 경량 HTTP 서버 구동
//  - GET /metrics → Stats 지표를 Prometheus 텍스트 형식으로 노출
//
// [사용법]
//  StressMonitorServer monitorSvr(stats, 9092);
//  monitorSvr.Start();
//  monitorSvr.Stop();
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
#include <cstdio>
#include <climits>

#include "../../../MMOServer/ThirdParty/httplib.h"
#include "Stats.h"

class StressMonitorServer
{
public:
    explicit StressMonitorServer(const Stats& stats, int port = 9092)
        : _stats(stats), _port(port) {}

    ~StressMonitorServer() { Stop(); }

    StressMonitorServer(const StressMonitorServer&) = delete;
    StressMonitorServer& operator=(const StressMonitorServer&) = delete;

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
            wprintf(L"[StressMonitor] Listening on port %d\n", _port);
            bool ok = _svr->listen("0.0.0.0", _port);

            if (_stopFlag) break;

            if (!ok)
            {
                wprintf(L"[StressMonitor] listen failed on port %d. Retrying in %ds...\n",
                        _port, RETRY_INTERVAL_SEC);

                for (int i = 0; i < RETRY_INTERVAL_SEC * 10 && !_stopFlag; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        wprintf(L"[StressMonitor] Stopped\n");
    }

    // ══════════════════════════════════════════════════════════════
    // Prometheus exposition format 생성
    // ══════════════════════════════════════════════════════════════

    std::string BuildMetricsText()
    {
        std::ostringstream ss;

        // ── 카운터 ──
        WriteCounter(ss, "stress_send_packets_total",
                     "Total sent packets", _stats.sendCount);
        WriteCounter(ss, "stress_recv_packets_total",
                     "Total received packets", _stats.recvCount);
        WriteCounter(ss, "stress_connect_total",
                     "Total connection attempts", _stats.connectTotal);
        WriteCounter(ss, "stress_connect_fail_total",
                     "Total connection failures", _stats.connectFail);
        WriteCounter(ss, "stress_disconnect_from_server_total",
                     "Total server-initiated disconnects", _stats.disconnectFromServer);
        WriteCounter(ss, "stress_echo_not_recv_total",
                     "Total echo timeout (no response)", _stats.echoNotRecv);
        WriteCounter(ss, "stress_packet_error_total",
                     "Total packet errors", _stats.packetError);
        WriteCounter(ss, "stress_late_arrival_total",
                     "Total late arrivals", _stats.lateArrival);

        // ── 게이지 ──
        ss << "# HELP stress_connected_clients Current connected clients\n";
        ss << "# TYPE stress_connected_clients gauge\n";
        ss << "stress_connected_clients " << _stats.connectedCount.load() << "\n\n";

        // ── RTT 히스토그램 ──
        WriteRttHistogram(ss);

        return ss.str();
    }

    static void WriteCounter(std::ostringstream& ss,
                              const char* name, const char* help,
                              const std::atomic<int64_t>& value)
    {
        ss << "# HELP " << name << " " << help << "\n";
        ss << "# TYPE " << name << " counter\n";
        ss << name << " " << value.load() << "\n\n";
    }

    void WriteRttHistogram(std::ostringstream& ss)
    {
        // 비누적 버킷 스냅샷
        int64_t raw[Stats::RTT_BUCKET_COUNT];
        for (int i = 0; i < Stats::RTT_BUCKET_COUNT; ++i)
            raw[i] = _stats.rttBuckets[i].load();

        // 누적 변환
        int64_t cum[Stats::RTT_BUCKET_COUNT];
        cum[0] = raw[0];
        for (int i = 1; i < Stats::RTT_BUCKET_COUNT; ++i)
            cum[i] = cum[i - 1] + raw[i];

        int64_t rttCount = _stats.rttSamples.load();
        int64_t rttSumMs = _stats.rttSumMs.load();

        // 버킷 경계 (밀리초 → 초)
        static const char* leBounds[] = {
            "0.001", "0.005", "0.01", "0.02", "0.05",
            "0.1", "0.2", "0.5", "1.0"
        };

        ss << "# HELP stress_rtt_seconds Echo round-trip time\n";
        ss << "# TYPE stress_rtt_seconds histogram\n";

        for (int i = 0; i < Stats::RTT_BUCKET_COUNT - 1; ++i)
        {
            ss << "stress_rtt_seconds_bucket{le=\""
               << leBounds[i] << "\"} " << cum[i] << "\n";
        }
        ss << "stress_rtt_seconds_bucket{le=\"+Inf\"} "
           << cum[Stats::RTT_BUCKET_COUNT - 1] << "\n";

        ss << std::fixed << std::setprecision(6);
        ss << "stress_rtt_seconds_sum "
           << (static_cast<double>(rttSumMs) / 1000.0) << "\n";
        ss << std::defaultfloat;

        ss << "stress_rtt_seconds_count " << rttCount << "\n\n";
    }

private:
    const Stats& _stats;
    int _port;
    std::atomic<bool> _stopFlag{false};
    std::unique_ptr<httplib::Server> _svr;
    std::thread _httpThread;
};
