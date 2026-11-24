// RenderPipelineAssetTest — 파이프라인 · 렌더 패스 XML 이 실제 동작을 정하는지: 상태 값이 PSO 에 그대로 들어가고, 받아 줄 수 없는 조합은 읽기 오류이며, 렌더 패스의 첨부가 합쳐진다(nogpu).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

using namespace sw;

namespace
{
    struct RenderPipelineTestUtil
    {
        static constexpr const utf8* kOpaqueState = "_bEnableDepthTest=\"true\" _bEnableDepthWrite=\"true\" _cullMode=\"Back\"";

        /** @brief 메시 패스 하나를 든 파이프라인 XML 입니다. @p pipelineExtra 는 루트 안에 그대로 넣습니다. */
        static string makeXML( string_view passAttributes, string_view rootAttributes = "", string_view pipelineExtra = "",
                               string_view attachments = "<RenderPassAttachment _name=\"SceneDepth\" _format=\"D24_UNORM_S8_UINT\" _clearDepth=\"1\" _loadOp=\"Clear\"/>" )
        {
            string xml = "<RenderPipelineDesc _name=\"Test\" ";
            xml.append( rootAttributes );
            xml += "><_listAttachment><RenderPassAttachment _name=\"SceneColor\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _loadOp=\"Clear\"/>";
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
            const AbsolutePath path = test::makeScratchPath( "pipeline.xml" );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( path, xml ) );
            return asset.loadFromDiskFile( path.str() );
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
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"true\" _bEnableDepthWrite=\"false\" _blendPreset=\"Transparent\" _cullMode=\"None\"" ) );
    RHIPipelineStateDesc desc{};
    FramePSOManager::applyPassRenderState( desc, RenderPassKind::ForwardOpaque, &asset.getGraphPass()[0] );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( desc._renderState._bEnableDepthTest ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( desc._renderState._bEnableDepthWrite ) );                // 표는 깊이를 쓴다
    SW_EXPECT_TRUE( desc._renderState._arrBlendTarget[0] == BlendTargetState::makeStraightAlpha() ); // 표는 섞지 않는다
    SW_EXPECT_TRUE( desc._renderState._cullMode == CullMode::None );                                 // 표는 뒷면 컬링이다
    SW_EXPECT_TRUE( desc._depthStencilFormat != RHIFormat::Unknown );

    RenderPipelineAsset noDepth;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::loadMeshState( noDepth, "_bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _cullMode=\"Front\"", "ForwardOpaque", false ) );
    FramePSOManager::applyPassRenderState( desc, RenderPassKind::ForwardOpaque, &noDepth.getGraphPass()[0] );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( desc._renderState._bEnableDepthTest ) );
    SW_EXPECT_TRUE( desc._depthStencilFormat == RHIFormat::Unknown );
    SW_EXPECT_TRUE( desc._renderState._cullMode == CullMode::Front );
}

/** @brief [RenderPipelineAssetTest] XML 이 없는 내부 슬롯은 표의 값이다 */
SW_TEST_CASE( RenderPipelineAssetTest, InternalSlotsUseTheTable )
{
    RHIPipelineStateDesc desc{};
    FramePSOManager::applyPassRenderState( desc, RenderPassKind::ForwardOpaqueNoDepthWrite, nullptr );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( desc._renderState._bEnableDepthTest ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( desc._renderState._bEnableDepthWrite ) );
    SW_EXPECT_TRUE( desc._renderState._cullMode == CullMode::Back );

    FramePSOManager::applyPassRenderState( desc, RenderPassKind::Canvas, nullptr );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( desc._renderState._bEnableDepthTest ) );
    SW_EXPECT_TRUE( desc._renderState._arrBlendTarget[0] == BlendTargetState::makePremultipliedAlpha() );
    SW_EXPECT_TRUE( desc._renderState._cullMode == CullMode::None );
}

/** @brief [RenderPipelineAssetTest] 실행이 받아 줄 수 없는 조합은 무시하지 않고 읽기 오류다 */
SW_TEST_CASE( RenderPipelineAssetTest, ImpossibleStateIsRejected )
{
    SW_EXPECT_LOG( "Serialization.UnknownEnumerator" ); // 오타는 enum 이 읽기에서 거절한다
    SW_EXPECT_LOG( "Serialization.OrphanValue" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassFullscreenCull" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassRequiredInputRole" ); // 아래 Tonemap 패스는 입력을 적지 않았다
    SW_EXPECT_LOG( "RenderPipelineAsset.PassDepthTestNoDepth" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassDepthWithoutTest" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassDepthWriteNoTest" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    RenderPipelineAsset asset;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"true\" _bEnableDepthWrite=\"true\" _cullMode=\"Sideways\"" ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _cullMode=\"Back\"", "Tonemap", false ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, RenderPipelineTestUtil::kOpaqueState, "ForwardOpaque", false ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _cullMode=\"Back\"" ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"false\" _bEnableDepthWrite=\"true\" _cullMode=\"Back\"", "ForwardOpaque", false ) );
}

/** @brief [RenderPipelineAssetTest] 반투명의 블렌드를 끄는 것은 고를 수 있지만 경고한다 */
SW_TEST_CASE( RenderPipelineAssetTest, TransparentWithoutBlendWarns )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.PassBlendOff" );
    RenderPipelineAsset asset;
    SW_EXPECT_TRUE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"true\" _bEnableDepthWrite=\"false\" _cullMode=\"Back\"", "Transparent" ) );
}

/** @brief [RenderPipelineAssetTest] 렌더 패스의 첨부가 파이프라인 첨부 앞에 합쳐져 패스가 그 첨부를 쓴다 */
SW_TEST_CASE( RenderPipelineAssetTest, RenderPassAttachmentsAreMerged )
{
    StringBuilder<constant::buffer::kMax256> passAttributes;
    passAttributes.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" %#", RenderPipelineTestUtil::kOpaqueState );
    const string        xml = RenderPipelineTestUtil::makeXML( passAttributes.view(), "",
                                                               "<_listRenderPassRef><item>engine/renderpass/defaultrenderpass.xml</item></_listRenderPassRef>", "" );
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, xml ) );
    const vector<RenderPassAttachment>& listAttachment = asset.getDesc()._listAttachment;
    SW_REQUIRE_EQUAL( size_t( 4 ), listAttachment.size() );
    SW_EXPECT_EQUAL( string( "SceneDepth" ), listAttachment[0]._name );
    SW_EXPECT_EQUAL( string( "ShadowMap" ), listAttachment[1]._name );
    SW_EXPECT_EQUAL( string( "LocalShadowMap" ), listAttachment[2]._name );
    SW_EXPECT_EQUAL( string( "SceneColor" ), listAttachment[3]._name );
}

/** @brief [RenderPipelineAssetTest] 읽지 못한 렌더 패스는 로드 실패이고, 파이프라인과 이름이 겹치는 첨부는 검증 오류다 */
SW_TEST_CASE( RenderPipelineAssetTest, BadRenderPassRefFails )
{
    SW_EXPECT_LOG( "Xml.FileNotFound" );
    SW_EXPECT_LOG( "RenderPipelineAsset.RenderPassRefNotLoaded" );
    SW_EXPECT_LOG( "RenderPipelineAsset.AttachmentNameDuplicated" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    StringBuilder<constant::buffer::kMax256> passAttributesBuilder;
    passAttributesBuilder.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" %#", RenderPipelineTestUtil::kOpaqueState );
    const string_view   passAttributes = passAttributesBuilder.view();
    RenderPipelineAsset missing;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( missing, RenderPipelineTestUtil::makeXML( passAttributes, "", "<_listRenderPassRef><item>engine/renderpass/nothing.xml</item></_listRenderPassRef>" ) ) );
    RenderPipelineAsset duplicated; // 기본 첨부에 SceneDepth 가 있고 렌더 패스에도 있다
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( duplicated, RenderPipelineTestUtil::makeXML( passAttributes, "", "<_listRenderPassRef><item>engine/renderpass/defaultrenderpass.xml</item></_listRenderPassRef>" ) ) );
}

/** @brief [RenderPipelineAssetTest] 대소문자만 다른 첨부 이름은 겹침이다 — 실행 때 첨부를 대소문자를 무시하는 이름으로 찾는다 */
SW_TEST_CASE( RenderPipelineAssetTest, AttachmentNamesDifferingOnlyInCaseAreDuplicates )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.AttachmentNameDuplicated" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    StringBuilder<constant::buffer::kMax256> passAttributesBuilder;
    passAttributesBuilder.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" %#", RenderPipelineTestUtil::kOpaqueState );
    const string_view   passAttributes = passAttributesBuilder.view();
    const string        attachments    = "<RenderPassAttachment _name=\"SceneDepth\" _format=\"D24_UNORM_S8_UINT\" _clearDepth=\"1\" _loadOp=\"Clear\"/>"
                                         "<RenderPassAttachment _name=\"scenedepth\" _format=\"D24_UNORM_S8_UINT\" _clearDepth=\"1\" _loadOp=\"Clear\"/>";
    RenderPipelineAsset asset;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( passAttributes, "", "", attachments ) ) );
}

/** @brief [RenderPipelineAssetTest] 셰이딩 방식은 패스 구성과 맞아야 한다. 진입점은 적은 값을 그대로 가진다 */
SW_TEST_CASE( RenderPipelineAssetTest, ShadingModelMatchesPasses )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.ShadingModelUnknown" );
    SW_EXPECT_LOG( "RenderPipelineAsset.ShadingModelPassMismatch" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    StringBuilder<constant::buffer::kMax256> passAttributesBuilder;
    passAttributesBuilder.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" _vertexEntryPoint=\"MyVS\" %#", RenderPipelineTestUtil::kOpaqueState );
    const string_view   passAttributes = passAttributesBuilder.view();
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( passAttributes, "_shadingModel=\"Forward\"" ) ) );
    SW_EXPECT_EQUAL( string( "MyVS" ), asset.getGraphPass()[0]._vertexEntryPoint );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( passAttributes, "_shadingModel=\"Deferred\"" ) ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( passAttributes, "_shadingModel=\"Ray\"" ) ) );
}

