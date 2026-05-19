// ==========================================================================
// CZone — 존 단위 데이터 컨테이너
//
// [책임]
//  - 맵 크기, CSectorManager, 플레이어 목록 소유
//  - 플레이어 입장/퇴장, 섹터 등록/해제 등 데이터 관리
//  - 게임 로직(패킷 처리, 브로드캐스트)은 CGameServer가 담당
//
// [설계]
//  - 싱글스레드 전제 (게임 로직 스레드 1개에서만 호출)
//  - 스폰 좌표는 맵 중앙 고정 (추후 랜덤/지정 스폰 확장 가능)
// ==========================================================================
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "SectorManager.h"

class CPlayer;

// Tick에서 섹터 변경이 발생한 플레이어 정보
struct SectorChangeInfo
{
    CPlayer* player;
    int32_t oldSectorX;
    int32_t oldSectorY;
};

class CZone
{
public:
    CZone();
    ~CZone();

    // 존 초기화 (존 ID, 맵 ID, 맵 크기, 섹터 크기)
    bool Init(int32_t zoneId, int32_t mapId, int32_t mapWidth, int32_t mapHeight, int32_t sectorSize);

    // 플레이어 입장 — 스폰 좌표 설정, 섹터 등록
    // playerId와 CPlayer의 생성/삭제는 CGameServer가 담당
    bool EnterZone(CPlayer* player);

    // 플레이어 퇴장 — 섹터 해제, _players 맵에서 제거 (delete하지 않음)
    void LeaveZone(int32_t playerId);

    // 프레임 갱신 — MOVING 플레이어 좌표 갱신, 섹터 변경 감지
    // outClampedPlayers: 맵 경계 클램핑으로 IDLE 전환된 플레이어 (MOVE_STOP 브로드캐스트 필요)
    void Tick(float deltaTime, std::vector<SectorChangeInfo>& outSectorChanges,
             std::vector<CPlayer*>& outClampedPlayers);

    // 플레이어 조회 (playerId 기반)
    CPlayer* FindPlayer(int32_t playerId) const;

    // 존 정보
    int32_t GetZoneId() const { return _zoneId; }
    int32_t GetMapId() const { return _mapId; }
    int32_t GetMapWidth() const { return _mapWidth; }
    int32_t GetMapHeight() const { return _mapHeight; }
    int32_t GetPlayerCount() const { return static_cast<int32_t>(_players.size()); }
    const std::unordered_map<int32_t, CPlayer*>& GetPlayers() const { return _players; }

    // 섹터매니저 접근 (이동 처리 등 외부에서 필요)
    CSectorManager& GetSectorManager() { return _sectorManager; }
    const CSectorManager& GetSectorManager() const { return _sectorManager; }

private:
    // 스폰 좌표 계산 (현재: 맵 중앙)
    void CalcSpawnPos(float& outX, float& outY) const;

    int32_t _zoneId;
    int32_t _mapId;
    int32_t _mapWidth;
    int32_t _mapHeight;

    CSectorManager _sectorManager;

    // playerId → CPlayer*
    std::unordered_map<int32_t, CPlayer*> _players;
};
