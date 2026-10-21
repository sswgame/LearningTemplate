#include "pch.h"

#include "Core/Diagnostics/ProfilerTimeline.h"

#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Container/CompareUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetFlowLog.h"

#include <thread>

namespace sw
{
    namespace
    {
        struct ProfilerTimelineInternal
        {
            /** @brief 사건 칸의 슬롯 · 깊이를 한 칸에 담을 때 깊이를 올리는 비트 수입니다. */
            static constexpr uint32 kDepthShift = 32;

            /** @brief 타임라인마다 고유 번호를 나눠 줍니다(0 은 "캐시 없음"). */
            static uint32 allocateGeneration()
            {
                static atomic<uint32> s_nextGeneration{ 1 };
                return s_nextGeneration.fetch_add( 1, std::memory_order_relaxed );
            }
        };

        /** @brief 이 스레드가 마지막으로 쓴 타임라인의 버퍼입니다. 세대가 다르면(다른 타임라인) 다시 찾는다. */
        struct ThreadBufferCache
        {
            uint32 _generation{ 0 };
            void*  _pBuffer{ nullptr };
        };
        thread_local ThreadBufferCache t_threadBufferCache{};
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 스레드 하나의 사건 링입니다. 칸은 relaxed 원자라 덮이는 중에 읽어도 레이스가 아닙니다. */
    struct ProfilerTimeline::ThreadBuffer
    {
        atomic<uint64> _writeCount{ 0 };                 ///< 지금까지 쓴 사건 수(쓰는 스레드만 올린다, release)
        atomic<uint64> _arrBeginNanos[kEventCapacity]{}; ///< 사건 시작
        atomic<uint64> _arrEndNanos[kEventCapacity]{};   ///< 사건 끝
        atomic<uint64> _arrSlotDepth[kEventCapacity]{};  ///< 슬롯(아래 32 비트) · 깊이(위 32 비트)
#if SW_PROFILER_TRACE_ENABLED
        vector<ProfilerTimelineEvent> _listLong;           ///< 긴 캡처의 사건(쓰는 스레드만 덧붙인다 — 읽기는 캡처를 멈춘 뒤)
        uint32                        _longEpoch{ 0 };     ///< `_listLong` 이 속한 캡처 번호
        atomic<bool>                  _bLongBusy{ false }; ///< 덧붙이는 중 — 멈추는 쪽이 끝나기를 기다린다
#endif
        string          _name;        ///< OS 스레드 이름 또는 "Thread <번호>"(만들 때 한 번 정한다)
        std::thread::id _ownerThread; ///< 쓰는 스레드 — 스레드 지역 캐시가 빗나갔을 때 같은 스레드의 버퍼를 다시 찾는다
    };

    ProfilerTimeline::ProfilerTimeline()
        : _arrThreadBuffer{}
        , _arrFrameBeginNanos{}
        , _frameWriteCount{ 0 }
        , _threadCount{ 0 }
        , _generation{ ProfilerTimelineInternal::allocateGeneration() }
        , _bRecording{ false }
#if SW_PROFILER_TRACE_ENABLED
        , _longLimit{ 0 }
        , _longEpoch{ 0 }
        , _bLongTruncated{ false }
        , _bLongPrevRecording{ false }
        , _bLongFrameBusy{ false }
#endif
    {
    }

    ProfilerTimeline::~ProfilerTimeline()
    {
        setRecording( false ); // 흐름 링의 기록 수를 되돌린다
        for ( atomic<ThreadBuffer*>& slot : _arrThreadBuffer )
        {
            sw_delete( slot.exchange( nullptr, std::memory_order_acq_rel ) );
        }
    }

    ProfilerTimeline::ThreadBuffer* ProfilerTimeline::ensureThreadBuffer()
    {
        ThreadBufferCache& cache = t_threadBufferCache;
        if ( cache._generation == _generation )
            return static_cast<ThreadBuffer*>( cache._pBuffer );

        // 캐시가 다른 타임라인을 가리킨다. 이 스레드의 버퍼가 이미 있으면 그것을 쓴다(스레드 하나가 타임라인 둘을 오가도 슬롯이 늘지 않게).
        cache._generation                   = _generation;
        cache._pBuffer                      = nullptr;
        const std::thread::id currentThread = std::this_thread::get_id();
        const uint32          registered    = MathUtil::min( _threadCount.load( std::memory_order_acquire ), kMaxThread );
        for ( uint32 bufferIndex = 0; bufferIndex < registered; ++bufferIndex )
        {
            ThreadBuffer* pExisting = _arrThreadBuffer[bufferIndex].load( std::memory_order_acquire );
            if ( pExisting != nullptr && pExisting->_ownerThread == currentThread )
            {
                cache._pBuffer = pExisting;
                return pExisting;
            }
        }

        // 처음 기록하는 스레드다. 슬롯을 하나 잡고 버퍼를 만든다. 슬롯이 없으면 이 스레드는 기록하지 않는다(캐시에 nullptr 을 남겨 다시 찾지 않는다).
        const uint32 index = _threadCount.fetch_add( 1, std::memory_order_acq_rel );
        if ( index >= kMaxThread )
        {
            _threadCount.store( kMaxThread, std::memory_order_release );
            return nullptr;
        }
        ThreadBuffer* pBuffer = sw_new ThreadBuffer();
        pBuffer->_ownerThread = currentThread;
        utf8 arrName[constant::kMaxBuffer64]{};
        if ( ThreadName::tryGetCurrentThreadName( arrName, constant::kMaxBuffer64 ) )
            pBuffer->_name = arrName;
        else
            pBuffer->_name = string( "Thread " ) + to_string( index );
        _arrThreadBuffer[index].store( pBuffer, std::memory_order_release );
        cache._pBuffer = pBuffer;
        return pBuffer;
    }

