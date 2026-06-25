#include "GameServer.h"
#include "Player.h"
#include "../../Shared/Protocol/Protocol.h"
#include "SerialBuffer.h"
#include "../../Shared/Common/ErrorLog.h"
#include <iostream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstddef>  // offsetof
#include <cassert>  // 직렬화 필드 누락/폭 검증

// ==========================================================================
// S2C 직렬화 헬퍼 (파일 로컬)
//
// 와이어 = payload 영역 바이트 그대로 (WSASend가 GetReadBufferPtr()+GetDataSize()만 전송).
// pack(1)이라 패딩이 없으므로 필드를 선언 순서/폭대로 쓰면 구조체와 바이트 동일.
// 스칼라는 <<, 고정배열/문자열은 SetData(raw) — << 문자열 연산자는 길이접두(2B)가 끼어
// 와이어가 깨지므로 사용 금지.
//
// [소유권 계약] 빌더는 RefCount=1(소유권 1개)인 버퍼를 반환하고,
//   송신 함수(BroadcastAroundSector/SendPacket의 CSerialBuffer* 오버로드)가 이를 소비한다.
// ==========================================================================
namespace
{
    // 헤더 시작: size placeholder(아래서 백패치) + type
    inline void BeginPacket(CSerialBuffer* buf, MsgType type)
    {
        *buf << static_cast<WORD>(0);                 // header.size (placeholder)
        *buf << static_cast<WORD>(type);              // header.type
    }

    // 헤더 종료: size 백패치(= 전체 payload 바이트) + Seal
    // 소유권 1은 Alloc()/Clear()에서 이미 확보됨(RefCount=1) → 여기서 AddRef 불필요
    inline void FinalizePacket(CSerialBuffer* buf)
    {
        *reinterpret_cast<uint16_t*>(buf->GetPayloadBufferPtr()) =
            static_cast<uint16_t>(buf->GetDataSize());
        buf->Seal();
    }

    // MOVE_START / MOVE_STOP은 레이아웃 동일 — 3곳/2곳에서 재사용되므로 빌더로 DRY 처리
    CSerialBuffer* MakeMoveStart(int32_t playerId, uint8_t direction, float x, float y)
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_MOVE_START::TYPE);
        *buf << static_cast<int>(playerId);
        *buf << static_cast<BYTE>(direction);
        *buf << x;
        *buf << y;
        FinalizePacket(buf);
        assert(buf->GetDataSize() == sizeof(MSG_S2C_MOVE_START));
        return buf;
    }

    CSerialBuffer* MakeMoveStop(int32_t playerId, uint8_t direction, float x, float y)
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_MOVE_STOP::TYPE);
        *buf << static_cast<int>(playerId);
        *buf << static_cast<BYTE>(direction);
        *buf << x;
        *buf << y;
        FinalizePacket(buf);
        assert(buf->GetDataSize() == sizeof(MSG_S2C_MOVE_STOP));
        return buf;
    }

    CSerialBuffer* MakeSyncPosition(int32_t playerId, float x, float y)
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_SYNC_POSITION::TYPE);
        *buf << static_cast<int>(playerId);
        *buf << x;
        *buf << y;
        FinalizePacket(buf);
        assert(buf->GetDataSize() == sizeof(MSG_S2C_SYNC_POSITION));
        return buf;
    }

    CSerialBuffer* MakeZoneInfo(CZone* zone)
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_ZONE_INFO::TYPE);
        *buf << static_cast<int>(zone->GetMapId());
        *buf << static_cast<int>(CMapManager::GetChannelIndexFromZoneId(zone->GetZoneId()));
        *buf << static_cast<int>(zone->GetMapWidth());
        *buf << static_cast<int>(zone->GetMapHeight());
        *buf << static_cast<int>(zone->GetSectorManager().GetSectorSize());
        FinalizePacket(buf);
        assert(buf->GetDataSize() == sizeof(MSG_S2C_ZONE_INFO));
        return buf;
    }

    CSerialBuffer* MakeCreateMyPlayer(CPlayer* target)
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_CREATE_MY_PLAYER::TYPE);
        *buf << static_cast<int>(target->_playerId);
        *buf << static_cast<BYTE>(target->_direction);
        *buf << static_cast<BYTE>(target->_displayChar);
        *buf << static_cast<BYTE>(target->_colorIndex);
        *buf << target->_x;
        *buf << target->_y;
        *buf << static_cast<int>(target->_speed);
        FinalizePacket(buf);
        assert(buf->GetDataSize() == sizeof(MSG_S2C_CREATE_MY_PLAYER));
        return buf;
    }

    CSerialBuffer* MakeCreateOtherPlayer(CPlayer* player, SpawnReason reason)
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_CREATE_OTHER_PLAYER::TYPE);
        *buf << static_cast<int>(player->_playerId);
        *buf << static_cast<BYTE>(player->_direction);
        *buf << static_cast<BYTE>(player->_moveState);
        *buf << static_cast<BYTE>(player->_displayChar);
        *buf << static_cast<BYTE>(player->_colorIndex);
        *buf << static_cast<BYTE>(reason);
        *buf << player->_x;
        *buf << player->_y;
        *buf << static_cast<int>(player->_speed);
        FinalizePacket(buf);
        assert(buf->GetDataSize() == sizeof(MSG_S2C_CREATE_OTHER_PLAYER));
        return buf;
    }

    CSerialBuffer* MakeDeletePlayer(int32_t playerId)
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_DELETE_PLAYER::TYPE);
        *buf << static_cast<int>(playerId);
        FinalizePacket(buf);
        assert(buf->GetDataSize() == sizeof(MSG_S2C_DELETE_PLAYER));
        return buf;
    }

    CSerialBuffer* MakeZoneChangeOk(CPlayer* target, int32_t mapId, int32_t channelIndex)
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_ZONE_CHANGE_OK::TYPE);
        *buf << static_cast<int>(mapId);
        *buf << static_cast<int>(channelIndex);
        *buf << static_cast<int>(target->_playerId);
        *buf << static_cast<BYTE>(target->_displayChar);
        *buf << static_cast<BYTE>(target->_colorIndex);
        *buf << static_cast<BYTE>(target->_direction);
        *buf << target->_x;
        *buf << target->_y;
        FinalizePacket(buf);
        assert(buf->GetDataSize() == sizeof(MSG_S2C_ZONE_CHANGE_OK));
        return buf;
    }

    CSerialBuffer* MakeZoneChangeFail(uint8_t reason)
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_ZONE_CHANGE_FAIL::TYPE);
        *buf << static_cast<BYTE>(reason);
        FinalizePacket(buf);
        assert(buf->GetDataSize() == sizeof(MSG_S2C_ZONE_CHANGE_FAIL));
        return buf;
    }

    CSerialBuffer* MakeAdminLoginOk()
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_ADMIN_LOGIN_OK::TYPE);    // 페이로드 없음 (헤더만)
        FinalizePacket(buf);
        assert(buf->GetDataSize() == sizeof(MSG_S2C_ADMIN_LOGIN_OK));
        return buf;
    }

    CSerialBuffer* MakeAdminLoginFail()
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_ADMIN_LOGIN_FAIL::TYPE);  // 페이로드 없음 (헤더만)
        FinalizePacket(buf);
        assert(buf->GetDataSize() == sizeof(MSG_S2C_ADMIN_LOGIN_FAIL));
        return buf;
    }

    // 가변 길이 — message는 고정배열/문자열이라 SetData(raw). msgLen은 null 제외 글자 수.
    CSerialBuffer* MakeChat(int32_t playerId, uint8_t displayChar, uint8_t colorIndex,
                             wchar_t* message, uint16_t msgLen)
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_CHAT::TYPE);
        *buf << static_cast<int>(playerId);
        *buf << static_cast<BYTE>(displayChar);
        *buf << static_cast<BYTE>(colorIndex);
        buf->SetData(reinterpret_cast<char*>(message),
            (msgLen + 1) * sizeof(wchar_t));               // message (가변, null 포함, raw)
        FinalizePacket(buf);
        assert(buf->GetDataSize() ==
            offsetof(MSG_S2C_CHAT, message) + (msgLen + 1) * sizeof(wchar_t));
        return buf;
    }

