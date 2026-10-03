/**
 * @file TestShaderBakeRequest.cpp
 * @brief 베이커가 모으는 요청이 **런타임이 실제로 요구하는 퍼뮤테이션**을 덮는지 본다.
 * @details GPU 도 셰이더 컴파일러도 필요 없다 — 파이프라인 XML 과 머티리얼을 읽어 (셰이더 · 진입점 ·
 *          define) 목록을 만들고 해시를 비교할 뿐이다. 그래서 nogpu 라벨의 `EngineTest_NoGPU` 에 든다.
 *
 *          이 파일이 있는 이유: 베이커는 패스의 define 을 **파이프라인 XML 의 `_listPermutation`** 에서만
 *          읽었는데, 런타임은 G버퍼 패스에 `SW_PASS_GBUFFER=1` 을 **C++ 에서** 얹었다. 두 자리가 어긋나자
 *          런타임이 찾는 해시를 아무도 굽지 않았고, Shipping 은 런타임 컴파일이 없으므로 G버퍼 드로우가
 *          통째로 사라졌다 — 디퍼드 화면이 한 색으로 남고 SSAO 는 가림을 하나도 내지 않았다.
 *          그 증상은 GPU 스위트(`RenderPassGpuTest`)에서만 보였고, 그 스위트는 CI 가 돌리지 않는다.
 *          여기서는 그림을 그리지 않고 **목록만** 대조하므로 CI 가 잡는다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Renderer/Bake/ShaderBakeDriver.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassTypeTraits.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 이 스템(예: "gbuffer")·스테이지·해시를 가진 요청이 목록에 있는가. */
    bool hasRequestInternal( const sw::vector<sw::ShaderBakeRequest>& listRequest,
                             sw::string_view                          stemLower,
                             sw::ShaderStage                          stage,
                             uint64                                   permutationHash )
    {
        for ( const sw::ShaderBakeRequest& request : listRequest )
        {
            if ( request._stage != stage || request._permutationHash != permutationHash )
                continue;
            if ( sw::ShaderBaker::getStemLower( request._shaderPath ) == stemLower )
                return true;
        }
        return false;
    }

    /** @brief 이 경로(정규화)·진입점·스테이지·해시를 가진 요청이 목록에 있는가. */
    bool hasExactRequestInternal( const sw::vector<sw::ShaderBakeRequest>& listRequest,
                                  sw::string_view                          shaderPath,
                                  sw::string_view                          entryPoint,
                                  sw::ShaderStage                          stage,
                                  uint64                                   permutationHash )
    {
        const sw::string normPath = sw::FileUtil::normalizeSeparators( shaderPath );
        for ( const sw::ShaderBakeRequest& request : listRequest )
        {
            if ( request._stage == stage && request._permutationHash == permutationHash && request._entryPoint == entryPoint &&
                 request._shaderPath == normPath )
                return true;
        }
        return false;
    }
} // namespace

/**
 * @brief [ShaderBakeRequestTest] 패스가 C++ 에서 얹는 define 까지 요청에 든다
 * @details `FrameRendererUtil::getPassDefine` 이 런타임과 베이커가 함께 보는 **유일한 정본**이다.
 *          베이커가 그것을 안 보면(예전이 그랬다) 런타임이 요청하는 해시가 목록에 없다.
 */
