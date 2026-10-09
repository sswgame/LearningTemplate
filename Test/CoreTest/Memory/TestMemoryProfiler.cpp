#include "pch.h"

#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Memory/Memory.h"

#include "TestFramework/TestFramework.h"

#include <thread>

using namespace sw;

// ------------------------------------------------------------------------------
// 1) MemoryProfilerTest — 할당·해제 바이트
// ------------------------------------------------------------------------------
/**
 * @brief [MemoryProfilerTest] 할당·해제 바이트 추적
 */
SW_TEST_CASE( MemoryProfilerTest, BasicTracking )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );
    profiler.setDetailedTrackingEnabled( true );

    const auto& beforeStats  = profiler.getStats( MemoryTag::Game );
    uint64      initialBytes = beforeStats._currentAllocatedBytes.load();

    void*  dummyPtr = reinterpret_cast<void*>( 0x12345678 );
    uint64 hash     = profiler.recordAllocation( dummyPtr, 1024, MemoryTag::Game );

    const auto& afterStats = profiler.getStats( MemoryTag::Game );
    uint64      afterBytes = afterStats._currentAllocatedBytes.load();

    SW_EXPECT_TRUE( afterBytes > initialBytes );

    auto topStacks = profiler.getTopCallStacks();
    SW_EXPECT_TRUE( !topStacks.empty() );

    profiler.recordFree( dummyPtr, 1024, MemoryTag::Game, hash );

    const auto& finalStats = profiler.getStats( MemoryTag::Game );
    SW_EXPECT_EQUAL( initialBytes, finalStats._currentAllocatedBytes.load() );

    profiler.shutdown();
}

/**
 * @brief [MemoryProfilerTest] 세지 않은 해제가 카운터를 0 아래로 접지 않는지 검증
 * @details 두 카운터(`_currentAllocatedBytes` · `_currentAllocationCount`)는 `uint64` 다.
 *          그냥 `fetch_sub` 하면 0 아래가 **1.8e19** 로 접힌다. 할당은 세지 않았는데 해제만
 *          세는 경우가 실제로 있다 — 에디터의 프로파일러 패널에 **추적 켜기 체크박스**가
 *          있어서, 켜기 전에 잡힌 블록들이 켠 뒤에 풀리면 정확히 그 일이 난다.
 */
SW_TEST_CASE( MemoryProfilerTest, FreeWithoutMatchingAllocationDoesNotWrap )
{
    MemoryProfiler profiler;
    profiler.initialize();

    // 추적을 **끈 채** 할당한다 — 카운터는 올라가지 않는다.
    profiler.setTrackingEnabled( false );
    void* pDummy = reinterpret_cast<void*>( 0x1234'5678 );
    profiler.recordAllocation( pDummy, 4096, MemoryTag::Game );

    const uint64 beforeBytes = profiler.getStats( MemoryTag::Game )._currentAllocatedBytes.load();
    const uint64 beforeCount = profiler.getStats( MemoryTag::Game )._currentAllocationCount.load();

    // 켜고 해제한다 — 세지 않은 것을 빼게 된다.
    profiler.setTrackingEnabled( true );
    profiler.recordFree( pDummy, 4096, MemoryTag::Game, 0 );

    const uint64 afterBytes = profiler.getStats( MemoryTag::Game )._currentAllocatedBytes.load();
    const uint64 afterCount = profiler.getStats( MemoryTag::Game )._currentAllocationCount.load();

    // 0 아래로 접히면 여기가 1.8e19 가 된다.
    SW_EXPECT_TRUE( afterBytes <= beforeBytes );
    SW_EXPECT_TRUE( afterCount <= beforeCount );

    profiler.shutdown();
}

/**
 * @brief [MemoryProfilerTest] 할당 횟수 누계는 해제해도 줄지 않는다 — churn 을 세는 근거
 * @details 살아 있는 양은 잡았다 놓은 것을 못 본다. 같은 자리에서 세 번 잡았다 놓으면 누계는 3, 살아 있는 것은 0 이고,
 *          횟수 순 표에는 그 자리가 남지만 살아 있는 바이트 순 표에는 없다.
 */
SW_TEST_CASE( MemoryProfilerTest, TotalAllocationCountSurvivesFrees )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );
    profiler.setDetailedTrackingEnabled( true );

    const uint64 totalBefore = profiler.getTotalAllocationCount();
    void*        pDummy      = reinterpret_cast<void*>( 0x2468'ACE0 );
    for ( uint32 round = 0; round < 3; ++round )
    {
        const uint64 hash = profiler.recordAllocation( pDummy, 256, MemoryTag::EngineMisc );
        profiler.recordFree( pDummy, 256, MemoryTag::EngineMisc, hash );
    }

    SW_EXPECT_EQUAL( uint64( 3 ), profiler.getTotalAllocationCount() - totalBefore );
    SW_EXPECT_EQUAL( uint64( 0 ), profiler.getStats( MemoryTag::EngineMisc )._currentAllocationCount.load() );

    const vector<CallStackAllocInfo> listByChurn = profiler.getTopCallStacks( TopCallStackOrder::TotalCount );
    SW_ASSERT_FALSE( listByChurn.empty() );
    SW_EXPECT_EQUAL( uint64( 3 ), listByChurn.front()._totalCount );
    SW_EXPECT_EQUAL( uint64( 0 ), listByChurn.front()._currentCount );
    SW_EXPECT_TRUE( profiler.getTopCallStacks( TopCallStackOrder::LiveBytes ).empty() );

    profiler.shutdown();
}

