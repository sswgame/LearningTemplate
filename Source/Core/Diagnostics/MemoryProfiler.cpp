#include "pch.h"

#include "Core/Diagnostics/MemoryProfiler.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/CompareUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

// ASan 빌드에서는 CRT 누수 검사를 쓰지 않는다. ASan 이 힙을 자기 것으로 바꾸므로 `_CrtSetDbgFlag` 류가 모두 효과가 없고,
// 검사가 **도는 척만 하고 아무것도 잡지 못한다**(증상은 "set but not used" 경고 정도로 조용하다). Linux 쪽은 아래에서
// ASan 이면 LSAN 을 쓴다. Windows ASan 에는 LSAN 이 없으므로 여기서는 누수 검사가 **없는 것으로** 둔다.

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_DEBUG ) && !defined( SW_SHIPPING ) && !defined( SW_SANITIZER_ADDRESS )
    #define SW_HAS_CRT_LEAK_CHECK 1
#elif defined( SW_PLATFORM_LINUX ) && defined( SW_DEBUG ) && !defined( SW_SHIPPING )
    #if defined( __has_feature )
        #if __has_feature( address_sanitizer )
            #define SW_HAS_LSAN_LEAK_CHECK 1
        #endif
    #endif
    #if defined( __SANITIZE_ADDRESS__ )
        #define SW_HAS_LSAN_LEAK_CHECK 1
    #endif
#endif

#if defined( SW_HAS_LSAN_LEAK_CHECK )
extern "C" int32 __lsan_do_recoverable_leak_check( void );
#endif

namespace sw
{
    namespace
    {
        struct MemoryProfilerInternal
        {
            /**
             * @brief 칸의 합(부호 있음)을 살아 있는 양으로 읽습니다. 0 아래는 0 입니다.
             * @details 해제는 센 블록만 오므로(블록 헤더의 기록기 번호) 끝난 합은 0 아래로 가지 않습니다. 다만 칸을 차례로 읽는 동안 다른
             *          스레드가 앞 칸에 할당하고 뒤 칸에서 풀면 읽은 합이 잠깐 0 아래일 수 있어, 부호 없는 값으로 접히지 않게 자릅니다.
             */
            static uint64 clampLiveSum( int64 sum )
            {
                return sum > 0 ? static_cast<uint64>( sum ) : 0;
            }

#if defined( SW_HAS_CRT_LEAK_CHECK )
            static inline _CrtMemState s_leakBaseline{};
            static inline bool         s_bHasLeakBaseline{ false };

            static bool heapGrewVsBaseline( const _CrtMemState& now )
            {
                return now.lSizes[_NORMAL_BLOCK] > s_leakBaseline.lSizes[_NORMAL_BLOCK] ||
                       now.lCounts[_NORMAL_BLOCK] > s_leakBaseline.lCounts[_NORMAL_BLOCK] ||
                       now.lSizes[_CLIENT_BLOCK] > s_leakBaseline.lSizes[_CLIENT_BLOCK] ||
                       now.lCounts[_CLIENT_BLOCK] > s_leakBaseline.lCounts[_CLIENT_BLOCK];
            }
#endif

            template <typename... Args>
            [[maybe_unused]] static void printLeakMessage( const utf8* pFormat, Args&&... args )
            {
                utf8 arrBuf[constant::kMaxBuffer1024]{};
                formatstring( arrBuf, static_cast<uint32>( sizeof( arrBuf ) ), pFormat, std::forward<Args>( args )... );
                std::fputs( arrBuf, stderr );
                std::fputc( '\n', stderr );
            }

            /** @brief `MemoryTag` 값 순서의 표시 이름입니다. */
            static constexpr const utf8* kArrTagName[] = {
                "Unknown",
                "EngineMisc",
                "Task",
                "Reflection",
                "Asset",
                "Scene",
                "Texture",
                "Mesh",
                "Material",
                "Shader",
                "Animation",
                "Audio",
                "Physics",
                "Navigation",
                "RenderCPU",
                "UI",
                "Script",
                "Editor",
                "Game",
            };
            static_assert( sizeof( kArrTagName ) / sizeof( kArrTagName[0] ) == static_cast<size_t>( MemoryTag::MaxTags ),
                           "kArrTagName must have one name per MemoryTag" );