SW_TEST_CASE( ShaderBakeRequestTest, PassDefineReachesBakedRequests )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    sw::vector<sw::ShaderBakeRequest> listRequest;
    sw::ShaderBakeDriver::collectAllRequests( sw::ResourceUtil::getRootFolderPath(), listRequest );
    SW_EXPECT_TRUE_MSG( listRequest.empty() == false, "요청을 하나도 모으지 못했다 — 리소스 루트를 못 찾았을 수 있다" );
    SW_ASSERT_TRUE( listRequest.empty() == false );

    // 런타임이 G버퍼 패스에 얹는 define 집합 — FrameRendererResources 가 PSO 를 만들 때 쓰는 그것이다.
    const sw::vector<sw::string> listGbufferDefine = sw::FrameRendererUtil::getPassDefine( sw::RenderPassType::GBuffer );
    SW_EXPECT_TRUE_MSG( listGbufferDefine.empty() == false,
                        "G버퍼 패스가 얹는 define 이 사라졌다 — 이 테스트가 지키려던 축이 없어졌다" );
    SW_ASSERT_TRUE( listGbufferDefine.empty() == false );

    const uint64 gbufferPermHash = sw::ShaderBaker::computePermutationHash( listGbufferDefine );
    SW_EXPECT_TRUE_MSG( hasRequestInternal( listRequest, "gbuffer", sw::ShaderStage::Vertex, gbufferPermHash ),
                        "G버퍼 패스 define 이 든 VS 요청이 없다 — Shipping 에서 G버퍼 드로우가 통째로 사라진다" );
    SW_EXPECT_TRUE_MSG( hasRequestInternal( listRequest, "gbuffer", sw::ShaderStage::Pixel, gbufferPermHash ),
                        "G버퍼 패스 define 이 든 PS 요청이 없다 — Shipping 에서 G버퍼 드로우가 통째로 사라진다" );
}

/**
 * @brief [ShaderBakeRequestTest] 요청 목록에 같은 (셰이더·진입점·스테이지·해시) 가 두 번 들지 않는다
 * @details `appendRequestUnique` 의 계약이다. 중복은 그 자체로 치명적이진 않지만 같은 것을 네 번 굽게
 *          만들고, 무엇보다 "런타임 요청 = 요청" 대조를 흐린다.
 */
SW_TEST_CASE( ShaderBakeRequestTest, RequestsAreUnique )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    sw::vector<sw::ShaderBakeRequest> listRequest;
    sw::ShaderBakeDriver::collectAllRequests( sw::ResourceUtil::getRootFolderPath(), listRequest );
    SW_ASSERT_TRUE( listRequest.empty() == false );

    uint32 duplicateCount = 0;
    for ( size_t outer = 0; outer < listRequest.size(); ++outer )
    {
        for ( size_t inner = outer + 1; inner < listRequest.size(); ++inner )
        {
            const bool bSame = listRequest[outer]._stage == listRequest[inner]._stage &&
                               listRequest[outer]._permutationHash == listRequest[inner]._permutationHash &&
                               listRequest[outer]._entryPoint == listRequest[inner]._entryPoint &&
                               listRequest[outer]._shaderPath == listRequest[inner]._shaderPath;
            if ( bSame )
                ++duplicateCount;
        }
    }

    SW_EXPECT_EQUAL( 0u, duplicateCount );
}

/**
 * @brief [ShaderBakeRequestTest] 요청이 가리키는 셰이더는 도메인을 뗀 경로(`shaders/…`)가 서로 다르다
 * @details 같은 셰이더를 두 도메인(`common/` · `engine/`)에 사본으로 두면 한쪽만 고쳐도 다른 쪽을 부르는 패스는 옛 코드로 그리고,
 *          팩 상대 키(`shaders/x.hlsl`)는 검색 순서(game → common → engine)에서 앞 도메인의 사본을 집는다. 공유할 코드는 `.hlsli` 하나로
 *          두고 전역 경로 하나에서 부른다.
 */
