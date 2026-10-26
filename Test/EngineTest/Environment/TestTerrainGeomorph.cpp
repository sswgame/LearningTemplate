#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Environment/Terrain/TerrainMeshBuilder.h"
#include "Engine/Graphics/RHI/RHITypes.h"

#include "TestFramework/TestFramework.h"

// 지형 지오모프(EnvironmentRemainders E4) — 정점에 구워 둔 섞기 값이 거친 LOD 메시의 높이와 같고, 섞는 구간이 LOD 교체 임계값에서 끝난다. 디바이스 없음(nogpu).
//
// GPU 쪽(terrain.hlsl 의 computeWorldPositionOffset)은 여기서 돌지 않는다. 그 식이 읽는 정점 값과 "거친 높이로 옮긴 정점이 거친 메시 위에 놓인다"는 성질을 CPU 에서 본다.

using namespace sw;

namespace
{
    struct TerrainGeomorphTestUtil
    {
        /** @brief 파동 높이장입니다(셀 1 m, 한 변 @p resolution 샘플). */
        static TerrainHeightfield makeField( uint32 resolution )
        {
            HeightfieldData data;
            data._resolution = resolution;
            data._listHeight.resize( static_cast<size_t>( resolution ) * resolution );
            for ( uint32 sampleZ = 0; sampleZ < resolution; ++sampleZ )
            {
                for ( uint32 sampleX = 0; sampleX < resolution; ++sampleX )
                {
                    const float32 height                                                    = 0.5f + 0.4f * MathUtil::sin( static_cast<float32>( sampleX ) * 0.55f ) * MathUtil::cos( static_cast<float32>( sampleZ ) * 0.45f );
                    data._listHeight[static_cast<size_t>( sampleZ ) * resolution + sampleX] = static_cast<uint16>( height * 65535.0f );
                }
            }
            TerrainHeightfield field;
            const float32      extent = static_cast<float32>( resolution - 1 );
            (void)field.initialize( data, float3{}, float2{ extent, extent }, 0.0f, 10.0f ); // 테스트 준비 — 실패하면 뒤 단언이 드러낸다
            return field;
        }

        /** @brief 삼각형 목록(청크 로컬 xz)에서 점 (x, z) 의 높이를 구합니다. 어느 삼각형에도 없으면 false 입니다. */
        static bool sampleMeshHeight( const vector<RHIVertex>& listVertex, float32 x, float32 z, float32& outHeight )
        {
            for ( size_t index = 0; index + 2 < listVertex.size(); index += 3 )
            {
                const RHIVertex& a           = listVertex[index];
                const RHIVertex& b           = listVertex[index + 1];
                const RHIVertex& c           = listVertex[index + 2];
                const float32    determinant = ( b._arrPosition[2] - c._arrPosition[2] ) * ( a._arrPosition[0] - c._arrPosition[0] ) +
                                            ( c._arrPosition[0] - b._arrPosition[0] ) * ( a._arrPosition[2] - c._arrPosition[2] );
                if ( MathUtil::abs( determinant ) < 1.0e-9f )
                    continue;
                const float32     weightA  = ( ( b._arrPosition[2] - c._arrPosition[2] ) * ( x - c._arrPosition[0] ) + ( c._arrPosition[0] - b._arrPosition[0] ) * ( z - c._arrPosition[2] ) ) / determinant;
                const float32     weightB  = ( ( c._arrPosition[2] - a._arrPosition[2] ) * ( x - c._arrPosition[0] ) + ( a._arrPosition[0] - c._arrPosition[0] ) * ( z - c._arrPosition[2] ) ) / determinant;
                const float32     weightC  = 1.0f - weightA - weightB;
                constexpr float32 kSlack   = -1.0e-4f;
                const bool        bOutside = weightA < kSlack || weightB < kSlack || weightC < kSlack;
                if ( bOutside )
                    continue;
                outHeight = weightA * a._arrPosition[1] + weightB * b._arrPosition[1] + weightC * c._arrPosition[1];
                return true;
            }
            return false;
        }

        /** @brief 정점의 UV 가 돌려주는 샘플 좌표입니다. */
        static void findSample( const RHIVertex& vertex, float32 last, uint32& outSampleX, uint32& outSampleZ )
        {
            outSampleX = static_cast<uint32>( MathUtil::round( vertex._arrUv[0] * last ) );
            outSampleZ = static_cast<uint32>( MathUtil::round( vertex._arrUv[1] * last ) );
        }
    };
} // namespace

/**
 * @brief [TerrainGeomorphTest] 정점에 거친 LOD 높이와의 차이 · 섞기 구간 · 청크 상자를 굽는다 - 거친 격자 위의 정점은 0 이고 사이의 정점은 거친 메시의 높이로 가는 차이다
 * @details 변이: 대각선을 (x+1, z)-(x, z+1) 로 바꿔 평균하면 두 축 사이 정점의 차이가 어긋나 거친 메시 높이 비교가 진다.
 */
