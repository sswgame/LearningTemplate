#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/Support/RHIConstantBufferMirror.h"
#include "Engine/Graphics/RHI/Support/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"
#include "Engine/Graphics/RHI/Support/RHIShaderRequest.h"
#include "Engine/Graphics/RHI/Support/RHITimestamp.h"
#include "Engine/Graphics/RHI/Support/RHITransferSerials.h"
#include "Engine/Graphics/RHI/Support/RHITransferUtil.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIRequiredVersion.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/RHIFakeDevice.h"

#include "TestFramework/Harness/TestHostRuntime.h"
#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief `releaseRHI` 를 받으면 디바이스의 종료 단계 목록에 적는 테스트 자원입니다. */
    class ShutdownStepRecordingResource final : public sw::RHIRenderResource
    {
    public:
        explicit ShutdownStepRecordingResource( test::FakeRHIDevice* pDevice )
            : _pDevice{ pDevice }
        {
        }

        void releaseRHI( sw::IRHIDevice* pDevice ) override
        {
            if ( pDevice == _pDevice )
                _pDevice->_listShutdownStep.push_back( "releaseRHI" );
        }
        void forgetRHI( sw::IRHIDevice* ) override {}

    private:
        test::FakeRHIDevice* _pDevice;
    };

    constexpr uint32 kFakeDeviceGeneration = 7;

    /**
     * @brief 전송 계약만 흉내 내는 팩토리입니다. 나머지 가상 함수는 쓰이지 않는다(호출되면 0 · false 를 돌려준다).
     * @details 제출은 `_busyCountBeforeAccept` 번 `Busy` 를 돌려준 뒤 받아들이고, 폴링은 `_pendingCountBeforeFinal` 번 `Pending` 을 돌려준 뒤 `_finalStatus` 를 준다.
     *          읽기 결과를 가져가거나 버린 횟수를 세어 `RHITransferUtil` 이 짝을 맞추는지 본다.
     */
    class FakeTransferFactory final : public sw::IRHIResourceFactory
    {
    public:
        uint32                _busyCountBeforeAccept{ 0 };
        uint32                _pendingCountBeforeFinal{ 0 };
        sw::RHITransferStatus _finalStatus{ sw::RHITransferStatus::Complete };
        bool                  _bRejectSubmit{ false };
        uint32                _submitCount{ 0 };
        uint32                _pollCount{ 0 };
        uint32                _takeCount{ 0 };
        uint32                _discardCount{ 0 };

        sw::RHITransferSubmit submit()
        {
            ++_submitCount;
            if ( _bRejectSubmit )
                return sw::RHITransferSubmit::rejected();
            if ( _submitCount <= _busyCountBeforeAccept )
                return sw::RHITransferSubmit::busy();
            return sw::RHITransferSubmit::accepted( _submitCount, kFakeDeviceGeneration );
        }

        sw::RHITransferSubmit submitTextureUpload( sw::RHITextureHandle, const sw::RHITextureUploadDesc& ) override { return submit(); }
        sw::RHITransferSubmit submitTextureRegionUpload( sw::RHITextureHandle, const sw::RHITextureRegionUploadDesc& ) override { return submit(); }
        sw::RHITransferSubmit submitTextureReadback( sw::RHITextureHandle, uint32, uint32, sw::RHITextureMipSpan& ) override { return submit(); }
        sw::RHITransferStatus pollTransfer( sw::RHITransferTicket ) override
        {
            ++_pollCount;
            return _pollCount <= _pendingCountBeforeFinal ? sw::RHITransferStatus::Pending : _finalStatus;
        }
        bool takeTextureReadback( sw::RHITransferTicket, sw::vector<uint8>& outBytes ) override
        {
            ++_takeCount;
            outBytes.assign( 4, 7 );
            return true;
        }
        void discardTextureReadback( sw::RHITransferTicket ) override { ++_discardCount; }

        sw::RHIPipelineStateHandle createPipelineState( const sw::RHIPipelineStateDesc& ) override { return 0; }
        sw::RHIPipelineStateHandle createComputePipelineState( sw::string_view, sw::string_view ) override { return 0; }
        void                       destroyPipelineState( sw::RHIPipelineStateHandle ) override {}
        sw::RHIRenderPassHandle    createRenderPass( const sw::RHIRenderPassDesc& ) override { return 0; }
        void                       destroyRenderPass( sw::RHIRenderPassHandle ) override {}
        sw::RHIBufferHandle        createConstantBuffer( uint32 ) override { return 0; }
        void                       updateConstantBuffer( sw::RHIBufferHandle, const void*, uint32 ) override {}
        sw::RHIBufferHandle        createStructuredBuffer( uint32, uint32 ) override { return 0; }
        void                       updateStructuredBufferRegions( sw::RHIBufferHandle, const void*, const sw::RHIBufferCopyRegion*, uint32 ) override {}
        sw::RHIBufferHandle        createVertexBuffer( const void*, uint32 ) override { return 0; }
        void                       updateVertexBufferRegions( sw::RHIBufferHandle, const void*, const sw::RHIBufferCopyRegion*, uint32 ) override {}
        sw::RHIBufferHandle        createIndexBuffer( const void*, uint32 ) override { return 0; }
        void                       destroyBuffer( sw::RHIBufferHandle ) override {}
        sw::RHIBufferHandle        createReadbackBuffer( uint32, uint32 ) override { return 0; }
        [[nodiscard]] bool         readReadbackBuffer( sw::RHIBufferHandle, uint32, void* ) override { return false; }
        sw::RHITextureHandle       createTexture2D( const sw::RHITextureDesc& ) override { return 0; }
        void                       destroyTexture( sw::RHITextureHandle ) override {}
        sw::RHIFormat              getTextureFormat( sw::RHITextureHandle ) const override { return sw::RHIFormat::Unknown; }
        sw::RHIDescriptorIndex     registerBindlessTexture( sw::RHITextureHandle ) override { return sw::kInvalidDescriptorIndex; }
        void                       unregisterBindlessTexture( sw::RHIDescriptorIndex ) override {}
        sw::RHIDescriptorIndex     registerBindlessBufferSrv( sw::RHIBufferHandle ) override { return sw::kInvalidDescriptorIndex; }
        void                       unregisterBindlessResource( sw::RHIDescriptorIndex ) override {}
        sw::RHIDescriptorIndex     registerBindlessBufferUav( sw::RHIBufferHandle ) override { return sw::kInvalidDescriptorIndex; }
        sw::RHIDescriptorIndex     registerBindlessTextureUav( sw::RHITextureHandle ) override { return sw::kInvalidDescriptorIndex; }
        void                       unregisterBindlessUav( sw::RHIDescriptorIndex ) override {}
    };
} // namespace

// Engine/Graphics/RHI/Support — 디바이스 없이 도는 RHI 보조 자료구조.
// 핸들 표의 세대 무효화 · 해제 큐의 지연과 펜스 · 셰이더 요청 해석.
// 가짜 디바이스로 보는 종료 단계 순서(RHIDeviceShutdownTest)도 여기 있다. 실제 디바이스를 만드는 케이스는 TestRHIDevice.cpp 에 있다 (CI 가 못 돌린다).
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
 * @brief [RHIHandleTableTest] generation이 올라가면 옛 핸들은 무효이고 슬롯은 재사용된다
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
SW_TEST_CASE( RHIReleaseQueueTest, GPUFenceRelease )
{
    sw::RHIReleaseQueue            queue( 3 );
    bool                           bDestroyed{ false };
    sw::RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, [&bDestroyed]()
    {
        bDestroyed = true;
    } );

    queue.enqueueGPURelease( releaseDel, 4 );
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
 * @brief [RHIReleaseQueueTest] 두 스레드가 tickFrame · tickCompleted 를 동시에 불러도 콜백이 한 번씩만 돈다
 * @details DX12 는 렌더 스레드의 endFrame 과 리소스를 만드는 스레드의 업로드 펜스 대기가 둘 다 tickCompleted 를 부른다.
 *          스크래치 목록을 잠금 없이 바꾸던 때는 두 tick 이 같은 벡터를 동시에 만졌다.
 */
SW_TEST_CASE( RHIReleaseQueueTest, ConcurrentTicksRunEachCallbackOnce )
{
    constexpr uint32    kCount = 2000;
    sw::RHIReleaseQueue queue( 1 );
    std::atomic<uint32> calledCount{ 0 };
    for ( uint32 index = 0; index < kCount; ++index )
    {
        sw::RHIResourceReleaseDelegate releaseDel = SW_DELEGATE_LAMBDA( sw::RHIResourceReleaseDelegate, [&calledCount]()
        {
            calledCount.fetch_add( 1u, std::memory_order_relaxed );
        } );
        if ( ( index & 1u ) == 0 )
            queue.enqueueRelease( releaseDel );
        else
            queue.enqueueGPURelease( releaseDel, index );
    }
    std::thread frameThread( [&queue]()
    {
        for ( uint32 index = 0; index < kCount; ++index )
        {
            queue.tickFrame();
        }
    } );
    for ( uint32 index = 0; index < kCount; ++index )
    {
        queue.tickCompleted( index );
    }
    frameThread.join();
    SW_EXPECT_EQUAL( kCount, calledCount.load() );
    SW_EXPECT_EQUAL( 0u, queue.getPendingReleaseCount() );
}

