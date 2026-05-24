#pragma once

#include <cstdint>

// 패킷 타입 (혼용 방지를 위해 L7 Msg로 표기)
enum class MsgType : uint16_t
{
	// C2S: Client to Server
	// S2C: Server to Client

    //--------------------------------------------------
    // 에코 (NetWorkLib_EchoTest 모드 전용)
    //--------------------------------------------------
    ECHO = 0,

    //--------------------------------------------------
    // 이동
    //--------------------------------------------------
    C2S_MOVE_START = 1000,  // 클라이언트 → 서버: 이동 시작
    C2S_MOVE_STOP,          // 클라이언트 → 서버: 이동 정지

    S2C_MOVE_START,         // 서버 → 클라이언트: 다른 플레이어 이동 시작
    S2C_MOVE_STOP,          // 서버 → 클라이언트: 다른 플레이어 이동 정지

    //--------------------------------------------------
    // 스폰 / 삭제
    //--------------------------------------------------
    S2C_CREATE_MY_PLAYER,   // 서버 → 클라이언트: 내 캐릭터 생성
    S2C_CREATE_OTHER_PLAYER,// 서버 → 클라이언트: 다른 캐릭터 생성 (시야 진입)
    S2C_DELETE_PLAYER,      // 서버 → 클라이언트: 캐릭터 삭제 (시야 이탈/퇴장)

    //--------------------------------------------------
    // 채팅
    //--------------------------------------------------
    C2S_CHAT,               // 클라이언트 → 서버: 채팅 메시지 전송
    S2C_CHAT,               // 서버 → 클라이언트: 채팅 메시지 브로드캐스트

    //--------------------------------------------------
    // 좌표 보정
    //--------------------------------------------------
    S2C_SYNC_POSITION,  // 서버 → 클라이언트: 좌표 강제 보정

    //--------------------------------------------------
    // 존 정보 / 존 이동
    //--------------------------------------------------
    S2C_ZONE_INFO,        // 서버 → 클라이언트: 존 메타 정보 (맵 크기 등)
    C2S_ZONE_CHANGE,      // 클라이언트 → 서버: 맵 이동 요청
    S2C_ZONE_CHANGE_OK,   // 서버 → 클라이언트: 이동 성공
    S2C_ZONE_CHANGE_FAIL, // 서버 → 클라이언트: 이동 실패

    //--------------------------------------------------
    // 하트비트
    //--------------------------------------------------
    C2S_HEARTBEAT,          // 클라이언트 → 서버: 연결 유지 하트비트

    //--------------------------------------------------
    // 운영자
    //--------------------------------------------------
    C2S_ADMIN_LOGIN,        // 클라이언트 → 서버: 운영자 인증 요청
    S2C_ADMIN_LOGIN_OK,     // 서버 → 클라이언트: 인증 성공
    S2C_ADMIN_LOGIN_FAIL,   // 서버 → 클라이언트: 인증 실패

    //--------------------------------------------------
    // 에러
    //--------------------------------------------------
    S2C_ERROR
};

// 패킷 헤더 (모든 패킷 공통)
#pragma pack(push, 1)
struct MsgHeader
{
    uint16_t size;        // 패킷 전체 크기 (헤더 포함)
    MsgType type;         // 패킷 타입
};

// 에코 테스트용 헤더 (GameCodiEchoTest 더미 클라이언트 호환)
struct EchoMsgHeader
{
    uint16_t size;        // 페이로드 크기 (헤더 미포함)
};

// NetWorkLib_EchoTest: MsgHeader(4byte) + uint64_t 에코 값
constexpr uint16_t ECHO_BODY_SIZE  = static_cast<uint16_t>(sizeof(uint64_t));
constexpr uint16_t ECHO_TOTAL_SIZE = static_cast<uint16_t>(sizeof(MsgHeader) + sizeof(uint64_t));

//==================================================
// 이동
//==================================================

// C2S: 이동 시작 (방향키 누름)
struct MSG_C2S_MOVE_START
{
    MsgHeader header;
    uint8_t direction;    // Direction enum
    float x;              // 클라이언트 예측 좌표 (서버 허용 범위 내 수용)
    float y;
};

// C2S: 이동 정지 (방향키 뗌)
struct MSG_C2S_MOVE_STOP
{
    MsgHeader header;
    uint8_t direction;    // 마지막 방향 (캐릭터가 바라보는 방향)
    float x;              // 정지 시점 좌표
    float y;
};

