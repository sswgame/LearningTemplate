#include "pch.h"

#include "GameFramework/Kits/Network/NetSimulation/NetSimHarness.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    namespace
    {
        struct NetSimHarnessInternal
        {
            /** @brief 씨앗 하나에서 끝점마다 다른 씨앗을 냅니다(splitmix64 한 걸음). */
            static uint64 mixSeed( uint64 seed, uint64 salt )
            {
                uint64 value = seed + 0x9E3779B97F4A7C15ull * ( salt + 1u );
                value        = ( value ^ ( value >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
                value        = ( value ^ ( value >> 27 ) ) * 0x94D049BB133111EBull;
                return value ^ ( value >> 31 );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    NetSimTrafficCounter::NetSimTrafficCounter( INetTransport* pInner )
        : _pInner{ pInner }
        , _listDestination{}
        , _traffic{}
    {
    }

    bool NetSimTrafficCounter::send( const NetAddress& to, const uint8* pData, int32 size )
    {
        if ( _pInner == nullptr )
            return false;
        _traffic._sentBytes += static_cast<uint64>( MathUtil::max( 0, size ) );
        ++_traffic._sentPacketCount;
        Destination* pDestination = nullptr;
        for ( Destination& destination : _listDestination )
        {
            if ( destination._to == to )
            {
                pDestination = &destination;
                break;
            }
        }
        if ( pDestination == nullptr )
        {
            _listDestination.push_back( Destination{ to, 0 } );
            pDestination = &_listDestination.back();
        }
        pDestination->_sentBytes += static_cast<uint64>( MathUtil::max( 0, size ) );
        return _pInner->send( to, pData, size );
    }

    bool NetSimTrafficCounter::receive( NetAddress& outFrom, vector<uint8>& outBuffer )
    {
        if ( _pInner == nullptr || _pInner->receive( outFrom, outBuffer ) == false )
            return false;
        _traffic._receivedBytes += static_cast<uint64>( outBuffer.size() );
        ++_traffic._receivedPacketCount;
        return true;
    }

    NetAddress NetSimTrafficCounter::getLocalAddress() const { return _pInner != nullptr ? _pInner->getLocalAddress() : NetAddress{}; }

    void NetSimTrafficCounter::update( float64 time )
    {
        if ( _pInner != nullptr )
            _pInner->update( time );
    }

    uint64 NetSimTrafficCounter::getSentBytesTo( const NetAddress& to ) const
    {
        for ( const Destination& destination : _listDestination )
        {
            if ( destination._to == to )
                return destination._sentBytes;
        }
        return 0;
    }
} // namespace sw

namespace sw
{
    NetSimWorld::NetSimWorld( NetSimRole role, int32 worldIndex, INetTransport* pTransport, const NetHostSettings& hostSettings )
        : _counter{ pTransport }
        , _host{}
        , _router{}
        , _pScene{ make_unique<Scene>( role == NetSimRole::Server ? "NetSimServer" : "NetSimClient" ) }
        , _pSession{}
        , _listUnhandled{}
        , _listEvent{}
        , _worldIndex{ worldIndex }
        , _localTick{ 0 }
        , _role{ role }
    {
        _host.initialize( &_counter, hostSettings );
    }

    NetSimWorld::~NetSimWorld() { stop(); }

    bool NetSimWorld::isConnected() const { return isServer() || _host.getConnectionState( 0 ) == NetConnectionState::Connected; }

    GameObjectManager& NetSimWorld::getObjectManager() { return *_pScene->getObjectManager(); }

    void NetSimWorld::start( INetSimGame* pGame )
    {
        if ( pGame != nullptr )
            _pSession = pGame->createSession( *this );
        getObjectManager().beginPlay();
    }

    void NetSimWorld::tick( float32 deltaTime )
    {
        _listUnhandled.clear();
        _listEvent.clear();
        (void)_router.pump( _host, &_listUnhandled, &_listEvent );
        if ( _pSession != nullptr )
        {
            for ( const NetHostEvent& event : _listEvent )
                _pSession->onHostEvent( *this, event );
            _pSession->onTickBegin( *this, deltaTime );
        }
        // 씬의 `tick` 이 아니라 매니저를 바로 — 씬은 오디오 리스너 · 에미터를 프로세스에 하나인 오디오 엔진에 넣는다(월드가 여럿이면 서로 덮는다).
        getObjectManager().tick( deltaTime );
        if ( _pSession != nullptr )
            _pSession->onTickEnd( *this, deltaTime );
        ++_localTick;
    }

    void NetSimWorld::stop()
    {
        if ( _pScene == nullptr )
            return;
        if ( _pSession != nullptr )
            _pSession->onWorldStopping( *this );
        GameObjectManager& manager = getObjectManager();
        if ( manager.hasBegunPlay() )
        {
            manager.endPlay();
            manager.tick( 0.0f ); // 끝난 플레이의 물리 바디를 놓는다
        }
        _pSession.reset();
        _pScene.reset();
    }
} // namespace sw

namespace sw
{
    NetSimHarness::NetSimHarness()
        : _settings{}
        , _pNetwork{}
        , _listLink{}
        , _pServer{}
        , _listClient{}
        , _pGame{ nullptr }
        , _time{ 0.0 }
        , _tick{ 0 }
        , _nextWorldIndex{ 1 }
        , _bStepping{ false }
    {
    }

    NetSimHarness::~NetSimHarness() { shutdown(); }

    bool NetSimHarness::initialize( const NetSimSettings& settings, INetSimGame* pGame )
    {
        shutdown();
        if ( settings._tickInterval <= 0.0 )
        {
            SW_LOG_ERROR( "NetSimHarness: tick interval must be positive (%#)", settings._tickInterval );
            return false;
        }
        _settings       = settings;
        _pGame          = pGame;
        _time           = 0.0;
        _tick           = 0;
        _nextWorldIndex = 1;
        _pNetwork       = make_unique<LoopbackNetwork>( static_cast<uint32>( NetSimHarnessInternal::mixSeed( settings._seed, 0xFFFFu ) ) );
        Link* pLink     = createLink( 0 );
        if ( pLink == nullptr )
            return false;
        _pServer = unique_ptr<NetSimWorld>( createWorld( NetSimRole::Server, 0, *pLink ) );
        if ( _pServer->getHost().listen() == false )
        {
            SW_LOG_ERROR( "NetSimHarness: server host failed to listen" );
            shutdown();
            return false;
        }
        _pServer->start( _pGame );
        return true;
    }

    void NetSimHarness::shutdown()
    {
        SW_ASSERT( _bStepping == false && "NetSimHarness::shutdown called from inside step" );
        _listClient.clear();
        _pServer.reset();
        _listLink.clear();
        _pNetwork.reset();
        _pGame = nullptr;
    }

    NetSimHarness::Link* NetSimHarness::createLink( int32 worldIndex )
    {
        const uint32       portOffset = static_cast<uint32>( worldIndex );
        const uint16       port       = static_cast<uint16>( static_cast<uint32>( _settings._serverPort ) + portOffset );
        LoopbackTransport* pEndpoint  = _pNetwork->createEndpoint( port );
        if ( pEndpoint == nullptr )
        {
            SW_LOG_ERROR( "NetSimHarness: port %# is taken", port );
            return nullptr;
        }
        unique_ptr<Link> pLink = make_unique<Link>();
        pLink->_pEmulation     = make_unique<NetEmulationTransport>( pEndpoint, static_cast<uint32>( NetSimHarnessInternal::mixSeed( _settings._seed, portOffset ) ) );
        pLink->_address        = pEndpoint->getLocalAddress();
        pLink->_worldIndex     = worldIndex;
        _listLink.push_back( std::move( pLink ) );
        return _listLink.back().get();
    }

    NetSimHarness::Link* NetSimHarness::findLink( int32 worldIndex )
    {
        for ( unique_ptr<Link>& pLink : _listLink )
        {
            if ( pLink->_worldIndex == worldIndex )
                return pLink.get();
        }
        return nullptr;
    }

    NetSimWorld* NetSimHarness::createWorld( NetSimRole role, int32 worldIndex, Link& link )
    {
        NetHostSettings hostSettings = _settings._hostSettings;
        hostSettings._saltSeed       = NetSimHarnessInternal::mixSeed( _settings._seed, 0x10000u + static_cast<uint64>( worldIndex ) ) | 1u;
        // 보내기 간격이 틱의 배수면 틱 시각(틱 × 간격)의 반올림 때문에 한 틱 늦게 나갈 수 있다 — 반 틱 당겨 "k 틱마다 한 번" 이 정확히 지켜지게.
        hostSettings._sendInterval = MathUtil::max( 0.0, hostSettings._sendInterval - _settings._tickInterval * 0.5 );
        return sw_new NetSimWorld( role, worldIndex, link._pEmulation.get(), hostSettings );
    }

    int32 NetSimHarness::addClient( const NetSimLinkConditions& conditions )
    {
        SW_ASSERT( _bStepping == false && "NetSimHarness::addClient called from inside step" );
        if ( _pServer == nullptr )
            return -1;
        const int32 worldIndex = _nextWorldIndex;
        Link*       pLink      = createLink( worldIndex );
        if ( pLink == nullptr )
            return -1;
        ++_nextWorldIndex;
        applyLinkConditions( worldIndex, conditions );
        unique_ptr<NetSimWorld> pWorld = unique_ptr<NetSimWorld>( createWorld( NetSimRole::Client, worldIndex, *pLink ) );
        if ( pWorld->getHost().connect( _pServer->getAddress() ) == false )
        {
            SW_LOG_ERROR( "NetSimHarness: client %# failed to start connecting", worldIndex );
            return -1;
        }
        pWorld->start( _pGame );
        _listClient.push_back( std::move( pWorld ) );
        return worldIndex;
    }

    void NetSimHarness::removeClient( int32 worldIndex )
    {
        SW_ASSERT( _bStepping == false && "NetSimHarness::removeClient called from inside step" );
        for ( size_t index = 0; index < _listClient.size(); ++index )
        {
            NetSimWorld& world = *_listClient[index];
            if ( world.getWorldIndex() != worldIndex )
                continue;
            // 끊김 알림을 흉내 줄에 넣는다 — 끝점(Link)은 남아 다음 망 단계들이 마저 내보낸다.
            world.getHost().disconnectAll();
            world.getHost().update( _time );
            _listClient.erase( _listClient.begin() + static_cast<std::ptrdiff_t>( index ) );
            return;
        }
    }

    void NetSimHarness::setLinkConditions( int32 worldIndex, const NetSimLinkConditions& conditions ) { applyLinkConditions( worldIndex, conditions ); }

    void NetSimHarness::applyLinkConditions( int32 worldIndex, const NetSimLinkConditions& conditions )
    {
        Link* pClientLink = findLink( worldIndex );
        Link* pServerLink = findLink( 0 );
        if ( pClientLink == nullptr || pServerLink == nullptr || worldIndex == 0 )
            return;
        pClientLink->_pEmulation->setDefaultConditions( conditions._upstream );
        pServerLink->_pEmulation->setConditions( pClientLink->_address, conditions._downstream );
    }

    NetSimWorld* NetSimHarness::findClient( int32 worldIndex ) const
    {
        for ( const unique_ptr<NetSimWorld>& pWorld : _listClient )
        {
            if ( pWorld->getWorldIndex() == worldIndex )
                return pWorld.get();
        }
        return nullptr;
    }

    void NetSimHarness::collectClients( vector<NetSimWorld*>& outListWorld ) const
    {
        outListWorld.clear();
        for ( const unique_ptr<NetSimWorld>& pWorld : _listClient )
            outListWorld.push_back( pWorld.get() );
    }

    bool NetSimHarness::areAllClientsConnected() const
    {
        for ( const unique_ptr<NetSimWorld>& pWorld : _listClient )
        {
            if ( pWorld->isConnected() == false )
                return false;
        }
        return true;
    }

    void NetSimHarness::step()
    {
        if ( _pServer == nullptr )
            return;
        _bStepping = true;
        ++_tick;
        _time                   = static_cast<float64>( _tick ) * _settings._tickInterval;
        const float32 deltaTime = static_cast<float32>( _settings._tickInterval );
        _pServer->tick( deltaTime );
        for ( unique_ptr<NetSimWorld>& pWorld : _listClient )
            pWorld->tick( deltaTime );
        updateNetwork();
        _bStepping = false;
    }

    void NetSimHarness::updateNetwork()
    {
        // 1) 보내기 — 이번 틱에 쌓인 메시지가 패킷이 되어 흉내 줄로 간다.
        _pServer->getHost().update( _time );
        for ( unique_ptr<NetSimWorld>& pWorld : _listClient )
            pWorld->getHost().update( _time );
        // 2) 흉내 줄에서 때가 된 것을 망에 싣고 이 시각까지 배달한다(떠난 클라이언트의 끝점도 — 끊김 알림이 아직 줄에 있을 수 있다).
        for ( unique_ptr<Link>& pLink : _listLink )
            pLink->_pEmulation->update( _time );
        _pNetwork->advance( _time );
        // 3) 받기 — 같은 시각이라 보내기 간격이 막아 다시 보내지는 않는다.
        _pServer->getHost().update( _time );
        for ( unique_ptr<NetSimWorld>& pWorld : _listClient )
            pWorld->getHost().update( _time );
    }

    void NetSimHarness::stepTicks( uint32 tickCount )
    {
        for ( uint32 index = 0; index < tickCount; ++index )
            step();
    }

    int32 NetSimHarness::stepUntil( bool ( *pPredicate )( const NetSimHarness&, void* ), void* pContext, uint32 maxTicks )
    {
        if ( pPredicate == nullptr )
            return -1;
        for ( uint32 index = 0; index <= maxTicks; ++index )
        {
            if ( pPredicate( *this, pContext ) )
                return static_cast<int32>( index );
            if ( index < maxTicks )
                step();
        }
        return -1;
    }
} // namespace sw
