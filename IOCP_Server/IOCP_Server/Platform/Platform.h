#pragma once
// ==========================================================================
// Platform — OS별로 갈리는 저수준 동작을 한곳에 격리하는 경계(seam)
//
//   게임·네트워크 로직은 이 헤더의 Platform::* 함수만 호출하고,
//   실제 구현은 OS가 #ifdef로 고른다. (Windows / Linux)
//   목표: windows.h 의존을 이 경계 뒤로 몰아, 나머지 코드가 플랫폼 중립이 되게.
//
//   [현재 입주자]
//     SetHighResolutionTimer — 시스템 타이머 해상도 상향 (Sleep/틱 정밀도).
//                              Windows=timeBeginPeriod / Linux=불필요(no-op).
//     GetExecutableDir       — 실행 파일 디렉토리 (설정 파일 경로 등).
//                              Windows=GetModuleFileNameA / Linux=/proc/self/exe.
//     SetProcessAffinity     — 프로세스 CPU 코어 고정. Win=SetProcessAffinityMask / Linux=sched_setaffinity.
//     GetAvailableCoreCount  — affinity 가용 코어 수(워커 자동 산정). Win=GetProcessAffinityMask / Linux=sched_getaffinity.
//     InstallShutdownHandler — Ctrl+C/SIGTERM → 콜백. Win=SetConsoleCtrlHandler / Linux=sigaction+폴링.
//     ThreadCpuHandle / CaptureCurrentThreadCpu / GetThreadCpuTimeNs
//                            — 스레드 CPU 시간(모니터링). Win=GetThreadTimes / Linux=clock_gettime(per-thread).
//
//   NOTE: 지금은 헤더 전용(inline). 입주자가 늘고 windows.h 격리가 중요해지면
//         선언/구현을 Platform.cpp로 분리해 windows.h를 단일 TU에 가둔다.
// ==========================================================================

#include <string>
#include <cstdint>
#include <thread>

#ifdef _WIN32
    #include <Windows.h>
    #pragma comment(lib, "winmm.lib")   // timeBeginPeriod / timeEndPeriod
#else
    #include <unistd.h>                  // readlink (/proc/self/exe)
    #include <sched.h>                   // sched_setaffinity / CPU_SET
    #include <csignal>                   // sigaction (종료 시그널)
    #include <atomic>                    // 종료 플래그
    #include <chrono>                    // 폴링 슬립
    #include <pthread.h>                 // pthread_getcpuclockid (스레드 CPU clock)
    #include <time.h>                    // clock_gettime / clockid_t
#endif

namespace Platform
{
    // 시스템 타이머 해상도를 1ms로 올리거나(enable=true) 되돌린다(false).
    //   Windows: timeBeginPeriod/timeEndPeriod(1) — Sleep·대기 정밀도 ~15ms → 1ms.
    //   Linux  : nanosleep 계열이 이미 고해상도라 불필요 → no-op.
    //   enable/disable는 반드시 쌍으로 호출 (중첩 카운트는 OS가 관리).
    inline void SetHighResolutionTimer(bool enable)
    {
#ifdef _WIN32
        if (enable)
            timeBeginPeriod(1);
        else
            timeEndPeriod(1);
#else
        (void)enable;
#endif
    }

    // 실행 파일이 위치한 디렉토리 (끝에 경로 구분자 포함). 실패 시 "".
    //   Windows: GetModuleFileNameA (CP_ACP narrow — ifstream이 여는 인코딩과 일치)
    //   Linux  : readlink("/proc/self/exe")
    inline std::string GetExecutableDir()
    {
#ifdef _WIN32
        char buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return std::string();
        std::string path(buf, n);
#else
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0)
            return std::string();
        std::string path(buf, static_cast<size_t>(n));
#endif
        size_t pos = path.find_last_of("/\\");
        if (pos == std::string::npos)
            return std::string();
        return path.substr(0, pos + 1);   // 구분자 포함
    }

    // 프로세스를 지정한 논리코어 비트마스크에 고정. 성공 시 true.
    //   Windows: SetProcessAffinityMask / Linux: sched_setaffinity(CPU_SET)
    inline bool SetProcessAffinity(uint64_t mask)
    {
#ifdef _WIN32
        return SetProcessAffinityMask(GetCurrentProcess(),
                                      static_cast<DWORD_PTR>(mask)) != 0;
#else
        cpu_set_t set;
        CPU_ZERO(&set);
        for (int i = 0; i < 64; ++i)
            if (mask & (1ull << i))
                CPU_SET(i, &set);
        return sched_setaffinity(0, sizeof(set), &set) == 0;
#endif
    }

    // affinity로 제한된 가용 논리코어 수. 미제한/불명이면 하드웨어 논리코어 수로 폴백.
    //   (worker 스레드 자동 산정용 — affinity 설정 이후 호출해야 정확)
    inline int GetAvailableCoreCount()
    {
#ifdef _WIN32
        DWORD_PTR procMask = 0, sysMask = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask) && procMask != 0)
        {
            int count = 0;
            for (DWORD_PTR m = procMask; m != 0; m &= (m - 1))
                ++count;
            if (count > 0)
                return count;
        }
