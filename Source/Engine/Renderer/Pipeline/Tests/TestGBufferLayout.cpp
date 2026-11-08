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
 * @brief [GBufferLayoutTest] 값 풀이는 셰이더 읽기와 같다 — 노멀 칸 (0, 0) 은 +Z, 머티리얼 칸은 금속도 · 거칠기 · 그려짐, 알베도 알파는 셰이딩 모델
 */
SW_TEST_CASE( GBufferLayoutTest, DecodeFollowsTheShaderEncoding )
{
    vector<GBufferDecodedValue> listValue;
    GBufferLayout::decodeTexel( GBufferLayout::kArrAttachment[1], float4{ 0.0f, 0.0f, 0.0f, 0.0f }, listValue );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listValue.size() ) );
    SW_EXPECT_EQUAL( 3u, listValue[0]._componentCount );
    SW_EXPECT_NEAR( 1.0f, listValue[0]._value._z, 1.0e-5f );

    GBufferLayout::decodeTexel( GBufferLayout::kArrAttachment[2], float4{ 0.25f, 0.75f, 0.0f, 1.0f }, listValue );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( listValue.size() ) ); // 금속도 · 거칠기 · 그려짐(쓰지 않는 칸은 뺀다)
    SW_EXPECT_NEAR( 0.25f, listValue[0]._value._x, 1.0e-6f );
    SW_EXPECT_NEAR( 0.75f, listValue[1]._value._x, 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, listValue[2]._value._x, 1.0e-6f );

    GBufferLayout::decodeTexel( GBufferLayout::kArrAttachment[0], float4{ 0.5f, 0.5f, 0.5f, 0.0f }, listValue );
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( listValue.size() ) );
    SW_EXPECT_NEAR( 0.0f, listValue[3]._value._x, 1.0e-6f ); // Unlit
}

/**
 * @brief [GBufferLayoutTest] 판 2 의 넷째 슬롯은 화면 속도다 — 역할 Velocity, R16G16 실수, 클리어 0, 값은 u · v 그대로(부호 있는 UV 차이)
 */
SW_TEST_CASE( GBufferLayoutTest, VelocityIsTheLastSlot )
{
    SW_EXPECT_EQUAL( 2u, GBufferLayout::kVersion );
    const GBufferAttachmentSpec& velocity = GBufferLayout::kArrAttachment[GBufferLayout::kVelocitySlot];
    SW_EXPECT_EQUAL( GBufferLayout::kAttachmentCount - 1u, GBufferLayout::kVelocitySlot );
    SW_EXPECT_TRUE( velocity._role == RenderPassInputRole::Velocity );
    SW_EXPECT_TRUE( velocity._format == RHIFormat::R16G16_FLOAT );
    SW_EXPECT_TRUE( resolveRenderPassInputRole( velocity._pName, false, "" ) == RenderPassInputRole::Velocity ); // 이름이 곧 역할이다
    vector<GBufferDecodedValue> listValue;
    GBufferLayout::decodeTexel( velocity, float4{ 0.125f, -0.25f, 0.0f, 0.0f }, listValue );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listValue.size() ) );
    SW_EXPECT_NEAR( 0.125f, listValue[0]._value._x, 1.0e-6f );
    SW_EXPECT_NEAR( -0.25f, listValue[1]._value._x, 1.0e-6f );
}
