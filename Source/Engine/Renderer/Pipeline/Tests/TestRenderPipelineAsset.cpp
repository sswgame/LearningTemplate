// RenderPipelineAssetTest — 파이프라인 XML 이 고를 수 있는 값과 표가 정하는 값의 경계(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

using namespace sw;

namespace
{
    struct RenderPipelineTestUtil
    {
        /** @brief 패스 하나(@p passAttributes)를 든 파이프라인 XML 입니다. */
        static string makeXML( string_view passAttributes )
        {
            string xml = "<RenderPipelineDesc _name=\"Test\"><_listAttachment>"
                         "<RenderPassAttachment _name=\"SceneColor\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _bClear=\"true\"/>"
                         "<RenderPassAttachment _name=\"SceneDepth\" _format=\"D24_UNORM_S8_UINT\" _clearColor=\"1,0,0,0\" _bClear=\"true\"/>"
                         "</_listAttachment><_listPass><RenderGraphPassDesc _name=\"Opaque\" _type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" ";
            xml.append( passAttributes );
            xml += "><_listOutput><item>SceneColor</item><item>SceneDepth</item></_listOutput></RenderGraphPassDesc></_listPass></RenderPipelineDesc>";
            return xml;
        }

        [[nodiscard]] static bool load( RenderPipelineAsset& asset, string_view passAttributes )
        {
            const string path = test::makeScratchPath( "pipeline.xml" );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( path, makeXML( passAttributes ) ) );
            return asset.loadFromDiskFile( path );
        }
    };
} // namespace

/** @brief [RenderPipelineAssetTest] 적지 않은 칸(입력 · 퍼뮤테이션 · 셰이더)은 비어 있어도 읽히고, 셰이더는 표의 기본값이 된다 */
SW_TEST_CASE( RenderPipelineAssetTest, MinimalPassLoads )
{
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, "" ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), asset.getGraphPass().size() );
    const RenderGraphPassDesc& pass = asset.getGraphPass()[0];
    SW_EXPECT_TRUE( pass._resolvedType == RenderPassType::ForwardOpaque );
    SW_EXPECT_TRUE( pass._listInput.empty() );
    SW_EXPECT_TRUE( pass._shaderPath.empty() );
}

/** @brief [RenderPipelineAssetTest] 표가 정하는 렌더 상태 · 진입점은 XML 에 적으면 오류다 — 표와 어긋난 값이 조용히 먹거나 무시되지 않는다 */
SW_TEST_CASE( RenderPipelineAssetTest, RenderStateCannotBeWritten )
{
    SW_EXPECT_LOG( "Serialization.OrphanValue" );
    for ( const utf8* pAttribute : { "_bEnableBlend=\"true\"", "_bEnableDepthTest=\"false\"", "_bEnableDepthWrite=\"false\"", "_cullMode=\"None\"", "_vertexEntryPoint=\"VSMain\"", "_pixelEntryPoint=\"PSMain\"" } )
    {
        RenderPipelineAsset asset;
        SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, pAttribute ) );
    }
}

/** @brief [RenderPipelineAssetTest] 파이프라인이 고르는 렌더 상태의 근거인 표: 반투명은 섞고 깊이를 쓰지 않는다. 풀스크린은 깊이가 없다. 외곽선 껍질은 앞면을 컬링한다 */
SW_TEST_CASE( RenderPipelineAssetTest, PassTableDecidesRenderState )
{
    const RenderPassTypeInfo& transparent = getRenderPassTypeInfo( RenderPassType::Transparent );
    SW_EXPECT_TRUE( transparent.hasFlag( RenderPassTraitFlag::kBlend ) );
    SW_EXPECT_TRUE( transparent.hasFlag( RenderPassTraitFlag::kDepthTest ) );
    SW_EXPECT_FALSE( transparent.hasFlag( RenderPassTraitFlag::kDepthWrite ) );

    const RenderPassTypeInfo& opaque = getRenderPassTypeInfo( RenderPassType::ForwardOpaque );
    SW_EXPECT_FALSE( opaque.hasFlag( RenderPassTraitFlag::kBlend ) );
    SW_EXPECT_TRUE( opaque.hasFlag( RenderPassTraitFlag::kDepthWrite ) );

    SW_EXPECT_FALSE( getRenderPassTypeInfo( RenderPassType::Tonemap ).hasFlag( RenderPassTraitFlag::kDepthTest ) );
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::MeshOutline ).hasFlag( RenderPassTraitFlag::kCullFront ) );
}
