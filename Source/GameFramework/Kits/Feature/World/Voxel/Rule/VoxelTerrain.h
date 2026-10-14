/**
 * @file VoxelTerrain.h
 * @brief 시드로 결정되는 지형을 생성합니다. 값 노이즈(`Utility/ValueNoise.h`)로 높이를 정하고, 흙, 돌, 모래 해변, 물, 나무를 배치합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class VoxelWorld;

    /** @brief 지형 생성 설정입니다. 블록은 id 로 고르며, 카탈로그에 없으면 그 층을 건너뜁니다. */
    struct VoxelTerrainSettings
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
    };
} // namespace sw

namespace sw
{
    /** @brief 생성 결과입니다. 테스트와 로그에 씁니다. */
    struct VoxelTerrainReport
    {
        int32  _minHeight{ 0 };
        int32  _maxHeight{ 0 };
        uint32 _treeCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct VoxelTerrainGenerator
     * @brief 월드를 지형으로 채웁니다. 기둥마다 아래에서 위로 기반암, 돌, 흙을 채우고, 맨 위는 물가 2블록 안이면 모래, 아니면 풀로 덮습니다. 물 높이까지는 물로 채웁니다.
     * @details 풀 위에는 시드 해시로 나무(줄기 4~6블록과 잎 덩어리)를 세운다. 나무는 청크 경계를 넘어도 되지만 월드 밖으로는 잘라 낸다.
     */
    struct SW_KIT_API VoxelTerrainGenerator
    {
        /** @brief 기둥의 지면 높이(맨 위 블록의 y)입니다. 월드를 쓰지 않으므로 미리보기와 스폰 위치에도 씁니다. */
        static int32              computeSurfaceHeight( int32 x, int32 z, const VoxelTerrainSettings& settings );
        static VoxelTerrainReport fillWorld( VoxelWorld& world, const VoxelTerrainSettings& settings );
    };
} // namespace sw
