#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/Support/RHIConstantBufferShadow.h"
#include "Engine/Graphics/RHI/Support/RHIGpuTimestamp.h"
#include "Engine/Graphics/RHI/Support/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"
#include "Engine/Graphics/RHI/Support/RHIShaderRequest.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIApiVersion.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/RHIFakeDevice.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief `releaseRhi` 를 받으면 디바이스의 종료 단계 목록에 적는 시험 자원입니다. */
    class ShutdownStepRecordingResource final : public sw::RHIRenderResource
    {
    public:
        explicit ShutdownStepRecordingResource( test::FakeRHIDevice* pDevice )
            : _pDevice{ pDevice }
        {
        }

        void releaseRhi( sw::IRHIDevice* pDevice ) override
        {
            if ( pDevice == _pDevice )
                _pDevice->_listShutdownStep.push_back( "releaseRhi" );
        }
        void forgetRhi( sw::IRHIDevice* ) override {}

    private:
        test::FakeRHIDevice* _pDevice;
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
 * @brief [RHIShaderRequestTest] 파이프라인 서술체를 컴파일 요청으로 해석하는 규칙 — 네 백엔드가 같이 쓴다.
 * @details 진입점 기본값, define 은 두 스테이지에, RT 0 개 + 뎁스 테스트면 픽셀 스테이지 없음(경로가 있어도),
 *          RT 수는 뎁스 전용 0 / 그 밖 1 이상. 백엔드마다 규칙을 복사하면 어느 하나가 define 이나 뎁스 전용 판정을
 *          빠뜨린다 — 규칙이 한 곳이면 빠질 자리가 없다. GPU 가 필요 없다(nogpu).
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
 *          가드는 이 헬퍼 한 자리에 있다 — 백엔드 · 자원 종류마다 손으로 적으면 어느 한 종류에서 가드가 빠져
 *          그 종류의 이중 해제만 통과한다.
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

    // 범위 밖은 조용히 무시한다(로그도 남기지 않는다).
    SW_EXPECT_EQUAL( 0u, sw::releaseFreeListIndex( listRegistered, listFree, 9999u, 0u, "test" ) );
}

/**
 * @brief [RHIGpuTimestampTest] 기준점은 가장 이른 틱이고, 안 적힌 칸은 음수다
 */
SW_TEST_CASE( RHIGpuTimestampTest, OriginIsEarliestTickAndUnwrittenSlotsAreNegative )
{
    uint64 arrTick[sw::constant::kMaxGpuTimestampSlot]{};
    arrTick[0]             = 1000; // 낮은 번호지만 늦은 시각 — 이것을 기준으로 삼으면 슬롯 5 가 음수(→ 0)가 된다
    arrTick[5]             = 400;
    arrTick[31]            = 1600;
    const uint32 readyMask = ( 1u << 0 ) | ( 1u << 5 ) | ( 1u << 31 );

    sw::vector<float32> listMicro;
    SW_ASSERT_TRUE( sw::RHIGpuTimestamp::resolveMicro( arrTick, readyMask, 0.5, listMicro ) );
    SW_ASSERT_TRUE( listMicro.size() == sw::constant::kMaxGpuTimestampSlot );
    SW_EXPECT_NEAR_EQUAL( 300.0f, listMicro[0], 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listMicro[5], 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 600.0f, listMicro[31], 1e-3f );
    SW_EXPECT_TRUE( listMicro[1] < 0.0f && listMicro[30] < 0.0f );

    // 준비된 칸이 없으면 비운다 — 호출자는 빈 목록을 "이번 프레임 없음" 으로 읽는다.
    SW_EXPECT_TRUE( sw::RHIGpuTimestamp::resolveMicro( arrTick, 0, 0.5, listMicro ) == false );
    SW_EXPECT_TRUE( listMicro.empty() );
}

/**
 * @brief [RHIConstantBufferShadowTest] 한 번 쓴 값이 링이 도는 동안 나머지 칸에 채워지고, 다 채우면 목록에서 빠진다
 * @details 링 상수버퍼의 칸 `kMaxFrameCountInFlight` 개를 배열로 흉내 낸다. 칸 0 에 쓰고 링을 한 바퀴 돌리면 모든 칸이 같은 값이어야
 *          한다. 채우는 도중 짧은 쓰기는 앞부분만 덮고, 부순 버퍼는 채우지 않는다.
 */
