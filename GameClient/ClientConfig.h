#pragma once

// ==========================================================================
// ClientConfig — INI 파일 기반 클라이언트 설정 로더
//
// [사용법]
//  ClientConfig config;
//  config.Load();  // 실행 파일 옆의 ClientConfig.ini 로드
//
// [INI 포맷]
//  [Server]
//  IP=127.0.0.1
//  Port=6000
// ==========================================================================

#include <string>
#include <iostream>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

struct ClientConfig
{
    std::string serverIp  = "127.0.0.1";
    int         serverPort = 6000;

    // 실행 파일 경로 기준으로 ClientConfig.ini 로드
    bool Load()
    {
        // 실행 파일 디렉토리에서 INI 경로 구성
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);

        std::wstring iniPath(exePath);
        size_t pos = iniPath.find_last_of(L"\\/");
        iniPath = iniPath.substr(0, pos + 1) + L"ClientConfig.ini";

        // 파일 존재 확인
        DWORD attr = GetFileAttributesW(iniPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES)
        {
            std::cout << "[ClientConfig] ClientConfig.ini not found. Using defaults." << std::endl;
            return false;
        }

        const wchar_t* path = iniPath.c_str();

        // [Server] 섹션
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Server", L"IP", L"127.0.0.1", buf, 256, path);
        serverIp = WtoA(buf);

        serverPort = GetPrivateProfileIntW(L"Server", L"Port", 6000, path);

        PrintConfig();
        return true;
    }

private:
    static std::string WtoA(const wchar_t* wstr)
    {
        int len = WideCharToMultiByte(CP_ACP, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        std::string result(len - 1, '\0');
        WideCharToMultiByte(CP_ACP, 0, wstr, -1, &result[0], len, nullptr, nullptr);
        return result;
    }

    void PrintConfig() const
    {
        std::cout << "[ClientConfig] Loaded from INI" << std::endl;
        std::cout << "  ServerIP   : " << serverIp << std::endl;
        std::cout << "  ServerPort : " << serverPort << std::endl;
    }
};
