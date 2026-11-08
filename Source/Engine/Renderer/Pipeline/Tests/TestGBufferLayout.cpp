#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Renderer/Pipeline/GBufferLayout.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

// G버퍼 명세 한 벌 — 셰이더 판 번호 · PSO 포맷 · 값 풀이가 같은 표를 본다.

using namespace sw;

/**
 * @brief [GBufferLayoutTest] 셰이더의 판 번호(binding_output.hlsli `SW_GBUFFER_LAYOUT_VERSION`)가 C++ 명세의 판과 같다 — 한쪽만 바꾸면 진다
 */
SW_TEST_CASE( GBufferLayoutTest, ShaderVersionMatchesTheLayout )
{
    string text;
    SW_REQUIRE_TRUE( AssetIO::readTextAsset( AssetID{ "engine/shaders/binding_output.hlsli" }, text ) );
    constexpr string_view kDefine = "#define SW_GBUFFER_LAYOUT_VERSION ";
    const size_t          at      = text.find( kDefine.data(), 0, kDefine.size() );
    SW_REQUIRE_TRUE( at != string::npos );
    const size_t lineEnd = text.find( '\n', at );
    string_view  number  = string_view( text ).substr( at + kDefine.size(), lineEnd - at - kDefine.size() );
    number               = StringUtil::trim( number );
    int32 version{ 0 };
    SW_REQUIRE_TRUE( StringUtil::parseInt( number, version ) );
    SW_EXPECT_EQUAL( static_cast<int32>( GBufferLayout::kVersion ), version );
}

/**
 * @brief [GBufferLayoutTest] G버퍼 패스의 PSO 포맷은 명세의 첨부 포맷이고, 이름으로 첨부를 찾는다
 */
SW_TEST_CASE( GBufferLayoutTest, PassFormatsComeFromTheLayout )
{
    const RenderPassTypeInfo& info = getRenderPassTypeInfo( RenderPassType::GBuffer );
    SW_REQUIRE_EQUAL( GBufferLayout::kAttachmentCount, info._colorTargetCount );
    for ( uint32 slot = 0; slot < GBufferLayout::kAttachmentCount; ++slot )
    {
        SW_EXPECT_TRUE( info._pColorFormat[slot] == GBufferLayout::kArrAttachment[slot]._format );
        SW_EXPECT_TRUE( GBufferLayout::findAttachment( GBufferLayout::kArrAttachment[slot]._pName ) == &GBufferLayout::kArrAttachment[slot] );
    }
    SW_EXPECT_TRUE( GBufferLayout::findAttachment( "SceneColor" ) == nullptr );
}

/**
 * @brief [GBufferLayoutTest] 값 풀이는 셰이더 읽기와 같다 — 노멀 칸 (0, 0) 은 +Z, 머티리얼 칸은 금속도 · 거칠기 · 반사율 · 가림, 알베도 알파는 셰이딩 모델 번호
 */
SW_TEST_CASE( GBufferLayoutTest, DecodeFollowsTheShaderEncoding )
{
    vector<GBufferDecodedValue> listValue;
    GBufferLayout::decodeTexel( GBufferLayout::kArrAttachment[1], float4{ 0.0f, 0.0f, 0.0f, 0.0f }, listValue );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listValue.size() ) );
    SW_EXPECT_EQUAL( 3u, listValue[0]._componentCount );
    SW_EXPECT_NEAR( 1.0f, listValue[0]._value._z, 1.0e-5f );

    GBufferLayout::decodeTexel( GBufferLayout::kArrAttachment[2], float4{ 0.25f, 0.75f, 0.5f, 1.0f }, listValue );
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( listValue.size() ) ); // 금속도 · 거칠기 · 반사율 · 가림
    SW_EXPECT_NEAR( 0.25f, listValue[0]._value._x, 1.0e-6f );
    SW_EXPECT_NEAR( 0.75f, listValue[1]._value._x, 1.0e-6f );
    SW_EXPECT_NEAR( 0.5f, listValue[2]._value._x, 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, listValue[3]._value._x, 1.0e-6f );

    GBufferLayout::decodeTexel( GBufferLayout::kArrAttachment[0], float4{ 0.5f, 0.5f, 0.5f, GBufferLayout::encodeShadingModel( ShadingModelID::Cloth ) }, listValue );
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( listValue.size() ) );
    SW_EXPECT_NEAR( static_cast<float32>( ShadingModelID::Cloth ), listValue[3]._value._x, 1.0e-6f );
    SW_EXPECT_TRUE( string_view( listValue[3]._pName ) == "Shading model: Cloth" );
}

/**
 * @brief [GBufferLayoutTest] 셰이딩 모델 번호는 8 비트 UNORM 칸을 지나도 같은 번호로 돌아온다 — 클리어 0 은 None(그리지 않음), 표 밖 값은 Count
 */
SW_TEST_CASE( GBufferLayoutTest, ShadingModelRoundTripsThroughUnorm8 )
{
    for ( uint32 id = 0; id < static_cast<uint32>( ShadingModelID::Count ); ++id )
    {
        const float32 encoded   = GBufferLayout::encodeShadingModel( static_cast<ShadingModelID>( id ) );
        const float32 quantized = static_cast<float32>( static_cast<uint32>( encoded * 255.0f + 0.5f ) ) / 255.0f; // RGBA8 에 적고 읽은 값
        SW_EXPECT_TRUE( GBufferLayout::decodeShadingModel( quantized ) == static_cast<ShadingModelID>( id ) );
    }
    SW_EXPECT_TRUE( GBufferLayout::decodeShadingModel( 0.0f ) == ShadingModelID::None );
    SW_EXPECT_TRUE( GBufferLayout::decodeShadingModel( 1.0f ) == ShadingModelID::Count );
    SW_EXPECT_TRUE( string_view( GBufferLayout::getShadingModelName( ShadingModelID::Count ) ) == "Shading model: (out of table)" );
}

/**
 * @brief [GBufferLayoutTest] 셰이더의 번호 표(shading_model_ids.hlsli)가 C++ enum 과 같은 줄을 가진다 — 셰이더에만 번호를 더하면 진다
 * @details enum 은 같은 파일의 define 으로 만들지만, 셰이더가 쓰는 define 이 enum 에 빠졌는지는 글을 세어야 안다.
 */
SW_TEST_CASE( GBufferLayoutTest, ShaderShadingModelTableMatchesTheEnum )
{
    string text;
    SW_REQUIRE_TRUE( AssetIO::readTextAsset( AssetID{ "engine/shaders/shading_model_ids.hlsli" }, text ) );
    uint32                defineCount = 0;
    constexpr string_view kPrefix     = "#define SW_SHADING_MODEL_ID_";
    for ( size_t at = text.find( kPrefix.data(), 0, kPrefix.size() ); at != string::npos; at = text.find( kPrefix.data(), at + 1, kPrefix.size() ) )
        ++defineCount;
    // 번호 줄 + COUNT + SCALE.
    SW_EXPECT_EQUAL( static_cast<uint32>( ShadingModelID::Count ) + 2u, defineCount );
}
