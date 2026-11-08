// RenderPipelineAssetTest — 파이프라인 · 렌더 패스 XML 이 실제 동작을 정하는지: 상태 값이 PSO 에 그대로 들어가고, 받아 줄 수 없는 조합은 읽기 오류이며, 렌더 패스의 첨부가 합쳐진다(nogpu).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
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
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"true\" _bEnableDepthWrite=\"false\" _blendPreset=\"Transparent\" _cullMode=\"None\"" ) );
    RHIPipelineStateDesc desc{};
    FramePSOManager::applyPassRenderState( desc, RenderPassType::ForwardOpaque, &asset.getGraphPass()[0] );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( desc._renderState._bEnableDepthTest ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( desc._renderState._bEnableDepthWrite ) );                   // 표는 깊이를 쓴다
    SW_EXPECT_TRUE( desc._renderState._arrBlendTarget[0] == RHIBlendTargetState::makeStraightAlpha() ); // 표는 섞지 않는다
    SW_EXPECT_TRUE( desc._renderState._cullMode == RHICullMode::None );                                 // 표는 뒷면 컬링이다
    SW_EXPECT_TRUE( desc._depthStencilFormat != RHIFormat::Unknown );

    RenderPipelineAsset noDepth;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::loadMeshState( noDepth, "_bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _cullMode=\"Front\"", "ForwardOpaque", false ) );
    FramePSOManager::applyPassRenderState( desc, RenderPassType::ForwardOpaque, &noDepth.getGraphPass()[0] );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( desc._renderState._bEnableDepthTest ) );
    SW_EXPECT_TRUE( desc._depthStencilFormat == RHIFormat::Unknown );
    SW_EXPECT_TRUE( desc._renderState._cullMode == RHICullMode::Front );
}

/** @brief [RenderPipelineAssetTest] XML 이 없는 내부 슬롯은 표의 값이다 */
SW_TEST_CASE( RenderPipelineAssetTest, InternalSlotsUseTheTable )
{
    RHIPipelineStateDesc desc{};
    FramePSOManager::applyPassRenderState( desc, RenderPassType::ForwardOpaqueNoDepthWrite, nullptr );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( desc._renderState._bEnableDepthTest ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( desc._renderState._bEnableDepthWrite ) );
    SW_EXPECT_TRUE( desc._renderState._cullMode == RHICullMode::Back );

    FramePSOManager::applyPassRenderState( desc, RenderPassType::Canvas, nullptr );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( desc._renderState._bEnableDepthTest ) );
    SW_EXPECT_TRUE( desc._renderState._arrBlendTarget[0] == RHIBlendTargetState::makePremultipliedAlpha() );
    SW_EXPECT_TRUE( desc._renderState._cullMode == RHICullMode::None );
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

/** @brief [RenderPipelineAssetTest] 대소문자만 다른 첨부 이름은 겹침이다 — 실행 때 첨부를 대소문자를 무시하는 이름으로 찾는다 */
SW_TEST_CASE( RenderPipelineAssetTest, AttachmentNamesDifferingOnlyInCaseAreDuplicates )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.AttachmentNameDuplicated" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    const string        passAttributes = string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" " ) + RenderPipelineTestUtil::kOpaqueState;
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
    const string        passAttributes = string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" _vertexEntryPoint=\"MyVS\" " ) + RenderPipelineTestUtil::kOpaqueState;
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
            string attributes = "_shaderPath=\"game/empty/shaders/compute_channelswap.hlsl\" ";
            attributes.append( kNoState );
            attributes += " ";
            attributes.append( extra );
            return attributes;
        }
    };
} // namespace

