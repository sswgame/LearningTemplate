#include "pch.h"

#include "GameFramework/Kits/Simulation/Voxel/VoxelTerrain.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Foundation/Utility/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/ValueNoise.h"
#include "GameFramework/Kits/Simulation/Voxel/VoxelWorld.h"

namespace sw
{
    namespace
    {
        struct VoxelTerrainInternal
        {
            /** @brief 카탈로그에 있으면 쓰고 없으면 건너뜁니다. 월드 밖도 건너뜁니다. */
            static void placeBlock( VoxelWorld& world, int32 x, int32 y, int32 z, VoxelBlockIndex block )
            {
                if ( block == kVoxelAirBlock )
                    return;
                (void)world.setBlock( x, y, z, block );
            }

            /** @brief 나무 하나 — 줄기와 위가 둥근 잎 덩어리입니다. 잎은 빈 칸에만 놓습니다. */
            static void placeTree( VoxelWorld& world, int32 x, int32 groundY, int32 z, int32 trunkHeight, VoxelBlockIndex logBlock, VoxelBlockIndex leavesBlock )
            {
                const int32 topY = groundY + trunkHeight;
                for ( int32 y = topY - 2; y <= topY + 1; ++y )
                {
                    const int32 radius = ( y <= topY - 1 ) ? 2 : 1;
                    for ( int32 dz = -radius; dz <= radius; ++dz )
                    {
                        for ( int32 dx = -radius; dx <= radius; ++dx )
                        {
                            const bool bCorner = MathUtil::abs( dx ) == radius && MathUtil::abs( dz ) == radius;
                            if ( bCorner && ( radius == 1 || y == topY - 2 ) )
                                continue;
                            if ( world.getBlock( x + dx, y, z + dz ) == kVoxelAirBlock )
                                placeBlock( world, x + dx, y, z + dz, leavesBlock );
                        }
                    }
                }
                for ( int32 y = groundY + 1; y <= topY; ++y )
                {
                    placeBlock( world, x, y, z, logBlock );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    int32 VoxelTerrainGenerator::computeSurfaceHeight( int32 x, int32 z, const VoxelTerrainSettings& settings )
    {
        const float32 noise = ValueNoise::sampleFractal( static_cast<float32>( x ) * settings._noiseScale, static_cast<float32>( z ) * settings._noiseScale,
                                                         settings._seed, settings._octaveCount );
        // 제곱으로 낮은 땅을 넓히고 산을 드물게 — 평지 · 물가가 생긴다.
        const int32 height = settings._baseHeight + static_cast<int32>( MathUtil::round( noise * noise * 1.6f * static_cast<float32>( settings._heightAmplitude ) ) );
        return MathUtil::clamp( height, 1, kVoxelChunkHeight - 10 );
    }

    VoxelTerrainReport VoxelTerrainGenerator::fillWorld( VoxelWorld& world, const VoxelTerrainSettings& settings )
    {
        VoxelTerrainReport report;
        report._minHeight                 = kVoxelChunkHeight;
        report._maxHeight                 = 0;
        const VoxelBlockCatalog* pCatalog = world.getCatalog();
        if ( pCatalog == nullptr )
            return report;

        const VoxelBlockIndex grassBlock   = pCatalog->findBlockIndex( settings._grassBlock );
        const VoxelBlockIndex dirtBlock    = pCatalog->findBlockIndex( settings._dirtBlock );
        const VoxelBlockIndex stoneBlock   = pCatalog->findBlockIndex( settings._stoneBlock );
        const VoxelBlockIndex sandBlock    = pCatalog->findBlockIndex( settings._sandBlock );
        const VoxelBlockIndex waterBlock   = pCatalog->findBlockIndex( settings._waterBlock );
        const VoxelBlockIndex bedrockBlock = pCatalog->findBlockIndex( settings._bedrockBlock );
        const VoxelBlockIndex logBlock     = pCatalog->findBlockIndex( settings._logBlock );
        const VoxelBlockIndex leavesBlock  = pCatalog->findBlockIndex( settings._leavesBlock );

        for ( int32 z = 0; z < world.getSizeZ(); ++z )
        {
            for ( int32 x = 0; x < world.getSizeX(); ++x )
            {
                const int32 height = computeSurfaceHeight( x, z, settings );
                report._minHeight  = MathUtil::min( report._minHeight, height );
                report._maxHeight  = MathUtil::max( report._maxHeight, height );
                const bool bBeach  = settings._waterLevel > 0 && height <= settings._waterLevel + 1;
                for ( int32 y = 0; y <= height; ++y )
                {
                    VoxelBlockIndex block = stoneBlock;
                    if ( y == 0 && bedrockBlock != kVoxelAirBlock )
                        block = bedrockBlock;
                    else if ( y == height )
                        block = bBeach ? sandBlock : grassBlock;
                    else if ( y > height - settings._dirtDepth )
                        block = bBeach ? sandBlock : dirtBlock;
                    VoxelTerrainInternal::placeBlock( world, x, y, z, block );
                }
                for ( int32 y = height + 1; y < settings._waterLevel; ++y )
                {
                    VoxelTerrainInternal::placeBlock( world, x, y, z, waterBlock );
                }
            }
        }

        // 나무는 지형이 다 선 뒤에 — 이웃 기둥이 나중에 잎을 덮어쓰지 않게.
        if ( logBlock != kVoxelAirBlock && settings._treeChance > 0.0f )
        {
            for ( int32 z = 2; z < world.getSizeZ() - 2; ++z )
            {
                for ( int32 x = 2; x < world.getSizeX() - 2; ++x )
                {
                    const int32 groundY = world.findTopSolidY( x, z );
                    if ( groundY < 0 || world.getBlock( x, groundY, z ) != grassBlock || groundY + 8 >= kVoxelChunkHeight )
                        continue;
                    const uint32  treeHash = GameHash::hashCoord( x, z, settings._seed ^ 0x5bd1e995u );
                    const float32 roll     = static_cast<float32>( treeHash & 0xffffu ) / 65535.0f;
                    if ( roll >= settings._treeChance )
                        continue;
                    // 이웃 나무와 줄기가 붙지 않게 — 3 칸 안에 줄기가 있으면 건너뛴다.
                    bool bCrowded = false;
                    for ( int32 dz = -3; dz <= 3 && bCrowded == false; ++dz )
                    {
                        for ( int32 dx = -3; dx <= 3 && bCrowded == false; ++dx )
                        {
                            bCrowded = world.getBlock( x + dx, groundY + 1, z + dz ) == logBlock;
                        }
                    }
                    if ( bCrowded )
                        continue;
                    const int32 trunkHeight = 4 + static_cast<int32>( ( treeHash >> 16 ) % 3u );
                    VoxelTerrainInternal::placeTree( world, x, groundY, z, trunkHeight, logBlock, leavesBlock );
                    ++report._treeCount;
                }
            }
        }
        world.markAllChunksDirty();
        return report;
    }
} // namespace sw
