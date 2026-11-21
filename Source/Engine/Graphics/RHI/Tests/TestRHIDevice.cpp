/**
 * @file TestRHIDevice.cpp
 * @brief GPU 없이 보는 `IRHIDevice` 계약 — 백엔드 표 조회, 종료 단계 순서, 미룬 핸들 해제, 네이티브 핸들 조회, 선택 능력, Vulkan 요구 판입니다.
 * @details 디바이스가 필요한 자리는 가짜 디바이스(`RHIFakeDevice.h`)로 대신한다. 실제 백엔드를 세우는 확인(업로드 · 읽기 · 그리기)은 시나리오가 한다.
 */
#include "pch.h"

#include "Core/Concurrency/DevCounter.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanMemoryAccount.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanPipelineCacheFile.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIRequiredVersion.h"

#include <iterator>
#include <thread>

using namespace sw;

namespace
{
    /** @brief `releaseRHI` 를 받으면 디바이스의 종료 단계 목록에 적는 테스트 자원입니다. */
    class ShutdownStepRecordingResource final : public RHIRenderResource
    {
    public:
        explicit ShutdownStepRecordingResource( test::FakeRHIDevice* pDevice )
            : _pDevice{ pDevice }
        {
        }

        void releaseRHI( IRHIDevice* pDevice ) override
        {
            if ( pDevice == _pDevice )
                _pDevice->_listShutdownStep.push_back( "releaseRHI" );
        }
        void clearRHI( IRHIDevice* ) override {}

    private:
        test::FakeRHIDevice* _pDevice;
    };
} // namespace

/**
 * @brief [RHIRenderResourceTest] 장치가 리소스에 통보하는 동안 다른 스레드가 그 리소스를 파괴하면 단언한다(R-17)
 * @details 통보는 잠금 밖이라(통보가 다른 리소스를 파괴할 수 있다) 다른 스레드의 파괴를 막지 못한다 — 예전엔 아무도 알리지 않아 해제 후 사용으로 끝났다.
 *          통보받는 쪽은 `this` 를 다시 읽지 않는다(파괴된 뒤라). 래치는 바깥 원자값이다.
 */
SW_TEST_CASE( RHIRenderResourceTest, DestroyWhileNotifiedFromAnotherThreadAsserts )
{
    SW_EXPECT_LOG( SW_ASSERT_LOG_ID );

    struct NotifyLatch
    {
        atomic<uint32> _stage{ 0 }; ///< 0 통보 전, 1 통보 안, 2 파괴 끝
    };
    class LatchedResource final : public RHIRenderResource
    {
    public:
        explicit LatchedResource( NotifyLatch* pLatch )
            : _pLatch{ pLatch }
        {
        }
        void releaseRHI( IRHIDevice* ) override
        {
            NotifyLatch* const pLatch = _pLatch; // 아래 기다림 동안 이 객체가 파괴된다 — 래치 주소만 쥔다
            pLatch->_stage.store( 1 );
            while ( pLatch->_stage.load() != 2 )
                ThreadUtil::yield();
        }
        void clearRHI( IRHIDevice* ) override {}

    private:
        NotifyLatch* _pLatch;
    };

    NotifyLatch               latch;
    LatchedResource* const    pResource = make_unique<LatchedResource>( &latch ).release(); // 다른 스레드가 sw_delete 로 지운다
    test::ScopedAssertCapture assertCapture;
    std::thread               destroyThread( [&latch, pResource]()
    {
        while ( latch._stage.load() != 1 )
            ThreadUtil::yield();
        sw_delete( pResource );
        latch._stage.store( 2 );
    } );
    test::FakeRHIDevice       device;
    RHIRenderResource::releaseAllForDevice( &device );
    destroyThread.join();
    if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
}

/**
 * @brief [RHIDeviceTest] 백엔드 이름 · 셰이더 타깃 · 정적 능력 조회는 모두 기술자 표 한 줄을 읽고, 표에 없는 값은 "Unknown" 이고 만들 수 없다
 */
