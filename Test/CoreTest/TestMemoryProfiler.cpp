#include "pch.h"

#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryProfiler.h"

#include "TestFramework/TestFramework.h"

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
 *
 *          같은 함수 안의 콜스택 표는 처음부터 `>= size` 로 막고 있었다 — 위쪽만 빠져 있었다.
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

    // 이제 켜고 해제한다 — 세지 않은 것을 빼게 된다.
    profiler.setTrackingEnabled( true );
    profiler.recordFree( pDummy, 4096, MemoryTag::Game, 0 );

    const uint64 afterBytes = profiler.getStats( MemoryTag::Game )._currentAllocatedBytes.load();
    const uint64 afterCount = profiler.getStats( MemoryTag::Game )._currentAllocationCount.load();

    // 고치기 전에는 여기가 1.8e19 였다.
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
 * @details 예전에는 결과 버퍼를 추적 가드 안에서 잡아 할당은 세지 않고, 호출한 쪽이 풀 때만 세어 현재 사용량이 줄기만 했다. 프로파일러
 *          패널이 매 프레임 불러 태그 카운터가 0 에 붙었다.
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
        listKeepAlive.emplace_back( 256 + index );

    const MemoryTag tag       = MemoryProfiler::getCurrentMemoryTag();
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

    // 다른 스레드의 할당이 섞여 들 수 있어 정확히 같을 필요는 없다. 예전 결함은 호출마다 버퍼 한 벌씩 줄어 64 벌 줄었다.
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
    const MemoryTag  outerTag    = MemoryProfiler::getCurrentMemoryTag();
    const uint64     textureBase = pProfiler->getStats( MemoryTag::Texture )._currentAllocatedBytes.load();
    const uint64     meshBase    = pProfiler->getStats( MemoryTag::Mesh )._currentAllocatedBytes.load();

    void* pTextureBlock{ nullptr };
    void* pMeshBlock{ nullptr };
    {
        SW_MEMORY_SCOPE( Texture );
        SW_EXPECT_TRUE( MemoryProfiler::getCurrentMemoryTag() == MemoryTag::Texture );
        pTextureBlock = Memory::allocate( kBlockBytes );
        {
            SW_MEMORY_SCOPE( Mesh );
            SW_MEMORY_SCOPE( Mesh );
            SW_EXPECT_TRUE( MemoryProfiler::getCurrentMemoryTag() == MemoryTag::Mesh );
            pMeshBlock = Memory::allocate( kBlockBytes );
        }
        SW_EXPECT_TRUE( MemoryProfiler::getCurrentMemoryTag() == MemoryTag::Texture );
    }
    SW_EXPECT_TRUE( MemoryProfiler::getCurrentMemoryTag() == outerTag );

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
