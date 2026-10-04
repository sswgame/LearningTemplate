/**
 * @file TerrainMeshBuilder.h
 * @brief 지형 청크 메시(지오 밉맵 LOD) — 청크마다 LOD 를 고르고, 이웃이 더 거친 변은 정점을 접어 틈 없이 잇습니다.
 * @details LOD l 은 샘플을 2^l 칸마다 씁니다. 이웃 청크가 더 거친 LOD L 이면 맞닿은 변의 정점 가운데 L 격자에 없는 것을
 *          아래쪽 L 격자 점으로 **접습니다**(정점 위치를 그 점으로 옮기고, 넓이가 0 이 된 삼각형은 버립니다). 그러면 그 변에 쓰인 정점
 *          집합이 이웃 쪽 변과 똑같아 T 접합도 틈도 없습니다(스커트 · 인덱스 변형 표가 필요 없습니다). 거친 쪽은 손대지 않습니다.
 *          정점은 청크 중심 기준 로컬 좌표이고, 월드 위치 = 로컬 + 청크 이동(`computeChunkTranslation`)입니다. 칸 크기와 원점이 이진 소수로
 *          정확히 나타나면(예: 1 m · 0.5 m 칸, 정수 원점) 이웃 청크의 같은 정점이 비트까지 같습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

namespace sw
{
    struct RHIVertex;

    class TerrainHeightfield;

    /** @brief 청크의 네 변입니다. 이웃 LOD 배열의 순서입니다. */
    enum class TerrainChunkSide : uint8
    {
        West = 0, ///< x−
        East,     ///< x+
        South,    ///< z−
        North,    ///< z+
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 높이장을 청크로 나눈 모양입니다. */
    struct TerrainChunkLayout
    {
        uint32 _chunkCells{ 0 }; ///< 청크 한 변의 칸 수(2 의 거듭제곱)
        uint32 _chunkCountX{ 0 };
        uint32 _chunkCountZ{ 0 };
        uint32 _maxLod{ 0 }; ///< log2( _chunkCells ) — 그 LOD 에서 청크가 칸 하나다

        uint32 getChunkCount() const { return _chunkCountX * _chunkCountZ; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TerrainMeshBuilder
     * @brief 청크 배치 · 바운드 · LOD 선택 · 정점 만들기입니다. 씬 없이 돕니다(시험이 틈 없음을 정점으로 확인합니다).
     */
    struct SW_API TerrainMeshBuilder
    {
        /**
         * @brief 해상도 @p resolution 의 높이장을 한 변 @p chunkCells 칸의 청크로 나눕니다.
         * @return @p chunkCells 가 2 의 거듭제곱이 아니거나 (해상도 − 1) 이 그것으로 나눠떨어지지 않으면 false 입니다.
         */
        [[nodiscard]] static bool makeLayout( uint32 resolution, uint32 chunkCells, TerrainChunkLayout& outLayout );

        /** @brief 청크의 이동(월드)입니다 — x · z 는 청크 중심, y 는 지형 원점의 높이입니다. 메시 정점은 이 점 기준입니다. */
        static float3 computeChunkTranslation( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ );
        /** @brief 청크의 정점 전부를 담는 구의 반지름입니다(중심은 `computeChunkTranslation`). */
        static float32 computeChunkBoundsRadius( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ );

        /**
         * @brief 거리로 LOD 를 고릅니다. 거리 < @p lodDistance 이면 0, 그 두 배까지 1, 네 배까지 2 … 이고 @p maxLod 에서 멈춥니다.
         */
        static uint32 selectLod( float32 distance, float32 lodDistance, uint32 maxLod );

        /**
         * @brief 청크 (x, z) 를 LOD @p lod 로 만듭니다. @p arrNeighborLod 는 `TerrainChunkSide` 순서의 이웃 LOD 이고, 지형 가장자리 변은
         *        자기 LOD 를 넣습니다. 구멍 칸을 덮는 사각형은 만들지 않습니다.
         * @details 정점: 위치(청크 이동 기준) · 노멀(샘플 중앙 차분) · UV(지형 정규 좌표 u = 샘플 x / (N−1)) · 색(흰색). 삼각형 목록(인덱스 없음)입니다.
         */
        static void buildChunkVertices( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ, uint32 lod,
                                        const uint32 ( &arrNeighborLod )[static_cast<uint32>( TerrainChunkSide::Count )], vector<RHIVertex>& outListVertex );
    };
} // namespace sw