SW_TEST_CASE( RHIDeviceTest, BackendTypeNameQuery )
{
    SW_EXPECT_EQUAL( string( "DirectX11" ), string( RHI::getBackendTypeName( RHIBackend::DirectX11 ) ) );
    SW_EXPECT_EQUAL( string( "DirectX12" ), string( RHI::getBackendTypeName( RHIBackend::DirectX12 ) ) );
    SW_EXPECT_EQUAL( string( "Vulkan" ), string( RHI::getBackendTypeName( RHIBackend::Vulkan ) ) );
    SW_EXPECT_EQUAL( string( "OpenGL" ), string( RHI::getBackendTypeName( RHIBackend::OpenGL ) ) );

    for ( const RHIBackendInfo& info : RHIBackendInfoTable::kArrInfo )
    {
        SW_EXPECT_EQUAL( string( info._pName ), string( RHI::getBackendTypeName( info._backend ) ) );
        SW_EXPECT_TRUE( RHI::getShaderTargetFormat( info._backend ) == info._shaderTarget );
        SW_EXPECT_TRUE( RHIAvailability::query( info._backend )._bGPUCulling == info._capabilities._bGPUCulling );
    }
    const RHIBackend outOfRange = static_cast<RHIBackend>( SW_COUNT_OF( RHIBackendInfoTable::kArrInfo ) );
    SW_EXPECT_EQUAL( string( "Unknown" ), string( RHI::getBackendTypeName( outOfRange ) ) );
    SW_EXPECT_FALSE( RHIAvailability::isAvailable( outOfRange ) );
}

/**
 * @brief [RHIDeviceTest] bindless 는 네 백엔드가 모두 광고하고, 네이티브 bindless 는 DX12 · Vulkan 만 광고한다
 */
SW_TEST_CASE( RHIDeviceTest, CapabilityMatrixNativeVsEmulated )
{
    const RHICapabilities dx12 = RHIAvailability::query( RHIBackend::DirectX12 );
    SW_EXPECT_TRUE( dx12._bBindless != SW_FALSE );
    SW_EXPECT_TRUE( dx12._bNativeBindless != SW_FALSE );

    const RHICapabilities dx11 = RHIAvailability::query( RHIBackend::DirectX11 );
    SW_EXPECT_TRUE( dx11._bBindless != SW_FALSE );
    SW_EXPECT_TRUE( dx11._bNativeBindless == SW_FALSE );

    const RHICapabilities gl = RHIAvailability::query( RHIBackend::OpenGL );
    SW_EXPECT_TRUE( gl._bBindless != SW_FALSE );
    SW_EXPECT_TRUE( gl._bNativeBindless == SW_FALSE );

    const RHICapabilities vk = RHIAvailability::query( RHIBackend::Vulkan );
    SW_EXPECT_TRUE( vk._bBindless != SW_FALSE );
    SW_EXPECT_TRUE( vk._bNativeBindless != SW_FALSE );
}

/**
 * @brief [RHIDeviceTest] 선택 능력(타임스탬프 · 컨텍스트 스레드 바인딩)이 없는 디바이스는 둘 다 nullptr 이고, 배타 컨텍스트 스레드도 요구하지 않는다
 * @details `requiresExclusiveContextThread` 는 바인딩 능력이 있는지와 같은 답이어야 한다. 실제 백엔드의 답은 백엔드를 세우는 시나리오가 본다.
 */
SW_TEST_CASE( RHIDeviceTest, DeviceWithoutOptionalCapabilitiesAnswersNullptr )
{
    test::FakeRHIDevice device;
    SW_EXPECT_TRUE_MSG( device.findTimestampProvider() == nullptr, "a device without timestamps must answer nullptr" );
    SW_EXPECT_TRUE_MSG( device.findContextThreadBinding() == nullptr, "a device without a thread-bound context must answer nullptr" );
    SW_EXPECT_TRUE_MSG( device.requiresExclusiveContextThread() == false, "no context binding means no exclusive context thread" );
}

/**
 * @brief [RHIDeviceShutdownTest] `IRHIDevice::shutdown` 이 종료 단계를 정한 순서로 한 번씩 부른다
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
    SW_REQUIRE_EQUAL( SW_COUNT_OF( arrExpected ), device._listShutdownStep.size() );
    for ( size_t index = 0; index < SW_COUNT_OF( arrExpected ); ++index )
    {
        SW_EXPECT_EQUAL( string( arrExpected[index] ), string( device._listShutdownStep[index] ) );
    }
}

/**
 * @brief [RHIDeviceShutdownTest] 종료가 끝났는데 해제 큐에 남은 것이 있으면 단언한다 — 그 해제는 네이티브 디바이스가 사라진 뒤에 불린다
 * @details 해제 큐는 2 단계(GPU 대기)에서 비워야 한다. 남은 채로 3 · 4 단계를 지나면 해제 큐의 소멸자가 죽은 디바이스로 콜백을 부른다.
 *          `SW_LOG_ASSERT` 는 모든 구성에서 오류 로그를 남기고, 멈추는 단언(수)은 Debug 에만 있다.
 */
