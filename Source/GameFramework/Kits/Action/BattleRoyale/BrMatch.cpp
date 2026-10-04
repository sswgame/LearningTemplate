#include "pch.h"

#include "GameFramework/Kits/Action/BattleRoyale/BrMatch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Action/BattleRoyale/BrCatalog.h"

namespace sw
{
    BrMatch::BrMatch()
        : _listPlayer{}
        , _listSupplyDrop{}
        , _eventBuffer{}
        , _listMatchScratch{}
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

    void BrMatch::initialize( const BrCatalog* pCatalog, const ItemCatalog* pItemCatalog, const LootCatalog* pLootCatalog, uint32 seed,
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
            _zone.initialize( pCatalog->getZoneSettings(), pCatalog->getMapSize(), GameHash::mix32( seed ^ 0x5a4f4e45u ), pTerrain );
    }

    int32 BrMatch::addTeam( const hashed_string& name ) { return _matchState.addTeam( name ); }

    int32 BrMatch::addPlayer( int32 team )
    {
        if ( _pCatalog == nullptr || _matchState.findTeam( team ) == nullptr )
            return -1;
        const BrPlayerSettings& playerSettings = _pCatalog->getPlayerSettings();
        const int32             participant    = _matchState.addParticipant( team, hashed_string{} );
        _listPlayer.emplace_back();
        BrPlayer& player = _listPlayer.back();
        player._team     = team;

        VitalitySettings vitality;
        vitality._maxHealth               = playerSettings._maxHealth;
        vitality._downedHealth            = playerSettings._downedHealth;
        vitality._bleedoutRate            = playerSettings._bleedoutRate;
        vitality._reviveTime              = playerSettings._reviveTime;
        vitality._reviveHealthRatio       = playerSettings._reviveHealthRatio;
        vitality._invulnerableAfterRevive = 0.0f;
        vitality._bDownedEnabled          = SW_TRUE;
        vitality._bDamageInterruptsRevive = SW_FALSE; // 끊기는 이 키트가 정한다(자기장 피해는 끊지 않는다)
        player._vitality.initialize( vitality );

        InteractionConfig revive;
        revive._duration          = playerSettings._reviveTime;
        revive._maxParticipants   = playerSettings._maxRevivers;
        revive._bResetOnInterrupt = SW_TRUE;
        player._revive.initialize( revive, nullptr, static_cast<uint32>( participant + 1 ) );

        player._loadout.initialize( _pCatalog );
        player._inventory.initialize( _pItemCatalog, playerSettings._slotCount, player._loadout.computeCarryLimit() );
        return participant;
    }

    void BrMatch::start()
    {
        _matchState.start();
        collectMatchEvents();
        pushEvent( BrEvent::Kind::PlayersRemaining, -1, -1, -1, countRemainingPlayers() );
    }

    void BrMatch::update( float32 deltaTime )
    {
        const int32 stepCount = _stepTimer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            stepFixed( _stepTimer.getStep() );
    }

    void BrMatch::setPlayerPosition( int32 player, const float2& position )
    {
        if ( isValidPlayer( player ) )
            _listPlayer[static_cast<size_t>( player )]._position = position;
    }

    float32 BrMatch::applyDamage( int32 victim, int32 attacker, float32 damage, BrHitZone hitZone )
    {
        if ( isValidPlayer( victim ) == false || damage <= 0.0f || _matchState.getPhase() != MatchPhase::InProgress )
            return 0.0f;
        BrPlayer& target = _listPlayer[static_cast<size_t>( victim )];
        if ( target.isDead() )
            return 0.0f;
        if ( isValidPlayer( attacker ) && attacker != victim && _listPlayer[static_cast<size_t>( attacker )]._team == target._team )
            return 0.0f;

        float32 bodyDamage = damage;
        if ( target.isAlive() )
        {
            const BrArmorResult armor = target._loadout.absorbDamage( damage, hitZone );
            bodyDamage                = armor._damage;
            if ( armor._bBroken == SW_TRUE )
                pushEvent( BrEvent::Kind::ArmorBroken, victim, attacker, target._team, static_cast<int32>( hitZone ) );
        }
        else if ( target._revive.getParticipantCount() > 0 && isValidPlayer( attacker ) )
        {
            interruptRevive( victim, true ); // 기절한 사람이 적에게 맞으면 부활이 처음부터
        }
        const VitalityDamageResult result = target._vitality.applyDamage( bodyDamage, 0.0f, attacker );
        const float32              dealt  = result._shieldAbsorbed + result._healthDamage;
        if ( isValidPlayer( attacker ) && dealt > 0.0f )
            _matchState.reportDamage( attacker, victim, dealt );
        processVitality( victim );
        return dealt;
    }