/** @brief [RenderPipelineAssetTest] 컴퓨트 패스: 셰이더 · 입력 · 출력 하나면 읽힌다. 진입점 · 그룹 크기는 적은 값이다 */
SW_TEST_CASE( RenderPipelineAssetTest, ComputePassLoads )
{
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, ComputePassTestUtil::makeXML( ComputePassTestUtil::withShader( "_computeEntryPoint=\"MyCS\" _threadGroupSize=\"4\"" ), "<item>A</item>", "<item>B</item>" ) ) );
    const RenderGraphPassDesc& pass = asset.getGraphPass()[0];
    SW_EXPECT_TRUE( pass._type == RenderPassType::Compute );
    SW_EXPECT_EQUAL( string( "MyCS" ), pass._computeEntryPoint );
    SW_EXPECT_EQUAL( 4u, pass._threadGroupSize );
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::Compute ).hasFlag( RenderPassTraitFlag::kPipelineCompute ) );
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
    const string blended = "_shaderPath=\"game/empty/shaders/compute_channelswap.hlsl\" _cullMode=\"None\" _bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _blendPreset=\"Transparent\"";
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( blended, "", "<item>B</item>" ) ) );
    const string passAttributes = string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" _computeEntryPoint=\"CSMain\" " ) + RenderPipelineTestUtil::kOpaqueState;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( passAttributes ) ) );
}

/** @brief [RenderPipelineAssetTest] 선택 스테이지: 패스 칸의 진입점을 그대로 가진다. 헐은 도메인과 함께 적고, 머티리얼 셰이더로 그리는 패스 · 컴퓨트 패스에는 쓸 수 없다 */
SW_TEST_CASE( RenderPipelineAssetTest, OptionalStagesFollowTheRules )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.PassHullNeedsDomain" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PassOptionalStageNotAllowed" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    RenderPipelineAsset stages;
    SW_REQUIRE_TRUE( stages.loadFromXMLFile( AssetID{ string_view( "game/empty/pipeline/tessellationstagepipeline.xml" ) } ) );
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

    RenderPipelineAsset asset;
    const string        opaque = string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" " ) + RenderPipelineTestUtil::kOpaqueState;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( opaque + " _hullEntryPoint=\"HSMain\"" ) ) ); // 도메인이 없고 머티리얼 패스다
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( opaque + " _geometryEntryPoint=\"GSMain\"" ) ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, ComputePassTestUtil::makeXML( ComputePassTestUtil::withShader( "_geometryEntryPoint=\"GSMain\"" ), "", "<item>B</item>" ) ) );
}

