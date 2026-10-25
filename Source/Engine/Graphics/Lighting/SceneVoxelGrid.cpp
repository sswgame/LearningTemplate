#include "pch.h"

#include "Engine/Graphics/Lighting/SceneVoxelGrid.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct SceneVoxelGridInternal
        {
            /// @brief 방향 성분이 이보다 작으면 그 축은 평행으로 본다(0 으로 나누지 않게).
            static constexpr float32 kParallelEpsilon = 1.0e-8f;
            /// @brief 삼각형-상자 판정에서 상자를 살짝 키우는 비율 — 면에 정확히 닿는 삼각형이 이웃 칸에서 빠지지 않게 한다.
            static constexpr float32 kOverlapSlack = 1.0e-4f;

            /** @brief 점 하나를 축 배열로 풉니다. */
            static void unpack( const float3& value, float32 ( &outArr )[3] )
            {
                outArr[0] = value._x;
                outArr[1] = value._y;
                outArr[2] = value._z;
            }

            /** @brief 축 @p axis 를 걸어 들어온 면의 바깥 노멀입니다(걸음 방향 @p stepSign 의 반대). */
            static float3 buildNormal( int32 axis, int32 stepSign )
            {
                float3 normal{ 0.0f, 0.0f, 0.0f };
                if ( axis == 0 )
                    normal._x = static_cast<float32>( -stepSign );
                else if ( axis == 1 )
                    normal._y = static_cast<float32>( -stepSign );
                else
                    normal._z = static_cast<float32>( -stepSign );
                return normal;
            }

            /** @brief 한 분리축 위에서 삼각형의 투영 구간이 상자 반지름 밖이면 true(분리됨)입니다. */
            static bool isSeparatedOnAxis( const float3& axis, const float3& v0, const float3& v1, const float3& v2, float32 half )
            {
                const float32 p0     = v0.dot( axis );
                const float32 p1     = v1.dot( axis );
                const float32 p2     = v2.dot( axis );
                const float32 radius = half * ( MathUtil::abs( axis._x ) + MathUtil::abs( axis._y ) + MathUtil::abs( axis._z ) );
                const float32 low    = MathUtil::min( p0, MathUtil::min( p1, p2 ) );
                const float32 high   = MathUtil::max( p0, MathUtil::max( p1, p2 ) );
                return low > radius || high < -radius;
            }

            /**
             * @brief 중심 @p center, 반 변 @p half 인 상자와 삼각형이 겹치는지입니다(분리축 판정 13 개 — 모서리 외적 9, 상자 면 3, 삼각형 면 1).
             */
            static bool overlapsTriangleBox( const float3& center, float32 half, const float3& a, const float3& b, const float3& c )
            {
                const float3 v0 = a - center;
                const float3 v1 = b - center;
                const float3 v2 = c - center;
                const float3 e0 = v1 - v0;
                const float3 e1 = v2 - v1;
                const float3 e2 = v0 - v2;

                // 상자 면 셋 — 삼각형의 축 정렬 상자가 상자 밖이면 분리된다.
                if ( MathUtil::min( v0._x, MathUtil::min( v1._x, v2._x ) ) > half || MathUtil::max( v0._x, MathUtil::max( v1._x, v2._x ) ) < -half )
                    return false;
                if ( MathUtil::min( v0._y, MathUtil::min( v1._y, v2._y ) ) > half || MathUtil::max( v0._y, MathUtil::max( v1._y, v2._y ) ) < -half )
                    return false;
                if ( MathUtil::min( v0._z, MathUtil::min( v1._z, v2._z ) ) > half || MathUtil::max( v0._z, MathUtil::max( v1._z, v2._z ) ) < -half )
                    return false;

                // 삼각형 면.
                const float3 normal = e0.cross( e1 );
                if ( isSeparatedOnAxis( normal, v0, v1, v2, half ) )
                    return false;

                // 모서리 셋 × 상자 축 셋.
                const float3 arrEdge[3]{ e0, e1, e2 };
                const float3 arrBoxAxis[3]{
                    float3{1.0f, 0.0f, 0.0f},
                    float3{0.0f, 1.0f, 0.0f},
                    float3{0.0f, 0.0f, 1.0f}
                };
                for ( const float3& edge : arrEdge )
                {
                    for ( const float3& boxAxis : arrBoxAxis )
                    {
                        const float3 axis = boxAxis.cross( edge );
                        if ( axis.getLengthSquared() <= kParallelEpsilon )
                            continue; // 모서리가 이 상자 축과 평행 — 다른 분리축이 이미 다룬다
                        if ( isSeparatedOnAxis( axis, v0, v1, v2, half ) )
                            return false;
                    }
                }
                return true;
            }
        };
    } // namespace

    SceneVoxelGrid SceneVoxelGrid::make( const AABB& bounds, float32 targetCellSize )
    {
        SceneVoxelGrid grid;
        if ( bounds.isValid() == false || targetCellSize <= 0.0f )
            return grid;
        const float3  size       = bounds._max - bounds._min;
        const float32 longest    = MathUtil::max( size._x, MathUtil::max( size._y, size._z ) );
        float32       cellSize   = targetCellSize;
        const float32 maxCellCnt = static_cast<float32>( kMaxCellsPerAxis );
        if ( longest / cellSize > maxCellCnt )
            cellSize = longest / maxCellCnt;
        grid._origin   = bounds._min;
        grid._cellSize = cellSize;
        grid._dimX     = MathUtil::max( static_cast<uint32>( MathUtil::ceil( size._x / cellSize ) ), 1u );
        grid._dimY     = MathUtil::max( static_cast<uint32>( MathUtil::ceil( size._y / cellSize ) ), 1u );
        grid._dimZ     = MathUtil::max( static_cast<uint32>( MathUtil::ceil( size._z / cellSize ) ), 1u );
        grid._listWord.assign( ( grid.getCellCount() + 31u ) / 32u, 0u );
        return grid;
    }

    AABB SceneVoxelGrid::getBounds() const
    {
        const float3 size{ static_cast<float32>( _dimX ) * _cellSize, static_cast<float32>( _dimY ) * _cellSize, static_cast<float32>( _dimZ ) * _cellSize };
        return AABB{ _origin, _origin + size };
    }

    void SceneVoxelGrid::setOccupied( uint32 cellX, uint32 cellY, uint32 cellZ )
    {
        if ( containsCell( cellX, cellY, cellZ ) == false )
            return;
        const uint32 index = computeCellIndex( cellX, cellY, cellZ );
        _listWord[index >> 5] |= ( 1u << ( index & 31u ) );
    }

    bool SceneVoxelGrid::isOccupied( uint32 cellX, uint32 cellY, uint32 cellZ ) const
    {
        if ( containsCell( cellX, cellY, cellZ ) == false )
            return false;
        const uint32 index = computeCellIndex( cellX, cellY, cellZ );
        return ( _listWord[index >> 5] & ( 1u << ( index & 31u ) ) ) != 0u;
    }

    uint32 SceneVoxelGrid::countOccupied() const
    {
        uint32 count = 0;
        for ( const uint32 word : _listWord )
        {
            uint32 bits = word;
            while ( bits != 0u )
            {
                bits &= bits - 1u;
                ++count;
            }
        }
        return count;
    }

    void SceneVoxelGrid::voxelizeTriangle( const float3& a, const float3& b, const float3& c )
    {
        using Internal = SceneVoxelGridInternal;
        if ( isEmpty() )
            return;
        const float3 low  = float3::min( a, float3::min( b, c ) ) - _origin;
        const float3 high = float3::max( a, float3::max( b, c ) ) - _origin;
        float32      arrLow[3];
        float32      arrHigh[3];
        Internal::unpack( low, arrLow );
        Internal::unpack( high, arrHigh );
        const uint32 arrDim[3]{ _dimX, _dimY, _dimZ };
        uint32       arrFirst[3];
        uint32       arrLast[3];
        for ( uint32 axis = 0; axis < 3; ++axis )
        {
            // 삼각형의 상자가 격자 밖이면 그릴 것이 없다.
            const float32 gridExtent = static_cast<float32>( arrDim[axis] ) * _cellSize;
            if ( arrHigh[axis] < 0.0f || arrLow[axis] > gridExtent )
                return;
            arrFirst[axis] = static_cast<uint32>( MathUtil::max( MathUtil::floor( arrLow[axis] / _cellSize ), 0.0f ) );
            arrLast[axis]  = MathUtil::min( static_cast<uint32>( MathUtil::max( MathUtil::floor( arrHigh[axis] / _cellSize ), 0.0f ) ), arrDim[axis] - 1u );
        }
        const float32 half = _cellSize * 0.5f * ( 1.0f + Internal::kOverlapSlack );
        for ( uint32 cellZ = arrFirst[2]; cellZ <= arrLast[2]; ++cellZ )
        {
            for ( uint32 cellY = arrFirst[1]; cellY <= arrLast[1]; ++cellY )
            {
                for ( uint32 cellX = arrFirst[0]; cellX <= arrLast[0]; ++cellX )
                {
                    const float3 center = _origin + float3{ ( static_cast<float32>( cellX ) + 0.5f ) * _cellSize, ( static_cast<float32>( cellY ) + 0.5f ) * _cellSize,
                                                            ( static_cast<float32>( cellZ ) + 0.5f ) * _cellSize };
                    if ( Internal::overlapsTriangleBox( center, half, a, b, c ) )
                        setOccupied( cellX, cellY, cellZ );
                }
            }
        }
    }

    // 길이를 나누지 않는다: 슬랩 판정 · 시작 칸 · 칸 걷기가 지역 변수(`tNear` · 칸 · 축 · 경계 거리)를 모두 공유하는 한 알고리즘이고,
    // 103 줄로 경계라 단계 결과 구조체를 둘 만큼의 이득이 없다.
    bool SceneVoxelGrid::traceRay( const float3& origin, const float3& direction, float32 maxDistance, SceneVoxelHit& outHit ) const
    {
        using Internal = SceneVoxelGridInternal;
        if ( isEmpty() || maxDistance <= 0.0f )
            return false;
        float32 arrOrigin[3];
        float32 arrDirection[3];
        Internal::unpack( origin - _origin, arrOrigin );
        Internal::unpack( direction, arrDirection );
        const uint32 arrDim[3]{ _dimX, _dimY, _dimZ };

        // 격자 상자와 광선의 겹침 구간 [tNear, tFar](슬랩 판정). 들어오는 면의 축을 기억해 두었다가 첫 칸의 노멀로 쓴다.
        float32 tNear     = 0.0f;
        float32 tFar      = maxDistance;
        int32   entryAxis = -1;
        for ( uint32 axis = 0; axis < 3; ++axis )
        {
            const float32 extent = static_cast<float32>( arrDim[axis] ) * _cellSize;
            if ( MathUtil::abs( arrDirection[axis] ) < Internal::kParallelEpsilon )
            {
                if ( arrOrigin[axis] < 0.0f || arrOrigin[axis] > extent )
                    return false;
                continue;
            }
            const float32 inverse = 1.0f / arrDirection[axis];
            float32       tEnter  = ( 0.0f - arrOrigin[axis] ) * inverse;
            float32       tExit   = ( extent - arrOrigin[axis] ) * inverse;
            if ( tEnter > tExit )
            {
                const float32 swapped = tEnter;
                tEnter                = tExit;
                tExit                 = swapped;
            }
            if ( tEnter > tNear )
            {
                tNear     = tEnter;
                entryAxis = static_cast<int32>( axis );
            }
            tFar = MathUtil::min( tFar, tExit );
            if ( tNear > tFar )
                return false;
        }

        // 시작 칸 — 들어오는 지점의 칸이다(경계 위의 오차는 격자 안으로 자른다).
        int32 arrCell[3];
        for ( uint32 axis = 0; axis < 3; ++axis )
        {
            const float32 position = arrOrigin[axis] + arrDirection[axis] * tNear;
            const int32   cell     = static_cast<int32>( MathUtil::max( MathUtil::floor( position / _cellSize ), 0.0f ) );
            arrCell[axis]          = MathUtil::min( cell, static_cast<int32>( arrDim[axis] ) - 1 );
        }
        if ( isOccupied( static_cast<uint32>( arrCell[0] ), static_cast<uint32>( arrCell[1] ), static_cast<uint32>( arrCell[2] ) ) )
        {
            // 첫 칸에서 바로 닿았다 — 들어온 면이 있으면 그 면, 안에서 시작했으면 지배 축의 반대 면을 노멀로 쓴다.
            int32 normalAxis = entryAxis;
            if ( normalAxis < 0 )
            {
                const float32 absX = MathUtil::abs( arrDirection[0] );
                const float32 absY = MathUtil::abs( arrDirection[1] );
                const float32 absZ = MathUtil::abs( arrDirection[2] );
                normalAxis         = ( absX >= absY && absX >= absZ ) ? 0 : ( ( absY >= absZ ) ? 1 : 2 );
            }
            outHit._distance = tNear;
            outHit._normal   = Internal::buildNormal( normalAxis, arrDirection[normalAxis] >= 0.0f ? 1 : -1 );
            return true;
        }

        int32   arrStep[3];
        float32 arrTMax[3];
        float32 arrTDelta[3];
        for ( uint32 axis = 0; axis < 3; ++axis )
        {
            if ( MathUtil::abs( arrDirection[axis] ) < Internal::kParallelEpsilon )
            {
                arrStep[axis]   = 0;
                arrTMax[axis]   = MathUtil::kMaxFloat;
                arrTDelta[axis] = MathUtil::kMaxFloat;
                continue;
            }
            const bool    bForward = arrDirection[axis] > 0.0f;
            const float32 boundary = static_cast<float32>( bForward ? arrCell[axis] + 1 : arrCell[axis] ) * _cellSize;
            arrStep[axis]          = bForward ? 1 : -1;
            arrTMax[axis]          = ( boundary - arrOrigin[axis] ) / arrDirection[axis];
            arrTDelta[axis]        = _cellSize / MathUtil::abs( arrDirection[axis] );
        }
        const uint32 maxStepCount = _dimX + _dimY + _dimZ + 3u;
        for ( uint32 stepIndex = 0; stepIndex < maxStepCount; ++stepIndex )
        {
            const uint32 axis = ( arrTMax[0] <= arrTMax[1] && arrTMax[0] <= arrTMax[2] ) ? 0u : ( ( arrTMax[1] <= arrTMax[2] ) ? 1u : 2u );
            if ( arrTMax[axis] > tFar )
                return false;
            const float32 enterDistance = arrTMax[axis];
            arrCell[axis] += arrStep[axis];
            if ( arrCell[axis] < 0 || arrCell[axis] >= static_cast<int32>( arrDim[axis] ) )
                return false;
            arrTMax[axis] += arrTDelta[axis];
            if ( isOccupied( static_cast<uint32>( arrCell[0] ), static_cast<uint32>( arrCell[1] ), static_cast<uint32>( arrCell[2] ) ) )
            {
                outHit._distance = enterDistance;
                outHit._normal   = Internal::buildNormal( static_cast<int32>( axis ), arrStep[axis] );
                return true;
            }
        }
        return false;
    }
} // namespace sw