SW_TEST_CASE( RHIConstantBufferShadowTest, FillsStaleSlotsOnceAndForgets )
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

    sw::RHIConstantBufferShadow shadow;
    const uint8                 arrFirst[kSlotBytes] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    shadow.write( 7, 0, arrFirst, kSlotBytes, writeSlot );
    SW_EXPECT_EQUAL( 1u, writeCount );
    SW_EXPECT_EQUAL( 1u, shadow.getPendingBufferCount() );

    // 같은 칸으로 돌아와도(아직 다른 칸이 남았다) 다시 쓰지 않는다.
    shadow.fillSlot( 0, writeSlot );
    SW_EXPECT_EQUAL( 1u, writeCount );

    for ( uint32 slot = 1; slot < kSlotCount; ++slot )
        shadow.fillSlot( slot, writeSlot );
    SW_EXPECT_EQUAL( kSlotCount, writeCount );
    for ( uint32 slot = 0; slot < kSlotCount; ++slot )
        SW_EXPECT_TRUE( sw::Memory::compare( arrSlot[slot], arrFirst, kSlotBytes ) == 0 );
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
        shadow.fillSlot( slot, writeSlot );
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

    const utf8* const arrExpected[] = { "releaseRhi", "waitIdleInternal", "detachCommandRecordingInternal", "shutdownInternal" };
    SW_ASSERT_EQUAL( static_cast<size_t>( std::size( arrExpected ) ), device._listShutdownStep.size() );
    for ( size_t index = 0; index < std::size( arrExpected ); ++index )
        SW_EXPECT_STREQ( arrExpected[index], device._listShutdownStep[index] );
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
 * @brief [VulkanApiVersionTest] 물리 디바이스 선택이 요구 판(셰이더 굽기 타깃) 미만의 디바이스를 거른다.
 * @details 셰이더는 vulkan1.3 타깃(SPIR-V 1.6)으로 굽는다. 1.2 디바이스는 SPIR-V 1.5 까지만 받으므로 그 디바이스를 고르면 셰이더 모듈 전부가 무효다.
 *          맨 위 변형 비트는 판 비교에 넣지 않는다.
 */
SW_TEST_CASE( VulkanApiVersionTest, DeviceBelowTheShaderTargetIsRejected )
{
    using sw::VulkanRHIApiVersion;
    const uint32 kRequiredMajor = VulkanRHIApiVersion::kRequiredMajor;
    const uint32 kRequiredMinor = VulkanRHIApiVersion::kRequiredMinor;
    SW_ASSERT_TRUE( kRequiredMinor > 0u );

    const uint32 belowWithPatch = VulkanRHIApiVersion::makeApiVersion( kRequiredMajor, kRequiredMinor - 1u ) | 0xFFFu;
    SW_EXPECT_FALSE( VulkanRHIApiVersion::isApiVersionSupported( belowWithPatch ) );
    SW_EXPECT_FALSE( VulkanRHIApiVersion::isApiVersionSupported( belowWithPatch | ( 1u << 29u ) ) );
    SW_EXPECT_TRUE( VulkanRHIApiVersion::isApiVersionSupported( VulkanRHIApiVersion::makeApiVersion( kRequiredMajor, kRequiredMinor ) ) );
    SW_EXPECT_TRUE( VulkanRHIApiVersion::isApiVersionSupported( VulkanRHIApiVersion::makeApiVersion( kRequiredMajor, kRequiredMinor + 1u ) ) );
}

/**
 * @brief [VulkanApiVersionTest] 구운 Vulkan SPIR-V 의 판이 디바이스 요구 판의 타깃과 같다.
 * @details 셰이더 굽기 타깃(`-fspv-target-env`)과 물리 디바이스의 최소 판은 `VulkanRHIApiVersion` 하나에서 나온다. 한쪽만 바꾸거나 다시 굽지 않으면
 *          디바이스가 받지 못하는 판의 모듈이 남는다. 커밋된 vulkan 폴더 .spv 헤더의 판을 읽어 대조한다.
 */
SW_TEST_CASE( VulkanApiVersionTest, BakedVulkanSpirvMatchesTheRequiredApi )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    constexpr uint32 kSpirvMagic     = 0x07230203u;
    const uint32     expectedVersion = sw::VulkanRHIApiVersion::computeSpirvVersion( sw::VulkanRHIApiVersion::kRequiredMinor );
    const utf8*      arrDomain[]     = { "engine", "common" };

    uint32 checkedCount{ 0 };
    for ( const utf8* pDomain : arrDomain )
    {
        const sw::string       binDirectory = sw::FileUtil::joinPath( sw::ResourceUtil::getRootFolderPath(), sw::string( pDomain ) + "/shaders/bin/vulkan" );
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
            SW_EXPECT_TRUE_MSG( version == expectedVersion, ( spirvPath + ": 디바이스 요구 판과 다른 SPIR-V 판으로 구워졌다 — 다시 구울 것" ).c_str() );
            ++checkedCount;
        }
    }
    if ( checkedCount == 0 )
        SW_TEST_SKIP( "No baked Vulkan SPIR-V found (App.exe --bake-shaders)" );
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
        expectedMipBytes += static_cast<uint64>( size ) * size * 4ull;
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
        SW_EXPECT_TRUE( sw::string_view( sw::RHIMemoryLedger::getKindName( static_cast<sw::RHIMemoryKind>( kindIndex ) ) ) != "Invalid" );
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
