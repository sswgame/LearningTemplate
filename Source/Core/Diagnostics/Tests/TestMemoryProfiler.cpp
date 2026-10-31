// MemoryProfiler — 바이트 · 횟수 추적, 세부 추적의 스택 표, 태그 스코프 · 태그 이름 · 순서, 최고치, 예산 경고, 꺼진 추적, 센 블록만 빼는 해제, 기록기 번호,
// 누수 창(태그 합계 · 스택 전수 · 다른 스레드 · 잘못 쓰기), 곡선 판정, 이름으로 태그 찾기, 스레드를 건너는 태그.
// 지역 프로파일러는 할당기에 걸리지 않는다(엔진의 것이 먼저 걸려 있다 — `Memory::registerAllocationTracker`) — 케이스가 넣은 기록만 센다.
#include "pch.h"

#include "Core/Concurrency/thread.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 보고서에서 태그와 태어난 회차가 맞는 줄을 찾습니다. 없으면 nullptr 입니다. */
    const LeakWindowEntry* findLeakEntry( const LeakWindowReport& report, MemoryTag tag, uint32 round )
    {
        for ( const LeakWindowEntry& entry : report._listEntry )
        {
            if ( entry._tag == tag && entry._bornRound == round )
                return &entry;
        }
        return nullptr;
    }
} // namespace

/**
 * @brief 할당·해제 바이트 추적
 */
SW_TEST_CASE( MemoryProfilerTest, BasicTracking )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );
    profiler.setDetailedTrackingEnabled( true );

    const auto& beforeStats  = profiler.getStats( MemoryTag::Game );
    uint64      initialBytes = beforeStats._currentAllocatedBytes;

    void*                  dummyPtr = reinterpret_cast<void*>( 0x12345678 );
    const AllocationRecord born     = profiler.recordAllocation( dummyPtr, 1024, MemoryTag::Game );

    const auto& afterStats = profiler.getStats( MemoryTag::Game );
    uint64      afterBytes = afterStats._currentAllocatedBytes;

    SW_EXPECT_TRUE( afterBytes > initialBytes );

    auto topStacks = profiler.getTopCallStacks();
    SW_EXPECT_TRUE( !topStacks.empty() );

    profiler.recordFree( dummyPtr, 1024, MemoryTag::Game, born );

    const auto& finalStats = profiler.getStats( MemoryTag::Game );
    SW_EXPECT_EQUAL( initialBytes, finalStats._currentAllocatedBytes );

    profiler.shutdown();
}

/**
 * @brief 할당 횟수 누계는 해제해도 줄지 않는다 — churn 을 세는 근거
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
        const AllocationRecord born = profiler.recordAllocation( pDummy, 256, MemoryTag::EngineMisc );
        profiler.recordFree( pDummy, 256, MemoryTag::EngineMisc, born );
    }

    SW_EXPECT_EQUAL( uint64( 3 ), profiler.getTotalAllocationCount() - totalBefore );
    SW_EXPECT_EQUAL( uint64( 0 ), profiler.getStats( MemoryTag::EngineMisc )._currentAllocationCount );

    const vector<CallStackAllocInfo> listByChurn = profiler.getTopCallStacks( TopCallStackOrder::TotalCount );
    SW_REQUIRE_FALSE( listByChurn.empty() );
    SW_EXPECT_EQUAL( uint64( 3 ), listByChurn.front()._totalCount );
    SW_EXPECT_EQUAL( uint64( 0 ), listByChurn.front()._currentCount );
    SW_EXPECT_TRUE( profiler.getTopCallStacks( TopCallStackOrder::LiveBytes ).empty() );

    profiler.shutdown();
}

/**
 * @brief 세부 추적을 켜고 할당한 블록은 추적을 끈 뒤에 해제해도 스택 표에서 빠진다
 * @details 해제가 지금 스위치를 보면 껐다 켠 사이의 해제가 표에서 빠져 살아 있는 양이 위로 어긋난다. 판정은 할당 때 정해진 해시다.
 *          반대로 꺼 둔 채 할당한 블록(해시 0)은 표에 오른 적이 없으므로, 켠 뒤 해제해도 표를 건드리지 않는다.
 */
