#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterBattle.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Character/Combat/Damage/ElementChart.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"

namespace sw
{
    namespace
    {
        struct MonsterBattleInternal
        {
            static constexpr const utf8* kArrStatName[kMonsterStatCount] = { "HP", "Attack", "Defense", "SpecialAttack", "SpecialDefense", "Speed" };

            static bool isSpeciesStab( const MonsterCollectorCatalog& catalog, const MonsterInstance& monster, const hashed_string& moveType )
            {
                const MonsterSpeciesDef* pSpecies = catalog.findSpecies( monster._speciesID );
                return pSpecies != nullptr && moveType.empty() == false && pSpecies->hasType( moveType );
            }

            static const vector<hashed_string>& findTypes( const MonsterCollectorCatalog& catalog, const MonsterInstance& monster )
            {
                static const vector<hashed_string> s_listEmpty;
                const MonsterSpeciesDef*           pSpecies = catalog.findSpecies( monster._speciesID );
                return pSpecies != nullptr ? pSpecies->_listType : s_listEmpty;
            }

            static constexpr uint32 kTurnOrderStream = 0x5bd1e995u; ///< 턴 순서 난수를 전투 난수와 가른다
            static constexpr int32  kPercent         = 100;
            static constexpr int32  kHalf            = 2;

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
        , _weatherID{}
        , _pCatalog{ nullptr }
        , _pChart{ nullptr }
        , _weatherTurns{ 0 }
        , _escapeAttempts{ 0 }
        , _result{ MonsterBattleResult::Ongoing }
        , _bWild{ SW_FALSE }
    {
    }

    void MonsterBattle::initialize( const MonsterCollectorCatalog& catalog, const ElementChart& chart, uint32 seed )
    {
        _pCatalog = &catalog;
        _pChart   = &chart;
        _random.setSeed( seed );
        _turnOrder.initialize( TurnOrderMode::Rounds, GameRandom::deriveSeed( seed, MonsterBattleInternal::kTurnOrderStream ) );
    }

    void MonsterBattle::start( const vector<MonsterInstance>& listPlayer, const vector<MonsterInstance>& listFoe, bool bWild )
    {
        _arrSide[kPlayerSide]              = Side{};
        _arrSide[kFoeSide]                 = Side{};
        _arrSide[kPlayerSide]._listMonster = listPlayer;
        _arrSide[kFoeSide]._listMonster    = listFoe;
        _bWild                             = bWild;
        _result                            = MonsterBattleResult::Ongoing;
        _escapeAttempts                    = 0;
        _captured                          = MonsterInstance{};
        _weatherID                         = hashed_string{};
        _weatherTurns                      = 0;
        _eventBuffer.clear();
        _turnOrder.removeActor( kPlayerSide );
        _turnOrder.removeActor( kFoeSide );
        _turnOrder.addActor( kPlayerSide, 1.0f );
        _turnOrder.addActor( kFoeSide, 1.0f );
        for ( int32 side = 0; side < kSideCount; ++side )
        {
            const optional<int32> first = findFirstUsable( side );
            if ( first.has_value() == false )
            {
                setResult( side == kPlayerSide ? MonsterBattleResult::Lost : MonsterBattleResult::Won );
                return;
            }
            _arrSide[side]._activeIndex = *first;
        }
    }

    void MonsterBattle::setWeather( const hashed_string& weatherID, int32 turns )
    {
        if ( weatherID.empty() )
        {
            if ( _weatherID.empty() == false )
                pushEvent( MonsterBattleEvent::Kind::WeatherEnded, kPlayerSide, 0, _weatherID );
            _weatherID    = hashed_string{};
            _weatherTurns = 0;
            return;
        }
        if ( _pCatalog == nullptr || _pCatalog->findWeather( weatherID ) == nullptr )
        {
            // 없애면 오타 하나가 지금 날씨를 말없이 지운다.
            SW_LOG_ERROR_ID( "MonsterBattle.UnknownWeather", "MonsterBattle: refused unknown weather '%#'", weatherID.c_str() );
            return;
        }
        _weatherID    = weatherID;
        _weatherTurns = turns > 0 ? turns : kEndlessWeather;
        pushEvent( MonsterBattleEvent::Kind::WeatherStarted, kPlayerSide, _weatherTurns, weatherID );
    }

