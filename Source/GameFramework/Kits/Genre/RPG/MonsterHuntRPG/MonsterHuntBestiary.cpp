#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntBestiary.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntCatalog.h"
#include "GameFramework/Runtime/Character/Combat/Damage/ElementChart.h"

namespace sw
{
    MonsterHuntBestiary::MonsterHuntBestiary()
        : _listEntry{}
        , _eventBuffer{}
        , _pCatalog{ nullptr }
        , _pChart{ nullptr }
    {
    }

    void MonsterHuntBestiary::initialize( const MonsterHuntCatalog* pCatalog, const ElementChart* pChart )
    {
        _pCatalog = pCatalog;
        _pChart   = pChart;
        _listEntry.clear();
        _eventBuffer.clear();
    }

    bool MonsterHuntBestiary::readBook( const hashed_string& monsterID )
    {
        const MonsterHuntMonsterDef* pMonster = _pCatalog != nullptr ? _pCatalog->findMonster( monsterID ) : nullptr;
        return pMonster != nullptr && raiseKnowledge( monsterID, pMonster->_readLevel );
    }

    bool MonsterHuntBestiary::recordKill( const hashed_string& monsterID )
    {
        const MonsterHuntMonsterDef* pMonster = _pCatalog != nullptr ? _pCatalog->findMonster( monsterID ) : nullptr;
        if ( pMonster == nullptr )
            return false;
        if ( findEntry( monsterID ) == nullptr )
        {
            Entry entry;
            entry._monsterID = monsterID;
            _listEntry.push_back( entry );
        }
        Entry* pEntry = findEntryMutable( monsterID );
        ++pEntry->_killCount;
        const int32 level = MathUtil::min( pMonster->_killCap, pEntry->_killCount / pMonster->_killsPerLevel );
        return raiseKnowledge( monsterID, level );
    }

    bool MonsterHuntBestiary::investigate( const hashed_string& monsterID )
    {
        const MonsterHuntMonsterDef* pMonster = _pCatalog != nullptr ? _pCatalog->findMonster( monsterID ) : nullptr;
        return pMonster != nullptr && raiseKnowledge( monsterID, pMonster->_investigateLevel );
    }

    int32 MonsterHuntBestiary::getKnowledge( const hashed_string& monsterID ) const
    {
        const Entry* pEntry = findEntry( monsterID );
        return pEntry != nullptr ? pEntry->_knowledge : 0;
    }

    int32 MonsterHuntBestiary::getKillCount( const hashed_string& monsterID ) const
    {
        const Entry* pEntry = findEntry( monsterID );
        return pEntry != nullptr ? pEntry->_killCount : 0;
    }

    void MonsterHuntBestiary::collectKnownWeaknesses( const hashed_string& monsterID, vector<const MonsterHuntWeakness*>& outListWeakness ) const
    {
        outListWeakness.clear();
        const MonsterHuntMonsterDef* pMonster = _pCatalog != nullptr ? _pCatalog->findMonster( monsterID ) : nullptr;
        if ( pMonster == nullptr )
            return;
        const int32 knowledge = getKnowledge( monsterID );
        for ( const MonsterHuntWeakness& weakness : pMonster->_listWeakness )
        {
            if ( weakness._knowledge <= knowledge )
                outListWeakness.push_back( &weakness );
        }
    }

    float32 MonsterHuntBestiary::computeMultiplier( const hashed_string& monsterID, const hashed_string& attackElement ) const
    {
        const MonsterHuntMonsterDef* pMonster             = _pCatalog != nullptr ? _pCatalog->findMonster( monsterID ) : nullptr;
        const bool                   bNoElementMultiplier = pMonster == nullptr || _pChart == nullptr || attackElement.empty();
        if ( bNoElementMultiplier )
            return 1.0f;
        return _pChart->computeMultiplier( attackElement, pMonster->_listElement );
    }

    void MonsterHuntBestiary::pollEvents( vector<MonsterHuntBestiaryEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    MonsterHuntBestiary::Entry* MonsterHuntBestiary::findEntryMutable( const hashed_string& monsterID )
    {
        return VectorUtil::findBy<&Entry::_monsterID>( _listEntry, monsterID );
    }

    const MonsterHuntBestiary::Entry* MonsterHuntBestiary::findEntry( const hashed_string& monsterID ) const
    {
        return VectorUtil::findBy<&Entry::_monsterID>( _listEntry, monsterID );
    }

    bool MonsterHuntBestiary::raiseKnowledge( const hashed_string& monsterID, int32 level )
    {
        const MonsterHuntMonsterDef* pMonster = _pCatalog->findMonster( monsterID );
        if ( findEntry( monsterID ) == nullptr )
        {
            Entry entry;
            entry._monsterID = monsterID;
            _listEntry.push_back( entry );
        }
        Entry*      pEntry   = findEntryMutable( monsterID );
        const int32 newLevel = MathUtil::min( level, pMonster->_maxKnowledge );
        if ( newLevel <= pEntry->_knowledge )
            return false;
        const int32 oldLevel = pEntry->_knowledge;
        pEntry->_knowledge   = newLevel;
        MonsterHuntBestiaryEvent raised;
        raised._kind      = MonsterHuntBestiaryEvent::Kind::KnowledgeRaised;
        raised._monsterID = monsterID;
        raised._value     = newLevel;
        _eventBuffer.push( raised );
        for ( const MonsterHuntWeakness& weakness : pMonster->_listWeakness )
        {
            if ( weakness._knowledge <= oldLevel || weakness._knowledge > newLevel )
                continue;
            MonsterHuntBestiaryEvent revealed;
            revealed._kind       = MonsterHuntBestiaryEvent::Kind::WeaknessRevealed;
            revealed._monsterID  = monsterID;
            revealed._weaknessID = weakness._id;
            revealed._value      = newLevel;
            _eventBuffer.push( revealed );
        }
        return true;
    }

    void MonsterHuntBestiary::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        io.reflected( *this );
        if ( io.isWriting() )
            return;
        for ( const Entry& entry : _listEntry )
        {
            io.check( _pCatalog->findMonster( entry._monsterID ) != nullptr ); // 카탈로그 검사만 손으로 — 수의 범위는 `PROPERTY( Min )` 이 지킨다
        }
        _eventBuffer.clear();
    }

} // namespace sw
