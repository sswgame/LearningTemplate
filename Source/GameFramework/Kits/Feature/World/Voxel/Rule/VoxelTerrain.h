/**
 * @file VoxelTerrain.h
 * @brief 시드로 결정되는 지형을 생성합니다. 값 노이즈(`Core/Math/ValueNoise.h`)로 높이를 정하고, 흙, 돌, 모래 해변, 물, 나무를 배치합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"

namespace sw
{
    struct VoxelTerrainReport;

    /** @brief 돌 속 광석 한 층입니다. 층들의 확률은 앞에서부터 쌓여 한 칸에 광석 하나만 고른다. */
    struct VoxelOreLayer
    {
        hashed_string _block{};
        float32       _chance{ 0.0f }; ///< 돌 한 칸이 이 광석이 될 확률
        int32         _maxHeight{ 0 }; ///< 이 높이(y) 아래에만 놓는다
    };

    /** @brief 지형 생성 설정입니다. 블록은 id 로 고르며, 카탈로그에 없으면 그 층을 건너뜁니다. */
    struct SW_KIT_API VoxelTerrainSettings
    {
        uint32        _seed{ 1337u };
        int32         _baseHeight{ 18 };      ///< 노이즈가 0일 때의 높이
        int32         _heightAmplitude{ 18 }; ///< 노이즈가 1일 때의 높이에서 기본 높이를 뺀 값
        int32         _waterLevel{ 20 };      ///< 이 높이 아래의 빈 블록은 물로 채운다. 0 이하이면 물이 없다
        int32         _dirtDepth{ 3 };        ///< 맨 위 블록 아래의 흙 두께
        float32       _noiseScale{ 1.0f / 48.0f };
        int32         _octaveCount{ 4 };
        float32       _treeChance{ 0.012f }; ///< 풀 블록마다 나무가 설 확률
        hashed_string _grassBlock{ "grass" };
        hashed_string _dirtBlock{ "dirt" };
        hashed_string _stoneBlock{ "stone" };
        hashed_string _sandBlock{ "sand" };
        hashed_string _waterBlock{ "water" };
        hashed_string _bedrockBlock{ "bedrock" };
        hashed_string _logBlock{ "log" };
        hashed_string _leavesBlock{ "leaves" };
        /// 꾸밈(눈 · 광석)도 지형처럼 `_seed` 를 따른다 — 게임이 씨앗 없는 해시로 따로 꾸미면 씨앗을 바꿔도 광석 자리가 그대로다(감사 K-8).
        /// 마인크래프트의 지형 생성 단계(feature placement)가 같은 월드 씨앗을 쓰는 것과 같은 자리.
        hashed_string         _snowBlock{};     ///< 높은 풀 꼭대기를 덮는 블록. 비면 눈이 없다
        int32                 _snowHeight{ 0 }; ///< 풀 꼭대기가 이 높이(y) 이상이면 눈으로 덮는다
        vector<VoxelOreLayer> _listOreLayer{};  ///< 돌 속 광석. 지면 3 블록 아래부터 y = 1 까지

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