SW_TEST_CASE( MemoryProfilerTest, FreeAfterDetailedTrackingTurnedOffClearsTheStackTable )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );
    profiler.setDetailedTrackingEnabled( true );

    void*                  pTracked = reinterpret_cast<void*>( 0x3579'BDF0 );
    const AllocationRecord tracked  = profiler.recordAllocation( pTracked, 512, MemoryTag::EngineMisc );
    SW_REQUIRE_TRUE( tracked._callStackHash != 0 );
    SW_REQUIRE_FALSE( profiler.getTopCallStacks( TopCallStackOrder::LiveBytes ).empty() );

    profiler.setDetailedTrackingEnabled( false );
    profiler.recordFree( pTracked, 512, MemoryTag::EngineMisc, tracked );
    SW_EXPECT_TRUE( profiler.getTopCallStacks( TopCallStackOrder::LiveBytes ).empty() );

    void*                  pUntracked = reinterpret_cast<void*>( 0x4680'CE10 );
    const AllocationRecord untracked  = profiler.recordAllocation( pUntracked, 128, MemoryTag::EngineMisc );
    SW_EXPECT_EQUAL( uint64( 0 ), untracked._callStackHash );
    profiler.setDetailedTrackingEnabled( true );
    profiler.recordFree( pUntracked, 128, MemoryTag::EngineMisc, untracked );
    SW_EXPECT_TRUE( profiler.getTopCallStacks( TopCallStackOrder::LiveBytes ).empty() );
    SW_EXPECT_EQUAL( uint64( 0 ), profiler.getStats( MemoryTag::EngineMisc )._currentAllocationCount );

    profiler.shutdown();
}

/**
 * @brief `getTopCallStacks` 를 되풀이해 불러도 현재 사용량이 줄지 않는다.
 * @details 결과 버퍼를 추적 가드 안에서 잡으면 할당은 세지 않고 호출한 쪽이 풀 때만 세어 현재 사용량이 줄기만 한다. 프로파일러
 *          패널이 매 프레임 부르므로 태그 카운터가 0 에 붙는다.
 */
SW_TEST_CASE( MemoryProfilerTest, TopCallStackQueryDoesNotDriftLiveCounters )
{
    MemoryProfiler* pProfiler = MemoryProfiler::getActive(); // 테스트를 짓는 구성(배포본 밖)은 늘 엔진 프로파일러를 세운다(DiagnosticsSubsystem)
    SW_REQUIRE_NOT_NULL( pProfiler );

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
    const uint64    before    = pProfiler->getStats( tag )._currentAllocatedBytes;
    size_t          lastBytes = 0;
    for ( uint32 repeat = 0; repeat < 64; ++repeat )
    {
        const sw::vector<CallStackAllocInfo> listTop = pProfiler->getTopCallStacks();
        lastBytes                                    = listTop.capacity() * sizeof( CallStackAllocInfo );
    }
    const uint64 after = pProfiler->getStats( tag )._currentAllocatedBytes;

    pProfiler->setDetailedTrackingEnabled( bWasDetailed );
    pProfiler->setTrackingEnabled( bWasTracking );

    // 다른 스레드의 할당이 섞여 들 수 있어 정확히 같을 필요는 없다. 해제만 세면 호출마다 버퍼 한 벌씩 줄어 64 벌 준다.
    const uint64 drift = ( after < before ) ? ( before - after ) : 0;
    SW_EXPECT_TRUE_MSG( drift < static_cast<uint64>( lastBytes ) * 8, "현재 사용량이 조회할 때마다 줄었습니다" );
}

/**
 * @brief 태그 스코프 안의 할당은 그 태그로 세고, 스코프를 나가면 태그가 되돌아간다
 * @details 해제는 할당 헤더에 적힌 태그로 뺀다 — 스코프 밖(다른 태그 아래)에서 풀어도 같은 줄에서 빠진다.
 *          한 블록에 스코프 둘을 나란히 두는 것도 컴파일돼야 한다(변수 이름에 줄 번호가 붙는다).
 */
SW_TEST_CASE( MemoryProfilerTest, ScopedTagAttributesAllocationsAndRestores )
{
    MemoryProfiler* pProfiler = MemoryProfiler::getActive(); // 테스트를 짓는 구성(배포본 밖)은 늘 엔진 프로파일러를 세운다(DiagnosticsSubsystem)
    SW_REQUIRE_NOT_NULL( pProfiler );
    const bool bWasTracking = pProfiler->isTrackingEnabled();
    pProfiler->setTrackingEnabled( true );

    constexpr size_t kBlockBytes = 64 * 1024;
    const MemoryTag  outerTag    = Memory::getCurrentMemoryTag();
    const uint64     textureBase = pProfiler->getStats( MemoryTag::Texture )._currentAllocatedBytes;
    const uint64     meshBase    = pProfiler->getStats( MemoryTag::Mesh )._currentAllocatedBytes;

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

    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::Texture )._currentAllocatedBytes >= textureBase + kBlockBytes );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::Mesh )._currentAllocatedBytes >= meshBase + kBlockBytes );

    // 스코프 밖에서 풀어도 할당한 태그에서 빠진다.
    Memory::free( pTextureBlock );
    Memory::free( pMeshBlock );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::Texture )._currentAllocatedBytes < textureBase + kBlockBytes );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::Mesh )._currentAllocatedBytes < meshBase + kBlockBytes );

    pProfiler->setTrackingEnabled( bWasTracking );
}

