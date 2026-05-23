#include "Logger.h"

#ifdef USE_SPDLOG_LOGGER

#include <Windows.h>

namespace shared
{

std::shared_ptr<spdlog::logger> Logger::_logger;

// 실행 파일 이름에서 프로세스명 추출 (확장자 제외)
static std::string GetProcessName()
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string exePath(path);
    auto pos = exePath.find_last_of("\\/");
    std::string fileName = (pos != std::string::npos) ? exePath.substr(pos + 1) : exePath;
    auto dotPos = fileName.find_last_of('.');
    if (dotPos != std::string::npos)
        fileName = fileName.substr(0, dotPos);
    return fileName;
}

// 현재 날짜를 YYMMDD 형식으로 반환
static std::string GetDateString()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm localTm;
    localtime_s(&localTm, &time);
    char buf[16];
    strftime(buf, sizeof(buf), "%y%m%d", &localTm);
    return buf;
}

void Logger::Init(const std::string& logDir)
{
    // 로그 파일 경로 생성: {logDir}/{날짜}_{프로세스명}.log
    std::string logFileName = logDir + "/" + GetDateString() + "_" + GetProcessName() + ".log";
    // 예: logs/260522_IOCP_Server.log

    // 비동기 스레드풀: 큐 8192, 워커 1
    spdlog::init_thread_pool(8192, 1);

    // Release=info, Debug=debug
#ifdef _DEBUG
    const auto logLevel = spdlog::level::debug;
#else
    const auto logLevel = spdlog::level::info;
#endif

    // 콘솔 sink (색상 지원)
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(logLevel);

    // 파일 sink (rotating: 50MB × 5개)
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logFileName, 50 * 1024 * 1024, 5);
    fileSink->set_level(logLevel);

    // 멀티싱크 비동기 로거 생성 (큐 만료 시 가장 오래된 메시지 폐기 — 게임루프 블로킹 방지)
    std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
    _logger = std::make_shared<spdlog::async_logger>(
        "server",
        sinks.begin(), sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::overrun_oldest);

    _logger->set_level(logLevel);
    _logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");

    spdlog::register_logger(_logger);
    spdlog::set_default_logger(_logger);

    _logger->info("Logger initialized");
}

void Logger::Shutdown()
{
    if (_logger)
    {
        _logger->info("Logger shutting down");
        _logger->flush();
        spdlog::shutdown();
        _logger.reset();
    }
}

std::shared_ptr<spdlog::logger>& Logger::Get()
{
    return _logger;
}

} // namespace shared

#endif // USE_SPDLOG_LOGGER