    bool BrMatch::beginRevive( int32 reviver, int32 target )
    {
        if ( isValidPlayer( reviver ) == false || isValidPlayer( target ) == false || reviver == target )
            return false;
        BrPlayer&  helper = _listPlayer[static_cast<size_t>( reviver )];
        BrPlayer&  downed = _listPlayer[static_cast<size_t>( target )];
        const bool bValid = helper._team == downed._team && helper.isAlive() && downed.isDowned() && helper._revivingTarget < 0;
        if ( bValid == false || downed._revive.join( static_cast<uint32>( reviver ) ) == false )
            return false;
        helper._revivingTarget = target;
        if ( downed._revive.getParticipantCount() == 1 )
        {
            // 살리는 동안 출혈을 멈춘다 — 진행은 InteractionProgress 가 들고, Vitality 의 부활 시계는 배율 0 으로 세워 둔다.
            (void)downed._vitality.startRevive( reviver, 0.0f );
            pushEvent( BrEvent::Kind::ReviveStarted, target, reviver, downed._team, 0 );
        }
        return true;
    }

    void BrMatch::endRevive( int32 reviver )
    {
        if ( isValidPlayer( reviver ) == false )
            return;
        BrPlayer&   helper     = _listPlayer[static_cast<size_t>( reviver )];
        const int32 target     = helper._revivingTarget;
        helper._revivingTarget = -1;
        if ( isValidPlayer( target ) == false )
            return;
        BrPlayer& downed = _listPlayer[static_cast<size_t>( target )];
        (void)downed._revive.leave( static_cast<uint32>( reviver ) );
        if ( downed._revive.getParticipantCount() == 0 )
            downed._vitality.stopRevive();
    }