/**
 * @brief 태그마다 표시 이름이 있다(이름 표가 `MemoryTag` 와 같은 순서 · 같은 줄 수)
 */
SW_TEST_CASE( MemoryProfilerTest, EveryTagHasAName )
{
    SW_EXPECT_EQUAL( string( "Unknown" ), string( MemoryProfiler::getMemoryTagName( MemoryTag::Unknown ) ) );
    SW_EXPECT_EQUAL( string( "Texture" ), string( MemoryProfiler::getMemoryTagName( MemoryTag::Texture ) ) );
    SW_EXPECT_EQUAL( string( "RenderCPU" ), string( MemoryProfiler::getMemoryTagName( MemoryTag::RenderCPU ) ) );
    SW_EXPECT_EQUAL( string( "Game" ), string( MemoryProfiler::getMemoryTagName( MemoryTag::Game ) ) );
    SW_EXPECT_EQUAL( string( "Invalid" ), string( MemoryProfiler::getMemoryTagName( MemoryTag::MaxTags ) ) );
}

/**
 * @brief 태그 순서는 살아 있는 바이트가 큰 것부터이고, 합은 모든 태그의 합이다
 * @details 보고(`-gv_profileFrames`)와 에디터 프로파일러 패널이 같은 순서로 줄을 낸다.
 */
SW_TEST_CASE( MemoryProfilerTest, TagOrderFollowsLiveBytes )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );

    void* pDummy = reinterpret_cast<void*>( 0x1357'9BDF );
    (void)profiler.recordAllocation( pDummy, 1024, MemoryTag::Texture );
    (void)profiler.recordAllocation( pDummy, 4096, MemoryTag::Mesh );
    (void)profiler.recordAllocation( pDummy, 256, MemoryTag::Audio );

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
 * @brief 최고치는 읽을 때 본 값의 최고치다 — 해제해도 내려가지 않고, resetPeaks 가 지금 값으로 되돌리며, 두 번 읽는 사이의 순간 최고치는 잡지 않는다
 * @details 할당 경로는 스레드 칸에만 더하고 최고치는 합을 읽는 쪽(`getStats` · 프레임마다의 `reportExceededBudgets`)이 올린다(언리얼 LLM 과 같다).
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
    (void)profiler.getStats( MemoryTag::Audio ); // 여기서 읽은 8000 이 최고치가 된다
    profiler.recordFree( pSecond, 5000, MemoryTag::Audio );

    const MemoryProfileStats afterFree = profiler.getStats( MemoryTag::Audio );
    SW_EXPECT_EQUAL( uint64{ 3000 }, afterFree._currentAllocatedBytes );
    SW_EXPECT_EQUAL( uint64{ 8000 }, afterFree._peakAllocatedBytes );
    SW_EXPECT_EQUAL( uint64{ 2 }, afterFree._peakAllocationCount );

    profiler.resetPeaks();
    const MemoryProfileStats afterReset = profiler.getStats( MemoryTag::Audio );
    SW_EXPECT_EQUAL( uint64{ 3000 }, afterReset._peakAllocatedBytes );
    SW_EXPECT_EQUAL( uint64{ 1 }, afterReset._peakAllocationCount );

    // 읽지 않은 사이에 잡았다 놓은 순간 최고치(3000 + 7000)는 잡히지 않는다.
    (void)profiler.recordAllocation( pSecond, 7000, MemoryTag::Audio );
    profiler.recordFree( pSecond, 7000, MemoryTag::Audio );
    SW_EXPECT_EQUAL( uint64{ 3000 }, profiler.getStats( MemoryTag::Audio )._peakAllocatedBytes );

    profiler.recordFree( pFirst, 3000, MemoryTag::Audio );
    profiler.shutdown();
}

/**
 * @brief 예산을 넘으면 그때 한 번 알리고, 넘어 있는 동안은 다시 알리지 않으며, 90 % 아래로 내려가면 다시 건다
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
    SW_EXPECT_LOG( "MemoryProfiler.OverBudget" ); // 넘은 태그마다 경고 한 줄
    SW_EXPECT_EQUAL( 1u, profiler.reportExceededBudgets( &listExceeded ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listExceeded.size() );
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
    SW_EXPECT_EQUAL( 1u, profiler.reportExceededBudgets() ); // 다시 넘었다

    // 예산이 없는 태그는 보지 않는다.
    profiler.clearBudgets();
    SW_EXPECT_EQUAL( 0u, profiler.reportExceededBudgets() );
    profiler.recordFree( pExtra, 4000, MemoryTag::Texture );
    profiler.recordFree( pBlock, 8500, MemoryTag::Texture );
    profiler.shutdown();
}

/**
 * @brief 추적을 끄면 할당을 세지 않고 "세지 않았다" 고 답한다(콜 스택 해시도 0) — 꺼진 길은 분기 하나다
 * @details 세지 않은 블록의 해제는 할당기가 넘기지 않는다(`AllocationRecord::_bCounted`). 그 길은 아래 실제 할당기 케이스가 본다.
 */