namespace
{
    struct ComputePassTestUtil
    {
        static constexpr const utf8* kNoState = "_cullMode=\"None\" _bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\"";

        /** @brief 컴퓨트 패스 하나의 파이프라인 XML 입니다. */
        static string makeXML( string_view attributes, string_view inputs, string_view outputs )
        {
            string xml = "<RenderPipelineDesc _name=\"Test\"><_listAttachment>"
                         "<RenderPassAttachment _name=\"A\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _loadOp=\"DontCare\"/>"
                         "<RenderPassAttachment _name=\"B\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _loadOp=\"DontCare\"/>"
                         "<RenderPassAttachment _name=\"D\" _format=\"D24_UNORM_S8_UINT\" _clearDepth=\"1\" _loadOp=\"Clear\"/>"
                         "</_listAttachment><_listPass><RenderGraphPassDesc _name=\"Pass\" _type=\"Compute\" ";
            xml.append( attributes );
            xml += "><_listInput>";
            xml.append( inputs );
            xml += "</_listInput><_listOutput>";
            xml.append( outputs );
            xml += "</_listOutput></RenderGraphPassDesc></_listPass></RenderPipelineDesc>";
            return xml;
        }

        static string withShader( string_view extra = "" )
        {
            string attributes = "_shaderPath=\"game/playground/shaders/compute_channelswap.hlsl\" ";
            attributes.append( kNoState );
            attributes += " ";
            attributes.append( extra );
            return attributes;
        }
    };
} // namespace

/** @brief [RenderPipelineAssetTest] 컴퓨트 패스: 셰이더 · 입력 · 출력 하나면 읽힌다. 진입점은 적은 값이다 */
SW_TEST_CASE( RenderPipelineAssetTest, ComputePassLoads )
{
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, ComputePassTestUtil::makeXML( ComputePassTestUtil::withShader( "_computeEntryPoint=\"MyCS\"" ), "<item>A</item>", "<item>B</item>" ) ) );
    const RenderGraphPassDesc& pass = asset.getGraphPass()[0];
    SW_EXPECT_TRUE( pass._type == RenderPassKind::Compute );
    SW_EXPECT_EQUAL( string( "MyCS" ), pass._computeEntryPoint );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::Compute ).hasFlag( RenderPassTraitFlag::kPipelineCompute ) );
}

/** @brief [RenderPipelineAssetTest] 컴퓨트 패스: 실행이 받아 줄 수 없는 선언은 읽기 오류다. 진입점 칸은 컴퓨트 패스만 쓴다 */
SW_TEST_CASE( RenderPipelineAssetTest, ComputePassRejectsBadDeclarations )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.PassComputeNeedsShader" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassComputeOneOutput" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassComputeTooManyInputs" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassComputeDepthInput" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassComputeInputIsOutput" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassComputeBlend" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassComputeEntryOnCompute" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    SW_EXPECT_LOG( "Renderer.PassNoColorOutput" );
    using Util = ComputePassTestUtil;
    RenderPipelineAsset asset;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::kNoState, "", "<item>B</item>" ) ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::withShader(), "", "" ) ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::withShader(), "<item>A</item><item>A</item><item>A</item>", "<item>B</item>" ) ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::withShader(), "<item>D</item>", "<item>B</item>" ) ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::withShader(), "<item>B</item>", "<item>B</item>" ) ) );
    const string blended = "_shaderPath=\"game/playground/shaders/compute_channelswap.hlsl\" _cullMode=\"None\" _bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _blendPreset=\"Transparent\"";
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( blended, "", "<item>B</item>" ) ) );
    StringBuilder<constant::buffer::kMax256> passAttributes;
    passAttributes.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" _computeEntryPoint=\"CSMain\" %#", RenderPipelineTestUtil::kOpaqueState );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( passAttributes.view() ) ) );
}

/** @brief [RenderPipelineAssetTest] 선택 스테이지: 패스 칸의 진입점을 그대로 가진다. 헐은 도메인과 함께 적고, 머티리얼 셰이더로 그리는 패스 · 컴퓨트 패스에는 쓸 수 없다 */
SW_TEST_CASE( RenderPipelineAssetTest, OptionalStagesFollowTheRules )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.PassHullNeedsDomain" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassOptionalStageNotAllowed" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    RenderPipelineAsset stages;
    SW_REQUIRE_TRUE( stages.loadFromXMLFile( AssetID{ string_view( "game/playground/pipeline/tessellationstagepipeline.xml" ) } ) );
    bool bFoundHull = false;
    for ( const RenderGraphPassDesc& pass : stages.getGraphPass() )
    {
        if ( pass._hullEntryPoint.empty() == false )
        {
            bFoundHull = true;
            SW_EXPECT_EQUAL( string( "HSMain" ), pass._hullEntryPoint );
            SW_EXPECT_EQUAL( string( "DSMain" ), pass._domainEntryPoint );
        }
    }
    SW_EXPECT_TRUE( bFoundHull );

    RenderPipelineAsset                      asset;
    StringBuilder<constant::buffer::kMax256> hullPass;
    hullPass.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" %# _hullEntryPoint=\"HSMain\"", RenderPipelineTestUtil::kOpaqueState );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( hullPass.view() ) ) ); // 도메인이 없고 머티리얼 패스다
    StringBuilder<constant::buffer::kMax256> geometryPass;
    geometryPass.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" %# _geometryEntryPoint=\"GSMain\"", RenderPipelineTestUtil::kOpaqueState );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( geometryPass.view() ) ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, ComputePassTestUtil::makeXML( ComputePassTestUtil::withShader( "_geometryEntryPoint=\"GSMain\"" ), "", "<item>B</item>" ) ) );
}

/** @brief [RenderPipelineAssetTest] 엔진 파이프라인이 모두 렌더 패스를 합쳐 검증을 통과한다. 디퍼드 반투명은 섞는다 */
SW_TEST_CASE( RenderPipelineAssetTest, ShippedPipelinesLoad )
{
    for ( const utf8* pPath : { "engine/pipeline/forwardpipeline.xml", "engine/pipeline/forwardpipelinestaged.xml", "engine/pipeline/forwardprepasspipeline.xml", "engine/pipeline/forwardtoonpipeline.xml",
                                "engine/pipeline/forward2dpipeline.xml", "engine/pipeline/deferredpipeline.xml", "game/playground/pipeline/computepasspipeline.xml", "game/playground/pipeline/extensionpasspipeline.xml" } )
    {
        RenderPipelineAsset asset;
        SW_EXPECT_TRUE_MSG( asset.loadFromXMLFile( AssetID{ string_view( pPath ) } ), pPath );
    }
    RenderPipelineAsset deferred;
    SW_REQUIRE_TRUE( deferred.loadFromXMLFile( AssetID{ string_view( "engine/pipeline/deferredpipeline.xml" ) } ) );
    for ( const RenderGraphPassDesc& pass : deferred.getGraphPass() )
    {
        if ( pass._type == RenderPassKind::Transparent )
            SW_EXPECT_TRUE( pass._resolvedRenderState.isAnyBlendEnabled() );
        // 디퍼드 조명은 G버퍼 패스가 장면 색에 먼저 적은 자체 발광 위에 더한다(G버퍼 명세 판 3).
        if ( pass._type == RenderPassKind::Lighting )
            SW_EXPECT_TRUE( pass._resolvedRenderState.isAnyBlendEnabled() );
    }
}

/** @brief [RenderPipelineAssetTest] 부모를 가리키는 파이프라인은 부모 패스 위에 차이만 얹어 읽힌다(forwardtoon 은 forward 에서 피사계 심도를 지우고 톤맵 define 을 비운다) */
SW_TEST_CASE( RenderPipelineAssetTest, ChildPipelineMergesItsBase )
{
    RenderPipelineAsset toon;
    SW_REQUIRE_TRUE( toon.loadFromXMLFile( AssetID{ "engine/pipeline/forwardtoonpipeline.xml" } ) );
    SW_EXPECT_EQUAL( string( "ForwardToonPipeline" ), toon.getDesc()._name );
    const vector<RenderGraphPassDesc>& listPass = toon.getGraphPass();
    SW_EXPECT_FALSE( VectorUtil::containsIf( listPass, []( const RenderGraphPassDesc& pass )
    { return pass._name == "DepthOfField"; } ) );
    const RenderGraphPassDesc* pPresent = VectorUtil::findIf( listPass, []( const RenderGraphPassDesc& pass )
    { return pass._name == "Present"; } );
    SW_REQUIRE_TRUE( pPresent != nullptr );
    SW_EXPECT_TRUE( pPresent->_listPermutation.empty() );
    SW_EXPECT_EQUAL( string( "engine/shaders/post_chain.hlsl" ), pPresent->_shaderPath ); // 적지 않은 칸은 부모 것
}

