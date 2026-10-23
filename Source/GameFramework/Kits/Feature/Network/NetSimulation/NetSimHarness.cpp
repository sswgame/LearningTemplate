#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    namespace
    {
        /** @brief 시드 하나에서 엔드포인트마다 다른 시드를 만듭니다(splitmix64 한 단계). */
        uint64 mixNetSimSeed( uint64 seed, uint64 salt )
        {
            uint64 value = seed + HashUtil::kGoldenRatio64 * ( salt + 1u );
            return HashUtil::mix64( value );
        }
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

    NetAddress NetSimTrafficCounter::getLocalAddress() const
    {
        return _pInner != nullptr ? _pInner->getLocalAddress() : NetAddress{};
    }

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
    NetSimWorld::NetSimWorld( const SceneEngineServices& services, NetSimRole role, int32 worldIndex, INetTransport* pTransport, const NetHostConfig& hostSettings,
                              INetSimSceneSource* pSceneSource, Scene* pBorrowedScene )
        : _counter{ pTransport }
        , _host{}
        , _router{}
        , _pOwnedScene{ pBorrowedScene == nullptr ? make_unique<Scene>( role == NetSimRole::Server ? "NetSimServer" : "NetSimClient", services ) : unique_ptr<Scene>{} }
        , _pSceneSource{ pBorrowedScene != nullptr ? pSceneSource : nullptr }
        , _pScene{ pBorrowedScene != nullptr ? pBorrowedScene : _pOwnedScene.get() }
        , _pSession{}
        , _listUnhandled{}
        , _listEvent{}
        , _worldIndex{ worldIndex }
        , _localTick{ 0 }
        , _role{ role }
        , _bInputFocus{ false }
    {
        _host.initialize( &_counter, hostSettings );
    }

    NetSimWorld::~NetSimWorld()
    {
        stop();
    }

    bool NetSimWorld::isConnected() const
    {
        return isServer() || _host.getConnectionState( 0 ) == NetConnectionState::Connected;
    }

    GameObjectManager& NetSimWorld::getObjectManager()
    {
        return *_pScene->getObjectManager();
    }

    void NetSimWorld::start( INetSimGame* pGame )
    {
        if ( pGame != nullptr )
            _pSession = pGame->createSession( *this );
        getObjectManager().beginPlay();
    }

    void NetSimWorld::update( float32 deltaTime )
    {
        _listUnhandled.clear();
        _listEvent.clear();
        (void)_router.pump( _host, &_listUnhandled, &_listEvent );
        if ( _pSession != nullptr )
        {
            for ( const NetHostEvent& event : _listEvent )
            {
                _pSession->onHostEvent( *this, event );
            }
            _pSession->onTickBegin( *this, deltaTime );
        }
        // 씬의 `tick` 이 아니라 매니저를 직접 틱한다. 씬은 오디오 리스너와 에미터를 프로세스에 하나뿐인 오디오 엔진에 넣으므로, 월드가 여럿이면 서로 덮어쓴다.
        getObjectManager().update( deltaTime );
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
            manager.update( 0.0f ); // 끝난 플레이의 물리 바디를 해제한다
        }
        _pSession.reset();
        if ( _pSceneSource != nullptr )
            _pSceneSource->releaseScene( *_pScene );
        _pOwnedScene.reset();
        _pScene       = nullptr;
        _pSceneSource = nullptr;
    }
} // namespace sw

namespace sw
{
    NetSimHarness::NetSimHarness()
        : _settings{}
        , _services{}
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

    NetSimHarness::~NetSimHarness()
    {
        shutdown();
    }

    bool NetSimHarness::initialize( const SceneEngineServices& services, const NetSimConfig& settings, INetSimGame* pGame )
    {
        shutdown();
        if ( settings._tickInterval <= 0.0 )
        {
            SW_LOG_ERROR( "NetSimHarness: tick interval must be positive (%#)", settings._tickInterval );
            return false;
        }
        _settings       = settings;
        _services       = services;
        _pGame          = pGame;
        _time           = 0.0;
        _tick           = 0;
        _nextWorldIndex = 1;
        _pNetwork       = make_unique<LoopbackNetwork>();
        Link* pLink     = createLink( 0 );
        if ( pLink == nullptr )
            return false;
        _pServer = createWorld( NetSimRole::Server, 0, *pLink );
        if ( _pServer == nullptr )
        {
            shutdown();
            return false; // 씬을 빌리지 못했다 — createWorld 가 알렸다
        }
        if ( _pServer->_host.listen() == false )
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
        pLink->_pEmulation     = make_unique<NetEmulationTransport>( pEndpoint, static_cast<uint32>( mixNetSimSeed( _settings._seed, portOffset ) ) );
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

    unique_ptr<NetSimWorld> NetSimHarness::createWorld( NetSimRole role, int32 worldIndex, Link& link )
    {
        NetHostConfig hostSettings = _settings._hostSettings;
        hostSettings._saltSeed     = mixNetSimSeed( _settings._seed, 0x10000u + static_cast<uint64>( worldIndex ) ) | 1u;
        // 송신 간격이 틱의 배수일 때 틱 시각(틱 × 간격)의 반올림 오차로 한 틱 늦게 나갈 수 있다. 반 틱 앞당겨서 "k 틱마다 한 번" 이 정확히 지켜지게 한다.
        hostSettings._sendInterval = MathUtil::max( 0.0, hostSettings._sendInterval - _settings._tickInterval * 0.5 );
        Scene* pBorrowedScene      = nullptr;
        if ( _settings._pSceneSource != nullptr )
        {
            pBorrowedScene = _settings._pSceneSource->acquireScene( role, worldIndex );
            if ( pBorrowedScene == nullptr )
            {
                SW_LOG_ERROR( "NetSimHarness: the scene source gave no scene for world %#", worldIndex );
                return {};
            }
        }
        return make_unique<NetSimWorld>( _services, role, worldIndex, link._pEmulation.get(), hostSettings, _settings._pSceneSource, pBorrowedScene );
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
        unique_ptr<NetSimWorld> pWorld = createWorld( NetSimRole::Client, worldIndex, *pLink );
        if ( pWorld == nullptr )
            return -1; // 씬을 빌리지 못했다 — createWorld 가 알렸다
        if ( pWorld->_host.connect( _pServer->getAddress() ) == false )
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
            // 끊김 알림을 흉내 큐에 넣는다. 엔드포인트(Link)는 남아 있으므로 이후 망 단계에서 마저 내보낸다.
            world._host.disconnectAll();
            world._host.update( _time );
            _listClient.erase( _listClient.begin() + static_cast<std::ptrdiff_t>( index ) );
            return;
        }
    }

