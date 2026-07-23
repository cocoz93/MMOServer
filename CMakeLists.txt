cmake_minimum_required(VERSION 3.20)
project(MMO LANGUAGES CXX)

# C++17 (기존 vcxproj: stdcpp17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 단일구성 제너레이터(Ninja 등) 기본 Release — 기존 검증 기준
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# MySQL 8.0 설치 경로 (필요 시 -DMYSQL_DIR=... 로 override)
set(MYSQL_DIR "C:/Program Files/MySQL/MySQL Server 8.0" CACHE PATH "MySQL install dir")

set(SRV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/IOCP_Server/IOCP_Server)

if(WIN32)

# ─────────────────────────────────────────────────────────────
#  IOCP 서버 (Windows) — 기존 IOCP_Server.vcxproj 재현
# ─────────────────────────────────────────────────────────────
add_executable(IOCP_Server
    ${SRV_DIR}/IOCPServer.cpp
    ${SRV_DIR}/main.cpp
    ${SRV_DIR}/SerialBuffer.cpp
    ${SRV_DIR}/Player.cpp
    ${SRV_DIR}/SectorManager.cpp
    ${SRV_DIR}/Zone.cpp
    ${SRV_DIR}/MapManager.cpp
    ${SRV_DIR}/GameServer.cpp
    ${SRV_DIR}/DB/DBWorker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Shared/Common/Logger.cpp
)

target_include_directories(IOCP_Server PRIVATE
    ${SRV_DIR}                 # 프로젝트 헤더 (IOCPServer.h 등)
    ${SRV_DIR}/ThirdParty      # spdlog·fmt·httplib (헤더온리 번들)
    ${MYSQL_DIR}/include       # MySQL C API
)

target_compile_definitions(IOCP_Server PRIVATE
    _CONSOLE
    USE_SPDLOG_LOGGER
    UNICODE
    _UNICODE
)

if(MSVC)
    # 공통: UTF-8 소스, 표준 준수(/permissive-), SDL, 다중 컴파일
    target_compile_options(IOCP_Server PRIVATE /utf-8 /permissive- /sdl /MP)
    # Release: 함수단위링크(/Gy)·인트린식(/Oi)·전체프로그램최적화(/GL)
    target_compile_options(IOCP_Server PRIVATE $<$<CONFIG:Release>:/Gy /Oi /GL>)
    target_link_options(IOCP_Server PRIVATE /SUBSYSTEM:CONSOLE /DEBUG
                        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
    # libmysql.lib 는 소스의 #pragma comment(lib)로 자동 링크 → lib 디렉토리만 링크경로에
    target_link_directories(IOCP_Server PRIVATE ${MYSQL_DIR}/lib)
endif()

# 출력물을 기존 .sln과 동일하게 Run/bin 에 (런타임 DLL·INI가 거기 있음)
set_target_properties(IOCP_Server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_CURRENT_SOURCE_DIR}/Run/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/Run/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_CURRENT_SOURCE_DIR}/Run/bin
)
endif()  # WIN32

# ─────────────────────────────────────────────────────────────
#  epoll 에코 PoC (Linux) — Phase 2 툴체인/이벤트 루프 검증용
#  게임서버 본체와 무관한 독립 실행파일 (LockFree 포팅 전 단계)
# ─────────────────────────────────────────────────────────────
if(UNIX)
    add_executable(epoll_echo_poc
        ${SRV_DIR}/poc/epoll_echo_main.cpp
    )
    target_compile_features(epoll_echo_poc PRIVATE cxx_std_17)
    set_target_properties(epoll_echo_poc PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Run/bin)
endif()