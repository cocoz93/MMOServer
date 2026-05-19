// ==========================================================================
// CZoneManager — 존 컨테이너 + 맵/채널 관리
//
// [책임]
//  - 맵 설정 등록 및 맵별 채널(존) 관리
//  - 채널 자동 배정 (인원 초과 시 동적 생성)
//  - 빈 동적 채널 자동 정리
//
// [설계]
//  - 싱글스레드 전제 (게임 루프 스레드에서만 호출)
//  - GameServer가 소유, Zone은 ZoneManager가 소유
//  - zoneId 인코딩: mapId * 1000 + channelIndex
// ==========================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Zone.h"

// 맵 설정 데이터
struct MapConfig
{
    int32_t mapId;
    int32_t mapWidth;
    int32_t mapHeight;
    int32_t sectorSize;
    int32_t maxPlayersPerChannel;  // 채널당 최대 인원 (소프트 리밋)
};

class CZoneManager
{
public:
    CZoneManager();
    ~CZoneManager();

    // 맵 등록 (서버 시작 시) — 기본 채널(channelIndex=0) 자동 생성
    bool RegisterMap(const MapConfig& config);

    // 채널 자동 배정 — 여유 있는 채널 반환, 없으면 동적 생성
    CZone* FindOrCreateChannel(int32_t mapId);

    // 빈 동적 채널 정리 (channelIndex > 0이고 플레이어 0명)
    void CleanupEmptyChannels();

    // 존 조회
    CZone* GetZone(int32_t zoneId);

    // 전체 존 Tick — 섹터 변경 및 경계 클램핑 수집
    void TickAll(float deltaTime, std::vector<SectorChangeInfo>& outSectorChanges,
                 std::vector<CPlayer*>& outClampedPlayers);

    // 현재 맵을 제외한 랜덤 맵 ID 반환 (-1이면 선택 불가)
    int32_t GetRandomMapId(int32_t excludeMapId) const;

    // 전체 존 순회
    const std::unordered_map<int32_t, std::unique_ptr<CZone>>& GetZones() const { return _zones; }

    // zoneId 인코딩/디코딩 헬퍼
    static int32_t MakeZoneId(int32_t mapId, int32_t channelIndex);
    static int32_t GetMapIdFromZoneId(int32_t zoneId);
    static int32_t GetChannelIndexFromZoneId(int32_t zoneId);

private:
    // 채널 생성 내부 함수
    CZone* CreateChannel(int32_t mapId, int32_t channelIndex);

    // zoneId → CZone
    std::unordered_map<int32_t, std::unique_ptr<CZone>> _zones;

    // mapId → 맵 설정
    std::unordered_map<int32_t, MapConfig> _mapConfigs;

    // mapId → [zoneId 목록] (채널 관리)
    std::unordered_map<int32_t, std::vector<int32_t>> _mapChannels;
};