/**
 * @brief [MemoryProfilerTest] `getTopCallStacks` 를 되풀이해 불러도 현재 사용량이 줄지 않는다.
 * @details 결과 버퍼를 추적 가드 안에서 잡으면 할당은 세지 않고 호출한 쪽이 풀 때만 세어 현재 사용량이 줄기만 한다. 프로파일러
 *          패널이 매 프레임 부르므로 태그 카운터가 0 에 붙는다.
 */
SW_TEST_CASE( MemoryProfilerTest, TopCallStackQueryDoesNotDriftLiveCounters )
{
    MemoryProfiler* pProfiler = MemoryProfiler::getActive();
    if ( pProfiler == nullptr )
        SW_TEST_SKIP( "no active memory profiler in this host" );

    const bool bWasTracking = pProfiler->isTrackingEnabled();
    const bool bWasDetailed = pProfiler->isDetailedTrackingEnabled();
    pProfiler->setTrackingEnabled( true );
    pProfiler->setDetailedTrackingEnabled( true );

    // 표에 항목이 있어야 결과 버퍼가 생긴다.
    sw::vector<sw::vector<uint8>> listKeepAlive;
    for ( uint32 index = 0; index < 64; ++index )
    {
        listKeepAlive.emplace_back( 256 + index );
    }

    const MemoryTag tag       = Memory::getCurrentMemoryTag();
    const uint64    before    = pProfiler->getStats( tag )._currentAllocatedBytes.load();
    size_t          lastBytes = 0;
    for ( uint32 repeat = 0; repeat < 64; ++repeat )
    {
        const sw::vector<CallStackAllocInfo> listTop = pProfiler->getTopCallStacks();
        lastBytes                                    = listTop.capacity() * sizeof( CallStackAllocInfo );
    }
    const uint64 after = pProfiler->getStats( tag )._currentAllocatedBytes.load();

    pProfiler->setDetailedTrackingEnabled( bWasDetailed );
    pProfiler->setTrackingEnabled( bWasTracking );

    // 다른 스레드의 할당이 섞여 들 수 있어 정확히 같을 필요는 없다. 해제만 세면 호출마다 버퍼 한 벌씩 줄어 64 벌 준다.
    const uint64 drift = ( after < before ) ? ( before - after ) : 0;
    SW_EXPECT_TRUE_MSG( drift < static_cast<uint64>( lastBytes ) * 8, "현재 사용량이 조회할 때마다 줄었습니다" );
}