/** @brief [RenderPipelineAssetTest] 쿠킹이 펼친 글은 원본을 합쳐 읽은 것과 같은 값이고, 부모를 가리키지 않는다(팩 쿠커의 펼치기와 같은 함수) */
SW_TEST_CASE( RenderPipelineAssetTest, CookedFlatTextReadsLikeTheSource )
{
    const AbsolutePath& contentRoot = engine::getSubsystem<AssetPaths>().getRootFolderPath();
    const TypeInfo&     typeInfo    = *RenderPipelineDesc::StaticType();
    for ( const utf8* pID : { "engine/pipeline/forwardtoonpipeline.xml", "engine/pipeline/forwardprepasspipeline.xml", "engine/pipeline/forward2dpipeline.xml", "engine/pipeline/forwardpipelinestaged.xml",
                              "game/playground/pipeline/extensionpasspipeline.xml", "game/playground/pipeline/geometrystagepipeline.xml", "game/playground/pipeline/computepasspipeline.xml" } )
    {
        string sourceText;
        SW_REQUIRE_TRUE( AssetIO::readTextAsset( AssetID{ pID }, sourceText ) );
        SW_EXPECT_FALSE( DataInheritance::findBasePath( sourceText ).empty() );

        DataInheritanceDiskSource source{ contentRoot, 8u }; // 설정(`EngineConfig.json`)과 같은 깊이 상한
        string                    flatText;
        bool                      bComposed = false;
        SW_REQUIRE_TRUE( ResourceComposer::compose( sourceText, pID, ResourceContext::forPackCook( source ), flatText, bComposed ) );
        SW_EXPECT_TRUE( bComposed );
        SW_EXPECT_TRUE( DataInheritance::findBasePath( flatText ).empty() );

        RenderPipelineAsset cooked;
        RenderPipelineAsset merged;
        const AbsolutePath  flatPath = test::makeScratchPath( "flat.xml" );
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( flatPath, flatText ) );
        SW_REQUIRE_TRUE( cooked.loadFromDiskFile( flatPath.str() ) );
        SW_REQUIRE_TRUE( merged.loadFromXMLFile( AssetID{ pID } ) );
        SW_EXPECT_EQUAL( XMLSerializer::serialize( &merged.getDesc(), typeInfo ), XMLSerializer::serialize( &cooked.getDesc(), typeInfo ) );
    }
}

/** @brief [RenderPipelineAssetTest] 자식으로 저장한 파일은 부모와 다른 칸만 적고, 다시 읽으면 같은 값이다. 기본값을 생략한 저장도 왕복한다 */
SW_TEST_CASE( RenderPipelineAssetTest, SavedChildRoundTrips )
{
    const TypeInfo&    typeInfo = *RenderPipelineDesc::StaticType();
    RenderPipelineDesc source;
    SW_REQUIRE_TRUE( ReflectedXMLFile::loadDesc( AssetID{ "engine/pipeline/forwardtoonpipeline.xml" }, source ) );
    source._listPass[0]._cullMode = CullMode::Front;

    const AbsolutePath           childPath = test::makeScratchPath( "child.xml" );
    const optional<AbsolutePath> childFile = FileUtil::requireAbsolutePath( childPath.str() );
    SW_REQUIRE_TRUE( childFile.has_value() );
    SW_REQUIRE_TRUE( ReflectedXMLFile::saveDescAsChild( *childFile, source, AssetID{ "engine/pipeline/forwardpipeline.xml" } ) );
    string childText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( childPath, childText ) );
    SW_EXPECT_EQUAL( string( "engine/pipeline/forwardpipeline.xml" ), DataInheritance::findBasePath( childText ) );
    SW_EXPECT_TRUE( childText.find( "Canvas" ) == string::npos ); // 같은 패스는 적지 않는다

    RenderPipelineDesc rereadChild;
    SW_REQUIRE_TRUE( ReflectedXMLFile::loadDescFromDiskFile( childPath.str(), rereadChild ) );
    SW_EXPECT_EQUAL( XMLSerializer::serialize( &source, typeInfo ), XMLSerializer::serialize( &rereadChild, typeInfo ) );

    const AbsolutePath           fullPath = test::makeScratchPath( "full.xml" );
    const optional<AbsolutePath> fullFile = FileUtil::requireAbsolutePath( fullPath.str() );
    SW_REQUIRE_TRUE( fullFile.has_value() );
    SW_REQUIRE_TRUE( ReflectedXMLFile::saveDesc( *fullFile, source ) );
    RenderPipelineDesc rereadFull;
    SW_REQUIRE_TRUE( ReflectedXMLFile::loadDescFromDiskFile( fullPath.str(), rereadFull ) );
    SW_EXPECT_EQUAL( XMLSerializer::serialize( &source, typeInfo ), XMLSerializer::serialize( &rereadFull, typeInfo ) );
}

/** @brief [RenderPipelineAssetTest] 문자열이던 칸은 enum 이다 — 오타는 읽기 오류이고 받는 철자는 enum 하나에서 나온다 */
SW_TEST_CASE( RenderPipelineAssetTest, MistypedEnumsAreLoadErrors )
{
    SW_EXPECT_LOG( "Serialization.UnknownEnumerator" );
    SW_EXPECT_LOG( "Serialization.OrphanValue" );
    RenderPipelineAsset                      asset;
    StringBuilder<constant::buffer::kMax256> unknownTypePass;
    unknownTypePass.appendFormat( "_type=\"ForwardOpaqe\" _depthAttachment=\"SceneDepth\" %#", RenderPipelineTestUtil::kOpaqueState );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( unknownTypePass.view() ) ) );
    StringBuilder<constant::buffer::kMax256> opaquePass;
    opaquePass.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" %#", RenderPipelineTestUtil::kOpaqueState );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( opaquePass.view(), "", "",
                                                                                           "<RenderPassAttachment _name=\"SceneDepth\" _format=\"D24_UNORM\"/>" ) ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"true\" _bEnableDepthWrite=\"true\" _cullMode=\"Back\" _depthCompare=\"Lesser\"" ) );
}

/** @brief [RenderPipelineAssetTest] 블렌드 칸: 프리셋 · 타깃별 값이 PSO 값이 되고, 없는 프리셋 · 함께 적기 · 수가 맞지 않는 타깃 · 알파 자리의 색 계수는 읽기 오류다 */
SW_TEST_CASE( RenderPipelineAssetTest, BlendDeclarationsFollowTheRules )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.PassBlendPresetUnknown" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassBlendPresetAndTargets" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassBlendTargetCount" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassBlendTargetInvalid" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthWrite=\"false\" _blendPreset=\"Additive\"" ) );
    const BlendTargetState& additive = asset.getGraphPass()[0]._resolvedRenderState._arrBlendTarget[3];
    SW_EXPECT_TRUE( additive._dstColor == BlendFactor::One );
    SW_EXPECT_TRUE( additive._bEnableBlend == SW_TRUE );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_blendPreset=\"Plasma\"" ) );

    const auto makeWithTargets = []( string_view passExtra, string_view targets )
    {
        StringBuilder<constant::buffer::kMax256> passAttributes;
        passAttributes.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" %#", passExtra );
        string                                   xml     = RenderPipelineTestUtil::makeXML( passAttributes.view() );
        const utf8* const                        pMarker = "<_listPermutation/>";
        StringBuilder<constant::buffer::kMax512> blendTargets;
        blendTargets.appendFormat( "<_listBlendTarget>%#</_listBlendTarget>", targets );
        xml.insert( xml.find( pMarker ), blendTargets.c_str(), blendTargets.size() );
        return xml;
    };
    constexpr string_view kTarget = "<BlendTargetState _bEnableBlend=\"true\" _srcColor=\"One\" _dstColor=\"One\" _colorWriteMask=\"RGB\"/>";
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, makeWithTargets( "", kTarget ) ) );
    SW_EXPECT_TRUE( asset.getGraphPass()[0]._resolvedRenderState._arrBlendTarget[2]._colorWriteMask == ColorWriteMask::RGB ); // 하나면 모든 타깃이다
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, makeWithTargets( "_blendPreset=\"Opaque\"", kTarget ) ) );
    StringBuilder<constant::buffer::kMax256> twoTargets;
    twoTargets.appendFormat( "%#%#", kTarget, kTarget );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, makeWithTargets( "", twoTargets.view() ) ) ); // 컬러 출력은 하나다
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, makeWithTargets( "", "<BlendTargetState _bEnableBlend=\"true\" _srcAlpha=\"SrcColor\"/>" ) ) );
}

/** @brief [RenderPipelineAssetTest] 스텐실은 깊이 첨부가 있어야 하고, 스텐실 칸은 PSO 값까지 간다 */
SW_TEST_CASE( RenderPipelineAssetTest, StencilNeedsTheDepthAttachment )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.PassRenderStateInvalid" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableStencil=\"true\" _stencilRef=\"7\" _stencilFrontCompare=\"Equal\" _stencilFrontPass=\"Replace\"" ) );
    RHIPipelineStateDesc desc{};
    FramePSOManager::applyPassRenderState( desc, RenderPassKind::ForwardOpaque, &asset.getGraphPass()[0] );
    SW_EXPECT_EQUAL( 7, static_cast<int32>( desc._renderState._stencilRef ) );
    SW_EXPECT_TRUE( desc._renderState._stencilFrontCompare == CompareFunc::Equal );
    SW_EXPECT_TRUE( desc._renderState._stencilFrontPass == StencilOp::Replace );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _bEnableStencil=\"true\"", "ForwardOpaque", false ) );
}

