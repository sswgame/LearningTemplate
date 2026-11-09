#include "pch.h"

#include "Core/Network/Tests/LoopbackCluster.h"

namespace sw::test
{
    LoopbackCluster::LoopbackCluster( uint32 seed )
        : _network{}
        , _listEmulation{}
        , _listEndpoint{}
        , _listThread{}
        , _time{ 0.0 }
        , _seed{ seed }
    {
    }

    LoopbackCluster::~LoopbackCluster()
    {
        stopThreads();
    }

    optional<int32> LoopbackCluster::addEndpoint( uint16 port, const NetEndpointConfig& settings )
    {
        SW_ASSERT( _listThread.empty() && "LoopbackCluster::addEndpoint called while endpoint threads run" );
        LoopbackTransport* pTransport = _network.createEndpoint( port );
        if ( pTransport == nullptr )
        {
            SW_LOG_ERROR_ID( "LoopbackCluster.PortTaken", "LoopbackCluster: port %# is taken", port );
            return nullopt;
        }
        const int32 endpointIndex = getEndpointCount();
        _listEmulation.emplace_back( pTransport, _seed + static_cast<uint32>( endpointIndex ) );
        _listEndpoint.emplace_back();
        _listEndpoint.back().initialize( &_listEmulation.back(), settings );
        return endpointIndex;
    }

    bool LoopbackCluster::connectClients( int32 stepCount, float64 deltaTime )
    {
        if ( _listEndpoint.empty() || _listEndpoint.front().listen() == false )
            return false;
        const NetAddress serverAddress = _listEmulation.front().getLocalAddress();
        bool             bStarted      = true;
        for ( size_t index = 1; index < _listEndpoint.size(); ++index )
        {
            bStarted = _listEndpoint[index].connect( serverAddress ) && bStarted;
        }
        for ( int32 index = 0; index < stepCount; ++index )
        {
            step( deltaTime );
        }
        return bStarted && areClientsConnected();
    }

    bool LoopbackCluster::startThreads( const NetEndpointThreadConfig& settings )
    {
        SW_ASSERT( _listThread.empty() && "LoopbackCluster::startThreads called twice" );
        bool bStarted = true;
        for ( NetEndpoint& endpoint : _listEndpoint )
        {
            _listThread.emplace_back();
            bStarted = _listThread.back().start( &endpoint, settings ) && bStarted;
        }
        return bStarted;
    }

    void LoopbackCluster::stopThreads()
    {
        for ( NetEndpointThread& thread : _listThread )
        {
            thread.stop();
        }
        _listThread.clear();
    }

    void LoopbackCluster::step( float64 deltaTime )
    {
        SW_ASSERT( _listThread.empty() && "LoopbackCluster::step called while endpoint threads run" );
        _time += deltaTime;
        // 흉내 줄을 먼저 모두 비운다 — 이 시각까지 닿을 패킷이 어느 엔드포인트가 받기 전에 배달된다.
        for ( NetEmulationTransport& emulation : _listEmulation )
        {
            emulation.update( _time );
        }
        for ( NetEndpoint& endpoint : _listEndpoint )
        {
            endpoint.update( _time );
        }
    }

    void LoopbackCluster::run( float64 seconds, float64 deltaTime )
    {
        for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += deltaTime )
        {
            step( deltaTime );
        }
    }

    void LoopbackCluster::setConditions( const NetEmulationConditions& conditions )
    {
        for ( NetEmulationTransport& emulation : _listEmulation )
        {
            emulation.setDefaultConditions( conditions );
        }
    }

    NetEndpoint& LoopbackCluster::getEndpoint( int32 endpointIndex )
    {
        SW_ASSERT( 0 <= endpointIndex && endpointIndex < getEndpointCount() && "LoopbackCluster: no such endpoint" );
        return _listEndpoint[static_cast<size_t>( endpointIndex )];
    }

    NetEndpointThread& LoopbackCluster::getThread( int32 endpointIndex )
    {
        SW_ASSERT( 0 <= endpointIndex && endpointIndex < static_cast<int32>( _listThread.size() ) && "LoopbackCluster: no such endpoint thread" );
        return _listThread[static_cast<size_t>( endpointIndex )];
    }

    bool LoopbackCluster::areClientsConnected() const
    {
        for ( size_t index = 1; index < _listEndpoint.size(); ++index )
        {
            if ( _listEndpoint[index].getConnectionState( 0 ) != NetConnectionState::Connected )
                return false;
        }
        return true;
    }
} // namespace sw::test