SW_TEST_CASE( RHIDeviceShutdownTest, PendingReleasesAfterShutdownAssert )
{
    SW_EXPECT_LOG( SW_ASSERT_LOG_ID );
    test::FakeRHIDevice device;
    device._pendingReleaseCount = 2;
    {
        test::ScopedAssertCapture assertCapture;
        device.shutdown();
        if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
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
 * @brief [RHIDeviceShutdownTest] 끝난 종료 뒤에 넣은 해제는 다음 종료(백엔드 소멸자)가 먼저 단언한다 — 그 해제를 부를 네이티브 디바이스가 없다
 */
SW_TEST_CASE( RHIDeviceShutdownTest, ReleasesQueuedAfterShutdownAssert )
{
    SW_EXPECT_LOG( SW_ASSERT_LOG_ID );
    test::FakeRHIDevice device;
    device.shutdown();
    device._pendingReleaseCount = 1; // 종료 뒤에 누군가 해제를 넣었다(가짜는 비우지 않으므로 이후 단계 단언도 함께 센다)
    {
        test::ScopedAssertCapture assertCapture;
        device.shutdown();
        if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_EQUAL( 3u, assertCapture.getCount() ); // 종료 뒤에 쌓인 것 · GPU 대기 뒤 · 4 단계 뒤
        else
            SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
    }
    device._pendingReleaseCount = 0; // 소멸자의 종료는 조용히
}

/**
 * @brief [RHIDeferredHandleTest] 렌더 스레드가 프레임을 들고 있는 동안 다른 스레드가 내리는 핸들은 미뤄지고, 프레임이 끝난 뒤 · `waitIdle` 이 내린다
 * @details 게임 스레드가 GPU 머티리얼(인스턴스)의 마지막 소유를 놓는 순간 렌더 스레드가 병렬 기록 중이면 bindless 표를 바꾸면 안 된다 — 핫 리로드로 뷰를
 *          걷은 게임들이 Debug 단언으로 죽었다. 렌더 스레드 자신 · 쉬는 렌더 스레드에서는 바로 내린다.
 */
SW_TEST_CASE( RHIDeferredHandleTest, ReleasesWaitForTheRenderThreadFrame )
{
    test::FakeRHIDevice device;

    // 렌더 스레드가 든 프레임이 없으면 바로 내린다.
    device.releaseBindlessIndex( RHIBindlessIndexSpace::Buffer, 7 );
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // 프레임을 넘긴 뒤 다른 스레드(렌더 스레드가 아니다)가 내리면 미룬다.
    device.notifyRenderFrameQueued();
    device.releaseBindlessIndex( RHIBindlessIndexSpace::Buffer, 8 );
    device.releaseHandle( RHIBufferHandle{ 9 } );
    device.releaseHandle( RHIBufferHandle{} ); // 빈 핸들은 무시
    SW_EXPECT_EQUAL( size_t( 2 ), device.getDeferredHandleCount() );

    // 프레임이 끝나도 렌더 스레드가 비우기 전까지는 남는다 — 비우면 내린다.
    device.notifyRenderFrameRetired();
    SW_EXPECT_EQUAL( size_t( 2 ), device.getDeferredHandleCount() );
    device.flushDeferredHandleReleases();
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // 남는 프레임 통보는 셈을 음수로 만들지 않는다.
    device.notifyRenderFrameRetired();
    device.releaseHandle( RHITextureHandle{ 10 } );
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // waitIdle 은 렌더 스레드를 비운 뒤 미룬 것을 내린다.
    device.notifyRenderFrameQueued();
    device.releaseBindlessIndex( RHIBindlessIndexSpace::Texture, 11 );
    SW_EXPECT_EQUAL( size_t( 1 ), device.getDeferredHandleCount() );
    device.waitIdle( RHIWaitIdleReason::Tool );
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );
    device.notifyRenderFrameRetired(); // 위에서 넘긴 프레임을 끝낸다
}

/**
 * @brief [RHIDeferredHandleTest] 렌더 스레드 비우기(`flushRenderThread`)는 GPU 를 기다리지도, 미룬 핸들을 내리지도 않는다 — 옛 자원은 패킷 · GPU 뒤로 계속 미뤄진다
 * @details 씬 활성화 · 에셋 핫 리로드 · 캡처가 쓰는 길이다. 비우기가 핸들을 내리면 게임 스레드가 짓고 있는 패킷의 옛 인덱스가 다른 텍스처로 다시 쓰인다.
 */
SW_TEST_CASE( RHIDeferredHandleTest, DrainRenderThreadWaitsForGPUIdle )
{
    test::FakeRHIDevice device;
    device.notifyRenderFrameQueued();
    device.releaseBindlessIndex( RHIBindlessIndexSpace::Texture, 13 );

    device.flushRenderThread();
    SW_EXPECT_EQUAL( size_t( 1 ), device.getDeferredHandleCount() );
    SW_EXPECT_EQUAL( uint64( 0 ), device.getGPUIdleWaitCount().get() );
    SW_EXPECT_TRUE( device._listShutdownStep.empty() ); // `waitIdleInternal` 이 불리지 않았다

    device.waitIdle( RHIWaitIdleReason::Tool );
    SW_EXPECT_EQUAL( uint64( 1 ), device.getGPUIdleWaitCount().get() );
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );
    device.notifyRenderFrameRetired();
}