SW_TEST_CASE( ShaderBakeRequestTest, RequestedShaderPathsAreUniqueAcrossDomains )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    sw::vector<sw::ShaderBakeRequest> listRequest;
    sw::ShaderBakeDriver::collectAllRequests( sw::ResourceUtil::getRootFolderPath(), listRequest );
    SW_ASSERT_TRUE( listRequest.empty() == false );

    // 도메인을 뗀 경로 → 처음 본 전역 경로
    sw::unordered_map<sw::string, sw::string> mapDomainlessToPath;
    sw::unordered_set<sw::string>             uniqueCopyPath;
    for ( const sw::ShaderBakeRequest& request : listRequest )
    {
        const sw::string normPath  = sw::FileUtil::normalizePath( request._shaderPath );
        const size_t     shaderPos = normPath.find( "shaders/" );
        if ( shaderPos == sw::string::npos )
            continue;
        const auto [it, bInserted] = mapDomainlessToPath.emplace( normPath.substr( shaderPos ), normPath );
        if ( bInserted || it->second == normPath || uniqueCopyPath.insert( normPath ).second == false )
            continue;
        SW_EXPECT_TRUE_MSG( false, ( normPath + " 와 " + it->second + " 가 같은 셰이더의 사본이다 — 하나를 지우고 경로 하나로 부를 것" ).c_str() );
    }
    SW_EXPECT_TRUE_MSG( mapDomainlessToPath.size() > 1, "셰이더 요청을 하나도 보지 못했다 — 이 시험이 아무것도 보지 않는다" );
    SW_EXPECT_EQUAL( size_t{ 0 }, uniqueCopyPath.size() );
}

/**
 * @brief [ShaderBakeRequestTest] 파이프라인 패스마다 런타임 PSO 가 컴파일할 (셰이더 · define) 을 베이커가 요청한다
 * @details 런타임은 `selectRenderPassShader` 로 패스의 셰이더(XML 의 `_shaderPath`, 없으면 패스 종류 표의 기본 셰이더)와 define
 *          (XML 퍼뮤테이션 + 패스 define)을 정한다. 베이커가 패스 종류를 다른 규칙으로 읽으면 그 패스만 매니페스트 미스로
 *          Shipping 에서 사라진다. 배포 파이프라인 넷을 `_shaderPath` 를 비우고 퍼뮤테이션 하나를 얹어 임시 루트에 다시 써서,
 *          모든 패스가 **기본 셰이더 + define** 경로를 타게 한 뒤 패스마다 대조한다.
 */