/** @brief [RenderPipelineAssetTest] 엔진 파이프라인이 모두 렌더 패스를 합쳐 검증을 통과한다. 디퍼드 반투명은 섞는다 */
SW_TEST_CASE( RenderPipelineAssetTest, ShippedPipelinesLoad )
{
    for ( const utf8* pPath : { "engine/pipeline/forwardpipeline.xml", "engine/pipeline/forwardpipelinestaged.xml", "engine/pipeline/forwardprepasspipeline.xml", "engine/pipeline/forwardtoonpipeline.xml",
                                "engine/pipeline/forward2dpipeline.xml", "engine/pipeline/deferredpipeline.xml", "game/empty/pipeline/computepasspipeline.xml", "game/empty/pipeline/extensionpasspipeline.xml" } )
    {
        RenderPipelineAsset asset;
        SW_EXPECT_TRUE_MSG( asset.loadFromXMLFile( AssetID{ string_view( pPath ) } ), pPath );
    }
    RenderPipelineAsset deferred;
    SW_REQUIRE_TRUE( deferred.loadFromXMLFile( AssetID{ string_view( "engine/pipeline/deferredpipeline.xml" ) } ) );
    for ( const RenderGraphPassDesc& pass : deferred.getGraphPass() )
    {
        if ( pass._type == RenderPassType::Transparent )
            SW_EXPECT_TRUE( pass._resolvedRenderState.isAnyBlendEnabled() );
        // 디퍼드 조명은 G버퍼 패스가 장면 색에 먼저 적은 자체 발광 위에 더한다(G버퍼 명세 판 3).
        if ( pass._type == RenderPassType::Lighting )
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
    const string&   contentRoot = engine::getSubsystem<AssetPaths>().getRootFolderPath();
    const TypeInfo& typeInfo    = *RenderPipelineDesc::StaticType();
    for ( const utf8* pID : { "engine/pipeline/forwardtoonpipeline.xml", "engine/pipeline/forwardprepasspipeline.xml", "engine/pipeline/forward2dpipeline.xml", "engine/pipeline/forwardpipelinestaged.xml",
                              "game/empty/pipeline/extensionpasspipeline.xml", "game/empty/pipeline/geometrystagepipeline.xml", "game/empty/pipeline/computepasspipeline.xml" } )
    {
        string sourceText;
        SW_REQUIRE_TRUE( AssetIO::readTextAsset( AssetID{ pID }, sourceText ) );
        SW_EXPECT_FALSE( DataInheritance::findBasePath( sourceText ).empty() );

        DataInheritanceDiskSource source{ contentRoot };
        string                    flatText;
        bool                      bComposed = false;
        SW_REQUIRE_TRUE( ResourceComposer::compose( sourceText, pID, ResourceContext::forPackCook( source ), flatText, bComposed ) );
        SW_EXPECT_TRUE( bComposed );
        SW_EXPECT_TRUE( DataInheritance::findBasePath( flatText ).empty() );

        RenderPipelineAsset cooked;
        RenderPipelineAsset merged;
        const string        flatPath = test::makeScratchPath( "flat.xml" );
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( flatPath, flatText ) );
        SW_REQUIRE_TRUE( cooked.loadFromDiskFile( flatPath ) );
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
    source._listPass[0]._cullMode = RHICullMode::Front;

    const string                 childPath = test::makeScratchPath( "child.xml" );
    const optional<AbsolutePath> childFile = FileUtil::requireAbsolutePath( childPath );
    SW_REQUIRE_TRUE( childFile.has_value() );
    SW_REQUIRE_TRUE( ReflectedXMLFile::saveDescAsChild( *childFile, source, AssetID{ "engine/pipeline/forwardpipeline.xml" } ) );
    string childText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( childPath, childText ) );
    SW_EXPECT_EQUAL( string( "engine/pipeline/forwardpipeline.xml" ), DataInheritance::findBasePath( childText ) );
    SW_EXPECT_TRUE( childText.find( "Canvas" ) == string::npos ); // 같은 패스는 적지 않는다

    RenderPipelineDesc rereadChild;
    SW_REQUIRE_TRUE( ReflectedXMLFile::loadDescFromDiskFile( childPath, rereadChild ) );
    SW_EXPECT_EQUAL( XMLSerializer::serialize( &source, typeInfo ), XMLSerializer::serialize( &rereadChild, typeInfo ) );

    const string                 fullPath = test::makeScratchPath( "full.xml" );
    const optional<AbsolutePath> fullFile = FileUtil::requireAbsolutePath( fullPath );
    SW_REQUIRE_TRUE( fullFile.has_value() );
    SW_REQUIRE_TRUE( ReflectedXMLFile::saveDesc( *fullFile, source ) );
    RenderPipelineDesc rereadFull;
    SW_REQUIRE_TRUE( ReflectedXMLFile::loadDescFromDiskFile( fullPath, rereadFull ) );
    SW_EXPECT_EQUAL( XMLSerializer::serialize( &source, typeInfo ), XMLSerializer::serialize( &rereadFull, typeInfo ) );
}