/**
 * @brief [RHIShaderRequestTest] 파이프라인 서술체를 컴파일 요청으로 해석하는 규칙 — 네 백엔드가 같이 쓴다.
 * @details 진입점 기본값, define 은 두 스테이지에, 픽셀 스테이지는 PS 경로가 있을 때만(뎁스 전용도 — 알파 컷오프),
 *          RT 수는 뎁스 전용 0 / 그 밖 1 이상. 백엔드마다 규칙을 복사하면 어느 하나가 define 이나 뎁스 전용 판정을
 *          빠뜨린다 — 규칙이 한 곳이면 빠질 자리가 없다. GPU 가 필요 없다(nogpu).
 */
SW_TEST_CASE( RHIShaderRequestTest, ResolvesEntryPointsDefinesAndDepthOnly )
{
    sw::RHIPipelineStateDesc desc{};
    desc._vertexShaderPath = test::getEngineDefaultAssets()._shaderForwardLit.get();
    desc._pixelShaderPath  = test::getEngineDefaultAssets()._shaderForwardLit.get();
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

    // 3) 뎁스 전용(RT 0 + 뎁스 테스트): RT 0. PS 경로가 있으면 PS 를 둔다(알파 컷오프의 버리기) — 패스 PSO 는 FrameRenderer 가 경로를 비운다.
    desc._numRenderTargets                       = 0;
    desc._bEnableDepthTest                       = SW_TRUE;
    const sw::RHIGraphicsShaderRequest depthOnly = sw::RHIShaderRequest::resolveGraphics( desc, sw::ShaderTargetFormat::DXBC_D3D11 );
    SW_EXPECT_TRUE( depthOnly._bDepthOnly != SW_FALSE );
    SW_EXPECT_TRUE( depthOnly._bHasPixelShader != SW_FALSE );
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
 * @brief [RHITimestampTest] 기준점은 가장 이른 틱이고, 안 적힌 칸은 음수다. 기준점은 GPU 시계 나노초로도 남는다
 * @details 외부 프로파일러(Tracy)는 기준점 + 칸 값으로 절대 GPU 시각을 만들어 `readGPUClockNanos` 로 맞춘 CPU 시계 위에 놓는다.
 *          기준점의 환산이 칸 값과 다르면 GPU 구간이 타임라인에서 통째로 밀린다.
 */
SW_TEST_CASE( RHITimestampTest, OriginIsEarliestTickAndUnwrittenSlotsAreNegative )
{
    uint64 arrTick[sw::constant::kMaxGPUTimestampSlot]{};
    arrTick[0]             = 1000; // 낮은 번호지만 늦은 시각 — 이것을 기준으로 삼으면 슬롯 5 가 음수(→ 0)가 된다
    arrTick[5]             = 400;
    arrTick[31]            = 1600;
    const uint32 readyMask = ( 1u << 0 ) | ( 1u << 5 ) | ( 1u << 31 );

    sw::RHITimestampFrame frame;
    SW_ASSERT_TRUE( sw::RHITimestamp::resolve( arrTick, readyMask, 500.0, frame ) ); // 틱 하나 = 500 ns = 0.5 us
    const sw::vector<float32>& listMicro = frame._listMicro;
    SW_ASSERT_TRUE( listMicro.size() == sw::constant::kMaxGPUTimestampSlot );
    SW_EXPECT_EQUAL( int64( 400 * 500 ), frame._originNanos );
    SW_EXPECT_EQUAL( int64( 1000 * 500 ), sw::RHITimestamp::convertTickToNanos( 1000, 500.0 ) );
    SW_EXPECT_NEAR_EQUAL( 300.0f, listMicro[0], 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listMicro[5], 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 600.0f, listMicro[31], 1e-3f );
    SW_EXPECT_TRUE( listMicro[1] < 0.0f && listMicro[30] < 0.0f );

    // 준비된 칸이 없으면 비운다 — 호출자는 빈 목록을 "이번 프레임 없음" 으로 읽는다.
    SW_EXPECT_TRUE( sw::RHITimestamp::resolve( arrTick, 0, 500.0, frame ) == false );
    SW_EXPECT_TRUE( frame._listMicro.empty() );
}

/**
 * @brief [RHIConstantBufferMirrorTest] 한 번 쓴 값이 링이 도는 동안 나머지 칸에 채워지고, 다 채우면 목록에서 빠진다
 * @details 링 상수버퍼의 칸 `kMaxFrameCountInFlight` 개를 배열로 흉내 낸다. 칸 0 에 쓰고 링을 한 바퀴 돌리면 모든 칸이 같은 값이어야
 *          한다. 채우는 도중 짧은 쓰기는 앞부분만 덮고, 부순 버퍼는 채우지 않는다.
 */
SW_TEST_CASE( RHIConstantBufferMirrorTest, FillsStaleSlotsOnceAndForgets )
{
    constexpr uint32 kSlotCount = sw::constant::kMaxFrameCountInFlight;
    constexpr uint32 kSlotBytes = 8;
    uint8            arrSlot[kSlotCount][kSlotBytes]{};
    uint32           writeCount{ 0 };
    auto             writeSlot = [&arrSlot, &writeCount]( sw::RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size )
    {
        SW_EXPECT_EQUAL( sw::RHIBufferHandle{ 7 }, buffer );
        sw::Memory::copy( arrSlot[slot], pData, size );
        ++writeCount;
    };

    sw::RHIConstantBufferMirror shadow;
    const uint8                 arrFirst[kSlotBytes] = { 1, 2, 3, 4, 5, 6, 7, 8 };
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
        SW_EXPECT_TRUE( sw::Memory::compare( arrSlot[slot], arrFirst, kSlotBytes ) == 0 );
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
    SW_EXPECT_TRUE( sw::Memory::compare( arrSlot[2 % kSlotCount], arrExpected, kSlotBytes ) == 0 );

    // 부순 버퍼는 채우지 않는다.
    shadow.forget( 7 );
    SW_EXPECT_EQUAL( 0u, shadow.getPendingBufferCount() );
    const uint32 writeCountBeforeForget = writeCount;
    for ( uint32 slot = 0; slot < kSlotCount; ++slot )
    {
        shadow.fillSlot( slot, writeSlot );
    }
    SW_EXPECT_EQUAL( writeCountBeforeForget, writeCount );
}

/**
 * @brief [RHIDeviceShutdownTest] `IRHIDevice::shutdown` 이 종료 단계를 정한 순서로 한 번씩 부르는지.
 * @details 자원 통보(디바이스가 살아 있을 때) → GPU 대기 · 해제 큐 비우기 → 프레임 스트림 · 커맨드 리스트 떼기 → 백엔드 자원 · 디바이스.
 *          백엔드는 이 순서를 다시 적지 않고 훅만 채운다 — 순서가 바뀌면 해제 큐에 넘긴 자원이 GPU 대기 없이 사라지거나,
 *          리스트가 내려간 디바이스에 반납하려 든다.
 */
SW_TEST_CASE( RHIDeviceShutdownTest, StepsRunInContractOrder )
{
    test::FakeRHIDevice           device;
    ShutdownStepRecordingResource resource( &device );

    device.shutdown();

    const utf8* const arrExpected[] = { "releaseRHI", "waitIdleInternal", "detachCommandRecordingInternal", "shutdownInternal" };
    SW_ASSERT_EQUAL( static_cast<size_t>( std::size( arrExpected ) ), device._listShutdownStep.size() );
    for ( size_t index = 0; index < std::size( arrExpected ); ++index )
    {
        SW_EXPECT_STREQ( arrExpected[index], device._listShutdownStep[index] );
    }
}

/**
 * @brief [RHIDeviceShutdownTest] 종료가 끝났는데 해제 큐에 남은 것이 있으면 단언한다 — 그 해제는 네이티브 디바이스가 사라진 뒤에 불린다.
 * @details 해제 큐는 2 단계(GPU 대기)에서 비워야 한다. 남은 채로 3 · 4 단계를 지나면 해제 큐의 소멸자가 죽은 디바이스로 콜백을 부른다.
 */
SW_TEST_CASE( RHIDeviceShutdownTest, PendingReleasesAfterShutdownAssert )
{
    SW_TEST_REQUIRES_FEATURE( DebugAsserts );
    test::FakeRHIDevice device;
    device._pendingReleaseCount = 2;
    {
        test::ScopedDefensiveTestLog defensiveLog( "a GPU release left after shutdown" );
        test::ScopedAssertCapture    assertCapture;
        device.shutdown();
        SW_EXPECT_EQUAL( 2u, assertCapture.getCount() ); // GPU 대기 뒤 · 4 단계 뒤
    }

    // 비운 채로 끝나면 단언이 없다.
    device._pendingReleaseCount = 0;
    {
        test::ScopedAssertCapture assertCapture;
        device.shutdown();
        SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
    }
}

/**
 * @brief [RHIDeviceShutdownTest] 끝난 종료 뒤에 넣은 해제는 다음 종료(백엔드 소멸자)가 먼저 단언한다 — 그 해제를 부를 네이티브 디바이스가 없다.
 */
SW_TEST_CASE( RHIDeviceShutdownTest, ReleasesQueuedAfterShutdownAssert )
{
    SW_TEST_REQUIRES_FEATURE( DebugAsserts );
    test::FakeRHIDevice device;
    device.shutdown();
    device._pendingReleaseCount = 1; // 종료 뒤에 누군가 해제를 넣었다(가짜는 비우지 않으므로 이후 단계 단언도 함께 센다)
    test::ScopedDefensiveTestLog defensiveLog( "a GPU release queued after shutdown" );
    test::ScopedAssertCapture    assertCapture;
    device.shutdown();
    SW_EXPECT_EQUAL( 3u, assertCapture.getCount() ); // 종료 뒤에 쌓인 것 · GPU 대기 뒤 · 4 단계 뒤
    device._pendingReleaseCount = 0;                 // 소멸자의 종료는 조용히
}

/**
 * @brief [RHIDeferredHandleTest] 렌더 스레드가 프레임을 들고 있는 동안 다른 스레드가 내리는 핸들은 미뤄지고, 프레임이 끝난 뒤 · `waitIdle` 이 내린다
 * @details 게임 스레드가 GPU 머티리얼(인스턴스)의 마지막 소유를 놓는 순간 렌더 스레드가 병렬 기록 중이면 bindless 표를 바꾸면 안 된다 — 핫 리로드로 뷰를
 *          걷은 StarSkirmish · VoxelCraft · Shooter3D 가 Debug 단언으로 죽었다. 렌더 스레드 자신 · 쉬는 렌더 스레드에서는 바로 내린다.
 */
SW_TEST_CASE( RHIDeferredHandleTest, ReleasesWaitForTheRenderThreadFrame )
{
    test::FakeRHIDevice device;

    // 렌더 스레드가 든 프레임이 없으면 바로 내린다.
    device.releaseHandle( sw::RHIHandleKind::BindlessResource, 7 );
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // 프레임을 넘긴 뒤 다른 스레드(렌더 스레드가 아니다)가 내리면 미룬다.
    device.notifyRenderFrameQueued();
    device.releaseHandle( sw::RHIHandleKind::BindlessResource, 8 );
    device.releaseHandle( sw::RHIHandleKind::Buffer, 9 );
    device.releaseHandle( sw::RHIHandleKind::Buffer, 0 ); // 빈 핸들은 무시
    SW_EXPECT_EQUAL( size_t( 2 ), device.getDeferredHandleCount() );

    // 프레임이 끝나도 렌더 스레드가 비우기 전까지는 남는다 — 비우면 내린다.
    device.notifyRenderFrameRetired();
    SW_EXPECT_EQUAL( size_t( 2 ), device.getDeferredHandleCount() );
    device.flushDeferredHandleReleases();
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // 남는 프레임 통보는 셈을 음수로 만들지 않는다.
    device.notifyRenderFrameRetired();
    device.releaseHandle( sw::RHIHandleKind::Texture, 10 );
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // waitIdle 은 렌더 스레드를 비운 뒤 미룬 것을 내린다.
    device.notifyRenderFrameQueued();
    device.releaseHandle( sw::RHIHandleKind::BindlessTexture, 11 );
    SW_EXPECT_EQUAL( size_t( 1 ), device.getDeferredHandleCount() );
    device.waitIdle();
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );
    device.notifyRenderFrameRetired(); // 위에서 넘긴 프레임을 끝낸다
}

