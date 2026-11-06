// RenderPipelineAssetTest — 파이프라인 · 렌더 패스 XML 이 실제 동작을 정하는지: 상태 값이 PSO 에 그대로 들어가고, 받아 줄 수 없는 조합은 읽기 오류이며, 렌더 패스의 첨부가 합쳐진다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

using namespace sw;

namespace
{
    struct RenderPipelineTestUtil
    {
        static constexpr const utf8* kOpaqueState = "_bEnableDepthTest=\"true\" _bEnableDepthWrite=\"true\" _bEnableBlend=\"false\" _cullMode=\"Back\"";

        /** @brief 메시 패스 하나를 든 파이프라인 XML 입니다. @p pipelineExtra 는 루트 안에 그대로 넣습니다. */
        static string makeXML( string_view passAttributes, string_view rootAttributes = "", string_view pipelineExtra = "",
                               string_view attachments = "<RenderPassAttachment _name=\"SceneDepth\" _format=\"D24_UNORM_S8_UINT\" _clearColor=\"1,0,0,0\" _bClear=\"true\"/>" )
        {
            string xml = "<RenderPipelineDesc _name=\"Test\" ";
            xml.append( rootAttributes );
            xml += "><_listAttachment><RenderPassAttachment _name=\"SceneColor\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _bClear=\"true\"/>";
            xml.append( attachments );
            xml += "</_listAttachment><_listPass><RenderGraphPassDesc _name=\"Pass\" ";
            xml.append( passAttributes );
            xml += "><_listInput/><_listOutput><item>SceneColor</item></_listOutput><_listPermutation/></RenderGraphPassDesc></_listPass>";
            xml.append( pipelineExtra );
            xml += "</RenderPipelineDesc>";
            return xml;
        }

        [[nodiscard]] static bool load( RenderPipelineAsset& asset, const string& xml )
        {
            const string path = test::makeScratchPath( "pipeline.xml" );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( path, xml ) );
            return asset.loadFromDiskFile( path );
        }

        /** @brief 메시 패스의 렌더 상태 표기 @p state 로 읽습니다. */
        [[nodiscard]] static bool loadMeshState( RenderPipelineAsset& asset, string_view state, string_view type = "ForwardOpaque", bool bDepthAttachment = true )
        {
            string attributes = "_type=\"";
            attributes.append( type );
            attributes += "\" ";
            if ( bDepthAttachment )
                attributes += "_depthAttachment=\"SceneDepth\" ";
            attributes.append( state );
            return load( asset, makeXML( attributes ) );
        }
    };
} // namespace

/** @brief [RenderPipelineAssetTest] XML 값이 표와 달라도 PSO 에 그대로 들어간다 — 데이터로 패스를 바꾼다 */
SW_TEST_CASE( RenderPipelineAssetTest, XMLStateReachesThePSO )
{
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"true\" _bEnableDepthWrite=\"false\" _bEnableBlend=\"true\" _cullMode=\"None\"" ) );
    RHIPipelineStateDesc desc{};
    FramePSOManager::applyPassRenderState( desc, RenderPassType::ForwardOpaque, &asset.getGraphPass()[0] );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( desc._bEnableDepthTest ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( desc._bEnableDepthWrite ) ); // 표는 깊이를 쓴다
    SW_EXPECT_EQUAL( 1, static_cast<int32>( desc._bEnableBlend ) );      // 표는 섞지 않는다
    SW_EXPECT_TRUE( desc._cullMode == RHICullMode::None );               // 표는 뒷면 컬링이다
    SW_EXPECT_TRUE( desc._depthStencilFormat != RHIFormat::Unknown );

    RenderPipelineAsset noDepth;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::loadMeshState( noDepth, "_bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _bEnableBlend=\"false\" _cullMode=\"Front\"", "ForwardOpaque", false ) );
    FramePSOManager::applyPassRenderState( desc, RenderPassType::ForwardOpaque, &noDepth.getGraphPass()[0] );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( desc._bEnableDepthTest ) );
    SW_EXPECT_TRUE( desc._depthStencilFormat == RHIFormat::Unknown );
    SW_EXPECT_TRUE( desc._cullMode == RHICullMode::Front );
}

/** @brief [RenderPipelineAssetTest] XML 이 없는 내부 슬롯은 표의 값이다. 프리멀티플라이는 언제나 표의 값이다 */
SW_TEST_CASE( RenderPipelineAssetTest, InternalSlotsUseTheTable )
{
    RHIPipelineStateDesc desc{};
    FramePSOManager::applyPassRenderState( desc, RenderPassType::ForwardOpaqueNoDepthWrite, nullptr );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( desc._bEnableDepthTest ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( desc._bEnableDepthWrite ) );
    SW_EXPECT_TRUE( desc._cullMode == RHICullMode::Back );

    FramePSOManager::applyPassRenderState( desc, RenderPassType::Canvas, nullptr );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( desc._bEnableDepthTest ) );
    SW_EXPECT_TRUE( desc._bPremultipliedAlpha == SW_TRUE );
    SW_EXPECT_TRUE( desc._cullMode == RHICullMode::None );
}

