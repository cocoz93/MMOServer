// ==========================================================================
// CSectorManager — 존 내부 2D 그리드 섹터 시스템
//
// [설계 기준]
//  - 콘솔 클라이언트 80x25 기준, 게임 뷰 80x20 타일
//  - 섹터 크기 20x20 (정사각형), 맵 크기 200x200 → 10x10 그리드 (100섹터)
//  - 주변 9섹터 순회 시 커버 범위 60x60 → 클라 뷰(80x20)보다 넉넉
//  - 섹터 크기 ≈ 클라 화면 반절, 시야보다 크게 잡아 화면 내 팝업 방지
//
// [사용 흐름]
//  Init(200, 200, 20) → 존 생성 시 호출
//  CalcSectorX/Y()    → 월드 좌표에서 섹터 좌표 계산
//  AddPlayer/Remove   → 섹터에 플레이어 등록/해제
//  GetAroundPlayers() → 브로드캐스트 대상 수집
//  GetSectorDiff()    → 섹터 변경 시 시야 진입/이탈 섹터 계산 (CREATE/DELETE용)
// ==========================================================================
#pragma once

#include <cstdint>
#include <vector>

class CPlayer;

class CSectorManager
{
public:
    CSectorManager();
    ~CSectorManager();

    // 초기화 (맵 크기, 섹터 크기)
    bool Init(int32_t mapWidth, int32_t mapHeight, int32_t sectorSize);

    // 월드 좌표 → 섹터 좌표 변환
    int32_t CalcSectorX(float worldX) const;
    int32_t CalcSectorY(float worldY) const;

    // 플레이어 등록/해제
    void AddPlayer(CPlayer* player, int32_t sectorX, int32_t sectorY);
    void RemovePlayer(CPlayer* player, int32_t sectorX, int32_t sectorY);

    // 단일 섹터의 플레이어 목록
    const std::vector<CPlayer*>& GetSectorPlayers(int32_t sectorX, int32_t sectorY) const;

    // 주변 9섹터(본인 섹터 포함) 플레이어 수집
    // exclude가 지정되면 해당 플레이어는 제외
    void GetAroundPlayers(int32_t sectorX, int32_t sectorY,
                          std::vector<CPlayer*>& outPlayers,
                          CPlayer* exclude = nullptr) const;

    // 섹터 이동 시 새로 시야에 들어온/나간 섹터 목록 계산
    // oldSector 주변에는 있었지만 newSector 주변에는 없는 섹터 → outRemoved
    // newSector 주변에는 있지만 oldSector 주변에는 없던 섹터 → outAdded
    struct SectorPos { int32_t x; int32_t y; };
    void GetSectorDiff(int32_t oldSectorX, int32_t oldSectorY,
                       int32_t newSectorX, int32_t newSectorY,
                       SectorPos* outAdded, int32_t& outAddedCount,
                       SectorPos* outRemoved, int32_t& outRemovedCount) const;

    int32_t GetSectorCountX() const { return _sectorCountX; }
    int32_t GetSectorCountY() const { return _sectorCountY; }
    int32_t GetSectorSize() const { return _sectorSize; }

    static constexpr int32_t MAX_AROUND_SECTORS = 9;

private:
    bool IsValidSector(int32_t sectorX, int32_t sectorY) const;

    // 주변 9섹터 좌표 수집 (경계 클램핑 포함, 고정 배열)
    // outSectors: 최대 9개, outCount: 실제 개수
    void GetAroundSectorList(int32_t sectorX, int32_t sectorY,
                             SectorPos* outSectors, int32_t& outCount) const;

    int32_t _mapWidth;
    int32_t _mapHeight;
    int32_t _sectorSize;
    int32_t _sectorCountX;
    int32_t _sectorCountY;

    // 2D 그리드: _sectors[y][x] = 해당 섹터의 플레이어 목록
    std::vector<std::vector<std::vector<CPlayer*>>> _sectors;

    // GetSectorPlayers에서 범위 밖 요청 시 반환할 빈 벡터
    static const std::vector<CPlayer*> EMPTY_SECTOR;
};