// S2C: 다른 플레이어 이동 시작
struct MSG_S2C_MOVE_START
{
    static constexpr MsgType TYPE = MsgType::S2C_MOVE_START;
    MsgHeader header;
    int32_t playerId;     // 이동하는 플레이어
    uint8_t direction;
    float x;              // 이동 시작 시점 좌표
    float y;

    MSG_S2C_MOVE_START() : header{ sizeof(*this), TYPE }, playerId(0), direction(0), x(0), y(0) {}
};

// S2C: 다른 플레이어 이동 정지
struct MSG_S2C_MOVE_STOP
{
    static constexpr MsgType TYPE = MsgType::S2C_MOVE_STOP;
    MsgHeader header;
    int32_t playerId;
    uint8_t direction;
    float x;              // 정지 좌표
    float y;

    MSG_S2C_MOVE_STOP() : header{ sizeof(*this), TYPE }, playerId(0), direction(0), x(0), y(0) {}
};

//==================================================
// 스폰 / 삭제
//==================================================

// S2C: 내 캐릭터 생성 (접속 직후)
struct MSG_S2C_CREATE_MY_PLAYER
{
    static constexpr MsgType TYPE = MsgType::S2C_CREATE_MY_PLAYER;
    MsgHeader header;
    int32_t playerId;
    uint8_t direction;
    uint8_t displayChar;  // 서버 권위 표시 문자 (ASCII: A-Z, a-z, 0-9)
    uint8_t colorIndex;   // 서버 권위 색상 인덱스 (0-6)
    float x;
    float y;
    int32_t speed;

    MSG_S2C_CREATE_MY_PLAYER() : header{ sizeof(*this), TYPE }, playerId(0), direction(0), displayChar('A'), colorIndex(0), x(0), y(0), speed(0) {}
};

// 스폰 사유 (S2C_CREATE_OTHER_PLAYER 전용)
enum class SpawnReason : uint8_t
{
    NORMAL = 0,         // 걸어서 시야 진입
    ZONE_TRANSFER = 1,  // 존/채널 이동으로 등장
    CONNECT = 2         // 최초 접속으로 등장
};

// S2C: 다른 캐릭터 생성 (시야 진입)
struct MSG_S2C_CREATE_OTHER_PLAYER
{
    static constexpr MsgType TYPE = MsgType::S2C_CREATE_OTHER_PLAYER;
    MsgHeader header;
    int32_t playerId;
    uint8_t direction;
    uint8_t moveState;    // MoveState enum (진입 시 이동 중일 수 있음)
    uint8_t displayChar;  // 서버 권위 표시 문자 (ASCII: A-Z, a-z, 0-9)
    uint8_t colorIndex;   // 서버 권위 색상 인덱스 (0-6)
    uint8_t spawnReason;  // SpawnReason enum (등장 사유)
    float x;
    float y;
    int32_t speed;

    MSG_S2C_CREATE_OTHER_PLAYER() : header{ sizeof(*this), TYPE }, playerId(0), direction(0), moveState(0), displayChar('A'), colorIndex(0), spawnReason(0), x(0), y(0), speed(0) {}
};

// S2C: 캐릭터 삭제 (시야 이탈 / 퇴장)
struct MSG_S2C_DELETE_PLAYER
{
    static constexpr MsgType TYPE = MsgType::S2C_DELETE_PLAYER;
    MsgHeader header;
    int32_t playerId;

    MSG_S2C_DELETE_PLAYER() : header{ sizeof(*this), TYPE }, playerId(0) {}
};

//==================================================
// 채팅
//==================================================

constexpr int32_t CHAT_MSG_MAX_LEN = 512; // wchar_t 기준 글자 수 (1024바이트)

// C2S: 채팅 메시지
struct MSG_C2S_CHAT
{
    MsgHeader header;
    wchar_t message[CHAT_MSG_MAX_LEN];
};

// S2C: 채팅 메시지 (발신자 정보 포함)
struct MSG_S2C_CHAT
{
    static constexpr MsgType TYPE = MsgType::S2C_CHAT;
    MsgHeader header;
    int32_t playerId;     // 발신자
    uint8_t displayChar;  // 발신자 표시 문자
    uint8_t colorIndex;   // 발신자 색상 인덱스
    wchar_t message[CHAT_MSG_MAX_LEN];

    MSG_S2C_CHAT() : header{ sizeof(*this), TYPE }, playerId(0), displayChar('A'), colorIndex(0), message{} {}
};