/** @brief [RenderPipelineAssetTest] 첨부의 로드 · 저장 동작 · 초기값을 읽고, 저장하지 않는 첨부를 뒤 패스가 쓰면 읽기 오류다 */
SW_TEST_CASE( RenderPipelineAssetTest, DiscardedAttachmentCannotBeRead )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.AttachmentDiscardedButUsed" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    using Util = RenderPipelineTestUtil;
    StringBuilder<constant::buffer::kMax256> passAttributesBuilder;
    passAttributesBuilder.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" %#", Util::kOpaqueState );
    const string_view   passAttributes = passAttributesBuilder.view();
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( Util::load( asset, Util::makeXML( passAttributes, "", "", "<RenderPassAttachment _name=\"SceneDepth\" _format=\"D24_UNORM_S8_UINT\" _clearDepth=\"0.5\" _clearStencil=\"3\" _storeOp=\"DontCare\"/>" ) ) );
    const RHIAttachmentOps ops = asset.getDesc()._listAttachment[1].getOps();
    SW_EXPECT_NEAR( 0.5f, ops._clearDepth, 1e-6f );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( ops._clearStencil ) );
    SW_EXPECT_TRUE( ops._storeOp == RHIRenderPassStoreOp::DontCare );

    // 같은 첨부를 여는 패스가 둘이면 뒤 패스가 정의되지 않은 값을 읽는다.
    string                                   xml = Util::makeXML( passAttributes, "", "", "<RenderPassAttachment _name=\"SceneDepth\" _format=\"D24_UNORM_S8_UINT\" _storeOp=\"DontCare\"/>" );
    StringBuilder<constant::buffer::kMax512> secondPass;
    secondPass.appendFormat( "<RenderGraphPassDesc _name=\"Second\" %#><_listOutput><item>SceneColor</item></_listOutput></RenderGraphPassDesc>", passAttributes );
    xml.insert( xml.find( "</_listPass>" ), secondPass.c_str(), secondPass.size() );
    SW_EXPECT_FALSE( Util::load( asset, xml ) );
}

namespace
{
    struct VelocityTestUtil
    {
        /** @brief 패스 하나가 @p outputs 를 컬러 · 깊이 출력으로 쓰는 파이프라인입니다. 속도 첨부의 포맷은 @p velocityFormat 입니다. */
        static string makeXML( string_view passType, string_view outputs, string_view velocityFormat = "R16G16_FLOAT", string_view passState = RenderPipelineTestUtil::kOpaqueState )
        {
            string xml = "<RenderPipelineDesc _name=\"Velocity\"><_listAttachment>"
                         "<RenderPassAttachment _name=\"SceneColor\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _loadOp=\"Clear\"/>"
                         "<RenderPassAttachment _name=\"SceneVelocity\" _format=\"";
            xml.append( velocityFormat );
            xml += "\" _clearColor=\"0,0,0,0\" _loadOp=\"Clear\"/>"
                   "<RenderPassAttachment _name=\"SceneDepth\" _format=\"D24_UNORM_S8_UINT\" _clearDepth=\"1\" _loadOp=\"Clear\"/>"
                   "</_listAttachment><_listPass><RenderGraphPassDesc _name=\"Pass\" _type=\"";
            xml.append( passType );
            xml += "\" _depthAttachment=\"SceneDepth\" ";
            xml.append( passState );
            xml += "><_listOutput>";
            xml.append( outputs );
            xml += "</_listOutput></RenderGraphPassDesc></_listPass></RenderPipelineDesc>";
            return xml;
        }
    };
} // namespace

/** @brief [RenderPipelineAssetTest] 화면 속도 출력은 데이터가 정한다 — 선언한 불투명 패스만 속도 define 을 받고, 내부 슬롯은 대신 그리는 패스의 선언을 따른다 */
SW_TEST_CASE( RenderPipelineAssetTest, VelocityOutputIsDeclaredByThePipeline )
{
    const string        kVelocityDefine{ kPassVelocityDefine };
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, VelocityTestUtil::makeXML( "ForwardOpaque", "<item>SceneColor</item><item>SceneVelocity</item><item>SceneDepth</item>" ) ) );
    const RenderGraphPassDesc& pass = asset.getGraphPass()[0];
    SW_EXPECT_TRUE( passWritesVelocity( RenderPassKind::ForwardOpaque, &pass ) );
    SW_EXPECT_TRUE( passWritesVelocity( RenderPassKind::ForwardOpaqueNoDepthWrite, &pass ) ); // 내부 슬롯은 ForwardOpaque 의 선언을 본다
    SW_EXPECT_FALSE( passWritesVelocity( RenderPassKind::Transparent, &pass ) );              // 반투명은 속도를 쓰지 않는다
    SW_EXPECT_FALSE( passWritesVelocity( RenderPassKind::ForwardOpaque, nullptr ) );
    const EngineDefaultAssets& assets = engine::getSubsystem<EngineDefaultAssets>();
    SW_EXPECT_TRUE( VectorUtil::contains( selectRenderPassShader( RenderPassKind::ForwardOpaque, &pass, &pass, assets )._listDefine, kVelocityDefine ) );
    SW_EXPECT_TRUE( VectorUtil::contains( selectRenderPassShader( RenderPassKind::ForwardOpaqueNoDepthWrite, nullptr, &pass, assets )._listDefine, kVelocityDefine ) );
    SW_EXPECT_FALSE( VectorUtil::contains( selectRenderPassShader( RenderPassKind::ForwardOpaqueNoDepthWrite, nullptr, nullptr, assets )._listDefine, kVelocityDefine ) );

    // 속도를 선언하지 않은 파이프라인은 첨부도 define 도 없다.
    RenderPipelineAsset withoutVelocity;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( withoutVelocity, VelocityTestUtil::makeXML( "ForwardOpaque", "<item>SceneColor</item><item>SceneDepth</item>" ) ) );
    SW_EXPECT_FALSE( passWritesVelocity( RenderPassKind::ForwardOpaque, &withoutVelocity.getGraphPass()[0] ) );
}

/** @brief [RenderPipelineAssetTest] 화면 속도 출력은 깊이를 쓰는 불투명 패스의 둘째 컬러 출력이고 명세의 포맷이다 — 어기면 읽기 오류다 */
SW_TEST_CASE( RenderPipelineAssetTest, VelocityOutputFollowsTheRules )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.VelocityWriterType" );
    SW_EXPECT_LOG( "RenderPipelineAsset.VelocityFormat" );
    SW_EXPECT_LOG( "RenderPipelineAsset.VelocityOutputSlot" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    RenderPipelineAsset asset;
    // 반투명은 깊이를 쓰지 않는다 — 속도가 깊이와 짝이 아니다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, VelocityTestUtil::makeXML( "Transparent", "<item>SceneColor</item><item>SceneVelocity</item>", "R16G16_FLOAT",
                                                                                     "_bEnableDepthTest=\"true\" _bEnableDepthWrite=\"false\" _cullMode=\"Back\" _blendPreset=\"Transparent\"" ) ) );
    // 포맷이 명세(R16G16 실수)와 다르다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, VelocityTestUtil::makeXML( "ForwardOpaque", "<item>SceneColor</item><item>SceneVelocity</item><item>SceneDepth</item>", "R16G16B16A16_FLOAT" ) ) );
    // 셰이더는 속도를 SV_TARGET1 에 쓴다 — 첫 출력이면 안 된다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, VelocityTestUtil::makeXML( "ForwardOpaque", "<item>SceneVelocity</item><item>SceneColor</item><item>SceneDepth</item>" ) ) );
}

/** @brief [RenderPipelineAssetTest] 엔진 파이프라인의 불투명 패스는 화면 속도를 쓰고 TAA · 모션 블러가 그것을 읽는다(2D 는 모션 블러가 없어 쓰지 않는다) */
SW_TEST_CASE( RenderPipelineAssetTest, ShippedPipelinesWireVelocity )
{
    struct PipelineCase
    {
        const utf8* _pPath;
        bool        _bWritesVelocity;
    };
    for ( const PipelineCase& expectation : {
              PipelineCase{   "engine/pipeline/deferredpipeline.xml",  true},
              PipelineCase{    "engine/pipeline/forwardpipeline.xml",  true},
              PipelineCase{"engine/pipeline/forwardtoonpipeline.xml",  true},
              PipelineCase{  "engine/pipeline/forward2dpipeline.xml", false}
    } )
    {
        RenderPipelineAsset asset;
        SW_REQUIRE_TRUE_MSG( asset.loadFromXMLFile( AssetID{ string_view( expectation._pPath ) } ), expectation._pPath );
        bool bAnyWriter = false;
        for ( const RenderGraphPassDesc& pass : asset.getGraphPass() )
        {
            bAnyWriter         = bAnyWriter || passWritesVelocity( pass._type, &pass );
            const bool bReader = pass._type == RenderPassKind::TAA || pass._type == RenderPassKind::MotionBlur;
            if ( bReader == false )
                continue;
            const bool bReadsVelocity = VectorUtil::containsIf( pass._listResolvedInput, []( const RenderGraphPassDesc::ResolvedAttachment& input )
            { return static_cast<RenderPassInputRole>( input._role ) == RenderPassInputRole::Velocity; } );
            SW_EXPECT_TRUE_MSG( bReadsVelocity, pass._name.c_str() );
        }
        SW_EXPECT_TRUE_MSG( bAnyWriter == expectation._bWritesVelocity, expectation._pPath );
    }
}

