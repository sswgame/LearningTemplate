#include "pch.h"

#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherBestiary.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Combat/ElementChart.h"
#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherCatalog.h"

namespace sw
{
    WitcherBestiary::WitcherBestiary()
        : _listEntry{}
        , _listEvent{}
        , _pCatalog{ nullptr }
        , _pChart{ nullptr }
    {
    }

    void WitcherBestiary::initialize( const WitcherCatalog* pCatalog, const ElementChart* pChart )
    {
        _pCatalog = pCatalog;
        _pChart   = pChart;
        _listEntry.clear();
        _listEvent.clear();
    }

    bool WitcherBestiary::readBook( const hashed_string& monsterId )
    {
        const WitcherMonsterDef* pMonster = _pCatalog != nullptr ? _pCatalog->findMonster( monsterId ) : nullptr;
        return pMonster != nullptr && raiseKnowledge( monsterId, pMonster->_readLevel );
    }

    bool WitcherBestiary::recordKill( const hashed_string& monsterId )
    {
        const WitcherMonsterDef* pMonster = _pCatalog != nullptr ? _pCatalog->findMonster( monsterId ) : nullptr;
        if ( pMonster == nullptr )
            return false;
        if ( findEntry( monsterId ) == nullptr )
        {
            Entry entry;
            entry._monsterId = monsterId;
            _listEntry.push_back( entry );
        }
        Entry* pEntry = findEntryMutable( monsterId );
        ++pEntry->_killCount;
        const int32 level = MathUtil::min( pMonster->_killCap, pEntry->_killCount / pMonster->_killsPerLevel );
        return raiseKnowledge( monsterId, level );
    }

    bool WitcherBestiary::investigate( const hashed_string& monsterId )
    {
        const WitcherMonsterDef* pMonster = _pCatalog != nullptr ? _pCatalog->findMonster( monsterId ) : nullptr;
        return pMonster != nullptr && raiseKnowledge( monsterId, pMonster->_investigateLevel );
    }

    int32 WitcherBestiary::getKnowledge( const hashed_string& monsterId ) const
    {
        const Entry* pEntry = findEntry( monsterId );
        return pEntry != nullptr ? pEntry->_knowledge : 0;
    }

    int32 WitcherBestiary::getKillCount( const hashed_string& monsterId ) const
    {
        const Entry* pEntry = findEntry( monsterId );
        return pEntry != nullptr ? pEntry->_killCount : 0;
    }

    void WitcherBestiary::collectKnownWeaknesses( const hashed_string& monsterId, vector<const WitcherWeakness*>& outListWeakness ) const
    {
        outListWeakness.clear();
        const WitcherMonsterDef* pMonster = _pCatalog != nullptr ? _pCatalog->findMonster( monsterId ) : nullptr;
        if ( pMonster == nullptr )
            return;
        const int32 knowledge = getKnowledge( monsterId );
        for ( const WitcherWeakness& weakness : pMonster->_listWeakness )
        {
            if ( weakness._knowledge <= knowledge )
                outListWeakness.push_back( &weakness );
        }
    }

    float32 WitcherBestiary::computeMultiplier( const hashed_string& monsterId, const hashed_string& attackElement ) const
    {
        const WitcherMonsterDef* pMonster = _pCatalog != nullptr ? _pCatalog->findMonster( monsterId ) : nullptr;
        if ( pMonster == nullptr || _pChart == nullptr || attackElement.empty() )
            return 1.0f;
        return _pChart->computeMultiplier( attackElement, pMonster->_listElement );
    }

    void WitcherBestiary::drainEvents( vector<WitcherBestiaryEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    WitcherBestiary::Entry* WitcherBestiary::findEntryMutable( const hashed_string& monsterId )
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._monsterId == monsterId )
                return &entry;
        }
        return nullptr;
    }

    const WitcherBestiary::Entry* WitcherBestiary::findEntry( const hashed_string& monsterId ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._monsterId == monsterId )
                return &entry;
        }
        return nullptr;
    }

    bool WitcherBestiary::raiseKnowledge( const hashed_string& monsterId, int32 level )
    {
        const WitcherMonsterDef* pMonster = _pCatalog->findMonster( monsterId );
        if ( findEntry( monsterId ) == nullptr )
        {
            Entry entry;
            entry._monsterId = monsterId;
            _listEntry.push_back( entry );
        }
        Entry*      pEntry   = findEntryMutable( monsterId );
        const int32 newLevel = MathUtil::min( level, pMonster->_maxKnowledge );
        if ( newLevel <= pEntry->_knowledge )
            return false;
        const int32 oldLevel = pEntry->_knowledge;
        pEntry->_knowledge   = newLevel;
        WitcherBestiaryEvent raised;
        raised._kind      = WitcherBestiaryEvent::Kind::KnowledgeRaised;
        raised._monsterId = monsterId;
        raised._value     = newLevel;
        _listEvent.push_back( raised );
        for ( const WitcherWeakness& weakness : pMonster->_listWeakness )
        {
            if ( weakness._knowledge <= oldLevel || weakness._knowledge > newLevel )
                continue;
            WitcherBestiaryEvent revealed;
            revealed._kind       = WitcherBestiaryEvent::Kind::WeaknessRevealed;
            revealed._monsterId  = monsterId;
            revealed._weaknessId = weakness._id;
            revealed._value      = newLevel;
            _listEvent.push_back( revealed );
        }
        return true;
    }
} // namespace sw
