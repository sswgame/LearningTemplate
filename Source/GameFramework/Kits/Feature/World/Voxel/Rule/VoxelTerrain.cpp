#include "pch.h"

#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelTerrain.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/ValueNoise.h"

#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    namespace
    {
        /** @brief 생성할 청크 하나의 블록입니다. 청크 밖 좌표는 읽으면 공기이고 쓰기는 버린다 — 이웃 청크에 선 나무의 잎이 이 청크에 걸친 몫만 남는다. */
        struct VoxelTerrainChunk
        {
            vector<VoxelBlockIndex>& _listBlock;
            int32                    _originX;
            int32                    _originZ;

            bool contains( int32 x, int32 y, int32 z ) const
            {
                return x >= _originX && z >= _originZ && x < _originX + kVoxelChunkSize && z < _originZ + kVoxelChunkSize && y >= 0 && y < kVoxelChunkHeight;
            }
            size_t computeIndex( int32 x, int32 y, int32 z ) const
            {
                return static_cast<size_t>( ( y * kVoxelChunkSize + ( z - _originZ ) ) * kVoxelChunkSize + ( x - _originX ) );
            }
            VoxelBlockIndex get( int32 x, int32 y, int32 z ) const { return contains( x, y, z ) ? _listBlock[computeIndex( x, y, z )] : kVoxelAirBlock; }
            /** @brief 카탈로그에 있으면 쓰고 없으면(공기) 건너뜁니다. 청크 밖도 건너뜁니다. */
            void place( int32 x, int32 y, int32 z, VoxelBlockIndex block )
            {
                if ( block != kVoxelAirBlock && contains( x, y, z ) )
                    _listBlock[computeIndex( x, y, z )] = block;
            }
        };

        /** @brief 생성에 쓰는 블록 번호입니다(카탈로그에 없는 id 는 공기 — 그 층을 건너뛴다). */
        struct VoxelTerrainPalette
        {
            VoxelBlockIndex _grass;
            VoxelBlockIndex _dirt;
            VoxelBlockIndex _stone;
            VoxelBlockIndex _sand;
            VoxelBlockIndex _water;
            VoxelBlockIndex _bedrock;
            VoxelBlockIndex _log;
            VoxelBlockIndex _leaves;
        };

        struct VoxelTerrainInternal
        {
            static constexpr uint32 kTreeSalt    = 0x5bd1e995u;
            static constexpr uint32 kOreSalt     = 0x27d4eb2fu;
            static constexpr int32  kTreeMargin  = 2; ///< 월드 가장자리에서 나무 줄기까지의 거리이자 잎 덩어리의 반경 — 이웃 청크의 이만큼 안 나무가 이 청크에 잎을 걸친다
            static constexpr int32  kTreeSpacing = 3; ///< 이 거리(체비쇼프) 안에는 나무가 둘 서지 않는다

            /** @brief 나무가 설 수 있는 기둥이면 그 나무 해시를 돌려줍니다(해시 판정 · 지면이 풀 · 높이 여유 · 월드 가장자리). 이웃 청크를 읽지 않는 순수 함수다. */
            static optional<uint32> findTreeSite( int32 x, int32 z, const VoxelTerrainSettings& settings, int32 sizeX, int32 sizeZ )
            {
                if ( x < kTreeMargin || z < kTreeMargin || x >= sizeX - kTreeMargin || z >= sizeZ - kTreeMargin )
                    return nullopt;
                const uint32  treeHash = HashUtil::hashCoord( x, z, settings._seed ^ kTreeSalt );
                const float32 roll     = static_cast<float32>( treeHash & 0xffffu ) / 65535.0f;
                if ( roll >= settings._treeChance )
                    return nullopt;
                const int32 groundY = VoxelTerrainSettings::computeSurfaceHeight( x, z, settings );
                const bool  bBeach  = settings._waterLevel > 0 && groundY <= settings._waterLevel + 1;
                if ( bBeach || groundY + 8 >= kVoxelChunkHeight )
                    return nullopt; // 모래 위 · 하늘에 닿는 자리에는 서지 않는다
                return treeHash;
            }

            /** @brief 나무 자리 중 이웃 자리(거리 `kTreeSpacing` 안)보다 해시가 크면(같으면 좌표가 큰 쪽) 섭니다. 생성 순서와 상관없이 같은 나무가 선다. */
            static bool isTreeRoot( int32 x, int32 z, const VoxelTerrainSettings& settings, int32 sizeX, int32 sizeZ )
            {
                const optional<uint32> treeHash = findTreeSite( x, z, settings, sizeX, sizeZ );
                if ( treeHash.has_value() == false )
                    return false;
                for ( int32 dz = -kTreeSpacing; dz <= kTreeSpacing; ++dz )
                {
                    for ( int32 dx = -kTreeSpacing; dx <= kTreeSpacing; ++dx )
                    {
                        if ( dx == 0 && dz == 0 )
                            continue;
                        const optional<uint32> otherHash  = findTreeSite( x + dx, z + dz, settings, sizeX, sizeZ );
                        const bool             bOtherWins = otherHash.has_value() && ( *otherHash > *treeHash || ( *otherHash == *treeHash && ( dz > 0 || ( dz == 0 && dx > 0 ) ) ) );
                        if ( bOtherWins )
                            return false;
                    }
                }
                return true;
            }

            /** @brief 나무 하나를 세웁니다. 줄기와 위가 둥근 잎 덩어리로 이루어지며, 잎은 빈 블록에만 놓고 줄기는 잎을 덮는다(어느 나무를 먼저 세워도 같다). */
            static void placeTree( VoxelTerrainChunk& chunk, int32 x, int32 groundY, int32 z, int32 trunkHeight, const VoxelTerrainPalette& blocks )
            {
                const int32 topY = groundY + trunkHeight;
                for ( int32 y = topY - 2; y <= topY + 1; ++y )
                {
                    const int32 radius = ( y <= topY - 1 ) ? kTreeMargin : 1;
                    for ( int32 dz = -radius; dz <= radius; ++dz )
                    {
                        for ( int32 dx = -radius; dx <= radius; ++dx )
                        {
                            const bool bCorner = MathUtil::abs( dx ) == radius && MathUtil::abs( dz ) == radius;
                            if ( bCorner && ( radius == 1 || y == topY - 2 ) )
                                continue;
                            if ( chunk.get( x + dx, y, z + dz ) == kVoxelAirBlock )
                                chunk.place( x + dx, y, z + dz, blocks._leaves );
                        }
                    }
                }
                for ( int32 y = groundY + 1; y <= topY; ++y )
                {
                    chunk.place( x, y, z, blocks._log );
                }
            }

            /** @brief 기둥에서 가장 높은 단단한 블록의 y 입니다(잎 포함 — 잎 아래 풀은 눈을 맞지 않는다). */
            static optional<int32> findTopSolidY( const VoxelTerrainChunk& chunk, const VoxelBlockCatalog& catalog, int32 x, int32 z )
            {
                for ( int32 y = kVoxelChunkHeight - 1; y >= 0; --y )
                {
                    if ( catalog.isSolid( chunk.get( x, y, z ) ) )
                        return y;
                }
                return nullopt;
            }

            /** @brief 높은 풀 꼭대기의 눈과 돌 속 광석입니다. 광석 해시는 나무와 다른 소금을 섞은 같은 씨앗이다. */
            static void decorate( VoxelTerrainChunk& chunk, const VoxelBlockCatalog& catalog, const VoxelTerrainSettings& settings, const VoxelTerrainPalette& blocks )
            {
                const VoxelBlockIndex   snowBlock = settings._snowBlock.empty() ? kVoxelAirBlock : catalog.findBlockIndex( settings._snowBlock );
                vector<VoxelBlockIndex> listOreBlock;
                listOreBlock.reserve( settings._listOreLayer.size() );
                for ( const VoxelOreLayer& layer : settings._listOreLayer )
                {
                    listOreBlock.push_back( catalog.findBlockIndex( layer._block ) );
                }
                for ( int32 z = chunk._originZ; z < chunk._originZ + kVoxelChunkSize; ++z )
                {
                    for ( int32 x = chunk._originX; x < chunk._originX + kVoxelChunkSize; ++x )
                    {
                        const optional<int32> foundTop = findTopSolidY( chunk, catalog, x, z );
                        if ( foundTop.has_value() == false )
                            continue; // 빈 기둥에는 눈도 광석도 없다
                        const int32 topY           = *foundTop;
                        const bool  bShouldSnowCap = snowBlock != kVoxelAirBlock && topY >= settings._snowHeight && chunk.get( x, topY, z ) == blocks._grass;
                        if ( bShouldSnowCap )
                            chunk.place( x, topY, z, snowBlock );
                        if ( listOreBlock.empty() || blocks._stone == kVoxelAirBlock )
                            continue;
                        for ( int32 y = 1; y < topY - 3; ++y )
                        {
                            if ( chunk.get( x, y, z ) != blocks._stone )
                                continue;
                            const float32 roll      = HashUtil::toUnitFloat( HashUtil::hashCoord( x, y, z, settings._seed ^ kOreSalt ) );
                            float32       threshold = 0.0f;
                            for ( size_t layerIndex = 0; layerIndex < listOreBlock.size(); ++layerIndex )
                            {
                                const VoxelOreLayer& layer = settings._listOreLayer[layerIndex];
                                threshold += layer._chance;
                                if ( roll >= threshold )
                                    continue;
                                if ( y < layer._maxHeight )
                                    chunk.place( x, y, z, listOreBlock[layerIndex] );
                                break;
                            }
                        }
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    int32 VoxelTerrainSettings::computeSurfaceHeight( int32 x, int32 z, const VoxelTerrainSettings& settings )
    {
        const float32 noise = ValueNoise::sampleFractal( static_cast<float32>( x ) * settings._noiseScale, static_cast<float32>( z ) * settings._noiseScale,
                                                         settings._seed, settings._octaveCount );
        // 제곱을 취해 낮은 땅을 넓히고 산을 드물게 만든다. 그래서 평지와 물가가 생긴다.
        const int32 height = settings._baseHeight + static_cast<int32>( MathUtil::round( noise * noise * 1.6f * static_cast<float32>( settings._heightAmplitude ) ) );
        return MathUtil::clamp( height, 1, kVoxelChunkHeight - 10 );
    }

    VoxelTerrainReport VoxelTerrainSettings::fillChunk( const VoxelWorld& world, const VoxelTerrainSettings& settings, int32 chunkX, int32 chunkZ, vector<VoxelBlockIndex>& outBlocks )
    {
        VoxelTerrainReport report;
        report._minHeight                 = kVoxelChunkHeight;
        report._maxHeight                 = 0;
        const VoxelBlockCatalog* pCatalog = world.getCatalog();
        outBlocks.assign( static_cast<size_t>( kVoxelChunkVolume ), kVoxelAirBlock );
        if ( pCatalog == nullptr )
            return report;

        const VoxelTerrainPalette blocks{ pCatalog->findBlockIndex( settings._grassBlock ), pCatalog->findBlockIndex( settings._dirtBlock ),
                                          pCatalog->findBlockIndex( settings._stoneBlock ), pCatalog->findBlockIndex( settings._sandBlock ),
                                          pCatalog->findBlockIndex( settings._waterBlock ), pCatalog->findBlockIndex( settings._bedrockBlock ),
                                          pCatalog->findBlockIndex( settings._logBlock ), pCatalog->findBlockIndex( settings._leavesBlock ) };
        VoxelTerrainChunk         chunk{ outBlocks, chunkX * kVoxelChunkSize, chunkZ * kVoxelChunkSize };

        for ( int32 z = chunk._originZ; z < chunk._originZ + kVoxelChunkSize; ++z )
        {
            for ( int32 x = chunk._originX; x < chunk._originX + kVoxelChunkSize; ++x )
            {
                const int32 height = computeSurfaceHeight( x, z, settings );
                report._minHeight  = MathUtil::min( report._minHeight, height );
                report._maxHeight  = MathUtil::max( report._maxHeight, height );
                const bool bBeach  = settings._waterLevel > 0 && height <= settings._waterLevel + 1;
                for ( int32 y = 0; y <= height; ++y )
                {
                    VoxelBlockIndex block = blocks._stone;
                    if ( y == 0 && blocks._bedrock != kVoxelAirBlock )
                        block = blocks._bedrock;
                    else if ( y == height )
                        block = bBeach ? blocks._sand : blocks._grass;
                    else if ( y > height - settings._dirtDepth )
                        block = bBeach ? blocks._sand : blocks._dirt;
                    chunk.place( x, y, z, block );
                }
                for ( int32 y = height + 1; y < settings._waterLevel; ++y )
                {
                    chunk.place( x, y, z, blocks._water );
                }
            }
        }

        // 나무는 지형을 다 채운 뒤에 세운다. 이웃 청크에 줄기가 선 나무도 잎이 이 청크에 걸칠 수 있어 테두리만큼 넓게 본다.
        if ( blocks._log != kVoxelAirBlock && blocks._grass != kVoxelAirBlock && settings._treeChance > 0.0f )
        {
            const int32 margin = VoxelTerrainInternal::kTreeMargin;
            for ( int32 z = chunk._originZ - margin; z < chunk._originZ + kVoxelChunkSize + margin; ++z )
            {
                for ( int32 x = chunk._originX - margin; x < chunk._originX + kVoxelChunkSize + margin; ++x )
                {
                    if ( VoxelTerrainInternal::isTreeRoot( x, z, settings, world.getSizeX(), world.getSizeZ() ) == false )
                        continue;
                    const uint32 treeHash    = HashUtil::hashCoord( x, z, settings._seed ^ VoxelTerrainInternal::kTreeSalt );
                    const int32  trunkHeight = 4 + static_cast<int32>( ( treeHash >> 16 ) % 3u );
                    VoxelTerrainInternal::placeTree( chunk, x, computeSurfaceHeight( x, z, settings ), z, trunkHeight, blocks );
                    if ( chunk.contains( x, 0, z ) )
                        ++report._treeCount;
                }
            }
        }
        VoxelTerrainInternal::decorate( chunk, *pCatalog, settings, blocks );
        return report;
    }

    VoxelChunkGenerator VoxelTerrainSettings::makeChunkGenerator( const VoxelTerrainSettings& settings )
    {
        return VoxelChunkGenerator( [settings]( const VoxelWorld& world, int32 chunkX, int32 chunkZ, vector<VoxelBlockIndex>& outBlocks )
        { (void)fillChunk( world, settings, chunkX, chunkZ, outBlocks ); } );
    }
} // namespace sw