/**
 * @brief [RHIDeferredHandleTest] 해제는 그때 든 프레임이 아니라 **다음에 넘길 프레임**이 끝나야 내린다 — 렌더 스레드가 쉬고 있어도 미룬다
 * @details 게임 스레드가 지었지만 아직 넘기지 않은 패킷도 옛 인덱스를 값으로 들고 있다(배치의 머티리얼 텍스처 SRV). 로드 중 텍스처를 진짜로 바꾼 머티리얼이
 *          그 텍스처를 놓자 바로 내려, 다음 패킷의 그림자 · 불투명 패스가 죽은 슬롯을 읽었다.
 */
SW_TEST_CASE( RHIDeferredHandleTest, ReleasesWaitForThePacketBeingBuilt )
{
    test::FakeRHIDevice device;

    // 프레임 A 가 든 동안 해제한다 — 게임 스레드는 다음 패킷 B 를 짓는 중이다. A 가 끝나도 B 가 남았다.
    device.notifyRenderFrameQueued();
    device.releaseBindlessIndex( RHIBindlessIndexSpace::Texture, 12 );
    device.notifyRenderFrameRetired();
    SW_EXPECT_EQUAL( size_t( 1 ), device.getDeferredHandleCount() );
    device.notifyRenderFrameQueued();
    device.notifyRenderFrameRetired();
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // 렌더 스레드가 떠 있으면 든 프레임이 없어도 미룬다(짓는 중인 패킷이 있다). 다음 프레임이 끝나면 내린다.
    device.setRenderThreadFlush( []( void* ) {}, nullptr, std::thread::id{} );
    device.releaseHandle( RHITextureHandle{ 13 } );
    SW_EXPECT_EQUAL( size_t( 1 ), device.getDeferredHandleCount() );
    device.notifyRenderFrameQueued();
    device.notifyRenderFrameRetired();
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // 렌더 스레드를 풀면 남은 것을 모두 내린다.
    device.releaseHandle( RHITextureHandle{ 14 } );
    device.setRenderThreadFlush( nullptr, nullptr, std::thread::id{} );
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );
}

/**
 * @brief [RHINativeHandlesTest] `IRHIDevice::queryNativeHandles` 가 다른 판의 `RHINativeHandles` 를 거절하고, 맞는 판에는 백엔드 훅이 채운 값을 준다
 * @details 에디터(EditorModule)는 RHI 백엔드의 구체 클래스를 모르고 이 POD 하나로 네이티브 핸들을 받는다. 따로 지은 모듈이 다른 판의 구조체를
 *          보내면 Engine 이 한 칸도 쓰지 않고 거절해야 한다 — 쓰면 다른 레이아웃 위에 핸들을 적어 조용히 깨진다. 판 번호와 크기를 각각 어긋나게 한다.
 */
