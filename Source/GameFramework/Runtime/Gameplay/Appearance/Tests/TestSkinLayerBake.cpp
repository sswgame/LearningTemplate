// SkinLayerBakeTest — 피부 층 굽기의 열쇠(같으면 다시 굽지 않는다)와 번짐 격자 → 텍스처 픽셀 대응(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Texture/TextureLayerBake.h"

#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"
#include "GameFramework/Runtime/Gameplay/Appearance/SkinLayer.h"
#include "GameFramework/Runtime/Gameplay/Appearance/SkinLayerBakeBinding.h"

using namespace sw;

namespace
{
    vector<ResolvedSkinLayer> makeStack()
    {
        ResolvedSkinLayer lipstick{};
        lipstick._value._layer    = hashed_string( "lipstick" );
        lipstick._value._color    = float4{ 0.8f, 0.05f, 0.1f, 1.0f };
        lipstick._mask            = hashed_string( "lips.dds" );
        lipstick._roughness       = 0.15f;
        lipstick._roughnessWeight = 1.0f;
        ResolvedSkinLayer tattoo{};
        tattoo._value._layer  = hashed_string( "rose_tattoo" );
        tattoo._value._offset = float2{ 0.7f, 0.3f };
        tattoo._mask          = hashed_string( "rose.dds" );
        tattoo._blend         = SkinLayerBlend::Multiply;
        tattoo._bPlaceable    = SW_TRUE;
        return { lipstick, tattoo };
    }
} // namespace

/**
 * @brief [SkinLayerBakeTest] 같은 층 목록은 같은 열쇠, 색 · 배치 · 순서 · 밑 텍스처 · 해상도가 바뀌면 다른 열쇠다
 * @details 전송 정밀도(색 8 비트) 안의 차이는 같은 굽기로 본다 — 받은 쪽과 보낸 쪽의 결과가 같으므로 다시 굽지 않는다.
 */
SW_TEST_CASE( SkinLayerBakeTest, KeyFollowsStackAndIgnoresSubQuantumNoise )
{
    const vector<ResolvedSkinLayer> stack = makeStack();
    const vector<uint32>            noSmear( stack.size(), 0u );
    const uint64                    key = SkinLayerBakeBinding::computeStackKey( stack, noSmear, "body.dds", 512, 7 );
    SW_EXPECT_EQUAL( key, SkinLayerBakeBinding::computeStackKey( stack, noSmear, "body.dds", 512, 7 ) );
    // 번짐이 없으면 주인이 달라도 같은 열쇠 — 같은 프리셋의 NPC 들이 한 장을 나눈다.
    SW_EXPECT_EQUAL( key, SkinLayerBakeBinding::computeStackKey( stack, noSmear, "body.dds", 512, 99 ) );

    vector<ResolvedSkinLayer> noisy = stack;
    noisy[0]._value._color._x += 0.0005f; // 1/255 보다 작다
    SW_EXPECT_EQUAL( key, SkinLayerBakeBinding::computeStackKey( noisy, noSmear, "body.dds", 512, 7 ) );

    vector<ResolvedSkinLayer> recolored = stack;
    recolored[0]._value._color._x       = 0.5f;
    SW_EXPECT_NOT_EQUAL( key, SkinLayerBakeBinding::computeStackKey( recolored, noSmear, "body.dds", 512, 7 ) );
    vector<ResolvedSkinLayer> moved = stack;
    moved[1]._value._offset._x      = 0.2f;
    SW_EXPECT_NOT_EQUAL( key, SkinLayerBakeBinding::computeStackKey( moved, noSmear, "body.dds", 512, 7 ) );
    const vector<ResolvedSkinLayer> reordered{ stack[1], stack[0] };
    SW_EXPECT_NOT_EQUAL( key, SkinLayerBakeBinding::computeStackKey( reordered, noSmear, "body.dds", 512, 7 ) );
    SW_EXPECT_NOT_EQUAL( key, SkinLayerBakeBinding::computeStackKey( stack, noSmear, "other.dds", 512, 7 ) );
    SW_EXPECT_NOT_EQUAL( key, SkinLayerBakeBinding::computeStackKey( stack, noSmear, "body.dds", 256, 7 ) );
}

/**
 * @brief [SkinLayerBakeTest] 번짐 번호가 바뀌면 다시 굽고, 번짐이 있으면 주인마다 다른 열쇠다
 */
