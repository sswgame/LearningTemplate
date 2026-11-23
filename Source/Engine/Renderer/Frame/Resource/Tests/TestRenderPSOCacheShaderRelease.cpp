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

    /** @brief 서로 다른 변형 키입니다(퍼뮤테이션 해시만 다르다). */
    MaterialPSOKey makeVariantKey( uint64 permutationHash )
    {
        MaterialPSOKey key{};
        key._passPSO         = RHIPipelineStateHandle{ 1 };
        key._permutationHash = permutationHash;
        return key;
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

    const RHIPipelineStateHandle kForwardPSO{ 1 };
    const RHIPipelineStateHandle kShadowPSO{ 2 };
    const RHIPipelineStateHandle kBlurPSO{ 3 };
    const RHIPipelineStateHandle kVariantPSO{ 4 };

    RenderPSOCache cache;
    cache.registerDesc( kForwardPSO, makeGraphicsDesc( "shaders/forward.hlsl" ) );
    cache.setEnginePSO( &device, RenderPassKind::ForwardOpaque, kForwardPSO );
    cache.registerDesc( kShadowPSO, makeGraphicsDesc( "shaders/shadow.hlsl" ) );
    cache.setEnginePSO( &device, RenderPassKind::Shadow, kShadowPSO );
    RHIPipelineStateDesc computeDesc{};
    computeDesc._computeShaderPath = "shaders/blur_cs.hlsl";
    cache.registerDesc( kBlurPSO, computeDesc );
    const hashed_string computePassName{ "Blur" };
    cache.setExtensionProgramPSO( &device, computePassName, kBlurPSO );
    // 머티리얼 변형: 만든 것(4 — 그림자 셰이더) · 실패한 것 · 만드는 중인 것.
    cache.registerDesc( kVariantPSO, makeGraphicsDesc( "shaders/shadow.hlsl" ) );
    cache.setMaterialPSO( makeVariantKey( 100 ), RenderPSOCache::MaterialPSOEntry::ready( kVariantPSO, true ) );
    cache.setMaterialPSO( makeVariantKey( 101 ), RenderPSOCache::MaterialPSOEntry::failed() );
    cache.setMaterialPSO( makeVariantKey( 102 ), RenderPSOCache::MaterialPSOEntry::building() );

    // 컴퓨트 셰이더 하나 — 그 PSO 만 놓고, 실패한 변형 · 만드는 중인 변형은 다시 만들게 지운다. 패스 PSO · 만든 변형은 그대로다.
    SW_EXPECT_EQUAL( 1u, cache.releasePSOsUsingShader( &device, "engine:shaders/blur_cs.hlsl" ) );
    SW_EXPECT_TRUE( factory._listDestroyedPipelineState == vector<RHIPipelineStateHandle>{ kBlurPSO } );
    RHIPipelineStateHandle found{ 0 };
    SW_EXPECT_FALSE( cache.findExtensionProgramPSO( computePassName, found ) );
    SW_EXPECT_EQUAL( kForwardPSO, cache.findEnginePSO( RenderPassKind::ForwardOpaque ) );
    SW_EXPECT_EQUAL( kShadowPSO, cache.findEnginePSO( RenderPassKind::Shadow ) );
    SW_EXPECT_EQUAL( kVariantPSO, cache.findMaterialPSO( makeVariantKey( 100 ) ) );
    SW_EXPECT_TRUE( cache.findMaterialPSOState( makeVariantKey( 101 ) ) == RenderPSOCache::MaterialPSOState::Absent );
    SW_EXPECT_TRUE( cache.findMaterialPSOState( makeVariantKey( 102 ) ) == RenderPSOCache::MaterialPSOState::Absent );

    // 포워드 셰이더 — 그 패스 PSO 를 놓고, 변형 키에 패스 PSO 핸들이 들어 있으니 만든 변형도 놓는다. 그림자 패스 PSO 는 그대로다.
    factory._listDestroyedPipelineState.clear();
    SW_EXPECT_EQUAL( 2u, cache.releasePSOsUsingShader( &device, "shaders/forward.hlsl" ) );
    SW_EXPECT_TRUE( VectorUtil::contains( factory._listDestroyedPipelineState, kForwardPSO ) );
    SW_EXPECT_TRUE( VectorUtil::contains( factory._listDestroyedPipelineState, kVariantPSO ) );
    SW_EXPECT_EQUAL( RHIPipelineStateHandle{}, cache.findEnginePSO( RenderPassKind::ForwardOpaque ) );
    SW_EXPECT_EQUAL( kShadowPSO, cache.findEnginePSO( RenderPassKind::Shadow ) );
    SW_EXPECT_TRUE( cache.findMaterialPSOState( makeVariantKey( 100 ) ) == RenderPSOCache::MaterialPSOState::Absent );
    RHIPipelineStateDesc desc{};
    SW_EXPECT_FALSE( cache.findDesc( kForwardPSO, desc ) );
    SW_EXPECT_TRUE( cache.findDesc( kShadowPSO, desc ) );
}

/**
 * @brief [RenderPSOCacheShaderReleaseTest] 엔진 · 출력 · 모듈 PSO 를 다른 핸들로 덮어쓰면 옛 핸들을 장치의 해제 진입점으로 놓는다 — 같은 핸들을 다시 두면 놓지 않는다
 * @details 변이: 덮어쓰기가 옛 핸들을 잊기만 하면(예전) 파괴 목록이 비어 진다. 같은 핸들도 놓으면 쓰는 PSO 를 놓아 둘째 단언이 진다.
 */