SW_TEST_CASE( ShaderBakeRequestTest, EveryPipelinePassShaderIsRequested )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    sw::EngineData engineData;
    SW_ASSERT_TRUE( engineData.loadFromResource() );

    const sw::string tempRoot    = test::makeTempDirectory( "bake_pipeline_probe" );
    const sw::string pipelineDir = sw::FileUtil::joinPath( tempRoot, "pipeline" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( pipelineDir ) );

    const utf8*      arrPipeline[]  = { "engine/pipeline/forwardpipeline.xml", "engine/pipeline/deferredpipeline.xml",
                                        "engine/pipeline/forwardprepasspipeline.xml", "engine/pipeline/forwardpipelinestaged.xml" };
    constexpr uint32 kPipelineCount = static_cast<uint32>( sizeof( arrPipeline ) / sizeof( arrPipeline[0] ) );

    sw::RenderPipelineResource arrProbe[kPipelineCount];
    for ( uint32 pipelineIndex = 0; pipelineIndex < kPipelineCount; ++pipelineIndex )
    {
        sw::RenderPipelineResource& probe = arrProbe[pipelineIndex];
        SW_ASSERT_TRUE_MSG( probe.loadFromXmlFile( arrPipeline[pipelineIndex] ), arrPipeline[pipelineIndex] );
        for ( sw::RenderGraphPassDesc& pass : probe.getDesc()._listPass )
        {
            pass._shaderPath.clear();
            pass._listPermutation.push_back( "SW_BAKE_PROBE=1" );
        }
        const sw::string probePath = sw::FileUtil::joinPath( pipelineDir, sw::FileUtil::getFileNamePart( arrPipeline[pipelineIndex] ) );
        SW_ASSERT_TRUE_MSG( probe.saveToXmlFile( probePath ), probePath.c_str() );
    }

    sw::vector<sw::ShaderBakeRequest> listRequest;
    sw::ShaderBakeDriver::collectAllRequests( tempRoot, listRequest );
    SW_ASSERT_TRUE( listRequest.empty() == false );

    bool arrCovered[sw::kRenderPassTypeCount]{};
    for ( const sw::RenderPipelineResource& probe : arrProbe )
    {
        for ( const sw::RenderGraphPassDesc& pass : probe.getGraphPass() )
        {
            const sw::RenderPassShaderSelection shader = sw::selectRenderPassShader( pass._resolvedType, &pass, engineData );
            const sw::string                    label  = probe.getDesc()._name + "/" + pass._name;
            SW_EXPECT_TRUE_MSG( shader._shaderPath.empty() == false, ( label + ": 기본 셰이더가 없다" ).c_str() );
            const uint64     permutationHash = sw::ShaderBaker::computePermutationHash( shader._listDefine );
            const sw::string vsEntry         = pass._vertexEntryPoint.empty() ? sw::string( "VSMain" ) : pass._vertexEntryPoint;
            SW_EXPECT_TRUE_MSG( hasExactRequestInternal( listRequest, shader._shaderPath, vsEntry, sw::ShaderStage::Vertex, permutationHash ),
                                ( label + ": 런타임 PSO 의 VS(" + shader._shaderPath + ") 요청이 없다 — Shipping 에서 이 패스가 사라진다" ).c_str() );
            if ( sw::FrameRendererUtil::hasPixelStage( pass, probe.getDesc()._listAttachment ) )
            {
                const sw::string psEntry = pass._pixelEntryPoint.empty() ? sw::string( "PSMain" ) : pass._pixelEntryPoint;
                SW_EXPECT_TRUE_MSG( hasExactRequestInternal( listRequest, shader._shaderPath, psEntry, sw::ShaderStage::Pixel, permutationHash ),
                                    ( label + ": 런타임 PSO 의 PS(" + shader._shaderPath + ") 요청이 없다 — Shipping 에서 이 패스가 사라진다" ).c_str() );
            }
            arrCovered[static_cast<uint32>( pass._resolvedType )] = true;
        }
    }

    // 배포 파이프라인이 파이프라인 타입을 모두 덮는지 본다(덮지 못한 타입은 이 대조가 보지 못한다).
    for ( uint32 typeIndex = 1; typeIndex < sw::kRenderPassTypeCount; ++typeIndex )
    {
        if ( sw::isPipelinePassType( static_cast<sw::RenderPassType>( typeIndex ) ) )
            SW_EXPECT_TRUE_MSG( arrCovered[typeIndex], ( "배포 파이프라인에 없는 패스 타입 " + sw::to_string( typeIndex ) ).c_str() );
    }
}

/**
 * @brief [ShaderBakeRequestTest] 씬 메시 패스 종류마다 · 뷰 모드마다 런타임이 만드는 변형을, 파이프라인 XML 이 하나도 없어도 요청한다
 * @details 런타임(`FrameRenderer::ensurePassResources`)은 로드한 파이프라인과 무관하게 패스 종류 표의 **모든** 종류로 엔진 PSO 를
 *          만들고(`selectRenderPassShader( type, nullptr, … )`), 뷰 모드가 바뀌면 머티리얼 없는 배치에도 그 위에 뷰 모드 define 을
 *          얹은 변형을 만든다(`findViewModeDefine`). 베이커가 파이프라인 XML 에 나오는 패스만 곱하면 어느 파이프라인에도 없는
 *          종류의 변형이 빠지고, Shipping 에서 `gv_viewMode` 를 바꾸면 그 PSO 를 만들지 못한다(셰이더 없음 오류 · Lit 으로 물러남).
 *          빈 임시 루트로 모아 XML 이 주는 몫을 빼고 표가 주는 몫만 본다.
 */