/**
 * @brief [RHIDeferredHandleTest] 해제는 그때 든 프레임이 아니라 **다음에 넘길 프레임**이 끝나야 내린다 — 렌더 스레드가 쉬고 있어도 미룬다
 * @details 게임 스레드가 지었지만 아직 넘기지 않은 패킷도 옛 인덱스를 값으로 들고 있다(배치의 머티리얼 텍스처 SRV). 로드 중 텍스처를 진짜로 바꾼 머티리얼이
 *          그 텍스처를 놓자 바로 내려, 다음 패킷의 그림자 · 불투명 패스가 죽은 슬롯을 읽었다(DX11 HarvestValley, BindlessIndexValidator, 3 번 중 2 번).
 */
SW_TEST_CASE( RHIDeferredHandleTest, ReleasesWaitForThePacketBeingBuilt )
{
    test::FakeRHIDevice device;

    // 프레임 A 가 든 동안 해제한다 — 게임 스레드는 다음 패킷 B 를 짓는 중이다. A 가 끝나도 B 가 남았다.
    device.notifyRenderFrameQueued();
    device.releaseHandle( sw::RHIHandleKind::BindlessTexture, 12 );
    device.notifyRenderFrameRetired();
    SW_EXPECT_EQUAL( size_t( 1 ), device.getDeferredHandleCount() );
    device.notifyRenderFrameQueued();
    device.notifyRenderFrameRetired();
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // 렌더 스레드가 떠 있으면 든 프레임이 없어도 미룬다(짓는 중인 패킷이 있다). 다음 프레임이 끝나면 내린다.
    device.setRenderThreadDrain( []( void* ) {}, nullptr, std::thread::id{} );
    device.releaseHandle( sw::RHIHandleKind::Texture, 13 );
    SW_EXPECT_EQUAL( size_t( 1 ), device.getDeferredHandleCount() );
    device.notifyRenderFrameQueued();
    device.notifyRenderFrameRetired();
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // 렌더 스레드를 풀면 남은 것을 모두 내린다.
    device.releaseHandle( sw::RHIHandleKind::Texture, 14 );
    device.setRenderThreadDrain( nullptr, nullptr, std::thread::id{} );
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );
}

/**
 * @brief [RHINativeHandlesTest] `IRHIDevice::queryNativeHandles` 가 다른 판의 `RHINativeHandles` 를 거절하고, 맞는 판에는 백엔드 훅이 채운 값을 준다.
 * @details 에디터(EditorModule)는 RHI 백엔드의 구체 클래스를 모르고 이 POD 하나로 네이티브 핸들을 받는다. 따로 지은 모듈이 다른 판의 구조체를
 *          보내면 Engine 이 한 칸도 쓰지 않고 거절해야 한다 — 쓰면 다른 레이아웃 위에 핸들을 적어 조용히 깨진다. 판 번호와 크기를 각각 어긋나게 한다.
 */
SW_TEST_CASE( RHINativeHandlesTest, QueryRejectsAnotherLayoutAndFillsTheMatchingOne )
{
    test::FakeRHIDevice device;
    int32               nativeDeviceStandIn{ 0 };
    device._pNativeDevice = &nativeDeviceStandIn;

    sw::RHINativeHandles handles{};
    SW_EXPECT_TRUE( device.queryNativeHandles( handles ) );
    SW_EXPECT_TRUE( handles._pDevice == &nativeDeviceStandIn );
    SW_EXPECT_TRUE( handles._backend == device.getBackendType() );
    SW_EXPECT_EQUAL( 1u, device._nativeHandleQueryCount );

    sw::RHINativeHandles otherVersion{};
    otherVersion._version = sw::kRHINativeHandlesVersion + 1u;
    sw::RHINativeHandles otherSize{};
    otherSize._byteSize = static_cast<uint32>( sizeof( sw::RHINativeHandles ) ) - static_cast<uint32>( sizeof( void* ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "native handle query with another RHINativeHandles layout" );
        SW_EXPECT_FALSE( device.queryNativeHandles( otherVersion ) );
        SW_EXPECT_FALSE( device.queryNativeHandles( otherSize ) );
    }
    SW_EXPECT_TRUE( otherVersion._pDevice == nullptr );
    SW_EXPECT_TRUE( otherSize._pDevice == nullptr );
    SW_EXPECT_EQUAL( 1u, device._nativeHandleQueryCount );

    // 디바이스가 없는 백엔드는 판이 맞아도 false 다.
    device._pNativeDevice = nullptr;
    sw::RHINativeHandles noDevice{};
    SW_EXPECT_FALSE( device.queryNativeHandles( noDevice ) );
}

/**
 * @brief [VulkanAPIVersionTest] 물리 디바이스 선택이 요구 판(셰이더 쿠킹 타깃) 미만의 디바이스를 거른다.
 * @details 셰이더는 vulkan1.3 타깃(SPIR-V 1.6)으로 쿠킹한다. 1.2 디바이스는 SPIR-V 1.5 까지만 받으므로 그 디바이스를 고르면 셰이더 모듈 전부가 무효다.
 *          맨 위 변형 비트는 판 비교에 넣지 않는다.
 */