#else
        cpu_set_t set;
        CPU_ZERO(&set);
        if (sched_getaffinity(0, sizeof(set), &set) == 0)
        {
            int count = CPU_COUNT(&set);
            if (count > 0)
                return count;
        }
#endif
        return static_cast<int>(std::thread::hardware_concurrency());
    }

    // 종료 시그널(Ctrl+C / SIGINT·SIGTERM) 수신 시 cb를 "정상 스레드 컨텍스트"에서 1회 호출.
    //   Windows: SetConsoleCtrlHandler (핸들러가 OS 주입 스레드에서 실행 → cb의 mutex/cv 안전)
    //   Linux  : sigaction으로 플래그만 세팅(async-signal-safe) + 폴링 스레드가 cb 호출.
    //            시그널 컨텍스트에서 cb 직접 호출 금지 — cb 내부 mutex/cv는 비동기시그널 안전이 아님.
    //   NOTE: Linux 경로는 리눅스 빌드 환경 전이라 미검증.
    namespace detail
    {
        inline void (*g_shutdownCb)() = nullptr;
#ifdef _WIN32
        inline BOOL WINAPI ConsoleCtrlProxy(DWORD ctrlType)
        {
            switch (ctrlType)
            {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
            case CTRL_CLOSE_EVENT:
            case CTRL_SHUTDOWN_EVENT:
            case CTRL_LOGOFF_EVENT:
                if (g_shutdownCb) g_shutdownCb();
                return TRUE;
            default:
                return FALSE;
            }
        }
#else
        inline std::atomic<bool> g_shutdownFlag{ false };
        inline void SignalSetFlag(int) { g_shutdownFlag.store(true, std::memory_order_relaxed); }
#endif
    }

    inline void InstallShutdownHandler(void (*cb)())
    {
        detail::g_shutdownCb = cb;
#ifdef _WIN32
        SetConsoleCtrlHandler(detail::ConsoleCtrlProxy, TRUE);
#else
        struct sigaction sa {};
        sa.sa_handler = detail::SignalSetFlag;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT,  &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
        // 폴링 스레드: 플래그가 서면 정상 컨텍스트에서 cb 호출 (cv.notify 안전)
        std::thread([]
        {
            while (!detail::g_shutdownFlag.load(std::memory_order_relaxed))
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (detail::g_shutdownCb) detail::g_shutdownCb();
        }).detach();
#endif
    }

    // ── 스레드 CPU 시간 측정 (모니터링) ──
    //   각 스레드가 시작 시 CaptureCurrentThreadCpu로 자기 핸들을 등록하고,
    //   외부 관측 스레드가 GetThreadCpuTimeNs로 그 스레드의 누적 CPU 시간(ns)을 읽는다.
    //   Windows: 스레드 실핸들(DuplicateHandle) + GetThreadTimes
    //   Linux  : per-thread CPU clock(pthread_getcpuclockid) + clock_gettime
#ifdef _WIN32
    using ThreadCpuHandle = HANDLE;
    inline constexpr ThreadCpuHandle kInvalidThreadCpuHandle = nullptr;
#else
    using ThreadCpuHandle = clockid_t;
    inline constexpr ThreadCpuHandle kInvalidThreadCpuHandle = static_cast<clockid_t>(-1);
#endif

    // 호출 스레드의 CPU 시간 측정 핸들을 캡처. 실패 시 kInvalidThreadCpuHandle.
    //   수명은 프로세스 종료까지 — Windows 복제 핸들은 명시적 Close 생략(진단용, 기존 동작 보존).
    inline ThreadCpuHandle CaptureCurrentThreadCpu()
    {
#ifdef _WIN32
        HANDLE dup = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                            GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS))
            return dup;
        return nullptr;
#else
        clockid_t cid;
        if (pthread_getcpuclockid(pthread_self(), &cid) == 0)
            return cid;
        return static_cast<clockid_t>(-1);
#endif
    }

    // 핸들이 가리키는 스레드의 누적 CPU 시간(ns, 커널+유저)을 out에 넣고 true. 실패/미등록이면 false.
    inline bool GetThreadCpuTimeNs(ThreadCpuHandle h, uint64_t& outNs)
    {
#ifdef _WIN32
        if (h == nullptr)
            return false;
        FILETIME ftCreate, ftExit, ftKernel, ftUser;
        if (!GetThreadTimes(h, &ftCreate, &ftExit, &ftKernel, &ftUser))
            return false;
        auto u64 = [](const FILETIME& ft) {
            return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        };
        outNs = (u64(ftKernel) + u64(ftUser)) * 100;   // FILETIME 100ns 단위 → ns
        return true;
#else
        if (h == static_cast<clockid_t>(-1))
            return false;
        struct timespec ts;
        if (clock_gettime(h, &ts) != 0)
            return false;
        outNs = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull
              + static_cast<uint64_t>(ts.tv_nsec);
        return true;
#endif
    }
}