/** @brief [RenderPipelineAssetTest] 화면 왜곡 누적은 왜곡 머티리얼(MATERIAL_DISTORTION)의 반투명 배치만 왜곡 퍼뮤테이션으로 그리고, 적용은 사본 · 왜곡 · 깊이를 읽는다 */
SW_TEST_CASE( RenderPipelineAssetTest, DistortionPassTypesFollowTheTable )
{
    SW_EXPECT_TRUE( isPipelinePassType( RenderPassKind::Distortion ) );
    SW_EXPECT_TRUE( isPipelinePassType( RenderPassKind::DistortionApply ) );
    const RenderPassKindInfo& accumulate = getRenderPassKindInfo( RenderPassKind::Distortion );
    SW_EXPECT_TRUE( accumulate.hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch ) );
    SW_EXPECT_FALSE( accumulate.hasFlag( RenderPassTraitFlag::kDepthWrite ) );
    const vector<string> listDistortion{ string( kMaterialDistortionDefine ), string( "MATERIAL_BLEND_TRANSLUCENT" ) };
    const vector<string> listPlain{ string( "MATERIAL_BLEND_TRANSLUCENT" ) };
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::Distortion ).shouldDrawMaterial( &listDistortion ) );
    SW_EXPECT_FALSE( getRenderPassKindInfo( RenderPassKind::Distortion ).shouldDrawMaterial( &listPlain ) );
    SW_EXPECT_FALSE( getRenderPassKindInfo( RenderPassKind::Distortion ).shouldDrawMaterial( nullptr ) ); // 머티리얼 없는 배치는 왜곡이 없다
    const EngineDefaultAssets& assets = engine::getSubsystem<EngineDefaultAssets>();
    SW_EXPECT_TRUE( VectorUtil::contains( selectRenderPassShader( RenderPassKind::Distortion, nullptr, nullptr, assets )._listDefine, string( kPassDistortionDefine ) ) );
    const RenderPassInputSignature* pApply = findRenderPassInputSignature( RenderPassKind::DistortionApply );
    SW_REQUIRE_TRUE( pApply != nullptr );
    SW_EXPECT_TRUE( pApply->hasRole( RenderPassInputRole::SceneColorCopy ) );
    SW_EXPECT_TRUE( pApply->hasRole( RenderPassInputRole::Distortion ) );
    SW_EXPECT_TRUE( pApply->hasRole( RenderPassInputRole::SceneDepth ) );
    SW_EXPECT_TRUE( resolveRenderPassInputRole( "SceneDistortion", false, "" ) == RenderPassInputRole::Distortion );
}

/** @brief [RenderPipelineAssetTest] 엔진 포워드 · 디퍼드는 반투명 앞에서 왜곡을 누적하고 적용한다 — 적용 패스는 누적 패스 뒤, 반투명 앞이고 장면 색에 쓴다 */
SW_TEST_CASE( RenderPipelineAssetTest, ShippedPipelinesWireDistortion )
{
    for ( const utf8* pPath : { "engine/pipeline/forwardpipeline.xml", "engine/pipeline/deferredpipeline.xml" } )
    {
        RenderPipelineAsset asset;
        SW_REQUIRE_TRUE_MSG( asset.loadFromXMLFile( AssetID{ string_view( pPath ) } ), pPath );
        const vector<RenderGraphPassDesc>& listPass         = asset.getGraphPass();
        size_t                             accumulateIndex  = listPass.size();
        size_t                             applyIndex       = listPass.size();
        size_t                             transparentIndex = listPass.size();
        for ( size_t index = 0; index < listPass.size(); ++index )
        {
            accumulateIndex  = listPass[index]._type == RenderPassKind::Distortion ? index : accumulateIndex;
            applyIndex       = listPass[index]._type == RenderPassKind::DistortionApply ? index : applyIndex;
            transparentIndex = listPass[index]._type == RenderPassKind::Transparent ? index : transparentIndex;
        }
        SW_REQUIRE_TRUE_MSG( accumulateIndex < applyIndex && applyIndex < transparentIndex, pPath );
        const RenderGraphPassDesc& accumulate = listPass[accumulateIndex];
        SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( accumulate._listResolvedColorOutput.size() ) );
        SW_EXPECT_TRUE( static_cast<RenderPassInputRole>( accumulate._listResolvedColorOutput[0]._role ) == RenderPassInputRole::Distortion );
        SW_EXPECT_TRUE( accumulate._resolvedRenderState.isAnyBlendEnabled() ); // 겹친 왜곡이 합쳐진다(가산)
    }
}

/** @brief [RenderPipelineAssetTest] 왜곡 적용이 왜곡 입력을 적지 않으면 읽기 오류다(입력 계약) */
SW_TEST_CASE( RenderPipelineAssetTest, DistortionApplyNeedsItsInputs )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.PassRequiredInputRole" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    const string        xml = "<RenderPipelineDesc _name=\"DistortionApply\"><_listAttachment>"
                              "<RenderPassAttachment _name=\"SceneColor\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _loadOp=\"Clear\"/>"
                              "<RenderPassAttachment _name=\"SceneColorCopy\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _loadOp=\"Clear\"/>"
                              "<RenderPassAttachment _name=\"SceneDepth\" _format=\"D24_UNORM_S8_UINT\" _clearDepth=\"1\" _loadOp=\"Clear\"/>"
                              "</_listAttachment><_listPass>"
                              "<RenderGraphPassDesc _name=\"Apply\" _type=\"DistortionApply\" _cullMode=\"None\" _bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\">"
                              "<_listInput><item>SceneColorCopy</item><item>SceneDepth</item></_listInput><_listOutput><item>SceneColor</item></_listOutput></RenderGraphPassDesc>"
                              "</_listPass></RenderPipelineDesc>";
    RenderPipelineAsset asset;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, xml ) );
}

namespace
{
    struct PostMaterialTestUtil
    {
        /** @brief HDR 원본 → 톤맵 → 후처리 머티리얼 패스 @p passes 를 가진 파이프라인 XML 입니다(패스는 목록 안에 그대로 넣는다). */
        static string makeXML( string_view passes )
        {
            string xml = "<RenderPipelineDesc _name=\"PostMaterial\"><_listAttachment>"
                         "<RenderPassAttachment _name=\"HDR\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _loadOp=\"Clear\"/>"
                         "<RenderPassAttachment _name=\"HDRPost\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _loadOp=\"Clear\"/>"
                         "<RenderPassAttachment _name=\"HDRAgain\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _loadOp=\"Clear\"/>"
                         "<RenderPassAttachment _name=\"LDR\" _format=\"R8G8B8A8_UNORM\" _clearColor=\"0,0,0,1\" _loadOp=\"Clear\"/>"
                         "</_listAttachment><_listPass>";
            xml.append( passes );
            xml.append( "</_listPass></RenderPipelineDesc>" );
            return xml;
        }

        /** @brief 패스 하나입니다. @p extra 는 패스 칸(자리 · 셰이더)이다. */
        static string makePass( string_view name, string_view type, string_view extra, string_view input, string_view output )
        {
            string pass = "<RenderGraphPassDesc _name=\"";
            pass.append( name ).append( "\" _type=\"" ).append( type ).append( "\" " ).append( extra );
            pass.append( " _cullMode=\"None\" _bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\"><_listInput><item>" ).append( input );
            pass.append( "</item></_listInput><_listOutput><item>" ).append( output ).append( "</item></_listOutput></RenderGraphPassDesc>" );
            return pass;
        }
    };
} // namespace

/** @brief [RenderPipelineAssetTest] 후처리 머티리얼 패스는 자리를 하나 적고, 자리마다 하나이며, 톤맵 대신 자리는 앞의 패스가 쓴 출력 위에 그린다 — 엔진 디퍼드는 세 자리를 모두 둔다 */
SW_TEST_CASE( RenderPipelineAssetTest, PostProcessMaterialPassesNameOneLocation )
{
    using Util = PostMaterialTestUtil;
    SW_EXPECT_LOG( "RenderPipelineAsset.PassBlendableLocation" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassPostMaterialLocationTwice" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassPostMaterialReplacingBase" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    const string        tonemap = Util::makePass( "Tonemap", "Tonemap", "", "HDRPost", "LDR" );
    RenderPipelineAsset asset;
    // 톤맵 앞 자리 하나 — 읽힌다.
    const string                              before = Util::makePass( "Before", "PostProcessMaterial", "_blendableLocation=\"BeforeTonemap\"", "HDR", "HDRPost" );
    StringBuilder<constant::buffer::kMax1024> passes;
    passes.appendFormat( "%#%#", before, tonemap );
    SW_EXPECT_TRUE( RenderPipelineTestUtil::load( asset, Util::makeXML( passes.view() ) ) );
    // 자리를 적지 않았다.
    passes.clear();
    passes.appendFormat( "%#%#", Util::makePass( "Before", "PostProcessMaterial", "", "HDR", "HDRPost" ), tonemap );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( passes.view() ) ) );
    // 같은 자리 둘.
    passes.clear();
    passes.appendFormat( "%#%#%#", before, tonemap, Util::makePass( "Again", "PostProcessMaterial", "_blendableLocation=\"BeforeTonemap\"", "HDR", "HDRAgain" ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( passes.view() ) ) );
    // 톤맵 대신 자리가 톤맵보다 앞이다 — 덮을 출력이 아직 없다.
    passes.clear();
    passes.appendFormat( "%#%#", Util::makePass( "Replace", "PostProcessMaterial", "_blendableLocation=\"ReplacingTonemap\"", "HDRPost", "LDR" ),
                         Util::makePass( "Tone", "Tonemap", "", "HDR", "HDRPost" ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( passes.view() ) ) );

    RenderPipelineAsset deferred;
    SW_REQUIRE_TRUE( deferred.loadFromXMLFile( AssetID{ string_view( "engine/pipeline/deferredpipeline.xml" ) } ) );
    uint32 locationMask = 0;
    for ( const RenderGraphPassDesc& pass : deferred.getGraphPass() )
    {
        if ( pass._type == RenderPassKind::PostProcessMaterial )
            locationMask |= 1u << static_cast<uint32>( pass._blendableLocation );
    }
    const uint32 expected = ( 1u << static_cast<uint32>( PostProcessBlendableLocation::BeforeTonemap ) ) | ( 1u << static_cast<uint32>( PostProcessBlendableLocation::AfterTonemap ) ) |
                            ( 1u << static_cast<uint32>( PostProcessBlendableLocation::ReplacingTonemap ) );
    SW_EXPECT_EQUAL( expected, locationMask );
}

