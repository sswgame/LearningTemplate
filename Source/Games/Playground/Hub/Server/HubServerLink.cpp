#include "pch.h"

#include "Games/Playground/Hub/Server/HubServerLink.h"

#include "Core/Algorithm/CompareUtil.h"
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

namespace sw
{
    static_assert( HubEntityType::kAvatar == 1 && HubEntityType::kHerb == 3, "HubEntityNetState::_typeID 의 Min · Max 를 엔티티 종류 번호와 맞춘다" );

    namespace
    {
        /**
         * @brief MMO 복제에 올리는 아바타 자리의 간격(m)입니다. 여기의 자리는 관심 영역 중심 · 보이는 목록에만 쓴다 — 걷는 동안 틱마다 다시 올리지 않게 굵게 자른다.
         *        보이는 움직임은 폰 복제(`HubPawnReplicationComponent`)가 틱마다 싣는다.
         */
        constexpr float32 kAvatarInterestStep   = 1.0f;
        constexpr float64 kReplacedFlushSeconds = 2.0; ///< 같은 계정이 다시 붙어 닫는 앞 연결에 남은 신뢰 메시지(행동 결과)를 비울 시간

        AbsolutePosition makeInterestPosition( const float3& position )
        {
            return AbsolutePosition{
                float3{ MathUtil::floor( position._x / kAvatarInterestStep ) * kAvatarInterestStep, position._y,
                       MathUtil::floor( position._z / kAvatarInterestStep ) * kAvatarInterestStep }
            };
        }
    } // namespace

    // ------------------------------------------------------------------------------
    // HubServerLink
    // ------------------------------------------------------------------------------
    HubServerLink::HubServerLink()
        : _world{}
        , _profiles{}
        , _netEvents{ *this }
        , _remoteCalls{}
        , _grid{}
        , _replicator{}
        , _mapAgent{}
        , _mapSessionTarget{}
        , _mapPendingAccount{}
        , _listLoadScratch{}
        , _listStateScratch{}
        , _mapPublished{}
        , _listSubscription{}
        , _stats{}
        , _beforeWithdraw{}
        , _pPeers{ nullptr }
        , _pRouter{ nullptr }
        , _autosaveTimer{ 0.0f }
    {
    }

    HubServerLink::~HubServerLink()
    {
        shutdown();
    }

    bool HubServerLink::initialize( INetPeers& peers, NetMessageRouter& router, const HubRules& rules, const StreamingCellGridDef& gridDef, HubServiceStoreSource storeSource )
    {
        if ( storeSource.isBound() == false || _world.initialize( rules ) == false )
            return false;
        if ( StreamingCellGrid::tryCreate( gridDef, _grid ) == false )
        {
            SW_LOG_ERROR_ID( "Hub.InterestGridInvalid", "[Hub] the interest grid (cell %# m, loading radius %# m) cannot be built", gridDef._cellSize, gridDef._loadingRadius );
            return false;
        }
        if ( _replicator.initialize( &peers, MMOReplicatorConfig{}, _grid ) == false )
            return false;
        _mapPublished.clear(); // 복제기를 새로 세웠다 — 모두 다시 올린다
        if ( _remoteCalls.initialize( &peers, &router, RemoteCallServerConfig{ rules._remoteCallBurstCount, rules._remoteCallRefillMilliseconds } ) == false )
            return false;
        _listSubscription.push_back( router.registerHandler( &_remoteCalls ) );
        _listSubscription.push_back( router.registerHandler( &_replicator ) );
        _listSubscription.push_back( router.registerHandler( &_netEvents ) );
        for ( const Subscription& subscription : _listSubscription )
        {
            if ( subscription.isActive() )
                continue;
            SW_LOG_ERROR_ID( "Hub.ServerRouterTaken", "[Hub] a message range of the hub server is already handled - the server link does not start" );
            _listSubscription.clear();
            return false;
        }
        _profiles.bindSource( std::move( storeSource ) );
        _pPeers        = &peers;
        _pRouter       = &router;
        _autosaveTimer = rules._autosaveSeconds;
        // 핫 리로드로 다시 선 링크는 이미 붙은 연결의 열림 사건을 받지 못한다(`GameNetSession` 경고) — 붙어 있는 연결을 들인다.
        vector<int32> listConnection;
        peers.collectConnected( listConnection );
        for ( const int32 connectionID : listConnection )
        {
            onConnectionOpened( connectionID );
        }
        SW_LOG_INFO_ID( "Hub.ServerStarted", "[Hub] hub server started - %# creatures and herbs", static_cast<uint32>( _world.getPlacements().size() ) );
        return true;
    }

