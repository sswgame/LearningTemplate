#include "pch.h"

#include "Games/Playground/Hub/HubNetLink.h"

#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // HubAvatarAgent — `Server` 함수 몸은 서버가 건 허브에 넘겨 판정을 알리고, `Client` 함수 몸은 받은 값을 적는다
    // ------------------------------------------------------------------------------
    void HubAvatarAgent::attackCreature( uint32 creatureID )
    {
        if ( _pWorld != nullptr )
            _onAction( *this, _pWorld->attackCreature( _avatarID, creatureID ) );
    }

    bool HubAvatarAgent::validateAttackCreature( uint32 creatureID ) const
    {
        return _pWorld != nullptr && _pWorld->isPlacementOfKind( creatureID, HubPlacementKind::Creature );
    }

    void HubAvatarAgent::gatherHerb( uint32 herbID )
    {
        if ( _pWorld != nullptr )
            _onAction( *this, _pWorld->gatherHerb( _avatarID, herbID ) );
    }

    bool HubAvatarAgent::validateGatherHerb( uint32 herbID ) const
    {
        return _pWorld != nullptr && _pWorld->isPlacementOfKind( herbID, HubPlacementKind::Herb );
    }

    void HubAvatarAgent::inviteToParty( uint32 avatarID )
    {
        if ( _pWorld != nullptr )
            _onAction( *this, _pWorld->inviteToParty( _avatarID, avatarID ) );
    }

    bool HubAvatarAgent::validateInviteToParty( uint32 avatarID ) const
    {
        return HubWorld::isValidAvatarID( avatarID ) && avatarID != _avatarID;
    }

    void HubAvatarAgent::acceptPartyInvite( uint32 leaderAvatarID )
    {
        if ( _pWorld != nullptr )
            _onAction( *this, _pWorld->acceptPartyInvite( _avatarID, leaderAvatarID ) );
    }

    void HubAvatarAgent::leaveParty()
    {
        if ( _pWorld != nullptr )
            _onAction( *this, _pWorld->leaveParty( _avatarID ) );
    }

    void HubAvatarAgent::receiveProgress( uint32 avatarID, int32 level, int32 experience, int32 coins, int32 herbCount, int32 defeatCount, int32 partySize, uint32 inviteFromID )
    {
        _avatarID            = avatarID;
        _shownLevel          = level;
        _shownExperience     = experience;
        _shownCoins          = coins;
        _shownHerbCount      = herbCount;
        _shownDefeatCount    = defeatCount;
        _shownPartySize      = partySize;
        _pendingInviteFromID = inviteFromID;
        SW_LOG_INFO_ID( "Hub.Progress", "[Hub] progress avatar=%# level=%# experience=%# coins=%# herbs=%# defeats=%# party=%# invite=%#", avatarID, level, experience, coins,
                        herbCount, defeatCount, partySize, inviteFromID );
    }

    void HubAvatarAgent::receiveActionResult( HubActionResult result )
    {
        _lastResult = result;
        ++_resultCount;
    }

    // ------------------------------------------------------------------------------
    // HubClientLink
    // ------------------------------------------------------------------------------
    HubClientLink::HubClientLink()
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

    bool HubClientLink::initialize( INetPeers& peers, NetMessageRouter& router )
    {
        _remoteCalls.initialize( &peers );
        if ( _remoteCalls.registerTarget( RemoteCallTargets::kSessionTargetID, *HubAvatarAgent::StaticType(), &_agent ) == false )
            return false;
        _listSubscription.push_back( router.registerHandler( &_remoteCalls ) );
        _listSubscription.push_back( router.registerHandler( &_view ) );
        for ( const Subscription& subscription : _listSubscription )
        {
            if ( subscription.isActive() )
                continue;
            SW_LOG_ERROR_ID( "Hub.ClientRouterTaken", "[Hub] a message range of the hub client is already handled - the client link does not start" );
            _listSubscription.clear();
            return false;
        }
        _bActive = SW_TRUE;
        return true;
    }

    void HubClientLink::shutdown()
    {
        _listSubscription.clear();
        _remoteCalls.unregisterTarget( RemoteCallTargets::kSessionTargetID );
        _bActive = SW_FALSE;
    }

    void HubClientLink::update()
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
            SW_LOG_WARNING_ID( "Hub.CallRejected", "[Hub] the server did not take call %# (%#)", notification._sequence, static_cast<int32>( notification._error ) );
        }
        vector<MMOClientEvent> listEvent;
        _view.pollEvents( listEvent ); // 보이는 목록은 `getView` 가 읽는다 — 알림은 쌓이지 않게 비운다
    }

    uint32 HubClientLink::callServerFunction( string_view functionName, const vector<ReflectedValue>& listArg )
    {
        if ( _bActive == SW_FALSE )
            return 0;
        return _remoteCalls.callServer( RemoteCallTargets::kSessionTargetID, *HubAvatarAgent::StaticType(), functionName, listArg );
    }
} // namespace sw