/** @brief [RenderPipelineAssetTest] 실행이 받아 줄 수 없는 조합은 무시하지 않고 읽기 오류다 */
SW_TEST_CASE( RenderPipelineAssetTest, ImpossibleStateIsRejected )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.PassCullModeUnknown" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassFullscreenCull" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassRequiredInputRole" ); // 아래 Tonemap 패스는 입력을 적지 않았다
    SW_EXPECT_LOG( "RenderPipelineAsset.PassDepthTestNoDepth" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassDepthWithoutTest" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassDepthWriteNoTest" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    RenderPipelineAsset asset;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"true\" _bEnableDepthWrite=\"true\" _bEnableBlend=\"false\" _cullMode=\"Sideways\"" ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _bEnableBlend=\"false\" _cullMode=\"Back\"", "Tonemap", false ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, RenderPipelineTestUtil::kOpaqueState, "ForwardOpaque", false ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _bEnableBlend=\"false\" _cullMode=\"Back\"" ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"false\" _bEnableDepthWrite=\"true\" _bEnableBlend=\"false\" _cullMode=\"Back\"", "ForwardOpaque", false ) );
}

/** @brief [RenderPipelineAssetTest] 반투명의 블렌드를 끄는 것은 고를 수 있지만 경고한다 */
SW_TEST_CASE( RenderPipelineAssetTest, TransparentWithoutBlendWarns )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.PassBlendOff" );
    RenderPipelineAsset asset;
    SW_EXPECT_TRUE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"true\" _bEnableDepthWrite=\"false\" _bEnableBlend=\"false\" _cullMode=\"Back\"", "Transparent" ) );
}

/** @brief [RenderPipelineAssetTest] 렌더 패스의 첨부가 파이프라인 첨부 앞에 합쳐져 패스가 그 첨부를 쓴다 */
SW_TEST_CASE( RenderPipelineAssetTest, RenderPassAttachmentsAreMerged )
{
    const string        xml = RenderPipelineTestUtil::makeXML( string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" " ) + RenderPipelineTestUtil::kOpaqueState, "",
                                                               "<_listRenderPassRef><item>engine/renderpass/defaultrenderpass.xml</item></_listRenderPassRef>", "" );
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, xml ) );
    const vector<RenderPassAttachment>& listAttachment = asset.getDesc()._listAttachment;
    SW_REQUIRE_EQUAL( size_t( 3 ), listAttachment.size() );
    SW_EXPECT_EQUAL( string( "SceneDepth" ), listAttachment[0]._name );
    SW_EXPECT_EQUAL( string( "ShadowMap" ), listAttachment[1]._name );
    SW_EXPECT_EQUAL( string( "SceneColor" ), listAttachment[2]._name );
}

/** @brief [RenderPipelineAssetTest] 읽지 못한 렌더 패스는 로드 실패이고, 파이프라인과 이름이 겹치는 첨부는 검증 오류다 */
SW_TEST_CASE( RenderPipelineAssetTest, BadRenderPassRefFails )
{
    SW_EXPECT_LOG( "Xml.FileNotFound" );
    SW_EXPECT_LOG( "RenderPipelineAsset.RenderPassRefNotLoaded" );
    SW_EXPECT_LOG( "RenderPipelineAsset.AttachmentNameDuplicated" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    const string        passAttributes = string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" " ) + RenderPipelineTestUtil::kOpaqueState;
    RenderPipelineAsset missing;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( missing, RenderPipelineTestUtil::makeXML( passAttributes, "", "<_listRenderPassRef><item>engine/renderpass/nothing.xml</item></_listRenderPassRef>" ) ) );
    RenderPipelineAsset duplicated; // 기본 첨부에 SceneDepth 가 있고 렌더 패스에도 있다
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( duplicated, RenderPipelineTestUtil::makeXML( passAttributes, "", "<_listRenderPassRef><item>engine/renderpass/defaultrenderpass.xml</item></_listRenderPassRef>" ) ) );
}

/** @brief [RenderPipelineAssetTest] 셰이딩 방식은 패스 구성과 맞아야 한다. 진입점은 적은 값을 그대로 가진다 */
SW_TEST_CASE( RenderPipelineAssetTest, ShadingModelMatchesPasses )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.ShadingModelUnknown" );
    SW_EXPECT_LOG( "RenderPipelineAsset.ShadingModelPassMismatch" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    const string        passAttributes = string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" _vertexEntryPoint=\"MyVS\" " ) + RenderPipelineTestUtil::kOpaqueState;
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( passAttributes, "_shadingModel=\"Forward\"" ) ) );
    SW_EXPECT_EQUAL( string( "MyVS" ), asset.getGraphPass()[0]._vertexEntryPoint );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( passAttributes, "_shadingModel=\"Deferred\"" ) ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( passAttributes, "_shadingModel=\"Ray\"" ) ) );
}

/** @brief [RenderPipelineAssetTest] 엔진 파이프라인이 모두 렌더 패스를 합쳐 검증을 통과한다. 디퍼드 반투명은 섞는다 */
SW_TEST_CASE( RenderPipelineAssetTest, ShippedPipelinesLoad )
{
    for ( const utf8* pPath : { "engine/pipeline/forwardpipeline.xml", "engine/pipeline/forwardpipelinestaged.xml", "engine/pipeline/forwardprepasspipeline.xml", "engine/pipeline/forwardtoonpipeline.xml",
                                "engine/pipeline/forward2dpipeline.xml", "engine/pipeline/deferredpipeline.xml" } )
    {
        RenderPipelineAsset asset;
        SW_EXPECT_TRUE_MSG( asset.loadFromXMLFile( AssetID{ string_view( pPath ) } ), pPath );
    }
    RenderPipelineAsset deferred;
    SW_REQUIRE_TRUE( deferred.loadFromXMLFile( AssetID{ string_view( "engine/pipeline/deferredpipeline.xml" ) } ) );
    for ( const RenderGraphPassDesc& pass : deferred.getGraphPass() )
    {
        if ( pass._resolvedType == RenderPassType::Transparent )
            SW_EXPECT_TRUE( pass._bEnableBlend );
    }
}
