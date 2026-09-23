#include "pch.h"

#include "Core/Concurrency/DataRaceDetector.h"

#if defined( SW_DEBUG )
    #include "Core/Container/string.h"
    #include "Core/Log/Logger.h"
    #include "Core/Process/CallStackCapture.h"

    #include <functional>
    #include <thread>

namespace sw
{
    SW_LOG_CALLER( "DataRaceDetector" );

    namespace
    {
        static constexpr uint32 kWriterBitShift = 16;
        static constexpr uint32 kCountMask      = 0xFFFF;
        static constexpr uint32 kWriterUnit     = 1u << kWriterBitShift;

        struct DataRaceDetectorInternal
        {
            /**
             * @brief 현재 스레드를 가리키는 0 이 아닌 값입니다.
             * @details 0 은 "주인 없음" 을 뜻하므로 쓸 수 없습니다. 해시가 0 으로 나오는 드문 경우에는 1 로 바꿉니다. 어떤 값이든
             *          스레드마다 다르기만 하면 되기 때문입니다.
             */
            static uint64 currentThreadId()
            {
                const uint64 id = static_cast<uint64>( std::hash<std::thread::id>{}( std::this_thread::get_id() ) );
                return ( id != 0 ) ? id : 1;
            }
        };
    } // namespace

    bool RaceDetectContext::isOwnedByCurrentThread() const
    {
        const uint64 ownerId = _ownerThreadId.load( std::memory_order_relaxed );
        return ownerId != 0 && ownerId == DataRaceDetectorInternal::currentThreadId();
    }

    /**
     * @brief 읽기 구간에 들어갑니다.
     */
    void RaceDetectContext::enterRead()
    {
        // 하위 16비트(읽기 수)를 원자적으로 1 올린다
        uint32 oldState = _state.fetch_add( 1, std::memory_order_acquire );
        uint32 writers  = oldState >> kWriterBitShift;
        uint32 readers  = oldState & kCountMask;

        if ( writers == 0 )
        {
            // 처음 들어온 스레드가 주인이 된다. 읽기끼리는 서로 보고하지 않으므로 누가 주인이어도 상관없다.
            if ( readers == 0 )
                _ownerThreadId.store( DataRaceDetectorInternal::currentThreadId(), std::memory_order_relaxed );
            return;
        }

        // 쓰는 스레드가 있다. 그 스레드가 현재 스레드 자신이면 레이스가 아니다(같은 스레드의 재진입).
        if ( isOwnedByCurrentThread() )
            return;

        triggerDataRace( "Concurrent read/write access detected! (Reader entered while Writer is active)" );
    }

    /**
     * @brief 읽기 구간에서 나옵니다.
     */
    void RaceDetectContext::exitRead()
    {
        // 마지막 하나가 나가면 주인 자리를 비운다.
        if ( _state.fetch_sub( 1, std::memory_order_release ) == 1 )
            _ownerThreadId.store( 0, std::memory_order_relaxed );
    }

    /**
     * @brief 쓰기 구간(독점)에 들어갑니다.
     */
    void RaceDetectContext::enterWrite()
    {
        // 상위 16비트(쓰기 수)를 원자적으로 1 올린다
        uint32 oldState = _state.fetch_add( kWriterUnit, std::memory_order_acquire );
        uint32 readers  = oldState & kCountMask;
        uint32 writers  = oldState >> kWriterBitShift;

        if ( writers == 0 && readers == 0 )
        {
            _ownerThreadId.store( DataRaceDetectorInternal::currentThreadId(), std::memory_order_relaxed );
            return;
        }

        // 이미 누가 들어와 있다. 그게 현재 스레드 자신이면 레이스가 아니다(같은 스레드의 재진입).
        // 쓰기 가드를 잡은 채 같은 객체의 다른 가드 메서드를 부르는 경우가 그렇다.
        if ( isOwnedByCurrentThread() )
            return;

        // 다른 쓰기가 이미 진행 중이면 쓰기/쓰기 충돌
        if ( writers > 0 )
            triggerDataRace( "Concurrent write/write access detected! (Writer entered while another Writer is active)" );
        // 읽기가 진행 중이면 읽기/쓰기 충돌
        else
            triggerDataRace( "Concurrent read/write access detected! (Writer entered while Reader is active)" );
    }

    /**
     * @brief 쓰기 구간에서 나옵니다.
     */
    void RaceDetectContext::exitWrite()
    {
        if ( _state.fetch_sub( kWriterUnit, std::memory_order_release ) == kWriterUnit )
            _ownerThreadId.store( 0, std::memory_order_relaxed );
    }

    /**
     * @brief 레이스를 찾았을 때 콜 스택을 캡처하고 디버거에서 멈춥니다.
     */
    void RaceDetectContext::triggerDataRace( const utf8* pMessage )
    {
        uint32    state   = _state.load( std::memory_order_relaxed );
        uint32    readers = state & kCountMask;
        uint32    writers = ( state >> kWriterBitShift ) & kCountMask;
        CallStack callStack;
        CallStackCapture::capture( callStack, 1 );
        string stackTrace = CallStackCapture::symbolize( callStack );

        SW_LOG_ERROR( "%s (ctx: %p, readers: %u, writers: %u)", pMessage, this, readers, writers );

        size_t start{ 0 };
        while ( start < stackTrace.size() )
        {
            size_t end = stackTrace.find( '\n', start );
            if ( end == string::npos )
                end = stackTrace.size();
            string line = stackTrace.substr( start, end - start );
            if ( line.empty() == false )
                SW_LOG_ERROR( "  %s", line.c_str() );
            start = end + 1;
        }

        SW_DEBUG_BREAK();
    }
} // namespace sw

#endif