//==================================================
// 좌표 보정
//==================================================

// S2C: 서버 권위 좌표 강제 동기화 (자기 자신 + 타인 주기적 동기화 겸용)
struct MSG_S2C_SYNC_POSITION
{
    static constexpr MsgType TYPE = MsgType::S2C_SYNC_POSITION;
    MsgHeader header;
    int32_t playerId;
    float x;
    float y;

    MSG_S2C_SYNC_POSITION() : header{ sizeof(*this), TYPE }, playerId(0), x(0), y(0) {}
};

//==================================================
// 존 정보 / 존 이동
//==================================================

// S2C: 존 메타 정보 (존 입장/이동 시 CREATE_MY_PLAYER 앞에 전송)
struct MSG_S2C_ZONE_INFO
{
    static constexpr MsgType TYPE = MsgType::S2C_ZONE_INFO;
    MsgHeader header;
    int32_t mapId;
    int32_t channelIndex;
    int32_t mapWidth;
    int32_t mapHeight;
    int32_t sectorSize;

    MSG_S2C_ZONE_INFO() : header{ sizeof(*this), TYPE }, mapId(0), channelIndex(0), mapWidth(0), mapHeight(0), sectorSize(0) {}
};

// C2S: 맵 이동 / 채널 이동 요청
struct MSG_C2S_ZONE_CHANGE
{
    MsgHeader header;
    int32_t targetMapId;
    int32_t targetChannelIndex = -1; // -1: 자동배정(기존 동작), 0 이상: 지정 채널
};

// S2C: 맵 이동 성공
struct MSG_S2C_ZONE_CHANGE_OK
{
    static constexpr MsgType TYPE = MsgType::S2C_ZONE_CHANGE_OK;
    MsgHeader header;
    int32_t mapId;
    int32_t channelIndex;
    int32_t playerId;
    uint8_t displayChar;  // 서버 권위 표시 문자
    uint8_t colorIndex;   // 서버 권위 색상 인덱스
    uint8_t direction;    // 서버 권위 방향 (Direction enum)
    float x;
    float y;

    MSG_S2C_ZONE_CHANGE_OK() : header{ sizeof(*this), TYPE }, mapId(0), channelIndex(0), playerId(0), displayChar('A'), colorIndex(0), direction(0), x(0), y(0) {}
};

// S2C: 맵 이동 실패
struct MSG_S2C_ZONE_CHANGE_FAIL
{
    static constexpr MsgType TYPE = MsgType::S2C_ZONE_CHANGE_FAIL;
    MsgHeader header;
    uint8_t reason;  // 0: 존재하지 않는 맵/채널, 1: 모든 채널 가득 참, 2: 이미 해당 채널

    MSG_S2C_ZONE_CHANGE_FAIL() : header{ sizeof(*this), TYPE }, reason(0) {}
};

//==================================================
// 운영자
//==================================================

constexpr int32_t ADMIN_KEY_MAX_LEN = 64;

// C2S: 운영자 인증 요청
struct MSG_C2S_ADMIN_LOGIN
{
    MsgHeader header;
    char key[ADMIN_KEY_MAX_LEN];
};

// S2C: 운영자 인증 성공
struct MSG_S2C_ADMIN_LOGIN_OK
{
    static constexpr MsgType TYPE = MsgType::S2C_ADMIN_LOGIN_OK;
    MsgHeader header;

    MSG_S2C_ADMIN_LOGIN_OK() : header{ sizeof(*this), TYPE } {}
};

// S2C: 운영자 인증 실패
struct MSG_S2C_ADMIN_LOGIN_FAIL
{
    static constexpr MsgType TYPE = MsgType::S2C_ADMIN_LOGIN_FAIL;
    MsgHeader header;

    MSG_S2C_ADMIN_LOGIN_FAIL() : header{ sizeof(*this), TYPE } {}
};

//==================================================
// 하트비트
//==================================================

// C2S: 연결 유지 하트비트 (페이로드 없음)
struct MSG_C2S_HEARTBEAT
{
    MsgHeader header;
};

//==================================================
// 에러
//==================================================

// S2C: 에러 응답
struct MSG_S2C_ERROR
{
    static constexpr MsgType TYPE = MsgType::S2C_ERROR;
    MsgHeader header;
    char message[256];

    MSG_S2C_ERROR() : header{ sizeof(*this), TYPE }, message{} {}
};

#pragma pack(pop)
