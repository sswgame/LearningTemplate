#include "pch.h"

#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/Support/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"
#include "Engine/Graphics/RHI/Support/RHIShaderRequest.h"

#include "TestFramework/TestFramework.h"

// Engine/Graphics/RHI/Support — 디바이스 없이 도는 RHI 보조 자료구조.
// 핸들 표의 세대 무효화 · 해제 큐의 지연과 펜스 · 셰이더 요청 해석.
// 디바이스를 만드는 케이스는 TestRHIDevice.cpp 에 있다 (CI 가 못 돌린다).
/**
 * @brief [RHIReleaseQueueTest] 지연 해제
 */

SW_TEST_CASE( RHIReleaseQueueTest, LatencyRelease )
{
    sw::RHIReleaseQueue queue( 3 );
    SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );

    bool                           bDestroyed{ false };
    sw::RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, [&bDestroyed]()
    {
        bDestroyed = true;
    } );

    queue.enqueueRelease( releaseDel );
    SW_EXPECT_EQUAL( 1u, queue.getPendingReleaseCount() );
    SW_EXPECT_FALSE( bDestroyed );

    queue.tickFrame();
    SW_EXPECT_FALSE( bDestroyed );

    queue.tickFrame();
    SW_EXPECT_FALSE( bDestroyed );

    queue.tickFrame();
    SW_EXPECT_TRUE( bDestroyed );
    SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

/**
 * @brief [RHIReleaseQueueTest] 전체 flush
 */
SW_TEST_CASE( RHIReleaseQueueTest, FlushAll )
{
    sw::RHIReleaseQueue            queue( 5 );
    int32                          releaseCount{ 0 };
    sw::RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, [&releaseCount]()
    {
        ++releaseCount;
    } );

    queue.enqueueRelease( releaseDel );
    queue.enqueueRelease( releaseDel );
    SW_EXPECT_EQUAL( 2u, queue.getPendingReleaseCount() );

    queue.flushAll();
    SW_EXPECT_EQUAL( 2, releaseCount );
    SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

/**
 * @brief [RHIHandleTable] generation이 올라가면 옛 핸들은 무효이고 슬롯은 재사용된다
 */
SW_TEST_CASE( RHIHandleTableTest, GenerationInvalidatesStaleHandles )
{
    sw::RHIHandleTable<uint32> table;
    const uint64               first = table.insert( 42u );
    SW_EXPECT_TRUE( first != 0 );
    uint32* slot = table.get( first );
    SW_ASSERT_NOT_NULL( slot );
    SW_EXPECT_EQUAL( 42u, *slot );

    uint32 taken{ 0 };
    SW_EXPECT_TRUE( table.take( first, taken ) );
    SW_EXPECT_EQUAL( 42u, taken );
    SW_EXPECT_TRUE( table.get( first ) == nullptr );

    const uint64 second = table.insert( 99u );
    SW_EXPECT_TRUE( second != 0 );
    SW_EXPECT_TRUE( second != first );
    SW_EXPECT_TRUE( table.get( first ) == nullptr );
    uint32* reused = table.get( second );
    SW_ASSERT_NOT_NULL( reused );
    SW_EXPECT_EQUAL( 99u, *reused );
}

/**
 * @brief [RHIReleaseQueueTest] GPU 펜스가 완료되기 전에는 해제하지 않는다
 */
SW_TEST_CASE( RHIReleaseQueueTest, GpuFenceRelease )
{
    sw::RHIReleaseQueue            queue( 3 );
    bool                           bDestroyed{ false };
    sw::RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, [&bDestroyed]()
    {
        bDestroyed = true;
    } );

    queue.enqueueGpuRelease( releaseDel, 4 );
    SW_EXPECT_EQUAL( 1u, queue.getPendingReleaseCount() );
    queue.tickCompleted( 3 );
    SW_EXPECT_FALSE( bDestroyed );
    queue.tickFrame();
    SW_EXPECT_FALSE( bDestroyed );
    queue.tickCompleted( 4 );
    SW_EXPECT_TRUE( bDestroyed );
    SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