    const MonsterInstance& MonsterBattle::getActive( int32 side ) const
    {
        static const MonsterInstance s_empty{};
        const Side&                  entry = _arrSide[side];
        if ( entry._activeIndex < 0 || entry._activeIndex >= static_cast<int32>( entry._listMonster.size() ) )
            return s_empty;
        return entry._listMonster[static_cast<size_t>( entry._activeIndex )];
    }

    const MonsterBattleRules& MonsterBattle::getRules() const
    {
        static const MonsterBattleRules s_default{};
        return _pCatalog != nullptr ? _pCatalog->getBattleRules() : s_default;
    }

    bool MonsterBattle::isInProgress() const
    {
        return _result == MonsterBattleResult::Ongoing && _pCatalog != nullptr && _arrSide[kPlayerSide]._listMonster.empty() == false && _arrSide[kFoeSide]._listMonster.empty() == false;
    }

    bool MonsterBattle::setAction( int32 side, const MonsterAction& action )
    {
        const bool bActionRejected = side < 0 || side >= kSideCount || isInProgress() == false || _arrSide[side]._bNeedsSwitch;
        if ( bActionRejected || canUseAction( side, action ) == false )
            return false;
        _arrSide[side]._action = action;
        return true;
    }

    bool MonsterBattle::canUseAction( int32 side, const MonsterAction& action ) const
    {
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
                const bool bInvalidSwitchTarget = action._index < 0 || action._index >= static_cast<int32>( entry._listMonster.size() ) || action._index == entry._activeIndex;
                if ( bInvalidSwitchTarget )
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
                if ( action._kind == MonsterActionKind::Ball && action._ballMultiplier <= 0.0f )
                    return false;
                break;
            }
        }
        return true;
    }

    bool MonsterBattle::isSideStateValid( int32 side ) const
    {
        const Side& entry = _arrSide[side];
        // 시작 전(개체 없음)의 나선 자리는 0 이다.
        if ( entry._listMonster.empty() )
            return entry._activeIndex == 0 && entry._bNeedsSwitch == false && entry._action._kind == MonsterActionKind::None;
        if ( entry._activeIndex < 0 || entry._activeIndex >= static_cast<int32>( entry._listMonster.size() ) || canUseAction( side, entry._action ) == false )
            return false;
        if ( _result != MonsterBattleResult::Ongoing )
            return true;
        // 진행 중에는 나선 개체가 살아 있거나, 쓰러졌으면 대신 나설 개체가 있어 교체를 기다린다.
        if ( entry._bNeedsSwitch == false )
            return getActive( side ).isFainted() == false;
        return findFirstUsable( side ).has_value();
    }