// ------------------------------------------------------------------------------
// 2) MemoryProfilerTest — 태그 스코프
// ------------------------------------------------------------------------------
/**
 * @brief [MemoryProfilerTest] 태그 스코프 안의 할당은 그 태그로 세고, 스코프를 나가면 태그가 되돌아간다
 * @details 해제는 할당 헤더에 적힌 태그로 뺀다 — 스코프 밖(다른 태그 아래)에서 풀어도 같은 줄에서 빠진다.
 *          한 블록에 스코프 둘을 나란히 두는 것도 컴파일돼야 한다(변수 이름에 줄 번호가 붙는다).
 */
SW_TEST_CASE( MemoryProfilerTest, ScopedTagAttributesAllocationsAndRestores )
{
    if constexpr ( kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    MemoryProfiler* pProfiler = MemoryProfiler::getActive();
    if ( pProfiler == nullptr )
        SW_TEST_SKIP( "no active memory profiler in this host" );
    const bool bWasTracking = pProfiler->isTrackingEnabled();
    pProfiler->setTrackingEnabled( true );

    constexpr size_t kBlockBytes = 64 * 1024;
    const MemoryTag  outerTag    = Memory::getCurrentMemoryTag();
    const uint64     textureBase = pProfiler->getStats( MemoryTag::Texture )._currentAllocatedBytes.load();
    const uint64     meshBase    = pProfiler->getStats( MemoryTag::Mesh )._currentAllocatedBytes.load();

    void* pTextureBlock{ nullptr };
    void* pMeshBlock{ nullptr };
    {
        SW_MEMORY_SCOPE( Texture );
        SW_EXPECT_TRUE( Memory::getCurrentMemoryTag() == MemoryTag::Texture );
        pTextureBlock = Memory::allocate( kBlockBytes );
        {
            SW_MEMORY_SCOPE( Mesh );
            SW_MEMORY_SCOPE( Mesh );
            SW_EXPECT_TRUE( Memory::getCurrentMemoryTag() == MemoryTag::Mesh );
            pMeshBlock = Memory::allocate( kBlockBytes );
        }
        SW_EXPECT_TRUE( Memory::getCurrentMemoryTag() == MemoryTag::Texture );
    }
    SW_EXPECT_TRUE( Memory::getCurrentMemoryTag() == outerTag );

    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::Texture )._currentAllocatedBytes.load() >= textureBase + kBlockBytes );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::Mesh )._currentAllocatedBytes.load() >= meshBase + kBlockBytes );

    // 스코프 밖에서 풀어도 할당한 태그에서 빠진다.
    Memory::free( pTextureBlock );
    Memory::free( pMeshBlock );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::Texture )._currentAllocatedBytes.load() < textureBase + kBlockBytes );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::Mesh )._currentAllocatedBytes.load() < meshBase + kBlockBytes );

    pProfiler->setTrackingEnabled( bWasTracking );
}

/**
 * @brief [MemoryProfilerTest] 태그마다 표시 이름이 있다(이름 표가 `MemoryTag` 와 같은 순서 · 같은 줄 수)
 */
SW_TEST_CASE( MemoryProfilerTest, EveryTagHasAName )
{
    SW_EXPECT_STREQ( "Unknown", MemoryProfiler::getMemoryTagName( MemoryTag::Unknown ) );
    SW_EXPECT_STREQ( "Texture", MemoryProfiler::getMemoryTagName( MemoryTag::Texture ) );
    SW_EXPECT_STREQ( "RenderCpu", MemoryProfiler::getMemoryTagName( MemoryTag::RenderCpu ) );
    SW_EXPECT_STREQ( "Game", MemoryProfiler::getMemoryTagName( MemoryTag::Game ) );
    SW_EXPECT_STREQ( "Invalid", MemoryProfiler::getMemoryTagName( MemoryTag::MaxTags ) );
}

/**
 * @brief [MemoryProfilerTest] 태그 순서는 살아 있는 바이트가 큰 것부터이고, 합은 모든 태그의 합이다
 * @details 보고(`-gv_profileFrames`)와 에디터 프로파일러 패널이 같은 순서로 줄을 낸다.
 */