    void ProfilerTimeline::setRecording( bool bRecording )
    {
        const bool bWasRecording = _bRecording.exchange( bRecording, std::memory_order_relaxed );
#if SW_NET_FLOW_LOG_ENABLED
        // 타임라인이 기록하는 동안 네트워크 흐름도 남긴다 — 트레이스 저장이 같은 구간의 흐름을 함께 쓴다(`NetFlowLog`).
        if ( bWasRecording != bRecording )
        {
            if ( bRecording )
                NetFlowLog::addRecorder();
            else
                NetFlowLog::removeRecorder();
        }
#else
        (void)bWasRecording; // 흐름 링은 트레이스 내보내기가 있는 구성에만 있다
#endif
    }

    void ProfilerTimeline::recordEvent( uint32 slot, uint64 beginNanos, uint64 endNanos, uint16 depth )
    {
        if ( isRecording() == false )
            return;
        ThreadBuffer* pBuffer = ensureThreadBuffer();
        if ( pBuffer == nullptr )
            return;
        const uint64 writeIndex = pBuffer->_writeCount.load( std::memory_order_relaxed );
        const uint32 cell       = static_cast<uint32>( writeIndex % kEventCapacity );
        pBuffer->_arrBeginNanos[cell].store( beginNanos, std::memory_order_relaxed );
        pBuffer->_arrEndNanos[cell].store( endNanos, std::memory_order_relaxed );
        pBuffer->_arrSlotDepth[cell].store( static_cast<uint64>( slot ) | ( static_cast<uint64>( depth ) << ProfilerTimelineInternal::kDepthShift ),
                                            std::memory_order_relaxed );
        pBuffer->_writeCount.store( writeIndex + 1, std::memory_order_release );

#if SW_PROFILER_TRACE_ENABLED
        if ( _longLimit.load( std::memory_order_relaxed ) != 0 )
        {
            // 멈추는 쪽과 맞물린다: 바쁨 표시를 먼저 올리고 상한을 다시 읽는다(seq_cst) — 멈춘 뒤에는 덧붙이지 않거나, 멈추는 쪽이 끝나기를 기다린다.
            pBuffer->_bLongBusy.store( true, std::memory_order_seq_cst );
            const uint32 limit = _longLimit.load( std::memory_order_seq_cst );
            if ( limit != 0 )
            {
                const uint32 epoch = _longEpoch.load( std::memory_order_relaxed );
                if ( pBuffer->_longEpoch != epoch )
                {
                    pBuffer->_listLong.clear();
                    pBuffer->_longEpoch = epoch;
                    pBuffer->_listLong.reserve( MathUtil::min<uint32>( limit, 65536 ) );
                }
                if ( pBuffer->_listLong.size() < limit )
                {
                    ProfilerTimelineEvent event;
                    event._beginNanos = beginNanos;
                    event._endNanos   = endNanos;
                    event._slot       = slot;
                    event._depth      = depth;
                    pBuffer->_listLong.push_back( event );
                }
                else
                {
                    _bLongTruncated.store( true, std::memory_order_relaxed );
                }
            }
            pBuffer->_bLongBusy.store( false, std::memory_order_release );
        }
#endif
    }