#if USE_SECTOR_AGGREGATION
    // 섹터 묶음 — dirty 플레이어 [0,count)의 최종 상태(위치·방향·이동상태)를 직렬화.
    // 스칼라 시퀀스(<<)라 SetData 불필요. count는 선기록(백패치 불필요).
    CSerialBuffer* MakeSectorUpdates(CPlayer** players, int count)
    {
        CSerialBuffer* buf = CSerialBuffer::Alloc();
        BeginPacket(buf, MSG_S2C_SECTOR_UPDATES::TYPE);
        *buf << static_cast<WORD>(count);
        for (int i = 0; i < count; ++i)
        {
            CPlayer* p = players[i];
            *buf << static_cast<int>(p->_playerId);
            *buf << static_cast<BYTE>(p->_direction);
            *buf << static_cast<BYTE>(p->_moveState);
            *buf << p->_x;
            *buf << p->_y;
        }
        FinalizePacket(buf);
        assert(buf->GetDataSize() ==
            static_cast<int>(sizeof(uint16_t) + count * sizeof(SectorUpdateEntry)));
        return buf;
    }
#endif
}

CGameServer::CGameServer(CMonitorManager& monitor)
    : _mode(ServerMode::GameServer)
    , _monitor(monitor)
    , _running(false)
{
}

CGameServer::~CGameServer()
{
    Stop();
}

bool CGameServer::Init(ServerMode mode, int port, int maxClients)
{
    return Init(mode, port, maxClients, nullptr, 0);
}

bool CGameServer::Init(ServerMode mode, int port, int maxClients,
                        const MapConfig* maps, int32_t mapCount)
{
    _mode = mode;

    _network = std::make_unique<CIOCPServer>(port, maxClients, _mode, _monitor);

    // 게임 서버 모드일 때만 맵 등록
    if (_mode == ServerMode::GameServer)
    {
        if (maps == nullptr || mapCount <= 0)
            return false;

        for (int32_t i = 0; i < mapCount; ++i)
        {
            if (!_mapManager.RegisterMap(maps[i]))
                return false;
        }

        // 첫 번째 맵을 기본 접속 맵으로 설정
        _defaultMapId = maps[0].mapId;

        // 프레임 재사용 컨테이너 reserve (워밍업 realloc 방지)
        // 전체 서버 범위 버퍼도 maxPerChannel 기준 — 초과 시 자연 증가 후 capacity 유지
        int32_t maxPerChannel = maps[0].maxPlayersPerChannel;
        _sessionToPlayer.reserve(maxClients);
        _broadcastBuffer.reserve(maxPerChannel);       // 주변 9섹터 단위
        _eventAroundBuffer.reserve(maxPerChannel);     // 주변 9섹터 단위
        _pendingSectorChanges.reserve(maxPerChannel);  // 전체 서버 프레임 이벤트
        _tickSectorChanges.reserve(maxPerChannel);     // 전체 서버 TickAll 결과
        _tickClampedPlayers.reserve(maxPerChannel);    // 전체 서버 TickAll 결과
        _sectorChangedSet.reserve(maxPerChannel);      // 전체 서버 dedup
        _dirtyMovers.reserve(maxClients);              // 전체 서버 dirty (이동/sync 묶음 대상)
    }

    return true;
}

bool CGameServer::Start()
{
    if (!_network->Start())
        return false;

    // 게임 서버 모드일 때만 게임 루프 스레드 시작
    // 에코 테스트는 CIOCPServer 내부에서 자체 처리
    if (_mode == ServerMode::GameServer)
    {
        _running = true;
        _gameThread = std::thread(&CGameServer::GameLoopThread, this);
    }

    const char* modeName = "Unknown";
    switch (_mode)
    {
    case ServerMode::GameCodiEchoTest:    modeName = "GameCodiEchoTest";    break;
    case ServerMode::NetWorkLib_EchoTest: modeName = "NetWorkLib_EchoTest"; break;
    case ServerMode::GameServer:          modeName = "GameServer";          break;
    }
    SLOG_INFO("[GameServer] Started - Mode: {}", modeName);

    return true;
}

void CGameServer::Stop()
{
    _running = false;

    if (_gameThread.joinable())
        _gameThread.join();

    if (_network)
    {
        _network->ShutdownServer();

        // 미처리 이벤트의 CSerialBuffer 정리
        NetworkEvent event(NetworkEvent::Type::CONNECTED, 0);
        while (_network->PopNetworkEvent(event))
        {
            if (event.pMsg != nullptr)
                event.pMsg->SubRef();
        }
    }

    // 잔여 플레이어 정리 (CGameServer가 생명주기 소유)
    for (auto& pair : _sessionToPlayer)
    {
        delete pair.second;
    }
    _sessionToPlayer.clear();
}

// ==========================================================================
// 게임 루프
// ==========================================================================

