#include "SectorManager.h"
#include "Player.h"
#include <algorithm>

const std::vector<CPlayer*> CSectorManager::EMPTY_SECTOR;

CSectorManager::CSectorManager()
    : _mapWidth(0)
    , _mapHeight(0)
    , _sectorSize(0)
    , _sectorCountX(0)
    , _sectorCountY(0)
{
}

CSectorManager::~CSectorManager()
{
}

bool CSectorManager::Init(int32_t mapWidth, int32_t mapHeight, int32_t sectorSize)
{
    if (mapWidth <= 0 || mapHeight <= 0 || sectorSize <= 0)
        return false;

    _mapWidth = mapWidth;
    _mapHeight = mapHeight;
    _sectorSize = sectorSize;
    _sectorCountX = (mapWidth + sectorSize - 1) / sectorSize;
    _sectorCountY = (mapHeight + sectorSize - 1) / sectorSize;

    // 2D 그리드 할당
    _sectors.resize(_sectorCountY);
    for (int32_t y = 0; y < _sectorCountY; ++y)
    {
        _sectors[y].resize(_sectorCountX);
    }

    return true;
}

int32_t CSectorManager::CalcSectorX(float worldX) const
{
    int32_t sx = static_cast<int32_t>(worldX) / _sectorSize;

    if (sx < 0) sx = 0;
    if (sx >= _sectorCountX) sx = _sectorCountX - 1;

    return sx;
}

int32_t CSectorManager::CalcSectorY(float worldY) const
{
    int32_t sy = static_cast<int32_t>(worldY) / _sectorSize;

    if (sy < 0) sy = 0;
    if (sy >= _sectorCountY) sy = _sectorCountY - 1;

    return sy;
}

void CSectorManager::AddPlayer(CPlayer* player, int32_t sectorX, int32_t sectorY)
{
    if (!IsValidSector(sectorX, sectorY))
        return;

    _sectors[sectorY][sectorX].push_back(player);
}

void CSectorManager::RemovePlayer(CPlayer* player, int32_t sectorX, int32_t sectorY)
{
    if (!IsValidSector(sectorX, sectorY))
        return;

    auto& players = _sectors[sectorY][sectorX];
    auto it = std::find(players.begin(), players.end(), player);
    if (it != players.end())
    {
        // 순서 보존 불필요 → O(1) 삭제
        *it = players.back();
        players.pop_back();
    }
}

const std::vector<CPlayer*>& CSectorManager::GetSectorPlayers(int32_t sectorX, int32_t sectorY) const
{
    if (!IsValidSector(sectorX, sectorY))
        return EMPTY_SECTOR;

    return _sectors[sectorY][sectorX];
}

void CSectorManager::GetAroundPlayers(int32_t sectorX, int32_t sectorY,
                                      std::vector<CPlayer*>& outPlayers,
                                      CPlayer* exclude) const
{
    outPlayers.clear();

    std::vector<SectorPos> aroundSectors;
    GetAroundSectorList(sectorX, sectorY, aroundSectors);

    for (const auto& pos : aroundSectors)
    {
        const auto& players = _sectors[pos.y][pos.x];
        for (CPlayer* p : players)
        {
            if (p != exclude)
            {
                outPlayers.push_back(p);
            }
        }
    }
}

void CSectorManager::GetSectorDiff(int32_t oldSectorX, int32_t oldSectorY,
                                   int32_t newSectorX, int32_t newSectorY,
                                   std::vector<SectorPos>& outAdded,
                                   std::vector<SectorPos>& outRemoved) const
{
    outAdded.clear();
    outRemoved.clear();

    std::vector<SectorPos> oldList;
    std::vector<SectorPos> newList;
    GetAroundSectorList(oldSectorX, oldSectorY, oldList);
    GetAroundSectorList(newSectorX, newSectorY, newList);

    // newList에 있지만 oldList에 없는 것 → added
    for (const auto& ns : newList)
    {
        bool found = false;
        for (const auto& os : oldList)
        {
            if (ns.x == os.x && ns.y == os.y)
            {
                found = true;
                break;
            }
        }
        if (!found)
            outAdded.push_back(ns);
    }

    // oldList에 있지만 newList에 없는 것 → removed
    for (const auto& os : oldList)
    {
        bool found = false;
        for (const auto& ns : newList)
        {
            if (os.x == ns.x && os.y == ns.y)
            {
                found = true;
                break;
            }
        }
        if (!found)
            outRemoved.push_back(os);
    }
}

bool CSectorManager::IsValidSector(int32_t sectorX, int32_t sectorY) const
{
    return sectorX >= 0 && sectorX < _sectorCountX
        && sectorY >= 0 && sectorY < _sectorCountY;
}

void CSectorManager::GetAroundSectorList(int32_t sectorX, int32_t sectorY,
                                         std::vector<SectorPos>& outSectors) const
{
    outSectors.clear();

    for (int32_t dy = -1; dy <= 1; ++dy)
    {
        for (int32_t dx = -1; dx <= 1; ++dx)
        {
            int32_t nx = sectorX + dx;
            int32_t ny = sectorY + dy;

            if (IsValidSector(nx, ny))
            {
                outSectors.push_back({ nx, ny });
            }
        }
    }
}