SW_TEST_CASE( ShaderBakeRequestTest, EveryViewModeVariantOfEveryMeshPassTypeIsRequested )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    sw::EngineData engineData;
    SW_ASSERT_TRUE( engineData.loadFromResource() );

    const sw::string emptyRoot = test::makeTempDirectory( "bake_no_pipeline" );
    SW_ASSERT_TRUE( emptyRoot.empty() == false );

    sw::vector<sw::ShaderBakeRequest> listRequest;
    sw::ShaderBakeDriver::collectAllRequests( emptyRoot, listRequest );
    SW_ASSERT_TRUE( listRequest.empty() == false );

    uint32 checkedVariantCount{ 0 };
    for ( uint32 typeIndex = 0; typeIndex < sw::kRenderPassTypeCount; ++typeIndex )
    {
        const sw::RenderPassType passType = static_cast<sw::RenderPassType>( typeIndex );
        if ( sw::FrameRendererUtil::drawsSceneMeshes( passType ) == false )
            continue;
        const sw::RenderPassShaderSelection passShader = sw::selectRenderPassShader( passType, nullptr, engineData );
        if ( passShader._shaderPath.empty() )
            continue;

        for ( uint32 viewModeIndex = 0; viewModeIndex < static_cast<uint32>( sw::RenderViewMode::Count ); ++viewModeIndex )
        {
            const sw::RenderViewMode viewMode        = static_cast<sw::RenderViewMode>( viewModeIndex );
            sw::vector<sw::string>   listDefine      = passShader._listDefine;
            const utf8*              pViewModeDefine = sw::FrameRendererUtil::findViewModeDefine( viewMode );
            if ( pViewModeDefine != nullptr && sw::FrameRendererUtil::appliesViewMode( passType ) )
                listDefine.push_back( pViewModeDefine );

            const uint64     permutationHash = sw::ShaderBaker::computePermutationHash( listDefine );
            const sw::string label           = passShader._shaderPath + " (pass type " + sw::to_string( typeIndex ) + ", view mode " +
                                     sw::to_string( viewModeIndex ) + ")";
            SW_EXPECT_TRUE_MSG( hasExactRequestInternal( listRequest, passShader._shaderPath, "VSMain", sw::ShaderStage::Vertex, permutationHash ),
                                ( label + ": 런타임이 만드는 VS 변형을 굽지 않는다" ).c_str() );
            if ( sw::FrameRendererUtil::hasPixelStage( passType ) )
                SW_EXPECT_TRUE_MSG( hasExactRequestInternal( listRequest, passShader._shaderPath, "PSMain", sw::ShaderStage::Pixel, permutationHash ),
                                    ( label + ": 런타임이 만드는 PS 변형을 굽지 않는다" ).c_str() );
            ++checkedVariantCount;
        }
    }
    SW_EXPECT_TRUE_MSG( checkedVariantCount > 0, "씬 메시 패스 종류가 하나도 없다 — 이 시험이 아무것도 보지 않는다" );
}

/**
 * @brief [ShaderBakeRequestTest] 구운 리플렉션 매니페스트(배포 팩에 실리는 것)가 베이커의 요청을 모두 담는다
 * @details 위 시험들은 "요청 ⊇ 런타임이 요구하는 것" 을 본다. 여기는 "구운 것 ⊇ 요청" — 요청 규칙을 바꾸고 `App.exe --bake-shaders` 를
 *          빠뜨리면 커밋된 산출물에 그 조합이 없어 Shipping 이 PSO 를 못 만든다. 매니페스트 키는 베이커의 바이너리 파일 이름이다.
 *          배포본은 고른 백엔드의 매니페스트만 실으므로 없는 백엔드 폴더는 건너뛰되, 하나는 있어야 한다.
 */