/**
 * @brief [RHIShaderRequestTest] 파이프라인 서술체를 컴파일 요청으로 해석하는 규칙 — 백엔드 넷이 각자 갖던 것.
 * @details 진입점 기본값, define 은 두 스테이지에, RT 0 개 + 뎁스 테스트면 픽셀 스테이지 없음(경로가 있어도),
 *          RT 수는 뎁스 전용 0 / 그 밖 1 이상. 네 곳에 복사돼 있을 때 Vulkan 은 define 을, DX12 는 뎁스 전용 판정을
 *          빠뜨렸다 — 규칙이 한 곳이면 빠질 자리가 없다. GPU 가 필요 없다(nogpu).
 */
SW_TEST_CASE( RHIShaderRequestTest, ResolvesEntryPointsDefinesAndDepthOnly )
{
    sw::RHIPipelineStateDesc desc{};
    desc._vertexShaderPath = "engine/shaders/forwardlit.hlsl";
    desc._pixelShaderPath  = "engine/shaders/forwardlit.hlsl";
    desc._listShaderDefine = { "SW_FORWARD=1", "FOO" };

    // 1) 기본: 진입점 기본값, define 은 두 스테이지에, RT 1.
    const sw::RHIGraphicsShaderRequest plain = sw::RHIShaderRequest::resolveGraphics( desc, sw::ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_TRUE( plain._bHasPixelShader != SW_FALSE );
    SW_EXPECT_TRUE( plain._bDepthOnly == SW_FALSE );
    SW_EXPECT_EQUAL( 1u, plain._numRenderTargets );
    SW_EXPECT_STREQ( "VSMain", plain._vertex._entryPoint.c_str() );
    SW_EXPECT_STREQ( "PSMain", plain._pixel._entryPoint.c_str() );
    SW_EXPECT_TRUE( plain._vertex._stage == sw::ShaderStage::Vertex && plain._pixel._stage == sw::ShaderStage::Pixel );
    SW_EXPECT_TRUE( plain._vertex._targetFormat == sw::ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_EQUAL( 2u, plain._vertex._listDefine.size() );
    SW_EXPECT_EQUAL( 2u, plain._pixel._listDefine.size() );
    SW_EXPECT_STREQ( "FOO", plain._pixel._listDefine[1]._name.c_str() );
    SW_EXPECT_STREQ( "1", plain._pixel._listDefine[1]._value.c_str() ); // 값 없는 define 은 1

    // 2) 명시한 진입점은 그대로.
    desc._vertexEntryPoint                   = "MyVS";
    desc._pixelEntryPoint                    = "MyPS";
    const sw::RHIGraphicsShaderRequest named = sw::RHIShaderRequest::resolveGraphics( desc, sw::ShaderTargetFormat::SPIRV_Vulkan );
    SW_EXPECT_STREQ( "MyVS", named._vertex._entryPoint.c_str() );
    SW_EXPECT_STREQ( "MyPS", named._pixel._entryPoint.c_str() );

    // 3) 뎁스 전용(RT 0 + 뎁스 테스트): 경로가 있어도 PS 없음, RT 0.
    desc._numRenderTargets                       = 0;
    desc._bEnableDepthTest                       = SW_TRUE;
    const sw::RHIGraphicsShaderRequest depthOnly = sw::RHIShaderRequest::resolveGraphics( desc, sw::ShaderTargetFormat::DXBC_D3D11 );
    SW_EXPECT_TRUE( depthOnly._bDepthOnly != SW_FALSE );
    SW_EXPECT_TRUE( depthOnly._bHasPixelShader == SW_FALSE );
    SW_EXPECT_EQUAL( 0u, depthOnly._numRenderTargets );

    // 4) RT 0 인데 뎁스 테스트가 없으면 뎁스 전용이 아니다 — RT 는 1 로 올리고 PS 는 붙는다.
    desc._bEnableDepthTest                     = SW_FALSE;
    const sw::RHIGraphicsShaderRequest noDepth = sw::RHIShaderRequest::resolveGraphics( desc, sw::ShaderTargetFormat::SPIRV_OpenGL );
    SW_EXPECT_TRUE( noDepth._bDepthOnly == SW_FALSE );
    SW_EXPECT_TRUE( noDepth._bHasPixelShader != SW_FALSE );
    SW_EXPECT_EQUAL( 1u, noDepth._numRenderTargets );

    // 5) PS 경로가 비면 PS 없음 (RT 는 그대로).
    desc._numRenderTargets = 2;
    desc._pixelShaderPath.clear();
    const sw::RHIGraphicsShaderRequest noPs = sw::RHIShaderRequest::resolveGraphics( desc, sw::ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_TRUE( noPs._bHasPixelShader == SW_FALSE );
    SW_EXPECT_EQUAL( 2u, noPs._numRenderTargets );
}

/**
 * @brief [RHIIndexFreeListTest] 이미 빈 슬롯을 다시 반납하면 **거절한다**
 * @details 거절하지 않으면 프리리스트에 같은 인덱스가 **두 번** 들어가고, 다음 두 번의 할당이
 *          그 인덱스를 **서로 다른 리소스에 발급한다**. 그 뒤로는 한쪽이 다른 쪽의 디스크립터를
 *          덮어쓴다.
 *
 *          이 가드는 원래 백엔드마다 손으로 적혀 있었고 종류마다 모양이 달랐다 — DX11 · GL 의
 *          buffer/texture 는 가드 + 에러 로그, DX11 의 uav 는 조용한 반환, **GL 의 uav 는 가드가
 *          아예 없었다.** 그래서 GL 에서만 UAV 이중 해제가 통과했다. 가드를 이 헬퍼 한 자리로
 *          옮겨 지금 쓰는 곳과 앞으로 쓸 곳이 같이 막히게 했다.
 */
SW_TEST_CASE( RHIIndexFreeListTest, DoubleReleaseIsRejected )
{
    sw::vector<uint32> listRegistered;
    sw::vector<uint32> listFree;

    const uint32 indexA = sw::allocateFreeListIndex( listRegistered, listFree, 100u );
    const uint32 indexB = sw::allocateFreeListIndex( listRegistered, listFree, 200u );
    SW_ASSERT_TRUE( indexA != indexB );

    // 한 번 반납하면 그 자리가 프리리스트로 간다.
    SW_EXPECT_EQUAL( 100u, sw::releaseFreeListIndex( listRegistered, listFree, indexA, 0u, "test" ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listFree.size() );

    // **두 번째 반납은 거절된다** — 안 그러면 프리리스트에 같은 인덱스가 둘이 된다.
    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_EQUAL( 0u, sw::releaseFreeListIndex( listRegistered, listFree, indexA, 0u, "test" ) );
    }
    SW_EXPECT_TRUE_MSG( listFree.size() == 1, "이중 해제가 프리리스트에 같은 인덱스를 두 번 넣었습니다" );

    // 그래서 다음 두 할당이 서로 다른 인덱스를 받는다.
    const uint32 reused = sw::allocateFreeListIndex( listRegistered, listFree, 300u );
    const uint32 fresh  = sw::allocateFreeListIndex( listRegistered, listFree, 400u );
    SW_EXPECT_EQUAL( indexA, reused );
    SW_EXPECT_TRUE_MSG( fresh != reused, "같은 인덱스가 두 리소스에 발급됐습니다" );
    SW_EXPECT_TRUE( fresh != indexB );

    // 범위 밖은 조용히 무시한다(로그도 남기지 않는다) — 예전 동작 그대로다.
    SW_EXPECT_EQUAL( 0u, sw::releaseFreeListIndex( listRegistered, listFree, 9999u, 0u, "test" ) );
}
