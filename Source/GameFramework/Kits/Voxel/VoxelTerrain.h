/**
 * @file VoxelTerrain.h
 * @brief 씨앗으로 고정된 지형 생성 — 값 노이즈 높이 · 흙 · 돌 · 모래 해변 · 물 · 나무입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class VoxelWorld;

    /**
     * @struct VoxelNoise
     * @brief 씨앗 고정 2D 값 노이즈입니다. 격자 꼭짓점에 정수 해시로 값을 두고 smoothstep 으로 잇습니다. 같은 씨앗이면 어느 기계에서나 같은 값입니다.
     */
    struct SW_GF_API VoxelNoise
    {
        /** @brief 정수 격자점의 값(0..1)입니다. */
        static float32 hashLattice( int32 x, int32 z, uint32 seed );
        /** @brief 한 옥타브(0..1)입니다. */
        static float32 sampleValue( float32 x, float32 z, uint32 seed );
        /** @brief 옥타브를 겹친 값(0..1)입니다. 옥타브마다 주파수 ×2 · 진폭 ×0.5. */
        static float32 sampleFractal( float32 x, float32 z, uint32 seed, int32 octaveCount );
    };

    /** @brief 지형 생성 설정입니다. 블록은 id 로 고르고 카탈로그에 없으면 그 층을 건너뜁니다. */
    struct VoxelTerrainSettings
    {
        uint32        _seed{ 1337u };
        int32         _baseHeight{ 18 };      ///< 노이즈 0 의 높이
        int32         _heightAmplitude{ 18 }; ///< 노이즈 1 의 높이 - 기본 높이
        int32         _waterLevel{ 20 };      ///< 이 높이 아래 빈 칸은 물(0 이하이면 물 없음)
        int32         _dirtDepth{ 3 };        ///< 맨 위 아래 흙 두께
        float32       _noiseScale{ 1.0f / 48.0f };
        int32         _octaveCount{ 4 };
        float32       _treeChance{ 0.012f }; ///< 풀 칸마다 나무가 설 확률
        hashed_string _grassBlock{ "grass" };
        hashed_string _dirtBlock{ "dirt" };
        hashed_string _stoneBlock{ "stone" };
        hashed_string _sandBlock{ "sand" };
        hashed_string _waterBlock{ "water" };
        hashed_string _bedrockBlock{ "bedrock" };
        hashed_string _logBlock{ "log" };
        hashed_string _leavesBlock{ "leaves" };
    };

    /** @brief 생성 결과(시험 · 로그)입니다. */
    struct VoxelTerrainReport
    {
        int32  _minHeight{ 0 };
        int32  _maxHeight{ 0 };
        uint32 _treeCount{ 0 };
    };

    /**
     * @struct VoxelTerrainGenerator
     * @brief 월드를 지형으로 채웁니다. 기둥마다: 바닥 기반암 → 돌 → 흙 → 맨 위(물가 2 칸 안이면 모래, 아니면 풀) → 물 높이까지 물.
     *        풀 위에는 씨앗 해시로 나무(줄기 4–6 · 잎 덩어리)를 세웁니다. 나무는 청크 경계를 넘어도 되지만 월드 밖으로는 자릅니다.
     */
    struct SW_GF_API VoxelTerrainGenerator
    {
        /** @brief 기둥의 지면 높이(맨 위 블록의 y)입니다. 월드를 쓰지 않으므로 미리보기 · 스폰 위치에도 씁니다. */
        static int32              computeSurfaceHeight( int32 x, int32 z, const VoxelTerrainSettings& settings );
        static VoxelTerrainReport generate( VoxelWorld& world, const VoxelTerrainSettings& settings );
    };
} // namespace sw