SW_TEST_CASE( ShaderBakeRequestTest, BakedManifestHoldsEveryRequest )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    sw::vector<sw::ShaderBakeRequest> listRequest;
    sw::ShaderBakeDriver::collectAllRequests( sw::ResourceUtil::getRootFolderPath(), listRequest );
    SW_ASSERT_TRUE( listRequest.empty() == false );

    const sw::ShaderTargetFormat arrFormat[] = { sw::ShaderTargetFormat::DXBC_D3D11, sw::ShaderTargetFormat::DXIL_D3D12,
                                                 sw::ShaderTargetFormat::SPIRV_Vulkan, sw::ShaderTargetFormat::SPIRV_OpenGL };

    // (bin 폴더 → 매니페스트) 캐시. 매니페스트가 없는 폴더는 빈 맵이 아니라 "없음" 으로 남긴다.
    sw::unordered_map<sw::string, sw::ShaderReflectionLibrary::EntryMap> mapManifest;
    sw::unordered_map<sw::string, bool>                                  mapManifestLoaded;

    uint32 checkedCount{ 0 };
    uint32 missingCount{ 0 };
    for ( const sw::ShaderBakeRequest& request : listRequest )
    {
        const size_t shaderPos = request._shaderPath.find( "shaders/" );
        if ( shaderPos == sw::string::npos )
            continue;
        const sw::string shaderDir = request._shaderPath.substr( 0, shaderPos + sizeof( "shaders/" ) - 1 );
        const sw::string stemLower = sw::ShaderBaker::getStemLower( request._shaderPath );

        for ( const sw::ShaderTargetFormat format : arrFormat )
        {
            const sw::string binDir = shaderDir + "bin/" + sw::string( sw::ShaderBaker::getSubfolderForFormat( format ) );
            if ( mapManifestLoaded.find( binDir ) == mapManifestLoaded.end() )
                mapManifestLoaded[binDir] = sw::ShaderReflectionLibrary::loadManifest( binDir, mapManifest[binDir] );
            if ( mapManifestLoaded[binDir] == false )
                continue;

            const sw::string key = sw::ShaderBaker::computeBinaryFileName( stemLower, request._stage, request._entryPoint, request._permutationHash,
                                                                           sw::ShaderBaker::getExtensionForFormat( format ) );
            ++checkedCount;
            if ( mapManifest[binDir].find( key ) == mapManifest[binDir].end() )
            {
                // 요청 규칙 하나가 어긋나면 수백 줄이 빠진다 — 앞의 몇 개만 이름을 남기고 나머지는 수로 센다.
                constexpr uint32 kReportedMissingLimit = 8;
                if ( missingCount < kReportedMissingLimit )
                    SW_EXPECT_TRUE_MSG( false, ( binDir + " 매니페스트에 '" + key + "' (" + request._shaderPath +
                                                 ") 가 없다 — App.exe --bake-shaders 로 다시 굽고 산출물을 커밋할 것" )
                                                   .c_str() );
                ++missingCount;
            }
        }
    }
    SW_EXPECT_TRUE_MSG( checkedCount > 0, "구운 매니페스트를 하나도 찾지 못했다 — 이 시험이 아무것도 보지 않는다" );
    SW_EXPECT_EQUAL( 0u, missingCount );
}

/**
 * @brief [ShaderBakeRequestTest] 커밋된 `shaders/bin/<rhi>` 폴더에는 지금 요청이 만드는 바이너리만 있다
 * @details 위 시험의 반대쪽 — "구운 것 ⊆ 요청". 베이커는 매니페스트를 요청으로 새로 쓰지만 요청에서 빠진 바이너리 파일은 지우지 않는다.
 *          셰이더나 구울 목록 줄을 지우고 그 바이너리를 남기면 매니페스트에 없는 파일이 배포 팩에 실린다.
 */
