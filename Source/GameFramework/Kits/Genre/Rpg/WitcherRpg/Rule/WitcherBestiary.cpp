#include "pch.h"

#include "GameFramework/Kits/Genre/Rpg/WitcherRpg/Rule/WitcherBestiary.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Combat/Damage/ElementChart.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/Rpg/WitcherRpg/Catalog/WitcherCatalog.h"

namespace sw
{
    WitcherBestiary::WitcherBestiary()
        : _listEntry{}
        , _eventBuffer{}
        , _pCatalog{ nullptr }
        , _pChart{ nullptr }
    {
    }

    void WitcherBestiary::initialize( const WitcherCatalog* pCatalog, const ElementChart* pChart )
    {
        _pCatalog = pCatalog;
        _pChart   = pChart;
        _listEntry.clear();
        _eventBuffer.clear();
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
        _eventBuffer.drainTo( outListEvent );
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
        _eventBuffer.push( raised );
        for ( const WitcherWeakness& weakness : pMonster->_listWeakness )
        {
            if ( weakness._knowledge <= oldLevel || weakness._knowledge > newLevel )
                continue;
            WitcherBestiaryEvent revealed;
            revealed._kind       = WitcherBestiaryEvent::Kind::WeaknessRevealed;
            revealed._monsterId  = monsterId;
            revealed._weaknessId = weakness._id;
            revealed._value      = newLevel;
            _eventBuffer.push( revealed );
        }
        return true;
    }

    void WitcherBestiary::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listEntry.size() );
        for ( const Entry& entry : _listEntry )
        {
            StateArchiveUtil::writeName( outArchive, entry._monsterId );
            outArchive << entry._knowledge;
            outArchive << entry._killCount;
        }
    }

    bool WitcherBestiary::readState( Archive& archive )
    {
        uint32 count = 0;
        // 괴물마다 이름(4) + 지식 · 처치 수(8)
        if ( _pCatalog == nullptr || StateArchiveUtil::readCount( archive, 12, count ) == false )
            return false;
        vector<Entry> listEntry( count );
        for ( Entry& entry : listEntry )
        {
            if ( StateArchiveUtil::readName( archive, entry._monsterId ) == false || _pCatalog->findMonster( entry._monsterId ) == nullptr )
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