    void ProfilerTimeline::recordFrameBegin( uint64 nanos )
    {
        if ( isRecording() == false )
            return;
        const uint64 writeIndex = _frameWriteCount.load( std::memory_order_relaxed );
        _arrFrameBeginNanos[writeIndex % kFrameCapacity].store( nanos, std::memory_order_relaxed );
        _frameWriteCount.store( writeIndex + 1, std::memory_order_release );

#if SW_PROFILER_TRACE_ENABLED
        if ( _longLimit.load( std::memory_order_relaxed ) != 0 )
        {
            _bLongFrameBusy.store( true, std::memory_order_seq_cst );
            const uint32 limit = _longLimit.load( std::memory_order_seq_cst );
            if ( limit != 0 )
            {
                if ( _listLongFrameBegin.size() < limit )
                    _listLongFrameBegin.push_back( nanos );
                else
                    _bLongTruncated.store( true, std::memory_order_relaxed );
            }
            _bLongFrameBusy.store( false, std::memory_order_release );
        }
#endif
    }

#if SW_PROFILER_TRACE_ENABLED
    void ProfilerTimeline::beginLongCapture( uint32 maxEventsPerThread )
    {
        endLongCapture(); // 진행 중이던 것을 먼저 닫아 쓰는 쪽이 모두 멈춘 뒤에 비운다
        _listLongFrameBegin.clear();
        _bLongTruncated.store( false, std::memory_order_relaxed );
        _bLongPrevRecording.store( isRecording(), std::memory_order_relaxed );
        _longEpoch.fetch_add( 1, std::memory_order_relaxed );
        setRecording( true );
        _longLimit.store( maxEventsPerThread == 0 ? 1 : maxEventsPerThread, std::memory_order_seq_cst );
    }

    void ProfilerTimeline::endLongCapture()
    {
        if ( _longLimit.exchange( 0, std::memory_order_seq_cst ) == 0 )
            return;
        // 덧붙이는 중인 쓰기가 끝나기를 기다린다(바쁨 표시는 상한을 읽기 전에 올라간다).
        const uint32 threadCount = MathUtil::min( _threadCount.load( std::memory_order_acquire ), kMaxThread );
        for ( uint32 threadIndex = 0; threadIndex < threadCount; ++threadIndex )
        {
            ThreadBuffer* pBuffer = _arrThreadBuffer[threadIndex].load( std::memory_order_acquire );
            while ( pBuffer != nullptr && pBuffer->_bLongBusy.load( std::memory_order_acquire ) )
            {
                std::this_thread::yield();
            }
        }
        while ( _bLongFrameBusy.load( std::memory_order_acquire ) )
        {
            std::this_thread::yield();
        }
        setRecording( _bLongPrevRecording.load( std::memory_order_relaxed ) );
    }

    bool ProfilerTimeline::collectLongCapture( vector<ProfilerTimelineThread>& outListThread, vector<uint64>& outListFrameBegin,
                                               bool& outTruncated ) const
    {
        outListThread.clear();
        outListFrameBegin        = _listLongFrameBegin;
        outTruncated             = _bLongTruncated.load( std::memory_order_relaxed );
        const uint32 epoch       = _longEpoch.load( std::memory_order_relaxed );
        const uint32 threadCount = MathUtil::min( _threadCount.load( std::memory_order_acquire ), kMaxThread );
        for ( uint32 threadIndex = 0; threadIndex < threadCount; ++threadIndex )
        {
            const ThreadBuffer* pBuffer           = _arrThreadBuffer[threadIndex].load( std::memory_order_acquire );
            const bool          bSkipThreadBuffer = pBuffer == nullptr || pBuffer->_longEpoch != epoch || pBuffer->_listLong.empty();
            if ( bSkipThreadBuffer )
                continue;
            ProfilerTimelineThread thread;
            thread._name      = pBuffer->_name;
            thread._listEvent = pBuffer->_listLong;
            std::stable_sort( thread._listEvent.begin(), thread._listEvent.end(), Ascending<&ProfilerTimelineEvent::_beginNanos>{} );
            outListThread.push_back( std::move( thread ) );
        }
        return outListThread.empty() == false;
    }
#endif