SW_TEST_CASE( ShaderBakeRequestTest, BakedFoldersHoldOnlyRequestedBinaries )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    const sw::string& rootDir = sw::ResourceUtil::getRootFolderPath();
    SW_ASSERT_TRUE( rootDir.empty() == false );

    sw::vector<sw::ShaderBakeRequest> listRequest;
    sw::ShaderBakeDriver::collectAllRequests( rootDir, listRequest );
    SW_ASSERT_TRUE( listRequest.empty() == false );

    const sw::ShaderTargetFormat arrFormat[] = { sw::ShaderTargetFormat::DXBC_D3D11, sw::ShaderTargetFormat::DXIL_D3D12,
                                                 sw::ShaderTargetFormat::SPIRV_Vulkan, sw::ShaderTargetFormat::SPIRV_OpenGL };

    // 베이커(ShaderBakeDriver::bakeAllShaders)와 같은 규칙으로 요청마다 바이너리의 절대 경로를 만든다.
    sw::unordered_set<sw::string> uniqueExpectedBinary;
    for ( const sw::ShaderBakeRequest& request : listRequest )
    {
        const sw::string absPath = sw::FileUtil::fileExists( request._shaderPath ) ? request._shaderPath
                                                                                   : sw::ResourceUtil::getResourcePath( request._shaderPath );
        if ( sw::FileUtil::fileExists( absPath ) == false )
            continue;
        const sw::string normPath  = sw::FileUtil::normalizeSeparators( absPath );
        const size_t     shaderPos = normPath.find( "/shaders/" );
        if ( shaderPos == sw::string::npos )
            continue;
        const sw::string binRoot   = normPath.substr( 0, shaderPos + sizeof( "/shaders" ) - 1 ) + "/bin";
        const sw::string stemLower = sw::ShaderBaker::getStemLower( normPath );
        for ( const sw::ShaderTargetFormat format : arrFormat )
        {
            const sw::string fileName = sw::ShaderBaker::computeBinaryFileName( stemLower, request._stage, request._entryPoint, request._permutationHash,
                                                                                sw::ShaderBaker::getExtensionForFormat( format ) );
            const sw::string binDir   = sw::FileUtil::joinPath( binRoot, sw::ShaderBaker::getSubfolderForFormat( format ) );
            uniqueExpectedBinary.insert( sw::FileUtil::normalizePath( sw::FileUtil::joinPath( binDir, fileName ) ) );
        }
    }
    SW_ASSERT_TRUE( uniqueExpectedBinary.empty() == false );

    sw::unordered_set<sw::string> uniqueExtension;
    for ( const sw::ShaderTargetFormat format : arrFormat )
        uniqueExtension.insert( sw::string( sw::ShaderBaker::getExtensionForFormat( format ) ) );

    uint32 checkedCount{ 0 };
    uint32 orphanCount{ 0 };
    for ( const sw::string& extension : uniqueExtension )
    {
        sw::vector<sw::string> listBinary;
        (void)sw::FileUtil::collectFiles( rootDir, extension, listBinary, true );
        for ( const sw::string& binaryPath : listBinary )
        {
            const sw::string normBinary = sw::FileUtil::normalizePath( binaryPath );
            if ( normBinary.find( "/shaders/bin/" ) == sw::string::npos )
                continue;
            ++checkedCount;
            if ( uniqueExpectedBinary.find( normBinary ) != uniqueExpectedBinary.end() )
                continue;
            constexpr uint32 kReportedOrphanLimit = 8;
            if ( orphanCount < kReportedOrphanLimit )
                SW_EXPECT_TRUE_MSG( false, ( binaryPath + " 를 만드는 요청이 없다 — 지운 셰이더 · 목록 줄의 바이너리를 같이 지울 것" ).c_str() );
            ++orphanCount;
        }
    }
    SW_EXPECT_TRUE_MSG( checkedCount > 0, "구운 바이너리를 하나도 찾지 못했다 — 이 시험이 아무것도 보지 않는다" );
    SW_EXPECT_EQUAL( 0u, orphanCount );
}