void CGameServer::GameLoopThread()
{
    using Clock = std::chrono::steady_clock;

    // CPU 점유율 측정용: 자기 실핸들을 복제해 모니터에 등록 (진단정리 6 사각지대 보강)
    // GetCurrentThread()는 의사핸들(호출 스레드 기준)이라 HTTP 스레드에서 못 씀 → 실핸들로 복제
    {
        HANDLE dup = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                            GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS))
        {
            _monitor._gameLoopThreadHandle = dup;
        }
    }

    auto prevTime = Clock::now();

    while (_running)
    {
        auto frameStart = Clock::now();

        // deltaTime 계산
        float deltaTime = std::chrono::duration<float>(frameStart - prevTime).count();
        prevTime = frameStart;

        // 1) 네트워크 이벤트 전부 소비
        _monitor._gameLoop._eventQueueSize = static_cast<LONG>(_network->GetEventQueueSize());
        auto phaseT1 = Clock::now();
        ProcessNetworkEvents();
        auto phaseT2 = Clock::now();

        // 2) 게임 로직 갱신 (좌표 이동 + 섹터 변경 감지 + 경계 클램핑)
        _tickSectorChanges.clear();
        _tickClampedPlayers.clear();
        _mapManager.TickAll(deltaTime, _tickSectorChanges, _tickClampedPlayers);

        // 3) 섹터 변경 배치 처리 (RecvMoveStop + TickAll 병합 → 삽입 시 중복 차단)
        for (const auto& change : _tickSectorChanges)
        {
            PushSectorChange(change.player, change.oldSectorX, change.oldSectorY);
        }

        // [계측] 멤버십(섹터이동 CREATE/DELETE) 송신 시간 — 구간 1쌍으로만 측정.
        //   per-call 금지: 멤버십은 ~1.04M/s라 건당 now()를 넣으면 측정 오버헤드가 대상을 오염시킴.
        auto membT0 = Clock::now();
        for (const auto& change : _pendingSectorChanges)
        {
            // 출발 섹터 == 현재 섹터이면 원위치 복귀 → 브로드캐스트 불필요
            if (change.oldSectorX == change.player->_sectorX &&
                change.oldSectorY == change.player->_sectorY)
                continue;

            CZone* zone = _mapManager.GetZone(change.player->_zoneId);
            if (zone != nullptr)
            {
                ProcessSectorChange(zone, change.player,
                                    change.oldSectorX, change.oldSectorY);
            }
        }
        _tickMembershipUs += std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - membT0).count();
        _pendingSectorChanges.clear();
        _sectorChangedSet.clear();

        // 3-1) 맵 경계 클램핑으로 정지된 플레이어에게 MOVE_STOP 브로드캐스트
        for (CPlayer* player : _tickClampedPlayers)
        {
            CZone* zone = _mapManager.GetZone(player->_zoneId);
            if (zone != nullptr)
            {
#if USE_SECTOR_AGGREGATION
                MarkMoveDirty(player);
#else
                BroadcastAroundSector(zone, player,
                    MakeMoveStop(player->_playerId, static_cast<uint8_t>(player->_direction),
                        player->_x, player->_y), false);  // 본인 포함
#endif
            }
        }

        auto phaseT3 = Clock::now();

        // 4) 주기적 위치 동기화 (MOVING 플레이어 → 주변 브로드캐스트)
        ++_syncFrameCount;
        if (_syncFrameCount >= SYNC_INTERVAL_FRAMES)
        {
            _syncFrameCount = 0;
            _mapManager.ForEachZone([&](CZone* zone)
            {
                for (CPlayer* player : zone->GetPlayers())
                {
                    if (player->_moveState != MoveState::MOVING)
                        continue;

                    // 델타 동기화: 마지막 동기화 이후 충분히 이동한 경우만 전송
                    float dx = player->_x - player->_lastSyncX;
                    float dy = player->_y - player->_lastSyncY;
                    if (dx * dx + dy * dy < SYNC_DISTANCE_THRESHOLD_SQ)
                        continue;

                    player->_lastSyncX = player->_x;
                    player->_lastSyncY = player->_y;

#if USE_SECTOR_AGGREGATION
                    MarkMoveDirty(player);
#else
                    BroadcastAroundSector(zone, player,
                        MakeSyncPosition(player->_playerId, player->_x, player->_y),
                        false);  // 본인 포함 (이동 중 클라-서버 좌표 드리프트 보정)
#endif
                }
            });
        }

#if USE_SECTOR_AGGREGATION
        // [묶음] 이번 틱 dirty를 섹터별 묶음으로 송신 (RequestSendMsg는 Deferred → 아래 flush가 묶어 보냄)
        FlushSectorUpdates();
#endif

        // [coalescing] 이번 틱에 쌓인 송신을 세션당 1회 WSASend로 flush (broadcast_sync 페이즈에 계상)
        // [계측] flush 구간을 별도 측정 → 비용종류별 "송신(WSASend)" 분리. _phaseBroadcastSyncUs는 그대로 둠(이 값을 포함).
        auto flushT0 = Clock::now();
        _network->FlushPendingSends();
        auto flushT1 = Clock::now();

        auto phaseT4 = Clock::now();

        // 구간별 시간 기록 (마이크로초 누적)
        InterlockedExchangeAdd64(&_monitor._gameLoop._phaseNetworkUs,
            std::chrono::duration_cast<std::chrono::microseconds>(phaseT2 - phaseT1).count());
        InterlockedExchangeAdd64(&_monitor._gameLoop._phaseGameLogicUs,
            std::chrono::duration_cast<std::chrono::microseconds>(phaseT3 - phaseT2).count());
        InterlockedExchangeAdd64(&_monitor._gameLoop._phaseBroadcastSyncUs,
            std::chrono::duration_cast<std::chrono::microseconds>(phaseT4 - phaseT3).count());

        // [계측] 비용종류별 — 틱 내 누적분을 1회씩 원자 반영 후 리셋 (송신은 flush 구간 직접 측정)
        InterlockedExchangeAdd64(&_monitor._gameLoop._broadcastGatherUs, _tickBroadcastGatherUs);
        InterlockedExchangeAdd64(&_monitor._gameLoop._broadcastEnqueueUs, _tickBroadcastEnqueueUs);
        InterlockedExchangeAdd64(&_monitor._gameLoop._flushSendUs,
            std::chrono::duration_cast<std::chrono::microseconds>(flushT1 - flushT0).count());
        InterlockedExchangeAdd64(&_monitor._gameLoop._membershipSends, _tickMembershipSends);  // 멤버십 변경 복사량(횟수)
        InterlockedExchangeAdd64(&_monitor._gameLoop._membershipCostUs, _tickMembershipUs);    // 멤버십 변경 송신 시간
        _tickBroadcastGatherUs = 0;
        _tickBroadcastEnqueueUs = 0;
        _tickMembershipSends = 0;
        _tickMembershipUs = 0;

        // 5) 빈 동적 채널 정리
        ++_cleanupFrameCount;
        if (_cleanupFrameCount >= CLEANUP_INTERVAL_FRAMES)
        {
            _cleanupFrameCount = 0;
            _mapManager.CleanupEmptyChannels();
        }

        // 6) Tick 시간 기록 + 프레임 제한
        auto frameEnd = Clock::now();
        double tickMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        _monitor._gameLoop.RecordTickTime(tickMs);

        int sleepMs = FRAME_INTERVAL_MS - static_cast<int>(tickMs);
        if (sleepMs > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    }
}

// ==========================================================================
// 이벤트 디스패치
// ==========================================================================

void CGameServer::ProcessNetworkEvents()
{
    // 프레임 진입 시점 스냅샷을 통째로 스왑 (락 1회). 처리 중 도착분은 다음 프레임으로 이월
    // _localEvents는 직전 프레임에 모두 소비되어 비어 있음 (멤버 재사용으로 deque 블록 보존)
    _network->SwapOutNetworkEvents(_localEvents);

    while (!_localEvents.empty())
    {
        NetworkEvent& event = _localEvents.front();
        switch (event.type)
        {
        case NetworkEvent::Type::CONNECTED:
            OnConnected(event.sessionId);
            break;

        case NetworkEvent::Type::DISCONNECTED:
            OnDisconnected(event.sessionId);
            break;

        case NetworkEvent::Type::RECEIVED:
            OnReceived(event.sessionId, event.pMsg);
            // handle-latency: enqueue(recv) → 처리완료(응답 송신 포함) 시간.
            // 틱 게이트 대기 + 핸들러 비용 = RTT의 서버 기여분. RECEIVED만 측정.
            if (event.enqueueTimeNs != 0)
            {
                int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                _monitor._gameLoop.RecordHandleLatency(
                    static_cast<double>(nowNs - event.enqueueTimeNs) / 1.0e6);
            }
            break;
        }
        _localEvents.pop();
    }
}