SW_TEST_CASE( VulkanAPIVersionTest, DeviceBelowTheShaderTargetIsRejected )
{
    using sw::VulkanRHIRequiredVersion;
    using sw::constant::kVulkanRequiredMajor;
    using sw::constant::kVulkanRequiredMinor;
    SW_ASSERT_TRUE( kVulkanRequiredMinor > 0u );

    const uint32 belowWithPatch = VulkanRHIRequiredVersion::makeAPIVersion( kVulkanRequiredMajor, kVulkanRequiredMinor - 1u ) | 0xFFFu;
    SW_EXPECT_FALSE( VulkanRHIRequiredVersion::isAPIVersionSupported( belowWithPatch ) );
    SW_EXPECT_FALSE( VulkanRHIRequiredVersion::isAPIVersionSupported( belowWithPatch | ( 1u << 29u ) ) );
    SW_EXPECT_TRUE( VulkanRHIRequiredVersion::isAPIVersionSupported( VulkanRHIRequiredVersion::makeAPIVersion( kVulkanRequiredMajor, kVulkanRequiredMinor ) ) );
    SW_EXPECT_TRUE( VulkanRHIRequiredVersion::isAPIVersionSupported( VulkanRHIRequiredVersion::makeAPIVersion( kVulkanRequiredMajor, kVulkanRequiredMinor + 1u ) ) );
}

/**
 * @brief [VulkanAPIVersionTest] 쿠킹된 Vulkan SPIR-V 의 판이 디바이스 요구 판의 타깃과 같다.
 * @details 셰이더 쿠킹 타깃(`-fspv-target-env`)과 물리 디바이스의 최소 버전은 `constant::kVulkanRequiredMajor` · `kVulkanRequiredMinor`(RHITypes.h) 하나에서 나온다. 한쪽만 바꾸거나 다시 쿠킹하지 않으면
 *          디바이스가 받지 못하는 판의 모듈이 남는다. 커밋된 vulkan 폴더 .spv 헤더의 판을 읽어 대조한다.
 */
SW_TEST_CASE( VulkanAPIVersionTest, CookedVulkanSpirvMatchesTheRequiredAPI )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    constexpr uint32 kSpirvMagic     = 0x07230203u;
    const uint32     expectedVersion = sw::VulkanRHIRequiredVersion::computeSpirvVersion( sw::constant::kVulkanRequiredMinor );
    const utf8*      arrDomain[]     = { "engine", "common" };

    uint32 checkedCount{ 0 };
    for ( const utf8* pDomain : arrDomain )
    {
        const sw::string       binDirectory = sw::FileUtil::joinPath( sw::ResourcePaths::getRootFolderPath(), sw::string( pDomain ) + "/shaders/bin/vulkan" );
        sw::vector<sw::string> listSpirvPath;
        if ( sw::FileUtil::collectFiles( binDirectory, ".spv", listSpirvPath, false ) == false )
            continue;
        for ( const sw::string& spirvPath : listSpirvPath )
        {
            sw::vector<uint8> bytes;
            if ( sw::FileUtil::readFile( spirvPath, bytes ) == false || bytes.size() < 8u )
            {
                SW_EXPECT_TRUE_MSG( false, ( spirvPath + ": SPIR-V 헤더를 읽지 못했다" ).c_str() );
                continue;
            }
            uint32 magic{ 0 };
            uint32 version{ 0 };
            std::memcpy( &magic, bytes.data(), sizeof( magic ) );
            std::memcpy( &version, bytes.data() + sizeof( magic ), sizeof( version ) );
            SW_EXPECT_TRUE_MSG( magic == kSpirvMagic, ( spirvPath + ": SPIR-V 가 아니다" ).c_str() );
            SW_EXPECT_TRUE_MSG( version == expectedVersion, ( spirvPath + ": 디바이스 요구 판과 다른 SPIR-V 판으로 쿠킹됐다 — 다시 쿠킹할 것" ).c_str() );
            ++checkedCount;
        }
    }
    if ( checkedCount == 0 )
        SW_TEST_SKIP( "No cooked Vulkan SPIR-V found (App.exe --cook-shaders)" );
}

/**
 * @brief [RHIMemoryLedgerTest] 장부는 올린 키의 크기를 기억했다가 내릴 때 그대로 빼고, 크기 모름은 바이트가 아니라 따로 센다
 * @details 버퍼 핸들과 텍스처 핸들은 다른 표라 같은 정수가 둘 다에 있다 — 키 공간이 가르지 않으면 텍스처를 내릴 때 버퍼가 빠진다.
 *          올리지 않은 키를 내리는 것(장부보다 먼저 만든 자원)은 아무것도 바꾸지 않는다. 같은 키를 다시 올리면(해제를 빠뜨림) 옛 값을 빼고 바꾼다.
 */
SW_TEST_CASE( RHIMemoryLedgerTest, FreeSubtractsWhatTheSameKeyAdded )
{
    sw::RHIMemoryLedger ledger;
    constexpr uint64    kSharedHandle = 0x100000001ull;
    ledger.recordAllocation( sw::RHIMemoryKey::makeBuffer( kSharedHandle ), sw::RHIMemoryKind::Buffer, 1000 );
    ledger.recordAllocation( sw::RHIMemoryKey::makeTexture( kSharedHandle ), sw::RHIMemoryKind::RenderTarget, 4096 );
    int32 poolStandIn{ 0 };
    ledger.recordAllocation( sw::RHIMemoryKey::makeDeviceObject( &poolStandIn ), sw::RHIMemoryKind::Descriptor, sw::kRHIMemoryUnknownBytes );

    SW_EXPECT_EQUAL( 1000ull, ledger.getStats( sw::RHIMemoryKind::Buffer )._liveBytes );
    SW_EXPECT_EQUAL( 4096ull, ledger.getStats( sw::RHIMemoryKind::RenderTarget )._liveBytes );
    SW_EXPECT_EQUAL( 0ull, ledger.getStats( sw::RHIMemoryKind::Descriptor )._liveBytes );
    SW_EXPECT_EQUAL( 0u, ledger.getStats( sw::RHIMemoryKind::Descriptor )._liveCount );
    SW_EXPECT_EQUAL( 1u, ledger.getStats( sw::RHIMemoryKind::Descriptor )._unknownSizeCount );
    SW_EXPECT_EQUAL( 5096ull, ledger.getTrackedBytes() );

    ledger.recordFree( sw::RHIMemoryKey::makeTexture( kSharedHandle ) );
    SW_EXPECT_EQUAL( 0ull, ledger.getStats( sw::RHIMemoryKind::RenderTarget )._liveBytes );
    SW_EXPECT_EQUAL( 1000ull, ledger.getStats( sw::RHIMemoryKind::Buffer )._liveBytes );

    ledger.recordFree( sw::RHIMemoryKey::makeTexture( kSharedHandle ) );
    ledger.recordFree( sw::RHIMemoryKey::makeBuffer( 0x7777ull ) );
    SW_EXPECT_EQUAL( 1000ull, ledger.getTrackedBytes() );

    ledger.recordAllocation( sw::RHIMemoryKey::makeBuffer( kSharedHandle ), sw::RHIMemoryKind::Buffer, 300 );
    SW_EXPECT_EQUAL( 300ull, ledger.getStats( sw::RHIMemoryKind::Buffer )._liveBytes );
    SW_EXPECT_EQUAL( 1u, ledger.getStats( sw::RHIMemoryKind::Buffer )._liveCount );

    ledger.recordFree( sw::RHIMemoryKey::makeDeviceObject( &poolStandIn ) );
    ledger.recordFree( sw::RHIMemoryKey::makeBuffer( kSharedHandle ) );
    for ( uint32 kindIndex = 0; kindIndex < sw::kRHIMemoryKindCount; ++kindIndex )
    {
        const sw::RHIMemoryKindStats stats = ledger.getStats( static_cast<sw::RHIMemoryKind>( kindIndex ) );
        SW_EXPECT_EQUAL( 0ull, stats._liveBytes );
        SW_EXPECT_EQUAL( 0u, stats._liveCount );
        SW_EXPECT_EQUAL( 0u, stats._unknownSizeCount );
    }
}

/**
 * @brief [RHIMemoryLedgerTest] 텍스처 서술의 줄과 논리 크기 — 네 백엔드가 같은 판정을 쓴다
 * @details 트랜지언트 풀 표시가 렌더 타깃보다 먼저다. 논리 크기는 밉 전부 × 면 수이고, BC 는 4x4 블록, 깊이는 D24S8 한 칸(4 바이트)이다.
 */