SW_TEST_CASE( SkinLayerBakeTest, SmearRevisionRebakesPerOwner )
{
    const vector<ResolvedSkinLayer> stack = makeStack();
    const vector<uint32>            noSmear( stack.size(), 0u );
    const vector<uint32>            smear{ 3u, 0u };
    const vector<uint32>            smearLater{ 4u, 0u };
    const uint64                    clean = SkinLayerBakeBinding::computeStackKey( stack, noSmear, "body.dds", 512, 7 );
    const uint64                    wet   = SkinLayerBakeBinding::computeStackKey( stack, smear, "body.dds", 512, 7 );
    SW_EXPECT_NOT_EQUAL( clean, wet );
    SW_EXPECT_NOT_EQUAL( wet, SkinLayerBakeBinding::computeStackKey( stack, smearLater, "body.dds", 512, 7 ) );
    SW_EXPECT_NOT_EQUAL( wet, SkinLayerBakeBinding::computeStackKey( stack, smear, "body.dds", 512, 8 ) );
    SW_EXPECT_TRUE( SkinLayerBakeBinding::makeTargetPath( wet, "albedo" ).find( "rendertarget/skinlayer/" ) == 0 );
}

/**
 * @brief [SkinLayerBakeTest] 번짐 격자를 굽기가 읽는 픽셀로 옮긴다 — 이동 0 은 0.5(128), 범위 끝은 0 · 255, 묽음은 B
 * @details 아래로 끈 흐름(+v)은 G 가 128 보다 크고, 셰이더가 (G × 2 − 1) × 범위로 되돌리면 같은 이동이다. 깨끗한 칸은 (128, 128, 0, 255) 다.
 */
SW_TEST_CASE( SkinLayerBakeTest, SmearPixelsEncodeOffsetAndDilution )
{
    SkinLayerSmearState smear;
    smear.initialize( 8 );
    vector<uint8> listPixel;
    smear.makeTexturePixels( TextureLayerBakeConstants::kSmearOffsetRange, listPixel );
    SW_REQUIRE_EQUAL( size_t( 8 * 8 * 4 ), listPixel.size() );
    SW_EXPECT_EQUAL( uint8( 128 ), listPixel[0] );
    SW_EXPECT_EQUAL( uint8( 128 ), listPixel[1] );
    SW_EXPECT_EQUAL( uint8( 0 ), listPixel[2] );
    SW_EXPECT_EQUAL( uint8( 255 ), listPixel[3] );

    // 가운데에서 아래(+v)로 흐름 — 저항 0, 묽힘 0.5.
    smear.advect( float2{ 0.5f, 0.5f }, float2{ 0.0f, 1.0f }, 0.2f, 1.0f, 0.0f, 0.5f );
    smear.makeTexturePixels( TextureLayerBakeConstants::kSmearOffsetRange, listPixel );
    const float2  center   = float2{ 0.5f, 0.5f };
    const float2  offset   = smear.sampleOffset( center );
    const uint32  last     = 7u;
    const uint32  cellX    = static_cast<uint32>( center._x * static_cast<float32>( last ) + 0.5f );
    const uint32  cellY    = static_cast<uint32>( center._y * static_cast<float32>( last ) + 0.5f );
    const size_t  pixel    = ( static_cast<size_t>( cellY ) * 8 + cellX ) * 4;
    const float32 decodedV = ( static_cast<float32>( listPixel[pixel + 1] ) / 255.0f * 2.0f - 1.0f ) * TextureLayerBakeConstants::kSmearOffsetRange;
    SW_EXPECT_TRUE( listPixel[pixel + 1] > 128 );
    SW_EXPECT_NEAR( offset._y, decodedV, 2.0f * TextureLayerBakeConstants::kSmearOffsetRange / 255.0f );
    SW_EXPECT_NEAR( smear.sampleDilution( center ), static_cast<float32>( listPixel[pixel + 2] ) / 255.0f, 1.0f / 255.0f );

    // 범위를 넘는 이동은 끝에서 자른다.
    SkinLayerSmearState far;
    far.initialize( 4 );
    far.drag( float2{ 0.5f, 0.5f }, float2{ 1.0f, 0.0f }, 1.0f, 50.0f, 0.0f );
    far.makeTexturePixels( TextureLayerBakeConstants::kSmearOffsetRange, listPixel );
    SW_EXPECT_EQUAL( uint8( 255 ), listPixel[( 1 * 4 + 1 ) * 4] );
}