void CGameServer::OnConnected(int64_t sessionId)
{
    // 이미 등록된 세션이면 무시 (중복 접속 방어)
    if (_sessionToPlayer.find(sessionId) != _sessionToPlayer.end())
    {
        _network->RequestDisconnectSession(sessionId);
        return;
    }

    // 기본 맵의 여유 채널에 입장
    CZone* zone = _mapManager.FindOrCreateChannel(_defaultMapId);
    if (zone == nullptr)
    {
        _network->RequestDisconnectSession(sessionId);
        return;
    }

    // 플레이어 생성 + ID/표시 속성 부여 (CGameServer가 생명주기 소유)
    CPlayer* player = new CPlayer();
    player->_playerId = AllocPlayerId();
    player->_displayChar = CalcDisplayChar(player->_playerId);
    player->_colorIndex = CalcColorIndex(player->_playerId);
    if (!zone->EnterZone(player))
    {
        delete player;
        _network->RequestDisconnectSession(sessionId);
        return;
    }

    // 경계 매핑 등록
    player->_sessionId = sessionId;
    _sessionToPlayer[sessionId] = player;

    // 델타 동기화 기준 좌표 초기화 (스폰 직후 불필요한 동기화 방지)
    player->_lastSyncX = player->_x;
    player->_lastSyncY = player->_y;

    // 1) 존 메타 정보 전송
    SendZoneInfo(player, zone);

    // 2) 본인에게 내 캐릭터 생성
    SendCreateMyPlayer(player);

    // 3) 주변 상호 CREATE 브로드캐스트
    BroadcastEnterZone(zone, player, SpawnReason::CONNECT);
}

void CGameServer::OnDisconnected(int64_t sessionId)
{
    auto it = _sessionToPlayer.find(sessionId);
    if (it == _sessionToPlayer.end())
        return;

    CPlayer* player = it->second;
    CZone* zone = _mapManager.GetZone(player->_zoneId);
    if (zone != nullptr)
    {
        // 주변 DELETE 브로드캐스트 + 섹터/존 해제
        BroadcastLeaveZone(zone, player);
    }

    // 경계 매핑 해제 (zone 유무와 무관하게 반드시 수행)
    _sessionToPlayer.erase(it);

    // 플레이어 삭제 (CGameServer가 생명주기 소유)
    delete player;
}

void CGameServer::OnReceived(int64_t sessionId, CSerialBuffer* pMsg)
{
    if (pMsg == nullptr)
        return;

    // 헤더에서 패킷 타입 읽기
    MsgHeader header;
    pMsg->PeekData(reinterpret_cast<char*>(&header), sizeof(header));

    // 타입별 최소 패킷 크기 검증
    uint16_t expectedSize = GetExpectedSize(header.type);
    if (expectedSize == 0 || header.size < expectedSize)
    {
        InterlockedIncrement64(&_monitor._packetErrors);
        pMsg->SubRef();
        return;
    }

    // 경계 계층: sessionId → CPlayer* 변환 (이후 컨텐츠 로직은 CPlayer*만 사용)
    auto it = _sessionToPlayer.find(sessionId);
    if (it == _sessionToPlayer.end())
    {
        pMsg->SubRef();
        return;
    }
    CPlayer* player = it->second;

    switch (header.type)
    {
    case MsgType::C2S_MOVE_START:
        RecvMoveStart(player, pMsg);
        break;

    case MsgType::C2S_MOVE_STOP:
        RecvMoveStop(player, pMsg);
        break;

    case MsgType::C2S_CHAT:
        RecvChat(player, pMsg);
        break;

    case MsgType::C2S_ZONE_CHANGE:
        RecvZoneChange(player, pMsg);
        break;

    case MsgType::C2S_ADMIN_LOGIN:
        RecvAdminLogin(player, pMsg);
        break;

    default:
        // C2S_HEARTBEAT 등 — 게임 로직 처리 불필요 (타임아웃 갱신은 IOCPServer 수신 시점에서 처리)
        break;
    }

    // SerialBuffer 해제 (소유권 1 반환 — RefCount=0이면 프리리스트로)
    pMsg->SubRef();
}

uint16_t CGameServer::GetExpectedSize(MsgType type)
{
    switch (type)
    {
    case MsgType::C2S_MOVE_START:   return sizeof(MSG_C2S_MOVE_START);
    case MsgType::C2S_MOVE_STOP:    return sizeof(MSG_C2S_MOVE_STOP);
    case MsgType::C2S_CHAT:         return sizeof(MsgHeader) + sizeof(wchar_t); // 가변 길이: 최소 1글자
    case MsgType::C2S_ZONE_CHANGE:  return sizeof(MSG_C2S_ZONE_CHANGE);
    case MsgType::C2S_HEARTBEAT:    return sizeof(MSG_C2S_HEARTBEAT);
    case MsgType::C2S_ADMIN_LOGIN:  return sizeof(MSG_C2S_ADMIN_LOGIN);
    default:                        return 0;
    }
}

// ==========================================================================
// 패킷 핸들러
// ==========================================================================

void CGameServer::RecvMoveStart(CPlayer* player, CSerialBuffer* pMsg)
{
    CZone* zone = _mapManager.GetZone(player->_zoneId);
    if (zone == nullptr)
        return;

    MSG_C2S_MOVE_START recvMsg;
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), sizeof(recvMsg));

    // Direction 범위 검증 (4방향)
    if (recvMsg.direction < static_cast<uint8_t>(Direction::UP) ||
        recvMsg.direction > static_cast<uint8_t>(Direction::RIGHT))
        return;

    Direction dir = static_cast<Direction>(recvMsg.direction);

    // 이동 중 방향 전환: 같은 방향이면 무시, 다른 방향이면 갱신 + 재브로드캐스트
    // ※ 좌표 수용보다 먼저 검사 — MOVING 중 반복 전송으로 서버 좌표를 밀어내는 치트 방지
    if (player->_moveState == MoveState::MOVING)
    {
        if (dir == player->_direction)
            return;

        // 벽 방향 검증: 벽 위치에서 벽 쪽으로 방향 전환 시 정지 처리
        if (IsBlockedByWall(zone, player, dir))
        {
            player->_moveState = MoveState::IDLE;
            player->_direction = dir;

#if USE_SECTOR_AGGREGATION
            MarkMoveDirty(player);
#else
            BroadcastAroundSector(zone, player,
                MakeMoveStop(player->_playerId, static_cast<uint8_t>(dir),
                    player->_x, player->_y), false);  // 본인 포함
#endif
            return;
        }

        player->_direction = dir;
        player->_lastSyncX = player->_x;
        player->_lastSyncY = player->_y;

#if USE_SECTOR_AGGREGATION
        MarkMoveDirty(player);
#else
        BroadcastAroundSector(zone, player,
            MakeMoveStart(player->_playerId, static_cast<uint8_t>(dir),
                player->_x, player->_y));
#endif
        return;
    }

    // 클라이언트 예측 좌표 수용 (IDLE → MOVING 전환 시에만):
    // 서버 좌표와의 오차가 허용 범위 내이면 채택
    {
        float dx = recvMsg.x - player->_x;
        float dy = recvMsg.y - player->_y;
        if (dx * dx + dy * dy <= MOVE_START_ACCEPT_DIST_SQ)
        {
            // 맵 경계 클램핑 후 수용
            float acceptX = recvMsg.x;
            float acceptY = recvMsg.y;
            float mapW = static_cast<float>(zone->GetMapWidth());
            float mapH = static_cast<float>(zone->GetMapHeight());
            if (acceptX < 0.0f)    acceptX = 0.0f;
            if (acceptX >= mapW)   acceptX = mapW - 1.0f;
            if (acceptY < 0.0f)    acceptY = 0.0f;
            if (acceptY >= mapH)   acceptY = mapH - 1.0f;
            player->_x = acceptX;
            player->_y = acceptY;

            // 좌표 수용으로 섹터가 변경되었을 수 있으므로 재계산
            int32_t newSectorX = zone->GetSectorManager().CalcSectorX(player->_x);
            int32_t newSectorY = zone->GetSectorManager().CalcSectorY(player->_y);
            if (newSectorX != player->_sectorX || newSectorY != player->_sectorY)
            {
                int32_t oldSectorX = player->_sectorX;
                int32_t oldSectorY = player->_sectorY;

                zone->GetSectorManager().RemovePlayer(player, oldSectorX, oldSectorY);
                player->_sectorX = newSectorX;
                player->_sectorY = newSectorY;
                zone->GetSectorManager().AddPlayer(player, newSectorX, newSectorY);

                PushSectorChange(player, oldSectorX, oldSectorY);
            }
        }
        // 범위 초과 시 서버 좌표 유지 (치트 방지)
    }

    // 벽 방향 검증: 벽 위치에서 벽 쪽으로 이동 시도 시 차단
    if (IsBlockedByWall(zone, player, dir))
    {
        SendSyncPosition(player);
        return;
    }

    // 플레이어 상태 갱신
    player->_direction = dir;
    player->_moveState = MoveState::MOVING;

    // 델타 동기화 기준 좌표 갱신 (이동 시작 직후 즉시 동기화 방지)
    player->_lastSyncX = player->_x;
    player->_lastSyncY = player->_y;

    // 주변에 MOVE_START 브로드캐스트 (묶음 모드: dirty 마킹만)