/** @brief [RenderPipelineAssetTest] 문자열이던 칸은 enum 이다 — 오타는 읽기 오류이고 받는 철자는 enum 하나에서 나온다 */
SW_TEST_CASE( RenderPipelineAssetTest, MistypedEnumsAreLoadErrors )
{
    SW_EXPECT_LOG( "Serialization.UnknownEnumerator" );
    SW_EXPECT_LOG( "Serialization.OrphanValue" );
    RenderPipelineAsset asset;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( string( "_type=\"ForwardOpaqe\" _depthAttachment=\"SceneDepth\" " ) + RenderPipelineTestUtil::kOpaqueState ) ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" " ) + RenderPipelineTestUtil::kOpaqueState, "", "",
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
    const RHIBlendTargetState& additive = asset.getGraphPass()[0]._resolvedRenderState._arrBlendTarget[3];
    SW_EXPECT_TRUE( additive._dstColor == RHIBlendFactor::One );
    SW_EXPECT_TRUE( additive._bEnableBlend == SW_TRUE );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_blendPreset=\"Plasma\"" ) );

    const auto makeWithTargets = []( string_view passExtra, string_view targets )
    {
        string            xml     = RenderPipelineTestUtil::makeXML( string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" " ) + string( passExtra ) );
        const utf8* const pMarker = "<_listPermutation/>";
        xml.insert( xml.find( pMarker ), string( "<_listBlendTarget>" ) + string( targets ) + "</_listBlendTarget>" );
        return xml;
    };
    constexpr string_view kTarget = "<RHIBlendTargetState _bEnableBlend=\"true\" _srcColor=\"One\" _dstColor=\"One\" _colorWriteMask=\"RGB\"/>";
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( asset, makeWithTargets( "", kTarget ) ) );
    SW_EXPECT_TRUE( asset.getGraphPass()[0]._resolvedRenderState._arrBlendTarget[2]._colorWriteMask == RHIColorWriteMask::RGB ); // 하나면 모든 타깃이다
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, makeWithTargets( "_blendPreset=\"Opaque\"", kTarget ) ) );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, makeWithTargets( "", string( kTarget ) + string( kTarget ) ) ) ); // 컬러 출력은 하나다
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, makeWithTargets( "", "<RHIBlendTargetState _bEnableBlend=\"true\" _srcAlpha=\"SrcColor\"/>" ) ) );
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
    FramePSOManager::applyPassRenderState( desc, RenderPassType::ForwardOpaque, &asset.getGraphPass()[0] );
    SW_EXPECT_EQUAL( 7, static_cast<int32>( desc._renderState._stencilRef ) );
    SW_EXPECT_TRUE( desc._renderState._stencilFrontCompare == RHICompareFunc::Equal );
    SW_EXPECT_TRUE( desc._renderState._stencilFrontPass == RHIStencilOp::Replace );
    SW_EXPECT_FALSE( RenderPipelineTestUtil::loadMeshState( asset, "_bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _bEnableStencil=\"true\"", "ForwardOpaque", false ) );
}