SW_TEST_CASE( RHIMemoryLedgerTest, TextureDescPicksTheKindAndLogicalSize )
{
    sw::RHITextureDesc sampled{};
    sampled._width  = 256;
    sampled._height = 256;
    sampled._format = sw::RHIFormat::R8G8B8A8_UNORM;
    SW_EXPECT_TRUE( sw::RHIMemoryLedger::classifyTexture( sampled ) == sw::RHIMemoryKind::Texture );
    SW_EXPECT_EQUAL( 256ull * 256ull * 4ull, sw::RHIMemoryLedger::computeTextureLogicalBytes( sampled ) );

    sw::RHITextureDesc mipped = sampled;
    mipped._mipLevels         = 9;
    uint64 expectedMipBytes{ 0 };
    for ( uint32 size = 256; size >= 1; size /= 2 )
    {
        expectedMipBytes += static_cast<uint64>( size ) * size * 4ull;
    }
    SW_EXPECT_EQUAL( expectedMipBytes, sw::RHIMemoryLedger::computeTextureLogicalBytes( mipped ) );

    sw::RHITextureDesc compressed{};
    compressed._width  = 6;
    compressed._height = 6;
    compressed._format = sw::RHIFormat::BC1_UNORM;
    SW_EXPECT_EQUAL( 2ull * 2ull * 8ull, sw::RHIMemoryLedger::computeTextureLogicalBytes( compressed ) );

    sw::RHITextureDesc cube{};
    cube._width     = 32;
    cube._height    = 32;
    cube._arraySize = sw::kCubeFaceCount;
    cube._dimension = sw::RHITextureDimension::TextureCube;
    cube._format    = sw::RHIFormat::R16G16B16A16_FLOAT;
    SW_EXPECT_EQUAL( 32ull * 32ull * 8ull * sw::kCubeFaceCount, sw::RHIMemoryLedger::computeTextureLogicalBytes( cube ) );

    sw::RHITextureDesc depth{};
    depth._width           = 64;
    depth._height          = 32;
    depth._format          = sw::RHIFormat::D24_UNORM_S8_UINT;
    depth._bIsDepthStencil = SW_TRUE;
    SW_EXPECT_TRUE( sw::RHIMemoryLedger::classifyTexture( depth ) == sw::RHIMemoryKind::RenderTarget );
    SW_EXPECT_EQUAL( 64ull * 32ull * 4ull, sw::RHIMemoryLedger::computeTextureLogicalBytes( depth ) );

    sw::RHITextureDesc transient = depth;
    transient._bIsTransient      = SW_TRUE;
    SW_EXPECT_TRUE( sw::RHIMemoryLedger::classifyTexture( transient ) == sw::RHIMemoryKind::TransientPool );

    sw::RHITextureDesc unknownFormat = sampled;
    unknownFormat._format            = sw::RHIFormat::Unknown;
    SW_EXPECT_EQUAL( sw::kRHIMemoryUnknownBytes, sw::RHIMemoryLedger::computeTextureLogicalBytes( unknownFormat ) );
}

/**
 * @brief [RHIMemoryLedgerTest] 줄 이름은 enum 값마다 하나이고, 정렬은 바이트가 큰 줄부터다(같으면 enum 순서)
 */
SW_TEST_CASE( RHIMemoryLedgerTest, KindNamesAndOrderByLiveBytes )
{
    for ( uint32 kindIndex = 0; kindIndex < sw::kRHIMemoryKindCount; ++kindIndex )
    {
        SW_EXPECT_TRUE( sw::string_view( sw::RHIMemoryLedger::getKindName( static_cast<sw::RHIMemoryKind>( kindIndex ) ) ) != "Invalid" );
    }
    SW_EXPECT_STREQ( "Invalid", sw::RHIMemoryLedger::getKindName( sw::RHIMemoryKind::MaxKinds ) );

    sw::RHIMemoryLedger ledger;
    ledger.recordAllocation( sw::RHIMemoryKey::makeBuffer( 1 ), sw::RHIMemoryKind::Buffer, 10 );
    ledger.recordAllocation( sw::RHIMemoryKey::makeTexture( 1 ), sw::RHIMemoryKind::TransientPool, 30 );
    ledger.recordAllocation( sw::RHIMemoryKey::makeTexture( 2 ), sw::RHIMemoryKind::Texture, 20 );
    const sw::array<sw::RHIMemoryKind, sw::kRHIMemoryKindCount> arrOrder = ledger.makeKindOrderByLiveBytes();
    SW_EXPECT_TRUE( arrOrder[0] == sw::RHIMemoryKind::TransientPool );
    SW_EXPECT_TRUE( arrOrder[1] == sw::RHIMemoryKind::Texture );
    SW_EXPECT_TRUE( arrOrder[2] == sw::RHIMemoryKind::Buffer );
    SW_EXPECT_TRUE( arrOrder[3] == sw::RHIMemoryKind::RenderTarget );
}

/**
 * @brief [RHIMemoryLedgerTest] "엔진 밖" 은 이 프로세스의 드라이버 사용량이 있을 때만 사용량 − 장부 합으로 계산한다
 * @details 드라이버 값을 모르면(새 장부) 계산하지 않는다 — 0 으로 지어내지 않는다. 디바이스 전체 사용량(GL NVX)은 다른 프로세스 몫이 섞여 빼지 않는다.
 *          장부가 논리 크기라 사용량보다 클 수 있어 결과는 부호가 있다.
 */
SW_TEST_CASE( RHIMemoryLedgerTest, SummaryComputesOutsideOnlyForProcessUsage )
{
    sw::RHIMemoryLedger ledger;
    ledger.recordAllocation( sw::RHIMemoryKey::makeTexture( 1 ), sw::RHIMemoryKind::Texture, 300 );
    ledger.recordAllocation( sw::RHIMemoryKey::makeBuffer( 1 ), sw::RHIMemoryKind::Buffer, sw::kRHIMemoryUnknownBytes );

    const sw::RHIMemorySummary unknown = ledger.makeSummary();
    SW_EXPECT_TRUE( unknown._budget._bUsageKnown == SW_FALSE && unknown._budget._bBudgetKnown == SW_FALSE );
    SW_EXPECT_TRUE( unknown._bOutsideKnown == SW_FALSE );
    SW_EXPECT_EQUAL( 300ull, unknown._trackedBytes );
    SW_EXPECT_EQUAL( 1u, unknown._unknownSizeCount );

    sw::RHIMemoryBudget processBudget{};
    processBudget._usageBytes  = 1000;
    processBudget._bUsageKnown = SW_TRUE;
    processBudget._scope       = sw::RHIMemoryScope::Process;
    ledger.setDriverBudget( processBudget );
    const sw::RHIMemorySummary process = ledger.makeSummary();
    SW_EXPECT_TRUE( process._bOutsideKnown == SW_TRUE );
    SW_EXPECT_EQUAL( 700ll, process._outsideBytes );

    processBudget._usageBytes = 100;
    ledger.setDriverBudget( processBudget );
    SW_EXPECT_EQUAL( -200ll, ledger.makeSummary()._outsideBytes );

    sw::RHIMemoryBudget deviceBudget = processBudget;
    deviceBudget._scope              = sw::RHIMemoryScope::Device;
    ledger.setDriverBudget( deviceBudget );
    SW_EXPECT_TRUE( ledger.makeSummary()._bOutsideKnown == SW_FALSE );
}

/**
 * @brief [RHIMemoryLedgerTest] 드라이버에게 물을 수 없는 백엔드는 `refreshGPUMemoryBudget` 뒤에도 모든 칸이 "모름" 이다
 * @details 앞서 적힌 값이 있어도 이번에 묻지 못하면 지운다 — 옛 숫자를 지금 값처럼 보여 주지 않는다.
 */
SW_TEST_CASE( RHIMemoryLedgerTest, RefreshWithoutDriverQueryLeavesEveryFieldUnknown )
{
    test::FakeRHIDevice device;
    sw::RHIMemoryBudget stale{};
    stale._usageBytes  = 4096;
    stale._bUsageKnown = SW_TRUE;
    device.getMemoryLedger().setDriverBudget( stale );

    device.refreshGPUMemoryBudget();
    const sw::RHIMemoryBudget budget = device.getMemoryLedger().getDriverBudget();
    SW_EXPECT_TRUE( budget._bUsageKnown == SW_FALSE && budget._bBudgetKnown == SW_FALSE && budget._bAvailableKnown == SW_FALSE );
    SW_EXPECT_EQUAL( 0ull, budget._usageBytes );
}

/**
 * @brief [RHIMemoryLedgerTest] `-gv_profileFrames` 의 GPU 표는 머리 · 쓰인 줄 · 드라이버 줄 · 엔진 밖 줄을 내고, 모르는 값은 "모름" 으로 찍는다
 * @details 장부에 값이 있는 줄(크기 모름만 있는 줄 포함)만 내고 빈 줄은 내지 않는다. 드라이버 사용량이 디바이스 전체 값이면 엔진 밖은 계산하지 않는다.
 */