#if USE_SECTOR_AGGREGATION
    MarkMoveDirty(player);
#else
    BroadcastAroundSector(zone, player,
        MakeMoveStart(player->_playerId, static_cast<uint8_t>(dir),
            player->_x, player->_y));
#endif
}

void CGameServer::RecvMoveStop(CPlayer* player, CSerialBuffer* pMsg)
{
    CZone* zone = _mapManager.GetZone(player->_zoneId);
    if (zone == nullptr)
        return;

    MSG_C2S_MOVE_STOP recvMsg;
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), sizeof(recvMsg));

    // 이동 중이 아니면 무시 (서버 벽 클램핑으로 이미 IDLE 처리됨)
    if (player->_moveState != MoveState::MOVING)
        return;

    // Direction 범위 검증 (4방향)
    if (recvMsg.direction < static_cast<uint8_t>(Direction::UP) ||
        recvMsg.direction > static_cast<uint8_t>(Direction::RIGHT))
        return;

    // 서버 권위 모델: 서버 좌표 유지, 상태만 변경
    player->_direction = static_cast<Direction>(recvMsg.direction);
    player->_moveState = MoveState::IDLE;

    // 섹터 이동 판정
    int32_t newSectorX = zone->GetSectorManager().CalcSectorX(player->_x);
    int32_t newSectorY = zone->GetSectorManager().CalcSectorY(player->_y);

    if (newSectorX != player->_sectorX || newSectorY != player->_sectorY)
    {
        int32_t oldSectorX = player->_sectorX;
        int32_t oldSectorY = player->_sectorY;

        // 섹터 데이터 갱신
        zone->GetSectorManager().RemovePlayer(player, oldSectorX, oldSectorY);
        player->_sectorX = newSectorX;
        player->_sectorY = newSectorY;
        zone->GetSectorManager().AddPlayer(player, newSectorX, newSectorY);

        // 시야 진입/이탈 — 대기열에 추가 (틱 끝에 배치 처리)
        PushSectorChange(player, oldSectorX, oldSectorY);
    }

    // 주변에 MOVE_STOP 브로드캐스트 (묶음 모드: dirty 마킹만)
#if USE_SECTOR_AGGREGATION
    MarkMoveDirty(player);
#else
    BroadcastAroundSector(zone, player,
        MakeMoveStop(player->_playerId, static_cast<uint8_t>(player->_direction),
            player->_x, player->_y));
#endif
}

void CGameServer::RecvChat(CPlayer* player, CSerialBuffer* pMsg)
{
    CZone* zone = _mapManager.GetZone(player->_zoneId);
    if (zone == nullptr)
        return;

    // 가변 길이 수신: header.size 기반으로 실제 메시지 길이 역산
    MsgHeader header;
    pMsg->PeekData(reinterpret_cast<char*>(&header), sizeof(header));

    uint16_t recvSize = header.size;
    if (recvSize > sizeof(MSG_C2S_CHAT))
        recvSize = sizeof(MSG_C2S_CHAT);  // 최대 크기 제한

    MSG_C2S_CHAT recvMsg{};
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), recvSize);

    // 메시지 길이 계산 (바이트 → wchar_t 글자 수)
    uint16_t msgBytes = recvSize - sizeof(MsgHeader);
    msgBytes -= msgBytes % sizeof(wchar_t);  // wchar_t 경계 정렬 (홀수 바이트 방어)
    uint16_t msgLen = msgBytes / sizeof(wchar_t);
    if (msgLen > CHAT_MSG_MAX_LEN - 1)
        msgLen = CHAT_MSG_MAX_LEN - 1;
    recvMsg.message[msgLen] = L'\0';  // null 종단 보장

    // 본인 포함 브로드캐스트 (송신 함수가 소유권 소비)
    BroadcastAroundSector(zone, player,
        MakeChat(player->_playerId, player->_displayChar, player->_colorIndex,
            recvMsg.message, msgLen), false);
}

// ==========================================================================
// 패킷 전송 추상화
// ==========================================================================

// ==========================================================================
// 계층 경계 헬퍼
// ==========================================================================

int32_t CGameServer::AllocPlayerId()
{
    return _nextPlayerId++;
}