    void HubServerLink::shutdown()
    {
        if ( _pPeers == nullptr )
            return;
        _beforeWithdraw.reset();
        saveAll(); // 저장소가 먼저 내려갔으면 `flushProfiles` 가 이미 맡겼다 — 바뀐 것이 없어 다시 맡기지 않는다
        vector<int32> listConnection;
        for ( const auto& entry : _mapAgent )
        {
            listConnection.push_back( entry.first );
        }
        for ( const int32 connectionID : listConnection )
        {
            _world.removeAvatar( _mapAgent[connectionID]->_avatarID );
        }
        _mapSessionTarget.clear();
        _mapAgent.clear();
        _mapPendingAccount.clear();
        _listSubscription.clear();
        // 맡긴 쓰기는 저장소가 끝내고 그 일이 결과를 로그로 남긴다 — 저장소는 온라인 조립의 것이라 여기서 닫지 않는다.
        _profiles.bindSource( HubServiceStoreSource{} );
        _pPeers  = nullptr;
        _pRouter = nullptr;
    }

    void HubServerLink::pollProfiles()
    {
        _listLoadScratch.clear();
        _profiles.poll( _listLoadScratch );
        for ( const HubProfileLoad& load : _listLoadScratch )
        {
            completeAdmission( load );
        }
    }

    void HubServerLink::flushProfiles()
    {
        if ( _pPeers == nullptr )
            return;
        saveAll();
        const Deadline deadline = Deadline::afterMilliseconds( ServiceStore::kStopDrainMilliseconds );
        while ( _profiles.getPendingCount() > 0 && deadline.isExpired() == false )
        {
            if ( _profiles.waitCompletions( deadline ) == false )
                break;
            pollProfiles(); // 쓰는 중에 들어온 바이트(쓰기 합치기)를 이어 맡긴다
        }
        if ( _profiles.getPendingCount() > 0 )
            SW_LOG_ERROR_ID( "Hub.ProfileFlushTimedOut", "[Hub] %# profile job(s) did not finish before the store stopped - the last progress may be lost", _profiles.getPendingCount() );
    }

    void HubServerLink::update( float32 deltaSeconds )
    {
        if ( _pPeers == nullptr )
            return;
        _world.update( deltaSeconds );
        pollProfiles();
        for ( const auto& entry : _mapAgent )
        {
            if ( _world.consumeDirtyFlag( entry.second->_avatarID ) )
                sendProgress( *entry.second );
        }
        _autosaveTimer -= deltaSeconds;
        if ( _autosaveTimer <= 0.0f )
        {
            _autosaveTimer = _world.getRules()._autosaveSeconds;
            saveAll();
        }
        publishEntities();
        _replicator.update( deltaSeconds );
    }

    void HubServerLink::collectAvatars( vector<HubAvatarSession>& outListSession ) const
    {
        outListSession.clear();
        for ( const auto& entry : _mapAgent )
        {
            const HubAvatar* pAvatar = _world.findAvatar( entry.second->_avatarID );
            if ( pAvatar != nullptr )
                outListSession.push_back( HubAvatarSession{ AbsolutePosition{ pAvatar->_profile._position }, pAvatar->_avatarID, entry.first } );
        }
        VectorUtil::sort( outListSession, Ascending<&HubAvatarSession::_avatarID>{} );
    }

    void HubServerLink::acceptConnection( int32 connectionID, uint64 accountID )
    {
        // 이미 들였거나 읽는 중인 연결은 다시 들이지 않는다(열림 사건과 핫 리로드의 `collectConnected` 가 겹칠 수 있다).
        const bool bKnownConnection = _mapAgent.find( connectionID ) != _mapAgent.end() || _mapPendingAccount.find( connectionID ) != _mapPendingAccount.end();
        if ( _pPeers == nullptr || bKnownConnection )
            return;
        // 앞서 거절해 끊는 중인 연결의 늦은 열림 사건도 들이지 않는다 — 다시 들이면 같은 연결을 두 번 거절한다.
        if ( _pPeers->getConnectionState( connectionID ) != NetConnectionState::Connected )
            return;
        if ( accountID == 0 )
        {
            ++_stats._refusedCount;
            // 허브 서버는 티켓을 요구하는 세션에서만 선다(`HubWorldComponent`) — 여기까지 온 주체 없는 연결은 인증기가 걸렀어야 할 것이라 끊는다(심층 방어).
            SW_LOG_WARNING_ID( "Hub.NoAccount", "[Hub] connection %# has no account - the hub needs a game connect ticket (server config _bGameConnectTicket); disconnecting it", connectionID );
            _pPeers->disconnect( connectionID, NetLinger::none() );
            return;
        }
        // 같은 계정이 이미 들어와 있거나 읽는 중이면 앞 연결을 저장하고 끊는다 — 저장이 읽기보다 먼저 맡겨지므로 새 연결은 그 진행을 읽는다.
        int32 previousConnectionID = -1;
        for ( const auto& entry : _mapAgent )
        {
            const HubAvatar* pAvatar = _world.findAvatar( entry.second->_avatarID );
            if ( pAvatar != nullptr && pAvatar->_accountID == accountID )
                previousConnectionID = entry.first;
        }
        for ( const auto& entry : _mapPendingAccount )
        {
            if ( entry.second == accountID )
                previousConnectionID = entry.first;
        }
        if ( previousConnectionID >= 0 && previousConnectionID != connectionID )
        {
            ++_stats._replacedCount;
            SW_LOG_INFO_ID( "Hub.SessionReplaced", "[Hub] account %# connected again on %# - the session on %# is saved and closed", accountID, connectionID, previousConnectionID );
            releaseAgent( previousConnectionID );
            _mapPendingAccount.erase( previousConnectionID );
            _pPeers->disconnect( previousConnectionID, NetLinger::waitUpTo( kReplacedFlushSeconds ) );
        }
        _mapPendingAccount[connectionID] = accountID;
        _profiles.requestLoad( accountID );
    }

