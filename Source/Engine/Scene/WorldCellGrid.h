/**
 * @file WorldCellGrid.h
 * @brief 월드 스트리밍(D1-7)의 런타임 격자입니다. 위치를 셀 좌표로 바꾸고, 스트리밍 원점 둘레의 셀을 고르고, 셀을 넘는 참조를 가립니다.
 *
 * [어디에 쓰는가]
 * 편집은 한 씬으로 하고, 쿠킹이 오브젝트 위치와 이 격자로 엔티티를 셀로 나눈다(`SceneCooker`). 런타임은 플레이어(서버는 접속자마다) 둘레의 셀을 활성 씬에 더해
 * 올리고 내린다. 언리얼 World Partition 의 런타임 격자(`UWorldPartitionRuntimeHashSet` 의 셀 크기 · 로딩 반경)와 같은 자리다.
 *
 *
 * [값의 기준]
 * 셀 크기와 로딩 반경은 데이터(`WorldCellGridDef`, 씬의 월드 설정)다. 코드에는 형식만 있고 기본값이 없다 — 셀 크기가 0 이하면 격자를
 * 만들지 않고 오류로 멈춘다(`tryCreate`).
 *
 * [셀을 넘는 참조]
 * 오브젝트는 같은 셀의 오브젝트나 항상 올라 있는 오브젝트(`_bAlwaysLoaded`)만 가리킬 수 있다. 항상 올라 있는 오브젝트는 셀이 아니라 기본 묶음에
 * 실리므로 항상 올라 있는 오브젝트만 가리킬 수 있다. 그 밖을 가리키면 쿠킹이 오류로 멈춘다 —
 * 런타임에 조용히 null 이 되는 참조를 만들지 않는다(`findCrossCellReferences`).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct WorldCellCoord
     * @brief 셀 좌표(XZ 평면)입니다. 높이로는 나누지 않습니다 — 지형 · 도시 월드는 위아래로 얕습니다.
     */
    struct WorldCellCoord
    {
        int32 _x{ 0 };
        int32 _z{ 0 };

        bool operator==( const WorldCellCoord& other ) const { return _x == other._x && _z == other._z; }
        bool operator!=( const WorldCellCoord& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WorldCellGridDef
     * @brief 격자 설정입니다(씬의 월드 설정 `WorldSettings::_cellGrid` 로 씬 파일에서 읽습니다). 셀 크기와 로딩 반경의 단위는 월드 단위(미터)입니다.
     */
    REFLECT()
    struct SW_API WorldCellGridDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Cell edge length; a streaming scene needs a positive value", Units = m )
        float32 _cellSize{ 0.0f }; ///< 셀 한 변. 0 이하이면 격자를 만들 수 없다
        PROPERTY( Min = 0.0, Tooltip = "Cells within this distance of a streaming source are loaded", Units = m )
        float32 _loadingRadius{ 0.0f }; ///< 스트리밍 원점에서 이 거리 안에 닿는 셀을 올린다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WorldCellObjectRecord
     * @brief 셀을 넘는 참조 검사에 넣는 오브젝트 한 줄입니다.
     */
    struct WorldCellObjectRecord
    {
        uint64         _objectID{ 0 };
        WorldCellCoord _cell{};
        bool           _bAlwaysLoaded{ false }; ///< 공간 로드를 하지 않는 오브젝트(항상 올라 있다)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WorldCellReference
     * @brief 오브젝트 사이의 참조 한 줄입니다(`_fromObjectID` 가 `_toObjectID` 를 가리킨다).
     */
    struct WorldCellReference
    {
        uint64 _fromObjectID{ 0 };
        uint64 _toObjectID{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class WorldCellGrid
     * @brief 셀 크기와 로딩 반경으로 셀을 계산합니다. 상태가 없는 값 타입입니다.
     */
    class SW_API WorldCellGrid
    {
    public:
        /**
         * @brief 설정으로 격자를 만듭니다.
         * @return 셀 크기가 0 이하이거나 로딩 반경이 음수이면 false 이고 outGrid 는 그대로입니다(오류 로그를 남깁니다).
         */
        [[nodiscard]] static bool tryCreate( const WorldCellGridDef& def, WorldCellGrid& outGrid );

        /** @brief 위치가 든 셀을 반환합니다. 셀 경계는 아래쪽(작은 좌표)에 속합니다. */
        WorldCellCoord computeCell( const float3& position ) const;
        /** @brief 셀의 XZ 사각형과 점 사이의 최단 거리 제곱을 반환합니다(셀 안이면 0). */
        float32 computeCellDistanceSquared( const WorldCellCoord& cell, const float3& position ) const;
        /**
         * @brief 원점 둘레에서 로딩 반경 안에 닿는 셀을 모두 채웁니다. 순서는 z, x 오름차순입니다(결정적).
         * @param outListCell 비우고 채웁니다.
         */
        void computeCellsInRange( const float3& origin, vector<WorldCellCoord>& outListCell ) const;

        /**
         * @brief 셀을 넘는 참조를 찾습니다. 같은 셀이거나 가리키는 쪽이 항상 올라 있으면 허용합니다.
         * @details 오브젝트 목록에 없는 대상도 위반으로 셉니다(어느 셀에 있을지 모른다). 쿠킹이 이 결과가 비어 있지 않으면 멈춥니다.
         * @param outListViolation 비우고 위반 참조를 넣은 순서대로 채웁니다.
         */
        static void findCrossCellReferences( const vector<WorldCellObjectRecord>& listObject, const vector<WorldCellReference>& listReference,
                                             vector<WorldCellReference>& outListViolation );

        /** @brief 셀 한 변의 길이를 반환합니다. */
        float32 getCellSize() const { return _def._cellSize; }
        /** @brief 로딩 반경을 반환합니다. */
        float32 getLoadingRadius() const { return _def._loadingRadius; }

    private:
        WorldCellGridDef _def{};
    };
} // namespace sw
