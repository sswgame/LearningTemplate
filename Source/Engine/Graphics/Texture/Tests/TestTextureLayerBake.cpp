#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Texture/TextureLayerBake.h"

// TextureLayerBakeTest — UV 공간 층 굽기의 루트 상수 묶음(layer_bake.hlsl 과 같은 배치)과 결과 · 작업 타깃 번갈기. 디바이스 없음(nogpu).

/**
 * @brief [TextureLayerBakeTest] 층 값이 셰이더가 푸는 비트 자리에 담긴다
 * @details 색은 r 이 낮은 바이트, 이동 · 영역은 [0,1] 16 비트 둘, 크기는 [0.01, 4], 회전은 [−180, 180] 의 낮은 16 비트에 섞기 · 배치 · 대칭 깃발이 위에 붙는다.
 */
SW_TEST_CASE( TextureLayerBakeTest, PacksLayerIntoShaderLayout )
{
    using Constants = sw::TextureLayerBakeConstants;
    sw::TextureLayerBakeLayer layer{};
    layer._color           = sw::float4{ 1.0f, 0.0f, 0.5f, 0.25f };
    layer._offset          = sw::float2{ 0.25f, 0.75f };
    layer._scale           = sw::float2{ 2.0f, Constants::kMinScale };
    layer._rotationDegrees = 90.0f;
    layer._regionMin       = sw::float2{ 0.5f, 0.0f };
    layer._regionMax       = sw::float2{ 1.0f, 0.5f };
    layer._roughness       = 0.2f;
    layer._roughnessWeight = 1.0f;
    layer._blend           = 2;
    layer._bPlaced         = SW_TRUE;
    layer._bMirror         = SW_TRUE;

    Constants constants{};
    constants.packLayer( layer );
    const uint32 color = constants._arrDword[Constants::kRootColor];
    SW_EXPECT_EQUAL( 255u, color & 0xFFu );
    SW_EXPECT_EQUAL( 0u, ( color >> 8 ) & 0xFFu );
    SW_EXPECT_EQUAL( 128u, ( color >> 16 ) & 0xFFu );
    SW_EXPECT_EQUAL( 64u, color >> 24 );
    SW_EXPECT_NEAR( 0.25f, Constants::decodeUnorm16( constants._arrDword[Constants::kRootOffset], 0.0f, 1.0f ), 1e-4f );
    SW_EXPECT_NEAR( 0.75f, Constants::decodeUnorm16( constants._arrDword[Constants::kRootOffset] >> 16, 0.0f, 1.0f ), 1e-4f );
    SW_EXPECT_NEAR( 2.0f, Constants::decodeUnorm16( constants._arrDword[Constants::kRootScale], Constants::kMinScale, Constants::kMaxScale ), 1e-3f );
    SW_EXPECT_NEAR( Constants::kMinScale, Constants::decodeUnorm16( constants._arrDword[Constants::kRootScale] >> 16, Constants::kMinScale, Constants::kMaxScale ), 1e-4f );
    const uint32 rotationFlags = constants._arrDword[Constants::kRootRotationFlags];
    SW_EXPECT_NEAR( 90.0f, Constants::decodeUnorm16( rotationFlags, -Constants::kMaxRotation, Constants::kMaxRotation ), 0.01f );
    SW_EXPECT_EQUAL( 2u, ( rotationFlags >> Constants::kBlendShift ) & 0x3u );
    SW_EXPECT_TRUE( ( rotationFlags & Constants::kFlagPlaced ) != 0u );
    SW_EXPECT_TRUE( ( rotationFlags & Constants::kFlagMirror ) != 0u );
    SW_EXPECT_NEAR( 0.5f, Constants::decodeUnorm16( constants._arrDword[Constants::kRootRegionMin], 0.0f, 1.0f ), 1e-4f );
    SW_EXPECT_NEAR( 0.5f, Constants::decodeUnorm16( constants._arrDword[Constants::kRootRegionMax] >> 16, 0.0f, 1.0f ), 1e-4f );
    SW_EXPECT_EQUAL( 51u, constants._arrDword[Constants::kRootSurface] & 0xFFu );
    SW_EXPECT_EQUAL( 255u, ( constants._arrDword[Constants::kRootSurface] >> 8 ) & 0xFFu );
    // 텍스처 칸은 그리는 쪽이 채운다 — 묶음이 건드리지 않는다.
    SW_EXPECT_EQUAL( 0u, constants._arrDword[Constants::kRootMask] );
    SW_EXPECT_TRUE( Constants::kRootDwordCount <= 16u ); // 네 백엔드 공통 루트 상수 한도(shaderslot::kRootConstantDwords)
}

/**
 * @brief [TextureLayerBakeTest] 범위 밖 값은 끝에서 자르고 회전 · 대칭이 없는 층은 깃발이 없다
 */
SW_TEST_CASE( TextureLayerBakeTest, ClampsOutOfRangeValues )
{
    using Constants = sw::TextureLayerBakeConstants;
    SW_EXPECT_EQUAL( 0u, Constants::encodeUnorm16( -1.0f, 0.0f, 1.0f ) );
    SW_EXPECT_EQUAL( 65535u, Constants::encodeUnorm16( 9.0f, 0.0f, 1.0f ) );
    SW_EXPECT_EQUAL( 255u, Constants::encodeUnorm8( 2.0f ) );
    sw::TextureLayerBakeLayer plain{};
    Constants                 constants{};
    constants.packLayer( plain );
    SW_EXPECT_EQUAL( 0u, constants._arrDword[Constants::kRootRotationFlags] & ( Constants::kFlagPlaced | Constants::kFlagMirror ) );
}

/**
 * @brief [TextureLayerBakeTest] 마지막 그리기가 결과 타깃에 떨어지게 뒤에서부터 번갈아 짚는다
 * @details 층 셋이면 결과 · 작업 · 결과, 넷이면 작업 · 결과 · 작업 · 결과. 층이 없으면 밑을 옮기는 한 번이 결과다.
 */
SW_TEST_CASE( TextureLayerBakeTest, LastDrawLandsOnResult )
{
    sw::TextureLayerBakeRequest request{};
    SW_EXPECT_EQUAL( 1u, request.getDrawCount() );
    SW_EXPECT_TRUE( request.writesResult( 0 ) );
    request._listLayer.resize( 3 );
    SW_EXPECT_TRUE( request.writesResult( 0 ) );
    SW_EXPECT_FALSE( request.writesResult( 1 ) );
    SW_EXPECT_TRUE( request.writesResult( 2 ) );
    request._listLayer.resize( 4 );
    SW_EXPECT_FALSE( request.writesResult( 0 ) );
    SW_EXPECT_TRUE( request.writesResult( 3 ) );
}
