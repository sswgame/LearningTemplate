/**
 * @file StreamingCellGrid.h
 * @brief 월드 스트리밍(D1-7)의 런타임 격자입니다. 위치를 셀 좌표로 바꾸고, 스트리밍 원점 둘레의 셀을 고르고, 셀을 넘는 참조를 가립니다.
 *
 * [어디에 쓰는가]
 * 편집은 한 씬으로 하고, 쿠킹이 오브젝트 위치와 이 격자로 엔티티를 셀로 나눈다(`SceneCooker`). 런타임은 플레이어(서버는 접속자마다) 둘레의 셀을 활성 씬에 더해
 * 올리고 내린다. 언리얼 World Partition 의 런타임 격자(`UWorldPartitionRuntimeHashSet` 의 셀 크기 · 로딩 반경)와 같은 자리다.
 *
 *
 * [값의 기준]
 * 셀 크기와 로딩 반경은 데이터(`StreamingCellGridDef`, 씬의 월드 설정)다. 코드에는 형식만 있고 기본값이 없다 — 셀 크기가 0 이하면 격자를
 * 만들지 않고 오류로 멈춘다(`tryCreate`).
 *
 * [셀을 넘는 참조]
 * 오브젝트는 같은 셀의 오브젝트나 항상 올라 있는 오브젝트(`_bAlwaysLoaded`)만 가리킬 수 있다. 항상 올라 있는 오브젝트는 셀이 아니라 기본 묶음에
 * 실리므로 항상 올라 있는 오브젝트만 가리킬 수 있다. 그 밖을 가리키면 쿠킹이 오류로 멈춘다 —
 * 런타임에 조용히 null 이 되는 참조를 만들지 않는다(`findCrossCellReferences`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Asset/StreamingSourcePriority.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct StreamingCellCoord
     * @brief 셀 좌표(XZ 평면)입니다. 높이로는 나누지 않습니다 — 지형 · 도시 월드는 위아래로 얕습니다.
     */
    struct StreamingCellCoord
    {
        int32 _x{ 0 };
        int32 _z{ 0 };

        bool operator==( const StreamingCellCoord& other ) const { return _x == other._x && _z == other._z; }
        bool operator!=( const StreamingCellCoord& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct StreamingCellBounds
     * @brief 셀 조각이 덮는 XZ 사각형(절대 좌표)입니다 — 셀 사각형에 그 조각 엔티티의 경계를 더한 것입니다.
     * @details 조각을 올릴지는 셀 사각형이 아니라 이것으로 잰다(언리얼 `UWorldPartitionRuntimeCell::GetStreamingBounds` — 내용 경계). 소속은 자리(피벗)로 정하므로 셀보다 큰 건물 · 다리 · 바닥은
     *          피벗 셀이 반경 밖이어도 몸이 반경 안일 수 있다 — 셀 사각형으로 재면 눈앞의 몸이 사라지고 밟은 바닥이 없다.
     */
    struct StreamingCellBounds
    {
        float32 _minX{ MathUtil::kMaxFloat };
        float32 _minZ{ MathUtil::kMaxFloat };
        float32 _maxX{ -MathUtil::kMaxFloat };
        float32 _maxZ{ -MathUtil::kMaxFloat };

        /** @brief 아무것도 더하지 않은 빈 경계면 false 입니다. */
        bool isValid() const { return _minX <= _maxX && _minZ <= _maxZ; }
        /** @brief 사각형 @p other 까지 덮게 넓힙니다(빈 것은 더하지 않는다). */
        void include( const StreamingCellBounds& other )
        {
            _minX = MathUtil::min( _minX, other._minX );
            _minZ = MathUtil::min( _minZ, other._minZ );
            _maxX = MathUtil::max( _maxX, other._maxX );
            _maxZ = MathUtil::max( _maxZ, other._maxZ );
        }
        /** @brief 점과 사각형 사이의 XZ 최단 거리 제곱입니다(안이면 0). */
        float32 computeDistanceSquared( const float3& position ) const
        {
            const float32 dx = MathUtil::max( MathUtil::max( _minX - position._x, 0.0f ), position._x - _maxX );
            const float32 dz = MathUtil::max( MathUtil::max( _minZ - position._z, 0.0f ), position._z - _maxZ );
            return dx * dx + dz * dz;
        }
        /** @brief 점이 사각형 안이면 true 입니다(경계 포함). */
        bool contains( const float3& position ) const { return _minX <= position._x && position._x <= _maxX && _minZ <= position._z && position._z <= _maxZ; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct StreamingCellGridDef
     * @brief 격자 설정입니다(씬의 월드 설정 `SceneSettings::_cellGrid` 로 씬 파일에서 읽습니다). 셀 크기와 로딩 반경의 단위는 월드 단위(미터)입니다.
     */
    REFLECT()
    struct SW_API StreamingCellGridDef
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
     * @struct StreamingSource
     * @brief 스트리밍 소스 하나 — 위치와 그 둘레에서 올릴 반경, 읽기 차례입니다(언리얼 World Partition 의 `FWorldPartitionStreamingSource` 자리).
     * @details 서버는 접속자(관찰자)마다 하나씩 넣고 올리는 셀은 모든 소스 반경의 합집합이다(`SceneStreamingSubsystem::setExternalSources`).
     */
    struct StreamingSource
    {
        float3                  _position{};
        float32                 _loadingRadius{ 0.0f }; ///< 이 거리 안에 닿는 셀을 올린다. 유한한 0 이상이어야 한다
        StreamingSourcePriority _priority{ StreamingSourcePriority::Normal };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct StreamingCellObjectRecord
     * @brief 셀을 넘는 참조 검사에 넣는 오브젝트 한 줄입니다.
     */
    struct StreamingCellObjectRecord
    {
        uint64              _objectID{ 0 };
        StreamingCellCoord  _cell{};
        StreamingCellBounds _bounds{};               ///< 그 엔티티(다른 엔티티가 아닌 자식까지)의 경계 — 크기가 없으면 빈 경계다
        bool                _bAlwaysLoaded{ false }; ///< 공간 로드를 하지 않는 오브젝트(항상 올라 있다) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct StreamingCellReference
     * @brief 오브젝트 사이의 참조 한 줄입니다(`_fromObjectID` 가 `_toObjectID` 를 가리킨다).
     */
    struct StreamingCellReference
    {
        uint64 _fromObjectID{ 0 };
        uint64 _toObjectID{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class StreamingCellGrid
     * @brief 셀 크기와 로딩 반경으로 셀을 계산합니다. 상태가 없는 값 타입입니다.
     */
    class SW_API StreamingCellGrid
    {
    public:
        /**
         * @brief 설정으로 격자를 만듭니다.
         * @return 셀 크기가 0 이하이거나 로딩 반경이 음수이면 false 이고 outGrid 는 그대로입니다(오류 로그를 남깁니다).
         */
        [[nodiscard]] static bool tryCreate( const StreamingCellGridDef& def, StreamingCellGrid& outGrid );

        /** @brief 조각 하나의 경계가 덮을 수 있는 셀 수의 상한입니다 — 넘는 엔티티는 쿠커가 늘 올라 있게 한다(언리얼도 격자 단계보다 큰 액터는 늘 올라 있다). */
        static constexpr int64 kMaxCoveredCells = 4096;

        /** @brief 셀 @p cell 의 사각형입니다. */
        StreamingCellBounds computeCellBounds( const StreamingCellCoord& cell ) const;
        /** @brief 사각형 @p bounds 가 덮는 셀 수입니다(빈 경계면 0). */
        int64 countCoveredCells( const StreamingCellBounds& bounds ) const;
        /** @brief 사각형 @p bounds 가 덮는 셀을 모두 채웁니다(비우고 채운다, z · x 오름차순). 먼저 `countCoveredCells` 로 상한을 본다. */
        void collectCoveredCells( const StreamingCellBounds& bounds, vector<StreamingCellCoord>& outListCell ) const;

        /** @brief 위치가 든 셀을 반환합니다. 셀 경계는 아래쪽(작은 좌표)에 속합니다. 셀 번호는 ±2^30 에서 포화합니다(망에서 온 거대 · NaN 좌표도 정의된 셀이 된다). */
        StreamingCellCoord computeCell( const float3& position ) const;
        /** @brief 셀의 XZ 사각형과 점 사이의 최단 거리 제곱을 반환합니다(셀 안이면 0). */
        float32 computeCellDistanceSquared( const StreamingCellCoord& cell, const float3& position ) const;
        /**
         * @brief 원점 둘레에서 로딩 반경 안에 닿는 셀을 모두 채웁니다. 순서는 z, x 오름차순입니다(결정적).
         * @param outListCell 비우고 채웁니다.
         */
        void computeCellsInRange( const float3& origin, vector<StreamingCellCoord>& outListCell ) const;
        /**
         * @brief 원점 둘레에서 반경 @p radius 안에 닿는 셀을 모두 채웁니다(소스마다 반경이 다를 때). 순서 · 비우기는 `computeCellsInRange` 와 같습니다.
         * @warning 셀 수는 (2 × 반경 / 셀 크기)² 이다 — 믿을 수 없는 반경으로 부르지 않는다. 스트리밍은 셀 수가 받은 셀보다 많으면 받은 셀을 훑는다(`SceneStreamingSubsystem::update`).
         */
        void computeCellsInRadius( const float3& origin, float32 radius, vector<StreamingCellCoord>& outListCell ) const;

        /**
         * @brief 셀을 넘는 참조를 찾습니다. 같은 셀이거나 가리키는 쪽이 항상 올라 있으면 허용합니다.
         * @details 오브젝트 목록에 없는 대상도 위반으로 셉니다(어느 셀에 있을지 모른다). 쿠킹이 이 결과가 비어 있지 않으면 멈춥니다.
         * @param outListViolation 비우고 위반 참조를 넣은 순서대로 채웁니다.
         */
        static void findCrossCellReferences( const vector<StreamingCellObjectRecord>& listObject, const vector<StreamingCellReference>& listReference,
                                             vector<StreamingCellReference>& outListViolation );

        /** @brief 셀 한 변의 길이를 반환합니다. */
        float32 getCellSize() const { return _def._cellSize; }
        /** @brief 로딩 반경을 반환합니다. */
        float32 getLoadingRadius() const { return _def._loadingRadius; }

    private:
        StreamingCellGridDef _def{};
    };
} // namespace sw
