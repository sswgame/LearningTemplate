#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BattleRoyaleMatch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BattleRoyaleCatalog.h"
#include "GameFramework/Runtime/Gameplay/Match/TeamAttitude.h"

#include <algorithm>

namespace sw
{
    BattleRoyaleMatch::BattleRoyaleMatch()
        : _listPlayer{}
        , _listSupplyDrop{}
        , _eventBuffer{}
        , _matchState{}
        , _zone{}
        , _random{}
        , _stepTimer{ kFixedStep, 1.0f }
        , _pCatalog{ nullptr }
        , _pItemCatalog{ nullptr }
        , _pLootCatalog{ nullptr }
        , _time{ 0.0f }
        , _nextSupplyDrop{ 0 }
    {
    }

    bool BattleRoyaleMatch::initialize( const BattleRoyaleCatalog* pCatalog, const ItemCatalog* pItemCatalog, const LootCatalog* pLootCatalog, uint32 seed,
                                        const IBrZoneTerrain* pTerrain )
    {
        _pCatalog     = pCatalog;
        _pItemCatalog = pItemCatalog;
        _pLootCatalog = pLootCatalog;
        _listPlayer.clear();
        _listSupplyDrop.clear();
        _eventBuffer.clear();
        _random.setSeed( seed );
        _stepTimer.reset();
        _time           = 0.0f;
        _nextSupplyDrop = 0;

        MatchSettings settings;
        settings._bRespawn              = SW_FALSE;
        settings._bLastTeamStandingWins = SW_TRUE;
        settings._scorePerKill          = 0;
        _matchState.initialize( settings );
        if ( pCatalog != nullptr )
            return _zone.initialize( pCatalog->getZoneSettings(), pCatalog->getMapSize(), HashUtil::mix32( seed ^ 0x5a4f4e45u ), pTerrain );
        return true;
    }

    int32 BattleRoyaleMatch::addTeam( const hashed_string& name )
    {
        return _matchState.addTeam( name );
    }