/** @brief [RenderPipelineAssetTest] 데칼 패스는 DBufferA · B · C 에만 쓴다(차례 그대로) — 엔진 디퍼드 · 포워드 프리패스는 깊이 프리패스 뒤 · 베이스 패스 앞에 두고 베이스 패스가 셋을 읽는다 */
SW_TEST_CASE( RenderPipelineAssetTest, DecalPassWritesTheDBuffer )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.PassBlendOff" ); // 틀린 파이프라인은 블렌드도 꺼 두었다 — 경고가 함께 난다
    SW_EXPECT_LOG( "RenderPipelineAsset.PassDecalOutputs" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    const string        xml = "<RenderPipelineDesc _name=\"Decal\"><_listAttachment>"
                              "<RenderPassAttachment _name=\"SceneColor\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _loadOp=\"Clear\"/>"
                              "<RenderPassAttachment _name=\"SceneDepth\" _format=\"D24_UNORM_S8_UINT\" _clearDepth=\"1\" _loadOp=\"Clear\"/>"
                              "</_listAttachment><_listPass>"
                              "<RenderGraphPassDesc _name=\"Decal\" _type=\"Decal\" _cullMode=\"Front\" _bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\">"
                              "<_listInput><item>SceneDepth</item></_listInput><_listOutput><item>SceneColor</item></_listOutput></RenderGraphPassDesc>"
                              "</_listPass></RenderPipelineDesc>";
    RenderPipelineAsset asset;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, xml ) );

    const RenderPassKind arrBasePass[] = { RenderPassKind::GBuffer, RenderPassKind::ForwardOpaque };
    const string_view    arrPath[]     = { "engine/pipeline/deferredpipeline.xml", "engine/pipeline/forwardprepasspipeline.xml" };
    for ( size_t pipelineIndex = 0; pipelineIndex < SW_COUNT_OF( arrPath ); ++pipelineIndex )
    {
        RenderPipelineAsset engine;
        SW_REQUIRE_TRUE( engine.loadFromXMLFile( AssetID{ arrPath[pipelineIndex] } ) );
        const vector<RenderGraphPassDesc>& listPass     = engine.getGraphPass();
        size_t                             prepassIndex = listPass.size();
        size_t                             decalIndex   = listPass.size();
        size_t                             baseIndex    = listPass.size();
        for ( size_t index = 0; index < listPass.size(); ++index )
        {
            prepassIndex = listPass[index]._type == RenderPassKind::DepthPrepass ? index : prepassIndex;
            decalIndex   = listPass[index]._type == RenderPassKind::Decal ? index : decalIndex;
            baseIndex    = listPass[index]._type == arrBasePass[pipelineIndex] ? index : baseIndex;
        }
        SW_REQUIRE_TRUE( prepassIndex < decalIndex && decalIndex < baseIndex && baseIndex < listPass.size() );
        SW_EXPECT_TRUE( listPass[decalIndex]._resolvedRenderState.isAnyBlendEnabled() );
        size_t dbufferReadCount = 0;
        for ( const RenderGraphPassDesc::ResolvedAttachment& input : listPass[baseIndex]._listResolvedInput )
        {
            dbufferReadCount += ( input._role >= static_cast<uint8>( RenderPassInputRole::DBufferA ) && input._role <= static_cast<uint8>( RenderPassInputRole::DBufferC ) ) ? 1u : 0u;
        }
        SW_EXPECT_EQUAL( size_t( 3 ), dbufferReadCount );
    }
}

/**
 * @brief [RenderPipelineAssetTest] 2 단계 가림의 뒤 단계(`LateOcclusion`)는 엔진 디퍼드 · 포워드 프리패스에서 깊이 프리패스 뒤 · 베이스 패스 앞의 뷰 패스이고 XML 은 자리만 적는다
 * @details 쓰는 자원(장면 깊이 · 깊이 사본 · HZB · 컬링 버퍼)은 종류가 선언한다(`FramePassExecutor::appendEngineResourceAccesses`) — XML 입력 · 출력 · 셰이더가 비어 있어야 한다.
 *          결함이면 뒤 단계가 깊이 프리패스 몸통 안에 숨어 선언 없이 컬링 버퍼를 옮기던 모양으로 돌아가거나, 베이스 패스 뒤에 와 더한 인스턴스가 그 프레임에 빠진다.
 *          변이: XML 에서 패스를 지우면 찾기 단언이, 베이스 패스 뒤로 옮기면 차례 단언이 진다.
 */
SW_TEST_CASE( RenderPipelineAssetTest, LateOcclusionSitsBetweenDepthPrepassAndBasePass )
{
    SW_EXPECT_TRUE( isViewPassType( RenderPassKind::LateOcclusion ) );
    SW_EXPECT_FALSE( isFramePassType( RenderPassKind::LateOcclusion ) );
    const RenderPassKind arrBasePass[] = { RenderPassKind::GBuffer, RenderPassKind::ForwardOpaque };
    const string_view    arrPath[]     = { "engine/pipeline/deferredpipeline.xml", "engine/pipeline/forwardprepasspipeline.xml" };
    for ( size_t pipelineIndex = 0; pipelineIndex < SW_COUNT_OF( arrPath ); ++pipelineIndex )
    {
        RenderPipelineAsset engine;
        SW_REQUIRE_TRUE( engine.loadFromXMLFile( AssetID{ arrPath[pipelineIndex] } ) );
        const vector<RenderGraphPassDesc>& listPass     = engine.getGraphPass();
        size_t                             prepassIndex = listPass.size();
        size_t                             lateIndex    = listPass.size();
        size_t                             baseIndex    = listPass.size();
        for ( size_t index = 0; index < listPass.size(); ++index )
        {
            prepassIndex = listPass[index]._type == RenderPassKind::DepthPrepass ? index : prepassIndex;
            lateIndex    = listPass[index]._type == RenderPassKind::LateOcclusion ? index : lateIndex;
            baseIndex    = listPass[index]._type == arrBasePass[pipelineIndex] ? index : baseIndex;
        }
        SW_REQUIRE_TRUE( prepassIndex < lateIndex && lateIndex < baseIndex && baseIndex < listPass.size() );
        const RenderGraphPassDesc& late = listPass[lateIndex];
        SW_EXPECT_TRUE( late._listInput.empty() && late._listOutput.empty() && late._shaderPath.empty() && late._depthAttachment.empty() );
    }
}

namespace
{
    struct FrameGraphTestUtil
    {
        /** @brief 메시 패스 하나에 프레임 자원 · 프레임 패스를 더한 파이프라인 XML 입니다. @p resources · @p framePasses 는 목록 안에 그대로 넣습니다. */
        static string makeXML( string_view resources, string_view framePasses )
        {
            string extra = "<_listResource>";
            extra.append( resources );
            extra += "</_listResource><_listFramePass>";
            extra.append( framePasses );
            extra += "</_listFramePass>";
            StringBuilder<constant::buffer::kMax256> passAttributes;
            passAttributes.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" %#", RenderPipelineTestUtil::kOpaqueState );
            return RenderPipelineTestUtil::makeXML( passAttributes.view(), "", extra );
        }

        static constexpr const utf8* kResources = "<RenderGraphResourceDesc _name=\"InstanceBuffer\" _kind=\"External\"/>"
                                                  "<RenderGraphResourceDesc _name=\"CullResults\" _kind=\"External\"/>"
                                                  "<RenderGraphResourceDesc _name=\"HZB\" _kind=\"Persistent\"/>";
        static constexpr const utf8* kCullPass  = "<RenderGraphPassDesc _name=\"GPUCull\" _type=\"GPUCull\"><_listInput><item>InstanceBuffer</item><item>HZB</item></_listInput>"
                                                  "<_listOutput><item>CullResults</item></_listOutput></RenderGraphPassDesc>";
    };
} // namespace

/**
 * @brief [RenderPipelineAssetTest] 프레임 자원 · 프레임 패스를 읽는다 — 종류(Persistent · External)와 입출력이 그대로 오고, 선언한 종류만 "선언함" 이다
 */
SW_TEST_CASE( RenderPipelineAssetTest, FrameGraphDeclarationsLoad )
{
    using Util = FrameGraphTestUtil;
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::kResources, Util::kCullPass ) ) );
    SW_REQUIRE_EQUAL( size_t( 3 ), asset.getResources().size() );
    SW_EXPECT_TRUE( asset.getResources()[2]._kind == RenderGraphResourceKind::Persistent );
    SW_REQUIRE_EQUAL( size_t( 1 ), asset.getFramePass().size() );
    SW_EXPECT_TRUE( asset.getFramePass()[0]._type == RenderPassKind::GPUCull );
    SW_EXPECT_EQUAL( size_t( 2 ), asset.getFramePass()[0]._listInput.size() );
    SW_EXPECT_TRUE( asset.hasFramePass( RenderPassKind::GPUCull ) );
    SW_EXPECT_FALSE( asset.hasFramePass( RenderPassKind::LightGrid ) );
}

/**
 * @brief [RenderPipelineAssetTest] 프레임 그래프가 실행과 맞지 않으면 읽기 오류다 — 뷰 패스 종류 · 첨부 이름 입력 · 셰이더 칸 · 같은 종류 둘 · Transient 자원 · 뷰 목록의 프레임 종류
 */
