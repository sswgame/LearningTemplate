#include "pch.h"

#include "Core/Network/Connection/NetEndpointThread.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/thread.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    NetEndpointThread::NetEndpointThread()
        : _thread{}
        , _pHost{ nullptr }
        , _settings{}
        , _updateCount{ 0 }
        , _bRunning{ SW_FALSE }
    {
    }

    NetEndpointThread::~NetEndpointThread()
    {
        stop();
    }

    float64 NetEndpointThread::getTime()
    {
        return static_cast<float64>( MonotonicClock::nowNanoseconds() ) * 1.0e-9;
    }

    bool NetEndpointThread::start( NetEndpoint* pHost, const NetEndpointThreadConfig& settings )
    {
        if ( pHost == nullptr || _thread.joinable() )
            return false;
        _pHost    = pHost;
        _settings = settings;
        _bRunning.store( true, std::memory_order_release );
        _thread = sw::thread( ThreadCreationKey{}, &NetEndpointThread::run, this );
        return true;
    }

    void NetEndpointThread::stop()
    {
        if ( _thread.joinable() == false )
            return;
        _thread.request_stop();
        _pHost->wake(); // 다음 기한까지 자는 중일 수 있다
        _thread.join();
        _bRunning.store( false, std::memory_order_release );
    }

    void NetEndpointThread::run( StopToken stopToken )
    {
        ThreadName::setCurrentThreadName( "Net" );
        _pHost->bindUpdateThread();
        const float64 maxWait = _settings._maxWaitSeconds > 0.0f ? static_cast<float64>( _settings._maxWaitSeconds ) : 0.0;
        while ( stopToken.isStopRequested() == false )
        {
            _pHost->update( getTime() );
            _updateCount.fetch_add( 1, std::memory_order_relaxed );
            // 받을 것이 오거나(UDP poll) 다음 기한(재전송 · 타임아웃 · 유지)이 되거나 다른 스레드가 보낼 것을 넣어 깨울 때까지 잔다.
            (void)_pHost->waitForReceive( getTime(), maxWait );
        }
        _pHost->releaseUpdateThread();
    }
} // namespace sw
