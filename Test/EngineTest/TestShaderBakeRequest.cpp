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
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Renderer/Bake/ShaderBakeDriver.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassTypeTraits.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
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
