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
            /** @brief 접은 뒤의 격자 자리(청크 안 칸 번호)입니다. */
            struct GridPoint
            {
                uint32 _x{ 0 };
                uint32 _z{ 0 };

                bool operator==( const GridPoint& other ) const { return _x == other._x && _z == other._z; }
            };

            /**
             * @brief 청크 안 격자 자리 (x, z)(기본 칸 단위)를 변 접기에 맞춰 옮깁니다.
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
                // x · z 는 (정수 칸 차) × 칸 크기다 — (원점 + 칸 × 크기) − 이동 으로 빼면 청크마다 반올림이 달라져 이웃의 같은 정점이 어긋난다.
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

            /** @brief 기본 칸 [x0, x0+step) × [z0, z0+step) 에 구멍이 하나라도 있으면 true 입니다. */
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
        outLayout = TerrainChunkLayout{};
        if ( resolution < 2 || chunkCells == 0 || MathUtil::isPowerOfTwo( chunkCells ) == false || ( resolution - 1 ) % chunkCells != 0 )
            return false;
        outLayout._chunkCells  = chunkCells;
        outLayout._chunkCountX = ( resolution - 1 ) / chunkCells;
        outLayout._chunkCountZ = outLayout._chunkCountX;
        outLayout._maxLod      = MathUtil::countTrailingZeros( static_cast<uint64>( chunkCells ) );
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

    uint32 TerrainMeshBuilder::selectLod( float32 distance, float32 lodDistance, uint32 maxLod )
    {
        if ( lodDistance <= 0.0f )
            return 0;
        uint32  lod       = 0;
        float32 threshold = lodDistance;
        while ( lod < maxLod && distance >= threshold )
        {
            ++lod;
            threshold *= 2.0f;
        }
        return lod;
    }

    void TerrainMeshBuilder::buildChunkVertices( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ, uint32 lod,
                                                 const uint32 ( &arrNeighborLod )[static_cast<uint32>( TerrainChunkSide::Count )], vector<RHIVertex>& outListVertex )
    {
        using Internal = TerrainMeshBuilderInternal;
        outListVertex.clear();
        if ( heightfield.isValid() == false || layout._chunkCells == 0 )
            return;
        lod               = MathUtil::min( lod, layout._maxLod );
        const uint32 step = 1u << lod;
        // 변마다 접는 간격(기본 칸 단위). 이웃이 더 고우면 그쪽이 이쪽에 맞춰 접으므로 이쪽은 자기 간격이다.
        uint32 arrSideStep[4]{};
        for ( uint32 side = 0; side < 4; ++side )
            arrSideStep[side] = 1u << MathUtil::max( lod, MathUtil::min( arrNeighborLod[side], layout._maxLod ) );

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
                    if ( triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[0] == triangle[2] )
                        continue;
                    for ( const Internal::GridPoint& corner : triangle )
                        outListVertex.push_back( Internal::makeVertex( heightfield, translation, baseX + cells / 2, baseZ + cells / 2, baseX + corner._x, baseZ + corner._z ) );
                }
            }
        }
    }
} // namespace sw