    bool ProfilerTimeline::collectRecentFrames( uint32 frameCount, vector<ProfilerTimelineThread>& outListThread, uint64& outBeginNanos,
                                                uint64& outEndNanos ) const
    {
        outListThread.clear();
        outBeginNanos = 0;
        outEndNanos   = 0;

        // 구간 = 끝난 프레임 N 개. 마지막 프레임 시작(지금 도는 프레임)이 끝이고, 그 N 개 앞의 시작이 처음이다.
        const uint64 frameWritten = _frameWriteCount.load( std::memory_order_acquire );
        if ( frameWritten < 2 )
            return false;
        const uint64 available = MathUtil::min<uint64>( frameWritten, kFrameCapacity ) - 1;
        const uint64 span      = MathUtil::clamp<uint64>( frameCount, 1, available );
        const uint64 lastIndex = frameWritten - 1;
        outEndNanos            = _arrFrameBeginNanos[lastIndex % kFrameCapacity].load( std::memory_order_relaxed );
        outBeginNanos          = _arrFrameBeginNanos[( lastIndex - span ) % kFrameCapacity].load( std::memory_order_relaxed );

        const uint32 threadCount = MathUtil::min( _threadCount.load( std::memory_order_acquire ), kMaxThread );
        for ( uint32 threadIndex = 0; threadIndex < threadCount; ++threadIndex )
        {
            const ThreadBuffer* pBuffer = _arrThreadBuffer[threadIndex].load( std::memory_order_acquire );
            if ( pBuffer == nullptr )
                continue;
            const uint64 written = pBuffer->_writeCount.load( std::memory_order_acquire );
            const uint64 first   = written > kEventCapacity ? written - kEventCapacity : 0;

            ProfilerTimelineThread thread;
            thread._name = pBuffer->_name;
            vector<uint64> listIndex; // 담은 사건의 쓰기 번호 — 복사하는 동안 덮인 것을 아래에서 버린다
            for ( uint64 eventIndex = first; eventIndex < written; ++eventIndex )
            {
                const uint32          cell = static_cast<uint32>( eventIndex % kEventCapacity );
                ProfilerTimelineEvent event;
                event._beginNanos      = pBuffer->_arrBeginNanos[cell].load( std::memory_order_relaxed );
                event._endNanos        = pBuffer->_arrEndNanos[cell].load( std::memory_order_relaxed );
                const uint64 slotDepth = pBuffer->_arrSlotDepth[cell].load( std::memory_order_relaxed );
                event._slot            = static_cast<uint32>( slotDepth & 0xFFFFFFFFull );
                event._depth           = static_cast<uint16>( slotDepth >> ProfilerTimelineInternal::kDepthShift );
                const bool bOverlaps   = event._beginNanos < outEndNanos && event._endNanos > outBeginNanos;
                if ( bOverlaps == false )
                    continue;
                thread._listEvent.push_back( event );
                listIndex.push_back( eventIndex );
            }
            // 복사하는 동안 쓰는 스레드가 앞지른 항목은 다른 사건으로 바뀌었을 수 있다. 지금 쓰는 중일 수 있는 항목(다음 번호)까지 버린다.
            const uint64 writtenAfter = pBuffer->_writeCount.load( std::memory_order_acquire );
            const uint64 validFirst   = ( writtenAfter + 1 > kEventCapacity ) ? writtenAfter + 1 - kEventCapacity : 0;
            if ( validFirst > first )
            {
                size_t keep = 0;
                for ( size_t eventPos = 0; eventPos < thread._listEvent.size(); ++eventPos )
                {
                    if ( listIndex[eventPos] >= validFirst )
                        thread._listEvent[keep++] = thread._listEvent[eventPos];
                }
                thread._listEvent.resize( keep );
            }
            if ( thread._listEvent.empty() )
                continue;
            std::stable_sort( thread._listEvent.begin(), thread._listEvent.end(), Ascending<&ProfilerTimelineEvent::_beginNanos>{} );
            outListThread.push_back( std::move( thread ) );
        }
        return true;
    }

    void ProfilerTimeline::collectFrameBegins( uint64 beginNanos, uint64 endNanos, vector<uint64>& outListFrameBegin ) const
    {
        outListFrameBegin.clear();
        const uint64 frameWritten = _frameWriteCount.load( std::memory_order_acquire );
        const uint64 first        = frameWritten > kFrameCapacity ? frameWritten - kFrameCapacity : 0;
        for ( uint64 frameIndex = first; frameIndex < frameWritten; ++frameIndex )
        {
            const uint64 nanos = _arrFrameBeginNanos[frameIndex % kFrameCapacity].load( std::memory_order_relaxed );
            if ( beginNanos <= nanos && nanos <= endNanos )
                outListFrameBegin.push_back( nanos );
        }
    }

    uint32 ProfilerTimeline::countActiveThreads( uint32 frameCount ) const
    {
        vector<ProfilerTimelineThread> listThread;
        uint64                         beginNanos{ 0 };
        uint64                         endNanos{ 0 };
        if ( collectRecentFrames( frameCount, listThread, beginNanos, endNanos ) == false )
            return 0;
        return static_cast<uint32>( listThread.size() );
    }

    void ProfilerTimeline::clear()
    {
        const uint32 threadCount = MathUtil::min( _threadCount.load( std::memory_order_acquire ), kMaxThread );
        for ( uint32 threadIndex = 0; threadIndex < threadCount; ++threadIndex )
        {
            ThreadBuffer* pBuffer = _arrThreadBuffer[threadIndex].load( std::memory_order_acquire );
            if ( pBuffer != nullptr )
                pBuffer->_writeCount.store( 0, std::memory_order_release );
        }
        _frameWriteCount.store( 0, std::memory_order_release );
    }
} // namespace sw