// playerId → 고유 표시 문자 (A-Z, a-z, 0-9 = 62종)
uint8_t CGameServer::CalcDisplayChar(int32_t playerId)
{
    static constexpr char CHARS[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    return static_cast<uint8_t>(CHARS[static_cast<uint32_t>(playerId) % 62]);
}

// playerId → 고유 색상 인덱스 (0-6, 7종)
uint8_t CGameServer::CalcColorIndex(int32_t playerId)
{
    return static_cast<uint8_t>((static_cast<uint32_t>(playerId) / 62) % 7);
}

// ── 패킷 전송 추상화 ──
// 단일 플레이어에게 패킷 전송 (CSerialBuffer — 소유권 1을 소비)
// 빌더가 반환한 RefCount=1 버퍼를 넘기면, 송신 후 SubRef로 소유권을 회수한다.
void CGameServer::SendPacket(CPlayer* target, CSerialBuffer* pMsg)
{
    if (target->_sessionId != -1)
    {
        pMsg->AddRef();   // 송신용 소유권 — RequestSendMsg(CSerialBuffer*)가 소비
        // 게임루프 송신은 묶어 보낼 수 있음을 표시(Deferred) — 실제 지연 여부는 USE_SEND_COALESCING이 결정
        const int dataSize = pMsg->GetDataSize();
        if (_network->RequestSendMsg(target->_sessionId, pMsg, SendFlush::Deferred))
        {
            InterlockedIncrement64(&_monitor._sendPackets);
            InterlockedExchangeAdd64(&_monitor._sendEnqueuedBytes, static_cast<LONG64>(dataSize));
        }
    }
    pMsg->SubRef();       // 빌더가 넘긴 소유권 1 회수 (세션 무효여도 안전 회수)
}

// 주변 브로드캐스트 (CSerialBuffer — 빌더 RefCount=1 버퍼를 소비)
void CGameServer::BroadcastAroundSector(CZone* zone, CPlayer* player, CSerialBuffer* pMsg, bool excludeSelf)
{
    // [계측] 비용종류별 — gather(주변 모으기) / enqueue(수신자별 복사) 구간을 호출당 2~3회 now()로 분리.
    //   per-call 측정이라 오버헤드가 수신자 수가 아닌 호출 수에 비례. 누적은 틱 끝 1회만 원자반영.
    auto _measGatherT0 = std::chrono::steady_clock::now();

    _broadcastBuffer.clear();
    CPlayer* exclude = excludeSelf ? player : nullptr;
    zone->GetSectorManager().GetAroundPlayers(
        player->_sectorX, player->_sectorY, _broadcastBuffer, exclude);

    auto _measGatherT1 = std::chrono::steady_clock::now();
    _tickBroadcastGatherUs += std::chrono::duration_cast<std::chrono::microseconds>(
        _measGatherT1 - _measGatherT0).count();

    InterlockedIncrement64(&_monitor._gameLoop._broadcastCalls);
    InterlockedExchangeAdd64(&_monitor._gameLoop._broadcastTargets,
        static_cast<LONG64>(_broadcastBuffer.size()));

    // 유효 타겟(세션 보유) 수 선카운트 → 타겟별 AddRef를 1회 배치 AddRef로 압축 (원자연산 N→1)
    // 단일 게임루프 스레드 내 호출이라 두 패스 사이 _sessionId 변동 없음 → 카운트 정합 보장
    size_t validCount = 0;
    for (CPlayer* other : _broadcastBuffer)
    {
        if (other->_sessionId != -1)
            ++validCount;
    }

    if (validCount > 0)
        pMsg->AddRef(static_cast<LONG64>(validCount));   // 타겟별 소유권 일괄 확보

    // 송신 메트릭은 타겟별 원자증가 대신 성공분을 지역 누적 후 1회 반영 (원자연산 N→1)
    const int dataSize = pMsg->GetDataSize();
    size_t sentPkts = 0;
    for (CPlayer* other : _broadcastBuffer)
    {
        if (other->_sessionId == -1)
            continue;
        if (_network->RequestSendMsg(other->_sessionId, pMsg, SendFlush::Deferred))
            ++sentPkts;
    }
    if (sentPkts > 0)
    {
        InterlockedExchangeAdd64(&_monitor._sendPackets, static_cast<LONG64>(sentPkts));
        InterlockedExchangeAdd64(&_monitor._sendEnqueuedBytes,
            static_cast<LONG64>(sentPkts) * dataSize);
    }
    pMsg->SubRef();   // 빌더가 넘긴 소유권 1 회수 (타겟 0명이어도 안전 회수)

    // [계측] enqueue 구간 = gather 직후 ~ 여기 (precount + 배치 AddRef + 수신자별 RequestSendMsg 복사 + 메트릭).
    //   복사가 곧 USE_LOCKFREE_SENDQ 토글이 없애려는 비용 → A/B로 이 값의 변화를 본다.
    auto _measEnqueueT1 = std::chrono::steady_clock::now();
    _tickBroadcastEnqueueUs += std::chrono::duration_cast<std::chrono::microseconds>(
        _measEnqueueT1 - _measGatherT1).count();
}

void CGameServer::SendZoneInfo(CPlayer* target, CZone* zone)
{
    SendPacket(target, MakeZoneInfo(zone));
}

void CGameServer::SendCreateMyPlayer(CPlayer* target)
{
    SendPacket(target, MakeCreateMyPlayer(target));
}

void CGameServer::SendCreateOtherPlayer(CPlayer* target, CPlayer* player, SpawnReason reason)
{
    ++_tickMembershipSends;   // 멤버십 변경 복사 집계 (BroadcastAroundSector 밖 경로)
    SendPacket(target, MakeCreateOtherPlayer(player, reason));
}

void CGameServer::SendDeletePlayer(CPlayer* target, CPlayer* player)
{
    ++_tickMembershipSends;   // 멤버십 변경 복사 집계 (BroadcastAroundSector 밖 경로)
    SendPacket(target, MakeDeletePlayer(player->_playerId));
}

void CGameServer::SendSyncPosition(CPlayer* target)
{
    SendPacket(target, MakeSyncPosition(target->_playerId, target->_x, target->_y));
}

void CGameServer::RecvZoneChange(CPlayer* player, CSerialBuffer* pMsg)
{
    int64_t sessionId = player->_sessionId;
    if (sessionId == -1)
        return;

    CZone* oldZone = _mapManager.GetZone(player->_zoneId);
    if (oldZone == nullptr)
        return;

    MSG_C2S_ZONE_CHANGE recvMsg;
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), sizeof(recvMsg));

    int32_t targetMapId = recvMsg.targetMapId;
    int32_t targetChannelIndex = recvMsg.targetChannelIndex;

    CZone* newZone = nullptr;

    if (targetChannelIndex >= 0)
    {
        // ── 채널 지정 이동 (같은 맵 내) ──
        int32_t currentMapId = CMapManager::GetMapIdFromZoneId(oldZone->GetZoneId());

        newZone = _mapManager.FindChannel(currentMapId, targetChannelIndex);
        if (newZone == nullptr)
        {
            SendZoneChangeFail(player, 0);  // 채널 없음
            return;
        }
        if (newZone->GetZoneId() == oldZone->GetZoneId())
        {
            SendZoneChangeFail(player, 2);  // 이미 해당 채널
            return;
        }

        // 인원 제한 체크 (admin은 스킵)
        if (!player->_isAdmin)
        {
            int32_t maxPlayers = _mapManager.GetMaxPlayersPerChannel(currentMapId);
            if (maxPlayers > 0 && newZone->GetPlayerCount() >= maxPlayers)
            {
                SendZoneChangeFail(player, 1);  // 채널 가득 참
                return;
            }
        }

        targetMapId = currentMapId;
    }
    else
    {
        // ── 기존 맵 이동 (자동 채널 배정) ──

        // 랜덤 맵 이동 요청 처리
        if (targetMapId == -1)
        {
            int32_t currentMapId = CMapManager::GetMapIdFromZoneId(oldZone->GetZoneId());
            targetMapId = _mapManager.GetRandomMapId(currentMapId);
            if (targetMapId == -1)
            {
                SendZoneChangeFail(player, 0);
                return;
            }
        }

        newZone = _mapManager.FindOrCreateChannel(targetMapId, player->_isAdmin);
        if (newZone == nullptr)
        {
            SendZoneChangeFail(player, 0);
            return;
        }
    }

    // ── 현재 존에서 퇴장 ──
    BroadcastLeaveZone(oldZone, player);

    // ── 새 존에 입장 (player 객체 재활용, playerId 유지) ──

    if (!newZone->EnterZone(player))
    {
        // 입장 실패 시 원래 맵의 여유 채널로 복귀 시도
        CZone* fallback = _mapManager.FindOrCreateChannel(CMapManager::GetMapIdFromZoneId(oldZone->GetZoneId()));
        if (fallback != nullptr && fallback->EnterZone(player))
        {
            SendZoneChangeFail(player, 1);

            // 델타 동기화 기준 좌표 초기화
            player->_lastSyncX = player->_x;
            player->_lastSyncY = player->_y;

            // 복귀한 존에서 본인 + 주변 상호 통보
            SendZoneInfo(player, fallback);
            SendCreateMyPlayer(player);
            BroadcastEnterZone(fallback, player, SpawnReason::NORMAL);

            return;
        }

        // 복귀도 실패 → 좀비 방지를 위해 연결 해제
        _sessionToPlayer.erase(sessionId);
        delete player;
        _network->RequestDisconnectSession(sessionId);
        return;
    }

    InterlockedIncrement64(&_monitor._gameLoop._zoneChangeCount);

    // 델타 동기화 기준 좌표 초기화
    player->_lastSyncX = player->_x;
    player->_lastSyncY = player->_y;

    // 존 메타 정보 + 존 이동 성공 통보
    SendZoneInfo(player, newZone);
    int32_t channelIndex = CMapManager::GetChannelIndexFromZoneId(newZone->GetZoneId());
    SendZoneChangeOk(player, targetMapId, channelIndex);

    // 주변 상호 CREATE 브로드캐스트
    BroadcastEnterZone(newZone, player, SpawnReason::ZONE_TRANSFER);
}

