#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Editor/Panels/RenderTarget/RenderTargetViewUtil.h"

#include "TestFramework/TestFramework.h"

// RenderTargetViewUtilTest — 렌더 타깃 창의 채널 · 깊이 선형화 · 픽셀 값 계산(에디터 보강 "렌더 타깃 보기"). ImGui 없음.

namespace
{
    /** @brief float32 깊이 텍셀(kRHIDepthReadbackFormat 배치)로 width x height 바이트를 만듭니다. */
    sw::vector<uint8> makeDepthBytes( const sw::vector<float32>& listDepth )
    {
        sw::vector<uint8> byte( listDepth.size() * sizeof( float32 ) );
        sw::Memory::copy( byte.data(), listDepth.data(), byte.size() );
        return byte;
    }

    sw::RHITextureMipSpan makeLayout( sw::RHIFormat format, uint32 width, uint32 height )
    {
        sw::RHITextureMipSpan layout{};
        (void)sw::computeRHITextureMipLayout( format, width, height, 0, layout );
        return layout;
    }
} // namespace

/**
 * @brief [RenderTargetViewUtilTest] 선형화는 0 을 가까운 면, 1 을 먼 면으로 되돌리고 사이는 원근 투영의 역이다
 */
SW_TEST_CASE( RenderTargetViewUtilTest, LinearizeDepthInvertsPerspective )
{
    using sw::editor::RenderTargetViewUtil;
    SW_EXPECT_NEAR_EQUAL( 0.1f, RenderTargetViewUtil::linearizeDepth( 0.0f, 0.1f, 100.0f ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, RenderTargetViewUtil::linearizeDepth( 1.0f, 0.1f, 100.0f ), 0.05f ); // f - (f - n) 의 float 뺄셈 오차
    // z = 10 의 투영 깊이 f(z - n) / (z(f - n)) 를 넣으면 10 이 나온다.
    const float32 depthAt10 = 100.0f * ( 10.0f - 0.1f ) / ( 10.0f * ( 100.0f - 0.1f ) );
    SW_EXPECT_NEAR_EQUAL( 10.0f, RenderTargetViewUtil::linearizeDepth( depthAt10, 0.1f, 100.0f ), 1e-3f );
    // 면 값이 잘못됐으면 깊이를 그대로 돌려준다.
    SW_EXPECT_NEAR_EQUAL( 0.5f, RenderTargetViewUtil::linearizeDepth( 0.5f, 10.0f, 1.0f ), 1e-6f );
}

/**
 * @brief [RenderTargetViewUtilTest] 깊이 미리보기는 자동 범위에서 배경(1)을 빼고 가까운 것이 검정, 먼 것이 흰색, 배경도 흰색이다
 */
SW_TEST_CASE( RenderTargetViewUtilTest, DepthPreviewStretchesVisibleRange )
{
    using namespace sw;
    using editor::RenderTargetPreviewImage;
    using editor::RenderTargetViewSettings;
    using editor::RenderTargetViewUtil;

    // 2x2: 가까운 점 · 먼 점 · 사이 점 · 배경.
    const float32           nearDepth = 0.90f;
    const float32           farDepth  = 0.99f;
    const vector<uint8>     byte      = makeDepthBytes( { nearDepth, farDepth, 0.95f, 1.0f } );
    const RHITextureMipSpan layout    = makeLayout( kRHIDepthReadbackFormat, 2, 2 );

    RenderTargetViewSettings settings{};
    settings._nearPlane = 0.1f;
    settings._farPlane  = 100.0f;
    RenderTargetPreviewImage image{};
    SW_ASSERT_TRUE( RenderTargetViewUtil::makePreviewImage( byte, layout, RHIFormat::D24_UNORM_S8_UINT, true, settings, image ) );
    SW_ASSERT_EQUAL( 16u, static_cast<uint32>( image._byte.size() ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( image._byte[0] ) );   // 가까운 점 = 범위의 바닥
    SW_EXPECT_EQUAL( 255u, static_cast<uint32>( image._byte[4] ) ); // 먼 점 = 범위의 꼭대기
    SW_EXPECT_TRUE( image._byte[8] > 0 && image._byte[8] < 255 );
    SW_EXPECT_EQUAL( 255u, static_cast<uint32>( image._byte[12] ) ); // 배경
    SW_EXPECT_NEAR_EQUAL( RenderTargetViewUtil::linearizeDepth( nearDepth, 0.1f, 100.0f ), image._depthLow, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( RenderTargetViewUtil::linearizeDepth( farDepth, 0.1f, 100.0f ), image._depthHigh, 1e-3f );

    // 선형화를 끄고 자동 범위도 끄면 원래 깊이를 그대로 회색으로 보인다.
    settings._bLinearizeDepth = SW_FALSE;
    settings._bAutoDepthRange = SW_FALSE;
    SW_ASSERT_TRUE( RenderTargetViewUtil::makePreviewImage( byte, layout, RHIFormat::D24_UNORM_S8_UINT, true, settings, image ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( MathUtil::round( nearDepth * 255.0f ) ), static_cast<uint32>( image._byte[0] ) );
}

/**
 * @brief [RenderTargetViewUtilTest] 채널 하나를 고르면 그 값이 회색으로 나오고, BGRA 는 순서를 바로잡는다
 */
SW_TEST_CASE( RenderTargetViewUtilTest, ChannelSelectionShowsOneChannelAsGray )
{
    using namespace sw;
    using editor::RenderTargetChannel;
    using editor::RenderTargetPreviewImage;
    using editor::RenderTargetViewSettings;
    using editor::RenderTargetViewUtil;

    const vector<uint8>      byte{ 10, 20, 30, 40 };
    const RHITextureMipSpan  layout = makeLayout( RHIFormat::R8G8B8A8_UNORM, 1, 1 );
    RenderTargetViewSettings settings{};
    RenderTargetPreviewImage image{};

    settings._channel = RenderTargetChannel::Green;
    SW_ASSERT_TRUE( RenderTargetViewUtil::makePreviewImage( byte, layout, RHIFormat::R8G8B8A8_UNORM, false, settings, image ) );
    SW_EXPECT_EQUAL( 20u, static_cast<uint32>( image._byte[0] ) );
    SW_EXPECT_EQUAL( 20u, static_cast<uint32>( image._byte[2] ) );
    SW_EXPECT_EQUAL( 255u, static_cast<uint32>( image._byte[3] ) );

    settings._channel = RenderTargetChannel::Alpha;
    SW_ASSERT_TRUE( RenderTargetViewUtil::makePreviewImage( byte, layout, RHIFormat::R8G8B8A8_UNORM, false, settings, image ) );
    SW_EXPECT_EQUAL( 40u, static_cast<uint32>( image._byte[1] ) );

    // BGRA 의 첫 바이트는 파랑이다.
    settings._channel = RenderTargetChannel::Red;
    SW_ASSERT_TRUE( RenderTargetViewUtil::makePreviewImage( byte, layout, RHIFormat::B8G8R8A8_UNORM, false, settings, image ) );
    SW_EXPECT_EQUAL( 30u, static_cast<uint32>( image._byte[0] ) );

    // 블록 압축은 풀지 않는다.
    SW_EXPECT_FALSE( RenderTargetViewUtil::makePreviewImage( byte, layout, RHIFormat::BC1_UNORM, false, settings, image ) );
}

/**
 * @brief [RenderTargetViewUtilTest] 픽셀 값은 포맷대로 풀린다 — half · float 채널, 없는 채널 0, 없는 알파 1, 범위 밖은 false
 */
SW_TEST_CASE( RenderTargetViewUtilTest, ReadPixelDecodesFormats )
{
    using namespace sw;
    using editor::RenderTargetViewUtil;

    // half 1.0 = 0x3C00, 0.5 = 0x3800, -2.0 = 0xC000, 0 = 0.
    const uint16  arrHalf[4]{ 0x3C00, 0x3800, 0xC000, 0x0000 };
    vector<uint8> listHalf( sizeof( arrHalf ) );
    Memory::copy( listHalf.data(), arrHalf, sizeof( arrHalf ) );
    float4 value{};
    SW_ASSERT_TRUE( RenderTargetViewUtil::readPixel( listHalf, makeLayout( RHIFormat::R16G16B16A16_FLOAT, 1, 1 ), RHIFormat::R16G16B16A16_FLOAT, false, 0, 0,
                                                     value ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, value._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, value._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, value._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, value._w, 1e-6f );

    const float32 arrFloat[2]{ 3.5f, -1.25f };
    vector<uint8> listFloat( sizeof( arrFloat ) );
    Memory::copy( listFloat.data(), arrFloat, sizeof( arrFloat ) );
    SW_ASSERT_TRUE( RenderTargetViewUtil::readPixel( listFloat, makeLayout( RHIFormat::R32G32_FLOAT, 1, 1 ), RHIFormat::R32G32_FLOAT, false, 0, 0, value ) );
    SW_EXPECT_NEAR_EQUAL( 3.5f, value._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( -1.25f, value._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, value._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, value._w, 1e-6f );
    SW_EXPECT_FALSE( RenderTargetViewUtil::readPixel( listFloat, makeLayout( RHIFormat::R32G32_FLOAT, 1, 1 ), RHIFormat::R32G32_FLOAT, false, 1, 0, value ) );

    // 깊이는 float32 배치로 온다(원래 포맷은 D24).
    const vector<uint8> listDepth = makeDepthBytes( { 0.25f, 0.75f } );
    SW_ASSERT_TRUE( RenderTargetViewUtil::readPixel( listDepth, makeLayout( kRHIDepthReadbackFormat, 2, 1 ), RHIFormat::D24_UNORM_S8_UINT, true, 1, 0, value ) );
    SW_EXPECT_NEAR_EQUAL( 0.75f, value._x, 1e-6f );
}

/**
 * @brief [RenderTargetViewUtilTest] D24 텍셀의 아래 24비트가 깊이이고 위 8비트(스텐실)는 버린다 — 네 백엔드 읽기가 이 배치로 바꾼다
 */
SW_TEST_CASE( RenderTargetViewUtilTest, Depth24UnpacksLowBits )
{
    using namespace sw;
    const uint32 arrTexel[3]{ 0x00000000u, 0xAB00FFFFu | 0x00FF0000u, 0x5A800000u };
    uint8        arrByte[sizeof( arrTexel )]{};
    Memory::copy( arrByte, arrTexel, sizeof( arrTexel ) );
    convertRHIDepth24ToFloat( arrByte, 3 );
    float32 arrDepth[3]{};
    Memory::copy( arrDepth, arrByte, sizeof( arrDepth ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, arrDepth[0], 1e-7f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, arrDepth[1], 1e-7f );
    SW_EXPECT_NEAR_EQUAL( static_cast<float32>( 0x800000u ) / static_cast<float32>( 0xFFFFFFu ), arrDepth[2], 1e-7f );
}

/**
 * @brief [RenderTargetViewUtilTest] 미리보기 위의 점은 그린 크기에 비례한 텍셀로 바뀌고, 그림 밖은 false 다
 */
SW_TEST_CASE( RenderTargetViewUtilTest, PreviewPointMapsToTexel )
{
    using namespace sw;
    using editor::RenderTargetViewUtil;
    int2 texel{};
    SW_ASSERT_TRUE( RenderTargetViewUtil::mapPreviewToTexel( float2{ 0.0f, 0.0f }, float2{ 400.0f, 200.0f }, 1600, 800, texel ) );
    SW_EXPECT_EQUAL( 0, texel._x );
    SW_EXPECT_EQUAL( 0, texel._y );
    SW_ASSERT_TRUE( RenderTargetViewUtil::mapPreviewToTexel( float2{ 200.0f, 150.0f }, float2{ 400.0f, 200.0f }, 1600, 800, texel ) );
    SW_EXPECT_EQUAL( 800, texel._x );
    SW_EXPECT_EQUAL( 600, texel._y );
    SW_ASSERT_TRUE( RenderTargetViewUtil::mapPreviewToTexel( float2{ 399.99f, 199.99f }, float2{ 400.0f, 200.0f }, 1600, 800, texel ) );
    SW_EXPECT_EQUAL( 1599, texel._x );
    SW_EXPECT_EQUAL( 799, texel._y );
    SW_EXPECT_FALSE( RenderTargetViewUtil::mapPreviewToTexel( float2{ 400.0f, 10.0f }, float2{ 400.0f, 200.0f }, 1600, 800, texel ) );
    SW_EXPECT_FALSE( RenderTargetViewUtil::mapPreviewToTexel( float2{ -1.0f, 10.0f }, float2{ 400.0f, 200.0f }, 1600, 800, texel ) );
}
