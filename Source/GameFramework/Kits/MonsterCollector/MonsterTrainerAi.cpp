#include "pch.h"

#include "GameFramework/Kits/MonsterCollector/MonsterTrainerAi.h"

#include "GameFramework/Combat/ElementChart.h"

namespace sw
{
    MonsterAction MonsterTrainerAi::chooseAction( const MonsterBattle& battle, int32 side )
    {
        const int32 activeIndex = battle.getActiveIndex( side );
        if ( isDisadvantaged( battle, side, activeIndex ) )
        {
            const vector<MonsterInstance>& listMonster = battle.getParty( side );
            const int32                    foeSide     = 1 - side;
            int32                          bestIndex   = -1;
            float32                        bestDamage  = -1.0f;
            for ( int32 index = 0; index < static_cast<int32>( listMonster.size() ); ++index )
            {
                const MonsterInstance& candidate = listMonster[static_cast<size_t>( index )];
                if ( index == activeIndex || candidate.isFainted() )
                    continue;
                if ( computeThreatMultiplier( battle, candidate, foeSide ) >= 2.0f || computeBestOwnMultiplier( battle, candidate, foeSide ) < 1.0f )
                    continue;
                float32 expectedDamage = 0.0f;
                (void)chooseBestMoveSlot( battle, side, index, expectedDamage );
                if ( expectedDamage > bestDamage )
                {
                    bestDamage = expectedDamage;
                    bestIndex  = index;
                }
            }
            if ( bestIndex >= 0 )
                return MonsterAction::makeSwitch( bestIndex );
        }
        float32     expectedDamage = 0.0f;
        const int32 slot           = chooseBestMoveSlot( battle, side, activeIndex, expectedDamage );
        return slot >= 0 ? MonsterAction::makeMove( slot ) : MonsterAction{};
    }

    int32 MonsterTrainerAi::chooseBestMoveSlot( const MonsterBattle& battle, int32 side, int32 partyIndex, float32& outExpectedDamage )
    {
        outExpectedDamage                          = 0.0f;
        const vector<MonsterInstance>& listMonster = battle.getParty( side );
        if ( partyIndex < 0 || partyIndex >= static_cast<int32>( listMonster.size() ) )
            return -1;
        const MonsterInstance& monster    = listMonster[static_cast<size_t>( partyIndex )];
        const int32            foeSide    = 1 - side;
        const int32            stageSide  = partyIndex == battle.getActiveIndex( side ) ? side : -1;
        int32                  bestSlot   = -1;
        float32                bestDamage = -1.0f;
        for ( int32 slot = 0; slot < MonsterInstance::kMoveSlotCount; ++slot )
        {
            const MonsterMoveSlot& moveSlot = monster._arrMove[slot];
            if ( moveSlot.isEmpty() || moveSlot._pp <= 0 )
                continue;
            const float32 damage = battle.computeExpectedDamage( stageSide, monster, moveSlot._moveId, foeSide, battle.getActive( foeSide ) );
            if ( damage > bestDamage )
            {
                bestDamage = damage;
                bestSlot   = slot;
            }
        }
        outExpectedDamage = bestDamage > 0.0f ? bestDamage : 0.0f;
        return bestSlot;
    }

    bool MonsterTrainerAi::isDisadvantaged( const MonsterBattle& battle, int32 side, int32 partyIndex )
    {
        const vector<MonsterInstance>& listMonster = battle.getParty( side );
        if ( partyIndex < 0 || partyIndex >= static_cast<int32>( listMonster.size() ) )
            return false;
        const MonsterInstance& monster = listMonster[static_cast<size_t>( partyIndex )];
        const int32            foeSide = 1 - side;
        return computeBestOwnMultiplier( battle, monster, foeSide ) < 1.0f && computeThreatMultiplier( battle, monster, foeSide ) >= 2.0f;
    }

    int32 MonsterTrainerAi::chooseReplacement( const MonsterBattle& battle, int32 side )
    {
        const vector<MonsterInstance>& listMonster = battle.getParty( side );
        int32                          bestIndex   = -1;
        float32                        bestDamage  = -1.0f;
        for ( int32 index = 0; index < static_cast<int32>( listMonster.size() ); ++index )
        {
            if ( listMonster[static_cast<size_t>( index )].isFainted() )
                continue;
            float32 expectedDamage = 0.0f;
            (void)chooseBestMoveSlot( battle, side, index, expectedDamage );
            if ( expectedDamage > bestDamage )
            {
                bestDamage = expectedDamage;
                bestIndex  = index;
            }
        }
        return bestIndex;
    }

    float32 MonsterTrainerAi::computeBestOwnMultiplier( const MonsterBattle& battle, const MonsterInstance& monster, int32 foeSide )
    {
        const MonsterCatalog* pCatalog = battle.getCatalog();
        float32               best     = 0.0f;
        bool                  bAny     = false;
        for ( const MonsterMoveSlot& moveSlot : monster._arrMove )
        {
            const MonsterMoveDef* pMove = pCatalog != nullptr ? pCatalog->findMove( moveSlot._moveId ) : nullptr;
            if ( pMove == nullptr || pMove->isDamaging() == false || moveSlot._pp <= 0 )
                continue;
            const float32 multiplier = battle.computeTypeMultiplier( pMove->_type, battle.getActive( foeSide ) );
            best                     = bAny ? ( multiplier > best ? multiplier : best ) : multiplier;
            bAny                     = true;
        }
        return bAny ? best : 1.0f;
    }

    float32 MonsterTrainerAi::computeThreatMultiplier( const MonsterBattle& battle, const MonsterInstance& monster, int32 foeSide )
    {
        const MonsterCatalog*    pCatalog = battle.getCatalog();
        const ElementChart*      pChart   = battle.getChart();
        const MonsterSpeciesDef* pFoe     = pCatalog != nullptr ? pCatalog->findSpecies( battle.getActive( foeSide )._speciesId ) : nullptr;
        const MonsterSpeciesDef* pSelf    = pCatalog != nullptr ? pCatalog->findSpecies( monster._speciesId ) : nullptr;
        if ( pFoe == nullptr || pSelf == nullptr || pChart == nullptr )
            return 1.0f;
        float32 threat = 0.0f;
        for ( const hashed_string& type : pFoe->_listType )
        {
            const float32 multiplier = pChart->computeMultiplier( type, pSelf->_listType );
            threat                   = multiplier > threat ? multiplier : threat;
        }
        return threat;
    }
} // namespace sw
