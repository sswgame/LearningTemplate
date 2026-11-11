#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorEncounter.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"

namespace sw
{
    SurvivalHorrorEncounter::SurvivalHorrorEncounter()
        : _listInvestigator{}
        , _monster{}
        , _turnOrder{}
        , _random{}
        , _monsterToughness{ 0 }
        , _successFace{ 5 }
        , _turnCount{ 0 }
        , _state{ SurvivalHorrorEncounterState::Ongoing }
    {
    }

    bool SurvivalHorrorEncounter::initialize( const SurvivalHorrorMonsterDef& monster, const vector<SurvivalHorrorInvestigator>& listInvestigator, uint32 seed, int32 successFace )
    {
        // 주사위는 여섯 면이다. 범위 밖 성공 눈은 맞추지 않고 거절한다.
        if ( successFace < 1 || successFace > 6 )
        {
            SW_LOG_ERROR_ID( "SurvivalHorrorEncounter.SurvivalHorrorEncounterRefusedSuccessFace", "SurvivalHorrorEncounter: refused success face %# - it must be within [1, 6]", successFace );
            return false;
        }
        if ( seed == 0 )
        {
            SW_LOG_ERROR_ID( "SurvivalHorrorEncounter.RefusedSeedZero", "SurvivalHorrorEncounter: refused seed 0 - give a non-zero seed" );
            return false;
        }
        if ( hasValidActorIDs( listInvestigator ) == false )
        {
            SW_LOG_ERROR_ID( "SurvivalHorrorEncounter.RefusedActorID", "SurvivalHorrorEncounter: refused investigators - an actor id is negative, repeated or the monster's" );
            return false;
        }
        _monster          = monster;
        _listInvestigator = listInvestigator;
        _monsterToughness = monster._toughness;
        _successFace      = successFace;
        _turnCount        = 0;
        _state            = SurvivalHorrorEncounterState::Ongoing;
        _random.setSeed( seed );
        _turnOrder.initialize( TurnOrderMode::Rounds, GameRandom::deriveSeed( seed, 0x5bd1e995u ) );
        for ( SurvivalHorrorInvestigator& investigator : _listInvestigator )
        {
            investigator._bDefeated = SW_FALSE;
            _turnOrder.addActor( investigator._actorID, investigator._speed );
            defeatIfBroken( investigator );
        }
        _turnOrder.addActor( kMonsterActorID, monster._speed );
        refreshState();
        return true;
    }

    SurvivalHorrorTurnResult SurvivalHorrorEncounter::playNextTurn()
    {
        SurvivalHorrorTurnResult result;
        if ( _state != SurvivalHorrorEncounterState::Ongoing )
            return result;
        result._actorID = _turnOrder.next();
        ++_turnCount;
        if ( result._actorID == kMonsterActorID )
        {
            SurvivalHorrorInvestigator* pTarget = nullptr;
            for ( SurvivalHorrorInvestigator& investigator : _listInvestigator )
            {
                if ( investigator._bDefeated == SW_TRUE )
                    continue;
                const bool bBetterTarget = pTarget == nullptr || investigator._health < pTarget->_health || ( investigator._health == pTarget->_health && investigator._actorID < pTarget->_actorID );
                if ( bBetterTarget )
                    pTarget = &investigator;
            }
            if ( pTarget != nullptr )
            {
                result._targetID   = pTarget->_actorID;
                result._healthLoss = DepletingValue::take( pTarget->_health, _monster._damage )._taken;
                defeatIfBroken( *pTarget );
            }
            refreshState();
            return result;
        }

        SurvivalHorrorInvestigator* pInvestigator = findInvestigator( result._actorID );
        if ( pInvestigator == nullptr || pInvestigator->_bDefeated == SW_TRUE )
            return result;
        result._horrorSuccesses = rollSuccesses( pInvestigator->_will );
        if ( result._horrorSuccesses == 0 )
        {
            result._sanityLoss = DepletingValue::take( pInvestigator->_sanity, _monster._horror )._taken;
            defeatIfBroken( *pInvestigator );
        }
        if ( pInvestigator->_bDefeated == SW_FALSE )
        {
            result._combatSuccesses = rollSuccesses( pInvestigator->_strength );
            DepletingValue::take( _monsterToughness, result._combatSuccesses );
        }
        refreshState();
        return result;
    }

