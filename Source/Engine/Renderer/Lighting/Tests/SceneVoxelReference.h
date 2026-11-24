/**
 * @file SceneVoxelReference.h
 * @brief 복셀 셰이더(`scene_voxelize.hlsl` · `probe_update.hlsl` 의 `traceVoxelRay`)의 CPU 기준 구현입니다 — 같은 식을 CPU 에서 돌려 닫힌 값과 견준다.
 * @details 점유 비트 · 발광 순위는 실행 중에는 GPU 버퍼에만 있다(`SceneVoxelGrid.h` 머리말). 셰이더의 식을 고치면 이 헤더도 함께 고친다.
 *          배포 구성은 `Tests/` 를 짓지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Lighting/SceneVoxelGrid.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Renderer/Lighting/SceneVoxelizer.h"

namespace sw::test
{
    /** @brief 복셀 광선이 처음 닿은 면입니다. */
    struct SceneVoxelHit
    {
        float32 _distance{ 0.0f };           ///< 광선 시작점에서 닿은 곳까지의 거리
        float3  _normal{ 0.0f, 1.0f, 0.0f }; ///< 닿은 칸의 면 노멀(들어온 면의 바깥 방향 — 축 정렬)
    };
} // namespace sw::test

namespace sw::test
{
    /** @brief 점유 복셀 격자 하나의 CPU 기준 구현입니다. */
    struct SceneVoxelReference
    {
        /// @brief 방향 성분이 이보다 작으면 그 축은 평행으로 본다(0 으로 나누지 않게).
        static constexpr float32 kParallelEpsilon = 1.0e-8f;
        /// @brief 삼각형-상자 판정에서 상자를 살짝 키우는 비율 — 면에 정확히 닿는 삼각형이 이웃 칸에서 빠지지 않게 한다.
        static constexpr float32 kOverlapSlack = 1.0e-4f;
        /// @brief 평면이 지나는 층을 고를 때의 여유(칸 크기 비율, scene_voxelize.hlsl kLayerSlack).
        static constexpr float32 kLayerSlack = 1.0e-2f;

        /// @brief 일 하나에서 월드 행렬(되돌리기는 정적 사본의 자리)이 시작하는 행(scene_voxelize.hlsl kJobWorld).
        static constexpr uint32 kJobWorldRow = 5;

        SceneVoxelGrid _grid{};
        vector<uint32> _listWord{}; ///< 점유 비트(칸 번호 / 32 번째 단어의 칸 번호 % 32 번째 비트)

        /** @brief @p grid 모양의 빈 격자입니다. */
        explicit SceneVoxelReference( const SceneVoxelGrid& grid )
            : _grid{ grid }
            , _listWord( grid.getWordCount(), 0u )
        {
        }
        /** @brief 칠한 단어 버퍼 @p words 의 @p wordOffset 부터를 점유 비트로 듭니다. */
        SceneVoxelReference( const SceneVoxelGrid& grid, const vector<uint32>& words, uint32 wordOffset )
            : _grid{ grid }
            , _listWord( words.begin() + wordOffset, words.begin() + wordOffset + grid.getWordCount() )
        {
        }

        /** @brief 칸이 켜졌는지입니다. 범위 밖이면 false 입니다. */
        bool isOccupied( uint32 cellX, uint32 cellY, uint32 cellZ ) const
        {
            if ( _grid.containsCell( cellX, cellY, cellZ ) == false )
                return false;
            const uint32 index = _grid.computeCellIndex( cellX, cellY, cellZ );
            return ( _listWord[index >> 5] & ( 1u << ( index & 31u ) ) ) != 0u;
        }

        /** @brief 켜진 칸 수입니다. */
        uint32 countOccupied() const
        {
            uint32 count = 0;
            for ( const uint32 word : _listWord )
            {
                for ( uint32 bits = word; bits != 0u; bits &= bits - 1u )
                {
                    ++count;
                }
            }
            return count;
        }