    void BrMatch::drainEvents( vector<BrEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    const BrPlayer* BrMatch::findPlayer( int32 player ) const { return isValidPlayer( player ) ? &_listPlayer[static_cast<size_t>( player )] : nullptr; }

    BrPlayer* BrMatch::findPlayerMutable( int32 player ) { return isValidPlayer( player ) ? &_listPlayer[static_cast<size_t>( player )] : nullptr; }

    int32 BrMatch::countRemainingPlayers() const
    {
        int32 count = 0;
        for ( const BrPlayer& player : _listPlayer )
            count += player.isDead() ? 0 : 1;
        return count;
    }

    void BrMatch::stepFixed( float32 deltaTime )
    {
        if ( _matchState.getPhase() != MatchPhase::InProgress )
            return;
        _time += deltaTime;
        _matchState.update( deltaTime );
        _zone.update( deltaTime );

        for ( size_t index = 0; index < _listPlayer.size(); ++index )
        {
            BrPlayer& player = _listPlayer[index];
            if ( player.isDead() )
                continue;
            const float32 zoneDamage = _zone.computeDamagePerSecond( player._position ) * deltaTime;
            if ( zoneDamage > 0.0f )
            {
                (void)player._vitality.applyDamage( zoneDamage, 0.0f, -1 );
                processVitality( static_cast<int32>( index ) );
            }
        }
        for ( size_t index = 0; index < _listPlayer.size(); ++index )
        {
            BrPlayer& player = _listPlayer[index];
            if ( player.isDead() )
                continue;
            player._vitality.update( deltaTime );
            processVitality( static_cast<int32>( index ) );
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

    void BrMatch::processVitality( int32 player )
    {
        BrPlayer& target = _listPlayer[static_cast<size_t>( player )];
        // 지역 목록 — 처리 중에 다른 사람의 알림을 다시 꺼낸다(팀 전멸).
        vector<VitalityEvent> listEvent;
        target._vitality.drainEvents( listEvent );
        for ( const VitalityEvent& event : listEvent )
        {
            if ( event._type == VitalityEventType::Downed )
                handleDowned( player, event._instigatorId );
            else if ( event._type == VitalityEventType::Died )
                handleDeath( player, event._instigatorId );
        }
    }

    void BrMatch::handleDowned( int32 player, int32 instigator )
    {
        BrPlayer& target = _listPlayer[static_cast<size_t>( player )];
        target._downedBy = instigator;
        target._revive.reset();
        endRevive( player ); // 살리던 사람이 기절하면 그만둔다
        if ( isValidPlayer( instigator ) )
            ++_listPlayer[static_cast<size_t>( instigator )]._knocks;
        pushEvent( BrEvent::Kind::PlayerDowned, player, instigator, target._team, 0 );
        resolveTeamWipe( target._team );
    }

    void BrMatch::handleDeath( int32 player, int32 instigator )
    {
        BrPlayer& target = _listPlayer[static_cast<size_t>( player )];
        // 기절한 뒤의 죽음(출혈 · 자기장 · 전멸)은 기절시킨 사람의 처치다.
        const int32 killer = isValidPlayer( instigator ) ? instigator : target._downedBy;
        endRevive( player );
        for ( BrPlayer& other : _listPlayer )
        {
            if ( other._revivingTarget == player )
                other._revivingTarget = -1;
        }
        target._revive.reset();
        if ( isValidPlayer( killer ) && _listPlayer[static_cast<size_t>( killer )]._team != target._team )
            ++_listPlayer[static_cast<size_t>( killer )]._kills;
        _matchState.reportKill( player, killer );
        const int32 remaining = countRemainingPlayers();
        pushEvent( BrEvent::Kind::PlayerKilled, player, killer, target._team, remaining );
        pushEvent( BrEvent::Kind::PlayersRemaining, -1, -1, -1, remaining );
        collectMatchEvents();
        resolveTeamWipe( target._team );
    }

    void BrMatch::interruptRevive( int32 target, bool bNotify )
    {
        BrPlayer& downed = _listPlayer[static_cast<size_t>( target )];
        for ( BrPlayer& other : _listPlayer )
        {
            if ( other._revivingTarget == target )
                other._revivingTarget = -1;
        }
        downed._revive.reset();
        downed._vitality.stopRevive();
        if ( bNotify )
            pushEvent( BrEvent::Kind::ReviveInterrupted, target, -1, downed._team, 0 );
    }

    void BrMatch::finishRevive( int32 target )
    {
        BrPlayer& downed  = _listPlayer[static_cast<size_t>( target )];
        int32     reviver = -1;
        for ( size_t index = 0; index < _listPlayer.size(); ++index )
        {
            if ( _listPlayer[index]._revivingTarget == target )
            {
                reviver                            = reviver < 0 ? static_cast<int32>( index ) : reviver;
                _listPlayer[index]._revivingTarget = -1;
            }
        }
        // 진행이 다 찼다 — 세워 둔 Vitality 의 부활 시계를 한 번에 끝까지 돌린다.
        (void)downed._vitality.startRevive( reviver, 1.0f );
        downed._vitality.update( downed._vitality.getSettings()._reviveTime );
        downed._vitality.discardEvents();
        downed._revive.reset();
        downed._downedBy = -1;
        pushEvent( BrEvent::Kind::PlayerRevived, target, reviver, downed._team, 0 );
    }

    void BrMatch::resolveTeamWipe( int32 team )
    {
        for ( const BrPlayer& player : _listPlayer )
        {
            if ( player._team == team && player.isAlive() )
                return;
        }
        // 서 있는 팀원이 없다 — 기절한 팀원은 모두 죽는다.
        for ( size_t index = 0; index < _listPlayer.size(); ++index )
        {
            BrPlayer& player = _listPlayer[index];
            if ( player._team != team || player.isDowned() == false )
                continue;
            player._vitality.kill( player._downedBy );
            processVitality( static_cast<int32>( index ) );
        }
    }

    void BrMatch::dropSupply()
    {
        BrSupplyDrop drop;
        drop._time = _time;
        (void)_zone.pickPointInside( _random, drop._position );
        if ( _pLootCatalog != nullptr )
            (void)BrLootPlacement::rollTableAt( *_pLootCatalog, _pCatalog->getSupplyDropSettings()._tableId, drop._position, -1, 1, _random, drop._listItem );
        _listSupplyDrop.push_back( drop );
        BrEvent event;
        event._kind     = BrEvent::Kind::SupplyDropLanded;
        event._value    = static_cast<int32>( _listSupplyDrop.size() ) - 1;
        event._position = drop._position;
        _eventBuffer.push( event );
    }

    void BrMatch::collectMatchEvents()
    {
        _listMatchScratch.clear();
        _matchState.drainEvents( _listMatchScratch );
        for ( const MatchEvent& event : _listMatchScratch )
        {
            if ( event._kind == MatchEvent::Kind::TeamEliminated )
                pushEvent( BrEvent::Kind::TeamEliminated, -1, -1, event._team, event._value );
            else if ( event._kind == MatchEvent::Kind::MatchEnded )
                pushEvent( BrEvent::Kind::MatchEnded, -1, -1, event._team, 0 );
        }
    }

    void BrMatch::pushEvent( BrEvent::Kind kind, int32 player, int32 other, int32 team, int32 value )
    {
        BrEvent event;
        event._kind   = kind;
        event._player = player;
        event._other  = other;
        event._team   = team;
        event._value  = value;
        if ( isValidPlayer( player ) )
            event._position = _listPlayer[static_cast<size_t>( player )]._position;
        _eventBuffer.push( event );
    }
} // namespace sw
