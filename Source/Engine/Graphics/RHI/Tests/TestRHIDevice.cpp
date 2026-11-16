/**
 * @file TestRHIDevice.cpp
 * @brief GPU 없이 보는 `IRHIDevice` 계약 — 백엔드 표 조회, 종료 단계 순서, 미룬 핸들 해제, 네이티브 핸들 조회, 선택 능력, Vulkan 요구 판입니다.
 * @details 디바이스가 필요한 자리는 가짜 디바이스(`RHIFakeDevice.h`)로 대신한다. 실제 백엔드를 세우는 확인(업로드 · 읽기 · 그리기)은 시나리오가 한다.
 */
#include "pch.h"

#include "Core/Delegate/Delegate.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIRequiredVersion.h"

#include <iterator>

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
    const RHIBackend outOfRange = static_cast<RHIBackend>( std::size( RHIBackendInfoTable::kArrInfo ) );
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
    SW_REQUIRE_EQUAL( std::size( arrExpected ), device._listShutdownStep.size() );
    for ( size_t index = 0; index < std::size( arrExpected ); ++index )
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
    device.releaseHandle( RHIHandleKind::BindlessResource, 7 );
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // 프레임을 넘긴 뒤 다른 스레드(렌더 스레드가 아니다)가 내리면 미룬다.
    device.notifyRenderFrameQueued();
    device.releaseHandle( RHIHandleKind::BindlessResource, 8 );
    device.releaseHandle( RHIHandleKind::Buffer, 9 );
    device.releaseHandle( RHIHandleKind::Buffer, 0 ); // 빈 핸들은 무시
    SW_EXPECT_EQUAL( size_t( 2 ), device.getDeferredHandleCount() );

    // 프레임이 끝나도 렌더 스레드가 비우기 전까지는 남는다 — 비우면 내린다.
    device.notifyRenderFrameRetired();
    SW_EXPECT_EQUAL( size_t( 2 ), device.getDeferredHandleCount() );
    device.flushDeferredHandleReleases();
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // 남는 프레임 통보는 셈을 음수로 만들지 않는다.
    device.notifyRenderFrameRetired();
    device.releaseHandle( RHIHandleKind::Texture, 10 );
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // waitIdle 은 렌더 스레드를 비운 뒤 미룬 것을 내린다.
    device.notifyRenderFrameQueued();
    device.releaseHandle( RHIHandleKind::BindlessTexture, 11 );
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
    device.releaseHandle( RHIHandleKind::BindlessTexture, 13 );

    device.flushRenderThread();
    SW_EXPECT_EQUAL( size_t( 1 ), device.getDeferredHandleCount() );
    SW_EXPECT_EQUAL( uint64( 0 ), device.getGPUIdleWaitCount() );
    SW_EXPECT_TRUE( device._listShutdownStep.empty() ); // `waitIdleInternal` 이 불리지 않았다

    device.waitIdle( RHIWaitIdleReason::Tool );
    SW_EXPECT_EQUAL( uint64( 1 ), device.getGPUIdleWaitCount() );
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
    device.releaseHandle( RHIHandleKind::BindlessTexture, 12 );
    device.notifyRenderFrameRetired();
    SW_EXPECT_EQUAL( size_t( 1 ), device.getDeferredHandleCount() );
    device.notifyRenderFrameQueued();
    device.notifyRenderFrameRetired();
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // 렌더 스레드가 떠 있으면 든 프레임이 없어도 미룬다(짓는 중인 패킷이 있다). 다음 프레임이 끝나면 내린다.
    device.setRenderThreadFlush( []( void* ) {}, nullptr, std::thread::id{} );
    device.releaseHandle( RHIHandleKind::Texture, 13 );
    SW_EXPECT_EQUAL( size_t( 1 ), device.getDeferredHandleCount() );
    device.notifyRenderFrameQueued();
    device.notifyRenderFrameRetired();
    SW_EXPECT_EQUAL( size_t( 0 ), device.getDeferredHandleCount() );

    // 렌더 스레드를 풀면 남은 것을 모두 내린다.
    device.releaseHandle( RHIHandleKind::Texture, 14 );
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
