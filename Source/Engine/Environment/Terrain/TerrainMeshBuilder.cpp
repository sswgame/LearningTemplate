#include "pch.h"

#include "Engine/Environment/Terrain/TerrainMeshBuilder.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    namespace
    {
        struct TerrainMeshBuilderInternal
        {
            /** @brief 접은 뒤의 격자 위치입니다(청크 안의 셀 번호). */
            struct GridPoint
            {
                uint32 _x{ 0 };
                uint32 _z{ 0 };

                bool operator==( const GridPoint& other ) const { return _x == other._x && _z == other._z; }
            };

            /**
             * @brief 청크 안 격자 위치 (x, z)(기본 셀 단위)를 변 접기에 맞춰 옮깁니다.
             * @details 이웃이 더 거친 변 위의 점은 그 변을 따라 아래쪽 거친 격자 점으로 내린다. 모서리 점은 두 변 모두의 거친 격자에 있다.
             */
            static GridPoint collapse( uint32 x, uint32 z, uint32 chunkCells, const uint32 ( &arrSideStep )[4] )
            {
                GridPoint    point{ x, z };
                const uint32 westStep  = arrSideStep[static_cast<uint32>( TerrainChunkSide::West )];
                const uint32 eastStep  = arrSideStep[static_cast<uint32>( TerrainChunkSide::East )];
                const uint32 southStep = arrSideStep[static_cast<uint32>( TerrainChunkSide::South )];
                const uint32 northStep = arrSideStep[static_cast<uint32>( TerrainChunkSide::North )];
                if ( x == 0 )
                    point._z = ( z / westStep ) * westStep;
                else if ( x == chunkCells )
                    point._z = ( z / eastStep ) * eastStep;
                if ( z == 0 )
                    point._x = ( x / southStep ) * southStep;
                else if ( z == chunkCells )
                    point._x = ( x / northStep ) * northStep;
                return point;
            }

            static RHIVertex makeVertex( const TerrainHeightfield& heightfield, const float3& translation, uint32 centerSampleX, uint32 centerSampleZ, uint32 sampleX,
                                         uint32 sampleZ )
            {
                const float32 height   = heightfield.getSampleHeight( static_cast<int32>( sampleX ), static_cast<int32>( sampleZ ) );
                const float3  normal   = heightfield.computeSampleNormal( sampleX, sampleZ );
                const float32 last     = static_cast<float32>( heightfield.getResolution() - 1 );
                const float2  cellSize = heightfield.getCellSize();
                // x 와 z 는 (정수 셀 차이) × 셀 크기다. (원점 + 셀 × 크기) - 이동 으로 빼면 청크마다 반올림이 달라져 이웃의 같은 정점이 어긋난다.
                const float32 localX = static_cast<float32>( static_cast<int32>( sampleX ) - static_cast<int32>( centerSampleX ) ) * cellSize._x;
                const float32 localZ = static_cast<float32>( static_cast<int32>( sampleZ ) - static_cast<int32>( centerSampleZ ) ) * cellSize._y;
                RHIVertex     vertex{};
                vertex._arrPosition[0] = localX;
                vertex._arrPosition[1] = height - translation._y;
                vertex._arrPosition[2] = localZ;
                vertex._arrNormal[0]   = normal._x;
                vertex._arrNormal[1]   = normal._y;
                vertex._arrNormal[2]   = normal._z;
                vertex._arrUv[0]       = static_cast<float32>( sampleX ) / last;
                vertex._arrUv[1]       = static_cast<float32>( sampleZ ) / last;
                vertex._arrColor[0]    = 1.0f;
                vertex._arrColor[1]    = 1.0f;
                vertex._arrColor[2]    = 1.0f;
                vertex._arrColor[3]    = 1.0f;
                return vertex;
            }

            /// 섞을 곳이 없는 청크(더 거친 LOD 가 없다)의 섞기 거리입니다 - 어떤 거리도 이 값에 닿지 않아 섞는 비율이 늘 0 이다.
            static constexpr float32 kNoMorphDistance = 1.0e30f;

            /**
             * @brief 샘플 (sampleX, sampleZ) 의 높이를 한 걸음 거친 LOD(간격 2 × @p step)의 메시가 보이는 높이로 바꿨을 때의 변화량입니다.
             * @details 거친 격자 위의 정점은 0 이다. 한 축만 거친 격자 사이에 있으면 거친 변의 중점(두 이웃의 평균)이고, 두 축이 모두 사이에 있으면 거친 셀의 중심이라
             *          거친 메시가 삼각형을 나누는 대각선((x, z)-(x+1, z+1)) 위의 점이다 - 대각선 두 끝의 평균이다.
             */
            static float32 computeCoarseHeightDelta( const TerrainHeightfield& heightfield, uint32 sampleX, uint32 sampleZ, uint32 step )
            {
                const uint32 coarseStep = step * 2;
                const bool   bBetweenX  = sampleX % coarseStep != 0;
                const bool   bBetweenZ  = sampleZ % coarseStep != 0;
                if ( bBetweenX == false && bBetweenZ == false )
                    return 0.0f;
                const int32   x      = static_cast<int32>( sampleX );
                const int32   z      = static_cast<int32>( sampleZ );
                const int32   offset = static_cast<int32>( step );
                const float32 fine   = heightfield.getSampleHeight( x, z );
                float32       coarse = 0.0f;
                if ( bBetweenX && bBetweenZ )
                    coarse = 0.5f * ( heightfield.getSampleHeight( x - offset, z - offset ) + heightfield.getSampleHeight( x + offset, z + offset ) );
                else if ( bBetweenX )
                    coarse = 0.5f * ( heightfield.getSampleHeight( x - offset, z ) + heightfield.getSampleHeight( x + offset, z ) );
                else
                    coarse = 0.5f * ( heightfield.getSampleHeight( x, z - offset ) + heightfield.getSampleHeight( x, z + offset ) );
                return coarse - fine;
            }

            /** @brief 기본 셀 [x0, x0+step) × [z0, z0+step) 에 구멍이 하나라도 있으면 true 를 반환합니다. */
            static bool hasHole( const TerrainHeightfield& heightfield, uint32 cellX0, uint32 cellZ0, uint32 step )
            {
                if ( heightfield.getHoleCells().empty() )
                    return false;
                for ( uint32 offsetZ = 0; offsetZ < step; ++offsetZ )
                {
                    for ( uint32 offsetX = 0; offsetX < step; ++offsetX )
                    {
                        if ( heightfield.isHoleCell( cellX0 + offsetX, cellZ0 + offsetZ ) )
                            return true;
                    }
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool TerrainMeshBuilder::makeLayout( uint32 resolution, uint32 chunkCells, TerrainChunkLayout& outLayout )
    {
        outLayout                      = TerrainChunkLayout{};
        const bool bInvalidChunkLayout = resolution < 2 || chunkCells == 0 || MathUtil::isPowerOfTwo( chunkCells ) == false || ( resolution - 1 ) % chunkCells != 0;
        if ( bInvalidChunkLayout )
            return false;
        outLayout._chunkCells  = chunkCells;
        outLayout._chunkCountX = ( resolution - 1 ) / chunkCells;
        outLayout._chunkCountZ = outLayout._chunkCountX;
        outLayout._maxLOD      = MathUtil::countTrailingZeros( static_cast<uint64>( chunkCells ) );
        return true;
    }

    float3 TerrainMeshBuilder::computeChunkTranslation( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ )
    {
        const float3 origin   = heightfield.getOrigin();
        const float2 cellSize = heightfield.getCellSize();
        const uint32 centerX  = chunkX * layout._chunkCells + layout._chunkCells / 2;
        const uint32 centerZ  = chunkZ * layout._chunkCells + layout._chunkCells / 2;
        return float3{ origin._x + static_cast<float32>( centerX ) * cellSize._x, origin._y, origin._z + static_cast<float32>( centerZ ) * cellSize._y };
    }

    float32 TerrainMeshBuilder::computeChunkBoundsRadius( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ )
    {
        const float3 translation = computeChunkTranslation( heightfield, layout, chunkX, chunkZ );
        float32      maxVertical = 0.0f;
        for ( uint32 offsetZ = 0; offsetZ <= layout._chunkCells; ++offsetZ )
        {
            for ( uint32 offsetX = 0; offsetX <= layout._chunkCells; ++offsetX )
            {
                const float32 height = heightfield.getSampleHeight( static_cast<int32>( chunkX * layout._chunkCells + offsetX ),
                                                                    static_cast<int32>( chunkZ * layout._chunkCells + offsetZ ) );
                maxVertical          = MathUtil::max( maxVertical, MathUtil::abs( height - translation._y ) );
            }
        }
        const float2  cellSize = heightfield.getCellSize();
        const float32 halfX    = 0.5f * static_cast<float32>( layout._chunkCells ) * cellSize._x;
        const float32 halfZ    = 0.5f * static_cast<float32>( layout._chunkCells ) * cellSize._y;
        return MathUtil::sqrt( halfX * halfX + halfZ * halfZ + maxVertical * maxVertical );
    }

    uint32 TerrainMeshBuilder::selectLOD( float32 distance, float32 lodDistance, uint32 maxLOD )
    {
        if ( lodDistance <= 0.0f )
            return 0;
        uint32  lod       = 0;
        float32 threshold = lodDistance;
        while ( lod < maxLOD && distance >= threshold )
        {
            ++lod;
            threshold *= 2.0f;
        }
        return lod;
    }

    void TerrainMeshBuilder::makeChunkVertices( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ, uint32 lod,
                                                const uint32 ( &arrNeighborLOD )[static_cast<uint32>( TerrainChunkSide::Count )], vector<RHIVertex>& outListVertex )
    {
        using Internal = TerrainMeshBuilderInternal;
        outListVertex.clear();
        if ( heightfield.isValid() == false || layout._chunkCells == 0 )
            return;
        lod               = MathUtil::min( lod, layout._maxLOD );
        const uint32 step = 1u << lod;
        // 변마다 접는 간격(기본 셀 단위)이다. 이웃이 더 고우면 그쪽이 이쪽에 맞춰 접으므로 이쪽은 자기 간격을 쓴다.
        uint32 arrSideStep[4]{};
        for ( uint32 side = 0; side < 4; ++side )
        {
            arrSideStep[side] = 1u << MathUtil::max( lod, MathUtil::min( arrNeighborLOD[side], layout._maxLOD ) );
        }

        const float3 translation = computeChunkTranslation( heightfield, layout, chunkX, chunkZ );
        const uint32 baseX       = chunkX * layout._chunkCells;
        const uint32 baseZ       = chunkZ * layout._chunkCells;
        const uint32 cells       = layout._chunkCells;
        outListVertex.reserve( static_cast<size_t>( cells / step ) * ( cells / step ) * 6 );

        for ( uint32 z = 0; z < cells; z += step )
        {
            for ( uint32 x = 0; x < cells; x += step )
            {
                if ( Internal::hasHole( heightfield, baseX + x, baseZ + z, step ) )
                    continue;
                const Internal::GridPoint p00 = Internal::collapse( x, z, cells, arrSideStep );
                const Internal::GridPoint p10 = Internal::collapse( x + step, z, cells, arrSideStep );
                const Internal::GridPoint p01 = Internal::collapse( x, z + step, cells, arrSideStep );
                const Internal::GridPoint p11 = Internal::collapse( x + step, z + step, cells, arrSideStep );
                // 위에서 볼 때 앞면이 되는 감김(MeshUtil::createPlane 과 같다): (00, 01, 11), (00, 11, 10). 접혀 넓이가 0 이 된 것은 버린다.
                const Internal::GridPoint arrTriangle[2][3] = {
                    {p00, p01, p11},
                    {p00, p11, p10}
                };
                for ( const Internal::GridPoint( &triangle )[3] : arrTriangle )
                {
                    const bool bDegenerateTriangle = triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[0] == triangle[2];
                    if ( bDegenerateTriangle )
                        continue;
                    for ( const Internal::GridPoint& corner : triangle )
                    {
                        outListVertex.push_back( Internal::makeVertex( heightfield, translation, baseX + cells / 2, baseZ + cells / 2, baseX + corner._x, baseZ + corner._z ) );
                    }
                }
            }
        }
    }

    void TerrainMeshBuilder::applyGeomorph( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ, uint32 lod, float32 lodDistance,
                                            vector<RHIVertex>& inoutListVertex )
    {
        using Internal = TerrainMeshBuilderInternal;
        if ( heightfield.isValid() == false || layout._chunkCells == 0 )
            return;
        lod                       = MathUtil::min( lod, layout._maxLOD );
        const uint32  step        = 1u << lod;
        const uint32  cells       = layout._chunkCells;
        const uint32  baseX       = chunkX * cells;
        const uint32  baseZ       = chunkZ * cells;
        const float32 last        = static_cast<float32>( heightfield.getResolution() - 1 );
        const float3  origin      = heightfield.getOrigin();
        const float2  cellSize    = heightfield.getCellSize();
        const bool    bHasCoarser = lod < layout._maxLOD && lodDistance > 0.0f;
        // 이 LOD 를 쓰는 거리는 교체 임계값(selectLOD) 아래다 - 섞기는 그 임계값에 가까워질 때 일어난다.
        const float32 switchDistance = lodDistance * static_cast<float32>( step );
        const float32 morphStart     = bHasCoarser ? switchDistance * kMorphStartRatio : Internal::kNoMorphDistance;
        const float32 morphEnd       = bHasCoarser ? switchDistance * kMorphEndRatio : Internal::kNoMorphDistance;
        const float32 minX           = origin._x + static_cast<float32>( baseX ) * cellSize._x;
        const float32 maxX           = origin._x + static_cast<float32>( baseX + cells ) * cellSize._x;
        const float32 minZ           = origin._z + static_cast<float32>( baseZ ) * cellSize._y;
        const float32 maxZ           = origin._z + static_cast<float32>( baseZ + cells ) * cellSize._y;
        const float32 groundHeight   = heightfield.getSampleHeight( static_cast<int32>( baseX + cells / 2 ), static_cast<int32>( baseZ + cells / 2 ) );
        for ( RHIVertex& vertex : inoutListVertex )
        {
            const uint32  sampleX = static_cast<uint32>( MathUtil::round( vertex._arrUv[0] * last ) );
            const uint32  sampleZ = static_cast<uint32>( MathUtil::round( vertex._arrUv[1] * last ) );
            const float32 delta   = bHasCoarser ? Internal::computeCoarseHeightDelta( heightfield, sampleX, sampleZ, step ) : 0.0f;
            vertex._arrColor[0]   = delta;
            vertex._arrColor[1]   = morphStart;
            vertex._arrColor[2]   = morphEnd;
            vertex._arrColor[3]   = minX;
            vertex._arrTangent[0] = minZ;
            vertex._arrTangent[1] = maxX;
            vertex._arrTangent[2] = maxZ;
            vertex._arrTangent[3] = groundHeight;
        }
    }
} // namespace sw