    void MonsterBattle::resolveRound()
    {
        if ( isInProgress() == false )
            return;
        if ( _arrSide[kPlayerSide]._bNeedsSwitch || _arrSide[kFoeSide]._bNeedsSwitch )
            return;

        for ( int32 side = 0; side < kSideCount; ++side )
        {
            const MonsterAction& action   = _arrSide[side]._action;
            int32                priority = kNonMovePriority;
            if ( action._kind == MonsterActionKind::Move )
            {
                const MonsterMoveDef* pMove = _pCatalog->findMove( getActive( side )._arrMove[action._index]._moveID );
                priority                    = pMove != nullptr ? pMove->_priority : 0;
            }
            _turnOrder.setPriority( side, priority );
            _turnOrder.setSpeed( side, computeEffectiveSpeed( side ) );
        }
        _turnOrder.restartRound();

        for ( int32 turn = 0; turn < kSideCount && _result == MonsterBattleResult::Ongoing; ++turn )
        {
            const optional<int32> side = _turnOrder.next();
            if ( side.has_value() == false || *side >= kSideCount )
                break;
            executeAction( *side );
        }

        if ( _result == MonsterBattleResult::Ongoing )
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

    bool MonsterBattle::resolveStatusBeforeAct( int32 side )
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
                if ( _random.nextInt( 1, MonsterBattleInternal::kPercent ) <= getRules()._thawChance )
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
                if ( _random.nextInt( 1, MonsterBattleInternal::kPercent ) <= getRules()._paralysisChance )
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
        if ( getActive( side ).isFainted() || resolveStatusBeforeAct( side ) == false )
            return;
        const int32           foeSide  = 1 - side;
        MonsterInstance&      attacker = getActiveMutable( side );
        MonsterMoveSlot&      moveSlot = attacker._arrMove[slot];
        const MonsterMoveDef* pMove    = _pCatalog->findMove( moveSlot._moveID );
        if ( pMove == nullptr || moveSlot._pp <= 0 )
        {
            pushEvent( MonsterBattleEvent::Kind::NoPp, side, 0, moveSlot._moveID );
            return;
        }
        --moveSlot._pp;
        pushEvent( MonsterBattleEvent::Kind::MoveUsed, side, slot, pMove->_id );

        // 모르는 날씨는 카탈로그 읽기가 이미 오류로 알렸다.
        if ( const MonsterWeatherDef* pWeather = _pCatalog->findWeather( pMove->_weatherID ) )
            setWeather( pMove->_weatherID, pWeather->_turns );

        const bool bTargetsFoe = pMove->isDamaging() || pMove->_status != MonsterStatus::None ||
                                 ( pMove->_statStages != 0 && pMove->_statTarget == MonsterEffectTarget::Foe );
        if ( bTargetsFoe && getActive( foeSide ).isFainted() )
            return;
        const bool bMissedByAccuracy = bTargetsFoe && pMove->_accuracy > 0 && _random.nextInt( 1, MonsterBattleInternal::kPercent ) > pMove->_accuracy;
        if ( bMissedByAccuracy )
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
            input._bCritical         = _random.nextInt( 1, computeCriticalDivisor( getRules(), pMove->_critStage ) ) == 1;
            input._randomPercent     = _random.nextInt( getRules()._rollMinPercent, MonsterBattleInternal::kPercent );
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
            applyDamage( foeSide, computeDamage( getRules(), input ), MonsterBattleEvent::Kind::Damage, pMove->_id );
            _eventBuffer.getLast()._multiplier = typeMultiplier;
            if ( getActive( foeSide ).isFainted() )
            {
                handleFaint( foeSide );
                return;
            }
        }