SW_TEST_CASE( RenderPSOCacheShaderReleaseTest, OverwriteReleasesThePreviousPSO )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory = &factory;

    const RHIPipelineStateHandle kOld{ 11 };
    const RHIPipelineStateHandle kNew{ 12 };
    const hashed_string          programKey{ "Program" };
    RenderPSOCache               cache;
    cache.setEnginePSO( &device, RenderPassKind::Shadow, kOld );
    cache.setEnginePSO( &device, RenderPassKind::Shadow, kOld ); // 같은 핸들 — 놓지 않는다
    SW_EXPECT_TRUE( factory._listDestroyedPipelineState.empty() );
    cache.setEnginePSO( &device, RenderPassKind::Shadow, kNew );
    SW_EXPECT_TRUE( factory._listDestroyedPipelineState == vector<RHIPipelineStateHandle>{ kOld } );
    SW_EXPECT_EQUAL( kNew, cache.findEnginePSO( RenderPassKind::Shadow ) );

    factory._listDestroyedPipelineState.clear();
    cache.setOutputPSO( &device, RenderPassKind::Present, RHIFormat::R8G8B8A8_UNORM, kOld );
    cache.setOutputPSO( &device, RenderPassKind::Present, RHIFormat::R8G8B8A8_UNORM, kNew );
    cache.setExtensionProgramPSO( &device, programKey, kOld );
    cache.setExtensionProgramPSO( &device, programKey, RHIPipelineStateHandle{} ); // 실패(0)로 덮어도 옛 핸들은 놓는다
    SW_EXPECT_TRUE( factory._listDestroyedPipelineState == ( vector<RHIPipelineStateHandle>{ kOld, kOld } ) );
}

/**
 * @brief [RenderPSOCacheTest] 변형 키는 축마다 값으로 견준다 — 축 하나만 달라도 다른 변형이다
 * @details 변이: 비교에서 축 하나(보기 모드 · 정점 형식 · 컬 반전)를 빼면 그 축의 단언이 진다.
 */
SW_TEST_CASE( RenderPSOCacheTest, MaterialVariantKeyComparesEveryAxis )
{
    RenderPSOCache cache;
    SW_EXPECT_TRUE( cache.beginMaterialPSO( makeVariantKey( 7 ) ) );
    SW_EXPECT_FALSE( cache.beginMaterialPSO( makeVariantKey( 7 ) ) );
    MaterialPSOKey viewMode  = makeVariantKey( 7 );
    viewMode._viewMode       = RenderViewMode::Wireframe;
    MaterialPSOKey vertex    = makeVariantKey( 7 );
    vertex._vertexFormat     = RHIMeshVertexFormat::HighPrecision;
    MaterialPSOKey culling   = makeVariantKey( 7 );
    culling._bReverseCulling = SW_TRUE;
    MaterialPSOKey passPSO   = makeVariantKey( 7 );
    passPSO._passPSO         = RHIPipelineStateHandle{ 2 };
    SW_EXPECT_TRUE( cache.beginMaterialPSO( viewMode ) );
    SW_EXPECT_TRUE( cache.beginMaterialPSO( vertex ) );
    SW_EXPECT_TRUE( cache.beginMaterialPSO( culling ) );
    SW_EXPECT_TRUE( cache.beginMaterialPSO( passPSO ) );
    SW_EXPECT_EQUAL( size_t{ 5 }, cache.countMaterialPSOs( RenderPSOCache::MaterialPSOState::Building ) );
}

/**
 * @brief [RenderPSOCacheShaderReleaseTest] 핫 리로드 앞에 넣은 변형 생성은 받지 않는다 — 줄은 비고, 그 키는 새로 시작할 수 있다
 * @details 변이: 만드는 중인 변형을 남기면(예전) 옛 바이트코드로 만든 PSO 가 그 키의 답이 되고, 패스 PSO 를 놓은 뒤에는 찾는 쪽 없는 옛 키 항목이 쌓인다.
 */
SW_TEST_CASE( RenderPSOCacheShaderReleaseTest, DiscardsVariantsQueuedBeforeReload )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory = &factory;

    RenderPSOCache cache;
    SW_REQUIRE_TRUE( cache.beginMaterialPSO( makeVariantKey( 7 ) ) );
    cache.requestMaterialPSO( &device, makeVariantKey( 7 ), makeGraphicsDesc( "shaders/forward.hlsl" ), true, false ); // 줄에만 있다(부른 스레드가 만드는 길)
    SW_EXPECT_EQUAL( 1u, cache.getPendingMaterialPSOBuilds() );
    SW_EXPECT_EQUAL( 0u, cache.releasePSOsUsingShader( &device, "shaders/forward.hlsl" ) );
    SW_EXPECT_EQUAL( 0u, cache.getPendingMaterialPSOBuilds() );
    SW_EXPECT_TRUE( cache.findMaterialPSOState( makeVariantKey( 7 ) ) == RenderPSOCache::MaterialPSOState::Absent );
    SW_EXPECT_EQUAL( 0u, cache.pollMaterialPSOBuilds( &device, false, 0u ) );
    SW_EXPECT_TRUE( cache.findMaterialPSOState( makeVariantKey( 7 ) ) == RenderPSOCache::MaterialPSOState::Absent );
    SW_EXPECT_TRUE( cache.beginMaterialPSO( makeVariantKey( 7 ) ) );
}