SW_TEST_CASE( MemoryProfilerTest, TagOrderFollowsLiveBytes )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );

    void* pDummy = reinterpret_cast<void*>( 0x1357'9BDF );
    profiler.recordAllocation( pDummy, 1024, MemoryTag::Texture );
    profiler.recordAllocation( pDummy, 4096, MemoryTag::Mesh );
    profiler.recordAllocation( pDummy, 256, MemoryTag::Audio );

    const array<MemoryTag, kMemoryTagCount> arrOrder = profiler.makeTagOrderByLiveBytes();
    SW_EXPECT_TRUE( arrOrder[0] == MemoryTag::Mesh );
    SW_EXPECT_TRUE( arrOrder[1] == MemoryTag::Texture );
    SW_EXPECT_TRUE( arrOrder[2] == MemoryTag::Audio );
    // 0 인 태그는 enum 순서 그대로 뒤에 온다.
    SW_EXPECT_TRUE( arrOrder[3] == MemoryTag::Unknown );
    SW_EXPECT_EQUAL( uint64( 1024 + 4096 + 256 ), profiler.getLiveAllocatedBytes() );

    profiler.recordFree( pDummy, 1024, MemoryTag::Texture );
    profiler.recordFree( pDummy, 4096, MemoryTag::Mesh );
    profiler.recordFree( pDummy, 256, MemoryTag::Audio );
    SW_EXPECT_EQUAL( uint64( 0 ), profiler.getLiveAllocatedBytes() );
    profiler.shutdown();
}

/**
 * @brief [MemoryProfilerTest] 최고치는 해제해도 내려가지 않고, resetPeaks 가 지금 값으로 되돌린다
 */
SW_TEST_CASE( MemoryProfilerTest, PeakKeepsTheHighWaterMark )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );

    void* const pFirst  = reinterpret_cast<void*>( 0x1000 );
    void* const pSecond = reinterpret_cast<void*>( 0x2000 );
    (void)profiler.recordAllocation( pFirst, 3000, MemoryTag::Audio );
    (void)profiler.recordAllocation( pSecond, 5000, MemoryTag::Audio );
    profiler.recordFree( pSecond, 5000, MemoryTag::Audio );

    const MemoryProfileStats& stats = profiler.getStats( MemoryTag::Audio );
    SW_EXPECT_EQUAL( uint64{ 3000 }, stats._currentAllocatedBytes.load() );
    SW_EXPECT_EQUAL( uint64{ 8000 }, stats._peakAllocatedBytes.load() );
    SW_EXPECT_EQUAL( uint64{ 2 }, stats._peakAllocationCount.load() );

    profiler.resetPeaks();
    SW_EXPECT_EQUAL( uint64{ 3000 }, stats._peakAllocatedBytes.load() );
    SW_EXPECT_EQUAL( uint64{ 1 }, stats._peakAllocationCount.load() );
    profiler.recordFree( pFirst, 3000, MemoryTag::Audio );
    profiler.shutdown();
}

/**
 * @brief [MemoryProfilerTest] 예산을 넘으면 그때 한 번 알리고, 넘어 있는 동안은 다시 알리지 않으며, 90 % 아래로 내려가면 다시 건다
 */