/** @brief [RenderPipelineAssetTest] 첨부의 로드 · 저장 동작 · 초기값을 읽고, 저장하지 않는 첨부를 뒤 패스가 쓰면 읽기 오류다 */
SW_TEST_CASE( RenderPipelineAssetTest, DiscardedAttachmentCannotBeRead )
{
    SW_EXPECT_LOG( "RenderPipelineAsset.AttachmentDiscardedButUsed" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineValidationFoundIssues" );
    SW_EXPECT_LOG( "RenderPipelineAsset.PipelineIssueNotLoaded" );
    using Util                         = RenderPipelineTestUtil;
    const string        passAttributes = string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" " ) + Util::kOpaqueState;
    RenderPipelineAsset asset;
    SW_REQUIRE_TRUE( Util::load( asset, Util::makeXML( passAttributes, "", "", "<RenderPassAttachment _name=\"SceneDepth\" _format=\"D24_UNORM_S8_UINT\" _clearDepth=\"0.5\" _clearStencil=\"3\" _storeOp=\"DontCare\"/>" ) ) );
    const RHIAttachmentOps ops = asset.getDesc()._listAttachment[1].getOps();
    SW_EXPECT_NEAR( 0.5f, ops._clearDepth, 1e-6f );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( ops._clearStencil ) );
    SW_EXPECT_TRUE( ops._storeOp == RHIRenderPassStoreOp::DontCare );

    // 같은 첨부를 여는 패스가 둘이면 뒤 패스가 정의되지 않은 값을 읽는다.
    string xml = Util::makeXML( passAttributes, "", "", "<RenderPassAttachment _name=\"SceneDepth\" _format=\"D24_UNORM_S8_UINT\" _storeOp=\"DontCare\"/>" );
    xml.insert( xml.find( "</_listPass>" ), string( "<RenderGraphPassDesc _name=\"Second\" " ) + passAttributes + "><_listOutput><item>SceneColor</item></_listOutput></RenderGraphPassDesc>" );
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
    SW_EXPECT_TRUE( passWritesVelocity( RenderPassType::ForwardOpaque, &pass ) );
    SW_EXPECT_TRUE( passWritesVelocity( RenderPassType::ForwardOpaqueNoDepthWrite, &pass ) ); // 내부 슬롯은 ForwardOpaque 의 선언을 본다
    SW_EXPECT_FALSE( passWritesVelocity( RenderPassType::Transparent, &pass ) );              // 반투명은 속도를 쓰지 않는다
    SW_EXPECT_FALSE( passWritesVelocity( RenderPassType::ForwardOpaque, nullptr ) );
    const EngineDefaultAssets& assets = engine::getSubsystem<EngineDefaultAssets>();
    SW_EXPECT_TRUE( VectorUtil::contains( selectRenderPassShader( RenderPassType::ForwardOpaque, &pass, &pass, assets )._listDefine, kVelocityDefine ) );
    SW_EXPECT_TRUE( VectorUtil::contains( selectRenderPassShader( RenderPassType::ForwardOpaqueNoDepthWrite, nullptr, &pass, assets )._listDefine, kVelocityDefine ) );
    SW_EXPECT_FALSE( VectorUtil::contains( selectRenderPassShader( RenderPassType::ForwardOpaqueNoDepthWrite, nullptr, nullptr, assets )._listDefine, kVelocityDefine ) );

    // 속도를 선언하지 않은 파이프라인은 첨부도 define 도 없다.
    RenderPipelineAsset withoutVelocity;
    SW_REQUIRE_TRUE( RenderPipelineTestUtil::load( withoutVelocity, VelocityTestUtil::makeXML( "ForwardOpaque", "<item>SceneColor</item><item>SceneDepth</item>" ) ) );
    SW_EXPECT_FALSE( passWritesVelocity( RenderPassType::ForwardOpaque, &withoutVelocity.getGraphPass()[0] ) );
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
            const bool bReader = pass._type == RenderPassType::TAA || pass._type == RenderPassType::MotionBlur;
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
    SW_EXPECT_TRUE( isPipelinePassType( RenderPassType::Distortion ) );
    SW_EXPECT_TRUE( isPipelinePassType( RenderPassType::DistortionApply ) );
    const RenderPassTypeInfo& accumulate = getRenderPassTypeInfo( RenderPassType::Distortion );
    SW_EXPECT_TRUE( accumulate.hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch ) );
    SW_EXPECT_FALSE( accumulate.hasFlag( RenderPassTraitFlag::kDepthWrite ) );
    const vector<string> listDistortion{ string( kMaterialDistortionDefine ), string( "MATERIAL_BLEND_TRANSLUCENT" ) };
    const vector<string> listPlain{ string( "MATERIAL_BLEND_TRANSLUCENT" ) };
    SW_EXPECT_TRUE( FrameRendererUtil::drawsMaterialInPass( RenderPassType::Distortion, &listDistortion ) );
    SW_EXPECT_FALSE( FrameRendererUtil::drawsMaterialInPass( RenderPassType::Distortion, &listPlain ) );
    SW_EXPECT_FALSE( FrameRendererUtil::drawsMaterialInPass( RenderPassType::Distortion, nullptr ) ); // 머티리얼 없는 배치는 왜곡이 없다
    const EngineDefaultAssets& assets = engine::getSubsystem<EngineDefaultAssets>();
    SW_EXPECT_TRUE( VectorUtil::contains( selectRenderPassShader( RenderPassType::Distortion, nullptr, nullptr, assets )._listDefine, string( kPassDistortionDefine ) ) );
    const RenderPassInputSignature* pApply = findRenderPassInputSignature( RenderPassType::DistortionApply );
    SW_REQUIRE_TRUE( pApply != nullptr );
    SW_EXPECT_TRUE( pApply->reads( RenderPassInputRole::SceneColorCopy ) );
    SW_EXPECT_TRUE( pApply->reads( RenderPassInputRole::Distortion ) );
    SW_EXPECT_TRUE( pApply->reads( RenderPassInputRole::SceneDepth ) );
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
            accumulateIndex  = listPass[index]._type == RenderPassType::Distortion ? index : accumulateIndex;
            applyIndex       = listPass[index]._type == RenderPassType::DistortionApply ? index : applyIndex;
            transparentIndex = listPass[index]._type == RenderPassType::Transparent ? index : transparentIndex;
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
    SW_EXPECT_TRUE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::makePass( "Before", "PostProcessMaterial", "_blendableLocation=\"BeforeTonemap\"", "HDR", "HDRPost" ) + tonemap ) ) );
    // 자리를 적지 않았다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::makePass( "Before", "PostProcessMaterial", "", "HDR", "HDRPost" ) + tonemap ) ) );
    // 같은 자리 둘.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::makePass( "Before", "PostProcessMaterial", "_blendableLocation=\"BeforeTonemap\"", "HDR", "HDRPost" ) + tonemap +
                                                                         Util::makePass( "Again", "PostProcessMaterial", "_blendableLocation=\"BeforeTonemap\"", "HDR", "HDRAgain" ) ) ) );
    // 톤맵 대신 자리가 톤맵보다 앞이다 — 덮을 출력이 아직 없다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::makePass( "Replace", "PostProcessMaterial", "_blendableLocation=\"ReplacingTonemap\"", "HDRPost", "LDR" ) +
                                                                         Util::makePass( "Tone", "Tonemap", "", "HDR", "HDRPost" ) ) ) );

    RenderPipelineAsset deferred;
    SW_REQUIRE_TRUE( deferred.loadFromXMLFile( AssetID{ string_view( "engine/pipeline/deferredpipeline.xml" ) } ) );
    uint32 locationMask = 0;
    for ( const RenderGraphPassDesc& pass : deferred.getGraphPass() )
    {
        if ( pass._type == RenderPassType::PostProcessMaterial )
            locationMask |= 1u << static_cast<uint32>( pass._blendableLocation );
    }
    const uint32 expected = ( 1u << static_cast<uint32>( PostProcessBlendableLocation::BeforeTonemap ) ) | ( 1u << static_cast<uint32>( PostProcessBlendableLocation::AfterTonemap ) ) |
                            ( 1u << static_cast<uint32>( PostProcessBlendableLocation::ReplacingTonemap ) );
    SW_EXPECT_EQUAL( expected, locationMask );
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
            const string passAttributes = string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" " ) + RenderPipelineTestUtil::kOpaqueState;
            return RenderPipelineTestUtil::makeXML( passAttributes, "", extra );
        }

        static constexpr const utf8* kResources = "<RenderGraphResourceDesc _name=\"InstanceBuffer\" _kind=\"External\"/>"
                                                  "<RenderGraphResourceDesc _name=\"CullResults\" _kind=\"External\"/>"
                                                  "<RenderGraphResourceDesc _name=\"OcclusionTiles\" _kind=\"Persistent\"/>";
        static constexpr const utf8* kCullPass  = "<RenderGraphPassDesc _name=\"GPUCull\" _type=\"GPUCull\"><_listInput><item>InstanceBuffer</item><item>OcclusionTiles</item></_listInput>"
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
    SW_EXPECT_TRUE( asset.getFramePass()[0]._type == RenderPassType::GPUCull );
    SW_EXPECT_EQUAL( size_t( 2 ), asset.getFramePass()[0]._listInput.size() );
    SW_EXPECT_TRUE( asset.hasFramePass( RenderPassType::GPUCull ) );
    SW_EXPECT_FALSE( asset.hasFramePass( RenderPassType::LightGrid ) );
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
    string twoCulls = string( Util::kCullPass ) + "<RenderGraphPassDesc _name=\"GPUCull2\" _type=\"GPUCull\"/>";
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::kResources, twoCulls ) ) );
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
    SW_EXPECT_TRUE( asset.getFramePass()[0]._queue == RHIQueueType::AsyncCompute );
    // 깊이 히스토리는 풀스크린 드로우다 — 컴퓨트 큐는 렌더 타깃을 모른다.
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, Util::makeXML( Util::kResources, "<RenderGraphPassDesc _name=\"DepthHistory\" _type=\"DepthHistory\" _queue=\"AsyncCompute\"/>" ) ) );
    // 뷰 패스는 뷰마다 다시 돌아 포크 · 조인 자리가 없다.
    const string viewAsync = string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" _queue=\"AsyncCompute\" " ) + RenderPipelineTestUtil::kOpaqueState;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( viewAsync ) ) );
}