SW_TEST_CASE( MemoryProfilerTest, DisabledTrackingRecordsNothing )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( false );
    profiler.setDetailedTrackingEnabled( true );

    void* const            pBlock = reinterpret_cast<void*>( 0x5000 );
    const AllocationRecord record = profiler.recordAllocation( pBlock, 4096, MemoryTag::Physics );
    SW_EXPECT_FALSE( record._bCounted );
    SW_EXPECT_EQUAL( uint64{ 0 }, record._callStackHash );
    const MemoryProfileStats& stats = profiler.getStats( MemoryTag::Physics );
    SW_EXPECT_EQUAL( uint64{ 0 }, stats._currentAllocatedBytes );
    SW_EXPECT_EQUAL( uint64{ 0 }, stats._totalAllocationCount );
    SW_EXPECT_EQUAL( uint64{ 0 }, stats._peakAllocatedBytes );
    SW_EXPECT_TRUE( profiler.getTopCallStacks().empty() );
    profiler.shutdown();
}

/**
 * @brief 해제는 센 블록만 뺀다 — 추적을 켜기 전에 잡은 블록을 켠 뒤에 풀어도, 켠 동안 잡은 블록을 끈 뒤에 풀어도 태그 통계가 어긋나지 않는다
 * @details 블록 헤더에 센 기록기의 번호가 남고 할당기는 그 블록의 해제만 기록기에 넘긴다. 에디터 프로파일러 패널의 추적 켜기 체크박스가 이 두 순서를 만든다.
 *          다른 스레드가 이 태그(`Navigation`)로 잡지 않는다는 가정으로 정확히 비교한다.
 */
SW_TEST_CASE( MemoryProfilerTest, FreeCountsOnlyBlocksTheTrackerCounted )
{
    // 할당기가 기록기에 넘기는 것은 블록 헤더가 있는 구성뿐이다(배포본은 헤더도 태그 스코프도 없다 — `Memory::getAllocationHeaderSize` 가 0).
    MemoryProfiler* pProfiler = MemoryProfiler::getActive(); // 테스트를 짓는 구성(배포본 밖)은 늘 엔진 프로파일러를 세운다(DiagnosticsSubsystem)
    SW_REQUIRE_NOT_NULL( pProfiler );
    const bool       bWasTracking = pProfiler->isTrackingEnabled();
    constexpr size_t kBlockBytes  = 4096;
    constexpr auto   kTag         = MemoryTag::Navigation;

    // 1) 끈 채 잡고 켠 뒤에 푼다 — 할당도 해제도 세지 않는다.
    pProfiler->setTrackingEnabled( false );
    void* pUncounted = Memory::allocate( kBlockBytes, kTag );
    SW_REQUIRE_TRUE( pUncounted != nullptr );
    pProfiler->setTrackingEnabled( true );
    const MemoryProfileStats beforeUncounted = pProfiler->getStats( kTag );
    Memory::free( pUncounted );
    const MemoryProfileStats afterUncounted = pProfiler->getStats( kTag );
    SW_EXPECT_EQUAL( beforeUncounted._totalFreedBytes, afterUncounted._totalFreedBytes );
    SW_EXPECT_EQUAL( beforeUncounted._currentAllocatedBytes, afterUncounted._currentAllocatedBytes );
    SW_EXPECT_EQUAL( beforeUncounted._currentAllocationCount, afterUncounted._currentAllocationCount );

    // 2) 켠 채 잡고 끈 뒤에 푼다 — 센 블록이라 끈 동안에도 뺀다.
    void* pCounted = Memory::allocate( kBlockBytes, kTag );
    SW_REQUIRE_TRUE( pCounted != nullptr );
    const MemoryProfileStats afterAllocate = pProfiler->getStats( kTag );
    SW_EXPECT_EQUAL( afterUncounted._currentAllocatedBytes + kBlockBytes, afterAllocate._currentAllocatedBytes );
    pProfiler->setTrackingEnabled( false );
    Memory::free( pCounted );
    pProfiler->setTrackingEnabled( true );
    const MemoryProfileStats afterFree = pProfiler->getStats( kTag );
    SW_EXPECT_EQUAL( afterAllocate._totalFreedBytes + kBlockBytes, afterFree._totalFreedBytes );
    SW_EXPECT_EQUAL( afterUncounted._currentAllocatedBytes, afterFree._currentAllocatedBytes );
    SW_EXPECT_EQUAL( afterUncounted._currentAllocationCount, afterFree._currentAllocationCount );

    pProfiler->setTrackingEnabled( bWasTracking );
}