SW_TEST_CASE( MemoryProfilerTest, BudgetWarnsOnceAndRearmsBelowNinetyPercent )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );
    profiler.setBudget( MemoryTag::Texture, 10000 );
    SW_EXPECT_EQUAL( uint64{ 10000 }, profiler.getBudget( MemoryTag::Texture ) );

    void* const pBlock = reinterpret_cast<void*>( 0x3000 );
    (void)profiler.recordAllocation( pBlock, 9000, MemoryTag::Texture );
    SW_EXPECT_EQUAL( 0u, profiler.reportExceededBudgets() ); // 아직 안 넘었다

    void* const pExtra = reinterpret_cast<void*>( 0x4000 );
    (void)profiler.recordAllocation( pExtra, 2000, MemoryTag::Texture );
    vector<MemoryTag> listExceeded;
    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_EQUAL( 1u, profiler.reportExceededBudgets( &listExceeded ) );
    }
    SW_ASSERT_EQUAL( size_t{ 1 }, listExceeded.size() );
    SW_EXPECT_TRUE( listExceeded[0] == MemoryTag::Texture );
    SW_EXPECT_EQUAL( 0u, profiler.reportExceededBudgets() ); // 넘어 있는 동안은 다시 알리지 않는다

    // 9500 은 예산의 95 % — 아직 다시 걸지 않는다. 8500(85 %)이면 다시 건다.
    profiler.recordFree( pExtra, 2000, MemoryTag::Texture );
    (void)profiler.recordAllocation( pExtra, 500, MemoryTag::Texture );
    SW_EXPECT_EQUAL( 0u, profiler.reportExceededBudgets() );
    profiler.recordFree( pExtra, 500, MemoryTag::Texture );
    profiler.recordFree( pBlock, 9000, MemoryTag::Texture );
    (void)profiler.recordAllocation( pBlock, 8500, MemoryTag::Texture );
    SW_EXPECT_EQUAL( 0u, profiler.reportExceededBudgets() );
    (void)profiler.recordAllocation( pExtra, 4000, MemoryTag::Texture );
    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_EQUAL( 1u, profiler.reportExceededBudgets() ); // 다시 넘었다
    }

    // 예산이 없는 태그는 보지 않는다.
    profiler.clearBudgets();
    SW_EXPECT_EQUAL( 0u, profiler.reportExceededBudgets() );
    profiler.recordFree( pExtra, 4000, MemoryTag::Texture );
    profiler.recordFree( pBlock, 8500, MemoryTag::Texture );
    profiler.shutdown();
}

/**
 * @brief [MemoryProfilerTest] 추적을 끄면 할당 · 해제를 세지 않는다(콜 스택 해시도 0) — 꺼진 길은 분기 하나다
 */
SW_TEST_CASE( MemoryProfilerTest, DisabledTrackingRecordsNothing )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( false );
    profiler.setDetailedTrackingEnabled( true );

    void* const  pBlock = reinterpret_cast<void*>( 0x5000 );
    const uint64 hash   = profiler.recordAllocation( pBlock, 4096, MemoryTag::Physics );
    SW_EXPECT_EQUAL( uint64{ 0 }, hash );
    const MemoryProfileStats& stats = profiler.getStats( MemoryTag::Physics );
    SW_EXPECT_EQUAL( uint64{ 0 }, stats._currentAllocatedBytes.load() );
    SW_EXPECT_EQUAL( uint64{ 0 }, stats._totalAllocationCount.load() );
    SW_EXPECT_EQUAL( uint64{ 0 }, stats._peakAllocatedBytes.load() );
    SW_EXPECT_TRUE( profiler.getTopCallStacks().empty() );
    profiler.recordFree( pBlock, 4096, MemoryTag::Physics );
    SW_EXPECT_EQUAL( uint64{ 0 }, stats._totalFreedBytes.load() );
    profiler.shutdown();
}

/**
 * @brief [MemoryProfilerTest] 기준선 뒤로 늘어난 태그만 큰 순서로 나온다(종료 누수 보고의 재료)
 */
SW_TEST_CASE( MemoryProfilerTest, TagGrowthSinceBaselineNamesTheGrownTags )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );
    SW_EXPECT_TRUE( profiler.collectTagGrowthSinceBaseline().empty() ); // 기준선 전

    void* const pKept = reinterpret_cast<void*>( 0x6000 );
    (void)profiler.recordAllocation( pKept, 100, MemoryTag::Scene );
    profiler.captureTagBaseline();
    SW_EXPECT_TRUE( profiler.hasTagBaseline() );

    void* const pSmall = reinterpret_cast<void*>( 0x7000 );
    void* const pLarge = reinterpret_cast<void*>( 0x8000 );
    (void)profiler.recordAllocation( pSmall, 64, MemoryTag::UI );
    (void)profiler.recordAllocation( pLarge, 640, MemoryTag::Script );
    profiler.recordFree( pKept, 100, MemoryTag::Scene ); // 줄어든 태그는 나오지 않는다

    const vector<MemoryTagGrowth> listGrowth = profiler.collectTagGrowthSinceBaseline();
    SW_ASSERT_EQUAL( size_t{ 2 }, listGrowth.size() );
    SW_EXPECT_TRUE( listGrowth[0]._tag == MemoryTag::Script );
    SW_EXPECT_EQUAL( int64{ 640 }, listGrowth[0]._byteDelta );
    SW_EXPECT_TRUE( listGrowth[1]._tag == MemoryTag::UI );
    SW_EXPECT_EQUAL( int64{ 1 }, listGrowth[1]._countDelta );

    profiler.recordFree( pSmall, 64, MemoryTag::UI );
    profiler.recordFree( pLarge, 640, MemoryTag::Script );
    SW_EXPECT_TRUE( profiler.collectTagGrowthSinceBaseline().empty() );
    profiler.shutdown();
}

