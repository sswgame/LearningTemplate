/**
 * @file TestRHIBackendSupport.cpp
 * @brief 네 백엔드가 함께 쓰는 보조 자료구조(`RHI/Backend`) 테스트 — 해제 큐의 지연과 펜스, 핸들 표의 세대 무효화, 셰이더 요청 해석, 상수 버퍼 링 거울입니다.
 */
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Backend/RHIBindlessRegistry.h"
#include "Engine/Graphics/RHI/Backend/RHIConstantBufferMirror.h"
#include "Engine/Graphics/RHI/Backend/RHIFrameFenceRing.h"
#include "Engine/Graphics/RHI/Backend/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Backend/RHIReleaseQueue.h"
#include "Engine/Graphics/RHI/Backend/RHIShaderRequest.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"

#include <thread>

using namespace sw;

/**
 * @brief [RHIReleaseQueueTest] `flushAll` 은 펜스와 무관하게 남은 해제를 모두 부른다
 */
SW_TEST_CASE( RHIReleaseQueueTest, FlushAll )
{
    RHIReleaseQueue            queue;
    int32                      releaseCount{ 0 };
    RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [&releaseCount]()
    { ++releaseCount; } );

    queue.enqueueGPURelease( releaseDel, 7 );
    queue.enqueueGPURelease( releaseDel, 9 );
    SW_EXPECT_EQUAL( 2u, queue.getPendingReleaseCount() );

    queue.flushAll();
    SW_EXPECT_EQUAL( 2, releaseCount );
    SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

/**
 * @brief [RHIReleaseQueueTest] GPU 펜스가 그 값에 닿기 전에는 해제하지 않는다
 */
SW_TEST_CASE( RHIReleaseQueueTest, GPUFenceRelease )
{
    RHIReleaseQueue            queue;
    bool                       bDestroyed{ false };
    RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [&bDestroyed]()
    { bDestroyed = true; } );

    queue.enqueueGPURelease( releaseDel, 4 );
    SW_EXPECT_EQUAL( 1u, queue.getPendingReleaseCount() );
    queue.tickCompleted( 3 );
    SW_EXPECT_FALSE( bDestroyed );
    queue.tickCompleted( 4 );
    SW_EXPECT_TRUE( bDestroyed );
    SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

/**
 * @brief [RHIReleaseQueueTest] 두 스레드가 tickCompleted 를 동시에 불러도 콜백이 한 번씩만 돈다
 * @details DX12 는 렌더 스레드의 endFrame 과 리소스를 만드는 스레드의 업로드 펜스 대기가 둘 다 tickCompleted 를 부른다.
 *          스크래치 목록을 잠금 없이 바꾸던 때는 두 tick 이 같은 벡터를 동시에 만졌다.
 */
SW_TEST_CASE( RHIReleaseQueueTest, ConcurrentTicksRunEachCallbackOnce )
{
    constexpr uint32 kCount = 2000;
    RHIReleaseQueue  queue;
    atomic<uint32>   calledCount{ 0 };
    for ( uint32 index = 0; index < kCount; ++index )
    {
        RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [&calledCount]()
        { calledCount.fetch_add( 1u, std::memory_order_relaxed ); } );
        queue.enqueueGPURelease( releaseDel, index );
    }
    std::thread otherThread( [&queue]()
    {
        for ( uint32 index = 0; index < kCount; ++index )
        {
            queue.tickCompleted( index );
        }
    } );
    for ( uint32 index = 0; index < kCount; ++index )
    {
        queue.tickCompleted( index );
    }
    otherThread.join();
    SW_EXPECT_EQUAL( kCount, calledCount.load() );
    SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

/**
 * @brief [RHIBindlessRegistryTest] 펜스 뒤 재사용 칸은 finishRelease 전에는 다시 나가지 않고, 이중 해제 · 상한 초과는 거절한다
 */