/**
 * @brief 기록기 번호는 인스턴스마다 다르고 0 이 아니다
 * @details 블록 헤더에는 기록기 주소가 아니라 이 번호가 남는다 — 같은 스택 주소에 새로 만든 프로파일러도 번호가 달라, 다른 기록기가 센 블록의 해제를 받지 않는다.
 */
SW_TEST_CASE( MemoryProfilerTest, TrackerIDsAreUniquePerInstance )
{
    uint64 firstID{ 0 };
    {
        MemoryProfiler first;
        firstID = first.getTrackerID();
    }
    MemoryProfiler second;
    SW_EXPECT_TRUE( firstID != 0 );
    SW_EXPECT_TRUE( second.getTrackerID() != 0 );
    SW_EXPECT_TRUE( second.getTrackerID() != firstID );
}

/**
 * @brief 태그 합계 창 — 창 안에서 태어나 닫힐 때 살아 있는 블록만 큰 순서로 나오고, 창 밖 오래된 블록의 해제는 새 누수를 상쇄하지 않는다
 * @details 예전 기준선 방식(살아 있는 양의 차이)은 `Scene` 에서 오래된 100 바이트를 풀고 새 50 바이트를 흘리면 차이가 음수라 누수를 가렸다.
 */
SW_TEST_CASE( MemoryProfilerTest, LeakWindowTagTotalsIgnoreFreesOfOlderBlocks )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );

    void* const            pOld     = reinterpret_cast<void*>( 0x6000 );
    const AllocationRecord oldBlock = profiler.recordAllocation( pOld, 100, MemoryTag::Scene ); // 창 밖에서 태어난 블록
    SW_EXPECT_EQUAL( 0u, oldBlock._windowStamp );

    profiler.beginLeakWindow( LeakWindowDetail::TagTotals );
    SW_EXPECT_TRUE( profiler.isLeakWindowOpen() );

    void* const            pSmall     = reinterpret_cast<void*>( 0x7000 );
    void* const            pLarge     = reinterpret_cast<void*>( 0x8000 );
    void* const            pNewScene  = reinterpret_cast<void*>( 0x9000 );
    void* const            pTemporary = reinterpret_cast<void*>( 0xA000 );
    const AllocationRecord smallBlock = profiler.recordAllocation( pSmall, 64, MemoryTag::UI );
    const AllocationRecord largeBlock = profiler.recordAllocation( pLarge, 640, MemoryTag::Script );
    const AllocationRecord sceneBlock = profiler.recordAllocation( pNewScene, 50, MemoryTag::Scene );
    const AllocationRecord tempBlock  = profiler.recordAllocation( pTemporary, 32, MemoryTag::UI );
    SW_EXPECT_TRUE( smallBlock._windowStamp != 0 );
    profiler.recordFree( pTemporary, 32, MemoryTag::UI, tempBlock ); // 창 안에서 태어나 창 안에서 풀린 블록은 남지 않는다
    profiler.recordFree( pOld, 100, MemoryTag::Scene, oldBlock );    // 창 밖에서 태어난 블록의 해제는 창의 값을 줄이지 않는다

    const LeakWindowReport report = profiler.endLeakWindow();
    SW_EXPECT_FALSE( profiler.isLeakWindowOpen() );
    SW_REQUIRE_TRUE( report._bValid );
    SW_EXPECT_TRUE( report._detail == LeakWindowDetail::TagTotals );
    SW_EXPECT_EQUAL( 1u, report._roundCount );

    const LeakWindowEntry* pScript = findLeakEntry( report, MemoryTag::Script, 1 );
    const LeakWindowEntry* pUI     = findLeakEntry( report, MemoryTag::UI, 1 );
    const LeakWindowEntry* pScene  = findLeakEntry( report, MemoryTag::Scene, 1 );
    SW_REQUIRE_TRUE( pScript != nullptr && pUI != nullptr && pScene != nullptr );
    SW_EXPECT_EQUAL( uint64{ 640 }, pScript->_bytes );
    SW_EXPECT_EQUAL( uint64{ 1 }, pScript->_blockCount );
    SW_EXPECT_EQUAL( uint64{ 64 }, pUI->_bytes );
    SW_EXPECT_EQUAL( uint64{ 1 }, pUI->_blockCount );
    SW_EXPECT_EQUAL( uint64{ 50 }, pScene->_bytes ); // 오래된 100 바이트를 풀어도 새 50 바이트가 가려지지 않는다
    SW_EXPECT_TRUE( pScript->_firstFrame.empty() );  // 태그 합계 창은 스택을 잡지 않는다
    SW_EXPECT_TRUE( report._listEntry.front()._bytes >= report._listEntry.back()._bytes );

    // 닫은 뒤의 해제는 창과 무관하다 — 센 양만 되돌아온다.
    profiler.recordFree( pSmall, 64, MemoryTag::UI, smallBlock );
    profiler.recordFree( pLarge, 640, MemoryTag::Script, largeBlock );
    profiler.recordFree( pNewScene, 50, MemoryTag::Scene, sceneBlock );
    SW_EXPECT_EQUAL( uint64{ 0 }, profiler.getStats( MemoryTag::Script )._currentAllocatedBytes );

    // 다음 창은 빈 합계에서 시작한다.
    profiler.beginLeakWindow( LeakWindowDetail::TagTotals );
    const LeakWindowReport empty = profiler.endLeakWindow();
    SW_EXPECT_TRUE( findLeakEntry( empty, MemoryTag::Script, 1 ) == nullptr );
    profiler.shutdown();
}