/**
 * @brief [MemoryProfilerTest] 데이터가 적는 태그 이름은 대소문자를 가리지 않고 찾고, 모르는 이름은 거절한다
 */
SW_TEST_CASE( MemoryProfilerTest, FindsTagsByName )
{
    MemoryTag tag{ MemoryTag::Unknown };
    SW_EXPECT_TRUE( MemoryProfiler::findMemoryTagByName( "texture", tag ) );
    SW_EXPECT_TRUE( tag == MemoryTag::Texture );
    SW_EXPECT_TRUE( MemoryProfiler::findMemoryTagByName( "UI", tag ) );
    SW_EXPECT_TRUE( tag == MemoryTag::UI );
    SW_EXPECT_TRUE( MemoryProfiler::findMemoryTagByName( "Script", tag ) );
    SW_EXPECT_TRUE( tag == MemoryTag::Script );
    SW_EXPECT_FALSE( MemoryProfiler::findMemoryTagByName( "Textures", tag ) );
    SW_EXPECT_FALSE( MemoryProfiler::findMemoryTagByName( "", tag ) );
}

/**
 * @brief [MemoryProfilerTest] 다른 스레드의 스코프 태그로 잡은 블록을 이 스레드에서 풀어도 같은 줄에서 빠지고, 명시 태그는 스코프를 이긴다
 * @details 태그는 스레드 로컬이고 해제는 헤더에 적힌 태그로 뺀다. 워커가 잡고 게임 스레드가 푸는 버퍼(로드 결과)가 그 모양이다.
 */
SW_TEST_CASE( MemoryProfilerTest, TagsFollowTheBlockAcrossThreads )
{
    if constexpr ( kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    MemoryProfiler* pProfiler = MemoryProfiler::getActive();
    if ( pProfiler == nullptr )
        SW_TEST_SKIP( "no active memory profiler in this host" );
    const bool bWasTracking = pProfiler->isTrackingEnabled();
    pProfiler->setTrackingEnabled( true );

    constexpr uint32 kBlockCount = 64;
    constexpr size_t kBlockBytes = 1024;
    const uint64     uiBase      = pProfiler->getStats( MemoryTag::UI )._currentAllocatedBytes.load();
    const uint64     physicsBase = pProfiler->getStats( MemoryTag::Physics )._currentAllocatedBytes.load();

    vector<void*> listBlock( kBlockCount, nullptr );
    void**        ppBlock = listBlock.data();
    void*         pExplicit{ nullptr };
    std::thread   worker( [ppBlock, &pExplicit]()
    {
        SW_MEMORY_SCOPE( UI );
        for ( uint32 index = 0; index < kBlockCount; ++index )
        {
            ppBlock[index] = Memory::allocate( kBlockBytes );
        }
        // 스코프가 UI 여도 명시한 태그로 센다.
        pExplicit = Memory::allocate( kBlockBytes, MemoryTag::Physics );
    } );
    worker.join();

    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::UI )._currentAllocatedBytes.load() >= uiBase + kBlockCount * kBlockBytes );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::Physics )._currentAllocatedBytes.load() >= physicsBase + kBlockBytes );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::UI )._peakAllocatedBytes.load() >= uiBase + kBlockCount * kBlockBytes );

    // 이 스레드(태그가 UI 가 아니다)에서 푼다.
    for ( void* pBlock : listBlock )
    {
        Memory::free( pBlock );
    }
    Memory::free( pExplicit );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::UI )._currentAllocatedBytes.load() < uiBase + kBlockBytes );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::Physics )._currentAllocatedBytes.load() < physicsBase + kBlockBytes );

    pProfiler->setTrackingEnabled( bWasTracking );
}