SW_TEST_CASE( RHINativeHandlesTest, QueryRejectsAnotherLayoutAndFillsTheMatchingOne )
{
    test::FakeRHIDevice device;
    int32               nativeDeviceStandIn{ 0 };
    device._pNativeDevice = &nativeDeviceStandIn;

    SW_EXPECT_LOG( "RHI.NativeHandlesLayoutMismatch" );
    SW_EXPECT_LOG( "RHI.NativeQueryWrongBackend" );

    RHINativeHandles handles{};
    SW_EXPECT_TRUE( device.queryNativeHandles( RHIBackend::DirectX12, handles ) );
    SW_EXPECT_TRUE( handles._pDevice == &nativeDeviceStandIn );
    SW_EXPECT_TRUE( handles._backend == device.getBackendType() );
    SW_EXPECT_EQUAL( 1u, device._nativeHandleQueryCount );

    RHINativeHandles otherVersion{};
    otherVersion._version = kRHINativeHandlesVersion + 1u;
    RHINativeHandles otherSize{};
    otherSize._byteSize = static_cast<uint32>( sizeof( RHINativeHandles ) ) - static_cast<uint32>( sizeof( void* ) );
    SW_EXPECT_FALSE( device.queryNativeHandles( RHIBackend::DirectX12, otherVersion ) );
    SW_EXPECT_FALSE( device.queryNativeHandles( RHIBackend::DirectX12, otherSize ) );
    SW_EXPECT_TRUE( otherVersion._pDevice == nullptr );
    SW_EXPECT_TRUE( otherSize._pDevice == nullptr );
    SW_EXPECT_EQUAL( 1u, device._nativeHandleQueryCount );

    // 핸들은 백엔드마다 뜻이 다른 `void*` 다 — 호출처가 캐스트할 백엔드가 아니면 판이 맞아도 한 칸도 쓰지 않고, 텍스처 포인터도 주지 않는다.
    // 컨텍스트 콜백도 같은 종류 확인을 거친다.
    RHINativeHandles otherBackend{};
    uint32           contextVisitCount{ 0 };
    const auto       countVisit = SW_DELEGATE_LAMBDA( RHINativeContextVisitor, [&contextVisitCount]( void* /*pContext*/ )
          { ++contextVisitCount; } );
    SW_EXPECT_FALSE( device.queryNativeHandles( RHIBackend::Vulkan, otherBackend ) );
    SW_EXPECT_TRUE( device.tryGetNativeTexturePointer( RHIBackend::DirectX11, 1u ) == nullptr );
    SW_EXPECT_FALSE( device.withNativeContext( RHIBackend::OpenGL, countVisit ) );
    SW_EXPECT_TRUE( otherBackend._pDevice == nullptr );
    SW_EXPECT_EQUAL( 0u, contextVisitCount );
    SW_EXPECT_TRUE( device.withNativeContext( RHIBackend::DirectX12, countVisit ) );
    SW_EXPECT_EQUAL( 1u, contextVisitCount );
    SW_EXPECT_EQUAL( 1u, device._nativeHandleQueryCount );

    // 디바이스가 없는 백엔드는 판이 맞아도 false 다.
    device._pNativeDevice = nullptr;
    RHINativeHandles noDevice{};
    SW_EXPECT_FALSE( device.queryNativeHandles( RHIBackend::DirectX12, noDevice ) );
}

/**
 * @brief [VulkanAPIVersionTest] 물리 디바이스 선택이 요구 판(셰이더 쿠킹 타깃) 미만의 디바이스를 거른다
 * @details 셰이더는 vulkan1.2 타깃(SPIR-V 1.5)으로 쿠킹한다. 1.1 디바이스는 SPIR-V 1.3 까지만 받으므로 그 디바이스를 고르면 셰이더 모듈 전부가 무효다.
 *          맨 위 변형 비트는 판 비교에 넣지 않는다. 요구 판이 1.2 인 것도 본다 — Android 기기 다수와 dzn 이 1.2 라 올리면 그 기기를 잃는다
 *          (올리려면 이 줄과 RHIDescriptors.h 의 까닭을 함께 고친다).
 */