/**
 * @brief 스택 전수 창 — 남은 블록을 태그 · 스택 · 태어난 회차로 묶고 첫 프레임을 적으며, 회차 안에서 풀린 블록은 빠진다
 */
SW_TEST_CASE( MemoryProfilerTest, LeakWindowFullStacksGroupByStackAndRound )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );

    void* const            pOld     = reinterpret_cast<void*>( 0xB000 );
    const AllocationRecord oldBlock = profiler.recordAllocation( pOld, 100, MemoryTag::Scene );

    profiler.beginLeakWindow( LeakWindowDetail::FullStacks );
    void* const            pFirstRound = reinterpret_cast<void*>( 0xC000 );
    const AllocationRecord firstBlock  = profiler.recordAllocation( pFirstRound, 128, MemoryTag::Scene );
    profiler.beginLeakRound();
    void* const            pSecondRound = reinterpret_cast<void*>( 0xD000 );
    void* const            pTemporary   = reinterpret_cast<void*>( 0xE000 );
    const AllocationRecord secondBlock  = profiler.recordAllocation( pSecondRound, 256, MemoryTag::Scene );
    const AllocationRecord tempBlock    = profiler.recordAllocation( pTemporary, 64, MemoryTag::Scene );
    SW_EXPECT_TRUE( secondBlock._windowStamp != firstBlock._windowStamp ); // 회차마다 스탬프가 다르다
    profiler.recordFree( pTemporary, 64, MemoryTag::Scene, tempBlock );
    profiler.recordFree( pOld, 100, MemoryTag::Scene, oldBlock ); // 창 밖 블록의 해제는 상쇄하지 않는다

    const LeakWindowReport report = profiler.endLeakWindow();
    SW_REQUIRE_TRUE( report._bValid );
    SW_EXPECT_TRUE( report._detail == LeakWindowDetail::FullStacks );
    SW_EXPECT_EQUAL( 2u, report._roundCount );

    const LeakWindowEntry* pRoundOne = findLeakEntry( report, MemoryTag::Scene, 1 );
    const LeakWindowEntry* pRoundTwo = findLeakEntry( report, MemoryTag::Scene, 2 );
    SW_REQUIRE_TRUE( pRoundOne != nullptr && pRoundTwo != nullptr );
    SW_EXPECT_EQUAL( uint64{ 128 }, pRoundOne->_bytes );
    SW_EXPECT_EQUAL( uint64{ 1 }, pRoundOne->_blockCount );
    SW_EXPECT_EQUAL( uint64{ 256 }, pRoundTwo->_bytes ); // 같은 회차에서 풀린 64 바이트는 빠졌다
    SW_EXPECT_EQUAL( uint64{ 1 }, pRoundTwo->_blockCount );
    SW_EXPECT_TRUE( pRoundTwo->_stackHash != 0 );
    SW_EXPECT_FALSE( pRoundTwo->_firstFrame.empty() ); // 호출한 쪽(이 테스트)이 첫 프레임이다
    SW_EXPECT_EQUAL( uint64{ 384 }, report._totalBytes );
    SW_EXPECT_EQUAL( uint64{ 0 }, report._foreignThreadBlockCount );

    profiler.recordFree( pFirstRound, 128, MemoryTag::Scene, firstBlock );
    profiler.recordFree( pSecondRound, 256, MemoryTag::Scene, secondBlock );
    profiler.shutdown();
}

/**
 * @brief 창을 연 스레드가 아닌 스레드의 할당도 섞여 들고, 보고서가 그 개수를 따로 알린다
 * @details 창은 프로세스 전체의 할당을 센다(스레드를 가르지 않는다). 그 한계를 숨기지 않고 `_foreignThreadBlockCount` 로 드러낸다.
 */
