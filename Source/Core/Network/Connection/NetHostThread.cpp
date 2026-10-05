#include "pch.h"

#include "Core/Network/Connection/NetHostThread.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    NetHostThread::NetHostThread()
        : _thread{}
        , _pHost{ nullptr }
        , _settings{}
        , _updateCount{ 0 }
        , _bStopRequested{ false }
        , _bRunning{ false }
    {
    }

    NetHostThread::~NetHostThread() { stop(); }

    float64 NetHostThread::getTime() { return static_cast<float64>( MonotonicClock::nowNanoseconds() ) * 1.0e-9; }

    bool NetHostThread::start( NetHost* pHost, const NetHostThreadSettings& settings )
    {
        if ( pHost == nullptr || _thread.joinable() )
            return false;
        _pHost    = pHost;
        _settings = settings;
        _bStopRequested.store( false, std::memory_order_release );
        _bRunning.store( true, std::memory_order_release );
        _thread = std::thread( &NetHostThread::run, this );
        return true;
    }

    void NetHostThread::stop()
    {
        if ( _thread.joinable() == false )
            return;
        _bStopRequested.store( true, std::memory_order_release );
        _thread.join();
        _bRunning.store( false, std::memory_order_release );
    }

    void NetHostThread::run()
    {
        ThreadName::setCurrentThreadName( "Net" );
        const float64 maxWait = _settings._maxWaitSeconds > 0.0f ? static_cast<float64>( _settings._maxWaitSeconds ) : 0.0;
        while ( _bStopRequested.load( std::memory_order_acquire ) == false )
        {
            _pHost->update( getTime() );
            _updateCount.fetch_add( 1, std::memory_order_relaxed );
            // 받을 것이 오면 바로 깨고(UDP poll), 없으면 최대 대기 뒤에 보낼 시각 · 타임아웃을 보러 돈다.
            (void)_pHost->waitForReceive( maxWait );
        }
    }
} // namespace sw