SW_TEST_CASE( VulkanAPIVersionTest, DeviceBelowTheShaderTargetIsRejected )
{
    using constant::render::kVulkanRequiredMajor;
    using constant::render::kVulkanRequiredMinor;
    SW_REQUIRE_TRUE( kVulkanRequiredMinor > 0u );

    const uint32 belowWithPatch = VulkanRHIRequiredVersion::makeAPIVersion( kVulkanRequiredMajor, kVulkanRequiredMinor - 1u ) | 0xFFFu;
    SW_EXPECT_FALSE( VulkanRHIRequiredVersion::isAPIVersionSupported( belowWithPatch ) );
    SW_EXPECT_FALSE( VulkanRHIRequiredVersion::isAPIVersionSupported( belowWithPatch | ( 1u << 29u ) ) );
    SW_EXPECT_TRUE( VulkanRHIRequiredVersion::isAPIVersionSupported( VulkanRHIRequiredVersion::makeAPIVersion( kVulkanRequiredMajor, kVulkanRequiredMinor ) ) );
    SW_EXPECT_TRUE( VulkanRHIRequiredVersion::isAPIVersionSupported( VulkanRHIRequiredVersion::makeAPIVersion( kVulkanRequiredMajor, kVulkanRequiredMinor + 1u ) ) );
    SW_EXPECT_EQUAL( 2u, kVulkanRequiredMinor );
    SW_EXPECT_EQUAL( ( 1u << 16u ) | ( 5u << 8u ), VulkanRHIRequiredVersion::computeSPIRVVersion( kVulkanRequiredMinor ) ); // vulkan1.2 → SPIR-V 1.5
    SW_EXPECT_EQUAL( ( 1u << 16u ) | ( 6u << 8u ), VulkanRHIRequiredVersion::computeSPIRVVersion( 3u ) );                   // vulkan1.3 → SPIR-V 1.6
}

/**
 * @brief [VulkanMemoryAccountTest] 장치 메모리 할당 수가 한도에서 남길 칸에 닿으면 더 받지 않고, 해제하면 다시 받는다(R-4)
 * @details 자원마다 `VkDeviceMemory` 하나라 한도(`maxMemoryAllocationCount`, 데스크톱도 흔히 4096)를 넘기면 드라이버가 실패하거나 정의되지 않은 동작이다 — 예전엔 아무도 세지 않았다.
 */
SW_TEST_CASE( VulkanMemoryAccountTest, RefusesPastTheReserve )
{
    VulkanMemoryAccount account;
    account.setLimit( 10u, 2u );
    SW_EXPECT_EQUAL( 8u, account.getUsableCount() );
    for ( uint32 index = 0; index < 8u; ++index )
    {
        SW_EXPECT_TRUE( account.tryAcquire() );
    }
    SW_EXPECT_FALSE( account.tryAcquire() );
    SW_EXPECT_EQUAL( 8u, account.getLiveCount() );
    account.release();
    SW_EXPECT_TRUE( account.tryAcquire() );

    VulkanMemoryAccount reserveOverLimit;
    reserveOverLimit.setLimit( 4u, 4u );
    SW_EXPECT_FALSE( reserveOverLimit.tryAcquire() );
}

/**
 * @brief [VulkanPipelineCacheFileTest] 파이프라인 캐시 파일이 장치 · 드라이버마다 따로다(R-13)
 * @details 파일이 하나면 하드웨어 → software → 하드웨어 실행이 하드웨어 캐시를 덮어(드라이버가 다른 장치의 데이터를 버린다) 매번 처음부터 컴파일한다.
 */
SW_TEST_CASE( VulkanPipelineCacheFileTest, FileIsPerDeviceAndDriver )
{
    const string hardware = VulkanPipelineCacheFile::makeRelativePath( 0x10DEu, 0x2684u, 0x8A3C0000u );
    SW_EXPECT_TRUE( hardware == "ShaderCache/vk_pipeline_cache_000010de_00002684_8a3c0000.bin" );
    SW_EXPECT_FALSE( hardware == VulkanPipelineCacheFile::makeRelativePath( 0x10005u, 0u, 1u ) );              // software(lavapipe)
    SW_EXPECT_FALSE( hardware == VulkanPipelineCacheFile::makeRelativePath( 0x10DEu, 0x2684u, 0x8A3C0001u ) ); // 드라이버 갱신
}

/**
 * @brief [VulkanPipelineCacheFileTest] 파이프라인 캐시는 주기가 지나고 크기가 바뀌었을 때 저장한다(R-13)
 * @details 예전엔 종료 때만 저장해 실행 중에 죽으면 그 실행의 컴파일이 모두 사라졌다. 크기가 그대로면 새 파이프라인이 없으므로 디스크를 건드리지 않는다.
 */