SW_TEST_CASE( RHIBindlessRegistryTest, DefersReuseAndRejectsDoubleRelease )
{
    RHIBindlessRegistry<uint64> registry( 2 );
    const RHIDescriptorIndex    first  = registry.allocate( 11 );
    const RHIDescriptorIndex    second = registry.allocate( 22 );
    SW_EXPECT_EQUAL( 0u, first );
    SW_EXPECT_EQUAL( 1u, second );
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, registry.allocate( 33 ) ); // 상한
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, registry.allocate( 0 ) );  // 빈 값

    SW_EXPECT_TRUE( registry.release( first, RHIBindlessReuse::AfterFence ) );
    SW_EXPECT_FALSE( registry.isLive( first ) );
    SW_EXPECT_FALSE( registry.release( first, RHIBindlessReuse::AfterFence ) ); // 이중 해제
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, registry.allocate( 44 ) );        // 대기 칸은 아직 나가지 않는다
    registry.finishRelease( first );
    SW_EXPECT_EQUAL( first, registry.allocate( 44 ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 44 ), registry.get( first ) );

    // 쓰기 훅이 거절하면 칸을 바로 돌려받는다.
    SW_EXPECT_TRUE( registry.release( second, RHIBindlessReuse::Immediately ) );
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, registry.allocate( 55, []( RHIDescriptorIndex )
    { return false; } ) );
    SW_EXPECT_EQUAL( second, registry.allocate( 55 ) );

    uint32 releasedCount{ 0 };
    registry.releaseIf( RHIBindlessReuse::Immediately, []( uint64 value )
    { return value == 55; },
                        [&releasedCount]( RHIDescriptorIndex )
    { ++releasedCount; } );
    SW_EXPECT_EQUAL( 1u, releasedCount );
    SW_EXPECT_FALSE( registry.isLive( second ) );
}

/**
 * @brief [RHIFrameFenceRingTest] 칸이 한 바퀴 돌면 가장 오래된 프레임을 기다리고, 끝난 프레임만 오래된 순서로 거둔다
 */
SW_TEST_CASE( RHIFrameFenceRingTest, BoundsFramesInFlightAndRetiresInOrder )
{
    constexpr uint32  kSlots = RHIFrameFenceRing::kSlotCount;
    RHIFrameFenceRing ring;
    SW_EXPECT_EQUAL( static_cast<uint64>( 1 ), ring.getRecordingSerial() );
    SW_EXPECT_TRUE( ring.isIdle() );

    uint32 waitCount{ 0 };
    for ( uint32 frame = 0; frame < kSlots; ++frame )
    {
        SW_EXPECT_TRUE( ring.waitForRecordingSlot( [&waitCount]( uint32, uint64 )
        { ++waitCount; return true; } ) );
        SW_EXPECT_EQUAL( frame, ring.getRecordingSlot() );
        SW_EXPECT_EQUAL( static_cast<uint64>( frame + 1 ), ring.markSubmitted() );
    }
    SW_EXPECT_EQUAL( 0u, waitCount ); // 칸이 모두 처음 쓰인다
    SW_EXPECT_EQUAL( static_cast<uint64>( 1 ), ring.getRecordingSlotBusySerial() );

    // 2 번이 끝났다고 해도 1 번이 안 끝났으면 워터마크는 그대로다(순서대로만 거둔다).
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), ring.retire( []( uint32, uint64 serial )
    { return serial != 1; } ) );

    uint64 waitedSerial{ 0 };
    SW_EXPECT_TRUE( ring.waitForRecordingSlot( [&waitedSerial]( uint32 slot, uint64 serial )
    {
        waitedSerial = serial;
        return slot == RHIFrameFenceRing::getSlot( serial );
    } ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 1 ), waitedSerial );
    SW_EXPECT_EQUAL( static_cast<uint64>( 1 ), ring.getCompletedSerial() );
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), ring.getRecordingSlotBusySerial() );

    ring.markAllCompleted();
    SW_EXPECT_TRUE( ring.isIdle() );
    ring.markCompleted( 1 ); // 뒤로 가지 않는다
    SW_EXPECT_EQUAL( static_cast<uint64>( kSlots ), ring.getCompletedSerial() );
}

/**
 * @brief [RHIHandleTableTest] 세대가 올라가면 옛 핸들은 무효이고 슬롯은 재사용된다
 */