        const bool bStatusProcs = pMove->_status != MonsterStatus::None && pMove->_statusChance > 0 && _random.nextInt( 1, MonsterBattleInternal::kPercent ) <= pMove->_statusChance;
        if ( bStatusProcs )
            applyStatus( foeSide, pMove->_status );
        if ( pMove->_statStages != 0 && _random.nextInt( 1, MonsterBattleInternal::kPercent ) <= pMove->_statChance )
            applyStatChange( pMove->_statTarget == MonsterEffectTarget::Self ? side : foeSide, pMove->_changedStat, pMove->_statStages );
    }

    void MonsterBattle::executeBall( float32 ballMultiplier )
    {
        MonsterInstance&           target   = getActiveMutable( kFoeSide );
        const MonsterSpeciesDef*   pSpecies = _pCatalog->findSpecies( target._speciesID );
        const int32                rate     = pSpecies != nullptr ? pSpecies->_catchRate : 1;
        const MonsterCaptureResult result   = computeCapture( getRules(), target.getMaxHP(), target._hp, rate, ballMultiplier, target._status, _random );
        for ( int32 shake = 1; shake <= result._shakes; ++shake )
        {
            pushEvent( MonsterBattleEvent::Kind::CaptureShake, kFoeSide, shake, target._speciesID );
        }
        if ( result._bCaught == false )
        {
            pushEvent( MonsterBattleEvent::Kind::BrokeFree, kFoeSide, result._shakes, target._speciesID );
            return;
        }
        _captured = target;
        pushEvent( MonsterBattleEvent::Kind::Captured, kFoeSide, 0, target._speciesID );
        setResult( MonsterBattleResult::Captured );
    }

    void MonsterBattle::executeRun()
    {
        // 3세대 도망 공식이다. 내 스피드가 상대 이상이면 반드시 도망치고, 아니면 F = ⌊A × 128 / B⌋ + 30 × 시도 횟수 를 0..255 와 비교한다(수치는 `<Rules>`).
        const MonsterBattleRules& rules       = getRules();
        const int32               playerSpeed = MathUtil::max( 1, getActive( kPlayerSide ).getStat( MonsterStat::Speed ) );
        const int32               foeSpeed    = MathUtil::max( 1, getActive( kFoeSide ).getStat( MonsterStat::Speed ) );
        ++_escapeAttempts;
        bool bEscaped = playerSpeed >= foeSpeed;
        if ( bEscaped == false )
        {
            const int32 odds = playerSpeed * rules._escapeSpeedFactor / foeSpeed + rules._escapePerAttempt * ( _escapeAttempts - 1 );
            bEscaped         = odds >= rules._escapeRollRange || _random.nextInt( 0, rules._escapeRollRange - 1 ) < odds;
        }
        if ( bEscaped == false )
        {
            pushEvent( MonsterBattleEvent::Kind::EscapeFailed, kPlayerSide, _escapeAttempts );
            return;
        }
        pushEvent( MonsterBattleEvent::Kind::Escaped, kPlayerSide, _escapeAttempts );
        setResult( MonsterBattleResult::Escaped );
    }

    void MonsterBattle::switchTo( int32 side, int32 partyIndex )
    {
        Side& entry = _arrSide[side];
        if ( partyIndex < 0 || partyIndex >= static_cast<int32>( entry._listMonster.size() ) )
            return;
        if ( entry._listMonster[static_cast<size_t>( partyIndex )].isFainted() )
            return;
        // 능력 변화는 나간 개체와 함께 사라지고, 맹독의 n 은 1로 돌아간다.
        MonsterInstance& leaving = getActiveMutable( side );
        if ( leaving._status == MonsterStatus::Toxic )
            leaving._statusTurns = 1;
        for ( int32& stage : entry._arrStage )
        {
            stage = 0;
        }
        entry._activeIndex  = partyIndex;
        entry._bNeedsSwitch = false;
        pushEvent( MonsterBattleEvent::Kind::Switched, side, partyIndex, getActive( side )._speciesID );
    }

    bool MonsterBattle::switchFainted( int32 side, int32 partyIndex )
    {
        const bool bInvalidSwitchRequest = side < 0 || side >= kSideCount || _arrSide[side]._bNeedsSwitch == false;
        if ( bInvalidSwitchRequest )
            return false;
        const Side& entry              = _arrSide[side];
        const bool  bInvalidPartyIndex = partyIndex < 0 || partyIndex >= static_cast<int32>( entry._listMonster.size() ) || entry._listMonster[static_cast<size_t>( partyIndex )].isFainted();
        if ( bInvalidPartyIndex )
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
            target._statusTurns = _random.nextInt( 1, getRules()._maxSleepTurns );
        else if ( status == MonsterStatus::Toxic )
            target._statusTurns = 1;
        pushEvent( MonsterBattleEvent::Kind::StatusApplied, targetSide, static_cast<int32>( status ), hashed_string( getEnumName( status ) ) );
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
        const int32      dealt  = DepletingValue::take( target._hp, amount )._taken;
        pushEvent( kind, side, dealt, id );
    }

    void MonsterBattle::handleFaint( int32 side )
    {
        const MonsterInstance& fainted = getActive( side );
        pushEvent( MonsterBattleEvent::Kind::Fainted, side, 0, fainted._speciesID );
        if ( side == kFoeSide && getActive( kPlayerSide ).isFainted() == false )
            awardExp( fainted );

        bool bHasOther = false;
        for ( const MonsterInstance& monster : _arrSide[side]._listMonster )
        {
            bHasOther = bHasOther || monster.isFainted() == false;
        }
        if ( bHasOther == false )
        {
            setResult( side == kPlayerSide ? MonsterBattleResult::Lost : MonsterBattleResult::Won );
            return;
        }
        _arrSide[side]._bNeedsSwitch = true;
        _arrSide[side]._action       = MonsterAction{};
    }

    void MonsterBattle::awardExp( const MonsterInstance& defeated )
    {
        const MonsterSpeciesDef* pDefeated = _pCatalog->findSpecies( defeated._speciesID );
        if ( pDefeated == nullptr )
            return;
        MonsterInstance& winner = getActiveMutable( kPlayerSide );
        const int64      amount = MonsterInstance::computeExpYield( *pDefeated, defeated._level, _bWild == false );
        pushEvent( MonsterBattleEvent::Kind::ExpGained, kPlayerSide, static_cast<int32>( amount ), winner._speciesID );
        MonsterInstance::addEffortValues( *_pCatalog, winner, *pDefeated );

        vector<MonsterGrowthEvent> listGrowth;
        (void)MonsterInstance::gainExp( *_pCatalog, winner, amount, listGrowth );
        for ( const MonsterGrowthEvent& growth : listGrowth )
        {
            MonsterBattleEvent::Kind kind = MonsterBattleEvent::Kind::LevelUp;
            if ( growth._kind == MonsterGrowthEvent::Kind::LearnedMove )
                kind = MonsterBattleEvent::Kind::LearnedMove;
            else if ( growth._kind == MonsterGrowthEvent::Kind::MoveLearnBlocked )
                kind = MonsterBattleEvent::Kind::MoveLearnBlocked;
            else if ( growth._kind == MonsterGrowthEvent::Kind::CanEvolve )
                continue; // 진화는 전투 뒤에 게임이 조건을 다시 보고 한다(`MonsterInstance::findEvolution`)
            pushEvent( kind, kPlayerSide, growth._level, growth._id );
        }
    }

    void MonsterBattle::applyEndOfRound()
    {
        const MonsterBattleRules& rules    = getRules();
        const MonsterWeatherDef*  pWeather = _weatherID.empty() ? nullptr : _pCatalog->findWeather( _weatherID );
        for ( int32 side = 0; side < kSideCount && _result == MonsterBattleResult::Ongoing; ++side )
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
                    applyDamage( side, MathUtil::max( 1, getActive( side ).getMaxHP() / pWeather->_chipDivisor ), MonsterBattleEvent::Kind::WeatherDamage,
                                 _weatherID );
            }

            MonsterInstance& monster = getActiveMutable( side );
            const int32      maxHP   = monster.getMaxHP();
            if ( monster.isFainted() == false )
            {
                if ( monster._status == MonsterStatus::Poison )
                {
                    applyDamage( side, MathUtil::max( 1, maxHP / rules._poisonDivisor ), MonsterBattleEvent::Kind::StatusDamage, MonsterBattleInternal::getPoisonName() );
                }
                else if ( monster._status == MonsterStatus::Burn )
                {
                    applyDamage( side, MathUtil::max( 1, maxHP / rules._burnDivisor ), MonsterBattleEvent::Kind::StatusDamage, MonsterBattleInternal::getBurnName() );
                }
                else if ( monster._status == MonsterStatus::Toxic )
                {
                    const int32 counter  = MathUtil::clamp( monster._statusTurns, 1, rules._toxicMaxTurns );
                    monster._statusTurns = counter + 1;
                    applyDamage( side, MathUtil::max( 1, maxHP * counter / rules._toxicDivisor ), MonsterBattleEvent::Kind::StatusDamage, MonsterBattleInternal::getToxicName() );
                }
            }
            if ( getActive( side ).isFainted() )
                handleFaint( side );
        }

        if ( _weatherTurns > 0 && --_weatherTurns == 0 )
        {
            pushEvent( MonsterBattleEvent::Kind::WeatherEnded, kPlayerSide, 0, _weatherID );
            _weatherID = hashed_string{};
        }
    }

    int32 MonsterBattle::computeDamage( const MonsterBattleRules& rules, const MonsterDamageInput& input )
    {
        if ( input._power <= 0 || input._typeMultiplier <= 0.0f )
            return 0;
        const int64 attack  = MathUtil::max( 1, input._attack );
        const int64 defense = MathUtil::max( 1, input._defense );
        int64       damage  = ( ( rules._levelFactor * static_cast<int64>( input._level ) / rules._levelDivisor + rules._levelBase ) * input._power * attack / defense ) / rules._powerDivisor + rules._damageBase;
        damage              = static_cast<int64>( static_cast<float64>( damage ) * static_cast<float64>( input._weatherMultiplier ) );
        if ( input._bCritical )
            damage = damage * rules._critPercent / MonsterBattleInternal::kPercent;
        damage = damage * MathUtil::clamp( input._randomPercent, rules._rollMinPercent, MonsterBattleInternal::kPercent ) / MonsterBattleInternal::kPercent;
        if ( input._bStab )
            damage = damage * rules._stabPercent / MonsterBattleInternal::kPercent;
        damage = static_cast<int64>( static_cast<float64>( damage ) * static_cast<float64>( input._typeMultiplier ) );
        if ( input._bBurned )
            damage /= rules._burnAttackDivisor;
        return static_cast<int32>( MathUtil::clamp<int64>( damage, 1, MathUtil::kMaxInt32 ) );
    }

    MonsterCaptureResult MonsterBattle::computeCapture( const MonsterBattleRules& rules, int32 maxHP, int32 hp, int32 catchRate, float32 ballMultiplier, MonsterStatus status, GameRandom& random )
    {
        MonsterCaptureResult result;
        const int64          safeMaxHP   = MathUtil::max( 1, maxHP );
        const int64          safeHP      = MathUtil::clamp<int64>( hp, 1, safeMaxHP );
        const int64          scaledMaxHP = rules._captureMaxHPFactor * safeMaxHP;
        const int64          rate        = ( scaledMaxHP - rules._captureHPFactor * safeHP ) * MathUtil::clamp( catchRate, 1, rules._captureMaxRate );
        float64              catchValue  = MathUtil::floor( static_cast<float64>( rate ) * static_cast<float64>( ballMultiplier ) / static_cast<float64>( scaledMaxHP ) );
        if ( status == MonsterStatus::Sleep || status == MonsterStatus::Freeze )
            catchValue *= static_cast<float64>( rules._captureHelplessScale );
        else if ( status != MonsterStatus::None )
            catchValue *= static_cast<float64>( rules._captureStatusScale );
        result._catchValue      = static_cast<int32>( MathUtil::max( 1.0, MathUtil::floor( catchValue ) ) );
        const int32 shownShakes = rules._captureShakeCount - 1;
        if ( result._catchValue >= rules._captureMaxRate )
        {
            result._shakeChance = rules._captureRollRange;
            result._shakes      = shownShakes;
            result._bCaught     = true;
            return result;
        }
        const float64 range = static_cast<float64>( rules._captureRollRange );
        const float64 ratio = range * static_cast<float64>( rules._captureMaxRate ) / static_cast<float64>( result._catchValue );
        const float64 inner = MathUtil::floor( MathUtil::sqrt( MathUtil::floor( MathUtil::sqrt( MathUtil::floor( ratio ) ) ) ) );
        const float64 outer = ( range - 1.0 ) * MathUtil::sqrt( MathUtil::sqrt( range ) );
        result._shakeChance = static_cast<int32>( MathUtil::floor( outer / MathUtil::max( 1.0, inner ) ) );
        int32 passed        = 0;
        for ( int32 check = 0; check < rules._captureShakeCount; ++check )
        {
            if ( random.nextInt( 0, rules._captureRollRange - 1 ) >= result._shakeChance )
                break;
            ++passed;
        }
        result._bCaught = passed == rules._captureShakeCount;
        result._shakes  = MathUtil::min( passed, shownShakes );
        return result;
    }

    float32 MonsterBattle::computeStageMultiplier( const MonsterBattleRules& rules, int32 stage )
    {
        const int32   clamped = MathUtil::clamp( stage, kMinStage, kMaxStage );
        const float32 base    = static_cast<float32>( rules._stageBase );
        if ( clamped >= 0 )
            return ( base + static_cast<float32>( clamped ) ) / base;
        return base / ( base - static_cast<float32>( clamped ) );
    }

    int32 MonsterBattle::computeCriticalDivisor( const MonsterBattleRules& rules, int32 critStage )
    {
        const size_t stage = static_cast<size_t>( MathUtil::max( 0, critStage ) );
        return stage < rules._listCritDivisor.size() ? rules._listCritDivisor[stage] : 1;
    }

    float32 MonsterBattle::computeTypeMultiplier( const hashed_string& moveType, const MonsterInstance& defender ) const
    {
        const bool bNoTypeMultiplier = _pChart == nullptr || _pCatalog == nullptr || moveType.empty();
        if ( bNoTypeMultiplier )
            return 1.0f;
        return _pChart->computeMultiplier( moveType, MonsterBattleInternal::findTypes( *_pCatalog, defender ) );
    }

    float32 MonsterBattle::computeExpectedDamage( int32 attackerSide, const MonsterInstance& attacker, const hashed_string& moveID, int32 defenderSide,
                                                  const MonsterInstance& defender ) const
    {
        const MonsterMoveDef* pMove = _pCatalog != nullptr ? _pCatalog->findMove( moveID ) : nullptr;
        if ( pMove == nullptr || pMove->isDamaging() == false )
            return 0.0f;
        const bool         bPhysical = pMove->_category == MonsterMoveCategory::Physical;
        MonsterDamageInput input;
        input._level                          = attacker._level;
        input._power                          = pMove->_power;
        input._attack                         = computeStatWithStage( attackerSide, attacker, bPhysical ? MonsterStat::Attack : MonsterStat::SpecialAttack, false, true );
        input._defense                        = computeStatWithStage( defenderSide, defender, bPhysical ? MonsterStat::Defense : MonsterStat::SpecialDefense, false, false );
        input._typeMultiplier                 = computeTypeMultiplier( pMove->_type, defender );
        input._weatherMultiplier              = computeWeatherMultiplier( pMove->_type );
        input._bStab                          = MonsterBattleInternal::isSpeciesStab( *_pCatalog, attacker, pMove->_type );
        input._bBurned                        = bPhysical && attacker._status == MonsterStatus::Burn;
        const MonsterBattleRules& rules       = getRules();
        const float32             averageRoll = static_cast<float32>( rules._rollMinPercent + MonsterBattleInternal::kPercent ) / static_cast<float32>( MonsterBattleInternal::kHalf * MonsterBattleInternal::kPercent );
        const float32             average     = static_cast<float32>( computeDamage( rules, input ) ) * averageRoll;
        const float32             accuracy    = pMove->_accuracy > 0 ? static_cast<float32>( pMove->_accuracy ) / static_cast<float32>( MonsterBattleInternal::kPercent ) : 1.0f;
        return average * accuracy;
    }

    float32 MonsterBattle::computeEffectiveSpeed( int32 side ) const
    {
        const MonsterInstance& monster = getActive( side );
        float32                speed   = static_cast<float32>( monster.getStat( MonsterStat::Speed ) ) * computeStageMultiplier( getRules(), getStage( side, MonsterStat::Speed ) );
        if ( monster._status == MonsterStatus::Paralysis )
            speed *= getRules()._paralysisSpeedScale;
        return speed;
    }

    int32 MonsterBattle::computeStatWithStage( int32 side, const MonsterInstance& monster, MonsterStat stat, bool bCritical, bool bAttacker ) const
    {
        int32 stage = side >= 0 && side < kSideCount ? getStage( side, stat ) : 0;
        // 급소는 쓰는 쪽의 떨어진 공격과 받는 쪽의 오른 방어를 무시한다.
        if ( bCritical && ( ( bAttacker && stage < 0 ) || ( bAttacker == false && stage > 0 ) ) )
            stage = 0;
        return MathUtil::max( 1, static_cast<int32>( static_cast<float32>( monster.getStat( stat ) ) * computeStageMultiplier( getRules(), stage ) ) );
    }

    float32 MonsterBattle::computeWeatherMultiplier( const hashed_string& moveType ) const
    {
        const bool bNoWeatherMultiplier = _weatherID.empty() || _pCatalog == nullptr || moveType.empty();
        if ( bNoWeatherMultiplier )
            return 1.0f;
        const MonsterWeatherDef* pWeather = _pCatalog->findWeather( _weatherID );
        if ( pWeather == nullptr )
            return 1.0f;
        if ( pWeather->_boostedType == moveType )
            return getRules()._weatherBoostScale;
        if ( pWeather->_weakenedType == moveType )
            return getRules()._weatherPenaltyScale;
        return 1.0f;
    }

    optional<int32> MonsterBattle::findFirstUsable( int32 side ) const
    {
        const vector<MonsterInstance>& listMonster = _arrSide[side]._listMonster;
        for ( int32 index = 0; index < static_cast<int32>( listMonster.size() ); ++index )
        {
            if ( listMonster[static_cast<size_t>( index )].isFainted() == false )
                return index;
        }
        return nullopt;
    }

    void MonsterBattle::setResult( MonsterBattleResult serviceResult )
    {
        _result = serviceResult;
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

    void MonsterBattle::pollEvents( vector<MonsterBattleEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void MonsterBattle::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        io.reflected( *this );
        for ( Side& side : _arrSide )
        {
            io.list( side._listMonster, MonsterInstance::kStateMinBytes, [&]( MonsterInstance& monster )
            {
                io.value( monster );
                if ( io.isReading() && io.isOk() )
                {
                    io.check( monster.isKnownTo( *_pCatalog ) );
                }
            } );
            for ( int32& stage : side._arrStage )
            {
                io.value( stage );
                io.check( kMinStage <= stage && stage <= kMaxStage );
            }
            io.value( side._action._ballMultiplier );
            io.value( side._action._index );
            io.enumValue( side._action._kind, MonsterActionKind::Run );
            io.value( side._activeIndex );
            uint8 bNeedsSwitch = side._bNeedsSwitch ? SW_TRUE : SW_FALSE; // bool 칸에 바로 읽으면 2 같은 바이트가 그대로 들어가 미정의 동작이다
            io.flag( bNeedsSwitch );
            if ( io.isOk() == false )
                return;
            if ( io.isReading() )
            {
                side._bNeedsSwitch = bNeedsSwitch == SW_TRUE;
            }
        }
        io.value( _turnOrder ); // 리플렉션 밖 값 — 턴 순서 · 난수는 손 코덱, 잡은 개체는 제 본문(머리 · 칸 사이 검사)으로 싣는다
        io.value( _random );
        io.value( _captured );
        if ( io.isWriting() || io.isOk() == false )
            return;
        // 날씨가 없으면 턴도 0 이고, 있으면 남은 턴이거나 끝없음(-1)이다.
        io.check( _weatherID.empty() ? _weatherTurns == 0 : ( _pCatalog->findWeather( _weatherID ) != nullptr && ( _weatherTurns == kEndlessWeather || _weatherTurns > 0 ) ) );
        io.check( _captured._speciesID.empty() ? _result != MonsterBattleResult::Captured : _captured.isKnownTo( *_pCatalog ) );
        for ( int32 side = 0; side < kSideCount; ++side )
        {
            io.check( isSideStateValid( side ) );
        }
        _eventBuffer.clear();
    }

} // namespace sw
