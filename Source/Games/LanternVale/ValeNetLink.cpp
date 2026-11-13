#include "pch.h"

#include "Games/LanternVale/ValeNetLink.h"

#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // ValeAvatarAgent — `Server` 함수 몸은 서버 링크의 골짜기에 넘기고, `Client` 함수 몸은 받은 값을 적는다
    // ------------------------------------------------------------------------------
    void ValeAvatarAgent::attackCreature( uint32 creatureID )
    {
        if ( _pServer != nullptr )
            _pServer->reportAction( *this, _pServer->getWorld().attackCreature( _avatarID, creatureID ) );
    }

    bool ValeAvatarAgent::validateAttackCreature( uint32 creatureID ) const
    {
        return _pServer != nullptr && _pServer->getWorld().isPlacementOfKind( creatureID, ValePlacementKind::Creature );
    }

    void ValeAvatarAgent::gatherHerb( uint32 herbID )
    {
        if ( _pServer != nullptr )
            _pServer->reportAction( *this, _pServer->getWorld().gatherHerb( _avatarID, herbID ) );
    }

    bool ValeAvatarAgent::validateGatherHerb( uint32 herbID ) const
    {
        return _pServer != nullptr && _pServer->getWorld().isPlacementOfKind( herbID, ValePlacementKind::Herb );
    }

    void ValeAvatarAgent::moveTo( float32 x, float32 z )
    {
        if ( _pServer != nullptr )
            _pServer->reportAction( *this, _pServer->getWorld().moveAvatar( _avatarID, x, z ) );
    }

    bool ValeAvatarAgent::validateMoveTo( float32 x, float32 z ) const
    {
        return _pServer != nullptr && _pServer->getWorld().isInsideVale( x, z );
    }

    void ValeAvatarAgent::inviteToParty( uint32 avatarID )
    {
        if ( _pServer != nullptr )
            _pServer->reportAction( *this, _pServer->getWorld().inviteToParty( _avatarID, avatarID ) );
    }

    bool ValeAvatarAgent::validateInviteToParty( uint32 avatarID ) const
    {
        return ValeWorld::isValidAvatarID( avatarID ) && avatarID != _avatarID;
    }

    void ValeAvatarAgent::acceptPartyInvite( uint32 leaderAvatarID )
    {
        if ( _pServer != nullptr )
            _pServer->reportAction( *this, _pServer->getWorld().acceptPartyInvite( _avatarID, leaderAvatarID ) );
    }

    void ValeAvatarAgent::leaveParty()
    {
        if ( _pServer != nullptr )
            _pServer->reportAction( *this, _pServer->getWorld().leaveParty( _avatarID ) );
    }

    void ValeAvatarAgent::receiveProgress( uint32 avatarID, int32 level, int32 experience, int32 coins, int32 herbCount, int32 defeatCount, int32 partySize, uint32 inviteFromID )
    {
        _avatarID            = avatarID;
        _shownLevel          = level;
        _shownExperience     = experience;
        _shownCoins          = coins;
        _shownHerbCount      = herbCount;
        _shownDefeatCount    = defeatCount;
        _shownPartySize      = partySize;
        _pendingInviteFromID = inviteFromID;
        SW_LOG_INFO_ID( "LanternVale.Progress", "[LanternVale] progress avatar=%# level=%# experience=%# coins=%# herbs=%# defeats=%# party=%# invite=%#", avatarID, level, experience, coins,
                        herbCount, defeatCount, partySize, inviteFromID );
    }

    void ValeAvatarAgent::receiveActionResult( ValeActionResult result )
    {
        _lastResult = result;
        ++_resultCount;
    }

    // ------------------------------------------------------------------------------
    // ValeServerLink
    // ------------------------------------------------------------------------------
    ValeServerLink::ValeServerLink()
        : _world{}
        , _profiles{}
        , _pStore{}
        , _remoteCalls{}
        , _grid{}
        , _replicator{}
        , _mapAgent{}
        , _mapPendingAccount{}
        , _listLoadScratch{}
        , _listStateScratch{}
        , _listSubscription{}
        , _stats{}
        , _pPeers{ nullptr }
        , _autosaveTimer{ 0.0f }
    {
    }

    ValeServerLink::~ValeServerLink()
    {
        shutdown();
    }

    bool ValeServerLink::initialize( INetPeers& peers, NetMessageRouter& router, const ValeRules& rules, const StreamingCellGridDef& gridDef, unique_ptr<ILocalStore> pStore )
    {
        if ( pStore == nullptr || _world.initialize( rules ) == false )
            return false;
        if ( StreamingCellGrid::tryCreate( gridDef, _grid ) == false )
        {
            SW_LOG_ERROR_ID( "LanternVale.InterestGridInvalid", "[LanternVale] the interest grid (cell %# m, loading radius %# m) cannot be built", gridDef._cellSize, gridDef._loadingRadius );
            return false;
        }
        if ( _replicator.initialize( &peers, MMOReplicatorConfig{}, _grid ) == false )
            return false;
        _remoteCalls.initialize( &peers );
        _listSubscription.push_back( router.registerHandler( &_remoteCalls ) );
        _listSubscription.push_back( router.registerHandler( &_replicator ) );
        _listSubscription.push_back( router.registerHandler( this ) );
        for ( const Subscription& subscription : _listSubscription )
        {
            if ( subscription.isActive() )
                continue;
            SW_LOG_ERROR_ID( "LanternVale.ServerRouterTaken", "[LanternVale] a message range of the vale server is already handled - the server link does not start" );
            _listSubscription.clear();
            return false;
        }
        _pStore = std::move( pStore );
        _profiles.bindStore( _pStore.get() );
        _pPeers        = &peers;
        _autosaveTimer = rules._autosaveSeconds;
        // 핫 리로드로 다시 선 링크는 이미 붙은 연결의 열림 사건을 받지 못한다(`GameNetSession` 경고) — 붙어 있는 연결을 들인다.
        vector<int32> listConnection;
        peers.collectConnected( listConnection );
        for ( const int32 connectionID : listConnection )
        {
            onConnectionOpened( connectionID );
        }
        SW_LOG_INFO_ID( "LanternVale.ServerStarted", "[LanternVale] vale server started - %# creatures and herbs", static_cast<uint32>( _world.getPlacements().size() ) );
        return true;
    }

    void ValeServerLink::shutdown()
    {
        if ( _pPeers == nullptr )
            return;
        saveAll();
        vector<int32> listConnection;
        for ( const auto& entry : _mapAgent )
        {
            listConnection.push_back( entry.first );
        }
        for ( const int32 connectionID : listConnection )
        {
            _remoteCalls.unregisterSessionTarget( connectionID );
            _world.removeAvatar( _mapAgent[connectionID]->_avatarID );
        }
        _mapAgent.clear();
        _mapPendingAccount.clear();
        _listSubscription.clear();
        if ( _pStore != nullptr )
        {
            _pStore->shutdown(); // 남은 쓰기를 끝까지 마친다
            _listLoadScratch.clear();
            _profiles.poll( _listLoadScratch ); // 쓰기 결과를 로그로 남긴다
        }
        _profiles.bindStore( nullptr );
        _pStore.reset();
        _pPeers = nullptr;
    }

    void ValeServerLink::update( float32 deltaSeconds )
    {
        if ( _pPeers == nullptr )
            return;
        _world.tick( deltaSeconds );
        _listLoadScratch.clear();
        _profiles.poll( _listLoadScratch );
        for ( const ValeProfileLoad& load : _listLoadScratch )
        {
            completeAdmission( load );
        }
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

    void ValeServerLink::acceptConnection( int32 connectionID, uint64 accountID )
    {
        // 이미 들였거나 읽는 중인 연결은 다시 들이지 않는다(열림 사건과 핫 리로드의 `collectConnected` 가 겹칠 수 있다).
        const bool bKnownConnection = _mapAgent.find( connectionID ) != _mapAgent.end() || _mapPendingAccount.find( connectionID ) != _mapPendingAccount.end();
        if ( _pPeers == nullptr || bKnownConnection )
            return;
        if ( accountID == 0 )
        {
            ++_stats._refusedCount;
            SW_LOG_WARNING_ID( "LanternVale.NoAccount", "[LanternVale] connection %# has no account - the vale needs a game connect ticket (server config _bGameConnectTicket); disconnecting it",
                               connectionID );
            _pPeers->disconnect( connectionID );
            return;
        }
        // 같은 계정이 이미 들어와 있거나 읽는 중이면 앞 연결을 저장하고 끊는다 — 저장이 읽기보다 먼저 맡겨지므로 새 연결은 그 진행을 읽는다.
        int32 previousConnectionID = -1;
        for ( const auto& entry : _mapAgent )
        {
            const ValeAvatar* pAvatar = _world.findAvatar( entry.second->_avatarID );
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
            SW_LOG_INFO_ID( "LanternVale.SessionReplaced", "[LanternVale] account %# connected again on %# - the session on %# is saved and closed", accountID, connectionID, previousConnectionID );
            releaseAgent( previousConnectionID );
            _mapPendingAccount.erase( previousConnectionID );
            _pPeers->disconnect( previousConnectionID );
        }
        _mapPendingAccount[connectionID] = accountID;
        _profiles.requestLoad( accountID );
    }

    void ValeServerLink::reportAction( ValeAvatarAgent& agent, ValeActionResult result )
    {
        ++_stats._actionCount;
        (void)_remoteCalls.callSession( agent._connectionID, "receiveActionResult", { ReflectedValue::make( result ) } );
        if ( _world.consumeDirtyFlag( agent._avatarID ) )
            sendProgress( agent );
    }

    NetHandleResult ValeServerLink::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        (void)context;
        (void)body;
        return NetHandleResult::NotMine; // 마스크 0 — 사건만 받는다
    }

    void ValeServerLink::onConnectionOpened( int32 connectionID )
    {
        if ( _pPeers != nullptr )
            acceptConnection( connectionID, _pPeers->getConnectionPrincipal( connectionID ) );
    }

    void ValeServerLink::onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
    {
        (void)reason;
        _mapPendingAccount.erase( connectionID );
        releaseAgent( connectionID );
    }

    void ValeServerLink::completeAdmission( const ValeProfileLoad& load )
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
            _pPeers->disconnect( connectionID );
            return;
        }
        ValeProfile profile = load._profile;
        if ( load._bFound == SW_FALSE )
            profile._position = _world.getRules()._avatarSpawn; // 새 계정은 나는 자리에서 시작한다
        unique_ptr<ValeAvatarAgent> pAgent = make_unique<ValeAvatarAgent>();
        pAgent->_pServer                   = this;
        pAgent->_connectionID              = connectionID;
        pAgent->_avatarID                  = _world.addAvatar( load._accountID, profile );
        if ( _remoteCalls.registerSessionTarget( connectionID, *ValeAvatarAgent::StaticType(), pAgent.get() ) == false )
        {
            _world.removeAvatar( pAgent->_avatarID );
            ++_stats._refusedCount;
            _pPeers->disconnect( connectionID );
            return;
        }
        _replicator.setObserver( connectionID, pAgent->_avatarID );
        ++_stats._admittedCount;
        SW_LOG_INFO_ID( "LanternVale.Admitted", "[LanternVale] account %# entered the vale as avatar %# on connection %# (level %#, %#)", load._accountID, pAgent->_avatarID, connectionID,
                        profile._level, load._bFound != SW_FALSE ? "stored progress" : "new account" );
        sendProgress( *pAgent );
        (void)_world.consumeDirtyFlag( pAgent->_avatarID );
        _mapAgent[connectionID] = std::move( pAgent );
    }

    void ValeServerLink::releaseAgent( int32 connectionID )
    {
        const auto iter = _mapAgent.find( connectionID );
        if ( iter == _mapAgent.end() )
            return;
        _remoteCalls.unregisterSessionTarget( connectionID );
        _replicator.removeObserver( connectionID );
        const ValeAvatar* pAvatar = _world.findAvatar( iter->second->_avatarID );
        if ( pAvatar != nullptr )
            _profiles.requestSave( pAvatar->_accountID, pAvatar->_profile );
        _replicator.removeEntity( iter->second->_avatarID );
        _world.removeAvatar( iter->second->_avatarID );
        _mapAgent.erase( iter );
    }

    void ValeServerLink::sendProgress( const ValeAvatarAgent& agent )
    {
        const ValeAvatar* pAvatar = _world.findAvatar( agent._avatarID );
        if ( pAvatar == nullptr )
            return;
        const ValeProfile& profile = pAvatar->_profile;
        (void)_remoteCalls.callSession( agent._connectionID, "receiveProgress",
                                        { ReflectedValue::make( pAvatar->_avatarID ), ReflectedValue::make( profile._level ), ReflectedValue::make( profile._experience ),
                                          ReflectedValue::make( profile._coins ), ReflectedValue::make( profile._herbCount ), ReflectedValue::make( profile._defeatCount ),
                                          ReflectedValue::make( _world.getPartySize( pAvatar->_avatarID ) ), ReflectedValue::make( pAvatar->_inviteFromID ) } );
    }

    void ValeServerLink::saveAll()
    {
        _world.forEachAvatar( [this]( const ValeAvatar& avatar )
        {
            _profiles.requestSave( avatar._accountID, avatar._profile );
        } );
    }

    void ValeServerLink::publishEntities()
    {
        // 상태 바이트 — 갈래(varuint) · 값(varint: 아바타는 레벨, 짐승은 체력, 버섯은 1). 위치는 엔티티 칸이 싣는다.
        const auto writeState = [this]( uint32 typeID, int32 value )
        {
            BitWriter writer( std::move( _listStateScratch ) );
            writer.writeVarUint( typeID );
            writer.writeVarInt( value );
            _listStateScratch = writer.releaseBytes();
        };
        _world.forEachAvatar( [this, &writeState]( const ValeAvatar& avatar )
        {
            writeState( kAvatarTypeID, avatar._profile._level );
            MMOEntity entity;
            entity._listState  = _listStateScratch;
            entity._position   = avatar._profile._position;
            entity._entityID   = avatar._avatarID;
            entity._typeID     = kAvatarTypeID;
            entity._importance = 2.0f; // 다른 플레이어는 짐승보다 먼저 보인다
            _replicator.setEntity( entity );
        } );
        for ( const ValePlacementState& spawn : _world.getPlacements() )
        {
            if ( spawn._bPresent == SW_FALSE )
            {
                _replicator.removeEntity( spawn._placementID );
                continue;
            }
            const uint32 typeID = spawn._def._kind == ValePlacementKind::Creature ? kCreatureTypeID : kHerbTypeID;
            writeState( typeID, spawn._health );
            MMOEntity entity;
            entity._listState = _listStateScratch;
            entity._position  = spawn._def._position;
            entity._entityID  = spawn._placementID;
            entity._typeID    = typeID;
            _replicator.setEntity( entity );
        }
    }

    // ------------------------------------------------------------------------------
    // ValeClientLink
    // ------------------------------------------------------------------------------
    ValeClientLink::ValeClientLink()
        : _agent{}
        , _remoteCalls{}
        , _view{}
        , _listRejectScratch{}
        , _listSubscription{}
        , _lastResultNanoseconds{ 0 }
        , _seenResultCount{ 0 }
        , _bActive{ SW_FALSE }
    {
    }

    bool ValeClientLink::initialize( INetPeers& peers, NetMessageRouter& router )
    {
        _remoteCalls.initialize( &peers );
        if ( _remoteCalls.registerTarget( RemoteCallTargets::kSessionTargetID, *ValeAvatarAgent::StaticType(), &_agent ) == false )
            return false;
        _listSubscription.push_back( router.registerHandler( &_remoteCalls ) );
        _listSubscription.push_back( router.registerHandler( &_view ) );
        for ( const Subscription& subscription : _listSubscription )
        {
            if ( subscription.isActive() )
                continue;
            SW_LOG_ERROR_ID( "LanternVale.ClientRouterTaken", "[LanternVale] a message range of the vale client is already handled - the client link does not start" );
            _listSubscription.clear();
            return false;
        }
        _bActive = SW_TRUE;
        return true;
    }

    void ValeClientLink::shutdown()
    {
        _listSubscription.clear();
        _remoteCalls.unregisterTarget( RemoteCallTargets::kSessionTargetID );
        _bActive = SW_FALSE;
    }

    void ValeClientLink::update()
    {
        if ( _bActive == SW_FALSE )
            return;
        if ( _agent._resultCount != _seenResultCount )
        {
            _seenResultCount       = _agent._resultCount;
            _lastResultNanoseconds = MonotonicClock::nowNanoseconds();
        }
        _listRejectScratch.clear();
        _remoteCalls.pollRejections( _listRejectScratch );
        for ( const RemoteCallRejectNotification& notification : _listRejectScratch )
        {
            SW_LOG_WARNING_ID( "LanternVale.CallRejected", "[LanternVale] the server did not take call %# (%#)", notification._sequence, static_cast<int32>( notification._error ) );
        }
        vector<MMOClientEvent> listEvent;
        _view.pollEvents( listEvent ); // 보이는 목록은 `getView` 가 읽는다 — 알림은 쌓이지 않게 비운다
    }

    uint32 ValeClientLink::callServerFunction( string_view functionName, const vector<ReflectedValue>& listArg )
    {
        if ( _bActive == SW_FALSE )
            return 0;
        return _remoteCalls.callServer( RemoteCallTargets::kSessionTargetID, *ValeAvatarAgent::StaticType(), functionName, listArg );
    }
} // namespace sw