SW_TEST_CASE( RHIMemoryLedgerTest, ReportPrintsUsedKindsAndUnknownDriverValues )
{
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
    sw::RHIMemoryLedger ledger;
    ledger.recordAllocation( sw::RHIMemoryKey::makeTexture( 1 ), sw::RHIMemoryKind::TransientPool, 3ull * 1024ull * 1024ull );
    int32 poolStandIn{ 0 };
    ledger.recordAllocation( sw::RHIMemoryKey::makeDeviceObject( &poolStandIn ), sw::RHIMemoryKind::Descriptor, sw::kRHIMemoryUnknownBytes );
    sw::RHIMemoryBudget deviceBudget{};
    deviceBudget._availableBytes  = 8ull * 1024ull * 1024ull;
    deviceBudget._bAvailableKnown = SW_TRUE;
    deviceBudget._scope           = sw::RHIMemoryScope::Device;
    ledger.setDriverBudget( deviceBudget );

    sw::mutex                mutex;
    sw::vector<sw::string>   listLine;
    const sw::DelegateHandle handle = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [&mutex, &listLine]( const sw::LogEntry& entry )
    {
        if ( entry._message.find( "[Profile]" ) == sw::string::npos )
            return;
        std::scoped_lock<sw::mutex> lock{ mutex };
        listLine.push_back( entry._message );
    } ) );
    ledger.report( "FakeBackend" );
    sw::Logger::unregisterGlobalListener( handle );

    sw::string joined;
    for ( const sw::string& line : listLine )
    {
        joined += line + "\n";
    }
    const sw::string_view text{ joined.c_str(), joined.size() };
    SW_EXPECT_TRUE_MSG( text.find( "GPU memory by kind (live, FakeBackend · allocation size)  3.0 MB in 1 resources  + 1 of unknown size" ) != sw::string_view::npos, joined.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "TransientPool  3.0 MB  100.0%  1 resources" ) != sw::string_view::npos, joined.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "Descriptor  0.0 MB  0.0%  0 resources  + 1 of unknown size" ) != sw::string_view::npos, joined.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "Buffer" ) == sw::string_view::npos, joined.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "usage unknown  budget unknown  available 8.0 MB" ) != sw::string_view::npos, joined.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "unknown - no driver usage for this process" ) != sw::string_view::npos, joined.c_str() );
#else
    SW_TEST_REQUIRES_FEATURE( DebugLog ); // Info logs are compiled out in this configuration
#endif
}

/**
 * @brief [RHITextureRegionUploadTest] 영역 업로드 검사 하나가 네 백엔드의 규칙이다 — 행 바이트 · 밉 크기 · 범위 · 포맷 · 데이터 크기
 * @details 64×32 R8 텍스처(밉 2 — 밉 1 은 32×16) · 면 2 로 분기마다 하나씩. 더하기가 넘치는 x + width 도 밖으로 본다.
 */
