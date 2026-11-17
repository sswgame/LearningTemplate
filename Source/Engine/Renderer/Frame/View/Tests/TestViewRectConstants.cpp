#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Frame/FrameRendererUtil.h"

// 뷰 사각형 상수(RS-3) — 사각형이 풀을 다 덮으면 배율이 정확히 1(그림이 같다), 줄면 그 비율이고 지난 배율을 함께 싣는다. 셰이더 PassCB 가 같은 이름을 선언한다.

using namespace sw;

namespace
{
    bool isNear( float32 a, float32 b )
    {
        return MathUtil::abs( a - b ) < 1.0e-6f;
    }
} // namespace

/** @brief [ViewRectConstantsTest] 사각형 = 풀이면 배율은 정확히 1 이다 — 셰이더의 UV 변환(uv × 1, 상한 1)이 값을 바꾸지 않아 기존 골든이 그대로다. */
SW_TEST_CASE( ViewRectConstantsTest, FullRectIsIdentity )
{
    const FrameRendererUtil::ViewRectConstants constants = FrameRendererUtil::makeViewRectConstants( 1280, 720, 1280, 720, float2::UnitScale );
    SW_EXPECT_TRUE( constants._scale._x == 1.0f && constants._scale._y == 1.0f );
    SW_EXPECT_TRUE( constants._scale._z == 1.0f && constants._scale._w == 1.0f );
    SW_EXPECT_TRUE( isNear( constants._size._x, 1280.0f ) && isNear( constants._size._y, 720.0f ) );
    SW_EXPECT_TRUE( isNear( constants._size._z, 1.0f / 1280.0f ) && isNear( constants._size._w, 1.0f / 720.0f ) );
}

/** @brief [ViewRectConstantsTest] 줄어든 사각형은 그 비율의 배율이고, 지난 프레임 배율(TAA 히스토리)을 zw 에 싣는다. 풀보다 큰 사각형은 풀로 자른다. */
SW_TEST_CASE( ViewRectConstantsTest, PartialRectScalesAndKeepsPreviousScale )
{
    const FrameRendererUtil::ViewRectConstants constants = FrameRendererUtil::makeViewRectConstants( 896, 504, 1280, 720, float2{ 0.8f, 0.8f } );
    SW_EXPECT_TRUE( isNear( constants._scale._x, 0.7f ) && isNear( constants._scale._y, 0.7f ) );
    SW_EXPECT_TRUE( isNear( constants._scale._z, 0.8f ) && isNear( constants._scale._w, 0.8f ) );
    SW_EXPECT_TRUE( isNear( constants._size._x, 896.0f ) && isNear( constants._size._y, 504.0f ) );

    const FrameRendererUtil::ViewRectConstants clipped = FrameRendererUtil::makeViewRectConstants( 2000, 900, 1280, 720, float2::UnitScale );
    SW_EXPECT_TRUE( clipped._scale._x == 1.0f && clipped._scale._y == 1.0f );
    SW_EXPECT_TRUE( isNear( clipped._size._x, 1280.0f ) && isNear( clipped._size._y, 720.0f ) );

    const FrameRendererUtil::ViewRectConstants empty = FrameRendererUtil::makeViewRectConstants( 0, 0, 0, 0, float2::UnitScale );
    SW_EXPECT_TRUE( empty._scale._x == 1.0f && empty._size._z == 0.0f );
}

/** @brief [ViewRectConstantsTest] 셰이더 계약: PassCB 가 C++ 이 채우는 이름(`passConstantNames`)으로 두 상수를 float4 로 선언하고, PassTexture 가 UV 를 바꾼다. */
SW_TEST_CASE( ViewRectConstantsTest, PassBufferDeclaresViewRectConstants )
{
    const string projectRoot = FileUtil::normalizeSeparators( ProcessRoots::get()._project.str() );
    SW_REQUIRE_FALSE( projectRoot.empty() );
    string bindingText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::joinPath( projectRoot, "Content/engine/shaders/binding.hlsli" ), bindingText ) );
    string textureText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::joinPath( projectRoot, "Content/engine/shaders/binding_texture.hlsli" ), textureText ) );

    const PassConstantNames& names = passConstantNames();
    SW_EXPECT_TRUE( bindingText.find( string( "float4 " ) + names._viewRectScale.c_str() + ";" ) != string::npos );
    SW_EXPECT_TRUE( bindingText.find( string( "float4 " ) + names._viewRectSize.c_str() + ";" ) != string::npos );
    SW_EXPECT_TRUE( textureText.find( "static float2 PassTexture::toViewRectUV( float2 uv )" ) != string::npos );
    SW_EXPECT_TRUE( textureText.find( "static float4 PassTexture::sampleAlbedoPrevious( float2 uv )" ) != string::npos );
}
