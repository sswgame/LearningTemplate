#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/View/HZBPyramid.h"

using namespace sw;

// HZB 피라미드 — 밉 배치(크기 · 시작 원소), 경계 상자 → 화면 사각형, 사각형을 덮는 밉 고르기. 셰이더(hzb.hlsli · gpu_cull.hlsl)가 같은 식이다.

namespace
{
    /** @brief 원점에서 +Z 를 보는 시야각 0.7 rad · 16:9 원근 투영(행 벡터 규약, 뷰는 단위)입니다 — TestOcclusionTileSnapshot 과 같은 카메라. */
    float4x4 makeViewProj()
    {
        const float32 scale     = 1.0f / MathUtil::tan( 0.35f );
        const float32 farPlane  = 1000.0f;
        const float32 nearPlane = 0.1f;
        float4x4      m{};
        m._11 = scale / ( 16.0f / 9.0f );
        m._22 = scale;
        m._33 = farPlane / ( farPlane - nearPlane );
        m._43 = -nearPlane * farPlane / ( farPlane - nearPlane );
        m._34 = 1.0f;
        m._44 = 0.0f;
        return m;
    }

    HZBScreenRect makeRect( float32 minU, float32 minV, float32 maxU, float32 maxV )
    {
        HZBScreenRect rect;
        rect._minU = minU;
        rect._minV = minV;
        rect._maxU = maxU;
        rect._maxV = maxV;
        return rect;
    }
} // namespace

/** @brief [HZBPyramidTest] 1920 x 1080 — 밉 0 은 2 x 2 텍셀마다 하나, 위로 반씩(올림), 1 x 1 에서 끝나고 시작 원소는 앞 밉들의 합이다. */
SW_TEST_CASE( HZBPyramidTest, LayoutHalvesWithRoundUpToOneTexel )
{
    const HZBLayout layout = HZBLayout::make( 1920, 1080 );
    SW_EXPECT_EQUAL( 960u, layout._arrMip[0]._width );
    SW_EXPECT_EQUAL( 540u, layout._arrMip[0]._height );
    SW_EXPECT_EQUAL( 480u, layout._arrMip[1]._width );
    SW_EXPECT_EQUAL( 270u, layout._arrMip[1]._height );
    SW_EXPECT_EQUAL( 135u, layout._arrMip[2]._height );
    SW_EXPECT_EQUAL( 68u, layout._arrMip[3]._height ); // 135 / 2 올림
    SW_EXPECT_EQUAL( 0u, layout._arrMip[0]._offset );
    SW_EXPECT_EQUAL( 960u * 540u, layout._arrMip[1]._offset );
    const HZBLayout::Mip& top = layout._arrMip[layout._mipCount - 1u];
    SW_EXPECT_EQUAL( 1u, top._width );
    SW_EXPECT_EQUAL( 1u, top._height );
    SW_EXPECT_EQUAL( 11u, layout._mipCount ); // 1920 / 2^11 = 0.94 → 1
    SW_EXPECT_EQUAL( top._offset + 1u, layout._texelCount );
    for ( uint32 mip = 1; mip < layout._mipCount; ++mip )
    {
        const HZBLayout::Mip& below = layout._arrMip[mip - 1u];
        const HZBLayout::Mip& level = layout._arrMip[mip];
        SW_EXPECT_EQUAL( below._offset + below._width * below._height, level._offset );
        SW_EXPECT_EQUAL( ( below._width + 1u ) / 2u, level._width ); // 위 텍셀 하나가 아래 2 x 2 를 덮는다 — 홀수 줄도 빠지지 않는다
    }
}

/** @brief [HZBPyramidTest] 가림 타일 되읽기 밉(16 px)의 크기가 타일 격자와 같다 — 작은 깊이도 피라미드가 그 밉까지 있다. */
SW_TEST_CASE( HZBPyramidTest, TileMipMatchesOcclusionTileGrid )
{
    static_assert( ( 1u << ( HZBLayout::kTileMip + 1u ) ) == FrameRendererUtil::kOcclusionTileSize, "타일 밉 텍셀 하나가 가림 타일 한 변이어야 한다" );
    const HZBLayout large = HZBLayout::make( 1283, 721 );
    SW_EXPECT_EQUAL( FrameRendererUtil::divideRoundUp( 1283, FrameRendererUtil::kOcclusionTileSize ), large._arrMip[HZBLayout::kTileMip]._width );
    SW_EXPECT_EQUAL( FrameRendererUtil::divideRoundUp( 721, FrameRendererUtil::kOcclusionTileSize ), large._arrMip[HZBLayout::kTileMip]._height );
    const HZBLayout tiny = HZBLayout::make( 4, 4 );
    SW_EXPECT_EQUAL( HZBLayout::kTileMip + 1u, tiny._mipCount );
    SW_EXPECT_EQUAL( 1u, tiny._arrMip[HZBLayout::kTileMip]._width );
    SW_EXPECT_EQUAL( 0u, HZBLayout::make( 0, 720 )._mipCount );
}