SW_TEST_CASE( MemoryProfilerTest, LeakWindowReportsAllocationsFromOtherThreads )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );
    profiler.beginLeakWindow( LeakWindowDetail::FullStacks );

    void* const            pOwn     = reinterpret_cast<void*>( 0xF000 );
    const AllocationRecord ownBlock = profiler.recordAllocation( pOwn, 128, MemoryTag::Physics );

    void* const      pForeign = reinterpret_cast<void*>( 0x10000 );
    AllocationRecord foreignBlock;
    thread           worker( [&profiler, pForeign, &foreignBlock]()
    {
        foreignBlock = profiler.recordAllocation( pForeign, 512, MemoryTag::Physics );
    } );
    worker.join();

    const LeakWindowReport report = profiler.endLeakWindow();
    SW_REQUIRE_TRUE( report._bValid );
    SW_EXPECT_EQUAL( uint64{ 640 }, report._totalBytes ); // 두 스레드의 블록이 한 창에 섞여 있다
    SW_EXPECT_EQUAL( uint64{ 1 }, report._foreignThreadBlockCount );
    SW_EXPECT_EQUAL( uint64{ 512 }, report._foreignThreadBytes );

    profiler.recordFree( pOwn, 128, MemoryTag::Physics, ownBlock );
    profiler.recordFree( pForeign, 512, MemoryTag::Physics, foreignBlock );
    profiler.shutdown();
}

/**
 * @brief 열린 창 없이 닫으면 오류다 — 단언하고 무효 보고서를 돌려주며, 이미 열린 창을 다시 열면 단언하고 기존 창을 둔다
 */
SW_TEST_CASE( MemoryProfilerTest, LeakWindowMisuseIsAnError )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );

    {
        test::ScopedAssertCapture assertCapture;
        const LeakWindowReport    withoutWindow = profiler.endLeakWindow();
        SW_EXPECT_FALSE( withoutWindow._bValid );
        SW_EXPECT_TRUE( withoutWindow._listEntry.empty() );
        if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
    }

    profiler.beginLeakWindow( LeakWindowDetail::TagTotals );
    {
        test::ScopedAssertCapture assertCapture;
        profiler.beginLeakWindow( LeakWindowDetail::FullStacks ); // 이미 열려 있다
        SW_EXPECT_TRUE( profiler.isLeakWindowOpen() );
        if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
    }
    const LeakWindowReport report = profiler.endLeakWindow();
    SW_EXPECT_TRUE( report._detail == LeakWindowDetail::TagTotals ); // 기존 창이 그대로다
    profiler.shutdown();
}

/**
 * @brief 곡선 판정 — 워밍업 뒤가 평평하면 통과하고, 회차마다 느는 곡선 · 점이 모자란 곡선은 통과하지 못한다
 */
SW_TEST_CASE( MemoryProfilerTest, JudgeLeakCurveSeparatesFlatFromGrowing )
{
    const LeakCurveCriteria criteria;

    // 첫 회가 크고(캐시가 찬다) 이후 평평하다 — 워밍업을 버리면 기울기 0 이다.
    vector<LeakCurvePoint> listFlat{
        { 9000, 12 }
    };
    for ( uint32 round = 0; round < 8; ++round )
    {
        listFlat.push_back( LeakCurvePoint{ 1000, 4 } );
    }
    const LeakCurveVerdict flat = MemoryProfiler::judgeLeakCurve( listFlat, criteria );
    SW_REQUIRE_TRUE( flat._bValid );
    SW_EXPECT_EQUAL( 8u, flat._sampleCount );
    SW_EXPECT_NEAR( 0.0, flat._slopeBytesPerRound, 1.0e-6 );
    SW_EXPECT_TRUE( flat._bFlat );

    // 회차마다 4096 바이트 · 블록 하나가 남는다.
    vector<LeakCurvePoint> listGrowing{
        { 0, 0 }
    };
    for ( uint32 round = 0; round < 8; ++round )
    {
        listGrowing.push_back( LeakCurvePoint{ 1000 + uint64{ 4096 } * round, 4 + uint64{ 1 } * round } );
    }
    const LeakCurveVerdict growing = MemoryProfiler::judgeLeakCurve( listGrowing, criteria );
    SW_REQUIRE_TRUE( growing._bValid );
    SW_EXPECT_NEAR( 4096.0, growing._slopeBytesPerRound, 1.0e-6 );
    SW_EXPECT_NEAR( 1.0, growing._slopeBlocksPerRound, 1.0e-6 );
    SW_EXPECT_FALSE( growing._bFlat );

    // 허용치 안의 잡음은 평평하다. 줄어드는 곡선(기울기 < 0)도 누수가 아니다.
    const vector<LeakCurvePoint> listNoisy{
        {5000, 9},
        {1000, 4},
        {1100, 4},
        { 990, 4},
        {1080, 4},
        {1000, 4}
    };
    SW_EXPECT_TRUE( MemoryProfiler::judgeLeakCurve( listNoisy, criteria )._bFlat );
    const vector<LeakCurvePoint> listShrinking{
        {5000, 9},
        {4000, 8},
        {3000, 7},
        {2000, 6}
    };
    SW_EXPECT_TRUE( MemoryProfiler::judgeLeakCurve( listShrinking, criteria )._bFlat );

    // 점이 모자라면 판정할 수 없다 — 평평하다고 말하지 않는다.
    const LeakCurveVerdict tooShort = MemoryProfiler::judgeLeakCurve( vector<LeakCurvePoint>{
                                                                          {1, 1},
                                                                          {2, 1}
    },
                                                                      criteria );
    SW_EXPECT_FALSE( tooShort._bValid );
    SW_EXPECT_FALSE( tooShort._bFlat );
    SW_EXPECT_FALSE( MemoryProfiler::judgeLeakCurve( vector<LeakCurvePoint>{}, criteria )._bFlat );
}

