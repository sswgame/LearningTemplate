/**
 * @file TestRHIDeviceRelease.cpp
 * @brief 장치 기반(`IRHIDevice`)이 맡은 지연 해제 · 메모리 추적 · 대기 뒤의 비우기입니다(nogpu). 백엔드는 표에서 빼고 놓는 일만 넘긴다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/RHIGeometryBuffer.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

namespace
{
    constexpr RHITextureHandle kTexture{ 5 };
    constexpr uint64           kBufferBytes = 64;

    uint32 getLiveCount( const IRHIDevice& device, RHIMemoryKind kind )
    {
        return device.getMemoryTracker().getStats( kind )._liveCount;
    }
} // namespace

/**
 * @brief [RHIDeviceReleaseTest] 입구가 만든 자원은 추적기에 오르고, 놓은 자원은 그 프레임이 GPU 에서 끝난 뒤에 내려간다
 * @details 예전에는 백엔드마다 올리고 내렸다 — WebGPU 는 둘 다 빠져 메모리 표가 비었다. 변이: 해제 요청 때 내리면 둘째 단언이, 내리지 않으면 셋째 단언이 진다.
 */
SW_TEST_CASE( RHIDeviceReleaseTest, TrackerFollowsCreationAndTheGPURelease )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory = &factory;
    const RHICapabilities capabilities{};
    factory.bindDevice( device, ShaderTargetFormat{}, capabilities );
    factory._allocationBytes = kBufferBytes;

    const uint32      value{ 1 };
    RHIGeometryBuffer buffer;
    SW_REQUIRE_TRUE( buffer.createVertex( device, &value, sizeof( value ) ) );
    SW_EXPECT_EQUAL( 1u, getLiveCount( device, RHIMemoryKind::Buffer ) );
    SW_EXPECT_EQUAL( kBufferBytes, device.getMemoryTracker().getTrackedBytes() );

    buffer.release( &device );
    SW_EXPECT_EQUAL( 1u, getLiveCount( device, RHIMemoryKind::Buffer ) );
    SW_EXPECT_EQUAL( 1u, device.getPendingReleaseCount() );
    device.completeGPUFrames();
    SW_EXPECT_EQUAL( 0u, getLiveCount( device, RHIMemoryKind::Buffer ) );
    SW_EXPECT_EQUAL( uint64( 0 ), device.getMemoryTracker().getTrackedBytes() );
}

/**
 * @brief [RHIDeviceReleaseTest] 끝나지 않은 전송이 쓰는 텍스처는 그 전송이 끝난 뒤의 프레임이 끝나야 놓인다
 * @details 변이: 전송을 보지 않으면 첫 묶음이, 전송만 보고 바로 놓으면 둘째 묶음이 진다.
 */
SW_TEST_CASE( RHIDeviceReleaseTest, TextureWaitsForItsTransferThenForAFrame )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory       = &factory;
    const uint64 serial            = device._transferSerials.issue();
    factory._textureTransferSerial = serial;

    uint32 releasedCount{ 0 };
    factory._pReleasedCount = &releasedCount;
    device.releaseHandle( kTexture );
    device.completeGPUFrames();
    device.completeTransfers();
    SW_EXPECT_EQUAL( 0u, releasedCount );

    device._transferSerials.advanceWatermark( serial );
    device.completeTransfers();
    SW_EXPECT_EQUAL( 0u, releasedCount );
    SW_EXPECT_EQUAL( 1u, device.getPendingReleaseCount() );
    device.completeGPUFrames();
    SW_EXPECT_EQUAL( 1u, releasedCount );
}

/**
 * @brief [RHIDeviceReleaseTest] 백엔드가 유휴를 확인하지 못한 대기는 남은 통지를 `Abandoned` 로, 확인한 대기는 `Completed` 로 보내고, 해제는 어느 쪽이든 놓는다
 * @details 예전에는 백엔드 다섯이 저마다 큐를 비웠다. 변이: 답을 보지 않고 비우면 첫 통지 단언이 진다.
 */
SW_TEST_CASE( RHIDeviceReleaseTest, UnconfirmedIdleAbandonsCompletions )
{
    for ( const uint8 bIdle : { SW_FALSE, SW_TRUE } )
    {
        test::FakeRHIDevice device;
        device._bIdleConfirmed = bIdle;
        vector<RHIFrameCompletion> listResult;
        uint32                     releasedCount{ 0 };
        device.notifyFrameCompletion( SW_DELEGATE_LAMBDA( RHIFrameCompletionDelegate, [&listResult]( RHIFrameCompletion result )
        { listResult.push_back( result ); } ) );
        device.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [&releasedCount]()
        { ++releasedCount; } ) );
        device.waitIdle( RHIWaitIdleReason::Tool );
        SW_REQUIRE_EQUAL( listResult.size(), static_cast<size_t>( 1 ) );
        SW_EXPECT_TRUE( listResult[0] == ( bIdle != SW_FALSE ? RHIFrameCompletion::Completed : RHIFrameCompletion::Abandoned ) );
        SW_EXPECT_EQUAL( 1u, releasedCount );
        SW_EXPECT_EQUAL( 0u, device.getPendingReleaseCount() );
    }
}

/** @brief [RHIDeviceReleaseTest] 워터마크는 내지 않은 시리얼로 올라가지 않는다 — 제거된 D3D12 장치의 펜스는 UINT64_MAX 를 답한다 */
SW_TEST_CASE( RHIDeviceReleaseTest, WatermarkStopsAtTheLastSubmittedSerial )
{
    RHIFrameFenceRing ring;
    (void)ring.markSubmitted();
    ring.markCompleted( UINT64_MAX );
    SW_EXPECT_EQUAL( uint64( 1 ), ring.getCompletedSerial() );
}