        /** @brief 삼각형(월드 좌표)과 겹치는 모든 칸을 켭니다 — 상자 안의 모든 칸을 분리축으로 본다(칠하기 셰이더가 같은 칸을 켜야 하는 기준). */
        void voxelizeTriangle( const float3& a, const float3& b, const float3& c )
        {
            const AABB                box{ float3::min( a, float3::min( b, c ) ), float3::max( a, float3::max( b, c ) ) };
            SceneVoxelGrid::CellRange range{};
            if ( _grid.findCellRange( box, range ) == false )
                return;
            const float32 half = _grid._cellSize * 0.5f * ( 1.0f + kOverlapSlack );
            forEachCell( range, [this, half, &a, &b, &c]( uint32 cellX, uint32 cellY, uint32 cellZ )
            {
                const float3 center = _grid._origin + float3{ static_cast<float32>( cellX ) + 0.5f, static_cast<float32>( cellY ) + 0.5f, static_cast<float32>( cellZ ) + 0.5f } * _grid._cellSize;
                if ( overlapsTriangleBox( center, half, a, b, c ) )
                    setBit( _listWord, 0u, _grid.computeCellIndex( cellX, cellY, cellZ ) );
            } );
        }

        /** @brief 칠할 일 한 벌(`SceneVoxelizer::planPaint`)을 셰이더와 같은 식으로 돌려 단어 버퍼 @p inoutWords 에 칠합니다. 정점은 메시의 CPU 정점이다. */
        static void executePlan( const SceneVoxelPaintPlan& plan, vector<uint32>& inoutWords )
        {
            vector<float3> listPosition;
            vector<uint32> listIndex;
            for ( const SceneVoxelPaintPlan::MeshSpan& span : plan._listMesh )
            {
                for ( const RHIVertex& vertex : span._mesh->getCPUView().getVertices() )
                {
                    listPosition.push_back( float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] } );
                }
                listIndex.insert( listIndex.end(), span._mesh->getIndices().begin(), span._mesh->getIndices().end() );
            }
            executeJobs( plan._listClearRow, listPosition, listIndex, inoutWords );
            executeJobs( plan._listPaintRow, listPosition, listIndex, inoutWords );
            executeJobs( plan._listRestoreRow, listPosition, listIndex, inoutWords );
            executeJobs( plan._listMovingRow, listPosition, listIndex, inoutWords );
        }

        /**
         * @brief 광선이 처음 만나는 켜진 칸을 찾습니다(Amanatides-Woo DDA — `probe_update.hlsl` 의 `traceVoxelRay` 와 같은 식).
         * @param direction 단위 벡터
         * @param maxDistance 이 거리까지만 봅니다
         * @return 만나면 true 이고 @p outHit 에 거리와 면 노멀을 적습니다. 광선이 켜진 칸 안에서 시작하면 거리 0 으로 닿는다.
         */
        bool traceRay( const float3& origin, const float3& direction, float32 maxDistance, SceneVoxelHit& outHit ) const
        {
            if ( _grid.isEmpty() || maxDistance <= 0.0f )
                return false;
            float32 arrOrigin[3];
            float32 arrDirection[3];
            unpack( origin - _grid._origin, arrOrigin );
            unpack( direction, arrDirection );
            const uint32 arrDim[3]{ _grid._dimX, _grid._dimY, _grid._dimZ };

            // 격자 상자와 광선의 겹침 구간 [tNear, tFar](슬랩 판정). 들어오는 면의 축을 기억해 두었다가 첫 칸의 노멀로 쓴다.
            float32 tNear     = 0.0f;
            float32 tFar      = maxDistance;
            int32   entryAxis = -1;
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                const float32 extent = static_cast<float32>( arrDim[axis] ) * _grid._cellSize;
                if ( MathUtil::abs( arrDirection[axis] ) < kParallelEpsilon )
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
                const int32   cell     = static_cast<int32>( MathUtil::max( MathUtil::floor( position / _grid._cellSize ), 0.0f ) );
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
                outHit._normal   = buildNormal( normalAxis, arrDirection[normalAxis] >= 0.0f ? 1 : -1 );
                return true;
            }

            int32   arrStep[3];
            float32 arrTMax[3];
            float32 arrTDelta[3];
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                if ( MathUtil::abs( arrDirection[axis] ) < kParallelEpsilon )
                {
                    arrStep[axis]   = 0;
                    arrTMax[axis]   = MathUtil::kMaxFloat;
                    arrTDelta[axis] = MathUtil::kMaxFloat;
                    continue;
                }
                const bool    bForward = arrDirection[axis] > 0.0f;
                const float32 boundary = static_cast<float32>( bForward ? arrCell[axis] + 1 : arrCell[axis] ) * _grid._cellSize;
                arrStep[axis]          = bForward ? 1 : -1;
                arrTMax[axis]          = ( boundary - arrOrigin[axis] ) / arrDirection[axis];
                arrTDelta[axis]        = _grid._cellSize / MathUtil::abs( arrDirection[axis] );
            }
            const uint32 maxStepCount = _grid._dimX + _grid._dimY + _grid._dimZ + 3u;
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
                    outHit._normal   = buildNormal( static_cast<int32>( axis ), arrStep[axis] );
                    return true;
                }
            }
            return false;
        }

    private:
        /** @brief 입력 행 하나에서 읽은 일입니다(scene_voxelize.hlsl loadJob). */
        struct Job
        {
            uint32                    _firstThread{ 0 };
            uint32                    _threadCount{ 0 };
            uint32                    _wordOffset{ 0 };
            uint32                    _emissionOffset{ 0 };
            uint32                    _emissionRank{ 0 };
            uint32                    _firstIndex{ 0 };
            uint32                    _firstVertex{ 0 };
            SceneVoxelGrid            _grid{};
            SceneVoxelGrid::CellRange _clip{};
            float4x4                  _world{};
        };

        static uint32 readBits( float32 value ) { return BitUtil::floatToBits( value ); }

        static Job loadJob( const vector<float4>& listRow, uint32 jobIndex )
        {
            const size_t  row = 1u + static_cast<size_t>( jobIndex ) * SceneVoxelizer::kJobRowCount;
            const float4& r0  = listRow[row];
            const float4& r1  = listRow[row + 1u];
            const float4& r2  = listRow[row + 2u];
            const float4& r3  = listRow[row + 3u];
            const float4& r4  = listRow[row + 4u];
            Job           job;
            job._firstThread       = readBits( r0._x );
            job._threadCount       = readBits( r0._y );
            job._wordOffset        = readBits( r0._z );
            job._emissionOffset    = readBits( r0._w );
            job._grid._dimX        = readBits( r1._x );
            job._grid._dimY        = readBits( r1._y );
            job._grid._dimZ        = readBits( r1._z );
            job._emissionRank      = readBits( r1._w );
            job._clip._arrFirst[0] = readBits( r2._x );
            job._clip._arrFirst[1] = readBits( r2._y );
            job._clip._arrFirst[2] = readBits( r2._z );
            job._firstIndex        = readBits( r2._w );
            job._clip._arrLast[0]  = readBits( r3._x );
            job._clip._arrLast[1]  = readBits( r3._y );
            job._clip._arrLast[2]  = readBits( r3._z );
            job._firstVertex       = readBits( r3._w );
            job._grid._origin      = float3{ r4._x, r4._y, r4._z };
            job._grid._cellSize    = r4._w;
            for ( uint32 matrixRow = 0; matrixRow < 4; ++matrixRow )
            {
                const float4&  source = listRow[row + 5u + matrixRow];
                float32* const pRow   = job._world.data() + matrixRow * 4u;
                pRow[0]               = source._x;
                pRow[1]               = source._y;
                pRow[2]               = source._z;
                pRow[3]               = source._w;
            }
            return job;
        }

        static void executeJobs( const vector<float4>& listRow, const vector<float3>& listPosition, const vector<uint32>& listIndex, vector<uint32>& inoutWords )
        {
            const uint32 jobCount    = readBits( listRow[0]._x );
            const uint32 threadCount = readBits( listRow[0]._y );
            const uint32 mode        = readBits( listRow[0]._z );
            for ( uint32 jobIndex = 0; jobIndex < jobCount; ++jobIndex )
            {
                const Job job = loadJob( listRow, jobIndex );
                SW_ASSERT( job._firstThread + job._threadCount <= threadCount );
                if ( mode == SceneVoxelizer::kJobModeRestore )
                {
                    const float4& staticRow            = listRow[1u + jobIndex * SceneVoxelizer::kJobRowCount + kJobWorldRow];
                    const uint32  staticWordOffset     = readBits( staticRow._x );
                    const uint32  staticEmissionOffset = readBits( staticRow._y );
                    forEachCell( job._clip, [&job, &inoutWords, staticWordOffset, staticEmissionOffset]( uint32 cellX, uint32 cellY, uint32 cellZ )
                    {
                        const uint32 index = job._grid.computeCellIndex( cellX, cellY, cellZ );
                        const uint32 bit   = 1u << ( index & 31u );
                        uint32&      word  = inoutWords[job._wordOffset + ( index >> 5 )];
                        word               = ( word & ~bit ) | ( inoutWords[staticWordOffset + ( index >> 5 )] & bit );
                        if ( job._emissionOffset != SceneVoxelizer::kNoEmission && staticEmissionOffset != SceneVoxelizer::kNoEmission )
                            inoutWords[job._emissionOffset + index] = inoutWords[staticEmissionOffset + index];
                    } );
                    continue;
                }
                if ( mode == SceneVoxelizer::kJobModeClear )
                {
                    forEachCell( job._clip, [&job, &inoutWords]( uint32 cellX, uint32 cellY, uint32 cellZ )
                    {
                        const uint32 index = job._grid.computeCellIndex( cellX, cellY, cellZ );
                        inoutWords[job._wordOffset + ( index >> 5 )] &= ~( 1u << ( index & 31u ) );
                        if ( job._emissionOffset != SceneVoxelizer::kNoEmission )
                            inoutWords[job._emissionOffset + index] = 0u;
                    } );
                    continue;
                }
                for ( uint32 triangle = 0; triangle < job._threadCount; ++triangle )
                {
                    float3 arrCorner[3];
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        arrCorner[corner] = float3::transform( listPosition[job._firstVertex + listIndex[job._firstIndex + triangle * 3u + corner]], job._world ) - job._grid._origin;
                    }
                    paintTriangle( job, arrCorner[0], arrCorner[1], arrCorner[2], inoutWords );
                }
            }
        }

        /** @brief 축 @p axis 를 마지막 성분으로 돌린 벡터입니다(scene_voxelize.hlsl toProjected). */
        static float3 toProjected( const float3& value, uint32 axis )
        {
            if ( axis == 0 )
                return float3{ value._y, value._z, value._x };
            return axis == 1 ? float3{ value._z, value._x, value._y } : value;
        }

        /** @brief 칠하기 — 삼각형 하나를 노멀의 가장 큰 축으로 투영해 켭니다(scene_voxelize.hlsl paintTriangle). */
        static void paintTriangle( const Job& job, const float3& cornerA, const float3& cornerB, const float3& cornerC, vector<uint32>& inoutWords )
        {
            const AABB                box{ float3::min( cornerA, float3::min( cornerB, cornerC ) ) + job._grid._origin, float3::max( cornerA, float3::max( cornerB, cornerC ) ) + job._grid._origin };
            SceneVoxelGrid::CellRange range{};
            if ( job._grid.findCellRange( box, range ) == false )
                return;
            float3 low{};
            float3 high{};
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                ( &low._x )[axis]  = static_cast<float32>( MathUtil::max( range._arrFirst[axis], job._clip._arrFirst[axis] ) );
                ( &high._x )[axis] = static_cast<float32>( MathUtil::min( range._arrLast[axis], job._clip._arrLast[axis] ) );
                if ( ( &low._x )[axis] > ( &high._x )[axis] )
                    return;
            }
            const float3 normal = ( cornerB - cornerA ).cross( cornerC - cornerB );
            const float3 size{ MathUtil::abs( normal._x ), MathUtil::abs( normal._y ), MathUtil::abs( normal._z ) };
            const uint32 axis = ( size._x >= size._y && size._x >= size._z ) ? 0u : ( size._y >= size._z ? 1u : 2u );
            const float3 a    = toProjected( cornerA, axis );
            const float3 b    = toProjected( cornerB, axis );
            const float3 c    = toProjected( cornerC, axis );
            const float3 n    = toProjected( normal, axis );
            if ( MathUtil::abs( n._z ) <= 0.0f )
                return;
            const float3  first    = toProjected( low, axis );
            const float3  last     = toProjected( high, axis );
            const float32 cellSize = job._grid._cellSize;
            const float32 half     = cellSize * 0.5f * ( 1.0f + kOverlapSlack );
            const float32 slopeU   = n._x / n._z;
            const float32 slopeV   = n._y / n._z;
            const float32 spread   = half * ( MathUtil::abs( slopeU ) + MathUtil::abs( slopeV ) ) + cellSize * kLayerSlack;
            for ( float32 columnV = first._y; columnV <= last._y; columnV += 1.0f )
            {
                for ( float32 columnU = first._x; columnU <= last._x; columnU += 1.0f )
                {
                    const float32 centerU    = ( columnU + 0.5f ) * cellSize;
                    const float32 centerV    = ( columnV + 0.5f ) * cellSize;
                    const float32 planeDepth = a._z - ( slopeU * ( centerU - a._x ) + slopeV * ( centerV - a._y ) );
                    const float32 layerFirst = MathUtil::clamp( MathUtil::floor( ( planeDepth - spread ) / cellSize ), first._z, last._z + 1.0f );
                    const float32 layerLast  = MathUtil::clamp( MathUtil::floor( ( planeDepth + spread ) / cellSize ), first._z - 1.0f, last._z );
                    for ( float32 layer = layerFirst; layer <= layerLast; layer += 1.0f )
                    {
                        if ( overlapsTriangleBox( float3{ centerU, centerV, ( layer + 0.5f ) * cellSize }, half, a, b, c ) == false )
                            continue;
                        // 투영의 역(scene_voxelize.hlsl fromProjectedCell) — 두 번 더 돌리면 제자리다.
                        const float3 cell  = toProjected( toProjected( float3{ columnU, columnV, layer }, axis ), axis );
                        const uint32 index = job._grid.computeCellIndex( static_cast<uint32>( cell._x ), static_cast<uint32>( cell._y ), static_cast<uint32>( cell._z ) );
                        setBit( inoutWords, job._wordOffset, index );
                        if ( job._emissionRank != 0u && job._emissionOffset != SceneVoxelizer::kNoEmission )
                            inoutWords[job._emissionOffset + index] = MathUtil::max( inoutWords[job._emissionOffset + index], job._emissionRank );
                    }
                }
            }
        }

        static void setBit( vector<uint32>& inoutWords, uint32 wordOffset, uint32 cellIndex ) { inoutWords[wordOffset + ( cellIndex >> 5 )] |= 1u << ( cellIndex & 31u ); }

        template <typename Visit>
        static void forEachCell( const SceneVoxelGrid::CellRange& range, Visit&& visit )
        {
            for ( uint32 cellZ = range._arrFirst[2]; cellZ <= range._arrLast[2]; ++cellZ )
            {
                for ( uint32 cellY = range._arrFirst[1]; cellY <= range._arrLast[1]; ++cellY )
                {
                    for ( uint32 cellX = range._arrFirst[0]; cellX <= range._arrLast[0]; ++cellX )
                    {
                        visit( cellX, cellY, cellZ );
                    }
                }
            }
        }

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
} // namespace sw::test