SW_TEST_CASE( RHIHandleTableTest, GenerationInvalidatesStaleHandles )
{
    RHIHandleTable<uint32> table;
    const uint64           first = table.insert( 42u );
    SW_EXPECT_TRUE( first != 0 );
    uint32* pSlot = table.get( first );
    SW_REQUIRE_NOT_NULL( pSlot );
    SW_EXPECT_EQUAL( 42u, *pSlot );

    uint32 taken{ 0 };
    SW_EXPECT_TRUE( table.take( first, taken ) );
    SW_EXPECT_EQUAL( 42u, taken );
    SW_EXPECT_TRUE( table.get( first ) == nullptr );

    const uint64 second = table.insert( 99u );
    SW_EXPECT_TRUE( second != 0 );
    SW_EXPECT_TRUE( second != first );
    SW_EXPECT_TRUE( table.get( first ) == nullptr );
    uint32* pReused = table.get( second );
    SW_REQUIRE_NOT_NULL( pReused );
    SW_EXPECT_EQUAL( 99u, *pReused );
}

/**
 * @brief [RHIShaderRequestTest] 파이프라인 서술체를 컴파일 요청으로 해석하는 규칙 — 네 백엔드가 같이 쓴다
 * @details 진입점 기본값, define 은 두 스테이지에, 픽셀 스테이지는 PS 경로가 있을 때만(뎁스 전용도 — 알파 컷오프),
 *          RT 수는 뎁스 전용 0 / 그 밖 1 이상. 백엔드마다 규칙을 복사하면 어느 하나가 define 이나 뎁스 전용 판정을
 *          빠뜨린다 — 규칙이 한 곳이면 빠질 자리가 없다. 경로는 읽지 않으므로 아무 글이나 된다.
 */
SW_TEST_CASE( RHIShaderRequestTest, ResolvesEntryPointsDefinesAndDepthOnly )
{
    RHIPipelineStateDesc desc{};
    desc._vertexShaderPath = "test/shaders/request.hlsl";
    desc._pixelShaderPath  = "test/shaders/request.hlsl";
    desc._listShaderDefine = { "SW_FORWARD=1", "FOO" };

    // 1) 기본: 진입점 기본값, define 은 두 스테이지에, RT 1.
    const RHIGraphicsShaderRequest plain = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_TRUE( plain._bHasPixelShader != SW_FALSE );
    SW_EXPECT_TRUE( plain._bDepthOnly == SW_FALSE );
    SW_EXPECT_EQUAL( 1u, plain._numRenderTargets );
    SW_EXPECT_EQUAL( string( "VSMain" ), plain._vertex._entryPoint );
    SW_EXPECT_EQUAL( string( "PSMain" ), plain._pixel._entryPoint );
    SW_EXPECT_TRUE( plain._vertex._stage == ShaderStage::Vertex && plain._pixel._stage == ShaderStage::Pixel );
    SW_EXPECT_TRUE( plain._vertex._targetFormat == ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_EQUAL( size_t( 2 ), plain._vertex._listDefine.size() );
    SW_REQUIRE_EQUAL( size_t( 2 ), plain._pixel._listDefine.size() );
    SW_EXPECT_EQUAL( string( "FOO" ), plain._pixel._listDefine[1]._name );
    SW_EXPECT_EQUAL( string( "1" ), plain._pixel._listDefine[1]._value ); // 값 없는 define 은 1

    // 2) 명시한 진입점은 그대로.
    desc._vertexEntryPoint               = "MyVS";
    desc._pixelEntryPoint                = "MyPS";
    const RHIGraphicsShaderRequest named = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::SPIRV_Vulkan );
    SW_EXPECT_EQUAL( string( "MyVS" ), named._vertex._entryPoint );
    SW_EXPECT_EQUAL( string( "MyPS" ), named._pixel._entryPoint );

    // 3) 뎁스 전용(RT 0 + 뎁스 테스트): RT 0. PS 경로가 있으면 PS 를 둔다(알파 컷오프의 버리기) — 패스 PSO 는 FrameRenderer 가 경로를 비운다.
    desc._numRenderTargets                   = 0;
    desc._renderState._bEnableDepthTest      = SW_TRUE;
    const RHIGraphicsShaderRequest depthOnly = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::DXBC_D3D11 );
    SW_EXPECT_TRUE( depthOnly._bDepthOnly != SW_FALSE );
    SW_EXPECT_TRUE( depthOnly._bHasPixelShader != SW_FALSE );
    SW_EXPECT_EQUAL( 0u, depthOnly._numRenderTargets );

    // 4) RT 0 인데 뎁스 테스트가 없으면 뎁스 전용이 아니다 — RT 는 1 로 올리고 PS 는 붙는다.
    desc._renderState._bEnableDepthTest    = SW_FALSE;
    const RHIGraphicsShaderRequest noDepth = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::SPIRV_OpenGL );
    SW_EXPECT_TRUE( noDepth._bDepthOnly == SW_FALSE );
    SW_EXPECT_TRUE( noDepth._bHasPixelShader != SW_FALSE );
    SW_EXPECT_EQUAL( 1u, noDepth._numRenderTargets );

    // 5) PS 경로가 비면 PS 없음 (RT 는 그대로).
    desc._numRenderTargets = 2;
    desc._pixelShaderPath.clear();
    const RHIGraphicsShaderRequest noPs = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_TRUE( noPs._bHasPixelShader == SW_FALSE );
    SW_EXPECT_EQUAL( 2u, noPs._numRenderTargets );
}

