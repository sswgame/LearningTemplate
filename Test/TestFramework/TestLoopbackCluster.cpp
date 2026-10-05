#include "pch.h"

#include "TestFramework/TestLoopbackCluster.h"

namespace test
{
    LoopbackCluster::LoopbackCluster( uint32 seed )
        : _network{}
        , _listEmulation{}
        , _listHost{}
        , _listThread{}
        , _time{ 0.0 }
        , _seed{ seed }
    {
    }

    LoopbackCluster::~LoopbackCluster() { stopThreads(); }

    int32 LoopbackCluster::addHost( uint16 port, const sw::NetHostSettings& settings )
    {
        SW_ASSERT( _listThread.empty() && "LoopbackCluster::addHost called while host threads run" );
        sw::LoopbackTransport* pEndpoint = _network.createEndpoint( port );
        if ( pEndpoint == nullptr )
        {
            SW_LOG_ERROR( "LoopbackCluster: port %# is taken", port );
            return -1;
        }
        const int32 hostIndex = getHostCount();
        _listEmulation.emplace_back( pEndpoint, _seed + static_cast<uint32>( hostIndex ) );
        _listHost.emplace_back();
        _listHost.back().initialize( &_listEmulation.back(), settings );
        return hostIndex;
    }

    bool LoopbackCluster::connectClients( int32 stepCount, float64 deltaTime )
    {
        if ( _listHost.empty() || _listHost.front().listen() == false )
            return false;
        const sw::NetAddress serverAddress = _listEmulation.front().getLocalAddress();
        bool                 bStarted      = true;
        for ( size_t index = 1; index < _listHost.size(); ++index )
        {
            bStarted = _listHost[index].connect( serverAddress ) && bStarted;
        }
        for ( int32 index = 0; index < stepCount; ++index )
        {
            step( deltaTime );
        }
        return bStarted && areClientsConnected();
    }

    bool LoopbackCluster::startThreads( const sw::NetHostThreadSettings& settings )
    {
        SW_ASSERT( _listThread.empty() && "LoopbackCluster::startThreads called twice" );
        bool bStarted = true;
        for ( sw::NetHost& host : _listHost )
        {
            _listThread.emplace_back();
            bStarted = _listThread.back().start( &host, settings ) && bStarted;
        }
        return bStarted;
    }

    void LoopbackCluster::stopThreads()
    {
        for ( sw::NetHostThread& thread : _listThread )
        {
            thread.stop();
        }
        _listThread.clear();
    }

    void LoopbackCluster::step( float64 deltaTime )
    {
        SW_ASSERT( _listThread.empty() && "LoopbackCluster::step called while host threads run" );
        _time += deltaTime;
        // 흉내 줄을 먼저 모두 비운다 — 이 시각까지 닿을 패킷이 어느 호스트가 받기 전에 배달된다.
        for ( sw::NetEmulationTransport& emulation : _listEmulation )
        {
            emulation.update( _time );
        }
        for ( sw::NetHost& host : _listHost )
        {
            host.update( _time );
        }
    }

    void LoopbackCluster::run( float64 seconds, float64 deltaTime )
    {
        for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += deltaTime )
        {
            step( deltaTime );
        }
    }

    void LoopbackCluster::setConditions( const sw::NetEmulationConditions& conditions )
    {
        for ( sw::NetEmulationTransport& emulation : _listEmulation )
        {
            emulation.setDefaultConditions( conditions );
        }
    }

    sw::NetHost& LoopbackCluster::getHost( int32 hostIndex )
    {
        SW_ASSERT( 0 <= hostIndex && hostIndex < getHostCount() && "LoopbackCluster: no such host" );
        return _listHost[static_cast<size_t>( hostIndex )];
    }

    sw::NetHostThread& LoopbackCluster::getThread( int32 hostIndex )
    {
        SW_ASSERT( 0 <= hostIndex && hostIndex < static_cast<int32>( _listThread.size() ) && "LoopbackCluster: no such host thread" );
        return _listThread[static_cast<size_t>( hostIndex )];
    }

    bool LoopbackCluster::areClientsConnected() const
    {
        for ( size_t index = 1; index < _listHost.size(); ++index )
        {
            if ( _listHost[index].getConnectionState( 0 ) != sw::NetConnectionState::Connected )
                return false;
        }
        return true;
    }
} // namespace test
