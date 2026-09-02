#include "pch.h"

#include "Core/Memory/MemoryProfiler.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/String/formatString.h"

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_DEBUG ) && !defined( SW_SHIPPING )
    #define SW_HAS_CRT_LEAK_CHECK 1
#elif ( defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS ) ) && defined( SW_DEBUG ) && !defined( SW_SHIPPING )
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
            static void printLeakMessage( const utf8* pFormat, Args&&... args )
            {
                utf8 arrBuf[constant::kMaxBuffer1024]{};
                formatstring( arrBuf, static_cast<uint32>( sizeof( arrBuf ) ), pFormat, std::forward<Args>( args )... );
                std::fputs( arrBuf, stderr );
                std::fputc( '\n', stderr );
            }

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
        switch ( tag )
        {
            case MemoryTag::Unknown:
                return "Unknown";
            case MemoryTag::Core:
                return "Core";
            case MemoryTag::Engine:
                return "Engine";
            case MemoryTag::Graphics:
                return "Graphics";
            case MemoryTag::Physics:
                return "Physics";
            case MemoryTag::Audio:
                return "Audio";
            case MemoryTag::Game:
                return "Game";
            case MemoryTag::Editor:
                return "Editor";
            case MemoryTag::MaxTags:
                return "MaxTags";
            default:
                return "Invalid";
        }
    }

    void MemoryProfiler::setCurrentMemoryTag( MemoryTag tag )
    {
        MemoryProfilerInternal::t_currentMemoryTag = tag;
    }

    MemoryTag MemoryProfiler::getCurrentMemoryTag()
    {
        return MemoryProfilerInternal::t_currentMemoryTag;
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
        const utf8* pPhase = ( pPhaseTag != nullptr && pPhaseTag[0] != '\0' ) ? pPhaseTag : "shutdown";

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

        // 훅이 더 이상 이 인스턴스를 보지 않게 먼저 해제합니다.
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
            return 0; // 방어 로직: 프로파일러 내부에서 해시 맵 할당 시 재귀 방지

        uint32 tagIdx = static_cast<uint32>( tag );
        if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
            tagIdx = 0;

        _arrStat[tagIdx]._totalAllocatedBytes.fetch_add( size, std::memory_order_relaxed );
        _arrStat[tagIdx]._currentAllocatedBytes.fetch_add( size, std::memory_order_relaxed );
        _arrStat[tagIdx]._currentAllocationCount.fetch_add( 1, std::memory_order_relaxed );

        uint64 outHash{ 0 };

        if ( _bDetailedTrackingEnabled.load( std::memory_order_relaxed ) )
        {
            CallStack stack;
            // skipFrames: capture(0), recordAllocation(1), operator new(2) — 상위 2프레임을 건너뜁니다.
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
            return; // 방어 로직: 프로파일러 내부에서 해시 맵 노드 해제 시 재귀 방지

        uint32 tagIdx = static_cast<uint32>( tag );
        if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
            tagIdx = 0;

        _arrStat[tagIdx]._totalFreedBytes.fetch_add( size, std::memory_order_relaxed );
        _arrStat[tagIdx]._currentAllocatedBytes.fetch_sub( size, std::memory_order_relaxed );
        _arrStat[tagIdx]._currentAllocationCount.fetch_sub( 1, std::memory_order_relaxed );

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

    vector<CallStackAllocInfo> MemoryProfiler::getTopCallStacks() const
    {
        vector<CallStackAllocInfo> listResult;
        MemoryProfilerInternal::t_bIsInsideProfiler = true;
        {
            std::scoped_lock<mutex> lock{ _stackMapMutex };
            listResult.reserve( _mapCallStackAllocInfo.size() );
            for ( const auto& [hash, info] : _mapCallStackAllocInfo )
            {
                if ( info._currentBytes > 0 )
                    listResult.push_back( info );
            }
        }
        MemoryProfilerInternal::t_bIsInsideProfiler = false;

        std::sort( listResult.begin(), listResult.end(), []( const CallStackAllocInfo& infoA, const CallStackAllocInfo& infoB )
        { return infoA._currentBytes > infoB._currentBytes; } );

        return listResult;
    }
} // namespace sw