SW_TEST_CASE( TerrainGeomorphTest, BakedDeltaMovesVerticesOntoTheCoarserMesh )
{
    const TerrainHeightfield field = TerrainGeomorphTestUtil::makeField( 33 );
    TerrainChunkLayout       layout;
    SW_ASSERT_TRUE( TerrainMeshBuilder::makeLayout( 33, 16, layout ) );
    const uint32      arrNeighbor[4]       = { 0, 0, 0, 0 };
    const uint32      arrCoarseNeighbor[4] = { 1, 1, 1, 1 };
    vector<RHIVertex> listFine;
    vector<RHIVertex> listCoarse;
    TerrainMeshBuilder::makeChunkVertices( field, layout, 0, 0, 0, arrNeighbor, listFine );
    TerrainMeshBuilder::makeChunkVertices( field, layout, 0, 0, 1, arrCoarseNeighbor, listCoarse );
    SW_ASSERT_TRUE( listFine.empty() == false );
    TerrainMeshBuilder::applyGeomorph( field, layout, 0, 0, 0, 40.0f, listFine );

    constexpr float32 kLODDistance = 40.0f;
    const float32     last         = 32.0f;
    uint32            morphedCount = 0;
    for ( const RHIVertex& vertex : listFine )
    {
        uint32 sampleX{ 0 };
        uint32 sampleZ{ 0 };
        TerrainGeomorphTestUtil::findSample( vertex, last, sampleX, sampleZ );
        const bool bOnCoarseGrid = sampleX % 2 == 0 && sampleZ % 2 == 0;
        if ( bOnCoarseGrid )
        {
            SW_EXPECT_NEAR_EQUAL( 0.0f, vertex._arrColor[0], 0.0f );
            continue;
        }
        ++morphedCount;
        // 거친 메시 위의 같은 (x, z) 높이(청크 로컬)와 같은 차이여야 한다 - 메시 둘의 정점은 같은 이동 기준이다.
        float32 coarseHeight{ 0.0f };
        SW_ASSERT_TRUE( TerrainGeomorphTestUtil::sampleMeshHeight( listCoarse, vertex._arrPosition[0], vertex._arrPosition[2], coarseHeight ) );
        SW_EXPECT_NEAR_EQUAL( coarseHeight, vertex._arrPosition[1] + vertex._arrColor[0], 1.0e-4f );
    }
    SW_EXPECT_TRUE( morphedCount > 0u );

    // 섞기 구간과 청크 상자(월드) - 청크 (0, 0) 은 0..16 m, 중앙 샘플 (8, 8) 의 높이가 지면이다.
    const RHIVertex& first = listFine[0];
    SW_EXPECT_NEAR_EQUAL( kLODDistance * TerrainMeshBuilder::kMorphStartRatio, first._arrColor[1], 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( kLODDistance * TerrainMeshBuilder::kMorphEndRatio, first._arrColor[2], 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, first._arrColor[3], 1.0e-4f );    // 최소 x
    SW_EXPECT_NEAR_EQUAL( 0.0f, first._arrTangent[0], 1.0e-4f );  // 최소 z
    SW_EXPECT_NEAR_EQUAL( 16.0f, first._arrTangent[1], 1.0e-4f ); // 최대 x
    SW_EXPECT_NEAR_EQUAL( 16.0f, first._arrTangent[2], 1.0e-4f ); // 최대 z
    SW_EXPECT_NEAR_EQUAL( field.getSampleHeight( 8, 8 ), first._arrTangent[3], 1.0e-4f );

    // 섞기 끝은 거친 LOD 로 바뀌는 가장 가까운 거리(임계값 × (1 - 히스테리시스))와 같다 - 바뀌는 순간 이미 거친 모양이다.
    SW_EXPECT_NEAR_EQUAL( 1.0f - TerrainMeshBuilder::kLODHysteresis, TerrainMeshBuilder::kMorphEndRatio, 1.0e-6f );
}

/**
 * @brief [TerrainGeomorphTest] 섞기 구간은 LOD 마다 임계값(LOD 거리 × 2^LOD)을 따르고, 더 거친 LOD 가 없는 청크는 섞지 않는다
 */
SW_TEST_CASE( TerrainGeomorphTest, MorphRangeFollowsTheLODAndTheCoarsestLODNeverMorphs )
{
    const TerrainHeightfield field = TerrainGeomorphTestUtil::makeField( 33 );
    TerrainChunkLayout       layout;
    SW_ASSERT_TRUE( TerrainMeshBuilder::makeLayout( 33, 16, layout ) );
    const uint32 arrNeighbor[4] = { 0, 0, 0, 0 };

    vector<RHIVertex> listLOD2;
    TerrainMeshBuilder::makeChunkVertices( field, layout, 1, 1, 2, arrNeighbor, listLOD2 );
    SW_ASSERT_TRUE( listLOD2.empty() == false );
    TerrainMeshBuilder::applyGeomorph( field, layout, 1, 1, 2, 40.0f, listLOD2 );
    SW_EXPECT_NEAR_EQUAL( 40.0f * 4.0f * TerrainMeshBuilder::kMorphStartRatio, listLOD2[0]._arrColor[1], 1.0e-3f ); // 임계값 160 m
    SW_EXPECT_NEAR_EQUAL( 40.0f * 4.0f * TerrainMeshBuilder::kMorphEndRatio, listLOD2[0]._arrColor[2], 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 16.0f, listLOD2[0]._arrColor[3], 1.0e-4f );   // 청크 (1, 1) 의 최소 x
    SW_EXPECT_NEAR_EQUAL( 16.0f, listLOD2[0]._arrTangent[0], 1.0e-4f ); // 최소 z

    vector<RHIVertex> listCoarsest;
    TerrainMeshBuilder::makeChunkVertices( field, layout, 0, 0, layout._maxLOD, arrNeighbor, listCoarsest );
    SW_ASSERT_TRUE( listCoarsest.empty() == false );
    TerrainMeshBuilder::applyGeomorph( field, layout, 0, 0, layout._maxLOD, 40.0f, listCoarsest );
    for ( const RHIVertex& vertex : listCoarsest )
    {
        SW_EXPECT_NEAR_EQUAL( 0.0f, vertex._arrColor[0], 0.0f );
        SW_EXPECT_TRUE( vertex._arrColor[1] >= 1.0e29f ); // 어떤 거리도 닿지 않는다
    }
}