/** @brief [HZBPyramidTest] 화면 가운데 앞의 상자는 가운데 사각형과 상자 앞면 깊이를 낸다. 카메라 뒤 · 화면 밖 · 가까운 평면을 걸친 상자는 모른다(nullopt). */
SW_TEST_CASE( HZBPyramidTest, ProjectBoxGivesRectAndNearestDepth )
{
    const float4x4                viewProj = makeViewProj();
    const optional<HZBScreenRect> front    = HZBScreenRect::projectBox( viewProj, float3{ 0.0f, 0.0f, 10.0f }, float3{ 1.0f, 1.0f, 1.0f } );
    SW_REQUIRE_TRUE( front.has_value() );
    SW_EXPECT_TRUE( front->_minU < 0.5f && front->_maxU > 0.5f );
    SW_EXPECT_TRUE( front->_minV < 0.5f && front->_maxV > 0.5f );
    SW_EXPECT_TRUE( MathUtil::abs( ( front->_minU + front->_maxU ) - 1.0f ) < 1.0e-5f ); // 좌우 대칭
    // 앞면(z = 9)의 깊이가 가장 가깝다 — 뒷면(z = 11)보다 작다.
    const float32 depthNine = ( 9.0f * viewProj._33 + viewProj._43 ) / 9.0f;
    SW_EXPECT_TRUE( MathUtil::abs( front->_minDepth - depthNine ) < 1.0e-5f );

    SW_EXPECT_FALSE( HZBScreenRect::projectBox( viewProj, float3{ 0.0f, 0.0f, -10.0f }, float3{ 1.0f, 1.0f, 1.0f } ).has_value() );  // 뒤
    SW_EXPECT_FALSE( HZBScreenRect::projectBox( viewProj, float3{ 500.0f, 0.0f, 10.0f }, float3{ 1.0f, 1.0f, 1.0f } ).has_value() ); // 오른쪽 밖
    SW_EXPECT_FALSE( HZBScreenRect::projectBox( viewProj, float3{ 0.0f, 0.0f, 0.5f }, float3{ 1.0f, 1.0f, 1.0f } ).has_value() );    // 눈을 감싼다
}

/** @brief [HZBPyramidTest] 화면 반쯤 걸친 상자는 사각형을 화면 안으로 자른다. */
SW_TEST_CASE( HZBPyramidTest, ProjectBoxClampsToScreen )
{
    const optional<HZBScreenRect> rect = HZBScreenRect::projectBox( makeViewProj(), float3{ 6.0f, 0.0f, 10.0f }, float3{ 2.0f, 1.0f, 1.0f } );
    SW_REQUIRE_TRUE( rect.has_value() );
    SW_EXPECT_TRUE( rect->_minU < 1.0f && rect->_minU > 0.5f );
    SW_EXPECT_EQUAL( 1.0f, rect->_maxU );
}

/** @brief [HZBPyramidTest] 밉 고르기 — 작은 사각형은 밉 0, 커질수록 굵은 밉, 화면 전체는 1 x 1 밉에 맞는다. 2^n 경계를 걸친 사각형은 한 단 더 굵다. */
SW_TEST_CASE( HZBPyramidTest, SelectMipFitsFootprint )
{
    const HZBLayout  layout     = HZBLayout::make( 1024, 1024 );
    constexpr uint32 kFootprint = 2;
    // 깊이 px 0..3 — 밉 0 의 텍셀 0 · 1 = 둘. 맞는다.
    SW_EXPECT_EQUAL( 0u, layout.selectMip( makeRect( 0.0f, 0.0f, 3.5f / 1024.0f, 3.5f / 1024.0f ), kFootprint ).value() );
    // 깊이 px 0..7 — 밉 0 은 넷, 밉 1 은 둘.
    SW_EXPECT_EQUAL( 1u, layout.selectMip( makeRect( 0.0f, 0.0f, 7.5f / 1024.0f, 7.5f / 1024.0f ), kFootprint ).value() );
    // 깊이 px 3..4 — 두 픽셀이지만 밉 0 의 텍셀 1 · 2 를 걸친다(둘 — 맞는다). 발자국 1 이면 밉 2(텍셀 0)까지 가야 한다.
    SW_EXPECT_EQUAL( 0u, layout.selectMip( makeRect( 3.5f / 1024.0f, 0.0f, 4.5f / 1024.0f, 0.5f / 1024.0f ), kFootprint ).value() );
    SW_EXPECT_EQUAL( 2u, layout.selectMip( makeRect( 3.5f / 1024.0f, 0.0f, 4.5f / 1024.0f, 0.5f / 1024.0f ), 1u ).value() );
    // 화면 전체 — 맨 위(1 x 1) 바로 아래 밉(2 x 2)에 맞는다.
    SW_EXPECT_EQUAL( layout._mipCount - 2u, layout.selectMip( makeRect( 0.0f, 0.0f, 1.0f, 1.0f ), kFootprint ).value() );
    SW_EXPECT_EQUAL( layout._mipCount - 1u, layout.selectMip( makeRect( 0.0f, 0.0f, 1.0f, 1.0f ), 1u ).value() );
    // 피라미드가 없으면 고를 밉이 없다.
    SW_EXPECT_FALSE( HZBLayout{}.selectMip( makeRect( 0.0f, 0.0f, 1.0f, 1.0f ), kFootprint ).has_value() );
}