    uint32 HubServerLink::computeAttackReadyTick( uint32 avatarID ) const
    {
        // 대기와 월드 시계는 같은 게임 시간으로 흐른다. 올린 뒤 한 틱을 더해 실수 반올림으로 이른 쪽에 서지 않게 한다.
        const HubAvatar* const pAvatar      = _world.findAvatar( avatarID );
        const float32          cooldown     = pAvatar != nullptr ? pAvatar->_attackCooldown : 0.0f;
        const uint32           cooldownTick = cooldown > 0.0f ? static_cast<uint32>( MathUtil::ceil( cooldown / NetClock::kTickSeconds ) ) + 1 : 0;
        return _pRouter->getWorldClock().getWholeTick() + cooldownTick;
    }

    void HubServerLink::reportAction( HubAvatarAgent& agent, HubActionResult result )
    {
        ++_stats._actionCount;
        (void)_remoteCalls.callSession( agent._connectionID, "receiveActionResult", { ReflectedValue::make( result ), ReflectedValue::make( computeAttackReadyTick( agent._avatarID ) ) } );
        if ( _world.consumeDirtyFlag( agent._avatarID ) )
            sendProgress( agent );
    }

    NetHandleResult HubServerLink::NetEvents::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        (void)context;
        (void)body;
        return NetHandleResult::NotMine; // 마스크 0 — 사건만 받는다
    }

    void HubServerLink::onConnectionOpened( int32 connectionID )
    {
        if ( _pPeers != nullptr )
            acceptConnection( connectionID, _pPeers->getConnectionPrincipal( connectionID ) );
    }

    void HubServerLink::onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
    {
        (void)reason;
        _mapPendingAccount.erase( connectionID );
        releaseAgent( connectionID );
    }

    void HubServerLink::completeAdmission( const HubProfileLoad& load )
    {
        int32 connectionID = -1;
        for ( const auto& entry : _mapPendingAccount )
        {
            if ( entry.second == load._accountID )
                connectionID = entry.first;
        }
        if ( connectionID < 0 )
            return; // 읽는 사이 연결이 닫혔다
        _mapPendingAccount.erase( connectionID );
        if ( load._bOk == SW_FALSE )
        {
            ++_stats._refusedCount;
            _pPeers->disconnect( connectionID, NetLinger::none() );
            return;
        }
        HubProfile profile = load._profile;
        if ( load._bFound == SW_FALSE )
            profile._position = _world.chooseAvatarStart(); // 새 계정은 빈 시작 자리에서 시작한다
        unique_ptr<HubAvatarAgent> pAgent = make_unique<HubAvatarAgent>();
        pAgent->_pWorld                   = &_world;
        pAgent->_onAction                 = SW_DELEGATE_METHOD( decltype( pAgent->_onAction ), &HubServerLink::reportAction, this );
        pAgent->_connectionID             = connectionID;
        pAgent->_avatarID                 = _world.addAvatar( load._accountID, profile );
        Subscription sessionTarget        = _remoteCalls.registerSessionTarget( connectionID, *HubAvatarAgent::StaticType(), pAgent.get() );
        if ( sessionTarget.isActive() == false )
        {
            _world.removeAvatar( pAgent->_avatarID );
            ++_stats._refusedCount;
            _pPeers->disconnect( connectionID, NetLinger::none() );
            return;
        }
        _replicator.setObserver( connectionID, pAgent->_avatarID );
        ++_stats._admittedCount;
        SW_LOG_INFO_ID( "Hub.Admitted", "[Hub] account %# entered the hub as avatar %# on connection %# (level %#, %#)", load._accountID, pAgent->_avatarID, connectionID,
                        profile._level, load._bFound != SW_FALSE ? "stored progress" : "new account" );
        sendProgress( *pAgent );
        (void)_world.consumeDirtyFlag( pAgent->_avatarID );
        _mapAgent[connectionID]         = std::move( pAgent );
        _mapSessionTarget[connectionID] = std::move( sessionTarget );
    }

    void HubServerLink::releaseAgent( int32 connectionID )
    {
        const auto iter = _mapAgent.find( connectionID );
        if ( iter == _mapAgent.end() )
            return;
        _mapSessionTarget.erase( connectionID );
        _replicator.removeObserver( connectionID );
        const HubAvatar* pAvatar = _world.findAvatar( iter->second->_avatarID );
        if ( pAvatar != nullptr )
        {
            _profiles.requestSave( pAvatar->_accountID, pAvatar->_profile );
            _profiles.releaseAccount( pAvatar->_accountID );
        }
        removePublishedEntity( iter->second->_avatarID );
        _world.removeAvatar( iter->second->_avatarID );
        _mapAgent.erase( iter );
    }

    void HubServerLink::sendProgress( const HubAvatarAgent& agent )
    {
        const HubAvatar* pAvatar = _world.findAvatar( agent._avatarID );
        if ( pAvatar == nullptr )
            return;
        const HubProfile& profile = pAvatar->_profile;
        (void)_remoteCalls.callSession( agent._connectionID, "receiveProgress",
                                        { ReflectedValue::make( pAvatar->_avatarID ), ReflectedValue::make( profile._level ), ReflectedValue::make( profile._experience ),
                                          ReflectedValue::make( profile._coins ), ReflectedValue::make( profile._herbCount ), ReflectedValue::make( profile._defeatCount ),
                                          ReflectedValue::make( _world.getPartySize( pAvatar->_avatarID ) ), ReflectedValue::make( pAvatar->_inviteFromID ) } );
    }

    void HubServerLink::saveAll()
    {
        _world.forEachAvatar( [this]( const HubAvatar& avatar )
        {
            _profiles.requestSave( avatar._accountID, avatar._profile );
        } );
    }

    void HubServerLink::publishEntities()
    {
        // 매 틱 모두를 다시 싣지 않는다 — 위치 · 상태 값이 지난번 올린 것과 다른 엔티티만 올린다(가만히 있는 아바타 · 짐승 · 버섯은 건너뛴다).
        _world.forEachAvatar( [this]( const HubAvatar& avatar )
        {
            publishEntity( avatar._avatarID, HubPublishedEntity{ makeInterestPosition( avatar._profile._position ), avatar._profile._level, HubEntityType::kAvatar }, 2.0f ); // 다른 플레이어는 짐승보다 먼저 보인다
        } );
        for ( const HubPlacementState& spawn : _world.getPlacements() )
        {
            if ( spawn._bPresent == SW_FALSE )
            {
                removePublishedEntity( spawn._placementID );
                continue;
            }
            const uint32 typeID = spawn._def._kind == HubPlacementKind::Creature ? HubEntityType::kCreature : HubEntityType::kHerb;
            publishEntity( spawn._placementID, HubPublishedEntity{ AbsolutePosition{ spawn._def._position }, spawn._health, typeID }, 1.0f );
        }
    }

    void HubServerLink::publishEntity( uint32 entityID, const HubPublishedEntity& value, float32 importance )
    {
        const auto iter = _mapPublished.find( entityID );
        if ( iter != _mapPublished.end() && iter->second == value )
            return;
        // 상태 바이트는 칸 선언(`HubEntityNetState`)이 정한다. 위치는 엔티티 칸이 싣는다.
        HubEntityNetState state;
        state._typeID     = value._typeID;
        state._stateValue = value._stateValue;
        BitWriter writer( std::move( _listStateScratch ) );
        writer.clear();
        const bool bWritten = ReplicatedLayout::writeFull( state, writer );
        _listStateScratch   = writer.releaseBytes();
        if ( bWritten == false )
        {
            SW_LOG_ERROR_ID( "HubServerLink.EntityStateOutOfRange", "HubServerLink: entity %# has a state outside the replicated range (type %#) - not published", entityID, value._typeID );
            return;
        }
        MMOEntity entity;
        entity._listState  = _listStateScratch;
        entity._position   = value._position;
        entity._entityID   = entityID;
        entity._typeID     = value._typeID;
        entity._importance = importance;
        _replicator.setEntity( entity );
        _mapPublished[entityID] = value;
        ++_stats._publishCount;
    }

    void HubServerLink::removePublishedEntity( uint32 entityID )
    {
        if ( _mapPublished.erase( entityID ) != 0 )
            _replicator.removeEntity( entityID );
    }

} // namespace sw
