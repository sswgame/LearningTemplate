#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/HorrorEncounter.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    HorrorEncounter::HorrorEncounter()
        : _listInvestigator{}
        , _monster{}
        , _turnOrder{}
        , _random{}
        , _monsterToughness{ 0 }
        , _successFace{ 5 }
        , _turnCount{ 0 }
        , _state{ HorrorEncounterState::Ongoing }
    {
    }

    bool HorrorEncounter::initialize( const HorrorMonsterDef& monster, const vector<HorrorInvestigator>& listInvestigator, uint32 seed, int32 successFace )
    {
        // 주사위는 여섯 면이다. 범위 밖 성공 눈은 맞추지 않고 거절한다.
        if ( successFace < 1 || successFace > 6 )
        {
            SW_LOG_ERROR_ID( "HorrorEncounter.HorrorEncounterRefusedSuccessFace", "HorrorEncounter: refused success face %# - it must be within [1, 6]", successFace );
            return false;
        }
        if ( seed == 0 )
        {
            SW_LOG_ERROR_ID( "HorrorEncounter.RefusedSeedZero", "HorrorEncounter: refused seed 0 - give a non-zero seed" );
            return false;
        }
        if ( hasValidActorIDs( listInvestigator ) == false )
        {
            SW_LOG_ERROR_ID( "HorrorEncounter.RefusedActorID", "HorrorEncounter: refused investigators - an actor id is negative, repeated or the monster's" );
            return false;
        }
        _monster          = monster;
        _listInvestigator = listInvestigator;
        _monsterToughness = monster._toughness;
        _successFace      = successFace;
        _turnCount        = 0;
        _state            = HorrorEncounterState::Ongoing;
        _random.setSeed( seed );
        _turnOrder.initialize( TurnOrderMode::Rounds, GameRandom::deriveSeed( seed, 0x5bd1e995u ) );
        for ( HorrorInvestigator& investigator : _listInvestigator )
        {
            investigator._bDefeated = SW_FALSE;
            _turnOrder.addActor( investigator._actorID, investigator._speed );
            defeatIfBroken( investigator );
        }
        _turnOrder.addActor( kMonsterActorID, monster._speed );
        refreshState();
        return true;
    }

    HorrorTurnResult HorrorEncounter::playNextTurn()
    {
        HorrorTurnResult result;
        if ( _state != HorrorEncounterState::Ongoing )
            return result;
        result._actorID = _turnOrder.next();
        ++_turnCount;
        if ( result._actorID == kMonsterActorID )
        {
            HorrorInvestigator* pTarget = nullptr;
            for ( HorrorInvestigator& investigator : _listInvestigator )
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
                result._healthLoss = MathUtil::min( pTarget->_health, _monster._damage );
                pTarget->_health -= result._healthLoss;
                defeatIfBroken( *pTarget );
            }
            refreshState();
            return result;
        }

        HorrorInvestigator* pInvestigator = findInvestigator( result._actorID );
        if ( pInvestigator == nullptr || pInvestigator->_bDefeated == SW_TRUE )
            return result;
        result._horrorSuccesses = rollSuccesses( pInvestigator->_will );
        if ( result._horrorSuccesses == 0 )
        {
            result._sanityLoss = MathUtil::min( pInvestigator->_sanity, _monster._horror );
            pInvestigator->_sanity -= result._sanityLoss;
            defeatIfBroken( *pInvestigator );
        }
        if ( pInvestigator->_bDefeated == SW_FALSE )
        {
            result._combatSuccesses = rollSuccesses( pInvestigator->_strength );
            _monsterToughness       = MathUtil::max( 0, _monsterToughness - result._combatSuccesses );
        }
        refreshState();
        return result;
    }

    int32 HorrorEncounter::rollSuccesses( int32 diceCount )
    {
        int32 successCount = 0;
        for ( int32 diceIndex = 0; diceIndex < diceCount; ++diceIndex )
        {
            if ( _random.nextInt( 1, 6 ) >= _successFace )
                ++successCount;
        }
        return successCount;
    }

    HorrorInvestigator* HorrorEncounter::findInvestigator( int32 actorID )
    {
        return VectorUtil::findBy<&HorrorInvestigator::_actorID>( _listInvestigator, actorID );
    }

    void HorrorEncounter::defeatIfBroken( HorrorInvestigator& investigator )
    {
        if ( investigator._bDefeated == SW_TRUE || ( investigator._health > 0 && investigator._sanity > 0 ) )
            return;
        investigator._bDefeated = SW_TRUE;
        _turnOrder.removeActor( investigator._actorID );
    }

    void HorrorEncounter::refreshState()
    {
        if ( _monsterToughness <= 0 )
        {
            _state = HorrorEncounterState::Victory;
            return;
        }
        for ( const HorrorInvestigator& investigator : _listInvestigator )
        {
            if ( investigator._bDefeated == SW_FALSE )
                return;
        }
        _state = HorrorEncounterState::Defeat;
    }

    bool HorrorEncounter::hasValidActorIDs( const vector<HorrorInvestigator>& listInvestigator )
    {
        for ( const HorrorInvestigator& investigator : listInvestigator )
        {
            if ( investigator._actorID < 0 || investigator._actorID == kMonsterActorID )
                return false;
        }
        return VectorUtil::findDuplicateBy<&HorrorInvestigator::_actorID>( listInvestigator ) == nullptr;
    }

    bool HorrorEncounter::isStateConsistent() const
    {
        int32 standingCount = 0;
        for ( const HorrorInvestigator& investigator : _listInvestigator )
        {
            const bool bBroken        = investigator._health <= 0 || investigator._sanity <= 0;
            const bool bStanding      = investigator._bDefeated == SW_FALSE;
            const bool bFieldsInvalid = investigator._speed <= 0.0f || investigator._health < 0 || investigator._sanity < 0 || investigator._will < 0 || investigator._strength < 0;
            if ( bFieldsInvalid || bBroken == bStanding || _turnOrder.hasActor( investigator._actorID ) != bStanding )
                return false;
            standingCount += bStanding ? 1 : 0;
        }
        if ( _turnOrder.hasActor( kMonsterActorID ) == false || _turnOrder.getActorCount() != standingCount + 1 )
            return false;
        if ( _monsterToughness < 0 || _monsterToughness > _monster._toughness || _turnCount < 0 )
            return false;
        const HorrorEncounterState expected = _monsterToughness == 0 ? HorrorEncounterState::Victory : ( standingCount == 0 ? HorrorEncounterState::Defeat : HorrorEncounterState::Ongoing );
        return _state == expected;
    }

    void HorrorEncounter::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, _monster._id );
        outArchive << static_cast<uint32>( _listInvestigator.size() );
        for ( const HorrorInvestigator& investigator : _listInvestigator )
        {
            outArchive << investigator._speed;
            outArchive << investigator._actorID;
            outArchive << investigator._health;
            outArchive << investigator._sanity;
            outArchive << investigator._will;
            outArchive << investigator._strength;
            outArchive << investigator._bDefeated;
        }
        _turnOrder.writeState( outArchive );
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _monsterToughness;
        outArchive << _turnCount;
        outArchive << static_cast<uint8>( _state );
    }

    bool HorrorEncounter::readState( Archive& archive )
    {
        hashed_string monsterID;
        uint32        count = 0;
        // 조사자마다 속도, 번호, 체력, 정신력, 의지, 힘(24) + 빠짐(1)
        const bool bHeadRead = StateArchiveUtil::readName( archive, monsterID ) && archive.readCount( 25, count );
        if ( bHeadRead == false || monsterID != _monster._id )
            return false;
        // 사본에 읽고 끝까지 맞으면 그때 바꾼다. 괴물 정의와 성공 눈은 사본도 그대로 가지고 있다.
        HorrorEncounter restored = *this;
        restored._listInvestigator.assign( count, HorrorInvestigator{} );
        for ( HorrorInvestigator& investigator : restored._listInvestigator )
        {
            archive >> investigator._speed;
            archive >> investigator._actorID;
            archive >> investigator._health;
            archive >> investigator._sanity;
            archive >> investigator._will;
            archive >> investigator._strength;
            archive >> investigator._bDefeated;
            if ( archive.isError() || investigator._bDefeated > SW_TRUE )
                return false;
        }
        uint8      state     = 0;
        const bool bPartRead = restored._turnOrder.readState( archive ) && StateArchiveUtil::readRandom( archive, restored._random );
        archive >> restored._monsterToughness;
        archive >> restored._turnCount;
        archive >> state;
        const bool bValid = bPartRead && archive.isOk() && state <= static_cast<uint8>( HorrorEncounterState::Defeat );
        if ( bValid == false )
            return false;
        restored._state = static_cast<HorrorEncounterState>( state );
        if ( hasValidActorIDs( restored._listInvestigator ) == false || restored.isStateConsistent() == false )
            return false;
        *this = std::move( restored );
        return true;
    }
} // namespace sw