SW_TEST_CASE( RHITextureRegionUploadTest, ValidationAcceptsInsideAndRejectsTheRest )
{
    uint8                          arrByte[16 * 8 * 4]{};
    sw::RHITextureRegionUploadDesc good{};
    good._pData      = arrByte;
    good._sizeBytes  = 16 * 8;
    good._x          = 16;
    good._y          = 8;
    good._width      = 16;
    good._height     = 8;
    good._mip        = 1;
    good._arraySlice = 1;

    uint32 rowBytes{ 0 };
    SW_EXPECT_TRUE( sw::validateTextureRegionUpload( sw::RHIFormat::R8_UNORM, 64, 32, 2, 2, good, rowBytes ) );
    SW_EXPECT_EQUAL( 16u, rowBytes );
    sw::RHITextureRegionUploadDesc rgba = good;
    rgba._sizeBytes                     = 16 * 8 * 4;
    SW_EXPECT_TRUE( sw::validateTextureRegionUpload( sw::RHIFormat::R8G8B8A8_UNORM, 64, 32, 2, 2, rgba, rowBytes ) );
    SW_EXPECT_EQUAL( 64u, rowBytes );

    SW_TEST_DEFENSIVE_SCOPE( "validateTextureRegionUpload logs every rejection" );
    sw::RHITextureRegionUploadDesc bad = good;
    bad._x                             = 17; // 밉 1 은 32 폭 — 17 + 16 > 32
    SW_EXPECT_FALSE( sw::validateTextureRegionUpload( sw::RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    bad        = good;
    bad._x     = 1;
    bad._width = sw::invalid_index::kUint32; // x + width 가 넘친다
    SW_EXPECT_FALSE( sw::validateTextureRegionUpload( sw::RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    bad      = good;
    bad._mip = 2;
    SW_EXPECT_FALSE( sw::validateTextureRegionUpload( sw::RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    bad             = good;
    bad._arraySlice = 2;
    SW_EXPECT_FALSE( sw::validateTextureRegionUpload( sw::RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    bad         = good;
    bad._height = 0;
    SW_EXPECT_FALSE( sw::validateTextureRegionUpload( sw::RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    bad            = good;
    bad._sizeBytes = 16 * 8 - 1;
    SW_EXPECT_FALSE( sw::validateTextureRegionUpload( sw::RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    bad        = good;
    bad._pData = nullptr;
    SW_EXPECT_FALSE( sw::validateTextureRegionUpload( sw::RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    SW_EXPECT_FALSE( sw::validateTextureRegionUpload( sw::RHIFormat::BC4_UNORM, 64, 32, 2, 2, good, rowBytes ) );
    SW_EXPECT_FALSE( sw::validateTextureRegionUpload( sw::RHIFormat::D24_UNORM_S8_UINT, 64, 32, 2, 2, good, rowBytes ) );
}

/**
 * @brief [RHITransferTest] 티켓과 제출 결과의 모양: 시리얼 0 은 무효이고, Busy · Rejected 는 티켓을 주지 않으며, 티켓은 장치 세대까지 같아야 같다.
 */
SW_TEST_CASE( RHITransferTest, TicketAndSubmitShape )
{
    SW_EXPECT_FALSE( sw::RHITransferTicket{}.isValid() );
    SW_EXPECT_TRUE( ( sw::RHITransferTicket{ 1, 1 } ).isValid() );
    SW_EXPECT_TRUE( ( sw::RHITransferTicket{ 7, 2 } ) == ( sw::RHITransferTicket{ 7, 2 } ) );
    SW_EXPECT_TRUE( ( sw::RHITransferTicket{ 7, 2 } ) != ( sw::RHITransferTicket{ 8, 2 } ) );
    SW_EXPECT_TRUE( ( sw::RHITransferTicket{ 7, 2 } ) != ( sw::RHITransferTicket{ 7, 3 } ) ); // 같은 시리얼이라도 다른 장치의 티켓이다

    const sw::RHITransferSubmit accepted = sw::RHITransferSubmit::accepted( 5, 3 );
    SW_EXPECT_TRUE( accepted.isAccepted() );
    SW_EXPECT_EQUAL( static_cast<uint64>( 5 ), accepted._ticket._serial );
    SW_EXPECT_EQUAL( 3u, accepted._ticket._deviceGeneration );
    SW_EXPECT_FALSE( sw::RHITransferSubmit::busy().isAccepted() );
    SW_EXPECT_FALSE( sw::RHITransferSubmit::busy()._ticket.isValid() );
    SW_EXPECT_FALSE( sw::RHITransferSubmit::rejected().isAccepted() );
    SW_EXPECT_FALSE( sw::RHITransferSubmit{}.isAccepted() ); // 기본값은 Rejected
}

/**
 * @brief [RHITransferTest] 시리얼 0 이나 세대 0 으로 받아들인 제출을 만들면 Debug 에서 단언이 걸린다(백엔드의 실수를 잡는다).
 */
SW_TEST_CASE( RHITransferTest, AcceptingSerialZeroTripsAnAssert )
{
    SW_TEST_REQUIRES_FEATURE( DebugAsserts );
    test::ScopedAssertCapture   assertCapture;
    const sw::RHITransferSubmit zeroSerial = sw::RHITransferSubmit::accepted( 0, 1 );
    SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
    SW_EXPECT_FALSE( zeroSerial.isAccepted() ); // 단언 뒤에도 무효 티켓이라 받아들인 것으로 치지 않는다
    const sw::RHITransferSubmit zeroGeneration = sw::RHITransferSubmit::accepted( 4, 0 );
    SW_EXPECT_EQUAL( 2u, assertCapture.getCount() );
    (void)zeroGeneration; // 세대 0 은 단언만 확인한다
}

/**
 * @brief [RHITransferTest] 장치 세대는 0 이 아니고 발급할 때마다 커진다. 장치(시리얼 발급기)마다 세대가 다르다.
 */
SW_TEST_CASE( RHITransferTest, DeviceGenerationIsNonZeroAndGrows )
{
    const uint32 first  = sw::allocateRHIDeviceGeneration();
    const uint32 second = sw::allocateRHIDeviceGeneration();
    SW_EXPECT_TRUE( first != 0 );
    SW_EXPECT_TRUE( first < second );

    const sw::RHITransferSerials serialsA;
    const sw::RHITransferSerials serialsB;
    SW_EXPECT_TRUE( serialsA.getGeneration() != serialsB.getGeneration() );
}

/**
 * @brief [RHITransferTest] 폴링 대기: Pending 이 몇 번 돈 뒤 Complete, Failed 는 곧바로 돌아오고, 시간이 다 되면 TimedOut 이다(상태 열거형과 섞이지 않는다).
 */
SW_TEST_CASE( RHITransferTest, WaitWhilePendingStopsOnCompleteFailedAndTimeout )
{
    int32 pollCount{ 0 };
    SW_EXPECT_TRUE( sw::RHITransferUtil::waitWhilePending( [&pollCount]()
    { return ++pollCount >= 3 ? sw::RHITransferStatus::Complete : sw::RHITransferStatus::Pending; } ) ==
                    sw::RHITransferWaitResult::Complete );
    SW_EXPECT_EQUAL( 3, pollCount );

    pollCount = 0;
    SW_EXPECT_TRUE( sw::RHITransferUtil::waitWhilePending( [&pollCount]()
    { ++pollCount; return sw::RHITransferStatus::Failed; } ) == sw::RHITransferWaitResult::Failed );
    SW_EXPECT_EQUAL( 1, pollCount );

    {
        SW_TEST_DEFENSIVE_SCOPE( "a transfer that never finishes is reported as an error and returns TimedOut" );
        SW_EXPECT_TRUE( sw::RHITransferUtil::waitWhilePending( []()
        { return sw::RHITransferStatus::Pending; }, 0 ) == sw::RHITransferWaitResult::TimedOut );
    }
}

/**
 * @brief [RHITransferTest] 제출 재시도: Busy 는 다시 제출하고, Rejected 는 다시 시도하지 않으며, 시간이 다 되면 false(마지막 결과는 Busy 그대로)다.
 */
SW_TEST_CASE( RHITransferTest, SubmitWhileBusyRetriesBusyButNotRejected )
{
    int32                 submitCount{ 0 };
    sw::RHITransferSubmit accepted{};
    SW_EXPECT_TRUE( sw::RHITransferUtil::submitWhileBusy( [&submitCount]()
    {
        return ++submitCount >= 3 ? sw::RHITransferSubmit::accepted( 9, 1 ) : sw::RHITransferSubmit::busy();
    }, accepted ) );
    SW_EXPECT_TRUE( accepted.isAccepted() );
    SW_EXPECT_EQUAL( 3, submitCount );

    submitCount = 0;
    sw::RHITransferSubmit rejected{};
    SW_EXPECT_TRUE( sw::RHITransferUtil::submitWhileBusy( [&submitCount]()
    {
        ++submitCount;
        return sw::RHITransferSubmit::rejected();
    }, rejected ) );
    SW_EXPECT_FALSE( rejected.isAccepted() );
    SW_EXPECT_EQUAL( 1, submitCount );

    {
        SW_TEST_DEFENSIVE_SCOPE( "a submit that stays Busy is reported as an error and gives up" );
        sw::RHITransferSubmit stuck{};
        SW_EXPECT_FALSE( sw::RHITransferUtil::submitWhileBusy( []()
        { return sw::RHITransferSubmit::busy(); }, stuck, 0 ) );
        SW_EXPECT_TRUE( stuck._result == sw::RHISubmitResult::Busy ); // Rejected 로 바꿔 쓰지 않는다
    }
}

/**
 * @brief [RHITransferTest] 시리얼 발급기: 판정은 무효 · 다른 세대 · 발급 전 시리얼이 Failed, 워터마크 아래가 Complete, 나머지는 Pending 이고, 장치 손실 뒤 Pending 은 Failed 다.
 */
SW_TEST_CASE( RHITransferTest, SerialsClassifyTicketsByWatermarkAndGeneration )
{
    sw::RHITransferSerials serials;
    const uint64           first  = serials.issue();
    const uint64           second = serials.issue();
    SW_EXPECT_EQUAL( static_cast<uint64>( 1 ), first );
    SW_EXPECT_EQUAL( static_cast<uint64>( 2 ), second );

    const sw::RHITransferTicket firstTicket{ first, serials.getGeneration() };
    const sw::RHITransferTicket secondTicket{ second, serials.getGeneration() };
    SW_EXPECT_TRUE( serials.classify( firstTicket ) == sw::RHITransferStatus::Pending );

    serials.advanceWatermark( first );
    SW_EXPECT_TRUE( serials.classify( firstTicket ) == sw::RHITransferStatus::Complete );
    SW_EXPECT_TRUE( serials.classify( firstTicket ) == sw::RHITransferStatus::Complete ); // 다시 물어도 Complete 다
    SW_EXPECT_TRUE( serials.classify( secondTicket ) == sw::RHITransferStatus::Pending );

    serials.advanceWatermark( 0 ); // 워터마크는 내려가지 않는다
    SW_EXPECT_EQUAL( first, serials.getWatermark() );

    // bindless 등록 보호: 전송한 적 없거나(0) 끝난 전송이면 통과하고, 끝나지 않은 전송이 남은 텍스처는 오류와 함께 거절한다.
    SW_EXPECT_TRUE( serials.isTextureReadyToRegister( 0, 11 ) );
    SW_EXPECT_TRUE( serials.isTextureReadyToRegister( first, 11 ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "registering a texture whose transfer has not finished is reported and refused" );
        SW_EXPECT_FALSE( serials.isTextureReadyToRegister( second, 11 ) );
    }

    serials.markLost();
    SW_EXPECT_TRUE( serials.classify( firstTicket ) == sw::RHITransferStatus::Complete ); // 이미 끝난 일은 그대로다
    {
        SW_TEST_DEFENSIVE_SCOPE( "a lost device fails the transfers that had not finished" );
        SW_EXPECT_TRUE( serials.classify( secondTicket ) == sw::RHITransferStatus::Failed );
    }

    {
        SW_TEST_DEFENSIVE_SCOPE( "invalid, foreign-generation and never-issued tickets are reported and Failed" );
        sw::RHITransferSerials other;
        SW_EXPECT_TRUE( other.classify( sw::RHITransferTicket{} ) == sw::RHITransferStatus::Failed );
        SW_EXPECT_TRUE( other.classify( sw::RHITransferTicket{ 1, serials.getGeneration() } ) == sw::RHITransferStatus::Failed ); // 다른 장치(세대)의 티켓
        SW_EXPECT_TRUE( other.classify( sw::RHITransferTicket{ 1, other.getGeneration() } ) == sw::RHITransferStatus::Failed );   // 발급한 적 없는 시리얼
    }
}

/**
 * @brief [RHITransferTest] 펜스 줄: 끝난 제출은 앞에서부터 연속으로만 워터마크를 올린다. 뒤의 것이 먼저 끝나도 앞이 끝나기 전에는 Complete 가 아니다.
 */
SW_TEST_CASE( RHITransferTest, FenceQueueRaisesWatermarkOnlyOverAContiguousPrefix )
{
    struct FakeProbe
    {
        bool _bDone;
    };

    sw::RHITransferSerials               serials;
    sw::RHITransferFenceQueue<FakeProbe> queue{ serials };
    const uint64                         first  = queue.enqueue( FakeProbe{ false } );
    const uint64                         second = queue.enqueue( FakeProbe{ true } ); // 뒤의 제출이 먼저 끝났다
    const uint64                         third  = queue.enqueue( FakeProbe{ false } );
    SW_EXPECT_EQUAL( 3u, queue.getPendingCount() );

    uint32     retiredCount{ 0 };
    const auto isDone = []( FakeProbe& probe )
    { return probe._bDone; };
    const auto retire = [&retiredCount]( FakeProbe& )
    { ++retiredCount; };

    SW_EXPECT_FALSE( queue.retireDone( isDone, retire ) ); // 맨 앞이 안 끝났다
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), serials.getWatermark() );
    SW_EXPECT_FALSE( serials.isComplete( second ) );

    // 맨 앞이 끝나면 앞의 둘이 함께 빠지고 워터마크는 둘째까지 오른다. 셋째는 아직이다.
    // (probe 를 줄 안에서 바꿀 수 없으므로 isDone 이 시리얼 대신 호출 횟수로 답한다.)
    uint32     askCount{ 0 };
    const auto isDoneFirstTwo = [&askCount]( FakeProbe& )
    { return ++askCount <= 2; };
    SW_EXPECT_TRUE( queue.retireDone( isDoneFirstTwo, retire ) );
    SW_EXPECT_EQUAL( 2u, retiredCount );
    SW_EXPECT_EQUAL( second, serials.getWatermark() );
    SW_EXPECT_TRUE( serials.isComplete( first ) );
    SW_EXPECT_FALSE( serials.isComplete( third ) );
    SW_EXPECT_EQUAL( 1u, queue.getPendingCount() );

    queue.retireAll( retire ); // 장치가 쉬었다 — 남은 것이 모두 끝난 것이다
    SW_EXPECT_EQUAL( 3u, retiredCount );
    SW_EXPECT_TRUE( serials.isComplete( third ) );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
}

/**
 * @brief [RHITransferTest] 끝난 뒤 발급하는 경로(`issueCompleted`): 발급한 시리얼이 곧바로 Complete 이고, 시리얼은 단조 증가한다.
 */
SW_TEST_CASE( RHITransferTest, IssueCompletedIsCompleteImmediately )
{
    sw::RHITransferSerials serials;
    const uint64           first  = serials.issueCompleted();
    const uint64           second = serials.issueCompleted();
    SW_EXPECT_TRUE( first < second );
    SW_EXPECT_TRUE( serials.classify( sw::RHITransferTicket{ first, serials.getGeneration() } ) == sw::RHITransferStatus::Complete );
    SW_EXPECT_TRUE( serials.classify( serials.makeAccepted( second )._ticket ) == sw::RHITransferStatus::Complete );
    SW_EXPECT_EQUAL( second, serials.getWatermark() );
}

/**
 * @brief [RHITransferTest] 읽기 보관표: 결과는 한 번만 나가고, 업로드 티켓(없는 시리얼)과 두 번째 가져가기는 false 다.
 */
SW_TEST_CASE( RHITransferTest, PendingReadbackTableGivesEachResultOnce )
{
    sw::RHIPendingReadbackTable<sw::vector<uint8>> table;
    table.add( 4, sw::vector<uint8>( 3, 9 ) );
    SW_EXPECT_EQUAL( 1u, table.getCount() );

    sw::vector<uint8> bytes;
    SW_EXPECT_FALSE( table.take( 5, bytes ) ); // 읽기가 아닌 시리얼
    SW_EXPECT_TRUE( table.take( 4, bytes ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), bytes.size() );
    SW_EXPECT_FALSE( table.take( 4, bytes ) ); // 이미 가져갔다
    SW_EXPECT_EQUAL( 0u, table.getCount() );
}

/**
 * @brief [RHITransferTest] 동기 변형: Busy 와 Pending 을 지나 끝나고, 거절은 한 번에 false 이며, 읽기는 결과를 가져가거나 버리는 짝이 맞는다.
 */
SW_TEST_CASE( RHITransferTest, AndWaitHelpersRetryBusyAndPairTakeOrDiscard )
{
    sw::RHITextureUploadDesc       uploadDesc{};
    sw::RHITextureRegionUploadDesc regionDesc{};
    FakeTransferFactory            uploadFactory;
    uploadFactory._busyCountBeforeAccept   = 2;
    uploadFactory._pendingCountBeforeFinal = 3;
    SW_EXPECT_TRUE( sw::RHITransferUtil::uploadTexture2DAndWait( uploadFactory, 1, uploadDesc ) );
    SW_EXPECT_EQUAL( 3u, uploadFactory._submitCount ); // Busy 두 번 + 받아들임
    SW_EXPECT_EQUAL( 4u, uploadFactory._pollCount );   // Pending 세 번 + Complete
    SW_EXPECT_TRUE( sw::RHITransferUtil::uploadTexture2DRegionAndWait( uploadFactory, 1, regionDesc ) );

    FakeTransferFactory rejectFactory;
    rejectFactory._bRejectSubmit = true;
    SW_EXPECT_FALSE( sw::RHITransferUtil::uploadTexture2DAndWait( rejectFactory, 1, uploadDesc ) );
    SW_EXPECT_EQUAL( 1u, rejectFactory._submitCount ); // 거절은 다시 시도하지 않는다
    SW_EXPECT_EQUAL( 0u, rejectFactory._pollCount );

    sw::vector<uint8>     bytes;
    sw::RHITextureMipSpan layout{};
    FakeTransferFactory   readFactory;
    readFactory._pendingCountBeforeFinal = 1;
    SW_EXPECT_TRUE( sw::RHITransferUtil::readbackTexture2DAndWait( readFactory, 1, 0, 0, bytes, layout ) );
    SW_EXPECT_EQUAL( 1u, readFactory._takeCount );
    SW_EXPECT_EQUAL( 0u, readFactory._discardCount ); // 가져갔으니 버릴 것이 없다
    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), bytes.size() );

    FakeTransferFactory failFactory;
    failFactory._finalStatus = sw::RHITransferStatus::Failed;
    SW_EXPECT_FALSE( sw::RHITransferUtil::readbackTexture2DAndWait( failFactory, 1, 0, 0, bytes, layout ) );
    SW_EXPECT_EQUAL( 0u, failFactory._takeCount );
    SW_EXPECT_EQUAL( 1u, failFactory._discardCount ); // 실패한 읽기는 결과를 버린다
}

/**
 * @brief [RHITransferTest] PendingReadback: 가져가지 않고 사라지면 소멸자가 버리고, 가져가면 버리지 않으며, 이동하면 소유가 옮겨 가고, 시간 초과 뒤에도 소멸자가 버린다.
 */
SW_TEST_CASE( RHITransferTest, PendingReadbackDiscardsWhatIsNeverTaken )
{
    FakeTransferFactory factory;
    {
        sw::RHITransferUtil::PendingReadback pending{
            factory, sw::RHITransferTicket{ 1, kFakeDeviceGeneration }
        };
        SW_EXPECT_TRUE( pending.isValid() );
    }
    SW_EXPECT_EQUAL( 1u, factory._discardCount );

    {
        sw::RHITransferUtil::PendingReadback pending{
            factory, sw::RHITransferTicket{ 2, kFakeDeviceGeneration }
        };
        sw::vector<uint8> bytes;
        SW_EXPECT_TRUE( pending.take( bytes ) );
        SW_EXPECT_FALSE( pending.isValid() );
    }
    SW_EXPECT_EQUAL( 1u, factory._discardCount ); // 가져간 것은 버리지 않는다

    {
        sw::RHITransferUtil::PendingReadback first{
            factory, sw::RHITransferTicket{ 3, kFakeDeviceGeneration }
        };
        sw::RHITransferUtil::PendingReadback second{ std::move( first ) };
        SW_EXPECT_FALSE( first.isValid() );
        SW_EXPECT_TRUE( second.isValid() );
    }
    SW_EXPECT_EQUAL( 2u, factory._discardCount ); // 이동한 뒤에도 한 번만 버린다

    factory._pendingCountBeforeFinal = 1000000;
    {
        sw::RHITransferUtil::PendingReadback pending{
            factory, sw::RHITransferTicket{ 4, kFakeDeviceGeneration }
        };
        SW_TEST_DEFENSIVE_SCOPE( "a read that does not finish in time is reported and then discarded" );
        const sw::RHITransferWaitResult result = sw::RHITransferUtil::waitWhilePending( [&pending]()
        { return pending.poll(); }, 0 );
        SW_EXPECT_TRUE( result == sw::RHITransferWaitResult::TimedOut );
    }
    SW_EXPECT_EQUAL( 3u, factory._discardCount );
}

/**
 * @brief [RHITransferTest] 막힌 구간(`ScopedForbidBlockingTransfer`) 안에서 동기 변형을 부르면 Debug 에서 단언이 걸리고, 구간 밖에서는 걸리지 않는다.
 */
SW_TEST_CASE( RHITransferTest, BlockingHelpersAssertInsideAForbiddenScope )
{
    SW_TEST_REQUIRES_FEATURE( DebugAsserts );
    FakeTransferFactory            factory;
    const sw::RHITextureUploadDesc desc{};
    test::ScopedAssertCapture      assertCapture;

    SW_EXPECT_TRUE( sw::RHITransferUtil::uploadTexture2DAndWait( factory, 1, desc ) );
    SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
    {
        const sw::RHITransferUtil::ScopedForbidBlockingTransfer forbid;
        SW_EXPECT_TRUE( sw::RHITransferUtil::uploadTexture2DAndWait( factory, 1, desc ) ); // 단언은 세어질 뿐 코드는 이어진다
        SW_EXPECT_TRUE( assertCapture.getCount() >= 1 );
    }
    const uint32 insideScopeCount = assertCapture.getCount();
    SW_EXPECT_TRUE( sw::RHITransferUtil::uploadTexture2DAndWait( factory, 1, desc ) );
    SW_EXPECT_EQUAL( insideScopeCount, assertCapture.getCount() ); // 스코프를 벗어나면 다시 허용된다
}

/**
 * @brief [RHITransferTest] 읽기 티켓(`PendingReadback`)을 쥔 스레드가 제출하는 동기 변형을 부르면 Debug 에서 단언이 걸리고, 놓거나 가져간 뒤에는 걸리지 않는다.
 */
SW_TEST_CASE( RHITransferTest, BlockingSubmitsAssertWhileAReadbackTicketIsHeld )
{
    SW_TEST_REQUIRES_FEATURE( DebugAsserts );
    FakeTransferFactory            factory;
    const sw::RHITextureUploadDesc desc{};
    test::ScopedAssertCapture      assertCapture;

    {
        sw::RHITransferUtil::PendingReadback pending{
            factory, sw::RHITransferTicket{ 1, kFakeDeviceGeneration }
        };
        SW_EXPECT_TRUE( sw::RHITransferUtil::uploadTexture2DAndWait( factory, 1, desc ) ); // 단언은 세어질 뿐 코드는 이어진다
        SW_EXPECT_TRUE( assertCapture.getCount() >= 1 );
    }
    const uint32 heldCount = assertCapture.getCount();
    SW_EXPECT_TRUE( sw::RHITransferUtil::uploadTexture2DAndWait( factory, 1, desc ) );
    SW_EXPECT_EQUAL( heldCount, assertCapture.getCount() ); // 놓으면 다시 허용된다

    sw::RHITransferUtil::PendingReadback taken{
        factory, sw::RHITransferTicket{ 2, kFakeDeviceGeneration }
    };
    sw::vector<uint8> bytes;
    SW_EXPECT_TRUE( taken.take( bytes ) );
    SW_EXPECT_TRUE( sw::RHITransferUtil::uploadTexture2DAndWait( factory, 1, desc ) );
    SW_EXPECT_EQUAL( heldCount, assertCapture.getCount() ); // 가져간 티켓은 세지 않는다
}