/**
 * @brief 데이터가 적는 태그 이름은 철자 그대로 찾고(대소문자가 다르면 모르는 이름), 모르는 이름은 거절한다
 */
SW_TEST_CASE( MemoryProfilerTest, FindsTagsByName )
{
    MemoryTag tag{ MemoryTag::Unknown };
    SW_EXPECT_FALSE( MemoryProfiler::findMemoryTagByName( "texture", tag ) );
    SW_EXPECT_TRUE( MemoryProfiler::findMemoryTagByName( "Texture", tag ) );
    SW_EXPECT_TRUE( tag == MemoryTag::Texture );
    SW_EXPECT_TRUE( MemoryProfiler::findMemoryTagByName( "UI", tag ) );
    SW_EXPECT_TRUE( tag == MemoryTag::UI );
    SW_EXPECT_TRUE( MemoryProfiler::findMemoryTagByName( "Script", tag ) );
    SW_EXPECT_TRUE( tag == MemoryTag::Script );
    SW_EXPECT_FALSE( MemoryProfiler::findMemoryTagByName( "Textures", tag ) );
    SW_EXPECT_FALSE( MemoryProfiler::findMemoryTagByName( "", tag ) );
}

/**
 * @brief 다른 스레드의 스코프 태그로 잡은 블록을 이 스레드에서 풀어도 같은 줄에서 빠지고, 명시 태그는 스코프를 이긴다
 * @details 태그는 스레드 로컬이고 해제는 헤더에 적힌 태그로 뺀다. 워커가 잡고 게임 스레드가 푸는 버퍼(로드 결과)가 그 모양이다.
 */
SW_TEST_CASE( MemoryProfilerTest, TagsFollowTheBlockAcrossThreads )
{
    MemoryProfiler* pProfiler = MemoryProfiler::getActive(); // 테스트를 짓는 구성(배포본 밖)은 늘 엔진 프로파일러를 세운다(DiagnosticsSubsystem)
    SW_REQUIRE_NOT_NULL( pProfiler );
    const bool bWasTracking = pProfiler->isTrackingEnabled();
    pProfiler->setTrackingEnabled( true );

    constexpr uint32 kBlockCount = 64;
    constexpr size_t kBlockBytes = 1024;
    const uint64     uiBase      = pProfiler->getStats( MemoryTag::UI )._currentAllocatedBytes;
    const uint64     physicsBase = pProfiler->getStats( MemoryTag::Physics )._currentAllocatedBytes;

    vector<void*> listBlock( kBlockCount, nullptr );
    void**        ppBlock = listBlock.data();
    void*         pExplicit{ nullptr };
    thread        worker( [ppBlock, &pExplicit]()
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

    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::UI )._currentAllocatedBytes >= uiBase + kBlockCount * kBlockBytes );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::Physics )._currentAllocatedBytes >= physicsBase + kBlockBytes );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::UI )._peakAllocatedBytes >= uiBase + kBlockCount * kBlockBytes );

    // 이 스레드(태그가 UI 가 아니다)에서 푼다.
    for ( void* pBlock : listBlock )
    {
        Memory::free( pBlock );
    }
    Memory::free( pExplicit );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::UI )._currentAllocatedBytes < uiBase + kBlockBytes );
    SW_EXPECT_TRUE( pProfiler->getStats( MemoryTag::Physics )._currentAllocatedBytes < physicsBase + kBlockBytes );

    pProfiler->setTrackingEnabled( bWasTracking );
}
