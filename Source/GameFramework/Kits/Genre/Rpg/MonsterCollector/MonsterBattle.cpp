#include "pch.h"

#include "GameFramework/Kits/Genre/Rpg/MonsterCollector/MonsterBattle.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Combat/Damage/ElementChart.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct MonsterBattleInternal
        {
            static constexpr const utf8* kArrStatName[kMonsterStatCount] = { "Hp", "Attack", "Defense", "SpecialAttack", "SpecialDefense", "Speed" };

            static bool isSpeciesStab( const MonsterCollectorCatalog& catalog, const MonsterInstance& monster, const hashed_string& moveType )
            {
                const MonsterSpeciesDef* pSpecies = catalog.findSpecies( monster._speciesId );
                return pSpecies != nullptr && moveType.empty() == false && pSpecies->hasType( moveType );
            }

            static const vector<hashed_string>& findTypes( const MonsterCollectorCatalog& catalog, const MonsterInstance& monster )
            {
                static const vector<hashed_string> s_listEmpty;
                const MonsterSpeciesDef*           pSpecies = catalog.findSpecies( monster._speciesId );
                return pSpecies != nullptr ? pSpecies->_listType : s_listEmpty;
            }

            static const hashed_string& getPoisonName()
            {
                static const hashed_string name( "Poison" );
                return name;
            }
            static const hashed_string& getBurnName()
            {
                static const hashed_string name( "Burn" );
                return name;
            }
            static const hashed_string& getToxicName()
            {
                static const hashed_string name( "Toxic" );
                return name;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MonsterBattle::MonsterBattle()
        : _arrSide{}
        , _eventBuffer{}
        , _turnOrder{}
        , _random{}
        , _captured{}
        , _weatherId{}
        , _pCatalog{ nullptr }
        , _pChart{ nullptr }
        , _weatherTurns{ 0 }
        , _escapeAttempts{ 0 }
        , _outcome{ MonsterBattleOutcome::Ongoing }
        , _bWild{ false }
    {
    }

    void MonsterBattle::initialize( const MonsterCollectorCatalog* pCatalog, const ElementChart* pChart, uint32 seed )
    {
        _pCatalog = pCatalog;
        _pChart   = pChart;
        _random.setSeed( seed );
        _turnOrder.initialize( TurnOrderMode::Rounds, seed ^ 0x5bd1e995u );
    }

    void MonsterBattle::start( const vector<MonsterInstance>& listPlayer, const vector<MonsterInstance>& listFoe, bool bWild )
    {
        _arrSide[kPlayerSide]              = Side{};
        _arrSide[kFoeSide]                 = Side{};
        _arrSide[kPlayerSide]._listMonster = listPlayer;
        _arrSide[kFoeSide]._listMonster    = listFoe;
        _bWild                             = bWild;
        _outcome                           = MonsterBattleOutcome::Ongoing;
        _escapeAttempts                    = 0;
        _captured                          = MonsterInstance{};
        _weatherId                         = hashed_string{};
        _weatherTurns                      = 0;
        _eventBuffer.clear();
        _turnOrder.removeActor( kPlayerSide );
        _turnOrder.removeActor( kFoeSide );
        _turnOrder.addActor( kPlayerSide, 1.0f );
        _turnOrder.addActor( kFoeSide, 1.0f );
        for ( int32 side = 0; side < kSideCount; ++side )
        {
            const int32 first = findFirstUsable( side );
            if ( first < 0 )
            {
                setOutcome( side == kPlayerSide ? MonsterBattleOutcome::Lost : MonsterBattleOutcome::Won );
                return;
            }
            _arrSide[side]._activeIndex = first;
        }
    }

    void MonsterBattle::setWeather( const hashed_string& weatherId, int32 turns )
    {
        if ( weatherId.empty() || _pCatalog == nullptr || _pCatalog->findWeather( weatherId ) == nullptr )
        {
            if ( _weatherId.empty() == false )
                pushEvent( MonsterBattleEvent::Kind::WeatherEnded, kPlayerSide, 0, _weatherId );
            _weatherId    = hashed_string{};
            _weatherTurns = 0;
            return;
        }
        _weatherId    = weatherId;
        _weatherTurns = turns > 0 ? turns : -1;
        pushEvent( MonsterBattleEvent::Kind::WeatherStarted, kPlayerSide, _weatherTurns, weatherId );
    }

    const MonsterInstance& MonsterBattle::getActive( int32 side ) const
    {
        static const MonsterInstance s_empty{};
        const Side&                  entry = _arrSide[side];
        if ( entry._activeIndex < 0 || entry._activeIndex >= static_cast<int32>( entry._listMonster.size() ) )
            return s_empty;
        return entry._listMonster[static_cast<size_t>( entry._activeIndex )];
    }

    bool MonsterBattle::setAction( int32 side, const MonsterAction& action )
    {
        if ( side < 0 || side >= kSideCount || _outcome != MonsterBattleOutcome::Ongoing || _arrSide[side]._bNeedsSwitch )
            return false;
        const Side& entry = _arrSide[side];
        switch ( action._kind )
        {
            case MonsterActionKind::None:
            {
                break;
            }
            case MonsterActionKind::Move:
            {
                if ( action._index < 0 || action._index >= MonsterInstance::kMoveSlotCount )
                    return false;
                if ( getActive( side )._arrMove[action._index].isEmpty() )
                    return false;
                break;
            }
            case MonsterActionKind::Switch:
            {
                if ( action._index < 0 || action._index >= static_cast<int32>( entry._listMonster.size() ) || action._index == entry._activeIndex )
                    return false;
                if ( entry._listMonster[static_cast<size_t>( action._index )].isFainted() )
                    return false;
                break;
            }
            case MonsterActionKind::Ball:
            case MonsterActionKind::Run:
            {
                if ( _bWild == false || side != kPlayerSide )
                    return false;
                break;
            }
        }
        _arrSide[side]._action = action;
        return true;
    }

    void MonsterBattle::resolveRound()
    {
        if ( _outcome != MonsterBattleOutcome::Ongoing || _pCatalog == nullptr )
            return;
        if ( _arrSide[kPlayerSide]._bNeedsSwitch || _arrSide[kFoeSide]._bNeedsSwitch )
            return;

        for ( int32 side = 0; side < kSideCount; ++side )
        {
            const MonsterAction& action   = _arrSide[side]._action;
            int32                priority = kNonMovePriority;
            if ( action._kind == MonsterActionKind::Move )
            {
                const MonsterMoveDef* pMove = _pCatalog->findMove( getActive( side )._arrMove[action._index]._moveId );
                priority                    = pMove != nullptr ? pMove->_priority : 0;
            }
            _turnOrder.setPriority( side, priority );
            _turnOrder.setSpeed( side, computeEffectiveSpeed( side ) );
        }
        _turnOrder.restartRound();

        for ( int32 turn = 0; turn < kSideCount && _outcome == MonsterBattleOutcome::Ongoing; ++turn )
        {
            const int32 side = _turnOrder.next();
            if ( side < 0 || side >= kSideCount )
                break;
            executeAction( side );
        }

        if ( _outcome == MonsterBattleOutcome::Ongoing )
            applyEndOfRound();
        for ( Side& entry : _arrSide )
        {
            entry._action = MonsterAction{};
        }
    }

    void MonsterBattle::executeAction( int32 side )
    {
        const MonsterAction action = _arrSide[side]._action;
        _arrSide[side]._action     = MonsterAction{};
        switch ( action._kind )
        {
            case MonsterActionKind::None:
            {
                return;
            }
            case MonsterActionKind::Move:
            {
                executeMove( side, action._index );
                return;
            }
            case MonsterActionKind::Switch:
            {
                switchTo( side, action._index );
                return;
            }
            case MonsterActionKind::Ball:
            {
                executeBall( action._ballMultiplier );
                return;
            }
            case MonsterActionKind::Run:
            {
                executeRun();
                return;
            }
        }
    }

    bool MonsterBattle::canActThisTurn( int32 side )
    {
        MonsterInstance& monster = getActiveMutable( side );
        switch ( monster._status )
        {
            case MonsterStatus::Sleep:
            {
                if ( monster._statusTurns <= 0 )
                {
                    monster._status = MonsterStatus::None;
                    pushEvent( MonsterBattleEvent::Kind::Woke, side );
                    return true;
                }
                --monster._statusTurns;
                pushEvent( MonsterBattleEvent::Kind::Asleep, side );
                return false;
            }
            case MonsterStatus::Freeze:
            {
                if ( _random.nextInt( 1, 100 ) <= kThawChance )
                {
                    monster._status = MonsterStatus::None;
                    pushEvent( MonsterBattleEvent::Kind::Thawed, side );
                    return true;
                }
                pushEvent( MonsterBattleEvent::Kind::Frozen, side );
                return false;
            }
            case MonsterStatus::Paralysis:
            {
                if ( _random.nextInt( 1, 100 ) <= kParalysisChance )
                {
                    pushEvent( MonsterBattleEvent::Kind::FullyParalyzed, side );
                    return false;
                }
                return true;
            }
            default:
            {
                return true;
            }
        }
    }

    void MonsterBattle::executeMove( int32 side, int32 slot )
    {
        if ( getActive( side ).isFainted() || canActThisTurn( side ) == false )
            return;
        const int32           foeSide  = 1 - side;
        MonsterInstance&      attacker = getActiveMutable( side );
        MonsterMoveSlot&      moveSlot = attacker._arrMove[slot];
        const MonsterMoveDef* pMove    = _pCatalog->findMove( moveSlot._moveId );
        if ( pMove == nullptr || moveSlot._pp <= 0 )
        {
            pushEvent( MonsterBattleEvent::Kind::NoPp, side, 0, moveSlot._moveId );
            return;
        }
        --moveSlot._pp;
        pushEvent( MonsterBattleEvent::Kind::MoveUsed, side, slot, pMove->_id );

        if ( pMove->_weatherId.empty() == false )
        {
            const MonsterWeatherDef* pWeather = _pCatalog->findWeather( pMove->_weatherId );
            setWeather( pMove->_weatherId, pWeather != nullptr ? pWeather->_turns : 5 );
        }

        const bool bTargetsFoe = pMove->isDamaging() || pMove->_status != MonsterStatus::None ||
                                 ( pMove->_statStages != 0 && pMove->_statTarget == MonsterEffectTarget::Foe );
        if ( bTargetsFoe && getActive( foeSide ).isFainted() )
            return;
        if ( bTargetsFoe && pMove->_accuracy > 0 && _random.nextInt( 1, 100 ) > pMove->_accuracy )
        {
            pushEvent( MonsterBattleEvent::Kind::Missed, side, 0, pMove->_id );
            return;
        }

        if ( pMove->isDamaging() )
        {
            const MonsterInstance& defender       = getActive( foeSide );
            const float32          typeMultiplier = computeTypeMultiplier( pMove->_type, defender );
            if ( typeMultiplier <= 0.0f )
            {
                pushEvent( MonsterBattleEvent::Kind::Immune, foeSide, 0, pMove->_id, 0.0f );
                return;
            }
            const bool         bPhysical = pMove->_category == MonsterMoveCategory::Physical;
            MonsterDamageInput input;
            input._bCritical         = _random.nextInt( 1, computeCriticalDivisor( pMove->_critStage ) ) == 1;
            input._randomPercent     = _random.nextInt( 85, 100 );
            input._level             = attacker._level;
            input._power             = pMove->_power;
            input._attack            = computeStatWithStage( side, attacker, bPhysical ? MonsterStat::Attack : MonsterStat::SpecialAttack, input._bCritical, true );
            input._defense           = computeStatWithStage( foeSide, defender, bPhysical ? MonsterStat::Defense : MonsterStat::SpecialDefense, input._bCritical, false );
            input._typeMultiplier    = typeMultiplier;
            input._weatherMultiplier = computeWeatherMultiplier( pMove->_type );
            input._bStab             = MonsterBattleInternal::isSpeciesStab( *_pCatalog, attacker, pMove->_type );
            input._bBurned           = bPhysical && attacker._status == MonsterStatus::Burn;
            if ( input._bCritical )
                pushEvent( MonsterBattleEvent::Kind::Critical, side, 0, pMove->_id );
            applyDamage( foeSide, computeDamage( input ), MonsterBattleEvent::Kind::Damage, pMove->_id );
            _eventBuffer.getLast()._multiplier = typeMultiplier;
            if ( getActive( foeSide ).isFainted() )
            {
                handleFaint( foeSide );
                return;
            }
        }

        if ( pMove->_status != MonsterStatus::None && pMove->_statusChance > 0 && _random.nextInt( 1, 100 ) <= pMove->_statusChance )
            applyStatus( foeSide, pMove->_status );
        if ( pMove->_statStages != 0 && _random.nextInt( 1, 100 ) <= pMove->_statChance )
            applyStatChange( pMove->_statTarget == MonsterEffectTarget::Self ? side : foeSide, pMove->_stat, pMove->_statStages );
    }

    void MonsterBattle::executeBall( float32 ballMultiplier )
    {
        MonsterInstance&           target   = getActiveMutable( kFoeSide );
        const MonsterSpeciesDef*   pSpecies = _pCatalog->findSpecies( target._speciesId );
        const int32                rate     = pSpecies != nullptr ? pSpecies->_catchRate : 1;
        const MonsterCaptureResult result   = computeCapture( target.getMaxHp(), target._hp, rate, ballMultiplier, target._status, _random );
        for ( int32 shake = 1; shake <= result._shakes; ++shake )
        {
            pushEvent( MonsterBattleEvent::Kind::CaptureShake, kFoeSide, shake, target._speciesId );
        }
        if ( result._bCaught == false )
        {
            pushEvent( MonsterBattleEvent::Kind::BrokeFree, kFoeSide, result._shakes, target._speciesId );
            return;
        }
        _captured = target;
        pushEvent( MonsterBattleEvent::Kind::Captured, kFoeSide, 0, target._speciesId );
        setOutcome( MonsterBattleOutcome::Captured );
    }

    void MonsterBattle::executeRun()
    {
        // 3세대 도망: 내 스피드가 상대 이상이면 반드시, 아니면 F = ⌊A × 128 / B⌋ + 30 × 시도 횟수 를 0..255 와 견준다.
        const int32 playerSpeed = MathUtil::max( 1, getActive( kPlayerSide ).getStat( MonsterStat::Speed ) );
        const int32 foeSpeed    = MathUtil::max( 1, getActive( kFoeSide ).getStat( MonsterStat::Speed ) );
        ++_escapeAttempts;
        bool bEscaped = playerSpeed >= foeSpeed;
        if ( bEscaped == false )
        {
            const int32 odds = playerSpeed * 128 / foeSpeed + 30 * ( _escapeAttempts - 1 );
            bEscaped         = odds > 255 || _random.nextInt( 0, 255 ) < odds;
        }
        if ( bEscaped == false )
        {
            pushEvent( MonsterBattleEvent::Kind::EscapeFailed, kPlayerSide, _escapeAttempts );
            return;
        }
        pushEvent( MonsterBattleEvent::Kind::Escaped, kPlayerSide, _escapeAttempts );
        setOutcome( MonsterBattleOutcome::Escaped );
    }

    void MonsterBattle::switchTo( int32 side, int32 partyIndex )
    {
        Side& entry = _arrSide[side];
        if ( partyIndex < 0 || partyIndex >= static_cast<int32>( entry._listMonster.size() ) )
            return;
        if ( entry._listMonster[static_cast<size_t>( partyIndex )].isFainted() )
            return;
        // 능력 변화는 나간 개체와 함께 사라지고, 맹독의 n 은 1 로 돌아간다.
        MonsterInstance& leaving = getActiveMutable( side );
        if ( leaving._status == MonsterStatus::Toxic )
            leaving._statusTurns = 1;
        for ( int32& stage : entry._arrStage )
        {
            stage = 0;
        }
        entry._activeIndex  = partyIndex;
        entry._bNeedsSwitch = false;
        pushEvent( MonsterBattleEvent::Kind::Switched, side, partyIndex, getActive( side )._speciesId );
    }

    bool MonsterBattle::switchFainted( int32 side, int32 partyIndex )
    {
        if ( side < 0 || side >= kSideCount || _arrSide[side]._bNeedsSwitch == false )
            return false;
        const Side& entry = _arrSide[side];
        if ( partyIndex < 0 || partyIndex >= static_cast<int32>( entry._listMonster.size() ) || entry._listMonster[static_cast<size_t>( partyIndex )].isFainted() )
            return false;
        switchTo( side, partyIndex );
        return true;
    }

    void MonsterBattle::applyStatus( int32 targetSide, MonsterStatus status )
    {
        MonsterInstance& target = getActiveMutable( targetSide );
        if ( target.isFainted() || target._status != MonsterStatus::None )
            return;
        if ( _pCatalog->isStatusImmune( status, MonsterBattleInternal::findTypes( *_pCatalog, target ) ) )
            return;
        target._status      = status;
        target._statusTurns = 0;
        if ( status == MonsterStatus::Sleep )
            target._statusTurns = _random.nextInt( 1, kMaxSleepTurns );
        else if ( status == MonsterStatus::Toxic )
            target._statusTurns = 1;
        pushEvent( MonsterBattleEvent::Kind::StatusApplied, targetSide, static_cast<int32>( status ), hashed_string( toString( status ) ) );
    }

    void MonsterBattle::applyStatChange( int32 targetSide, MonsterStat stat, int32 stages )
    {
        if ( getActive( targetSide ).isFainted() )
            return;
        int32&      stage  = _arrSide[targetSide]._arrStage[static_cast<size_t>( stat )];
        const int32 before = stage;
        stage              = MathUtil::clamp( stage + stages, kMinStage, kMaxStage );
        pushEvent( MonsterBattleEvent::Kind::StatChanged, targetSide, stage - before,
                   hashed_string( MonsterBattleInternal::kArrStatName[static_cast<size_t>( stat )] ) );
    }

    void MonsterBattle::applyDamage( int32 side, int32 amount, MonsterBattleEvent::Kind kind, const hashed_string& id )
    {
        MonsterInstance& target = getActiveMutable( side );
        const int32      dealt  = MathUtil::min( MathUtil::max( 0, amount ), target._hp );
        target._hp -= dealt;
        pushEvent( kind, side, dealt, id );
    }

    void MonsterBattle::handleFaint( int32 side )
    {
        const MonsterInstance& fainted = getActive( side );
        pushEvent( MonsterBattleEvent::Kind::Fainted, side, 0, fainted._speciesId );
        if ( side == kFoeSide && getActive( kPlayerSide ).isFainted() == false )
            awardExp( fainted );

        bool bHasOther = false;
        for ( const MonsterInstance& monster : _arrSide[side]._listMonster )
        {
            bHasOther = bHasOther || monster.isFainted() == false;
        }
        if ( bHasOther == false )
        {
            setOutcome( side == kPlayerSide ? MonsterBattleOutcome::Lost : MonsterBattleOutcome::Won );
            return;
        }
        _arrSide[side]._bNeedsSwitch = true;
        _arrSide[side]._action       = MonsterAction{};
        pushEvent( MonsterBattleEvent::Kind::NeedsSwitch, side );
    }

    void MonsterBattle::awardExp( const MonsterInstance& defeated )
    {
        const MonsterSpeciesDef* pDefeated = _pCatalog->findSpecies( defeated._speciesId );
        if ( pDefeated == nullptr )
            return;
        MonsterInstance& winner = getActiveMutable( kPlayerSide );
        const int64      amount = MonsterRules::computeExpYield( *pDefeated, defeated._level, _bWild == false );
        pushEvent( MonsterBattleEvent::Kind::ExpGained, kPlayerSide, static_cast<int32>( amount ), winner._speciesId );
        MonsterRules::addEffortValues( *_pCatalog, winner, *pDefeated );

        vector<MonsterGrowthEvent> listGrowth;
        (void)MonsterRules::gainExp( *_pCatalog, winner, amount, listGrowth );
        for ( const MonsterGrowthEvent& growth : listGrowth )
        {
            MonsterBattleEvent::Kind kind = MonsterBattleEvent::Kind::LevelUp;
            if ( growth._kind == MonsterGrowthEvent::Kind::LearnedMove )
                kind = MonsterBattleEvent::Kind::LearnedMove;
            else if ( growth._kind == MonsterGrowthEvent::Kind::MoveLearnBlocked )
                kind = MonsterBattleEvent::Kind::MoveLearnBlocked;
            else if ( growth._kind == MonsterGrowthEvent::Kind::CanEvolve )
                kind = MonsterBattleEvent::Kind::CanEvolve;
            pushEvent( kind, kPlayerSide, growth._level, growth._id );
        }
    }

    void MonsterBattle::applyEndOfRound()
    {
        const MonsterWeatherDef* pWeather = _weatherId.empty() ? nullptr : _pCatalog->findWeather( _weatherId );
        for ( int32 side = 0; side < kSideCount && _outcome == MonsterBattleOutcome::Ongoing; ++side )
        {
            if ( _arrSide[side]._bNeedsSwitch || getActive( side ).isFainted() )
                continue;
            if ( pWeather != nullptr && pWeather->_chipDivisor > 0 )
            {
                bool bImmune = false;
                for ( const hashed_string& type : MonsterBattleInternal::findTypes( *_pCatalog, getActive( side ) ) )
                {
                    for ( const hashed_string& immuneType : pWeather->_listChipImmuneType )
                    {
                        bImmune = bImmune || type == immuneType;
                    }
                }
                if ( bImmune == false )
                    applyDamage( side, MathUtil::max( 1, getActive( side ).getMaxHp() / pWeather->_chipDivisor ), MonsterBattleEvent::Kind::WeatherDamage,
                                 _weatherId );
            }

            MonsterInstance& monster = getActiveMutable( side );
            const int32      maxHp   = monster.getMaxHp();
            if ( monster.isFainted() == false )
            {
                if ( monster._status == MonsterStatus::Poison )
                {
                    applyDamage( side, MathUtil::max( 1, maxHp / 8 ), MonsterBattleEvent::Kind::StatusDamage, MonsterBattleInternal::getPoisonName() );
                }
                else if ( monster._status == MonsterStatus::Burn )
                {
                    applyDamage( side, MathUtil::max( 1, maxHp / 16 ), MonsterBattleEvent::Kind::StatusDamage, MonsterBattleInternal::getBurnName() );
                }
                else if ( monster._status == MonsterStatus::Toxic )
                {
                    const int32 counter  = MathUtil::clamp( monster._statusTurns, 1, 15 );
                    monster._statusTurns = counter + 1;
                    applyDamage( side, MathUtil::max( 1, maxHp * counter / 16 ), MonsterBattleEvent::Kind::StatusDamage, MonsterBattleInternal::getToxicName() );
                }
            }
            if ( getActive( side ).isFainted() )
                handleFaint( side );
        }

        if ( _weatherTurns > 0 && --_weatherTurns == 0 )
        {
            pushEvent( MonsterBattleEvent::Kind::WeatherEnded, kPlayerSide, 0, _weatherId );
            _weatherId = hashed_string{};
        }
    }

    int32 MonsterBattle::computeDamage( const MonsterDamageInput& input )
    {
        if ( input._power <= 0 || input._typeMultiplier <= 0.0f )
            return 0;
        const int64 attack  = MathUtil::max( 1, input._attack );
        const int64 defense = MathUtil::max( 1, input._defense );
        int64       damage  = ( ( 2 * static_cast<int64>( input._level ) / 5 + 2 ) * input._power * attack / defense ) / 50 + 2;
        damage              = static_cast<int64>( static_cast<float64>( damage ) * static_cast<float64>( input._weatherMultiplier ) );
        if ( input._bCritical )
            damage = damage * 3 / 2;
        damage = damage * MathUtil::clamp( input._randomPercent, 85, 100 ) / 100;
        if ( input._bStab )
            damage = damage * 3 / 2;
        damage = static_cast<int64>( static_cast<float64>( damage ) * static_cast<float64>( input._typeMultiplier ) );
        if ( input._bBurned )
            damage /= 2;
        return static_cast<int32>( MathUtil::clamp<int64>( damage, 1, 0x7fffffff ) );
    }

    MonsterCaptureResult MonsterBattle::computeCapture( int32 maxHp, int32 hp, int32 catchRate, float32 ballMultiplier, MonsterStatus status, GameRandom& random )
    {
        MonsterCaptureResult result;
        const int64          safeMaxHp = MathUtil::max( 1, maxHp );
        const int64          safeHp    = MathUtil::clamp<int64>( hp, 1, safeMaxHp );
        float64              catchValue =
            MathUtil::floor( static_cast<float64>( ( 3 * safeMaxHp - 2 * safeHp ) * MathUtil::clamp( catchRate, 1, 255 ) ) * static_cast<float64>( ballMultiplier ) / static_cast<float64>( 3 * safeMaxHp ) );
        if ( status == MonsterStatus::Sleep || status == MonsterStatus::Freeze )
            catchValue *= 2.0;
        else if ( status != MonsterStatus::None )
            catchValue *= 1.5;
        result._catchValue = static_cast<int32>( MathUtil::max( 1.0, MathUtil::floor( catchValue ) ) );
        if ( result._catchValue >= 255 )
        {
            result._shakeChance = 65536;
            result._shakes      = 3;
            result._bCaught     = true;
            return result;
        }
        const float64 inner = MathUtil::floor( MathUtil::sqrt( MathUtil::floor( MathUtil::sqrt( MathUtil::floor( 16711680.0 / result._catchValue ) ) ) ) );
        result._shakeChance = static_cast<int32>( MathUtil::floor( 1048560.0 / MathUtil::max( 1.0, inner ) ) );
        int32 passed        = 0;
        for ( int32 check = 0; check < 4; ++check )
        {
            if ( random.nextInt( 0, 65535 ) >= result._shakeChance )
                break;
            ++passed;
        }
        result._bCaught = passed == 4;
        result._shakes  = MathUtil::min( passed, 3 );
        return result;
    }

    float32 MonsterBattle::computeStageMultiplier( int32 stage )
    {
        const int32 clamped = MathUtil::clamp( stage, kMinStage, kMaxStage );
        if ( clamped >= 0 )
            return static_cast<float32>( 2 + clamped ) / 2.0f;
        return 2.0f / static_cast<float32>( 2 - clamped );
    }

    int32 MonsterBattle::computeCriticalDivisor( int32 critStage )
    {
        if ( critStage <= 0 )
            return 24;
        if ( critStage == 1 )
            return 8;
        if ( critStage == 2 )
            return 2;
        return 1;
    }

    float32 MonsterBattle::computeTypeMultiplier( const hashed_string& moveType, const MonsterInstance& defender ) const
    {
        if ( _pChart == nullptr || _pCatalog == nullptr || moveType.empty() )
            return 1.0f;
        return _pChart->computeMultiplier( moveType, MonsterBattleInternal::findTypes( *_pCatalog, defender ) );
    }

    float32 MonsterBattle::computeExpectedDamage( int32 attackerSide, const MonsterInstance& attacker, const hashed_string& moveId, int32 defenderSide,
                                                  const MonsterInstance& defender ) const
    {
        const MonsterMoveDef* pMove = _pCatalog != nullptr ? _pCatalog->findMove( moveId ) : nullptr;
        if ( pMove == nullptr || pMove->isDamaging() == false )
            return 0.0f;
        const bool         bPhysical = pMove->_category == MonsterMoveCategory::Physical;
        MonsterDamageInput input;
        input._level             = attacker._level;
        input._power             = pMove->_power;
        input._attack            = computeStatWithStage( attackerSide, attacker, bPhysical ? MonsterStat::Attack : MonsterStat::SpecialAttack, false, true );
        input._defense           = computeStatWithStage( defenderSide, defender, bPhysical ? MonsterStat::Defense : MonsterStat::SpecialDefense, false, false );
        input._typeMultiplier    = computeTypeMultiplier( pMove->_type, defender );
        input._weatherMultiplier = computeWeatherMultiplier( pMove->_type );
        input._bStab             = MonsterBattleInternal::isSpeciesStab( *_pCatalog, attacker, pMove->_type );
        input._bBurned           = bPhysical && attacker._status == MonsterStatus::Burn;
        const float32 average    = static_cast<float32>( computeDamage( input ) ) * 0.925f;
        const float32 accuracy   = pMove->_accuracy > 0 ? static_cast<float32>( pMove->_accuracy ) / 100.0f : 1.0f;
        return average * accuracy;
    }

    float32 MonsterBattle::computeEffectiveSpeed( int32 side ) const
    {
        const MonsterInstance& monster = getActive( side );
        float32                speed   = static_cast<float32>( monster.getStat( MonsterStat::Speed ) ) * computeStageMultiplier( getStage( side, MonsterStat::Speed ) );
        if ( monster._status == MonsterStatus::Paralysis )
            speed *= 0.5f;
        return speed;
    }

    int32 MonsterBattle::computeStatWithStage( int32 side, const MonsterInstance& monster, MonsterStat stat, bool bCritical, bool bAttacker ) const
    {
        int32 stage = side >= 0 && side < kSideCount ? getStage( side, stat ) : 0;
        // 급소는 쓰는 쪽의 떨어진 공격과 받는 쪽의 오른 방어를 무시한다.
        if ( bCritical && ( ( bAttacker && stage < 0 ) || ( bAttacker == false && stage > 0 ) ) )
            stage = 0;
        return MathUtil::max( 1, static_cast<int32>( static_cast<float32>( monster.getStat( stat ) ) * computeStageMultiplier( stage ) ) );
    }

    float32 MonsterBattle::computeWeatherMultiplier( const hashed_string& moveType ) const
    {
        if ( _weatherId.empty() || _pCatalog == nullptr || moveType.empty() )
            return 1.0f;
        const MonsterWeatherDef* pWeather = _pCatalog->findWeather( _weatherId );
        if ( pWeather == nullptr )
            return 1.0f;
        if ( pWeather->_boostedType == moveType )
            return 1.5f;
        if ( pWeather->_weakenedType == moveType )
            return 0.5f;
        return 1.0f;
    }

    int32 MonsterBattle::findFirstUsable( int32 side ) const
    {
        const vector<MonsterInstance>& listMonster = _arrSide[side]._listMonster;
        for ( int32 index = 0; index < static_cast<int32>( listMonster.size() ); ++index )
        {
            if ( listMonster[static_cast<size_t>( index )].isFainted() == false )
                return index;
        }
        return -1;
    }

    void MonsterBattle::setOutcome( MonsterBattleOutcome outcome )
    {
        _outcome = outcome;
        pushEvent( MonsterBattleEvent::Kind::BattleEnded, kPlayerSide, static_cast<int32>( outcome ) );
    }

    void MonsterBattle::pushEvent( MonsterBattleEvent::Kind kind, int32 side, int32 value, const hashed_string& id, float32 multiplier )
    {
        MonsterBattleEvent event;
        event._kind       = kind;
        event._side       = side;
        event._value      = value;
        event._id         = id;
        event._multiplier = multiplier;
        _eventBuffer.push( event );
    }

    void MonsterBattle::drainEvents( vector<MonsterBattleEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void MonsterBattle::writeState( Archive& outArchive ) const
    {
        for ( const Side& side : _arrSide )
        {
            outArchive << static_cast<uint32>( side._listMonster.size() );
            for ( const MonsterInstance& monster : side._listMonster )
            {
                monster.writeState( outArchive );
            }
            for ( const int32 stage : side._arrStage )
            {
                outArchive << stage;
            }
            outArchive << side._action._ballMultiplier;
            outArchive << side._action._index;
            outArchive << static_cast<uint8>( side._action._kind );
            outArchive << side._activeIndex;
            outArchive << side._bNeedsSwitch;
        }
        _turnOrder.writeState( outArchive );
        StateArchiveUtil::writeRandom( outArchive, _random );
        _captured.writeState( outArchive );
        StateArchiveUtil::writeName( outArchive, _weatherId );
        outArchive << _weatherTurns;
        outArchive << _escapeAttempts;
        outArchive << static_cast<uint8>( _outcome );
        outArchive << _bWild;
    }

    bool MonsterBattle::readState( Archive& archive )
    {
        if ( _pCatalog == nullptr )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 카탈로그 · 상성표는 사본이 그대로 든다.
        MonsterBattle restored = *this;
        for ( Side& side : restored._arrSide )
        {
            uint32 monsterCount = 0;
            if ( StateArchiveUtil::readCount( archive, MonsterInstance::kStateMinBytes, monsterCount ) == false )
                return false;
            side._listMonster.assign( monsterCount, MonsterInstance{} );
            for ( MonsterInstance& monster : side._listMonster )
            {
                if ( monster.readState( archive ) == false || _pCatalog->findSpecies( monster._speciesId ) == nullptr )
                    return false;
                for ( const MonsterMoveSlot& slot : monster._arrMove )
                {
                    if ( slot.isEmpty() == false && _pCatalog->findMove( slot._moveId ) == nullptr )
                        return false;
                }
            }
            for ( int32& stage : side._arrStage )
            {
                archive >> stage;
                if ( ( kMinStage <= stage && stage <= kMaxStage ) == false )
                    return false;
            }
            uint8 kind = 0;
            archive >> side._action._ballMultiplier;
            archive >> side._action._index;
            archive >> kind;
            archive >> side._activeIndex;
            archive >> side._bNeedsSwitch;
            const int32 monsterLimit = MathUtil::max( 1, static_cast<int32>( monsterCount ) );
            const bool  bValid       = archive.isOk() && kind <= static_cast<uint8>( MonsterActionKind::Run ) && 0 <= side._activeIndex && side._activeIndex < monsterLimit;
            if ( bValid == false )
                return false;
            side._action._kind = static_cast<MonsterActionKind>( kind );
        }

        uint8 outcome = 0;
        if ( restored._turnOrder.readState( archive ) == false || StateArchiveUtil::readRandom( archive, restored._random ) == false )
            return false;
        if ( restored._captured.readState( archive ) == false || StateArchiveUtil::readName( archive, restored._weatherId ) == false )
            return false;
        archive >> restored._weatherTurns;
        archive >> restored._escapeAttempts;
        archive >> outcome;
        archive >> restored._bWild;
        const bool bWeatherKnown = restored._weatherId.empty() || _pCatalog->findWeather( restored._weatherId ) != nullptr;
        const bool bValid        = archive.isOk() && bWeatherKnown && outcome <= static_cast<uint8>( MonsterBattleOutcome::Escaped );
        if ( bValid == false )
            return false;
        restored._outcome = static_cast<MonsterBattleOutcome>( outcome );
        restored._eventBuffer.clear();
        *this = std::move( restored );
        return true;
    }
} // namespace sw
