#include "pch.h"

#include "Engine/Environment/Placement/SurfaceLayerWeights.h"
#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/LayerWeightsData.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"

#include "TestFramework/TestFramework.h"

// 레이어 여덟 — 표면 레이어 가중치 값 형식과 스플랫 평면 뽑기(EnvironmentRemainders E3). 디바이스 없음(nogpu).
//
// GPU 쪽(terrain.hlsl 의 여덟 레이어 섞기)은 여기서 돌지 않는다. 대신 셰이더가 읽는 두 텍스처(평면 0 · 1)의 내용과 합 1 로 나누는 식을 CPU 에서 닫힌 값으로 본다.

using namespace sw;

/**
 * @brief [SurfaceLayerWeightsTest] 레이어 여덟을 번호로 읽고 쓰며, 범위 밖은 읽으면 0 · 쓰면 무시한다. 합으로 나누고 합이 0 이면 레이어 0 이 가득이다
 */
SW_TEST_CASE( SurfaceLayerWeightsTest, GetSetAndNormalize )
{
    SurfaceLayerWeights weights{
        float4{0.0f, 0.0f, 0.0f, 0.0f},
        float4{0.0f, 0.0f, 0.0f, 0.0f}
    };
    for ( uint32 layer = 0; layer < SurfaceLayerWeights::kLayerCount; ++layer )
    {
        weights.set( layer, static_cast<float32>( layer + 1 ) );
    }
    for ( uint32 layer = 0; layer < SurfaceLayerWeights::kLayerCount; ++layer )
    {
        SW_EXPECT_NEAR_EQUAL( static_cast<float32>( layer + 1 ), weights.get( layer ), 0.0f );
    }
    SW_EXPECT_NEAR_EQUAL( 4.0f, weights._low._w, 0.0f );  // 레이어 3 은 낮은 쪽의 끝
    SW_EXPECT_NEAR_EQUAL( 5.0f, weights._high._x, 0.0f ); // 레이어 4 는 높은 쪽의 처음
    SW_EXPECT_NEAR_EQUAL( 36.0f, weights.getSum(), 1.0e-5f );

    weights.set( SurfaceLayerWeights::kLayerCount, 100.0f ); // 범위 밖은 무시한다
    SW_EXPECT_NEAR_EQUAL( 36.0f, weights.getSum(), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, weights.get( SurfaceLayerWeights::kLayerCount ), 0.0f );

    const SurfaceLayerWeights normalized = weights.computeNormalized();
    SW_EXPECT_NEAR_EQUAL( 1.0f, normalized.getSum(), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 8.0f / 36.0f, normalized.get( 7 ), 1.0e-6f );

    const SurfaceLayerWeights empty = SurfaceLayerWeights{ float4{}, float4{} }.computeNormalized();
    SW_EXPECT_NEAR_EQUAL( 1.0f, empty.get( 0 ), 0.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, empty.getSum(), 0.0f );
}

/**
 * @brief [TerrainLayersTest] 스플랫 평면 뽑기는 텍셀의 레이어 여덟 바이트를 RGBA 두 장으로 나눈다 - 평면 0 은 레이어 0..3, 평면 1 은 4..7 · 사각형은 그 안만 뽑는다
 * @details 변이: 평면 1 이 레이어 4..7 대신 0..3 을 뽑으면(오프셋 누락) 높은 평면의 첫 비교가 진다. 텍셀 간격을 넷으로 두면 둘째 텍셀부터 어긋난다.
 */
SW_TEST_CASE( TerrainLayersTest, SplatPlanesSplitLayersIntoTwoRgbaTextures )
{
    HeightfieldData data;
    data._resolution = 3;
    data._listHeight.assign( 9, 0u );
    TerrainHeightfield field;
    SW_ASSERT_TRUE( field.initialize( data, float3{ 0.0f, 0.0f, 0.0f }, float2{ 2.0f, 2.0f }, 0.0f, 1.0f ) );

    // 3 × 1 스플랫 - 텍셀마다 레이어 i 의 바이트 = 텍셀 * 10 + i.
    vector<uint8> layerBytes;
    for ( uint32 texel = 0; texel < 3; ++texel )
    {
        for ( uint32 layer = 0; layer < SurfaceLayerWeights::kLayerCount; ++layer )
        {
            layerBytes.push_back( static_cast<uint8>( texel * 10 + layer ) );
        }
    }
    field.setSplat( 3, 1, layerBytes );
    SW_ASSERT_EQUAL( 3u, field.getSplatWidth() );

    vector<uint8> low;
    vector<uint8> high;
    SW_ASSERT_TRUE( field.makeFullSplatPlane( 0, low ) );
    SW_ASSERT_TRUE( field.makeFullSplatPlane( 1, high ) );
    const vector<uint8> expectedLow  = { 0, 1, 2, 3, 10, 11, 12, 13, 20, 21, 22, 23 };
    const vector<uint8> expectedHigh = { 4, 5, 6, 7, 14, 15, 16, 17, 24, 25, 26, 27 };
    SW_EXPECT_TRUE( low == expectedLow );
    SW_EXPECT_TRUE( high == expectedHigh );

    // 사각형은 그 안의 텍셀만 뽑는다(둘째 텍셀 하나).
    TerrainSampleRect middle;
    middle.include( 1, 0 );
    vector<uint8> middleHigh;
    SW_ASSERT_TRUE( field.makeSplatPlane( 1, middle, middleHigh ) );
    const vector<uint8> expectedMiddle = { 14, 15, 16, 17 };
    SW_EXPECT_TRUE( middleHigh == expectedMiddle );

    SW_TEST_DEFENSIVE_SCOPE( "an unknown plane or a rectangle outside the splat is rejected" );
    vector<uint8> rejected;
    SW_EXPECT_FALSE( field.makeFullSplatPlane( SurfaceLayerWeights::kPlaneCount, rejected ) );
    SW_EXPECT_TRUE( rejected.empty() );
    TerrainSampleRect outside;
    outside.include( 2, 0 );
    outside.include( 3, 0 );
    SW_EXPECT_FALSE( field.makeSplatPlane( 0, outside, rejected ) );
    TerrainHeightfield noSplat;
    SW_EXPECT_FALSE( noSplat.makeFullSplatPlane( 0, rejected ) );
}

/**
 * @brief [TerrainLayersTest] 높은 평면의 가중치도 합 1 로 나뉘어 읽힌다 - 레이어 6 만 있는 텍셀은 어디서 읽어도 레이어 6 이 1 이다
 */
SW_TEST_CASE( TerrainLayersTest, HighLayerWeightsAreReadAndNormalized )
{
    HeightfieldData data;
    data._resolution = 3;
    data._listHeight.assign( 9, 0u );
    TerrainHeightfield field;
    SW_ASSERT_TRUE( field.initialize( data, float3{ 0.0f, 0.0f, 0.0f }, float2{ 2.0f, 2.0f }, 0.0f, 1.0f ) );

    vector<uint8> layerBytes( 2 * 2 * SurfaceLayerWeights::kLayerCount, 0u );
    for ( size_t texel = 0; texel < 4; ++texel )
    {
        layerBytes[texel * SurfaceLayerWeights::kLayerCount + 6] = 40u; // 합이 1 이 아닌 값 - 읽으면 나뉜다
    }
    field.setSplat( 2, 2, layerBytes );
    const SurfaceLayerWeights weight = field.computeLayerWeightsAt( 1.0f, 1.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, weight.get( 6 ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, weight.getSum(), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, weight.get( 0 ), 1.0e-5f );
}
