#include "pch.h"

#include "Core/Network/Transport/DelayedStreamTransport.h"

#include "Core/Common/Defines.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Time/MonotonicClock.h"

#include <chrono>
#include <mutex>

namespace sw
{
    DelayedStreamTransport::DelayedStreamTransport( unique_ptr<IStreamTransport> pInner, float64 sendDelaySeconds )
        : _pInner{ std::move( pInner ) }
        , _delayNanoseconds{ sendDelaySeconds > 0.0 ? static_cast<int64>( sendDelaySeconds * static_cast<float64>( constant::kNanosecondsPerSecond ) ) : 0 }
        , _mutex{}
        , _listItem{}
        , _bStopping{ SW_FALSE }
        , _timerThread{}
    {
    }

    DelayedStreamTransport::~DelayedStreamTransport()
    {
        shutdown();
    }

    bool DelayedStreamTransport::initialize( IStreamHandler* pHandler, const StreamTransportConfig& settings )
    {
        if ( _pInner == nullptr || _timerThread.joinable() )
            return false;
        if ( _pInner->initialize( pHandler, settings ) == false )
            return false;
        _bStopping.store( SW_FALSE, std::memory_order_release );
        _timerThread = sw::thread( &DelayedStreamTransport::runTimer, this );
        return true;
    }

    void DelayedStreamTransport::shutdown()
    {
        if ( _timerThread.joinable() )
        {
            _bStopping.store( SW_TRUE, std::memory_order_release );
            _timerThread.join();
        }
        {
            // 아직 지연 중인 보내기는 버린다 — 전송을 내리면 연결이 모두 닫힌다.
            std::scoped_lock<mutex> lock{ _mutex };
            _listItem.clear();
        }
        if ( _pInner != nullptr )
            _pInner->shutdown();
    }

    StreamSendResult DelayedStreamTransport::send( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        if ( size <= 0 || handle.isValid() == false )
            return StreamSendResult::Closed;
        DelayedItem item;
        item._dueNanoseconds = MonotonicClock::nowNanoseconds() + _delayNanoseconds;
        item._handle         = handle;
        item._bytes.assign( pData, pData + size );
        std::scoped_lock<mutex> lock{ _mutex };
        _listItem.push_back( std::move( item ) );
        return StreamSendResult::Queued; // 한계: 안쪽 줄의 물금 · 가득 참은 지연이 끝난 뒤에야 안다(머리말)
    }

    void DelayedStreamTransport::close( StreamConnectionHandle handle, StreamCloseMode mode )
    {
        // 지연 중인 바이트를 앞질러 닫지 않게 같은 줄을 지난다.
        DelayedItem item;
        item._dueNanoseconds = MonotonicClock::nowNanoseconds() + _delayNanoseconds;
        item._handle         = handle;
        item._closeMode      = mode;
        item._bClose         = SW_TRUE;
        std::scoped_lock<mutex> lock{ _mutex };
        _listItem.push_back( std::move( item ) );
    }

    void DelayedStreamTransport::runTimer()
    {
        ThreadName::setCurrentThreadName( "DelayedStream" );
        while ( _bStopping.load( std::memory_order_acquire ) == false )
        {
            // 때가 된 것을 앞에서부터 꺼내 안쪽에 넘긴다(잠금 밖에서). 한 번 돌 때 1 ms 안쪽 해상도로 잔다 — 측정 도구라 바쁘게 돌지 않는다.
            const int64 nowNanoseconds = MonotonicClock::nowNanoseconds();
            for ( ;; )
            {
                DelayedItem item;
                {
                    std::scoped_lock<mutex> lock{ _mutex };
                    if ( _listItem.empty() || _listItem.front()._dueNanoseconds > nowNanoseconds )
                        break;
                    item = std::move( _listItem.front() );
                    _listItem.pop_front();
                }
                if ( item._bClose == SW_TRUE )
                    _pInner->close( item._handle, item._closeMode );
                else
                    (void)_pInner->send( item._handle, item._bytes.data(), static_cast<int32>( item._bytes.size() ) ); // 실패는 안쪽이 연결을 닫아 알린다
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
    }
} // namespace sw
