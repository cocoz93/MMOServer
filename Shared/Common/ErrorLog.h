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
    // Per-IO방식 사용 시 나올수있는 에러
    case 10004: // WSAEINTR : 사용자쪽에서 진행중인 I/O를 제거 (closesocket / CancelIO)
    case 10038: // WSAENOTSOCK: 유효하지 않은 소켓 핸들 (이미 close되었거나 잘못된 값)

    case 10053: // WSAECONNABORTED: 소프트웨어적으로 연결 중단 (타임아웃, 프로토콜 오류 등)
    case 10054: // WSAECONNRESET: 상대방이 연결을 강제로 끊음 (RST 수신)
    case 10064: // WSAEHOSTDOWN: 상대 호스트가 다운됨
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