SW_TEST_CASE( RenderPipelineAssetTest, FrameGraphRejectsWhatTheExecutorCannotRun )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.FramePassType" );
    SW_EXPECT_LOG( "RenderPipelineAsset.FramePassInputNotResource" );
    SW_EXPECT_LOG( "RenderPipelineAsset.FramePassShader" );
    SW_EXPECT_LOG( "RenderPipelineAsset.FramePassTypeDuplicated" );
    SW_EXPECT_LOG( "RenderPipelineAsset.FrameResourceTransient" );
    SW_EXPECT_LOG( "RenderPipelineAsset.FrameResourceIsAttachment" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassUnknownTypeNot" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    using Util = FrameGraphTestUtil;
    RenderPipelineAsset asset;
    // 뷰 패스 종류는 프레임 목록에 올 수 없다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::kResources, "<RenderGraphPassDesc _name=\"Sky\" _type=\"Sky\"/>" ) ) );
    // 입력은 선언한 자원이어야 한다(첨부 이름 SceneColor 는 자원이 아니다).
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load(
        asset, Util::makeXML( Util::kResources, "<RenderGraphPassDesc _name=\"LightGrid\" _type=\"LightGrid\"><_listInput><item>SceneColor</item></_listInput></RenderGraphPassDesc>" ) ) );
    // 셰이더는 표의 것이다 — 적으면 실행이 읽지 않는 칸이다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::kResources, "<RenderGraphPassDesc _name=\"MeshSkin\" _type=\"MeshSkin\" _shaderPath=\"engine/shaders/mesh_skin.hlsl\"/>" ) ) );
    // 같은 종류를 둘 — 컬링은 한 번에 모든 뷰를 돈다.
    StringBuilder<constant::buffer::kMax512> twoCulls;
    twoCulls.appendFormat( "%#<RenderGraphPassDesc _name=\"GPUCull2\" _type=\"GPUCull\"/>", Util::kCullPass );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::kResources, twoCulls.view() ) ) );
    // 자원은 Transient 일 수 없다(첨부의 몫).
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( "<RenderGraphResourceDesc _name=\"Scratch\" _kind=\"Transient\"/>", "" ) ) );
    // 자원 이름이 첨부와 겹친다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( "<RenderGraphResourceDesc _name=\"SceneColor\" _kind=\"External\"/>", "" ) ) );
    // 프레임 종류는 뷰 목록(_listPass)에 올 수 없다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( "_type=\"LightGrid\" _cullMode=\"None\" _bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\"" ) ) );
}

/**
 * @brief [RenderPipelineAssetTest] `_queue` — 프레임 컴퓨트 패스는 AsyncCompute 를 받고 읽은 값이 그대로 오며, 드로우 패스(깊이 히스토리)와 뷰 패스는 받지 않는다
 */
SW_TEST_CASE( RenderPipelineAssetTest, QueueAttributeIsForFrameComputePasses )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.FramePassAsyncNotCompute" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassAsyncNotFramePass" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    using Util = FrameGraphTestUtil;
    RenderPipelineAsset asset;
    const string        asyncGrid = "<RenderGraphPassDesc _name=\"LightGrid\" _type=\"LightGrid\" _queue=\"AsyncCompute\"><_listOutput><item>CullResults</item></_listOutput></RenderGraphPassDesc>";
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::kResources, asyncGrid ) ) );
    SW_EXPECT_TRUE( asset.getFramePass()[0]._queue == RHIQueueKind::AsyncCompute );
    // 깊이 히스토리는 풀스크린 드로우다 — 컴퓨트 큐는 렌더 타깃을 모른다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::kResources, "<RenderGraphPassDesc _name=\"DepthHistory\" _type=\"DepthHistory\" _queue=\"AsyncCompute\"/>" ) ) );
    // 뷰 패스는 뷰마다 다시 돌아 포크 · 조인 자리가 없다.
    StringBuilder<constant::buffer::kMax256> viewAsync;
    viewAsync.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" _queue=\"AsyncCompute\" %#", RenderPipelineTestUtil::kOpaqueState );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( viewAsync.view() ) ) );
}

/**
 * @brief [RenderPipelineAssetTest] 프레임 패스의 접근은 규칙에서 나온다 — 버퍼 입력 = SRV, 버퍼 출력 = UAV, 텍스처(`_viewable`) 출력 = 렌더 타깃, 규칙 밖은 간접 인자 · 복사 목록
 * @details 결함이면 그래프가 버퍼를 렌더 타깃으로 알거나(레벨 콜백이 버퍼 전이를 내지 않는다) 간접 인자로 읽는 버퍼를 SRV 로 옮긴다(DX12 · Vulkan 에서 지난 인자를 읽는다).
 */
SW_TEST_CASE( RenderPipelineAssetTest, FramePassAccessesFollowTheRules )
{
    RenderPipelineAsset                      asset;
    StringBuilder<constant::buffer::kMax512> resourcesBuilder;
    resourcesBuilder.appendFormat( "%#<RenderGraphResourceDesc _name=\"DepthHistory\" _kind=\"Persistent\" _viewable=\"Texture\"/>"
                                   "<RenderGraphResourceDesc _name=\"CullArgs\" _kind=\"External\"/>"
                                   "<RenderGraphResourceDesc _name=\"Readback\" _kind=\"External\"/>",
                                   FrameGraphTestUtil::kResources );
    const string_view resources = resourcesBuilder.view();
    const string      passes    = "<RenderGraphPassDesc _name=\"DepthHistory\" _type=\"DepthHistory\"><_listOutput><item>DepthHistory</item></_listOutput></RenderGraphPassDesc>"
                                  "<RenderGraphPassDesc _name=\"GPUCull\" _type=\"GPUCull\"><_listInput><item>InstanceBuffer</item><item>DepthHistory</item></_listInput>"
                                  "<_listOutput><item>CullResults</item></_listOutput><_listIndirectArgs><item>CullArgs</item></_listIndirectArgs>"
                                  "<_listCopySource><item>HZB</item></_listCopySource><_listCopyDest><item>Readback</item></_listCopyDest></RenderGraphPassDesc>";
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, FrameGraphTestUtil::makeXML( resources, passes ) ) );
    SW_EXPECT_TRUE( asset.getResources()[0]._viewable == RenderGraphViewableKind::Buffer ); // 기본은 버퍼

    const vector<RenderGraphResourceAccess> listHistory = asset.buildFramePassAccesses( asset.getFramePass()[0] );
    SW_REQUIRE_EQUAL( size_t( 1 ), listHistory.size() );
    SW_EXPECT_TRUE( listHistory[0]._access == RenderGraphAccess::RenderTarget && listHistory[0]._viewable == RenderGraphViewableKind::Texture );

    const vector<RenderGraphResourceAccess> listCull = asset.buildFramePassAccesses( asset.getFramePass()[1] );
    SW_REQUIRE_EQUAL( size_t( 6 ), listCull.size() );
    const auto findAccess = [&listCull]( const utf8* pName )
    {
        for ( const RenderGraphResourceAccess& access : listCull )
        {
            if ( access._resource == hashed_string( pName ) )
                return access;
        }
        return RenderGraphResourceAccess{};
    };
    SW_EXPECT_TRUE( findAccess( "InstanceBuffer" )._access == RenderGraphAccess::ShaderRead && findAccess( "InstanceBuffer" )._viewable == RenderGraphViewableKind::Buffer );
    // 컴퓨트 종류의 텍스처 입력은 RW 텍스처 읽기(UAV 읽기)다 — 에뮬 백엔드의 컴퓨트는 샘플 텍스처를 읽지 못한다.
    SW_EXPECT_TRUE( findAccess( "DepthHistory" )._access == RenderGraphAccess::UnorderedAccessRead && findAccess( "DepthHistory" )._viewable == RenderGraphViewableKind::Texture );
    SW_EXPECT_TRUE( findAccess( "CullResults" )._access == RenderGraphAccess::UnorderedAccess );
    SW_EXPECT_TRUE( findAccess( "CullArgs" )._access == RenderGraphAccess::IndirectArgs );
    SW_EXPECT_TRUE( findAccess( "HZB" )._access == RenderGraphAccess::CopySource );
    SW_EXPECT_TRUE( findAccess( "Readback" )._access == RenderGraphAccess::CopyDest );
}

/**
 * @brief [RenderPipelineAssetTest] 한 프레임 패스가 같은 이름을 두 접근으로 적으면 읽기 오류다 — 간접 인자는 버퍼이고, 규칙 밖 목록은 프레임 패스만 받는다
 * @details 결함이면 그래프가 그 패스를 버리거나(`Renderer.GraphDuplicateAccess`) 한 자원을 어느 상태로 둘지 정하지 못한다 — 로드에서 먼저 멈춘다.
 */
SW_TEST_CASE( RenderPipelineAssetTest, FramePassRejectsTwoAccessesOfOneName )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.FramePassDuplicateAccess" );
    SW_EXPECT_LOG( "RenderPipelineAsset.FramePassIndirectArgsNotBuffer" );
    SW_EXPECT_LOG( "RenderPipelineAsset.FramePassAccessNotResource" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassFrameAccessList" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    using Util = FrameGraphTestUtil;
    RenderPipelineAsset asset;
    // 입력과 출력에 같은 이름 — 읽고 쓰는 버퍼는 출력(UAV) 하나다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load(
        asset, Util::makeXML( Util::kResources, "<RenderGraphPassDesc _name=\"GPUCull\" _type=\"GPUCull\"><_listInput><item>HZB</item></_listInput><_listOutput><item>HZB</item></_listOutput></RenderGraphPassDesc>" ) ) );
    // 출력과 복사 원본에 같은 이름.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::kResources, "<RenderGraphPassDesc _name=\"GPUCull\" _type=\"GPUCull\"><_listOutput><item>CullResults</item></_listOutput>"
                                                                                           "<_listCopySource><item>CullResults</item></_listCopySource></RenderGraphPassDesc>" ) ) );
    // 간접 인자는 버퍼다.
    StringBuilder<constant::buffer::kMax512> textureResource;
    textureResource.appendFormat( "%#<RenderGraphResourceDesc _name=\"DepthHistory\" _kind=\"Persistent\" _viewable=\"Texture\"/>", Util::kResources );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load(
        asset, Util::makeXML( textureResource.view(), "<RenderGraphPassDesc _name=\"GPUCull\" _type=\"GPUCull\"><_listIndirectArgs><item>DepthHistory</item></_listIndirectArgs></RenderGraphPassDesc>" ) ) );
    // 규칙 밖 목록도 선언한 자원이다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load(
        asset, Util::makeXML( Util::kResources, "<RenderGraphPassDesc _name=\"GPUCull\" _type=\"GPUCull\"><_listCopyDest><item>Nowhere</item></_listCopyDest></RenderGraphPassDesc>" ) ) );
    // 뷰 패스는 첨부만 읽고 쓴다.
    StringBuilder<constant::buffer::kMax256> viewList;
    viewList.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" %#><_listCopySource><item>SceneColor</item></_listCopySource", RenderPipelineTestUtil::kOpaqueState );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( viewList.view() ) ) );
}