    void NetSimHarness::setLinkConditions( int32 worldIndex, const NetSimLinkConditions& conditions )
    {
        applyLinkConditions( worldIndex, conditions );
    }

    bool NetSimHarness::setInputFocus( int32 worldIndex )
    {
        NetSimWorld* pTarget = nullptr;
        if ( worldIndex == 0 && _pServer != nullptr )
            pTarget = _pServer.get();
        else if ( worldIndex > 0 )
            pTarget = findClient( worldIndex );
        if ( worldIndex != -1 && pTarget == nullptr )
            return false;
        if ( _pServer != nullptr )
            _pServer->_bInputFocus = false;
        for ( unique_ptr<NetSimWorld>& pWorld : _listClient )
        {
            pWorld->_bInputFocus = false;
        }
        if ( pTarget != nullptr )
            pTarget->_bInputFocus = true;
        return true;
    }

    int32 NetSimHarness::getInputFocus() const
    {
        if ( _pServer != nullptr && _pServer->_bInputFocus )
            return 0;
        for ( const unique_ptr<NetSimWorld>& pWorld : _listClient )
        {
            if ( pWorld->_bInputFocus )
                return pWorld->getWorldIndex();
        }
        return -1;
    }

    void NetSimHarness::applyLinkConditions( int32 worldIndex, const NetSimLinkConditions& conditions )
    {
        Link*      pClientLink   = findLink( worldIndex );
        Link*      pServerLink   = findLink( 0 );
        const bool bInvalidLinks = pClientLink == nullptr || pServerLink == nullptr || worldIndex == 0;
        if ( bInvalidLinks )
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
        {
            outListWorld.push_back( pWorld.get() );
        }
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

    uint64 NetSimHarness::getDroppedPacketCount() const
    {
        uint64 droppedCount = _pNetwork != nullptr ? _pNetwork->getDroppedCount() : 0u;
        for ( const unique_ptr<Link>& pLink : _listLink )
        {
            droppedCount += pLink->_pEmulation->getStats()._droppedCount;
        }
        return droppedCount;
    }

    void NetSimHarness::step()
    {
        if ( _pServer == nullptr )
            return;
        _bStepping = true;
        ++_tick;
        _time                   = static_cast<float64>( _tick ) * _settings._tickInterval;
        const float32 deltaTime = static_cast<float32>( _settings._tickInterval );
        _pServer->update( deltaTime );
        for ( unique_ptr<NetSimWorld>& pWorld : _listClient )
        {
            pWorld->update( deltaTime );
        }
        updateNetwork();
        _bStepping = false;
    }

    void NetSimHarness::updateNetwork()
    {
        // 1) 송신: 이번 틱에 쌓인 메시지가 패킷이 되어 흉내 큐로 간다.
        _pServer->_host.update( _time );
        for ( unique_ptr<NetSimWorld>& pWorld : _listClient )
        {
            pWorld->_host.update( _time );
        }
        // 2) 흉내 큐에서 전달 시각이 된 패킷을 망에 싣고 망이 배달한다. 떠난 클라이언트의 엔드포인트도 처리한다. 끊김 알림이 아직 큐에 있을 수 있기 때문이다.
        for ( unique_ptr<Link>& pLink : _listLink )
        {
            pLink->_pEmulation->update( _time );
        }
        _pNetwork->deliverInFlight();
        // 3) 수신: 같은 시각이므로 송신 간격에 막혀 다시 보내지는 않는다.
        _pServer->_host.update( _time );
        for ( unique_ptr<NetSimWorld>& pWorld : _listClient )
        {
            pWorld->_host.update( _time );
        }
    }

    void NetSimHarness::stepTicks( uint32 tickCount )
    {
        for ( uint32 index = 0; index < tickCount; ++index )
        {
            step();
        }
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