    int32 SurvivalHorrorEncounter::rollSuccesses( int32 diceCount )
    {
        int32 successCount = 0;
        for ( int32 diceIndex = 0; diceIndex < diceCount; ++diceIndex )
        {
            if ( _random.nextInt( 1, 6 ) >= _successFace )
                ++successCount;
        }
        return successCount;
    }

    SurvivalHorrorInvestigator* SurvivalHorrorEncounter::findInvestigator( int32 actorID )
    {
        return VectorUtil::findBy<&SurvivalHorrorInvestigator::_actorID>( _listInvestigator, actorID );
    }

    void SurvivalHorrorEncounter::defeatIfBroken( SurvivalHorrorInvestigator& investigator )
    {
        if ( investigator._bDefeated == SW_TRUE || ( investigator._health > 0 && investigator._sanity > 0 ) )
            return;
        investigator._bDefeated = SW_TRUE;
        _turnOrder.removeActor( investigator._actorID );
    }

    void SurvivalHorrorEncounter::refreshState()
    {
        if ( _monsterToughness <= 0 )
        {
            _state = SurvivalHorrorEncounterState::Victory;
            return;
        }
        for ( const SurvivalHorrorInvestigator& investigator : _listInvestigator )
        {
            if ( investigator._bDefeated == SW_FALSE )
                return;
        }
        _state = SurvivalHorrorEncounterState::Defeat;
    }

    bool SurvivalHorrorEncounter::hasValidActorIDs( const vector<SurvivalHorrorInvestigator>& listInvestigator )
    {
        for ( const SurvivalHorrorInvestigator& investigator : listInvestigator )
        {
            if ( investigator._actorID < 0 || investigator._actorID == kMonsterActorID )
                return false;
        }
        return VectorUtil::findDuplicateBy<&SurvivalHorrorInvestigator::_actorID>( listInvestigator ) == nullptr;
    }

    bool SurvivalHorrorEncounter::isStateConsistent() const
    {
        int32 standingCount = 0;
        for ( const SurvivalHorrorInvestigator& investigator : _listInvestigator )
        {
            const bool bBroken            = investigator._health <= 0 || investigator._sanity <= 0;
            const bool bStanding          = investigator._bDefeated == SW_FALSE;
            const bool bPropertiesInvalid = investigator._speed <= 0.0f || investigator._health < 0 || investigator._sanity < 0 || investigator._will < 0 || investigator._strength < 0;
            if ( bPropertiesInvalid || bBroken == bStanding || _turnOrder.hasActor( investigator._actorID ) != bStanding )
                return false;
            standingCount += bStanding ? 1 : 0;
        }
        if ( _turnOrder.hasActor( kMonsterActorID ) == false || _turnOrder.getActorCount() != standingCount + 1 )
            return false;
        if ( _monsterToughness < 0 || _monsterToughness > _monster._toughness || _turnCount < 0 )
            return false;
        const SurvivalHorrorEncounterState expected = _monsterToughness == 0 ? SurvivalHorrorEncounterState::Victory : ( standingCount == 0 ? SurvivalHorrorEncounterState::Defeat : SurvivalHorrorEncounterState::Ongoing );
        return _state == expected;
    }

    void SurvivalHorrorEncounter::serializeState( StateIO& io )
    {
        io.expect( _monster._id );
        // 조사자마다 속도, 번호, 체력, 정신력, 의지, 힘(24) + 빠짐(1)
        io.list( _listInvestigator, 25, [&]( SurvivalHorrorInvestigator& investigator )
        {
            io.value( investigator._speed );
            io.value( investigator._actorID );
            io.value( investigator._health );
            io.value( investigator._sanity );
            io.value( investigator._will );
            io.value( investigator._strength );
            io.flag( investigator._bDefeated );
        } );
        io.value( _turnOrder );
        io.value( _random );
        io.value( _monsterToughness );
        io.value( _turnCount );
        io.enumValue( _state, SurvivalHorrorEncounterState::Defeat );
        if ( io.isReading() && io.isOk() )
        {
            io.check( hasValidActorIDs( _listInvestigator ) && isStateConsistent() );
        }
    }

} // namespace sw
