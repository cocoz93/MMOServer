#pragma once
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <cstdint>
#include <cstdio>
#include <ctime>

// ─────────────────────────────────────────────────────────────────
// 무결성 위반 기록기
//
// 패딩 훼손(payload 바이트 어긋남)과 에코 순서 역전은 부하가 만들어내는 현상이
// 아니라 네트워크 라이브러리의 버그다. 카운터만 올리면 "몇 건 났다"만 남고
// 어느 세션의 몇 바이트가 어떻게 틀렸는지 추적할 단서가 사라지므로,
// 최초 MAX_ENTRIES건은 원본 바이트를 파일로 덤프한다.
//
// FailFast가 켜져 있으면 첫 위반에서 종료 플래그를 세운다.
//   - abort()를 쓰지 않는 이유: main의 정상 종료 경로(모니터/스레드 정지 →
//     최종 리포트 저장)를 그대로 타야 증거가 남는다. 즉사시키면 그게 날아간다.
//   - 계속 돌려봐야 링버퍼가 덮이면서 증거만 사라지고, 위반 이후의 RTT/TPS는
//     이미 신뢰할 수 없는 숫자다.
// ─────────────────────────────────────────────────────────────────
namespace Integrity
{
    inline constexpr int MAX_ENTRIES = 8;    // 파일 덤프 상한 (폭주 방지)
    inline constexpr int DUMP_BYTES  = 16;   // 불일치 지점부터 덤프할 바이트 수

    inline std::atomic<bool> g_failed  { false };
    inline std::atomic<bool> g_failFast{ true  };
    inline std::atomic<int>  g_entries { 0 };

    inline std::mutex g_mutex;                    // 아래 두 개를 보호
    inline FILE*      g_file = nullptr;
    inline wchar_t    g_path[MAX_PATH] = {};

    inline void Init(bool failFast)
    {
        g_failFast.store(failFast, std::memory_order_relaxed);
    }

    inline bool HasFailed()   { return g_failed.load(std::memory_order_acquire); }
    inline int  EntryCount()  { return g_entries.load(std::memory_order_relaxed); }
    inline const wchar_t* LogPath() { return g_path; }

    // ── 이하 Locked 접미사는 g_mutex를 잡은 상태에서만 호출 ──

    inline bool OpenLocked()
    {
        if (g_file) return true;

        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring dir(exePath);
        dir = dir.substr(0, dir.find_last_of(L"\\/") + 1) + L"Results";
        CreateDirectoryW(dir.c_str(), NULL);

        time_t now = time(nullptr);
        struct tm lt;
        localtime_s(&lt, &now);

        swprintf_s(g_path, _countof(g_path),
            L"%s\\Integrity_%04d%02d%02d_%02d%02d%02d.log",
            dir.c_str(),
            lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
            lt.tm_hour, lt.tm_min, lt.tm_sec);

        _wfopen_s(&g_file, g_path, L"w, ccs=UTF-8");
        return g_file != nullptr;
    }

    inline void WriteStampLocked(int seq, const wchar_t* kind)
    {
        time_t now = time(nullptr);
        struct tm lt;
        localtime_s(&lt, &now);
        fwprintf(g_file,
            L"[#%d] %02d:%02d:%02d  %s\n",
            seq, lt.tm_hour, lt.tm_min, lt.tm_sec, kind);
    }

    inline void DumpHexLocked(const wchar_t* label, const char* p, size_t len)
    {
        fwprintf(g_file, L"    %s :", label);
        for (size_t i = 0; i < len; ++i)
            fwprintf(g_file, L" %02X", static_cast<unsigned char>(p[i]));
        fwprintf(g_file, L"\n");
    }

    // 패딩 불일치 — 링버퍼 랩어라운드/코얼레싱/부분전송 경계 결함의 신호
    //   카운터 증가는 호출자(ThreadStats) 몫. 여기서는 증거 기록과 종료 플래그만 다룬다.
    inline void ReportPadding(int clientIndex, uint64_t echoValue, int packetSize,
                              const char* actual, const char* expected, size_t padLen)
    {
        const int seq = g_entries.fetch_add(1, std::memory_order_relaxed) + 1;

        if (seq <= MAX_ENTRIES)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (OpenLocked())
            {
                // 첫 어긋난 바이트 위치 — 랩 경계인지 오프셋 밀림인지 판단할 단서
                size_t off = 0;
                while (off < padLen && actual[off] == expected[off]) ++off;
                const size_t dumpLen = (padLen - off < DUMP_BYTES) ? (padLen - off) : DUMP_BYTES;

                WriteStampLocked(seq, L"PADDING MISMATCH");
                fwprintf(g_file,
                    L"    client=#%d  packetSize=%dB  echoValue=%llu  padLen=%zuB\n"
                    L"    first mismatch at padding offset %zu (packet offset %zu)\n",
                    clientIndex, packetSize, static_cast<unsigned long long>(echoValue), padLen,
                    off, off + 12);
                DumpHexLocked(L"expected", expected + off, dumpLen);
                DumpHexLocked(L"actual  ", actual   + off, dumpLen);
                fwprintf(g_file, L"\n");
                fflush(g_file);
            }
        }

        if (g_failFast.load(std::memory_order_relaxed))
            g_failed.store(true, std::memory_order_release);
    }

    // 에코 순서 역전 — 받아야 할 값을 건너뛰고 더 큰 값이 왔다 (유실/뒤섞임)
    inline void ReportOrder(int clientIndex, uint64_t expected, uint64_t received)
    {
        const int seq = g_entries.fetch_add(1, std::memory_order_relaxed) + 1;

        if (seq <= MAX_ENTRIES)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (OpenLocked())
            {
                WriteStampLocked(seq, L"ORDER VIOLATION");
                fwprintf(g_file,
                    L"    client=#%d  expected=%llu  received=%llu  (skipped %llu)\n\n",
                    clientIndex,
                    static_cast<unsigned long long>(expected),
                    static_cast<unsigned long long>(received),
                    static_cast<unsigned long long>(received - expected));
                fflush(g_file);
            }
        }

        if (g_failFast.load(std::memory_order_relaxed))
            g_failed.store(true, std::memory_order_release);
    }

    inline void Close()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file)
        {
            fclose(g_file);
            g_file = nullptr;
        }
    }
}
