/**
 * @file TerrainMeshBuilder.h
 * @brief 지형 청크 메시(지오 밉맵 LOD)입니다. 청크마다 LOD 를 고르고, 이웃이 더 거친 변은 정점을 접어 틈 없이 잇습니다.
 * @details LOD l 은 샘플을 2^l 셀마다 쓴다. 이웃 청크가 더 거친 LOD L 이면 맞닿은 변의 정점 가운데 L 격자에 없는 것을
 *          아래쪽 L 격자 점으로 **접는다**(정점 위치를 그 점으로 옮기고, 넓이가 0 이 된 삼각형은 버린다). 그러면 그 변에 쓰인 정점
 *          집합이 이웃 쪽 변과 똑같아 T 접합도 틈도 없다(스커트나 인덱스 변형 테이블이 필요 없다). 거친 쪽은 손대지 않는다.
 *          정점은 청크 중심 기준 로컬 좌표이고, 월드 위치 = 로컬 + 청크 이동(`computeChunkTranslation`)이다. 셀 크기와 원점이 이진 소수로
 *          정확히 나타나면(예: 1 m 와 0.5 m 셀, 정수 원점) 이웃 청크의 같은 정점이 비트까지 같다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
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
    /** @brief 높이장을 청크로 나눈 형태입니다. */
    struct TerrainChunkLayout
    {
        uint32 _chunkCells{ 0 }; ///< 청크 한 변의 셀 수다(2 의 거듭제곱이다).
        uint32 _chunkCountX{ 0 };
        uint32 _chunkCountZ{ 0 };
        uint32 _maxLOD{ 0 }; ///< log2( _chunkCells ) 이며, 그 LOD 에서 청크가 셀 하나가 된다.

        uint32 getChunkCount() const { return _chunkCountX * _chunkCountZ; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TerrainMeshBuilder
     * @brief 청크 배치, 바운드, LOD 선택, 정점 생성을 담당합니다. 씬 없이 돌며, 테스트가 틈이 없음을 정점으로 확인합니다.
     */
    struct SW_API TerrainMeshBuilder
    {
        /**
         * @brief 해상도 @p resolution 의 높이장을 한 변 @p chunkCells 셀의 청크로 나눕니다.
         * @return @p chunkCells 가 2 의 거듭제곱이 아니거나 (해상도 - 1) 이 그것으로 나눠떨어지지 않으면 false 를 반환합니다.
         */
        [[nodiscard]] static bool makeLayout( uint32 resolution, uint32 chunkCells, TerrainChunkLayout& outLayout );

        /** @brief 청크의 이동(월드)을 반환합니다. x 와 z 는 청크 중심이고, y 는 지형 원점의 높이입니다. 메시 정점은 이 점 기준입니다. */
        static float3 computeChunkTranslation( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ );
        /** @brief 청크의 정점 전부를 담는 구의 반지름입니다(중심은 `computeChunkTranslation`). */
        static float32 computeChunkBoundsRadius( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ );

        /**
         * @brief 거리로 LOD 를 고릅니다. 거리 < @p lodDistance 이면 0, 그 두 배까지 1, 네 배까지 2 … 이고 @p maxLOD 에서 멈춥니다.
         */
        static uint32 selectLOD( float32 distance, float32 lodDistance, uint32 maxLOD );

        /**
         * @brief LOD 를 바꾸기 전에 거리가 임계값을 넘어야 하는 비율입니다. 임계값에 선 카메라가 매 프레임 메시를 다시 만들지 않게 합니다(컴포넌트 틱이 쓴다).
         * @details 지오모프의 섞기 끝(`kMorphEndRatio`)이 이 값에서 나온다 - 청크가 거친 LOD 로 바뀌는 순간 섞기가 이미 끝나 있어야 한다.
         */
        static constexpr float32 kLODHysteresis = 0.1f;
        /// @brief 지오모프가 섞기 시작하는 거리 / LOD 교체 임계값입니다. 이보다 가까우면 자기 LOD 의 정점 그대로다.
        static constexpr float32 kMorphStartRatio = 0.6f;
        /// @brief 지오모프가 섞기를 끝내는 거리 / LOD 교체 임계값입니다. 거친 LOD 로 바뀌는 가장 가까운 거리(임계값 × (1 - 히스테리시스))에서 끝난다.
        static constexpr float32 kMorphEndRatio = 1.0f - kLODHysteresis;
        static_assert( kMorphStartRatio < kMorphEndRatio, "섞기 구간이 비면 안 된다" );

        /**
         * @brief 청크 (x, z) 를 LOD @p lod 로 만듭니다. @p arrNeighborLOD 는 `TerrainChunkSide` 순서의 이웃 LOD 이고, 지형 가장자리 변은
         *        자기 LOD 를 넣습니다. 구멍 셀을 덮는 사각형은 만들지 않습니다.
         * @details 정점은 위치(청크 이동 기준), 노멀(샘플 중앙 차분), UV(지형 정규 좌표 u = 샘플 x / (N-1)), 색(흰색)을 가진다. 삼각형 목록(인덱스 없음)이다.
         */
        static void makeChunkVertices( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ, uint32 lod,
                                       const uint32 ( &arrNeighborLOD )[static_cast<uint32>( TerrainChunkSide::Count )], vector<RHIVertex>& outListVertex );

        /**
         * @brief 지오모프 값을 정점의 색 · 탄젠트 칸에 씁니다(E4). `makeChunkVertices` 로 만든 정점 배열 @p inoutListVertex 를 그대로 받는다 - 정점의 UV 가 샘플 좌표를 되돌려 준다.
         * @details 정점 셰이더(`terrain.hlsl` 의 `computeWorldPositionOffset`)가 카메라와 청크 사이 거리로 섞는 비율을 구해 높이를 한 걸음 거친 LOD 의 높이로 옮긴다.
         *          청크가 거친 LOD 로 바뀌는 순간 정점이 이미 그 모양이라 튐이 없다. 거리는 컴포넌트가 LOD 를 고르는 거리(`computeChunkDistance` - 청크 상자까지의 평면 거리와 중앙 샘플 높이 차)와 같은 식이다.
         *          칸 배치(월드 값): 색 = (거친 LOD 높이 - 지금 높이, 섞기 시작 거리, 섞기 끝 거리, 청크 최소 x), 탄젠트 = (청크 최소 z, 청크 최대 x, 청크 최대 z, 중앙 샘플 높이).
         *          섞기 구간은 이 LOD 의 교체 임계값(@p lodDistance × 2^@p lod)의 `kMorphStartRatio` ~ `kMorphEndRatio` 배다. 더 거친 LOD 가 없는 청크(`_maxLOD`)는 섞이지 않는다.
         *          거친 LOD 가 삼각형을 나누는 대각선((x, z)-(x+1, z+1))이 같아서 거친 메시의 높이와 정확히 같다. 이 칸들은 지오모프를 쓰는 머티리얼만 읽는다 -
         *          메시는 `Mesh::setVerticesWithTangents` 로 넣어 탄젠트를 다시 구하지 않게 한다.
         * @param lodDistance 컴포넌트의 LOD 거리(`selectLOD` 의 첫 임계값)
         */
        static void applyGeomorph( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ, uint32 lod, float32 lodDistance,
                                   vector<RHIVertex>& inoutListVertex );
    };
} // namespace sw
