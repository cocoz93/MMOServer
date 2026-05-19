#include "Zone.h"
#include "Player.h"

CZone::CZone()
    : _zoneId(-1)
    , _mapId(-1)
    , _mapWidth(0)
    , _mapHeight(0)
{
}

CZone::~CZone()
{
    // CPlayer의 생명주기는 CGameServer가 관리
    _players.clear();
}

bool CZone::Init(int32_t zoneId, int32_t mapId, int32_t mapWidth, int32_t mapHeight, int32_t sectorSize)
{
    if (!_sectorManager.Init(mapWidth, mapHeight, sectorSize))
        return false;

    _zoneId = zoneId;
    _mapId = mapId;
    _mapWidth = mapWidth;
    _mapHeight = mapHeight;
    return true;
}

bool CZone::EnterZone(CPlayer* player)
{
    if (player == nullptr)
        return false;

    // 존 설정 (playerId, 표시 속성은 CGameServer가 부여 완료 상태)
    player->_zoneId = _zoneId;
    player->_direction = Direction::DOWN;
    player->_moveState = MoveState::IDLE;

    // 스폰 좌표 설정
    CalcSpawnPos(player->_x, player->_y);

    // 섹터 좌표 계산 및 등록
    player->_sectorX = _sectorManager.CalcSectorX(player->_x);
    player->_sectorY = _sectorManager.CalcSectorY(player->_y);
    _sectorManager.AddPlayer(player, player->_sectorX, player->_sectorY);

    // 플레이어 목록에 추가 (playerId 키)
    _players[player->_playerId] = player;

    return true;
}

void CZone::LeaveZone(int32_t playerId)
{
    auto it = _players.find(playerId);
    if (it == _players.end())
        return;

    CPlayer* player = it->second;

    // 섹터 해제
    _sectorManager.RemovePlayer(player, player->_sectorX, player->_sectorY);

    // 맵에서 제거 (delete는 CGameServer가 담당)
    _players.erase(it);
}

void CZone::Tick(float deltaTime, std::vector<SectorChangeInfo>& outSectorChanges,
                 std::vector<CPlayer*>& outClampedPlayers)
{
    for (auto& pair : _players)
    {
        CPlayer* player = pair.second;
        if (player->_moveState != MoveState::MOVING)
            continue;

        // 방향에 따라 좌표 갱신 (플레이어별 속도)
        static constexpr float DIAGONAL_FACTOR = 0.7071f; // 1/√2
        float dist = player->_speed * deltaTime;
        switch (player->_direction)
        {
        case Direction::UP:         player->_y -= dist; break;
        case Direction::DOWN:       player->_y += dist; break;
        case Direction::LEFT:       player->_x -= dist; break;
        case Direction::RIGHT:      player->_x += dist; break;
        case Direction::UP_LEFT:    player->_x -= dist * DIAGONAL_FACTOR; player->_y -= dist * DIAGONAL_FACTOR; break;
        case Direction::UP_RIGHT:   player->_x += dist * DIAGONAL_FACTOR; player->_y -= dist * DIAGONAL_FACTOR; break;
        case Direction::DOWN_LEFT:  player->_x -= dist * DIAGONAL_FACTOR; player->_y += dist * DIAGONAL_FACTOR; break;
        case Direction::DOWN_RIGHT: player->_x += dist * DIAGONAL_FACTOR; player->_y += dist * DIAGONAL_FACTOR; break;
        default: break;
        }

        // 맵 경계 클램핑
        bool clamped = false;
        if (player->_x < 0.0f)          { player->_x = 0.0f; clamped = true; }
        if (player->_x >= _mapWidth)     { player->_x = static_cast<float>(_mapWidth) - 1.0f; clamped = true; }
        if (player->_y < 0.0f)          { player->_y = 0.0f; clamped = true; }
        if (player->_y >= _mapHeight)    { player->_y = static_cast<float>(_mapHeight) - 1.0f; clamped = true; }

        // 경계에 닿으면 정지 → 클라이언트에 MOVE_STOP 통보 필요
        if (clamped)
        {
            player->_moveState = MoveState::IDLE;
            outClampedPlayers.push_back(player);
        }

        // 섹터 변경 판정
        int32_t newSectorX = _sectorManager.CalcSectorX(player->_x);
        int32_t newSectorY = _sectorManager.CalcSectorY(player->_y);

        if (newSectorX != player->_sectorX || newSectorY != player->_sectorY)
        {
            int32_t oldSectorX = player->_sectorX;
            int32_t oldSectorY = player->_sectorY;

            // 섹터 데이터 갱신
            _sectorManager.RemovePlayer(player, oldSectorX, oldSectorY);
            player->_sectorX = newSectorX;
            player->_sectorY = newSectorY;
            _sectorManager.AddPlayer(player, newSectorX, newSectorY);

            outSectorChanges.push_back({ player, oldSectorX, oldSectorY });
        }
    }
}

CPlayer* CZone::FindPlayer(int32_t playerId) const
{
    auto it = _players.find(playerId);
    if (it == _players.end())
        return nullptr;
    return it->second;
}

void CZone::CalcSpawnPos(float& outX, float& outY) const
{
    // 맵 중앙 스폰
    outX = static_cast<float>(_mapWidth / 2);
    outY = static_cast<float>(_mapHeight / 2);
}