/** @brief [RenderPipelineAssetTest] 엔진 파이프라인은 모두 프레임 패스 한 벌(framecomputerenderpass.xml)을 합쳐 컬링 · 그리드 · 스킨을 선언한다 */
SW_TEST_CASE( RenderPipelineAssetTest, ShippedPipelinesDeclareFramePasses )
{
    for ( const utf8* pPath : { "engine/pipeline/forwardpipeline.xml", "engine/pipeline/forwardpipelinestaged.xml", "engine/pipeline/forwardprepasspipeline.xml", "engine/pipeline/forwardtoonpipeline.xml",
                                "engine/pipeline/forward2dpipeline.xml", "engine/pipeline/deferredpipeline.xml" } )
    {
        RenderPipelineAsset asset;
        SW_REQUIRE_TRUE_MSG( asset.loadFromXMLFile( AssetID{ string_view( pPath ) } ), pPath );
        for ( const RenderPassType type : { RenderPassType::DepthHistory, RenderPassType::OcclusionTiles, RenderPassType::VFXSimulation, RenderPassType::InstanceAnimation, RenderPassType::MeshMorph,
                                            RenderPassType::MeshSkin, RenderPassType::GPUCull, RenderPassType::LightGrid, RenderPassType::VolumetricFog, RenderPassType::ProbeUpdate,
                                            RenderPassType::CanvasMarker } )
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
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::BloomDownsample ).hasFlag( RenderPassTraitFlag::kComputeImplementation ) );
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::DepthOfField ).hasFlag( RenderPassTraitFlag::kComputeImplementation ) );
    SW_EXPECT_FALSE( getRenderPassTypeInfo( RenderPassType::Bloom ).hasFlag( RenderPassTraitFlag::kComputeImplementation ) );

    // 셰이더는 픽셀 구현과 같은 파일 · define 에 컴퓨트 define 하나를 더한다 — 쿠커와 런타임이 같은 함수를 부른다.
    RenderGraphPassDesc downsample{};
    downsample._type                        = RenderPassType::BloomDownsample;
    downsample._implementation              = RenderPassExecutionMode::Compute;
    const EngineDefaultAssets&      assets  = engine::getSubsystem<EngineDefaultAssets>();
    const RenderPassShaderSelection pixel   = selectRenderPassShader( RenderPassType::BloomDownsample, &downsample, &downsample, assets );
    const RenderPassShaderSelection compute = selectRenderPassComputeShader( downsample, assets );
    SW_EXPECT_EQUAL( pixel._shaderPath, compute._shaderPath );
    SW_EXPECT_TRUE( VectorUtil::contains( compute._listDefine, string( kPassComputeImplementationDefine ) ) );
    SW_EXPECT_TRUE( VectorUtil::contains( compute._listDefine, string( kPassBloomDownsampleDefine ) ) );
    SW_EXPECT_FALSE( VectorUtil::contains( pixel._listDefine, string( kPassComputeImplementationDefine ) ) );

    // 컴퓨트 구현이 없는 종류에 적으면 실행이 읽지 않는 칸이다.
    RenderPipelineAsset asset;
    const string        opaqueCompute = string( "_type=\"ForwardOpaque\" _depthAttachment=\"SceneDepth\" _implementation=\"Compute\" " ) + RenderPipelineTestUtil::kOpaqueState;
    SW_EXPECT_FALSE( RenderPipelineTestUtil::load( asset, RenderPipelineTestUtil::makeXML( opaqueCompute ) ) );
}
