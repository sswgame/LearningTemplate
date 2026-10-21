#include "pch.h"

#include "Editor/Viewport/TerrainBrush.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    TerrainBrushGrid makeGrid( uint32 resolution )
    {
        TerrainBrushGrid grid;
        grid._origin     = float2{ 0.0f, 0.0f };
        grid._cellSize   = float2{ 1.0f, 1.0f };
        grid._resolution = resolution;
        return grid;
    }
} // namespace

/**
 * @brief [TerrainBrushTest] 감쇠: 안쪽 반경 안은 1, 반경 밖은 0, 그 사이는 단조롭게 준다
 */
SW_TEST_CASE( TerrainBrushTest, FalloffIsFullInsideAndZeroOutside )
{
    TerrainBrushSettings settings;
    settings._radius  = 4.0f;
    settings._falloff = 0.5f;
    SW_EXPECT_NEAR_EQUAL( 1.0f, TerrainBrush::computeFalloffWeight( 0.0f, settings ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, TerrainBrush::computeFalloffWeight( 2.0f, settings ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, TerrainBrush::computeFalloffWeight( 4.0f, settings ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, TerrainBrush::computeFalloffWeight( 3.0f, settings ), 1e-6f );
    SW_EXPECT_TRUE( TerrainBrush::computeFalloffWeight( 2.5f, settings ) > TerrainBrush::computeFalloffWeight( 3.5f, settings ) );

    settings._falloff = 0.0f;
    SW_EXPECT_NEAR_EQUAL( 1.0f, TerrainBrush::computeFalloffWeight( 3.99f, settings ), 1e-6f );
}

/**
 * @brief [TerrainBrushTest] 경계: 격자 모서리의 브러시는 격자 안 샘플만 고르고 반경 밖은 고르지 않는다
 */
SW_TEST_CASE( TerrainBrushTest, CollectClipsToGrid )
{
    TerrainBrushSettings settings;
    settings._radius  = 1.5f;
    settings._falloff = 0.0f;
    vector<TerrainBrushSample> listSample;
    TerrainBrush::collectSamples( float2{ 0.0f, 0.0f }, makeGrid( 5 ), settings, listSample );
    // 반경 1.5 안의 격자 점: (0,0) (1,0) (0,1) (1,1)
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( listSample.size() ) );
    for ( const TerrainBrushSample& sample : listSample )
    {
        SW_EXPECT_TRUE( sample._sampleX < 5u && sample._sampleZ < 5u );
        SW_EXPECT_TRUE( sample._weight > 0.0f );
    }

    TerrainBrush::collectSamples( float2{ -10.0f, -10.0f }, makeGrid( 5 ), settings, listSample );
    SW_EXPECT_TRUE( listSample.empty() );
}

/**
 * @brief [TerrainBrushTest] 올리기 · 내리기는 가중치 × 세기 × 시간만큼, 평탄화는 목표를 넘지 않고 다가간다
 */
SW_TEST_CASE( TerrainBrushTest, RaiseLowerAndFlatten )
{
    const uint32               resolution = 3;
    vector<float32>            listHeight( resolution * resolution, 0.0f );
    vector<TerrainBrushSample> listSample{
        TerrainBrushSample{1, 1, 1.0f},
        TerrainBrushSample{0, 1, 0.5f}
    };

    TerrainBrushSettings settings;
    settings._strength = 2.0f;
    settings._mode     = TerrainBrushMode::Raise;
    SW_EXPECT_TRUE( TerrainBrush::applyHeight( listHeight, resolution, listSample, settings, 0.0f, 0.5f ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listHeight[4], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listHeight[3], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listHeight[0], 1e-6f );

    settings._mode = TerrainBrushMode::Lower;
    SW_EXPECT_TRUE( TerrainBrush::applyHeight( listHeight, resolution, listSample, settings, 0.0f, 0.25f ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listHeight[4], 1e-6f );

    settings._mode     = TerrainBrushMode::Flatten;
    settings._strength = 100.0f;
    SW_EXPECT_TRUE( TerrainBrush::applyHeight( listHeight, resolution, listSample, settings, 3.0f, 1.0f ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, listHeight[4], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, listHeight[3], 1e-6f );
    SW_EXPECT_FALSE( TerrainBrush::applyHeight( listHeight, resolution, listSample, settings, 3.0f, 1.0f ) );
}

/**
 * @brief [TerrainBrushTest] 스무드는 프레임 시작 높이의 이웃 3 × 3 평균으로 당긴다 — 샘플 순서가 결과를 바꾸지 않고, 모서리는 격자 안 이웃만 센다
 */
SW_TEST_CASE( TerrainBrushTest, SmoothPullsTowardNeighborAverage )
{
    const uint32    resolution = 3;
    vector<float32> listHeight( resolution * resolution, 0.0f );
    listHeight[4] = 9.0f; // 가운데 봉우리

    TerrainBrushSettings settings;
    settings._mode     = TerrainBrushMode::Smooth;
    settings._strength = 100.0f; // 한 프레임에 목표까지
    const vector<TerrainBrushSample> listSample{
        TerrainBrushSample{1, 1, 1.0f},
        TerrainBrushSample{0, 0, 1.0f}
    };
    SW_EXPECT_TRUE( TerrainBrush::applyHeight( listHeight, resolution, listSample, settings, 0.0f, 1.0f ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listHeight[4], 1e-5f );  // 9 / 9
    SW_EXPECT_NEAR_EQUAL( 2.25f, listHeight[0], 1e-5f ); // 모서리 2 × 2 = 9 / 4, 가운데가 먼저 바뀌어도 프레임 시작 값으로 구한다
}

/**
 * @brief [TerrainBrushTest] 레이어 칠하기는 목표 레이어를 올리고 합을 1 로 지킨다
 */
SW_TEST_CASE( TerrainBrushTest, PaintLayerKeepsSumOne )
{
    const uint32               resolution = 2;
    vector<float4>             listWeight( resolution * resolution, float4{ 1.0f, 0.0f, 0.0f, 0.0f } );
    vector<TerrainBrushSample> listSample{
        TerrainBrushSample{ 0, 0, 1.0f }
    };

    TerrainBrushSettings settings;
    settings._mode       = TerrainBrushMode::PaintLayer;
    settings._layerIndex = 2;
    settings._strength   = 0.5f;
    SW_EXPECT_TRUE( TerrainBrush::applyLayerWeight( listWeight, resolution, listSample, settings, 1.0f ) );
    const float4& weight = listWeight[0];
    SW_EXPECT_NEAR_EQUAL( 0.5f, weight._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, weight._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, weight._x + weight._y + weight._z + weight._w, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listWeight[1]._x, 1e-6f );
}