// ── 운영자 인증 ──

static constexpr char ADMIN_KEY[] = "admin1234";  // 운영자 인증 키

void CGameServer::RecvAdminLogin(CPlayer* player, CSerialBuffer* pMsg)
{
    MSG_C2S_ADMIN_LOGIN recvMsg;
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), sizeof(recvMsg));

    // null-terminate 보장
    recvMsg.key[ADMIN_KEY_MAX_LEN - 1] = '\0';

    if (strcmp(recvMsg.key, ADMIN_KEY) == 0)
    {
        player->_isAdmin = true;
        SendPacket(player, MakeAdminLoginOk());
    }
    else
    {
        SendPacket(player, MakeAdminLoginFail());
    }
}

void CGameServer::SendZoneChangeOk(CPlayer* target, int32_t mapId, int32_t channelIndex)
{
    SendPacket(target, MakeZoneChangeOk(target, mapId, channelIndex));
}

void CGameServer::SendZoneChangeFail(CPlayer* target, uint8_t reason)
{
    SendPacket(target, MakeZoneChangeFail(reason));
}

// ==========================================================================
// 벽 방향 검증 — 경계 위치에서 벽 쪽 이동 차단 (클라이언트 IsBlockedByWall과 동일)
// ==========================================================================

bool CGameServer::IsBlockedByWall(CZone* zone, CPlayer* player, Direction dir)
{
    float maxX = static_cast<float>(zone->GetMapWidth()) - 1.0f;
    float maxY = static_cast<float>(zone->GetMapHeight()) - 1.0f;

    bool atLeft   = (player->_x <= 0.0f);
    bool atRight  = (player->_x >= maxX);
    bool atTop    = (player->_y <= 0.0f);
    bool atBottom = (player->_y >= maxY);

    switch (dir)
    {
    case Direction::LEFT:  return atLeft;
    case Direction::RIGHT: return atRight;
    case Direction::UP:    return atTop;
    case Direction::DOWN:  return atBottom;
    default: return false;
    }
}

// ==========================================================================
// 존 입장/퇴장 브로드캐스트
// ==========================================================================

void CGameServer::BroadcastEnterZone(CZone* zone, CPlayer* player, SpawnReason reason)
{
    _eventAroundBuffer.clear();
    zone->GetSectorManager().GetAroundPlayers(
        player->_sectorX, player->_sectorY, _eventAroundBuffer, player);

    for (CPlayer* other : _eventAroundBuffer)
    {
        SendCreateOtherPlayer(other, player, reason);  // 기존 플레이어에게 신규 플레이어 생성
        SendCreateOtherPlayer(player, other);           // 신규 플레이어에게 기존 플레이어 생성
    }
}

void CGameServer::BroadcastLeaveZone(CZone* zone, CPlayer* player)
{
    // 현재 섹터 기준 주변 DELETE
    _eventAroundBuffer.clear();
    zone->GetSectorManager().GetAroundPlayers(
        player->_sectorX, player->_sectorY, _eventAroundBuffer, player);

    for (CPlayer* other : _eventAroundBuffer)
    {
        SendDeletePlayer(other, player);
    }

    // 미처리 섹터 변경이 있으면 이전 섹터 전용 뷰어에게도 DELETE
    // (같은 프레임에 섹터 변경 + 존 이동/접속해제 시 고스트 방지)
    if (_sectorChangedSet.count(player))
    {
        for (const auto& change : _pendingSectorChanges)
        {
            if (change.player != player)
                continue;

            // 이전 섹터 전용 = old 주변에만 있고 현재 주변에는 없는 섹터
            CSectorManager::SectorPos added[CSectorManager::MAX_AROUND_SECTORS];
            CSectorManager::SectorPos removed[CSectorManager::MAX_AROUND_SECTORS];
            int32_t addedCount = 0;
            int32_t removedCount = 0;

            zone->GetSectorManager().GetSectorDiff(
                player->_sectorX, player->_sectorY,    // 현재 섹터 (이미 갱신된 값)
                change.oldSectorX, change.oldSectorY,   // 이전 섹터
                added, addedCount,                       // old 전용 섹터
                removed, removedCount);

            for (int32_t i = 0; i < addedCount; ++i)
            {
                const auto& players = zone->GetSectorManager().GetSectorPlayers(
                    added[i].x, added[i].y);
                for (CPlayer* other : players)
                {
                    SendDeletePlayer(other, player);
                }
            }
            break;
        }
    }

#if USE_SECTOR_AGGREGATION
    // 섹터 묶음 dirty에서도 제거 (틱 끝 FlushSectorUpdates dangling 방지 — 같은 틱 move→퇴장/존이동)
    if (player->_moveDirty)
    {
        player->_moveDirty = false;
        _dirtyMovers.erase(std::remove(_dirtyMovers.begin(), _dirtyMovers.end(), player),
            _dirtyMovers.end());
    }
#endif

    // 섹터 변경 대기열에서 제거
    _sectorChangedSet.erase(player);
    _pendingSectorChanges.erase(
        std::remove_if(_pendingSectorChanges.begin(), _pendingSectorChanges.end(),
            [player](const SectorChangeInfo& c) { return c.player == player; }),
        _pendingSectorChanges.end());

    zone->LeaveZone(player);
}

// ==========================================================================
// 섹터 변경 대기열 삽입 — 같은 플레이어는 최초 출발 섹터만 기록
// ==========================================================================

void CGameServer::PushSectorChange(CPlayer* player, int32_t oldSectorX, int32_t oldSectorY)
{
    if (!_sectorChangedSet.insert(player).second)
        return;  // 이미 기록됨 → 최초 출발 섹터 유지

    _pendingSectorChanges.push_back({ player, oldSectorX, oldSectorY });
}