    expected<int32> BattleRoyaleMatch::addPlayer( int32 team )
    {
        if ( _pCatalog == nullptr || _matchState.findTeam( team ) == nullptr )
            return unexpected( Error{ ErrorKind::InvalidArgument, "no catalog or the team does not exist" } );
        // 사람을 먼저 만들어 보고, 설정이 거절되면 판에 참가자를 더하지 않는다.
        BattleRoyalePlayer player;
        const int32        participant = _matchState.getParticipantCount();
        if ( initializePlayer( player, team, participant ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the catalog player health settings are out of range" } );
        const int32 added = _matchState.addParticipant( team, hashed_string{} ).value(); // 팀은 위에서 확인했다
        SW_ASSERT( added == participant );
        _listPlayer.push_back( std::move( player ) );
        return added;
    }

    bool BattleRoyaleMatch::initializePlayer( BattleRoyalePlayer& outPlayer, int32 team, int32 participant ) const
    {
        const BattleRoyalePlayerDef& playerSettings = _pCatalog->getPlayerSettings();
        BattleRoyalePlayer&          player         = outPlayer;
        player._team                                = team;

        HealthSettings healthMachine;
        healthMachine._maxHealth               = playerSettings._maxHealth;
        healthMachine._downedHealth            = playerSettings._downedHealth;
        healthMachine._bleedoutRate            = playerSettings._bleedoutRate;
        healthMachine._reviveTime              = playerSettings._reviveTime;
        healthMachine._reviveHealthRatio       = playerSettings._reviveHealthRatio;
        healthMachine._invulnerableAfterRevive = 0.0f;
        healthMachine._bDownedEnabled          = SW_TRUE;
        healthMachine._bDamageInterruptsRevive = SW_FALSE; // 부활이 끊기는 조건은 이 키트가 정한다(자기장 피해는 부활을 끊지 않는다)
        if ( player._healthMachine.initialize( healthMachine ) == false )
            return false;

        InteractionConfig revive;
        revive._duration          = playerSettings._reviveTime;
        revive._maxParticipants   = playerSettings._maxRevivers;
        revive._bResetOnInterrupt = SW_TRUE;
        if ( player._revive.initialize( revive, nullptr, static_cast<uint32>( participant + 1 ) ) == false )
            return false;

        player._loadout.initialize( _pCatalog );
        return player._inventory.initialize( _pItemCatalog, playerSettings._slotCount, player._loadout.computeCarryLimit() );
    }

    void BattleRoyaleMatch::start()
    {
        _matchState.start();
        collectMatchEvents();
        pushEvent( BattleRoyaleEvent::Kind::PlayersRemaining, nullopt, nullopt, nullopt, countRemainingPlayers() );
    }

    void BattleRoyaleMatch::update( float32 deltaTime )
    {
        const int32 stepCount = _stepTimer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepFixed( _stepTimer.getStep() );
        }
    }

    void BattleRoyaleMatch::setPlayerPosition( int32 player, const float2& position )
    {
        if ( isValidPlayer( player ) )
            _listPlayer[static_cast<size_t>( player )]._position = position;
    }

    float32 BattleRoyaleMatch::applyDamage( int32 victim, int32 attacker, float32 damage, BattleRoyaleHitZone hitZone )
    {
        const bool bDamageRejected = isValidPlayer( victim ) == false || ( damage > 0.0f ) == false || _matchState.getPhase() != MatchPhase::InProgress;
        if ( bDamageRejected )
            return 0.0f;
        // 없는 사람은 환경이다. 그 번호가 `_downedBy` 에 남으면 세이브를 읽을 수 없다.
        const optional<int32> instigator = isValidPlayer( attacker ) ? optional<int32>( attacker ) : nullopt;
        BattleRoyalePlayer&   target     = _listPlayer[static_cast<size_t>( victim )];
        if ( target.isDead() )
            return 0.0f;
        const bool bFriendlyFire = isValidPlayer( attacker ) && attacker != victim && TeamAttitudeUtil::isFriendly( _listPlayer[static_cast<size_t>( attacker )]._team, target._team );
        if ( bFriendlyFire )
            return 0.0f;

        float32 bodyDamage = damage;
        if ( target.isAlive() )
        {
            const BattleRoyaleArmorResult armor = target._loadout.absorbDamage( damage, hitZone );
            bodyDamage                          = armor._damage;
            if ( armor._bBroken == SW_TRUE )
                pushEvent( BattleRoyaleEvent::Kind::ArmorBroken, victim, instigator, target._team, static_cast<int32>( hitZone ) );
        }
        else if ( target._revive.getParticipantCount() > 0 && isValidPlayer( attacker ) )
        {
            interruptRevive( victim, true ); // 기절한 사람이 적에게 맞으면 부활이 처음부터 다시 시작된다
        }
        const HealthDamageResult result = target._healthMachine.applyDamage( bodyDamage, 0.0f, instigator );
        const float32            dealt  = result._shieldAbsorbed + result._healthDamage;
        if ( isValidPlayer( attacker ) && dealt > 0.0f )
            _matchState.reportDamage( attacker, victim, dealt );
        processHealthMachine( victim );
        return dealt;
    }

    bool BattleRoyaleMatch::beginRevive( int32 reviver, int32 target )
    {
        const bool bReviveRejected = isValidPlayer( reviver ) == false || isValidPlayer( target ) == false || reviver == target;
        if ( bReviveRejected )
            return false;
        BattleRoyalePlayer& helper = _listPlayer[static_cast<size_t>( reviver )];
        BattleRoyalePlayer& downed = _listPlayer[static_cast<size_t>( target )];
        const bool          bValid = TeamAttitudeUtil::isFriendly( helper._team, downed._team ) && helper.isAlive() && downed.isDowned() && helper._revivingTarget < 0;
        if ( bValid == false || downed._revive.join( static_cast<uint32>( reviver ) ) == false )
            return false;
        helper._revivingTarget = target;
        if ( downed._revive.getParticipantCount() == 1 )
        {
            // 살리는 동안 출혈을 멈춘다. 진행은 InteractionProgress 가 가지고 있고, HealthStateMachine 의 부활 시계는 배율 0으로 멈춰 둔다.
            (void)downed._healthMachine.startRevive( reviver, 0.0f );
            pushEvent( BattleRoyaleEvent::Kind::ReviveStarted, target, reviver, downed._team, 0 );
        }
        return true;
    }

    void BattleRoyaleMatch::endRevive( int32 reviver )
    {
        if ( isValidPlayer( reviver ) == false )
            return;
        BattleRoyalePlayer& helper = _listPlayer[static_cast<size_t>( reviver )];
        const int32         target = helper._revivingTarget;
        helper._revivingTarget     = constant::invalid_index::kInt32;
        if ( isValidPlayer( target ) == false )
            return;
        BattleRoyalePlayer& downed = _listPlayer[static_cast<size_t>( target )];
        (void)downed._revive.leave( static_cast<uint32>( reviver ) );
        if ( downed._revive.getParticipantCount() == 0 )
            downed._healthMachine.stopRevive();
    }

    void BattleRoyaleMatch::pollEvents( vector<BattleRoyaleEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    const BattleRoyalePlayer* BattleRoyaleMatch::findPlayer( int32 player ) const
    {
        return isValidPlayer( player ) ? &_listPlayer[static_cast<size_t>( player )] : nullptr;
    }

    BattleRoyalePlayer* BattleRoyaleMatch::findPlayerMutable( int32 player )
    {
        return isValidPlayer( player ) ? &_listPlayer[static_cast<size_t>( player )] : nullptr;
    }

    int32 BattleRoyaleMatch::countRemainingPlayers() const
    {
        int32 count = 0;
        for ( const BattleRoyalePlayer& player : _listPlayer )
        {
            count += player.isDead() ? 0 : 1;
        }
        return count;
    }

    void BattleRoyaleMatch::stepFixed( float32 deltaTime )
    {
        if ( _matchState.getPhase() != MatchPhase::InProgress )
            return;
        _time += deltaTime;
        _matchState.update( deltaTime );
        _zone.update( deltaTime );

        // 판이 끝난 틱에는 남은 사람을 더 깎지 않는다. 이긴 팀이 끝난 뒤에 죽는 킬 피드가 나오면 안 된다.
        // 한 틱에 함께 죽을 때 먼저 쓰러지는 사람이 지는 사람이다. 입장 순서가 아니라 체력이 적은 쪽부터 깎는다(같으면 입장 순서 - 결정적이다).
        vector<int32> listZoneOrder;
        for ( size_t index = 0; index < _listPlayer.size(); ++index )
        {
            listZoneOrder.push_back( static_cast<int32>( index ) );
        }
        std::stable_sort( listZoneOrder.begin(), listZoneOrder.end(), [this]( int32 left, int32 right )
        {
            const BattleRoyalePlayer& leftPlayer  = _listPlayer[static_cast<size_t>( left )];
            const BattleRoyalePlayer& rightPlayer = _listPlayer[static_cast<size_t>( right )];
            if ( leftPlayer.isDowned() != rightPlayer.isDowned() )
                return leftPlayer.isDowned();
            return leftPlayer.isDowned() ? leftPlayer._healthMachine.getDownedHealth() < rightPlayer._healthMachine.getDownedHealth() : leftPlayer._healthMachine.getHealth() < rightPlayer._healthMachine.getHealth();
        } );
        for ( size_t order = 0; order < listZoneOrder.size() && _matchState.getPhase() == MatchPhase::InProgress; ++order )
        {
            BattleRoyalePlayer& player = _listPlayer[static_cast<size_t>( listZoneOrder[order] )];
            if ( player.isDead() )
                continue;
            const float32 zoneDamage = _zone.computeDamagePerSecond( player._position ) * deltaTime;
            if ( zoneDamage > 0.0f )
            {
                (void)player._healthMachine.applyDamage( zoneDamage, 0.0f, nullopt ); // 결과는 processHealthMachine 가 사건으로 읽는다
                processHealthMachine( listZoneOrder[order] );
            }
        }
        for ( size_t index = 0; index < _listPlayer.size() && _matchState.getPhase() == MatchPhase::InProgress; ++index )
        {
            BattleRoyalePlayer& player = _listPlayer[index];
            if ( player.isDead() )
                continue;
            player._healthMachine.update( deltaTime );
            processHealthMachine( static_cast<int32>( index ) );
            if ( player.isDowned() && player._revive.getParticipantCount() > 0 )
            {
                player._revive.update( deltaTime );
                if ( player._revive.isCompleted() )
                    finishRevive( static_cast<int32>( index ) );
            }
        }
        if ( _pCatalog != nullptr )
        {
            const vector<float32>& listTime = _pCatalog->getSupplyDropSettings()._listTime;
            while ( _nextSupplyDrop < static_cast<int32>( listTime.size() ) && _time >= listTime[static_cast<size_t>( _nextSupplyDrop )] )
            {
                dropSupply();
                ++_nextSupplyDrop;
            }
        }
        collectMatchEvents();
    }

    void BattleRoyaleMatch::processHealthMachine( int32 player )
    {
        BattleRoyalePlayer& target = _listPlayer[static_cast<size_t>( player )];
        // 지역 목록에 복사해 둔다. 처리 중에 다른 사람의 알림을 다시 꺼내기 때문이다(팀 전멸).
        vector<HealthEvent> listEvent;
        target._healthMachine.pollEvents( listEvent );
        for ( const HealthEvent& event : listEvent )
        {
            if ( event._type == HealthEventType::Downed )
                handleDowned( player, event._instigatorID );
            else if ( event._type == HealthEventType::Died )
                handleDeath( player, event._instigatorID );
        }
    }

    void BattleRoyaleMatch::handleDowned( int32 player, optional<int32> instigator )
    {
        BattleRoyalePlayer& target = _listPlayer[static_cast<size_t>( player )];
        target._downedBy           = instigator.value_or( constant::invalid_index::kInt32 ); // 상태 바이트 칸이라 없으면 -1
        target._revive.reset();
        endRevive( player ); // 살리던 사람이 기절하면 그만둔다
        // 처치(`handleDeath`)와 같은 규칙이다. 자기 자신을 기절시킨 것은 세지 않는다.
        if ( instigator.has_value() && isValidPlayer( *instigator ) && TeamAttitudeUtil::isHostile( _listPlayer[static_cast<size_t>( *instigator )]._team, target._team ) )
            ++_listPlayer[static_cast<size_t>( *instigator )]._knocks;
        pushEvent( BattleRoyaleEvent::Kind::PlayerDowned, player, instigator, target._team, 0 );
        resolveTeamWipe( target._team );
    }

    void BattleRoyaleMatch::handleDeath( int32 player, optional<int32> instigator )
    {
        BattleRoyalePlayer& target = _listPlayer[static_cast<size_t>( player )];
        // 기절한 뒤의 죽음(출혈, 자기장, 전멸)은 기절시킨 사람의 처치로 센다.
        const optional<int32> killer = instigator.has_value() && isValidPlayer( *instigator ) ? instigator : target.findDownedBy();
        endRevive( player );
        for ( BattleRoyalePlayer& other : _listPlayer )
        {
            if ( other._revivingTarget == player )
                other._revivingTarget = constant::invalid_index::kInt32;
        }
        target._revive.reset();
        if ( killer.has_value() && isValidPlayer( *killer ) && TeamAttitudeUtil::isHostile( _listPlayer[static_cast<size_t>( *killer )]._team, target._team ) )
            ++_listPlayer[static_cast<size_t>( *killer )]._kills;
        _matchState.reportKill( player, killer );
        const int32 remaining = countRemainingPlayers();
        pushEvent( BattleRoyaleEvent::Kind::PlayerKilled, player, killer, target._team, remaining );
        pushEvent( BattleRoyaleEvent::Kind::PlayersRemaining, nullopt, nullopt, nullopt, remaining );
        collectMatchEvents();
        resolveTeamWipe( target._team );
    }

    void BattleRoyaleMatch::interruptRevive( int32 target, bool bNotify )
    {
        BattleRoyalePlayer& downed = _listPlayer[static_cast<size_t>( target )];
        for ( BattleRoyalePlayer& other : _listPlayer )
        {
            if ( other._revivingTarget == target )
                other._revivingTarget = constant::invalid_index::kInt32;
        }
        downed._revive.reset();
        downed._healthMachine.stopRevive();
        if ( bNotify )
            pushEvent( BattleRoyaleEvent::Kind::ReviveInterrupted, target, nullopt, downed._team, 0 );
    }

    void BattleRoyaleMatch::finishRevive( int32 target )
    {
        BattleRoyalePlayer& downed = _listPlayer[static_cast<size_t>( target )];
        optional<int32>     reviver{};
        for ( size_t index = 0; index < _listPlayer.size(); ++index )
        {
            if ( _listPlayer[index]._revivingTarget == target )
            {
                if ( reviver.has_value() == false )
                    reviver = static_cast<int32>( index );
                _listPlayer[index]._revivingTarget = constant::invalid_index::kInt32;
            }
        }
        // 진행이 다 찼다. 멈춰 둔 HealthStateMachine 의 부활 시계를 한 번에 끝까지 돌린다.
        (void)downed._healthMachine.startRevive( reviver.value_or( constant::invalid_index::kInt32 ), 1.0f ); // 살린 쪽은 체력 상태 바이트 칸이라 없으면 -1
        downed._healthMachine.update( downed._healthMachine.getSettings()._reviveTime );
        downed._healthMachine.discardEvents();
        downed._revive.reset();
        downed._downedBy = constant::invalid_index::kInt32;
        pushEvent( BattleRoyaleEvent::Kind::PlayerRevived, target, reviver, downed._team, 0 );
    }

    void BattleRoyaleMatch::resolveTeamWipe( int32 team )
    {
        for ( const BattleRoyalePlayer& player : _listPlayer )
        {
            if ( player._team == team && player.isAlive() )
                return;
        }
        // 서 있는 팀원이 없으면 기절한 팀원은 모두 죽는다.
        for ( size_t index = 0; index < _listPlayer.size(); ++index )
        {
            BattleRoyalePlayer& player = _listPlayer[index];
            if ( player._team != team || player.isDowned() == false )
                continue;
            player._healthMachine.kill( player.findDownedBy() );
            processHealthMachine( static_cast<int32>( index ) );
        }
    }

    void BattleRoyaleMatch::dropSupply()
    {
        BattleRoyaleSupplyDrop drop;
        drop._time = _time;
        (void)_zone.pickPointInside( _random, drop._position );
        if ( _pLootCatalog != nullptr )
            (void)BattleRoyaleGroundItem::rollTableAt( *_pLootCatalog, _pCatalog->getSupplyDropSettings()._tableID, drop._position, constant::invalid_index::kInt32, 1, _random, drop._listItem );
        _listSupplyDrop.push_back( drop );
        BattleRoyaleEvent event;
        event._kind     = BattleRoyaleEvent::Kind::SupplyDropLanded;
        event._value    = static_cast<int32>( _listSupplyDrop.size() ) - 1;
        event._position = drop._position;
        _eventBuffer.push( event );
    }

    void BattleRoyaleMatch::collectMatchEvents()
    {
        for ( const MatchEvent& event : _matchState.takeEvents() )
        {
            if ( event._kind == MatchEvent::Kind::TeamEliminated )
                pushEvent( BattleRoyaleEvent::Kind::TeamEliminated, nullopt, nullopt, event._team, event._value );
            else if ( event._kind == MatchEvent::Kind::MatchEnded )
                pushEvent( BattleRoyaleEvent::Kind::MatchEnded, nullopt, nullopt, event._team, 0 );
        }
    }

    void BattleRoyaleMatch::pushEvent( BattleRoyaleEvent::Kind kind, optional<int32> player, optional<int32> other, optional<int32> team, int32 value )
    {
        BattleRoyaleEvent event;
        event._kind   = kind;
        event._player = player;
        event._other  = other;
        event._team   = team;
        event._value  = value;
        if ( player.has_value() && isValidPlayer( *player ) )
            event._position = _listPlayer[static_cast<size_t>( *player )]._position;
        _eventBuffer.push( event );
    }

    void BattleRoyaleMatch::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        io.value( _matchState );
        io.value( _zone );
        io.value( _random );
        io.value( _stepTimer );
        io.value( _time );
        io.value( _nextSupplyDrop );
        io.check( 0.0f <= _time && 0 <= _nextSupplyDrop );
        uint32 playerCount = static_cast<uint32>( _listPlayer.size() );
        // 사람마다 팀(4) + 부활 수(4) + 진행(4) + 위치(8) + 정수 4개(16) 바이트 이상
        io.count( 36, playerCount );
        if ( io.check( playerCount == static_cast<uint32>( _matchState.getParticipantCount() ) ) == false )
            return;
        if ( io.isReading() )
        {
            _listPlayer.clear();
            _listPlayer.resize( playerCount );
            _eventBuffer.clear();
        }
        const int32 playerLimit = static_cast<int32>( playerCount );
        for ( size_t index = 0; index < _listPlayer.size(); ++index )
        {
            BattleRoyalePlayer& player = _listPlayer[index];
            int32               team   = player._team;
            io.value( team );
            if ( io.isReading() && io.check( _matchState.findTeam( team ) != nullptr && initializePlayer( player, team, static_cast<int32>( index ) ) ) == false )
                return;
            io.value( player._healthMachine );
            io.value( player._revive ); // 부활 진행(붙은 팀원과 붙은 순서, 진행량, 시계)
            if ( io.isOk() == false )
                return;
            if ( io.isReading() )
            {
                // 붙은 팀원은 모두 이 판의 사람 번호여야 한다.
                int32 knownReviverCount = 0;
                for ( uint32 reviver = 0; reviver < playerCount; ++reviver )
                {
                    knownReviverCount += player._revive.hasParticipant( reviver ) ? 1 : 0;
                }
                if ( io.check( knownReviverCount == player._revive.getParticipantCount() ) == false )
                    return;
            }
            io.value( player._inventory );
            io.value( player._loadout );
            io.value( player._position );
            io.value( player._kills );
            io.value( player._knocks );
            io.value( player._downedBy );
            io.value( player._revivingTarget );
            if ( io.check( 0 <= player._kills && 0 <= player._knocks && -1 <= player._downedBy && player._downedBy < playerLimit && -1 <= player._revivingTarget && player._revivingTarget < playerLimit ) == false )
                return;
        }
        // 상자마다 무더기 수(4) + 위치(8) + 시각(4) 바이트 이상
        io.list( _listSupplyDrop, 16, [&]( BattleRoyaleSupplyDrop& drop )
        {
            // 무더기마다 위치(8) + 이름(4) + 개수(4) + 지점(4) 바이트
            io.list( drop._listItem, 20, [&]( BattleRoyaleGroundItem& item )
            {
                io.value( item._position );
                io.value( item._itemID );
                io.value( item._count );
                io.value( item._spotIndex );
                io.check( item._count > 0 );
            } );
            io.value( drop._position );
            io.value( drop._time );
            io.check( 0.0f <= drop._time );
        } );
    }

} // namespace sw
