/**
 * @file TestRenderPSOCacheShaderRelease.cpp
 * @brief 셰이더 핫 리로드가 그 셰이더를 쓰는 PSO 만 놓는가 — 패스 자원 · 다른 셰이더의 PSO 는 그대로다(`RenderPSOCache::releasePSOsUsingShader`).
 * @details 디바이스는 가짜(`test::FakeRHIDevice`)이고 팩토리(`test::FakeRHIBufferFactory`)가 파괴한 PSO 를 적는다. desc 는 레이아웃 없이 적는다(`registerDesc` — 리플렉션이 필요 없다).
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"

using namespace sw;

namespace
{
    RHIPipelineStateDesc makeGraphicsDesc( const utf8* pShaderPath )
    {
        RHIPipelineStateDesc desc{};
        desc._vertexShaderPath = pShaderPath;
        desc._pixelShaderPath  = pShaderPath;
        return desc;
    }
} // namespace

/**
 * @brief [RenderPSOCacheShaderReleaseTest] 바뀐 셰이더의 PSO 만 파괴하고, 패스 PSO 를 놓으면 그 패스에서 나온 머티리얼 변형도 놓는다
 * @details 변이: 빈 경로를 일치로 보면(컴퓨트 desc 의 빈 정점 경로) 첫 놓기에서 엔진 PSO 둘이 함께 사라져 첫 단언 묶음이 진다.
 *          패스 PSO 를 놓을 때 변형을 남기면 변형 PSO(4)가 파괴되지 않아 둘째 묶음이 진다.
 */
SW_TEST_CASE( RenderPSOCacheShaderReleaseTest, ReleasesOnlyPSOsOfTheChangedShader )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory = &factory;

    RenderPSOCache cache;
    cache.registerDesc( 1, makeGraphicsDesc( "shaders/forward.hlsl" ) );
    cache.setEnginePSO( RenderPassKind::ForwardOpaque, 1 );
    cache.registerDesc( 2, makeGraphicsDesc( "shaders/shadow.hlsl" ) );
    cache.setEnginePSO( RenderPassKind::Shadow, 2 );
    RHIPipelineStateDesc computeDesc{};
    computeDesc._computeShaderPath = "shaders/blur_cs.hlsl";
    cache.registerDesc( 3, computeDesc );
    const hashed_string computePassName{ "Blur" };
    cache.setExtensionProgramPSO( computePassName, 3 );
    // 머티리얼 변형: 만든 것(4 — 그림자 셰이더) · 실패한 것 · 만드는 중인 것.
    cache.registerDesc( 4, makeGraphicsDesc( "shaders/shadow.hlsl" ) );
    cache.setMaterialPSO( 100, RenderPSOCache::MaterialPSOEntry::ready( 4, true ) );
    cache.setMaterialPSO( 101, RenderPSOCache::MaterialPSOEntry::failed() );
    cache.setMaterialPSO( 102, RenderPSOCache::MaterialPSOEntry::building() );

    // 컴퓨트 셰이더 하나 — 그 PSO 만 놓고, 실패한 변형은 다시 만들어 보게 지운다. 패스 PSO · 만든 변형 · 만드는 중인 변형은 그대로다.
    SW_EXPECT_EQUAL( 1u, cache.releasePSOsUsingShader( &device, "engine:shaders/blur_cs.hlsl" ) );
    SW_EXPECT_TRUE( factory._listDestroyedPipelineState == vector<RHIPipelineStateHandle>{ 3 } );
    RHIPipelineStateHandle found{ 0 };
    SW_EXPECT_FALSE( cache.findExtensionProgramPSO( computePassName, found ) );
    SW_EXPECT_EQUAL( RHIPipelineStateHandle{ 1 }, cache.findEnginePSO( RenderPassKind::ForwardOpaque ) );
    SW_EXPECT_EQUAL( RHIPipelineStateHandle{ 2 }, cache.findEnginePSO( RenderPassKind::Shadow ) );
    SW_EXPECT_EQUAL( RHIPipelineStateHandle{ 4 }, cache.findMaterialPSO( 100 ) );
    SW_EXPECT_TRUE( cache.findMaterialPSOState( 101 ) == RenderPSOCache::MaterialPSOState::Absent );
    SW_EXPECT_TRUE( cache.findMaterialPSOState( 102 ) == RenderPSOCache::MaterialPSOState::Building );

    // 포워드 셰이더 — 그 패스 PSO 를 놓고, 변형 키에 패스 PSO 핸들이 들어 있으니 만든 변형도 놓는다. 그림자 패스 PSO 는 그대로다.
    factory._listDestroyedPipelineState.clear();
    SW_EXPECT_EQUAL( 2u, cache.releasePSOsUsingShader( &device, "shaders/forward.hlsl" ) );
    SW_EXPECT_TRUE( VectorUtil::contains( factory._listDestroyedPipelineState, RHIPipelineStateHandle{ 1 } ) );
    SW_EXPECT_TRUE( VectorUtil::contains( factory._listDestroyedPipelineState, RHIPipelineStateHandle{ 4 } ) );
    SW_EXPECT_EQUAL( RHIPipelineStateHandle{ 0 }, cache.findEnginePSO( RenderPassKind::ForwardOpaque ) );
    SW_EXPECT_EQUAL( RHIPipelineStateHandle{ 2 }, cache.findEnginePSO( RenderPassKind::Shadow ) );
    SW_EXPECT_TRUE( cache.findMaterialPSOState( 100 ) == RenderPSOCache::MaterialPSOState::Absent );
    SW_EXPECT_TRUE( cache.findMaterialPSOState( 102 ) == RenderPSOCache::MaterialPSOState::Building );
    RHIPipelineStateDesc desc{};
    SW_EXPECT_FALSE( cache.findDesc( 1, desc ) );
    SW_EXPECT_TRUE( cache.findDesc( 2, desc ) );
}