/**
 * @brief [RHIConstantBufferMirrorTest] 한 번 쓴 값이 링이 도는 동안 나머지 칸에 채워지고, 다 채우면 목록에서 빠진다
 * @details 링 상수버퍼의 칸 `kMaxFrameCountInFlight` 개를 배열로 흉내 낸다. 칸 0 에 쓰고 링을 한 바퀴 돌리면 모든 칸이 같은 값이어야
 *          한다. 채우는 도중 짧은 쓰기는 앞부분만 덮고, 부순 버퍼는 채우지 않는다.
 */
SW_TEST_CASE( RHIConstantBufferMirrorTest, FillsStaleSlotsOnceAndForgets )
{
    constexpr uint32 kSlotCount = constant::render::kMaxFrameCountInFlight;
    constexpr uint32 kSlotBytes = 8;
    uint8            arrSlot[kSlotCount][kSlotBytes]{};
    uint32           writeCount{ 0 };
    auto             writeSlot = [&arrSlot, &writeCount]( RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size )
    {
        SW_EXPECT_EQUAL( RHIBufferHandle{ 7 }, buffer );
        Memory::copy( arrSlot[slot], pData, size );
        ++writeCount;
    };

    RHIConstantBufferMirror shadow;
    const uint8             arrFirst[kSlotBytes] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    shadow.write( 7, 0, arrFirst, kSlotBytes, writeSlot );
    SW_EXPECT_EQUAL( 1u, writeCount );
    SW_EXPECT_EQUAL( 1u, shadow.getPendingBufferCount() );

    // 같은 칸으로 돌아와도(아직 다른 칸이 남았다) 다시 쓰지 않는다.
    shadow.fillSlot( 0, writeSlot );
    SW_EXPECT_EQUAL( 1u, writeCount );

    for ( uint32 slot = 1; slot < kSlotCount; ++slot )
    {
        shadow.fillSlot( slot, writeSlot );
    }
    SW_EXPECT_EQUAL( kSlotCount, writeCount );
    for ( uint32 slot = 0; slot < kSlotCount; ++slot )
    {
        SW_EXPECT_TRUE( Memory::compare( arrSlot[slot], arrFirst, kSlotBytes ) == 0 );
    }
    SW_EXPECT_EQUAL( 0u, shadow.getPendingBufferCount() );

    // 다 채운 뒤에는 링이 더 돌아도 아무것도 쓰지 않는다.
    shadow.fillSlot( 1, writeSlot );
    SW_EXPECT_EQUAL( kSlotCount, writeCount );

    // 짧은 쓰기: 앞 두 바이트만 바뀌고 뒤는 그대로여야 한다.
    const uint8 arrPrefix[2] = { 9, 9 };
    shadow.write( 7, 1, arrPrefix, 2, writeSlot );
    shadow.fillSlot( 2 % kSlotCount, writeSlot );
    const uint8 arrExpected[kSlotBytes] = { 9, 9, 3, 4, 5, 6, 7, 8 };
    SW_EXPECT_TRUE( Memory::compare( arrSlot[2 % kSlotCount], arrExpected, kSlotBytes ) == 0 );

    // 부순 버퍼는 채우지 않는다.
    shadow.remove( 7 );
    SW_EXPECT_EQUAL( 0u, shadow.getPendingBufferCount() );
    const uint32 writeCountBeforeForget = writeCount;
    for ( uint32 slot = 0; slot < kSlotCount; ++slot )
    {
        shadow.fillSlot( slot, writeSlot );
    }
    SW_EXPECT_EQUAL( writeCountBeforeForget, writeCount );
}
