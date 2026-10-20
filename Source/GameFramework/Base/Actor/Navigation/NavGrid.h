/**
 * @file NavGrid.h
 * @brief 격자 내비게이션 맵입니다. 셀마다 지나는 값(1..254, 255 는 막힘)을 두고, 월드와 셀 변환, 시선(LOS), 가장 가까운 걸을 수 있는 셀 찾기를 제공합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 지날 수 없는 셀의 값입니다. */
    constexpr uint8 kNavBlockedCost = 255;
    /** @brief 보통 땅의 값입니다. 길은 이보다 작게, 늪은 크게 둡니다. */
    constexpr uint8 kNavDefaultCost = 10;

    /**
     * @class NavGrid
     * @brief XZ 평면의 균일 격자입니다. 셀 (x, y) 의 가운데는 월드 (origin.x + (x + 0.5) × 셀 크기, origin.y, origin.z + (y + 0.5) × 셀 크기) 입니다.
     * @details 언리얼과 유니티의 내비메시 대신 격자를 씁니다. RTS, 도시 건설, 타일 RPG 처럼 판이 격자로 생긴 장르는 격자가 정확하고, 건물을 놓고
     *          부술 때 그 셀만 바꾸면 끝입니다(내비메시는 그 영역을 다시 베이크한다). 값은 그 셀에 들어가는 비용입니다(10 = 보통, 5 = 길, 30 = 늪).
     *          바뀔 때마다 `getRevision` 이 오르므로 흐름장 · 경로 캐시가 낡았는지 압니다.
     */
    class SW_GF_API NavGrid
    {
    public:
        NavGrid();

        /** @brief 크기를 정하고 모든 셀을 `kNavDefaultCost` 로 둡니다. */
        void initialize( int32 width, int32 height, float32 cellSize, const float3& origin );

        void setCost( int32 x, int32 y, uint8 cost );
        void setBlocked( int32 x, int32 y, bool bBlocked ) { setCost( x, y, bBlocked ? kNavBlockedCost : kNavDefaultCost ); }
        /** @brief 사각 영역(양 끝 포함)의 값을 한꺼번에 바꿉니다. 건물을 놓을 때 씁니다. 리비전은 한 번만 오릅니다. */
        void setAreaCost( int32 minX, int32 minY, int32 maxX, int32 maxY, uint8 cost );

        bool isInside( int32 x, int32 y ) const { return _topology.isInside( x, y ); }
        bool isInside( const int2& cell ) const { return isInside( cell._x, cell._y ); }
        bool isWalkable( int32 x, int32 y ) const { return isInside( x, y ) && _listCost[static_cast<size_t>( _topology.toIndex( x, y ) )] != kNavBlockedCost; }
        bool isWalkable( const int2& cell ) const { return isWalkable( cell._x, cell._y ); }
        /** @brief 셀 값입니다. 밖은 막힘입니다. */
        uint8 getCost( int32 x, int32 y ) const { return isInside( x, y ) ? _listCost[static_cast<size_t>( _topology.toIndex( x, y ) )] : kNavBlockedCost; }

        /** @brief 월드 위치가 속한 셀입니다. 격자 밖이어도 계산하므로 `isInside` 로 확인합니다. */
        int2 computeCell( const float3& worldPosition ) const;
        /** @brief 셀 가운데의 월드 위치입니다(높이는 원점의 y). */
        float3 computeCellCenter( const int2& cell ) const;
        /**
         * @brief 두 셀 사이를 곧게 지날 수 있으면 true 입니다. 지나는 셀을 모두 보므로 대각선으로 두 막힌 셀 틈을 빠져나가지 않습니다.
         * @details 경로 다듬기(줄 당기기)와 시야(전장의 안개 · 감지)가 씁니다.
         */
        bool hasLineOfSight( const int2& from, const int2& to ) const;
        /** @brief @p cell 에서 @p maxRadius 셀 안의 가장 가까운 걸을 셀입니다. 없으면 false 입니다(목적지가 건물 안일 때). */
        [[nodiscard]] bool findNearestWalkable( const int2& cell, int32 maxRadius, int2& outCell ) const;

        int32         getWidth() const { return _topology._width; }
        int32         getHeight() const { return _topology._height; }
        float32       getCellSize() const { return _cellSize; }
        const float3& getOrigin() const { return _origin; }
        uint32        getRevision() const { return _revision; }
        int32         computeIndex( const int2& cell ) const { return _topology.toIndex( cell ); }

    private:
        vector<uint8> _listCost; ///< 행 우선(y × width + x)
        float3        _origin;
        float32       _cellSize;
        GridTopology  _topology; ///< 너비, 높이, 셀 번호
        uint32        _revision;
    };
} // namespace sw
