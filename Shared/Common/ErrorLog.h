#pragma once

#include <iostream>
#include <sstream>
#include <string>

namespace shared
{
inline bool ShouldIgnoreWsaError(int errorCode)
{
    switch (errorCode)
    {
    case 10054: // WSAECONNRESET: 상대의 정상 종료/강제 종료에서 자주 발생
        return true;
    default:
        return false;
    }
}

inline void LogError(const std::string& message)
{
    std::cerr << message << std::endl;
}

inline void LogError(const std::wstring& message)
{
    std::wcerr << message << std::endl;
}
}

#define LOG_ERROR_STREAM(expr) \
    do \
    { \
        std::ostringstream _logErrorOss; \
        _logErrorOss << expr; \
        ::shared::LogError(_logErrorOss.str()); \
    } while (0)

#define WLOG_ERROR_STREAM(expr) \
    do \
    { \
        std::wostringstream _logErrorOss; \
        _logErrorOss << expr; \
        ::shared::LogError(_logErrorOss.str()); \
    } while (0)

#define LOG_WSA_ERROR_STREAM(expr, errCodeExpr) \
    do \
    { \
        const int _wsaErr = (errCodeExpr); \
        if (!::shared::ShouldIgnoreWsaError(_wsaErr)) \
        { \
            std::ostringstream _logErrorOss; \
            _logErrorOss << expr << _wsaErr; \
            ::shared::LogError(_logErrorOss.str()); \
        } \
    } while (0)

#define WLOG_WSA_ERROR_STREAM(expr, errCodeExpr) \
    do \
    { \
        const int _wsaErr = (errCodeExpr); \
        if (!::shared::ShouldIgnoreWsaError(_wsaErr)) \
        { \
            std::wostringstream _logErrorOss; \
            _logErrorOss << expr << _wsaErr; \
            ::shared::LogError(_logErrorOss.str()); \
        } \
    } while (0)
