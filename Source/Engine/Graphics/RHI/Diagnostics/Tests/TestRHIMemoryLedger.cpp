/**
 * @file TestRHIMemoryLedger.cpp
 * @brief GPU 자원 장부(`RHIMemoryLedger`) 테스트 — 키 공간, 텍스처 줄 · 논리 크기, 정렬, "엔진 밖" 계산, 드라이버 값 갱신, 보고 글입니다.
 */
#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Log/Logger.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryLedger.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

/**
 * @brief [RHIMemoryLedgerTest] 장부는 올린 키의 크기를 기억했다가 내릴 때 그대로 빼고, 크기 모름은 바이트가 아니라 따로 센다
 * @details 버퍼 핸들과 텍스처 핸들은 다른 표라 같은 정수가 둘 다에 있다 — 키 공간이 가르지 않으면 텍스처를 내릴 때 버퍼가 빠진다.
 *          올리지 않은 키를 내리는 것(장부보다 먼저 만든 자원)은 아무것도 바꾸지 않는다. 같은 키를 다시 올리면(해제를 빠뜨림) 옛 값을 빼고 바꾼다.
 */
SW_TEST_CASE( RHIMemoryLedgerTest, FreeSubtractsWhatTheSameKeyAdded )
{
    SW_EXPECT_LOG( "RHI.MemoryKeyRecordedTwice" );
    RHIMemoryLedger  ledger;
    constexpr uint64 kSharedHandle = 0x100000001ull;
    ledger.recordAllocation( RHIMemoryKey::makeBuffer( kSharedHandle ), RHIMemoryKind::Buffer, 1000 );
    ledger.recordAllocation( RHIMemoryKey::makeTexture( kSharedHandle ), RHIMemoryKind::RenderTarget, 4096 );
    int32 poolStandIn{ 0 };
    ledger.recordAllocation( RHIMemoryKey::makeDeviceObject( &poolStandIn ), RHIMemoryKind::Descriptor, kRHIMemoryUnknownBytes );

    SW_EXPECT_EQUAL( 1000ull, ledger.getStats( RHIMemoryKind::Buffer )._liveBytes );
    SW_EXPECT_EQUAL( 4096ull, ledger.getStats( RHIMemoryKind::RenderTarget )._liveBytes );
    SW_EXPECT_EQUAL( 0ull, ledger.getStats( RHIMemoryKind::Descriptor )._liveBytes );
    SW_EXPECT_EQUAL( 0u, ledger.getStats( RHIMemoryKind::Descriptor )._liveCount );
    SW_EXPECT_EQUAL( 1u, ledger.getStats( RHIMemoryKind::Descriptor )._unknownSizeCount );
    SW_EXPECT_EQUAL( 5096ull, ledger.getTrackedBytes() );

    ledger.recordFree( RHIMemoryKey::makeTexture( kSharedHandle ) );
    SW_EXPECT_EQUAL( 0ull, ledger.getStats( RHIMemoryKind::RenderTarget )._liveBytes );
    SW_EXPECT_EQUAL( 1000ull, ledger.getStats( RHIMemoryKind::Buffer )._liveBytes );

    ledger.recordFree( RHIMemoryKey::makeTexture( kSharedHandle ) );
    ledger.recordFree( RHIMemoryKey::makeBuffer( 0x7777ull ) );
    SW_EXPECT_EQUAL( 1000ull, ledger.getTrackedBytes() );

    ledger.recordAllocation( RHIMemoryKey::makeBuffer( kSharedHandle ), RHIMemoryKind::Buffer, 300 ); // 같은 키를 다시 올린다 — 경고와 함께 바꾼다
    SW_EXPECT_EQUAL( 300ull, ledger.getStats( RHIMemoryKind::Buffer )._liveBytes );
    SW_EXPECT_EQUAL( 1u, ledger.getStats( RHIMemoryKind::Buffer )._liveCount );

    ledger.recordFree( RHIMemoryKey::makeDeviceObject( &poolStandIn ) );
    ledger.recordFree( RHIMemoryKey::makeBuffer( kSharedHandle ) );
    for ( uint32 kindIndex = 0; kindIndex < kRHIMemoryKindCount; ++kindIndex )
    {
        const RHIMemoryKindStats stats = ledger.getStats( static_cast<RHIMemoryKind>( kindIndex ) );
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
    RHITextureDesc sampled{};
    sampled._width  = 256;
    sampled._height = 256;
    sampled._format = RHIFormat::R8G8B8A8_UNORM;
    SW_EXPECT_TRUE( RHIMemoryLedger::classifyTexture( sampled ) == RHIMemoryKind::Texture );
    SW_EXPECT_EQUAL( 256ull * 256ull * 4ull, RHIMemoryLedger::computeTextureLogicalBytes( sampled ) );

    RHITextureDesc mipped = sampled;
    mipped._mipLevels     = 9;
    uint64 expectedMipBytes{ 0 };
    for ( uint32 size = 256; size >= 1; size /= 2 )
    {
        expectedMipBytes += static_cast<uint64>( size ) * size * 4ull;
    }
    SW_EXPECT_EQUAL( expectedMipBytes, RHIMemoryLedger::computeTextureLogicalBytes( mipped ) );

    RHITextureDesc compressed{};
    compressed._width  = 6;
    compressed._height = 6;
    compressed._format = RHIFormat::BC1_UNORM;
    SW_EXPECT_EQUAL( 2ull * 2ull * 8ull, RHIMemoryLedger::computeTextureLogicalBytes( compressed ) );

    RHITextureDesc cube{};
    cube._width     = 32;
    cube._height    = 32;
    cube._arraySize = kCubeFaceCount;
    cube._dimension = RHITextureDimension::TextureCube;
    cube._format    = RHIFormat::R16G16B16A16_FLOAT;
    SW_EXPECT_EQUAL( 32ull * 32ull * 8ull * kCubeFaceCount, RHIMemoryLedger::computeTextureLogicalBytes( cube ) );

    RHITextureDesc depth{};
    depth._width           = 64;
    depth._height          = 32;
    depth._format          = RHIFormat::D24_UNORM_S8_UINT;
    depth._bIsDepthStencil = SW_TRUE;
    SW_EXPECT_TRUE( RHIMemoryLedger::classifyTexture( depth ) == RHIMemoryKind::RenderTarget );
    SW_EXPECT_EQUAL( 64ull * 32ull * 4ull, RHIMemoryLedger::computeTextureLogicalBytes( depth ) );

    RHITextureDesc transient = depth;
    transient._bIsTransient  = SW_TRUE;
    SW_EXPECT_TRUE( RHIMemoryLedger::classifyTexture( transient ) == RHIMemoryKind::TransientPool );

    RHITextureDesc unknownFormat = sampled;
    unknownFormat._format        = RHIFormat::Unknown;
    SW_EXPECT_EQUAL( kRHIMemoryUnknownBytes, RHIMemoryLedger::computeTextureLogicalBytes( unknownFormat ) );
}

/**
 * @brief [RHIMemoryLedgerTest] 줄 이름은 enum 값마다 하나이고, 정렬은 바이트가 큰 줄부터다(같으면 enum 순서)
 */
SW_TEST_CASE( RHIMemoryLedgerTest, KindNamesAndOrderByLiveBytes )
{
    for ( uint32 kindIndex = 0; kindIndex < kRHIMemoryKindCount; ++kindIndex )
    {
        SW_EXPECT_TRUE( string_view( RHIMemoryLedger::getKindName( static_cast<RHIMemoryKind>( kindIndex ) ) ) != "Invalid" );
    }
    SW_EXPECT_EQUAL( string( "Invalid" ), string( RHIMemoryLedger::getKindName( RHIMemoryKind::MaxKinds ) ) );

    RHIMemoryLedger ledger;
    ledger.recordAllocation( RHIMemoryKey::makeBuffer( 1 ), RHIMemoryKind::Buffer, 10 );
    ledger.recordAllocation( RHIMemoryKey::makeTexture( 1 ), RHIMemoryKind::TransientPool, 30 );
    ledger.recordAllocation( RHIMemoryKey::makeTexture( 2 ), RHIMemoryKind::Texture, 20 );
    const array<RHIMemoryKind, kRHIMemoryKindCount> arrOrder = ledger.makeKindOrderByLiveBytes();
    SW_EXPECT_TRUE( arrOrder[0] == RHIMemoryKind::TransientPool );
    SW_EXPECT_TRUE( arrOrder[1] == RHIMemoryKind::Texture );
    SW_EXPECT_TRUE( arrOrder[2] == RHIMemoryKind::Buffer );
    SW_EXPECT_TRUE( arrOrder[3] == RHIMemoryKind::RenderTarget );
}

/**
 * @brief [RHIMemoryLedgerTest] "엔진 밖" 은 이 프로세스의 드라이버 사용량이 있을 때만 사용량 − 장부 합으로 계산한다
 * @details 드라이버 값을 모르면(새 장부) 계산하지 않는다 — 0 으로 지어내지 않는다. 디바이스 전체 사용량(GL NVX)은 다른 프로세스 몫이 섞여 빼지 않는다.
 *          장부가 논리 크기라 사용량보다 클 수 있어 결과는 부호가 있다.
 */
SW_TEST_CASE( RHIMemoryLedgerTest, SummaryComputesOutsideOnlyForProcessUsage )
{
    RHIMemoryLedger ledger;
    ledger.recordAllocation( RHIMemoryKey::makeTexture( 1 ), RHIMemoryKind::Texture, 300 );
    ledger.recordAllocation( RHIMemoryKey::makeBuffer( 1 ), RHIMemoryKind::Buffer, kRHIMemoryUnknownBytes );

    const RHIMemorySummary unknown = ledger.makeSummary();
    SW_EXPECT_TRUE( unknown._budget._bUsageKnown == SW_FALSE && unknown._budget._bBudgetKnown == SW_FALSE );
    SW_EXPECT_TRUE( unknown._bOutsideKnown == SW_FALSE );
    SW_EXPECT_EQUAL( 300ull, unknown._trackedBytes );
    SW_EXPECT_EQUAL( 1u, unknown._unknownSizeCount );

    RHIMemoryBudget processBudget{};
    processBudget._usageBytes  = 1000;
    processBudget._bUsageKnown = SW_TRUE;
    processBudget._scope       = RHIMemoryScope::Process;
    ledger.setDriverBudget( processBudget );
    const RHIMemorySummary process = ledger.makeSummary();
    SW_EXPECT_TRUE( process._bOutsideKnown == SW_TRUE );
    SW_EXPECT_EQUAL( 700ll, process._outsideBytes );

    processBudget._usageBytes = 100;
    ledger.setDriverBudget( processBudget );
    SW_EXPECT_EQUAL( -200ll, ledger.makeSummary()._outsideBytes );

    RHIMemoryBudget deviceBudget = processBudget;
    deviceBudget._scope          = RHIMemoryScope::Device;
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
    RHIMemoryBudget     stale{};
    stale._usageBytes  = 4096;
    stale._bUsageKnown = SW_TRUE;
    device.getMemoryLedger().setDriverBudget( stale );

    device.refreshGPUMemoryBudget();
    const RHIMemoryBudget budget = device.getMemoryLedger().getDriverBudget();
    SW_EXPECT_TRUE( budget._bUsageKnown == SW_FALSE && budget._bBudgetKnown == SW_FALSE && budget._bAvailableKnown == SW_FALSE );
    SW_EXPECT_EQUAL( 0ull, budget._usageBytes );
}

#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
/**
 * @brief [RHIMemoryLedgerTest] `-gv_profileFrames` 의 GPU 표는 머리 · 쓰인 줄 · 드라이버 줄 · 엔진 밖 줄을 내고, 모르는 값은 "모름" 으로 찍는다
 * @details 장부에 값이 있는 줄(크기 모름만 있는 줄 포함)만 내고 빈 줄은 내지 않는다. 드라이버 사용량이 디바이스 전체 값이면 엔진 밖은 계산하지 않는다.
 *          Info 로그가 컴파일되는 구성에서만 짓는다.
 */
SW_TEST_CASE( RHIMemoryLedgerTest, ReportPrintsUsedKindsAndUnknownDriverValues )
{
    RHIMemoryLedger ledger;
    ledger.recordAllocation( RHIMemoryKey::makeTexture( 1 ), RHIMemoryKind::TransientPool, 3ull * 1024ull * 1024ull );
    int32 poolStandIn{ 0 };
    ledger.recordAllocation( RHIMemoryKey::makeDeviceObject( &poolStandIn ), RHIMemoryKind::Descriptor, kRHIMemoryUnknownBytes );
    RHIMemoryBudget deviceBudget{};
    deviceBudget._availableBytes  = 8ull * 1024ull * 1024ull;
    deviceBudget._bAvailableKnown = SW_TRUE;
    deviceBudget._scope           = RHIMemoryScope::Device;
    ledger.setDriverBudget( deviceBudget );

    mutex          lineMutex;
    vector<string> listLine;
    Subscription   subscription = Logger::registerGlobalListener( SW_DELEGATE_LAMBDA( LogWrittenDelegate, [&lineMutex, &listLine]( const LogEntry& entry )
      {
        if ( entry._message.find( "[Profile]" ) == string::npos )
            return;
        ScopedLock<mutex> lock{ lineMutex };
        listLine.push_back( entry._message );
    } ) );
    ledger.report( "FakeBackend" );
    subscription.reset();

    string joined;
    for ( const string& line : listLine )
    {
        joined += line + "\n";
    }
    const string_view text{ joined.c_str(), joined.size() };
    SW_EXPECT_TRUE_MSG( text.find( "GPU memory by kind (live, FakeBackend · allocation size)  3.0 MB in 1 resources  + 1 of unknown size" ) != string_view::npos, joined.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "TransientPool  3.0 MB  100.0%  1 resources" ) != string_view::npos, joined.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "Descriptor  0.0 MB  0.0%  0 resources  + 1 of unknown size" ) != string_view::npos, joined.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "Buffer" ) == string_view::npos, joined.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "usage unknown  budget unknown  available 8.0 MB" ) != string_view::npos, joined.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "unknown - no driver usage for this process" ) != string_view::npos, joined.c_str() );
}
#endif
