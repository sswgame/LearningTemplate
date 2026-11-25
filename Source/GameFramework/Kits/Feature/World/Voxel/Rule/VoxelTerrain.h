/**
 * @file VoxelTerrain.h
 * @brief 시드로 결정되는 지형을 생성합니다. 값 노이즈(`Core/Math/ValueNoise.h`)로 높이를 정하고, 흙, 돌, 모래 해변, 물, 나무를 배치합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"

namespace sw
{
    struct VoxelBlockDef;
    struct VoxelTerrainReport;

    /** @brief 돌 속 광석 한 층입니다. 층들의 확률은 앞에서부터 쌓여 한 칸에 광석 하나만 고른다. */
    REFLECT()
    struct SW_KIT_API VoxelOreLayer
    {
        REFLECT_BODY();
        PROPERTY( Required )
        CatalogKey<VoxelBlockDef> _block{};
        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _chance{ 0.0f }; ///< 돌 한 칸이 이 광석이 될 확률
        PROPERTY( Required, Min = 0 )
        int32 _maxHeight{ 0 }; ///< 이 높이(y) 아래에만 놓는다
    };

    /** @brief 지형 생성 설정입니다. 값은 데이터(디렉터의 `_terrain`)에 있고 코드에는 기본값이 없다 — 디렉터가 그대로 넘긴다. 블록은 id 로 고르며, 카탈로그에 없으면 그 층을 건너뜁니다(디렉터는 시작할 때 모두 있는지 본다). */
    REFLECT()
    struct SW_KIT_API VoxelTerrainSettings
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Same seed, same island" )
        uint32 _seed{ 0 };
        PROPERTY( Required )
        int32 _baseHeight{ 0 }; ///< 노이즈가 0일 때의 높이
        PROPERTY( Required )
        int32 _heightAmplitude{ 0 }; ///< 노이즈가 1일 때의 높이에서 기본 높이를 뺀 값
        PROPERTY( Required )
        int32 _waterLevel{ 0 }; ///< 이 높이 아래의 빈 블록은 물로 채운다. 0 이하이면 물이 없다
        PROPERTY( Required, Min = 0 )
        int32 _dirtDepth{ 0 }; ///< 맨 위 블록 아래의 흙 두께
        PROPERTY( Required, Min = 0.0 )
        float32 _noiseScale{ 0.0f };
        PROPERTY( Required, Min = 1 )
        int32 _octaveCount{ 0 };
        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _treeChance{ 0.0f }; ///< 풀 블록마다 나무가 설 확률
        PROPERTY( Required )
        CatalogKey<VoxelBlockDef> _grassBlock{};
        PROPERTY( Required )
        CatalogKey<VoxelBlockDef> _dirtBlock{};
        PROPERTY( Required )
        CatalogKey<VoxelBlockDef> _stoneBlock{};
        PROPERTY( Required )
        CatalogKey<VoxelBlockDef> _sandBlock{};
        PROPERTY( Required )
        CatalogKey<VoxelBlockDef> _waterBlock{};
        PROPERTY( Required )
        CatalogKey<VoxelBlockDef> _bedrockBlock{};
        PROPERTY( Required )
        CatalogKey<VoxelBlockDef> _logBlock{};
        PROPERTY( Required )
        CatalogKey<VoxelBlockDef> _leavesBlock{};
        /// 꾸밈(눈 · 광석)도 지형처럼 `_seed` 를 따른다 — 게임이 씨앗 없는 해시로 따로 꾸미면 씨앗을 바꿔도 광석 자리가 그대로다(감사 K-8).
        /// 마인크래프트의 지형 생성 단계(feature placement)가 같은 월드 씨앗을 쓰는 것과 같은 자리.
        PROPERTY( Required )
        CatalogKey<VoxelBlockDef> _snowBlock{}; ///< 높은 풀 꼭대기를 덮는 블록
        PROPERTY( Required, Min = 0 )
        int32 _snowHeight{ 0 }; ///< 풀 꼭대기가 이 높이(y) 이상이면 눈으로 덮는다
        PROPERTY( Required, Tooltip = "Ore layers placed underground, the first one wins" )
        vector<VoxelOreLayer> _listOreLayer{}; ///< 돌 속 광석. 지면 3 블록 아래부터 y = 1 까지

        /** @brief 설정이 부르는 블록 id 마다 @p onBlock 을 부릅니다(디렉터가 시작할 때 카탈로그에 있는지 본다). */
        template <typename TOnBlock>
        void forEachBlock( TOnBlock onBlock ) const
        {
            for ( const CatalogKey<VoxelBlockDef>* pBlock : { &_grassBlock, &_dirtBlock, &_stoneBlock, &_sandBlock, &_waterBlock, &_bedrockBlock, &_logBlock, &_leavesBlock, &_snowBlock } )
            {
                onBlock( *pBlock );
            }
            for ( const VoxelOreLayer& layer : _listOreLayer )
            {
                onBlock( layer._block );
            }
        }

        // 청크 하나를 지형으로 채웁니다(생성 훅 — `VoxelWorld::setChunkGenerator`). 기둥마다 아래에서 위로 기반암, 돌, 흙을 채우고, 맨 위는 물가 2블록 안이면 모래,
        // 아니면 풀로 덮습니다. 물 높이까지는 물로 채웁니다. 이웃 청크를 읽지 않고 같은 씨앗이면 같은 블록이 나오므로, 내린 청크를 다시 올려도 같다.
        // 나무는 풀 위에 시드 해시로 서고(줄기 4~6블록과 잎 덩어리), 3블록 안의 다른 나무 자리보다 해시가 클 때만 선다 — 이웃 청크에 선 나무의 잎도 순서 없이 같은 자리에 온다.
        // 나무는 월드 가장자리 2블록 안에는 서지 않는다. 끝으로 높은 풀 꼭대기를 눈으로 덮고 돌 속에 광석을 시드 해시로 놓는다.
        /** @brief 기둥의 지면 높이(맨 위 블록의 y)입니다. 월드를 쓰지 않으므로 미리보기와 스폰 위치에도 씁니다. */
        static int32 computeSurfaceHeight( int32 x, int32 z, const VoxelTerrainSettings& settings );
        /** @brief 청크 (@p chunkX, @p chunkZ) 의 블록을 @p outBlocks(`kVoxelChunkVolume` 칸, 공기로 받는다)에 채웁니다. 월드는 크기와 카탈로그만 읽습니다. */
        static VoxelTerrainReport fillChunk( const VoxelWorld& world, const VoxelTerrainSettings& settings, int32 chunkX, int32 chunkZ, vector<VoxelBlockIndex>& outBlocks );
        /** @brief @p settings 사본으로 `fillChunk` 를 부르는 생성 훅입니다. */
        static VoxelChunkGenerator makeChunkGenerator( const VoxelTerrainSettings& settings );
    };
} // namespace sw

namespace sw
{
    /** @brief 청크 하나의 생성 결과입니다. 테스트와 로그에 씁니다. */
    struct VoxelTerrainReport
    {
        int32  _minHeight{ 0 };
        int32  _maxHeight{ 0 };
        uint32 _treeCount{ 0 }; ///< 줄기가 이 청크에 선 나무 수
    };
} // namespace sw