// ==========================================================================
// 섹터 변경 브로드캐스트
// ==========================================================================

void CGameServer::ProcessSectorChange(CZone* zone, CPlayer* player,
                                      int32_t oldSectorX, int32_t oldSectorY)
{
    CSectorManager::SectorPos added[CSectorManager::MAX_AROUND_SECTORS];
    CSectorManager::SectorPos removed[CSectorManager::MAX_AROUND_SECTORS];
    int32_t addedCount = 0;
    int32_t removedCount = 0;
    zone->GetSectorManager().GetSectorDiff(
        oldSectorX, oldSectorY,
        player->_sectorX, player->_sectorY,
        added, addedCount, removed, removedCount);

    // 이탈 섹터 — 상호 DELETE
    for (int32_t i = 0; i < removedCount; ++i)
    {
        const auto& players = zone->GetSectorManager().GetSectorPlayers(removed[i].x, removed[i].y);
        for (CPlayer* other : players)
        {
            SendDeletePlayer(other, player);  // 상대에게 나를 삭제
            SendDeletePlayer(player, other);   // 나에게 상대를 삭제
        }
    }

    // 진입 섹터 — 상호 CREATE
    for (int32_t i = 0; i < addedCount; ++i)
    {
        const auto& players = zone->GetSectorManager().GetSectorPlayers(added[i].x, added[i].y);
        for (CPlayer* other : players)
        {
            if (other == player)
                continue;  // 멀티섹터 점프 시 자기 자신 방지

            SendCreateOtherPlayer(other, player);  // 상대에게 나를 생성
            SendCreateOtherPlayer(player, other);   // 나에게 상대를 생성
        }
    }
}

#if USE_SECTOR_AGGREGATION
// ==========================================================================
// 섹터 묶음 (USE_SECTOR_AGGREGATION)
// ==========================================================================

// 즉시 브로드캐스트 대신 dirty 등록 — 같은 플레이어는 틱당 1회만.
// 묶음은 틱 끝 최종 상태를 읽으므로 여기선 등록만 (상태는 호출 직전에 이미 갱신됨).
void CGameServer::MarkMoveDirty(CPlayer* player)
{
    if (player->_moveDirty)
        return;
    player->_moveDirty = true;
    _dirtyMovers.push_back(player);
}

// 틱 끝: dirty 플레이어를 (zone, sector)별로 묶어 그 섹터 주변에 송신.
void CGameServer::FlushSectorUpdates()
{
    if (_dirtyMovers.empty())
        return;

    // [계측] 묶음 빌드+송신 전체를 enqueue 축에 합산 (baseline의 broadcast_enqueue와 같은 비용종류로 A/B 비교)
    auto measT0 = std::chrono::steady_clock::now();

    // (zoneId, sectorY, sectorX)로 정렬 → 같은 섹터가 연속 구간이 되어 그룹 단위로 처리
    std::sort(_dirtyMovers.begin(), _dirtyMovers.end(),
        [](CPlayer* a, CPlayer* b)
        {
            if (a->_zoneId != b->_zoneId)   return a->_zoneId < b->_zoneId;
            if (a->_sectorY != b->_sectorY) return a->_sectorY < b->_sectorY;
            return a->_sectorX < b->_sectorX;
        });

    const size_t total = _dirtyMovers.size();
    size_t i = 0;
    while (i < total)
    {
        CPlayer* head = _dirtyMovers[i];
        const int32_t zoneId = head->_zoneId;
        const int32_t sx = head->_sectorX;
        const int32_t sy = head->_sectorY;

        // head와 같은 섹터가 이어지는 끝(j)을 찾는다 → [i, j)가 한 섹터 묶음
        size_t j = i;
        while (j < total &&  // 배열 끝을 넘지 않는 동안
            _dirtyMovers[j]->_zoneId == zoneId &&  // j번째가 기준과 같은 zone이고
            _dirtyMovers[j]->_sectorX == sx &&     // 같은 sectorX이고
            _dirtyMovers[j]->_sectorY == sy)       // 같은 sectorY이면
            ++j;

        // [i, j) 묶음을 SECTOR_UPDATE_MAX_ENTRIES씩 잘라 패킷화 → 섹터(sx,sy) 주변에 broadcast
        CZone* zone = _mapManager.GetZone(zoneId);
        if (zone != nullptr)
        {
            // 한 섹터를 SECTOR_UPDATE_MAX_ENTRIES씩 끊어 송신 (버퍼 한계 가드 — 균등 부하엔 1청크)
            size_t k = i;
            while (k < j)
            {
                int chunk = static_cast<int>((std::min)(j - k, static_cast<size_t>(SECTOR_UPDATE_MAX_ENTRIES)));
                CSerialBuffer* buf = MakeSectorUpdates(&_dirtyMovers[k], chunk);
                BroadcastSectorPacket(zone, sx, sy, buf);
                k += chunk;
            }
        }
        i = j;
    }

    // dirty 리셋 (다음 틱 이월 없음)
    for (CPlayer* p : _dirtyMovers)
        p->_moveDirty = false;
    _dirtyMovers.clear();

    auto measT1 = std::chrono::steady_clock::now();
    _tickBroadcastEnqueueUs += std::chrono::duration_cast<std::chrono::microseconds>(measT1 - measT0).count();
}

// 섹터 좌표 기준 주변 9섹터에 묶음 패킷 전송.
// BroadcastAroundSector의 소유권(배치 AddRef)·송신 메트릭 패턴을 그대로 복제 (본인 포함).
void CGameServer::BroadcastSectorPacket(CZone* zone, int32_t sectorX, int32_t sectorY, CSerialBuffer* pMsg)
{
    _broadcastBuffer.clear();
    zone->GetSectorManager().GetAroundPlayers(sectorX, sectorY, _broadcastBuffer, nullptr);

    InterlockedIncrement64(&_monitor._gameLoop._broadcastCalls);
    InterlockedExchangeAdd64(&_monitor._gameLoop._broadcastTargets,
        static_cast<LONG64>(_broadcastBuffer.size()));

    // 유효 타겟(세션 보유) 선카운트 → 배치 AddRef (원자연산 N→1)
    size_t validCount = 0;
    for (CPlayer* other : _broadcastBuffer)
    {
        if (other->_sessionId != -1)
            ++validCount;
    }
    if (validCount > 0)
        pMsg->AddRef(static_cast<LONG64>(validCount));

    const int dataSize = pMsg->GetDataSize();
    size_t sentPkts = 0;
    for (CPlayer* other : _broadcastBuffer)
    {
        if (other->_sessionId == -1)
            continue;
        if (_network->RequestSendMsg(other->_sessionId, pMsg, SendFlush::Deferred))
            ++sentPkts;
    }
    if (sentPkts > 0)
    {
        InterlockedExchangeAdd64(&_monitor._sendPackets, static_cast<LONG64>(sentPkts));
        InterlockedExchangeAdd64(&_monitor._sendEnqueuedBytes,
            static_cast<LONG64>(sentPkts) * dataSize);
    }
    pMsg->SubRef();   // 빌더가 넘긴 소유권 1 회수 (타겟 0명이어도 안전 회수)
}
#endif // USE_SECTOR_AGGREGATION
