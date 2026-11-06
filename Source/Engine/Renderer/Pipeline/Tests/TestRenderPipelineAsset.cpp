// RenderPipelineAssetTest — 파이프라인 · 렌더 패스 XML 이 실제 동작을 정하는지: 상태 값이 PSO 에 그대로 들어가고, 받아 줄 수 없는 조합은 읽기 오류이며, 렌더 패스의 첨부가 합쳐진다(nogpu).
#include "pch.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

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

namespace
{
    struct ComputePassTestUtil
    {
        static constexpr const utf8* kNoState = "_cullMode=\"None\" _bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _bEnableBlend=\"false\"";

        /** @brief 컴퓨트 패스 하나의 파이프라인 XML 입니다. */
        static string makeXML( string_view attributes, string_view inputs, string_view outputs )
        {
            string xml = "<RenderPipelineDesc _name=\"Test\"><_listAttachment>"
                         "<RenderPassAttachment _name=\"A\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _bClear=\"false\"/>"
                         "<RenderPassAttachment _name=\"B\" _format=\"R16G16B16A16_FLOAT\" _clearColor=\"0,0,0,1\" _bClear=\"false\"/>"
                         "<RenderPassAttachment _name=\"D\" _format=\"D24_UNORM_S8_UINT\" _clearColor=\"1,0,0,0\" _bClear=\"true\"/>"
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
    SW_EXPECT_TRUE( pass._resolvedType == RenderPassType::Compute );
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
    const string blended = "_shaderPath=\"game/empty/shaders/compute_channelswap.hlsl\" _cullMode=\"None\" _bEnableDepthTest=\"false\" _bEnableDepthWrite=\"false\" _bEnableBlend=\"true\"";
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
        if ( pass._resolvedType == RenderPassType::Transparent )
            SW_EXPECT_TRUE( pass._bEnableBlend );
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
        SW_REQUIRE_TRUE( DataInheritance::flattenByRootName( sourceText, pID, source, flatText ) );
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
    source._listPass[0]._cullMode = "Front";

    const string                      childPath = test::makeScratchPath( "child.xml" );
    const std::optional<AbsolutePath> childFile = FileUtil::requireAbsolutePath( childPath );
    SW_REQUIRE_TRUE( childFile.has_value() );
    SW_REQUIRE_TRUE( ReflectedXMLFile::saveDescAsChild( *childFile, source, AssetID{ "engine/pipeline/forwardpipeline.xml" } ) );
    string childText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( childPath, childText ) );
    SW_EXPECT_EQUAL( string( "engine/pipeline/forwardpipeline.xml" ), DataInheritance::findBasePath( childText ) );
    SW_EXPECT_TRUE( childText.find( "Canvas" ) == string::npos ); // 같은 패스는 적지 않는다

    RenderPipelineDesc rereadChild;
    SW_REQUIRE_TRUE( ReflectedXMLFile::loadDescFromDiskFile( childPath, rereadChild ) );
    SW_EXPECT_EQUAL( XMLSerializer::serialize( &source, typeInfo ), XMLSerializer::serialize( &rereadChild, typeInfo ) );

    const string                      fullPath = test::makeScratchPath( "full.xml" );
    const std::optional<AbsolutePath> fullFile = FileUtil::requireAbsolutePath( fullPath );
    SW_REQUIRE_TRUE( fullFile.has_value() );
    SW_REQUIRE_TRUE( ReflectedXMLFile::saveDesc( *fullFile, source ) );
    RenderPipelineDesc rereadFull;
    SW_REQUIRE_TRUE( ReflectedXMLFile::loadDescFromDiskFile( fullPath, rereadFull ) );
    SW_EXPECT_EQUAL( XMLSerializer::serialize( &source, typeInfo ), XMLSerializer::serialize( &rereadFull, typeInfo ) );
}