            /** @brief @p counter 를 @p value 까지 올립니다(더 크면 그대로). 새 최고치가 아니면 읽기 한 번으로 끝난다. */
            static void raiseToAtLeast( atomic<uint64>& counter, uint64 value )
            {
                uint64 current = counter.load( std::memory_order_relaxed );
                while ( current < value && counter.compare_exchange_weak( current, value, std::memory_order_relaxed, std::memory_order_relaxed ) == false )
                {
                }
            }

            /** @brief 넘은 예산을 다시 감시하는 임계값(예산의 90 %)입니다. 경계에서 오락가락할 때 경고가 넘치지 않게 한다. */
            static constexpr uint64 kBudgetRearmPercent = 90;

            static inline thread_local bool       t_bIsInsideProfiler = false;
            static inline atomic<MemoryProfiler*> s_activeProfiler{ nullptr };
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* MemoryProfiler::getMemoryTagName( MemoryTag tag )
    {
        const uint32 tagIndex = static_cast<uint32>( tag );
        if ( tagIndex >= static_cast<uint32>( MemoryTag::MaxTags ) )
            return "Invalid";
        return MemoryProfilerInternal::kArrTagName[tagIndex];
    }

    bool MemoryProfiler::findMemoryTagByName( string_view name, MemoryTag& outTag )
    {
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            if ( StringUtil::equals( name, MemoryProfilerInternal::kArrTagName[tagIndex], true ) )
            {
                outTag = static_cast<MemoryTag>( tagIndex );
                return true;
            }
        }
        return false;
    }

    uint64 MemoryProfiler::getPlatformHeapBytes()
    {
#if defined( SW_HAS_CRT_LEAK_CHECK )
        _CrtMemState state{};
        _CrtMemCheckpoint( &state );
        return static_cast<uint64>( state.lSizes[_NORMAL_BLOCK] ) + static_cast<uint64>( state.lSizes[_CLIENT_BLOCK] );
#else
        return 0;
#endif
    }

    bool MemoryProfiler::getPlatformHeapTotals( uint64& outTotalBytes, uint64& outRequestCount )
    {
        outTotalBytes   = 0;
        outRequestCount = 0;
#if defined( SW_HAS_CRT_LEAK_CHECK )
        _CrtMemState state{};
        _CrtMemCheckpoint( &state );
        outTotalBytes = static_cast<uint64>( state.lTotalCount );

        // 요청 번호는 할당마다 하나씩 오른다. 새 블록의 번호가 곧 지금까지의 할당 수다(이 탐침 하나가 더해진다).
        void* pProbe = _malloc_dbg( 1, _NORMAL_BLOCK, __FILE__, __LINE__ );
        if ( pProbe == nullptr )
            return false;
        LONG       requestNumber{ 0 }; // _CrtIsMemoryBlock 이 long* 을 받는다(Windows 갈래만)
        const bool bKnown = _CrtIsMemoryBlock( pProbe, 1, &requestNumber, nullptr, nullptr ) != 0;
        _free_dbg( pProbe, _NORMAL_BLOCK );
        if ( bKnown == false || requestNumber <= 0 )
            return false;
        outRequestCount = static_cast<uint64>( requestNumber );
        return true;
#else
        return false;
#endif
    }

    void MemoryProfiler::enableMemoryLeakChecks()
    {
#if defined( SW_HAS_CRT_LEAK_CHECK )
        int32 flags = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
        flags |= _CRTDBG_ALLOC_MEM_DF;
        flags &= ~_CRTDBG_LEAK_CHECK_DF;
        _CrtSetDbgFlag( flags );

        _CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
        _CrtSetReportFile( _CRT_WARN, _CRTDBG_FILE_STDERR );
        _CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
        _CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDERR );

        MemoryProfilerInternal::s_bHasLeakBaseline = false;
#endif
    }

    void MemoryProfiler::captureMemoryLeakBaseline()
    {
        // 태그별 기준선은 플랫폼 검사와 따로 찍는다 — 종료 끝에 어느 용도가 늘었는지(`reportTagGrowthSinceBaseline`) 본다.
        MemoryProfiler* pActive = getActive();
        if ( pActive != nullptr )
            pActive->captureTagBaseline();
#if defined( SW_HAS_CRT_LEAK_CHECK )
        _CrtMemCheckpoint( &MemoryProfilerInternal::s_leakBaseline );
        MemoryProfilerInternal::s_bHasLeakBaseline = true;
        MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] CRT baseline captured (post-init): %# normal bytes in %# blocks.",
                                                  static_cast<uint64>( MemoryProfilerInternal::s_leakBaseline.lSizes[_NORMAL_BLOCK] ),
                                                  static_cast<uint64>( MemoryProfilerInternal::s_leakBaseline.lCounts[_NORMAL_BLOCK] ) );