/** @brief [RenderPipelineAssetTest] 엔진 파이프라인은 모두 프레임 패스 한 벌(framecomputerenderpass.xml)을 합쳐 컬링 · 그리드 · 스킨을 선언한다 */
SW_TEST_CASE( RenderPipelineAssetTest, ShippedPipelinesDeclareFramePasses )
{
    for ( const utf8* pPath : { "engine/pipeline/forwardpipeline.xml", "engine/pipeline/forwardpipelinestaged.xml", "engine/pipeline/forwardprepasspipeline.xml", "engine/pipeline/forwardtoonpipeline.xml",
                                "engine/pipeline/forward2dpipeline.xml", "engine/pipeline/deferredpipeline.xml" } )
    {
        RenderPipelineAsset asset;
        SW_REQUIRE_TRUE_MSG( asset.loadFromXMLFile( AssetID{ string_view( pPath ) } ), pPath );
        for ( const RenderPassKind type : { RenderPassKind::DepthHistory, RenderPassKind::HZBBuild, RenderPassKind::VFXSimulation, RenderPassKind::VFXParticleSort, RenderPassKind::VFXMeshInstance,
                                            RenderPassKind::InstanceAnimation, RenderPassKind::MeshMorph, RenderPassKind::MeshSkin, RenderPassKind::GPUCull, RenderPassKind::LightGrid,
                                            RenderPassKind::VolumetricFog, RenderPassKind::SceneVoxelize, RenderPassKind::ProbeUpdate, RenderPassKind::CanvasMarker } )
        {
            SW_EXPECT_TRUE_MSG( asset.hasFramePass( type ), pPath );
        }
    }
}

/**
 * @brief [RenderPipelineAssetTest] 컴퓨트 구현 — 표가 가진 종류(블룸 다운샘플 · 피사계 심도)만 고를 수 있고, 셰이더는 같은 파일에 컴퓨트 define 을 더한 것이며, 없는 종류에 적으면 읽기 오류다
 */
SW_TEST_CASE( RenderPipelineAssetTest, ComputeImplementationIsATableChoice )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.PassNoComputeImplementation" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::BloomDownsample ).hasFlag( RenderPassTraitFlag::kComputeImplementation ) );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::DepthOfField ).hasFlag( RenderPassTraitFlag::kComputeImplementation ) );
    SW_EXPECT_FALSE( getRenderPassKindInfo( RenderPassKind::Bloom ).hasFlag( RenderPassTraitFlag::kComputeImplementation ) );

    // 셰이더는 픽셀 구현과 같은 파일 · define 에 컴퓨트 define 하나를 더한다 — 쿠커와 런타임이 같은 함수를 부른다.
    RenderGraphPassDesc downsample{};
    downsample._type                        = RenderPassKind::BloomDownsample;
    downsample._implementation              = RenderPassExecutionMode::Compute;
    const EngineDefaultAssets&      assets  = engine::getSubsystem<EngineDefaultAssets>();
    const RenderPassShaderSelection pixel   = selectRenderPassShader( RenderPassKind::BloomDownsample, &downsample, &downsample, assets );
    const RenderPassShaderSelection compute = selectRenderPassComputeShader( downsample, assets );
    SW_EXPECT_EQUAL( pixel._shaderPath, compute._shaderPath );
    SW_EXPECT_TRUE( VectorUtil::contains( compute._listDefine, string( kPassComputeImplementationDefine ) ) );
    SW_EXPECT_TRUE( VectorUtil::contains( compute._listDefine, string( kPassBloomDownsampleDefine ) ) );
    SW_EXPECT_FALSE( VectorUtil::contains( pixel._listDefine, string( kPassComputeImplementationDefine ) ) );

    // 컴퓨트 구현이 없는 종류에 적으면 실행이 읽지 않는 칸이다.
    RenderPipelineAsset                      asset;
    StringBuilder<constant::buffer::kMax256> opaqueCompute;
    opaqueCompute.appendFormat( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" _implementation=\"Compute\" %#", RenderPipelineTestUtil::kOpaqueState );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( opaqueCompute.view() ) ) );
}

/**
 * @brief [RenderPipelineAssetTest] 디퍼드 조명 타일 종류 변형은 조명 패스의 셰이더 · define 에 종류 define 하나를 더한 것이고 종류마다 다르다 — 쿠커와 런타임이 같은 함수를 부른다.
 *        분류 컴퓨트는 네이티브 bindless · 간접 인자 능력에서만 PSO 가 있다(에뮬은 풀스크린 조명).
 */
SW_TEST_CASE( RenderPipelineAssetTest, DeferredTileVariantsAddTheClassDefine )
{
    RenderPipelineAsset deferred;
    SW_REQUIRE_TRUE( deferred.loadFromXMLFile( AssetID{ string_view( "engine/pipeline/deferredpipeline.xml" ) } ) );
    const RenderGraphPassDesc* pLighting = VectorUtil::findIf( deferred.getGraphPass(), []( const RenderGraphPassDesc& pass )
    { return pass._type == RenderPassKind::Lighting; } );
    SW_REQUIRE_NOT_NULL( pLighting );
    const EngineDefaultAssets&      assets   = engine::getSubsystem<EngineDefaultAssets>();
    const RenderPassShaderSelection lighting = selectRenderPassShader( RenderPassKind::Lighting, pLighting, pLighting, assets );
    const RenderPassShaderSelection complex  = selectDeferredTileLightingShader( *pLighting, SW_DEFERRED_TILE_CLASS_COMPLEX, assets );
    const RenderPassShaderSelection simple   = selectDeferredTileLightingShader( *pLighting, SW_DEFERRED_TILE_CLASS_SIMPLE, assets );
    SW_EXPECT_EQUAL( lighting._shaderPath, simple._shaderPath );
    SW_EXPECT_EQUAL( lighting._listDefine.size() + 1, simple._listDefine.size() );
    SW_EXPECT_TRUE( VectorUtil::contains( simple._listDefine, string( "SW_DEFERRED_TILE_CLASS=1" ) ) );
    SW_EXPECT_TRUE( VectorUtil::contains( complex._listDefine, string( "SW_DEFERRED_TILE_CLASS=0" ) ) );
    const RenderPassKindInfo& classify = getRenderPassKindInfo( RenderPassKind::DeferredTileClassify );
    SW_EXPECT_TRUE( classify.hasFlag( RenderPassTraitFlag::kCompute ) && classify.hasFlag( RenderPassTraitFlag::kRequiresNativeBindless ) &&
                    classify.hasFlag( RenderPassTraitFlag::kRequiresGPUCulling ) );
}

/**
 * @brief [RenderPipelineAssetTest] 디퍼드의 SSAO 는 조명 패스 앞에서 돌고 조명 패스만 AO 를 읽는다(RN-11)
 * @details AO 를 후처리(블룸)에서 최종 색 전체에 곱하면 직접광 · 자체 발광 · 반투명까지 어두워진다. 언리얼 · 유니티처럼 앰비언트 · 프로브 빛에만 든다(deferred_lighting.hlsl).
 *          블룸 · Present 의 입력 계약에서 AO 를 뺐으므로 후처리에 AOColor 를 선언한 XML 은 로드가 거절한다.
 */
SW_TEST_CASE( RenderPipelineAssetTest, AmbientOcclusionFeedsLightingOnly )
{
    RenderPipelineAsset deferred;
    SW_REQUIRE_TRUE( deferred.loadFromXMLFile( AssetID{ string_view( "engine/pipeline/deferredpipeline.xml" ) } ) );
    const vector<RenderGraphPassDesc>& listPass     = deferred.getGraphPass();
    size_t                             ssaoIndex    = listPass.size();
    size_t                             lightIndex   = listPass.size();
    bool                               bLightReadAO = false;
    for ( size_t passIndex = 0; passIndex < listPass.size(); ++passIndex )
    {
        const RenderGraphPassDesc& pass = listPass[passIndex];
        if ( pass._type == RenderPassKind::SSAO )
            ssaoIndex = passIndex;
        const bool bReadsAO = VectorUtil::containsIf( pass._listResolvedInput, []( const RenderGraphPassDesc::ResolvedAttachment& input )
        { return input._attachment == hashed_string( "AOColor" ); } );
        if ( pass._type == RenderPassKind::Lighting )
        {
            lightIndex   = passIndex;
            bLightReadAO = bReadsAO;
            continue;
        }
        SW_EXPECT_FALSE_MSG( bReadsAO, pass._name.c_str() ); // 조명 밖에서는 AO 를 읽지 않는다
    }
    SW_REQUIRE_TRUE( ssaoIndex < listPass.size() && lightIndex < listPass.size() );
    SW_EXPECT_TRUE( ssaoIndex < lightIndex );
    SW_EXPECT_TRUE( bLightReadAO );
}
