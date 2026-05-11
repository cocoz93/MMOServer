#include "ZoneManager.h"

CZoneManager::CZoneManager()
{
}

CZoneManager::~CZoneManager()
{
}

bool CZoneManager::RegisterMap(const MapConfig& config)
{
    // 중복 맵 등록 방지
    if (_mapConfigs.find(config.mapId) != _mapConfigs.end())
        return false;

    _mapConfigs[config.mapId] = config;
    _mapChannels[config.mapId] = std::vector<int32_t>();

    // 기본 채널(channelIndex=0) 자동 생성
    if (CreateChannel(config.mapId, 0) == nullptr)
        return false;

    return true;
}

CZone* CZoneManager::FindOrCreateChannel(int32_t mapId)
{
    auto configIt = _mapConfigs.find(mapId);
    if (configIt == _mapConfigs.end())
        return nullptr;

    const MapConfig& config = configIt->second;
    auto& channels = _mapChannels[mapId];

    // 여유 있는 기존 채널 탐색
    for (int32_t zoneId : channels)
    {
        CZone* zone = GetZone(zoneId);
        if (zone != nullptr && zone->GetPlayerCount() < config.maxPlayersPerChannel)
            return zone;
    }

    // 모든 채널이 가득 참 — 동적 채널 생성
    int32_t newChannelIndex = static_cast<int32_t>(channels.size());
    CZone* newZone = CreateChannel(mapId, newChannelIndex);
    return newZone;
}

void CZoneManager::CleanupEmptyChannels()
{
    for (auto& pair : _mapChannels)
    {
        auto& channels = pair.second;

        // channelIndex=0 (기본 채널)은 제거하지 않음
        // 뒤에서부터 순회하여 빈 동적 채널 제거
        for (int i = static_cast<int>(channels.size()) - 1; i >= 1; --i)
        {
            int32_t zoneId = channels[i];
            CZone* zone = GetZone(zoneId);
            if (zone != nullptr && zone->GetPlayerCount() == 0)
            {
                _zones.erase(zoneId);
                channels.erase(channels.begin() + i);
            }
        }
    }
}

CZone* CZoneManager::GetZone(int32_t zoneId)
{
    auto it = _zones.find(zoneId);
    if (it == _zones.end())
        return nullptr;
    return it->second.get();
}

void CZoneManager::RegisterSession(int64_t sessionId, int32_t zoneId)
{
    _sessionToZone[sessionId] = zoneId;
}

void CZoneManager::UnregisterSession(int64_t sessionId)
{
    _sessionToZone.erase(sessionId);
}

CZone* CZoneManager::FindZoneBySession(int64_t sessionId)
{
    auto it = _sessionToZone.find(sessionId);
    if (it == _sessionToZone.end())
        return nullptr;
    return GetZone(it->second);
}

void CZoneManager::TickAll(float deltaTime, std::vector<SectorChangeInfo>& outSectorChanges,
                           std::vector<CPlayer*>& outClampedPlayers)
{
    for (auto& pair : _zones)
    {
        pair.second->Tick(deltaTime, outSectorChanges, outClampedPlayers);
    }
}

int32_t CZoneManager::MakeZoneId(int32_t mapId, int32_t channelIndex)
{
    return mapId * 1000 + channelIndex;
}

int32_t CZoneManager::GetMapIdFromZoneId(int32_t zoneId)
{
    return zoneId / 1000;
}

int32_t CZoneManager::GetChannelIndexFromZoneId(int32_t zoneId)
{
    return zoneId % 1000;
}

CZone* CZoneManager::CreateChannel(int32_t mapId, int32_t channelIndex)
{
    auto configIt = _mapConfigs.find(mapId);
    if (configIt == _mapConfigs.end())
        return nullptr;

    const MapConfig& config = configIt->second;
    int32_t zoneId = MakeZoneId(mapId, channelIndex);

    // 중복 방지
    if (_zones.find(zoneId) != _zones.end())
        return nullptr;

    auto zone = std::make_unique<CZone>();
    if (!zone->Init(zoneId, config.mapId, config.mapWidth, config.mapHeight, config.sectorSize))
        return nullptr;

    CZone* ptr = zone.get();
    _zones[zoneId] = std::move(zone);
    _mapChannels[mapId].push_back(zoneId);

    return ptr;
}
