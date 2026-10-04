#include "pch.h"

#include "Core/Memory/MemoryProfiler.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

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
             * @brief 원자 카운터에서 빼되 **0 아래로 내려가지 않게** 합니다.
             * @details 카운터가 `uint64` 라 그냥 `fetch_sub` 하면 0 아래가 1.8e19 로 돌아갑니다. 할당은 세지 않았는데 해제만 세는 경우가
             *          실제로 있습니다. 에디터의 프로파일러 패널에서 **추적을 런타임에 켤 수 있어서**, 켜기 전에 할당된 블록이 켠 뒤에
             *          해제되면 그렇게 됩니다.
             */
            static void subtractSaturating( atomic<uint64>& counter, uint64 amount )
            {
                uint64 current = counter.load( std::memory_order_relaxed );
                while ( true )
                {
                    const uint64 next = ( current >= amount ) ? ( current - amount ) : 0;
                    if ( counter.compare_exchange_weak( current, next, std::memory_order_relaxed, std::memory_order_relaxed ) )
                        return;
                }
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
                "RenderCpu",
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

            /** @brief 넘은 예산을 다시 감시하는 문턱(예산의 90 %)입니다. 경계에서 오락가락할 때 경고가 넘치지 않게 한다. */
            static constexpr uint64 kBudgetRearmPercent = 90;

            static inline thread_local bool       t_bIsInsideProfiler = false;
            static inline atomic<MemoryProfiler*> s_activeProfiler{ nullptr };
            static inline thread_local MemoryTag  t_currentMemoryTag = MemoryTag::Unknown;
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

    void MemoryProfiler::setCurrentMemoryTag( MemoryTag tag )
    {
        MemoryProfilerInternal::t_currentMemoryTag = tag;
    }

    MemoryTag MemoryProfiler::getCurrentMemoryTag()
    {
        return MemoryProfilerInternal::t_currentMemoryTag;
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
    }

    void MemoryProfiler::shutdown()
    {
        if ( _bInitialized.exchange( false ) == false )
            return;

        // 훅이 더 이상 이 인스턴스를 보지 않도록 먼저 등록을 해제한다.
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

    uint64 MemoryProfiler::recordAllocation( void* pPtr, size_t size, MemoryTag tag )
    {
        (void)pPtr;
        if ( _bTrackingEnabled.load( std::memory_order_relaxed ) == false )
            return 0;

        if ( MemoryProfilerInternal::t_bIsInsideProfiler )
            return 0; // 방어: 프로파일러 안에서 해시 맵이 할당할 때의 재귀를 막는다

        uint32 tagIdx = static_cast<uint32>( tag );
        if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
            tagIdx = 0;

        MemoryProfileStats& stats = _arrStat[tagIdx];
        stats._totalAllocatedBytes.fetch_add( size, std::memory_order_relaxed );
        const uint64 liveBytes = stats._currentAllocatedBytes.fetch_add( size, std::memory_order_relaxed ) + size;
        const uint64 liveCount = stats._currentAllocationCount.fetch_add( 1, std::memory_order_relaxed ) + 1;
        stats._totalAllocationCount.fetch_add( 1, std::memory_order_relaxed );
        MemoryProfilerInternal::raiseToAtLeast( stats._peakAllocatedBytes, liveBytes );
        MemoryProfilerInternal::raiseToAtLeast( stats._peakAllocationCount, liveCount );

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

        return outHash;
    }

    void MemoryProfiler::recordFree( void* pPtr, size_t size, MemoryTag tag, uint64 callStackHash )
    {
        if ( _bTrackingEnabled.load( std::memory_order_relaxed ) == false )
            return;

        if ( MemoryProfilerInternal::t_bIsInsideProfiler )
            return; // 방어: 프로파일러 안에서 해시 맵 노드를 해제할 때의 재귀를 막는다

        uint32 tagIdx = static_cast<uint32>( tag );
        if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
            tagIdx = 0;

        _arrStat[tagIdx]._totalFreedBytes.fetch_add( size, std::memory_order_relaxed );

        // **세지 않은 것을 빼면 안 된다.** 두 카운터는 `uint64` 라 0 아래로 내려가면 1.8e19 로 돌아간다. 에디터의 프로파일러
        // 패널에 **추적 켜기 체크박스**가 있어서, 켜기 전에 할당된 블록이 켠 뒤에 해제되면 바로 그 일이 생긴다(할당은 세지 않았는데
        // 해제만 센다). 바로 아래 콜 스택 표도 `>= size` 로 막는다.
        MemoryProfilerInternal::subtractSaturating( _arrStat[tagIdx]._currentAllocatedBytes, size );
        MemoryProfilerInternal::subtractSaturating( _arrStat[tagIdx]._currentAllocationCount, 1 );

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

    const MemoryProfileStats& MemoryProfiler::getStats( MemoryTag tag ) const
    {
        uint32 tagIdx = static_cast<uint32>( tag );
        if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
            tagIdx = 0;
        return _arrStat[tagIdx];
    }

    uint64 MemoryProfiler::getLiveAllocatedBytes() const
    {
        uint64 total = 0;
        for ( const MemoryProfileStats& stat : _arrStat )
            total += stat._currentAllocatedBytes.load( std::memory_order_relaxed );
        return total;
    }

    array<MemoryTag, kMemoryTagCount> MemoryProfiler::makeTagOrderByLiveBytes() const
    {
        // 정렬하는 동안 다른 스레드가 값을 바꿔도 비교가 흔들리지 않게 한 번 읽어 둔다.
        array<uint64, kMemoryTagCount>    arrLiveBytes{};
        array<MemoryTag, kMemoryTagCount> arrTag{};
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            arrLiveBytes[tagIndex] = _arrStat[tagIndex]._currentAllocatedBytes.load( std::memory_order_relaxed );
            arrTag[tagIndex]       = static_cast<MemoryTag>( tagIndex );
        }
        std::stable_sort( arrTag.begin(), arrTag.end(), [&arrLiveBytes]( MemoryTag lhs, MemoryTag rhs )
        { return arrLiveBytes[static_cast<uint32>( lhs )] > arrLiveBytes[static_cast<uint32>( rhs )]; } );
        return arrTag;
    }

    void MemoryProfiler::resetPeaks()
    {
        for ( MemoryProfileStats& stats : _arrStat )
        {
            stats._peakAllocatedBytes.store( stats._currentAllocatedBytes.load( std::memory_order_relaxed ), std::memory_order_relaxed );
            stats._peakAllocationCount.store( stats._currentAllocationCount.load( std::memory_order_relaxed ), std::memory_order_relaxed );
        }
    }

    void MemoryProfiler::setBudget( MemoryTag tag, uint64 budgetBytes )
    {
        const uint32        tagIndex = static_cast<uint32>( tag ) < kMemoryTagCount ? static_cast<uint32>( tag ) : 0;
        MemoryProfileStats& stats    = _arrStat[tagIndex];
        stats._budgetBytes.store( budgetBytes, std::memory_order_relaxed );
        stats._bOverBudget.store( false, std::memory_order_relaxed );
    }

    uint64 MemoryProfiler::getBudget( MemoryTag tag ) const
    {
        return getStats( tag )._budgetBytes.load( std::memory_order_relaxed );
    }

    void MemoryProfiler::clearBudgets()
    {
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
            setBudget( static_cast<MemoryTag>( tagIndex ), 0 );
    }

    uint32 MemoryProfiler::reportExceededBudgets( vector<MemoryTag>* pOutListNewlyExceeded )
    {
        uint32 newlyExceededCount{ 0 };
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            MemoryProfileStats& stats  = _arrStat[tagIndex];
            const uint64        budget = stats._budgetBytes.load( std::memory_order_relaxed );
            if ( budget == 0 )
                continue;
            const uint64 liveBytes = stats._currentAllocatedBytes.load( std::memory_order_relaxed );
            const bool   bWasOver  = stats._bOverBudget.load( std::memory_order_relaxed );
            if ( bWasOver == false && liveBytes > budget )
            {
                stats._bOverBudget.store( true, std::memory_order_relaxed );
                ++newlyExceededCount;
                if ( pOutListNewlyExceeded != nullptr )
                    pOutListNewlyExceeded->push_back( static_cast<MemoryTag>( tagIndex ) );
                SW_LOG_WARNING( "[MemoryBudget] %# is over budget: %# KB live (peak %# KB) > %# KB budget", MemoryProfilerInternal::kArrTagName[tagIndex],
                                liveBytes / 1024, stats._peakAllocatedBytes.load( std::memory_order_relaxed ) / 1024, budget / 1024 );
            }
            else if ( bWasOver && liveBytes * 100 < budget * MemoryProfilerInternal::kBudgetRearmPercent )
            {
                stats._bOverBudget.store( false, std::memory_order_relaxed );
            }
        }
        return newlyExceededCount;
    }

    void MemoryProfiler::captureTagBaseline()
    {
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            _arrBaselineBytes[tagIndex] = _arrStat[tagIndex]._currentAllocatedBytes.load( std::memory_order_relaxed );
            _arrBaselineCount[tagIndex] = _arrStat[tagIndex]._currentAllocationCount.load( std::memory_order_relaxed );
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
            const int64 byteDelta = static_cast<int64>( _arrStat[tagIndex]._currentAllocatedBytes.load( std::memory_order_relaxed ) ) -
                                    static_cast<int64>( _arrBaselineBytes[tagIndex] );
            const int64 countDelta = static_cast<int64>( _arrStat[tagIndex]._currentAllocationCount.load( std::memory_order_relaxed ) ) -
                                     static_cast<int64>( _arrBaselineCount[tagIndex] );
            if ( byteDelta > 0 || countDelta > 0 )
                listGrowth.push_back( MemoryTagGrowth{ static_cast<MemoryTag>( tagIndex ), byteDelta, countDelta } );
        }
        std::sort( listGrowth.begin(), listGrowth.end(), []( const MemoryTagGrowth& lhs, const MemoryTagGrowth& rhs )
        { return lhs._byteDelta > rhs._byteDelta; } );
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
                                                      getMemoryTagName( growth._tag ), growth._byteDelta, growth._countDelta );
        }
        return static_cast<uint32>( listGrowth.size() );
    }

    uint64 MemoryProfiler::getTotalAllocationCount() const
    {
        uint64 total = 0;
        for ( const MemoryProfileStats& stat : _arrStat )
            total += stat._totalAllocationCount.load( std::memory_order_relaxed );
        return total;
    }

    uint64 MemoryProfiler::getLiveAllocationCount() const
    {
        uint64 total = 0;
        for ( const MemoryProfileStats& stat : _arrStat )
            total += stat._currentAllocationCount.load( std::memory_order_relaxed );
        return total;
    }

    vector<CallStackAllocInfo> MemoryProfiler::getTopCallStacks( TopCallStackOrder order ) const
    {
        // 결과 버퍼는 **가드 밖에서** 잡는다. 가드(`t_bIsInsideProfiler`)를 켠 뒤 reserve 하면 이 할당은 세지 않고, 호출한 쪽이
        // 가드 밖에서 풀 때는 세어 현재 사용량이 줄기만 한다(프로파일러 패널이 매 프레임 불러 태그 카운터가 0 에 붙는다). 표가 자란 만큼만
        // 여유를 두고, 가드 안에서는 그 용량 안에서만 넣어 다시 할당하지 않는다.
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
            std::sort( listResult.begin(), listResult.end(), []( const CallStackAllocInfo& infoA, const CallStackAllocInfo& infoB )
            { return infoA._currentBytes > infoB._currentBytes; } );
        }
        else
        {
            std::sort( listResult.begin(), listResult.end(), []( const CallStackAllocInfo& infoA, const CallStackAllocInfo& infoB )
            { return infoA._totalCount > infoB._totalCount; } );
        }
        return listResult;
    }
} // namespace sw
