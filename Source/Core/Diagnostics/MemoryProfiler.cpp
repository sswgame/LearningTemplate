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

            /** @brief 이 스레드에만 있는 변수입니다. 그 주소가 살아 있는 스레드 사이에서 겹치지 않아 스레드 식별 값으로 쓴다. */
            static inline thread_local uint8 t_threadMarker = 0;

            /** @brief 지금 스레드의 식별 값입니다(창을 연 스레드와 블록을 낳은 스레드를 비교하는 데만 쓴다). */
            static uint64 getCurrentThreadKey()
            {
                return static_cast<uint64>( reinterpret_cast<uintptr_t>( &t_threadMarker ) );
            }

            /** @brief 누수 창의 블록 테이블에서 @p pPtr 의 슬롯 번호입니다. 힙 블록은 16 바이트 단위라 아래 4 비트를 버린다(`ShardArray` 가 슬롯 수로 접는다). */
            static uint32 getPointerShardIndex( const void* pPtr )
            {
                return static_cast<uint32>( reinterpret_cast<uintptr_t>( pPtr ) >> 4 );
            }

            /** @brief 스택 첫 프레임에서 거를 프레임의 표지입니다 — 할당기 · 컨테이너 안쪽 프레임은 누가 할당했는지 말해 주지 않는다. */
            static constexpr const utf8* kArrAllocatorFrameMarker[] = {
                "Core/Memory/",
                "Core\\Memory\\",
                "Core/Container/",
                "Core\\Container\\",
                "sw::Memory::",
                "MemoryInternal::",
            };
            /** @brief 모든 프레임이 거를 프레임일 때 첫 프레임 항목에 쓰는 글입니다. */
            static constexpr const utf8* kNoCallerFrameText = "[no frame outside Core/Memory and Core/Container]";
            /** @brief 보고서를 stderr 로 남길 때의 최대 줄 수입니다(나머지는 몇 줄을 줄였는지만 적는다). */
            static constexpr uint32 kMaxPrintedLeakEntry = 64;

            /** @brief 심볼 글 한 줄이 할당기 · 컨테이너 안쪽 프레임이면 true 입니다. */
            static bool isAllocatorFrameText( string_view text )
            {
                for ( const utf8* pMarker : kArrAllocatorFrameMarker )
                {
                    if ( StringUtil::contains( text, pMarker ) )
                        return true;
                }
                return false;
            }

            /** @brief 스택에서 할당기 · 컨테이너 프레임을 건너뛴 첫 프레임의 심볼 글입니다(`[0] 함수 (파일:줄)`). 프레임마다 따로 변환한다. */
            static string describeFirstCallerFrame( const CallStack& stack )
            {
                for ( uint32 frameIndex = 0; frameIndex < stack._frameCount; ++frameIndex )
                {
                    CallStack singleFrame;
                    singleFrame._arrFrame[0] = stack._arrFrame[frameIndex];
                    singleFrame._frameCount  = 1;
                    const string text        = CallStackCapture::symbolize( singleFrame );
                    if ( isAllocatorFrameText( text ) == false )
                        return string( StringUtil::trim( text ) );
                }
                return string( kNoCallerFrameText );
            }
        };

        /** @brief 창을 닫을 때 블록 하나를 (태그, 스택, 회차) 로 묶기 위해 모은 표본입니다. */
        struct LeakBlockSample
        {
            uint64    _stackHash{ 0 };
            uint64    _size{ 0 };
            uint32    _round{ 0 };
            MemoryTag _tag{ MemoryTag::Unknown };
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
        LONG       requestNumber{ 0 }; // _CrtIsMemoryBlock 이 long* 을 받는다(Windows 분기만)
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
        // 태그별 기준선은 플랫폼 검사와 따로 잡는다 — 기동 뒤부터 종료까지 하나의 누수 창(태그 합계)을 열어 두고,
        // 종료 끝에 어느 용도가 늘었는지(`endLeakWindow` → `printLeakWindowReport`) 본다. 긴 창이라 스택은 잡지 않는다.
        MemoryProfiler* pActive = getActive();
        if ( pActive != nullptr )
            pActive->beginLeakWindow( LeakWindowDetail::TagTotals );
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

        // 기준선 없이는 살아 있는 블록 전부(부트스트랩 몫 포함)가 누수로 보여 진짜 누수를 가린다 — 덤프하지 않고 오류로 알린다.
        // 기준선은 기동 끝(`Engine::initialize`) 또는 헤드리스 진입(`HeadlessTaskState::enterHeadless`)에서 잡는다. 거기에 닿기 전에 기동이 멈췄어도 이 줄이 남는다.
        MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# — ERROR: no CRT baseline was captured, so leaks cannot be told from live startup blocks (nothing dumped)", pPhase );
        return 1;

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
        , _leakStampCounter{ 0 }
        , _leakFirstStamp{ 0 }
        , _leakCurrentStamp{ 0 }
        , _bLeakWindowStacks{ false }
        , _leakOwnerThreadKey{ 0 }
        , _leakShard{}
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
        if ( _bTrackingEnabled.load( std::memory_order_relaxed ) == false )
            return AllocationRecord{};

        if ( MemoryProfilerInternal::t_bIsInsideProfiler )
            return AllocationRecord{}; // 방어: 프로파일러 안에서 해시 맵이 할당할 때의 재귀를 막는다. 세지 않으므로 그 해제도 오지 않는다

        uint32 tagIdx = static_cast<uint32>( tag );
        if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
            tagIdx = 0;

        // 지금 스레드 슬롯에만 더한다 — 다른 스레드와 캐시 라인을 다투지 않는다. 최고치는 합을 읽는 쪽이 올린다(`computeTagLive`).
        MemoryTagShardCounter& counter = _shard.getCurrentThreadShard()._arrTag[tagIdx];
        counter._liveBytes.fetch_add( static_cast<int64>( size ), std::memory_order_relaxed );
        counter._liveCount.fetch_add( 1, std::memory_order_relaxed );
        counter._totalAllocatedBytes.fetch_add( size, std::memory_order_relaxed );
        counter._totalAllocationCount.fetch_add( 1, std::memory_order_relaxed );

        // 열린 창이면 이 블록에 회차 스탬프를 찍는다. 스탬프는 헤더에 남아 해제 때 돌아온다(`recordFree` 의 `born`).
        const uint32 windowStamp   = _leakCurrentStamp.load( std::memory_order_acquire );
        const bool   bWindowStacks = windowStamp != 0 && _bLeakWindowStacks.load( std::memory_order_relaxed );
        if ( windowStamp != 0 && bWindowStacks == false )
        {
            counter._windowLiveBytes.fetch_add( static_cast<int64>( size ), std::memory_order_relaxed );
            counter._windowLiveCount.fetch_add( 1, std::memory_order_relaxed );
        }

        uint64 outHash{ 0 };

        const bool bDetailed = _bDetailedTrackingEnabled.load( std::memory_order_relaxed );
        if ( bDetailed || bWindowStacks )
        {
            CallStack stack;
            // skipFrames: capture(0), recordAllocation(1), operator new(2). 위쪽 2프레임을 건너뛴다.
            CallStackCapture::capture( stack, 2 );

            if ( bDetailed )
            {
                outHash = stack._hash;

                MemoryProfilerInternal::t_bIsInsideProfiler = true;
                {
                    ScopedLock<mutex> lock{ _stackMapMutex };
                    auto&             info = _mapCallStackAllocInfo[outHash];
                    if ( info._stack._frameCount == 0 )
                        info._stack = stack;
                    info._currentBytes += size;
                    info._currentCount++;
                    info._totalBytes += size;
                    info._totalCount++;
                }
                MemoryProfilerInternal::t_bIsInsideProfiler = false;
            }

            if ( bWindowStacks )
                addLeakWindowBlock( pPtr, size, tag, windowStamp, stack );
        }

        return AllocationRecord{ outHash, true, windowStamp };
    }

    void MemoryProfiler::recordFree( void* pPtr, size_t size, MemoryTag tag, const AllocationRecord& born )
    {
        const uint64 callStackHash = born._callStackHash; // 해시는 할당 때 블록 헤더에 적혀 온다 — 주소로 다시 찾지 않는다
        // 추적 스위치는 보지 않는다 — 이 블록은 셀 때 켜져 있었다(할당기가 센 블록만 넘긴다). 꺼진 동안의 해제를 건너뛰면 살아 있는 양이 위로 어긋난다.
        uint32 tagIdx = static_cast<uint32>( tag );
        if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
            tagIdx = 0;

        // 지금 스레드 칸에서 뺀다. 할당한 스레드의 칸과 달라 이 칸은 음수가 될 수 있지만 합은 맞다.
        MemoryTagShardCounter& counter = _shard.getCurrentThreadShard()._arrTag[tagIdx];
        counter._totalFreedBytes.fetch_add( size, std::memory_order_relaxed );
        counter._liveBytes.fetch_sub( static_cast<int64>( size ), std::memory_order_relaxed );
        counter._liveCount.fetch_sub( 1, std::memory_order_relaxed );

        // 창 안에서 태어난 블록이면 창의 값에서 뺀다. 창 밖에서 태어난 블록(스탬프 0)은 창의 값과 무관하다.
        if ( born._windowStamp != 0 )
            releaseLeakWindowBlock( pPtr, size, counter, born._windowStamp );

        // 프로파일러 안(해시 맵을 고치는 중)에서는 콜 스택 테이블을 건드리지 않는다 — 재귀로 같은 잠금을 다시 잡는다.
        if ( MemoryProfilerInternal::t_bIsInsideProfiler )
            return;

        // 세부 추적을 지금 껐어도, 할당 때 스택을 적은 블록(`callStackHash != 0`)은 표에서 뺀다. 스위치로 가르면 껐다 켠 사이의 해제가 빠져 표의 살아 있는 양이 위로 어긋난다.
        if ( callStackHash != 0 )
        {
            MemoryProfilerInternal::t_bIsInsideProfiler = true;
            {
                ScopedLock<mutex> lock{ _stackMapMutex };
                auto              it = _mapCallStackAllocInfo.find( callStackHash );
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
            // 프레임마다 한 번 부르는 곳이라 예산이 없는 태그도 합을 읽어 최고치를 올린다(최고치를 프레임마다 보는 곳).
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

    void MemoryProfiler::beginLeakWindow( LeakWindowDetail detail )
    {
        const bool bAlreadyOpen = isLeakWindowOpen();
        SW_ASSERT( bAlreadyOpen == false && "beginLeakWindow called while a leak window is already open" );
        if ( bAlreadyOpen )
            return;

        clearLeakWindowData();
        _bLeakWindowStacks.store( detail == LeakWindowDetail::FullStacks, std::memory_order_relaxed );
        _leakOwnerThreadKey.store( MemoryProfilerInternal::getCurrentThreadKey(), std::memory_order_relaxed );

        // 스탬프 0 은 "창 밖" 이라 1 부터 나눈다. 첫 스탬프를 먼저 내보내고 지금 스탬프를 내보낸다(할당이 지금 스탬프를 보면 첫 스탬프도 보인다).
        const uint32 firstStamp = _leakStampCounter.fetch_add( 1, std::memory_order_relaxed ) + 1;
        _leakFirstStamp.store( firstStamp, std::memory_order_release );
        _leakCurrentStamp.store( firstStamp, std::memory_order_release );
    }

    void MemoryProfiler::beginLeakRound()
    {
        const bool bOpen   = isLeakWindowOpen();
        const bool bStacks = _bLeakWindowStacks.load( std::memory_order_relaxed );
        SW_ASSERT( bOpen && "beginLeakRound called without an open leak window" );
        SW_ASSERT( ( bOpen == false || bStacks ) && "a TagTotals leak window has a single round (use FullStacks to split rounds)" );
        if ( bOpen == false || bStacks == false )
            return;

        const uint32 stamp = _leakStampCounter.fetch_add( 1, std::memory_order_relaxed ) + 1;
        _leakCurrentStamp.store( stamp, std::memory_order_release );
    }

    LeakWindowReport MemoryProfiler::endLeakWindow()
    {
        LeakWindowReport report;
        const uint32     firstStamp = _leakFirstStamp.load( std::memory_order_acquire );
        SW_ASSERT( firstStamp != 0 && "endLeakWindow called without an open leak window" );
        if ( firstStamp == 0 )
            return report;

        // 먼저 닫는다 — 이 뒤의 할당에는 스탬프가 찍히지 않고, 이 뒤의 해제는 창의 값을 건드리지 않는다. 지금 스탬프를 먼저 0 으로 두어야
        // 해제 쪽이 첫 스탬프만 보고 닫히지 않은 창으로 착각하지 않는다(`releaseLeakWindowBlock`).
        const uint32 lastStamp = _leakCurrentStamp.exchange( 0, std::memory_order_acq_rel );
        _leakFirstStamp.store( 0, std::memory_order_release );

        report._bValid     = true;
        report._roundCount = lastStamp - firstStamp + 1;
        if ( _bLeakWindowStacks.load( std::memory_order_relaxed ) )
        {
            report._detail = LeakWindowDetail::FullStacks;
            collectLeakWindowStacks( firstStamp, report );
        }
        else
        {
            report._detail = LeakWindowDetail::TagTotals;
            collectLeakWindowTagTotals( report );
        }

        std::sort( report._listEntry.begin(), report._listEntry.end(), Descending<&LeakWindowEntry::_bytes>{} );
        for ( const LeakWindowEntry& entry : report._listEntry )
        {
            report._totalBytes += entry._bytes;
            report._totalBlockCount += entry._blockCount;
        }
        return report;
    }

    void MemoryProfiler::addLeakWindowBlock( void* pPtr, size_t size, MemoryTag tag, uint32 stamp, const CallStack& stack )
    {
        LeakWindowShard& shard = _leakShard.getShard( MemoryProfilerInternal::getPointerShardIndex( pPtr ) );

        // 테이블이 할당할 때(노드 · 버킷)의 재귀를 막는다. 세지 않으므로 그 해제도 오지 않는다.
        MemoryProfilerInternal::t_bIsInsideProfiler = true;
        {
            ScopedLock<mutex> lock{ shard._mutex };
            shard._mapBlock[pPtr] = LeakWindowBlock{ size, stack._hash, MemoryProfilerInternal::getCurrentThreadKey(), stamp, tag };
            if ( shard._mapStack.contains( stack._hash ) == false )
                shard._mapStack.emplace( stack._hash, stack );
        }
        MemoryProfilerInternal::t_bIsInsideProfiler = false;
    }

    void MemoryProfiler::releaseLeakWindowBlock( void* pPtr, size_t size, MemoryTagShardCounter& counter, uint32 stamp )
    {
        // 열린 창 안에서 태어난 블록만 뺀다. 창 밖의 오래된 블록 · 이미 닫힌 창의 블록은 지금 창의 값과 무관하다.
        const uint32 firstStamp        = _leakFirstStamp.load( std::memory_order_acquire );
        const uint32 currentStamp      = _leakCurrentStamp.load( std::memory_order_relaxed );
        const bool   bBornInOpenWindow = firstStamp != 0 && firstStamp <= stamp && stamp <= currentStamp;
        if ( bBornInOpenWindow == false )
            return;

        if ( _bLeakWindowStacks.load( std::memory_order_relaxed ) == false )
        {
            counter._windowLiveBytes.fetch_sub( static_cast<int64>( size ), std::memory_order_relaxed );
            counter._windowLiveCount.fetch_sub( 1, std::memory_order_relaxed );
            return;
        }

        // 이미 프로파일러 안이면(테이블을 고치는 중에 센 블록이 풀리는 경우) 바깥 가드를 지우지 않도록 이전 값을 되돌린다.
        LeakWindowShard& shard                      = _leakShard.getShard( MemoryProfilerInternal::getPointerShardIndex( pPtr ) );
        const bool       bWasInside                 = MemoryProfilerInternal::t_bIsInsideProfiler;
        MemoryProfilerInternal::t_bIsInsideProfiler = true;
        {
            ScopedLock<mutex> lock{ shard._mutex };
            auto              it = shard._mapBlock.find( pPtr );
            // 같은 주소를 다른 회차 · 다른 창이 다시 받았을 수 있다 — 스탬프가 같은 블록만 지운다.
            if ( it != shard._mapBlock.end() && it->second._stamp == stamp )
                shard._mapBlock.erase( it );
        }
        MemoryProfilerInternal::t_bIsInsideProfiler = bWasInside;
    }

    void MemoryProfiler::clearLeakWindowData()
    {
        for ( uint32 shardIndex = 0; shardIndex < ShardedCounterUtil::kShardCount; ++shardIndex )
        {
            LeakWindowShard& shard = _leakShard.getShard( shardIndex );
            {
                ScopedLock<mutex> lock{ shard._mutex };
                shard._mapBlock.clear();
                shard._mapStack.clear();
            }
            for ( MemoryTagShardCounter& counter : _shard.getShard( shardIndex )._arrTag )
            {
                counter._windowLiveBytes.store( 0, std::memory_order_relaxed );
                counter._windowLiveCount.store( 0, std::memory_order_relaxed );
            }
        }
    }

    void MemoryProfiler::collectLeakWindowTagTotals( LeakWindowReport& outReport )
    {
        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            // 읽으면서 0 으로 되돌린다 — 다음 창은 빈 합계에서 시작한다.
            int64 sumBytes = 0;
            int64 sumCount = 0;
            for ( uint32 shardIndex = 0; shardIndex < ShardedCounterUtil::kShardCount; ++shardIndex )
            {
                MemoryTagShardCounter& counter = _shard.getShard( shardIndex )._arrTag[tagIndex];
                sumBytes += counter._windowLiveBytes.exchange( 0, std::memory_order_relaxed );
                sumCount += counter._windowLiveCount.exchange( 0, std::memory_order_relaxed );
            }
            const uint64 bytes = MemoryProfilerInternal::clampLiveSum( sumBytes );
            const uint64 count = MemoryProfilerInternal::clampLiveSum( sumCount );
            if ( bytes == 0 && count == 0 )
                continue;

            LeakWindowEntry entry;
            entry._tag        = static_cast<MemoryTag>( tagIndex );
            entry._bornRound  = 1;
            entry._bytes      = bytes;
            entry._blockCount = count;
            outReport._listEntry.push_back( std::move( entry ) );
        }
    }

    void MemoryProfiler::collectLeakWindowStacks( uint32 firstStamp, LeakWindowReport& outReport )
    {
        const uint32 lastStamp = firstStamp + outReport._roundCount - 1;
        const uint64 ownerKey  = _leakOwnerThreadKey.load( std::memory_order_relaxed );

        // 테이블을 도는 동안 모으는 임시 그릇은 세지 않는다(가드). 슬롯마다 잠금을 짧게 잡고 비운다.
        vector<LeakBlockSample>          listSample;
        unordered_map<uint64, CallStack> mapStack;
        MemoryProfilerInternal::t_bIsInsideProfiler = true;
        for ( uint32 shardIndex = 0; shardIndex < ShardedCounterUtil::kShardCount; ++shardIndex )
        {
            LeakWindowShard&  shard = _leakShard.getShard( shardIndex );
            ScopedLock<mutex> lock{ shard._mutex };
            for ( const auto& blockPair : shard._mapBlock )
            {
                const LeakWindowBlock& block = blockPair.second;
                // 닫히기 전에 늦게 들어온 낡은 스탬프는 이 창의 블록이 아니다.
                if ( block._stamp < firstStamp || lastStamp < block._stamp )
                    continue;
                listSample.push_back( LeakBlockSample{ block._stackHash, block._size, block._stamp - firstStamp + 1, block._tag } );
                if ( block._threadKey != ownerKey )
                {
                    ++outReport._foreignThreadBlockCount;
                    outReport._foreignThreadBytes += block._size;
                }
            }
            for ( const auto& stackPair : shard._mapStack )
            {
                if ( mapStack.contains( stackPair.first ) == false )
                    mapStack.emplace( stackPair.first, stackPair.second );
            }
            shard._mapBlock.clear();
            shard._mapStack.clear();
        }
        MemoryProfilerInternal::t_bIsInsideProfiler = false;

        std::sort( listSample.begin(), listSample.end(), Ascending<&LeakBlockSample::_tag, &LeakBlockSample::_stackHash, &LeakBlockSample::_round>{} );

        // 같은 (태그, 스택, 회차) 가 이웃해 있으니 한 줄로 합친다. 첫 프레임은 스택마다 한 번만 심볼로 바꾼다(전역 잠금을 잡는 느린 길).
        unordered_map<uint64, string> mapFirstFrame;
        for ( const LeakBlockSample& sample : listSample )
        {
            const bool bSameGroup = outReport._listEntry.empty() == false && outReport._listEntry.back()._tag == sample._tag &&
                                    outReport._listEntry.back()._stackHash == sample._stackHash && outReport._listEntry.back()._bornRound == sample._round;
            if ( bSameGroup == false )
            {
                if ( mapFirstFrame.contains( sample._stackHash ) == false )
                {
                    const auto itStack = mapStack.find( sample._stackHash );
                    mapFirstFrame.emplace( sample._stackHash, MemoryProfilerInternal::describeFirstCallerFrame( itStack != mapStack.end() ? itStack->second : CallStack{} ) );
                }
                LeakWindowEntry entry;
                entry._tag        = sample._tag;
                entry._bornRound  = sample._round;
                entry._stackHash  = sample._stackHash;
                entry._firstFrame = mapFirstFrame.find( sample._stackHash )->second;
                outReport._listEntry.push_back( std::move( entry ) );
            }
            LeakWindowEntry& group = outReport._listEntry.back();
            group._bytes += sample._size;
            ++group._blockCount;
        }
    }

    uint32 MemoryProfiler::printLeakWindowReport( const LeakWindowReport& report, const utf8* pPhaseTag )
    {
        const utf8* pPhase = StringUtil::isNullOrEmpty( pPhaseTag ) ? "shutdown" : pPhaseTag;
        if ( report._bValid == false )
        {
            // 창 없이는 살아 있는 기동 블록(부트스트랩이 할당한 블록)과 누수를 가를 수 없다 — 조용히 넘기지 않고 오류로 알린다.
            MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# - ERROR: no leak window was open, so leaks cannot be told from live startup blocks", pPhase );
            return 0;
        }
        if ( report._listEntry.empty() )
        {
            MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# - nothing born in the leak window is still alive (%# rounds).", pPhase, report._roundCount );
            return 0;
        }

        uint32 printedCount{ 0 };
        for ( const LeakWindowEntry& entry : report._listEntry )
        {
            if ( printedCount == MemoryProfilerInternal::kMaxPrintedLeakEntry )
            {
                MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# - ... %# more lines not shown.", pPhase,
                                                          static_cast<uint32>( report._listEntry.size() ) - printedCount );
                break;
            }
            ++printedCount;
            if ( report._detail == LeakWindowDetail::FullStacks )
            {
                MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# - tag %# round %# kept %# bytes in %# blocks at %#", pPhase, getMemoryTagName( entry._tag ),
                                                          entry._bornRound, entry._bytes, entry._blockCount, entry._firstFrame.c_str() );
            }
            else
            {
                MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# - tag %# grew by %# bytes in %# blocks since the post-init baseline.", pPhase,
                                                          getMemoryTagName( entry._tag ), entry._bytes, entry._blockCount );
            }
        }
        if ( report._foreignThreadBlockCount != 0 )
        {
            MemoryProfilerInternal::printLeakMessage( "[MemoryLeak] %# - %# of these blocks (%# bytes) were allocated by threads other than the one that opened the window.", pPhase,
                                                      report._foreignThreadBlockCount, report._foreignThreadBytes );
        }
        return static_cast<uint32>( report._listEntry.size() );
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
            ScopedLock<mutex> lock{ _stackMapMutex };
            entryCount = _mapCallStackAllocInfo.size();
        }
        vector<CallStackAllocInfo> listResult;
        listResult.reserve( entryCount + entryCount / 4 + 16 );

        MemoryProfilerInternal::t_bIsInsideProfiler = true;
        {
            ScopedLock<mutex> lock{ _stackMapMutex };
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