#endif
    }

    int32 MemoryProfiler::reportMemoryLeaks( const utf8* pPhaseTag )
    {
        const utf8* pPhase = StringUtil::isNullOrEmpty( pPhaseTag ) ? "shutdown" : pPhaseTag;

#if defined( SW_HAS_CRT_LEAK_CHECK )
        if ( MemoryProfilerInternal::s_bHasLeakBaseline )
        {
            _CrtMemState now{};
            _CrtMemCheckpoint( &now );

            if ( MemoryProfilerInternal::heapGrewVsBaseline( now ) )
            {
                _CrtMemState diff{};
                _CrtMemDifference( &diff, &MemoryProfilerInternal::s_leakBaseline, &now );

                MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# — heap larger than post-init baseline (%# -> %# normal bytes).",
                                                          pPhase,
                                                          static_cast<uint64>( MemoryProfilerInternal::s_leakBaseline.lSizes[_NORMAL_BLOCK] ),
                                                          static_cast<uint64>( now.lSizes[_NORMAL_BLOCK] ) );
                _CrtMemDumpStatistics( &diff );
                _CrtDumpMemoryLeaks();
                return 1;
            }

            MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# — no CRT leaks (normal %# -> %# bytes).",
                                                      pPhase,
                                                      static_cast<uint64>( MemoryProfilerInternal::s_leakBaseline.lSizes[_NORMAL_BLOCK] ),
                                                      static_cast<uint64>( now.lSizes[_NORMAL_BLOCK] ) );
            return 0;
        }

        MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# — CRT _CrtDumpMemoryLeaks() (no baseline)", pPhase );
        return _CrtDumpMemoryLeaks();

#elif defined( SW_HAS_LSAN_LEAK_CHECK )
        MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# — __lsan_do_recoverable_leak_check()", pPhase );
        return __lsan_do_recoverable_leak_check() != 0 ? 1 : 0;

#else
    #if defined( SW_DEBUG ) && !defined( SW_SHIPPING )
        MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# — no in-process checker. Windows Debug CRT: rebuild Debug. "
                                                  "Linux: cmake -DSW_ENABLE_SANITIZER=ON OR valgrind --leak-check=full ./App",
                                                  pPhase );
    #else
        (void)pPhase;
    #endif
        return 0;
