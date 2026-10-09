#include "pch.h"

#include "GameFramework/Kits/Horror/SurvivalHorror/HorrorEncounter.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

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

    void HorrorEncounter::initialize( const HorrorMonsterDef& monster, const vector<HorrorInvestigator>& listInvestigator, uint32 seed, int32 successFace )
    {
        _monster          = monster;
        _listInvestigator = listInvestigator;
        _monsterToughness = monster._toughness;
        _successFace      = MathUtil::clamp( successFace, 1, 6 );
        _turnCount        = 0;
        _state            = HorrorEncounterState::Ongoing;
        _random.setSeed( seed );
        _turnOrder.initialize( TurnOrderMode::Rounds, seed ^ 0x5bd1e995u );
        for ( HorrorInvestigator& investigator : _listInvestigator )
        {
            investigator._bDefeated = SW_FALSE;
            _turnOrder.addActor( investigator._actorId, investigator._speed );
            defeatIfBroken( investigator );
        }
        _turnOrder.addActor( kMonsterActorId, monster._speed );
        refreshState();
    }

    HorrorTurnResult HorrorEncounter::playNextTurn()
    {
        HorrorTurnResult result;
        if ( _state != HorrorEncounterState::Ongoing )
            return result;
        result._actorId = _turnOrder.next();
        ++_turnCount;
        if ( result._actorId == kMonsterActorId )
        {
            HorrorInvestigator* pTarget = nullptr;
            for ( HorrorInvestigator& investigator : _listInvestigator )
            {
                if ( investigator._bDefeated == SW_TRUE )
                    continue;
                if ( pTarget == nullptr || investigator._health < pTarget->_health || ( investigator._health == pTarget->_health && investigator._actorId < pTarget->_actorId ) )
                    pTarget = &investigator;
            }
            if ( pTarget != nullptr )
            {
                result._targetId   = pTarget->_actorId;
                result._healthLoss = MathUtil::min( pTarget->_health, _monster._damage );
                pTarget->_health -= result._healthLoss;
                defeatIfBroken( *pTarget );
            }
            refreshState();
            return result;
        }

        HorrorInvestigator* pInvestigator = findInvestigator( result._actorId );
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

    HorrorInvestigator* HorrorEncounter::findInvestigator( int32 actorId )
    {
        for ( HorrorInvestigator& investigator : _listInvestigator )
        {
            if ( investigator._actorId == actorId )
                return &investigator;
        }
        return nullptr;
    }

    void HorrorEncounter::defeatIfBroken( HorrorInvestigator& investigator )
    {
        if ( investigator._bDefeated == SW_TRUE || ( investigator._health > 0 && investigator._sanity > 0 ) )
            return;
        investigator._bDefeated = SW_TRUE;
        _turnOrder.removeActor( investigator._actorId );
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

    void HorrorEncounter::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, _monster._id );
        outArchive << static_cast<uint32>( _listInvestigator.size() );
        for ( const HorrorInvestigator& investigator : _listInvestigator )
        {
            outArchive << investigator._speed;
            outArchive << investigator._actorId;
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
        hashed_string monsterId;
        uint32        count = 0;
        // 조사자마다 속도 · 번호 · 체력 · 정신력 · 의지 · 힘(24) + 빠짐(1)
        const bool bHeadRead = StateArchiveUtil::readName( archive, monsterId ) && StateArchiveUtil::readCount( archive, 25, count );
        if ( bHeadRead == false || monsterId != _monster._id )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 괴물 정의 · 성공 눈은 사본이 그대로 든다.
        HorrorEncounter restored = *this;
        restored._listInvestigator.assign( count, HorrorInvestigator{} );
        for ( HorrorInvestigator& investigator : restored._listInvestigator )
        {
            archive >> investigator._speed;
            archive >> investigator._actorId;
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
        const bool bValid = bPartRead && archive.isOk() && 0 <= restored._turnCount && state <= static_cast<uint8>( HorrorEncounterState::Defeat );
        if ( bValid == false )
            return false;
        restored._state = static_cast<HorrorEncounterState>( state );
        *this           = std::move( restored );
        return true;
    }
} // namespace sw
