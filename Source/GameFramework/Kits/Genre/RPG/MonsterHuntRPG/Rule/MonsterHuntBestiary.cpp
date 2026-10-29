#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/MonsterHuntBestiary.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Character/Combat/Damage/ElementChart.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Catalog/MonsterHuntCatalog.h"

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

    void MonsterHuntBestiary::drainEvents( vector<MonsterHuntBestiaryEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
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

    void MonsterHuntBestiary::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listEntry.size() );
        for ( const Entry& entry : _listEntry )
        {
            StateArchiveUtil::writeName( outArchive, entry._monsterID );
            outArchive << entry._knowledge;
            outArchive << entry._killCount;
        }
    }

    bool MonsterHuntBestiary::readState( Archive& archive )
    {
        uint32 count = 0;
        // 괴물마다 이름(4) + 지식과 처치 수(8) 바이트
        if ( _pCatalog == nullptr || StateArchiveUtil::readCount( archive, 12, count ) == false )
            return false;
        vector<Entry> listEntry( count );
        for ( Entry& entry : listEntry )
        {
            if ( StateArchiveUtil::readName( archive, entry._monsterID ) == false || _pCatalog->findMonster( entry._monsterID ) == nullptr )
                return false;
            archive >> entry._knowledge;
            archive >> entry._killCount;
            const bool bValid = archive.isOk() && 0 <= entry._knowledge && 0 <= entry._killCount;
            if ( bValid == false )
                return false;
        }
        _listEntry = std::move( listEntry );
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