#endif
    }

    MemoryProfiler::MemoryProfiler()
        : _bInitialized{ false }
        , _bTrackingEnabled{ true }
        , _bDetailedTrackingEnabled{ false }
        , _shard{}
        , _arrTagSummary{}
        , _arrBaselineBytes{}
        , _arrBaselineCount{}
        , _bHasTagBaseline{ false }
    {
    }

    MemoryProfiler::~MemoryProfiler()
    {
        shutdown();
    }

    void MemoryProfiler::initialize()
    {
        if ( _bInitialized.exchange( true ) )
            return;

        CallStackCapture::initialize();

        MemoryProfiler* pExpected{ nullptr };
        MemoryProfilerInternal::s_activeProfiler.compare_exchange_strong( pExpected, this, std::memory_order_acq_rel, std::memory_order_relaxed );
        Memory::registerAllocationTracker( this );
    }

    void MemoryProfiler::shutdown()
    {
        if ( _bInitialized.exchange( false ) == false )
            return;

        // 훅이 더 이상 이 인스턴스를 보지 않도록 먼저 등록을 해제한다.
        Memory::unregisterAllocationTracker( this );
        auto pExpected = this;
        MemoryProfilerInternal::s_activeProfiler.compare_exchange_strong( pExpected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed );

        _bTrackingEnabled.store( false, std::memory_order_relaxed );
        CallStackCapture::shutdown();
    }

    MemoryProfiler* MemoryProfiler::getActive()
    {
        return MemoryProfilerInternal::s_activeProfiler.load( std::memory_order_acquire );
    }

    void MemoryProfiler::setTrackingEnabled( bool bEnabled )
    {
        _bTrackingEnabled.store( bEnabled, std::memory_order_relaxed );
    }

    void MemoryProfiler::setDetailedTrackingEnabled( bool bEnabled )
    {
        _bDetailedTrackingEnabled.store( bEnabled, std::memory_order_relaxed );
    }

    AllocationRecord MemoryProfiler::recordAllocation( void* pPtr, size_t size, MemoryTag tag )
    {
        (void)pPtr;
        if ( _bTrackingEnabled.load( std::memory_order_relaxed ) == false )
            return AllocationRecord{};

        if ( MemoryProfilerInternal::t_bIsInsideProfiler )
            return AllocationRecord{}; // 방어: 프로파일러 안에서 해시 맵이 할당할 때의 재귀를 막는다. 세지 않으므로 그 해제도 오지 않는다

        uint32 tagIdx = static_cast<uint32>( tag );
        if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
            tagIdx = 0;

        // 지금 스레드 칸에만 더한다 — 다른 스레드와 캐시 라인을 다투지 않는다. 최고치는 합을 읽는 쪽이 올린다(`computeTagLive`).
        MemoryTagShardCounter& counter = _shard.getCurrentThreadShard()._arrTag[tagIdx];
        counter._liveBytes.fetch_add( static_cast<int64>( size ), std::memory_order_relaxed );
        counter._liveCount.fetch_add( 1, std::memory_order_relaxed );
        counter._totalAllocatedBytes.fetch_add( size, std::memory_order_relaxed );
        counter._totalAllocationCount.fetch_add( 1, std::memory_order_relaxed );

        uint64 outHash{ 0 };

        if ( _bDetailedTrackingEnabled.load( std::memory_order_relaxed ) )
        {
            CallStack stack;
            // skipFrames: capture(0), recordAllocation(1), operator new(2). 위쪽 2프레임을 건너뛴다.
            CallStackCapture::capture( stack, 2 );
            outHash = stack._hash;

            MemoryProfilerInternal::t_bIsInsideProfiler = true;
            {
                std::scoped_lock<mutex> lock{ _stackMapMutex };
                if ( pPtr != nullptr )
                    _mapPtrToCallStackHash[pPtr] = outHash;
                auto& info = _mapCallStackAllocInfo[outHash];
                if ( info._stack._frameCount == 0 )
                    info._stack = stack;
                info._currentBytes += size;
                info._currentCount++;
                info._totalBytes += size;
                info._totalCount++;
            }
            MemoryProfilerInternal::t_bIsInsideProfiler = false;
        }

        return AllocationRecord{ outHash, true };
    }

    void MemoryProfiler::recordFree( void* pPtr, size_t size, MemoryTag tag, uint64 callStackHash )
    {
        // 추적 스위치는 보지 않는다 — 이 블록은 셀 때 켜져 있었다(할당기가 센 블록만 넘긴다). 꺼진 동안의 해제를 건너뛰면 살아 있는 양이 위로 어긋난다.
        uint32 tagIdx = static_cast<uint32>( tag );
        if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
            tagIdx = 0;

        // 지금 스레드 칸에서 뺀다. 할당한 스레드의 칸과 달라 이 칸은 음수가 될 수 있지만 합은 맞다.
        MemoryTagShardCounter& counter = _shard.getCurrentThreadShard()._arrTag[tagIdx];
        counter._totalFreedBytes.fetch_add( size, std::memory_order_relaxed );
        counter._liveBytes.fetch_sub( static_cast<int64>( size ), std::memory_order_relaxed );
        counter._liveCount.fetch_sub( 1, std::memory_order_relaxed );

        // 프로파일러 안(해시 맵을 고치는 중)에서는 콜 스택 표를 건드리지 않는다 — 재귀로 같은 잠금을 다시 잡는다.
        if ( MemoryProfilerInternal::t_bIsInsideProfiler )
            return;

        if ( _bDetailedTrackingEnabled.load( std::memory_order_relaxed ) )
        {
            MemoryProfilerInternal::t_bIsInsideProfiler = true;
            {
                std::scoped_lock<mutex> lock{ _stackMapMutex };
                uint64                  hash = callStackHash;
                if ( hash == 0 && pPtr != nullptr )
                {
                    auto itPtr = _mapPtrToCallStackHash.find( pPtr );
                    if ( itPtr != _mapPtrToCallStackHash.end() )
                    {
                        hash = itPtr->second;
                        _mapPtrToCallStackHash.erase( itPtr );
                    }
                }
                else if ( pPtr != nullptr )
                {
                    _mapPtrToCallStackHash.erase( pPtr );
                }

                if ( hash != 0 )
                {
                    auto it = _mapCallStackAllocInfo.find( hash );
                    if ( it != _mapCallStackAllocInfo.end() )
                    {
                        if ( it->second._currentBytes >= size )
                            it->second._currentBytes -= size;
                        else
                            it->second._currentBytes = 0;

                        if ( it->second._currentCount > 0 )
                            it->second._currentCount--;
                    }
                }
            }
            MemoryProfilerInternal::t_bIsInsideProfiler = false;
        }
    }

    void MemoryProfiler::computeTagLive( uint32 tagIndex, uint64& outLiveBytes, uint64& outLiveCount ) const
    {
        int64 liveBytesSum = 0;
        int64 liveCountSum = 0;
        for ( uint32 shardIndex = 0; shardIndex < ShardedCounterUtil::kShardCount; ++shardIndex )
        {
            const MemoryTagShardCounter& counter = _shard.getShard( shardIndex )._arrTag[tagIndex];
            liveBytesSum += counter._liveBytes.load( std::memory_order_relaxed );
            liveCountSum += counter._liveCount.load( std::memory_order_relaxed );
        }
        outLiveBytes = MemoryProfilerInternal::clampLiveSum( liveBytesSum );
        outLiveCount = MemoryProfilerInternal::clampLiveSum( liveCountSum );

        MemoryTagSummary& summary = _arrTagSummary[tagIndex];
        MemoryProfilerInternal::raiseToAtLeast( summary._peakAllocatedBytes, outLiveBytes );
        MemoryProfilerInternal::raiseToAtLeast( summary._peakAllocationCount, outLiveCount );
    }

    MemoryProfileStats MemoryProfiler::getStats( MemoryTag tag ) const
    {
        uint32 tagIdx = static_cast<uint32>( tag );
        if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
            tagIdx = 0;

        MemoryProfileStats stats{};
        for ( uint32 shardIndex = 0; shardIndex < ShardedCounterUtil::kShardCount; ++shardIndex )
        {
            const MemoryTagShardCounter& counter = _shard.getShard( shardIndex )._arrTag[tagIdx];
            stats._totalAllocatedBytes += counter._totalAllocatedBytes.load( std::memory_order_relaxed );
            stats._totalAllocationCount += counter._totalAllocationCount.load( std::memory_order_relaxed );
            stats._totalFreedBytes += counter._totalFreedBytes.load( std::memory_order_relaxed );
        }
        computeTagLive( tagIdx, stats._currentAllocatedBytes, stats._currentAllocationCount );

        const MemoryTagSummary& summary = _arrTagSummary[tagIdx];
        stats._peakAllocatedBytes       = summary._peakAllocatedBytes.load( std::memory_order_relaxed );
        stats._peakAllocationCount      = summary._peakAllocationCount.load( std::memory_order_relaxed );
        stats._budgetBytes              = summary._budgetBytes.load( std::memory_order_relaxed );
        stats._bOverBudget              = summary._bOverBudget.load( std::memory_order_relaxed );
        return stats;
    }

    uint64 MemoryProfiler::getLiveAllocatedBytes() const
    {
        uint64 total = 0;
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            uint64 liveBytes = 0;
            uint64 liveCount = 0;
            computeTagLive( tagIndex, liveBytes, liveCount );
            total += liveBytes;
        }
        return total;
    }

    array<MemoryTag, kMemoryTagCount> MemoryProfiler::makeTagOrderByLiveBytes() const
    {
        // 정렬하는 동안 다른 스레드가 값을 바꿔도 비교가 흔들리지 않게 한 번 읽어 둔다.
        array<uint64, kMemoryTagCount>    arrLiveBytes{};
        array<MemoryTag, kMemoryTagCount> arrTag{};
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            uint64 liveCount = 0;
            computeTagLive( tagIndex, arrLiveBytes[tagIndex], liveCount );
            arrTag[tagIndex] = static_cast<MemoryTag>( tagIndex );
        }
        std::stable_sort( arrTag.begin(), arrTag.end(), [&arrLiveBytes]( MemoryTag lhs, MemoryTag rhs )
        { return arrLiveBytes[static_cast<uint32>( lhs )] > arrLiveBytes[static_cast<uint32>( rhs )]; } );
        return arrTag;
    }

    void MemoryProfiler::resetPeaks()
    {
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            uint64 liveBytes = 0;
            uint64 liveCount = 0;
            computeTagLive( tagIndex, liveBytes, liveCount );
            MemoryTagSummary& summary = _arrTagSummary[tagIndex];
            summary._peakAllocatedBytes.store( liveBytes, std::memory_order_relaxed );
            summary._peakAllocationCount.store( liveCount, std::memory_order_relaxed );
        }
    }

    void MemoryProfiler::setBudget( MemoryTag tag, uint64 budgetBytes )
    {
        const uint32      tagIndex = static_cast<uint32>( tag ) < kMemoryTagCount ? static_cast<uint32>( tag ) : 0;
        MemoryTagSummary& summary  = _arrTagSummary[tagIndex];
        summary._budgetBytes.store( budgetBytes, std::memory_order_relaxed );
        summary._bOverBudget.store( false, std::memory_order_relaxed );
    }

    uint64 MemoryProfiler::getBudget( MemoryTag tag ) const
    {
        const uint32 tagIndex = static_cast<uint32>( tag ) < kMemoryTagCount ? static_cast<uint32>( tag ) : 0;
        return _arrTagSummary[tagIndex]._budgetBytes.load( std::memory_order_relaxed );
    }

    void MemoryProfiler::clearBudgets()
    {
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            setBudget( static_cast<MemoryTag>( tagIndex ), 0 );
        }
    }

    uint32 MemoryProfiler::reportExceededBudgets( vector<MemoryTag>* pOutListNewlyExceeded )
    {
        uint32 newlyExceededCount{ 0 };
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            // 프레임마다 한 번 부르는 곳이라 예산이 없는 태그도 합을 읽어 최고치를 올린다(최고치를 프레임마다 보는 자리).
            uint64 liveBytes = 0;
            uint64 liveCount = 0;
            computeTagLive( tagIndex, liveBytes, liveCount );
            MemoryTagSummary& summary = _arrTagSummary[tagIndex];
            const uint64      budget  = summary._budgetBytes.load( std::memory_order_relaxed );
            if ( budget == 0 )
                continue;
            const bool bWasOver = summary._bOverBudget.load( std::memory_order_relaxed );
            if ( bWasOver == false && liveBytes > budget )
            {
                summary._bOverBudget.store( true, std::memory_order_relaxed );
                ++newlyExceededCount;
                if ( pOutListNewlyExceeded != nullptr )
                    pOutListNewlyExceeded->push_back( static_cast<MemoryTag>( tagIndex ) );
                SW_LOG_WARNING( "[MemoryBudget] %# is over budget: %# KB live (peak %# KB) > %# KB budget", MemoryProfilerInternal::kArrTagName[tagIndex],
                                liveBytes / 1024, summary._peakAllocatedBytes.load( std::memory_order_relaxed ) / 1024, budget / 1024 );
            }
            else if ( bWasOver && liveBytes * 100 < budget * MemoryProfilerInternal::kBudgetRearmPercent )
            {
                summary._bOverBudget.store( false, std::memory_order_relaxed );
            }
        }
        return newlyExceededCount;
    }

    void MemoryProfiler::captureTagBaseline()
    {
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            computeTagLive( tagIndex, _arrBaselineBytes[tagIndex], _arrBaselineCount[tagIndex] );
        }
        _bHasTagBaseline = true;
    }

    vector<MemoryTagGrowth> MemoryProfiler::collectTagGrowthSinceBaseline() const
    {
        vector<MemoryTagGrowth> listGrowth;
        if ( _bHasTagBaseline == false )
            return listGrowth;
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            uint64 liveBytes = 0;
            uint64 liveCount = 0;
            computeTagLive( tagIndex, liveBytes, liveCount );
            const int64 byteDelta       = static_cast<int64>( liveBytes ) - static_cast<int64>( _arrBaselineBytes[tagIndex] );
            const int64 blockCountDelta = static_cast<int64>( liveCount ) - static_cast<int64>( _arrBaselineCount[tagIndex] );
            if ( byteDelta > 0 || blockCountDelta > 0 )
                listGrowth.push_back( MemoryTagGrowth{ static_cast<MemoryTag>( tagIndex ), byteDelta, blockCountDelta } );
        }
        std::sort( listGrowth.begin(), listGrowth.end(), Descending<&MemoryTagGrowth::_byteDelta>{} );
        return listGrowth;
    }

    uint32 MemoryProfiler::reportTagGrowthSinceBaseline( const utf8* pPhaseTag ) const
    {
        if ( _bHasTagBaseline == false )
            return 0;
        const utf8*                   pPhase     = StringUtil::isNullOrEmpty( pPhaseTag ) ? "shutdown" : pPhaseTag;
        const vector<MemoryTagGrowth> listGrowth = collectTagGrowthSinceBaseline();
        if ( listGrowth.empty() )
        {
            MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# - no memory tag grew since the post-init baseline.", pPhase );
            return 0;
        }
        for ( const MemoryTagGrowth& growth : listGrowth )
        {
            MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# - tag %# grew by %# bytes in %# blocks since the post-init baseline.", pPhase,
                                                      getMemoryTagName( growth._tag ), growth._byteDelta, growth._blockCountDelta );
        }
        return static_cast<uint32>( listGrowth.size() );
    }

    uint64 MemoryProfiler::getTotalAllocatedBytes() const
    {
        uint64 totalBytes{ 0 };
        for ( uint32 shardIndex = 0; shardIndex < ShardedCounterUtil::kShardCount; ++shardIndex )
        {
            for ( const MemoryTagShardCounter& counter : _shard.getShard( shardIndex )._arrTag )
            {
                totalBytes += counter._totalAllocatedBytes.load( std::memory_order_relaxed );
            }
        }
        return totalBytes;
    }

    uint64 MemoryProfiler::getTotalAllocationCount() const
    {
        uint64 total = 0;
        for ( uint32 shardIndex = 0; shardIndex < ShardedCounterUtil::kShardCount; ++shardIndex )
        {
            for ( const MemoryTagShardCounter& counter : _shard.getShard( shardIndex )._arrTag )
            {
                total += counter._totalAllocationCount.load( std::memory_order_relaxed );
            }
        }
        return total;
    }

    uint64 MemoryProfiler::getLiveAllocationCount() const
    {
        uint64 total = 0;
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            uint64 liveBytes = 0;
            uint64 liveCount = 0;
            computeTagLive( tagIndex, liveBytes, liveCount );
            total += liveCount;
        }
        return total;
    }

    vector<CallStackAllocInfo> MemoryProfiler::getTopCallStacks( TopCallStackOrder order ) const
    {
        // 결과 버퍼는 **가드 밖에서** 잡아 호출한 쪽의 태그로 센다. 가드(`t_bIsInsideProfiler`) 안에서 잡으면 세지 않는 블록이 된다
        // (그 해제도 세지 않으므로 어긋나지는 않는다). 표가 자란 만큼만 여유를 두고, 가드 안에서는 그 용량 안에서만 넣어 다시 할당하지 않는다.
        size_t entryCount{ 0 };
        {
            std::scoped_lock<mutex> lock{ _stackMapMutex };
            entryCount = _mapCallStackAllocInfo.size();
        }
        vector<CallStackAllocInfo> listResult;
        listResult.reserve( entryCount + entryCount / 4 + 16 );

        MemoryProfilerInternal::t_bIsInsideProfiler = true;
        {
            std::scoped_lock<mutex> lock{ _stackMapMutex };
            for ( const auto& [hash, info] : _mapCallStackAllocInfo )
            {
                const bool bIncluded = ( order == TopCallStackOrder::LiveBytes ) ? ( info._currentBytes > 0 ) : ( info._totalCount > 0 );
                if ( bIncluded && listResult.size() < listResult.capacity() )
                    listResult.push_back( info );
            }
        }
        MemoryProfilerInternal::t_bIsInsideProfiler = false;
        if ( order == TopCallStackOrder::LiveBytes )
        {
            std::sort( listResult.begin(), listResult.end(), Descending<&CallStackAllocInfo::_currentBytes>{} );
        }
        else
        {
            std::sort( listResult.begin(), listResult.end(), Descending<&CallStackAllocInfo::_totalCount>{} );
        }
        return listResult;
    }
} // namespace sw