SW_TEST_CASE( VulkanPipelineCacheFileTest, PeriodicSaveNeedsIntervalAndGrowth )
{
    constexpr int64 kInterval = 1000;
    SW_EXPECT_TRUE( VulkanPipelineCacheFile::isPeriodicSaveDue( 1000, 0, kInterval, 200u, 100u ) );
    SW_EXPECT_FALSE( VulkanPipelineCacheFile::isPeriodicSaveDue( 999, 0, kInterval, 200u, 100u ) );  // 주기 전
    SW_EXPECT_FALSE( VulkanPipelineCacheFile::isPeriodicSaveDue( 5000, 0, kInterval, 100u, 100u ) ); // 새 파이프라인 없음
    SW_EXPECT_FALSE( VulkanPipelineCacheFile::isPeriodicSaveDue( 5000, 0, 0, 200u, 100u ) );         // 주기 없음(설정 누락은 초기화가 막는다)
}

/** @brief [RHIAvailabilityTest] 다른 백엔드: 지금 것이 아니고 이 플랫폼에서 되는 것이다. Windows 는 DX12 ↔ DX11, Linux 는 Vulkan ↔ OpenGL 이다 */
SW_TEST_CASE( RHIAvailabilityTest, OtherBackendFitsThePlatform )
{
    sw::RHIBackend other{};
#if defined( SW_PLATFORM_WINDOWS )
    SW_REQUIRE_TRUE( sw::RHIAvailability::findOtherBackend( sw::RHIBackend::DirectX12, other ) );
    SW_EXPECT_TRUE( other == sw::RHIBackend::DirectX11 );
    SW_REQUIRE_TRUE( sw::RHIAvailability::findOtherBackend( sw::RHIBackend::Vulkan, other ) );
    SW_EXPECT_TRUE( other == sw::RHIBackend::DirectX12 );
#elif defined( SW_PLATFORM_LINUX )
    SW_REQUIRE_TRUE( sw::RHIAvailability::findOtherBackend( sw::RHIBackend::Vulkan, other ) );
    SW_EXPECT_TRUE( other == sw::RHIBackend::OpenGL );
    SW_REQUIRE_TRUE( sw::RHIAvailability::findOtherBackend( sw::RHIBackend::OpenGL, other ) );
    SW_EXPECT_TRUE( other == sw::RHIBackend::Vulkan );
#else
    #error "Unsupported platform"
#endif
}

/**
 * @brief [RHIDeviceTest] UI 대상 포맷은 SDR 백버퍼 포맷이다 — BGRA8 백버퍼에서 HDR10 으로 바꿔도 BGRA8 이고(백버퍼는 R10G10B10A2), SDR 로 돌아와도 같다.
 *        결함이면 에디터 UI · Canvas PSO 가 HDR 백버퍼 포맷이나 RGBA8 로 만들어져 UI RT 와 렌더 타깃 포맷이 어긋난다
 */
SW_TEST_CASE( RHIDeviceTest, UITargetFormatStaysSDRUnderHDR )
{
    test::FakeRHIDevice device;
    device._sdrBackBufferFormatForTest = RHIFormat::B8G8R8A8_UNORM;
    device._displayOutputSupport       = static_cast<uint8>( makeColorSpaceBit( RHIColorSpace::SRGBNonLinear ) | makeColorSpaceBit( RHIColorSpace::HDR10ST2084 ) );
    SW_EXPECT_TRUE( device.getUITargetFormat() == RHIFormat::B8G8R8A8_UNORM );

    SW_REQUIRE_TRUE( device.setBackBufferColorSpace( RHIColorSpace::HDR10ST2084, RHIDisplayMetadata{} ) );
    SW_EXPECT_TRUE( device.getBackBufferFormat() == RHIFormat::R10G10B10A2_UNORM );
    SW_EXPECT_TRUE( device.getUITargetFormat() == RHIFormat::B8G8R8A8_UNORM );

    SW_REQUIRE_TRUE( device.setBackBufferColorSpace( RHIColorSpace::SRGBNonLinear, RHIDisplayMetadata{} ) );
    SW_EXPECT_TRUE( device.getUITargetFormat() == RHIFormat::B8G8R8A8_UNORM );
}
