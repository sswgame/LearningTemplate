/**
 * @file RTSUnitBuckets.h
 * @brief RTS 유닛의 이웃 탐색 버킷입니다. 걸음마다 살아 있는 유닛의 슬롯을 정사각 버킷에 연결 목록으로 넣고, 둘레 버킷만 훑게 합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Navigation/NavGrid.h"
#include "GameFramework/Runtime/Foundation/Utility/Grid/GridTopology.h"

namespace sw
{
    /**
     * @class RTSUnitBuckets
     * @brief 버킷마다 첫 슬롯(`_listHead`)과 슬롯마다 다음 슬롯(`_listNext`)을 듭니다. 할당 없이 걸음마다 다시 짓습니다(`rebuild`).
     * @details 유닛 슬롯 번호만 알고 유닛 모양은 모른다 — 위치를 주는 쪽(`RTSWorld`)이 넣고 거른다. 훑는 순서는 버킷 행 · 열, 버킷 안은 나중에 넣은 슬롯부터다
     *          (결정적 시뮬레이션이라 순서가 결과에 들어간다). 해시 격자(`SpatialHashGrid2D`)는 맵 크기가 정해진 판에 비해 무겁고 결과를 정렬해 순서가 달라진다.
     */
    class SW_KIT_API RTSUnitBuckets
    {
    public:
        /** @brief 격자 크기와 버킷 한 변(셀 수)으로 비웁니다. */
        void initialize( int32 gridWidth, int32 gridHeight, int32 bucketSize )
        {
            _bucketSize = bucketSize;
            _topology   = GridTopology{ ( gridWidth + bucketSize - 1 ) / bucketSize, ( gridHeight + bucketSize - 1 ) / bucketSize };
            _listHead.assign( static_cast<size_t>( _topology.getCellCount() ), constant::invalid_index::kInt32 );
            _listNext.clear();
        }

        /** @brief 비우고 슬롯 @p slotCount 개를 받을 준비를 합니다. 이어서 살아 있는 슬롯마다 `insert` 를 부릅니다. */
        void beginRebuild( size_t slotCount )
        {
            std::fill( _listHead.begin(), _listHead.end(), constant::invalid_index::kInt32 );
            _listNext.assign( slotCount, constant::invalid_index::kInt32 );
        }
        /** @brief 슬롯 @p slot 을 @p cell(격자 셀)이 속한 버킷에 넣습니다. 격자 밖 셀은 가장자리 버킷에 넣습니다. */
        void insert( int32 slot, const int2& cell )
        {
            const int32  bucketX                   = MathUtil::clamp( cell._x / _bucketSize, 0, _topology._width - 1 );
            const int32  bucketY                   = MathUtil::clamp( cell._y / _bucketSize, 0, _topology._height - 1 );
            const size_t bucket                    = static_cast<size_t>( _topology.toIndex( bucketX, bucketY ) );
            _listNext[static_cast<size_t>( slot )] = _listHead[bucket];
            _listHead[bucket]                      = slot;
        }

        /** @brief @p center 둘레 @p reach(월드 거리) 사각형에 걸린 버킷의 슬롯마다 @p function( slot ) 을 부릅니다. 걸러 내기는 부르는 쪽이 합니다. */
        template <typename TFunction>
        void forEachSlotNear( const NavGrid& grid, const float3& center, float32 reach, TFunction&& function ) const
        {
            if ( _listHead.empty() )
                return;
            const float32 bucketLength = grid.getCellSize() * static_cast<float32>( _bucketSize );
            const int32   lastBucketX  = _topology._width - 1;
            const int32   lastBucketY  = _topology._height - 1;
            const int32   minBucketX   = MathUtil::clamp( static_cast<int32>( ( center._x - reach - grid.getOrigin()._x ) / bucketLength ), 0, lastBucketX );
            const int32   maxBucketX   = MathUtil::clamp( static_cast<int32>( ( center._x + reach - grid.getOrigin()._x ) / bucketLength ), 0, lastBucketX );
            const int32   minBucketY   = MathUtil::clamp( static_cast<int32>( ( center._z - reach - grid.getOrigin()._z ) / bucketLength ), 0, lastBucketY );
            const int32   maxBucketY   = MathUtil::clamp( static_cast<int32>( ( center._z + reach - grid.getOrigin()._z ) / bucketLength ), 0, lastBucketY );
            for ( int32 bucketY = minBucketY; bucketY <= maxBucketY; ++bucketY )
            {
                for ( int32 bucketX = minBucketX; bucketX <= maxBucketX; ++bucketX )
                {
                    for ( int32 slot = _listHead[static_cast<size_t>( _topology.toIndex( bucketX, bucketY ) )]; slot >= 0; slot = _listNext[static_cast<size_t>( slot )] )
                        function( slot );
                }
            }
        }

    private:
        vector<int32> _listHead;        ///< 버킷마다 첫 슬롯(-1 이면 끝)
        vector<int32> _listNext;        ///< 슬롯마다 다음 슬롯
        GridTopology  _topology{};      ///< 버킷 격자
        int32         _bucketSize{ 1 }; ///< 버킷 한 변의 셀 수
    };
} // namespace sw
