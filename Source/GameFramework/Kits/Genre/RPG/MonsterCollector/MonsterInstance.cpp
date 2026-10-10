#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterInstance.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct MonsterInstanceInternal
        {
            /** @brief 레벨 @p level 의 기술을 배웁니다. 칸이 차면 막힘 알림을 냅니다. */
            static void learnLevelMoves( const MonsterCollectorCatalog& catalog, const MonsterSpeciesDef& species, MonsterInstance& inoutMonster, int32 level,
                                         vector<MonsterGrowthEvent>& outListEvent )
            {
                for ( const MonsterLearnEntry& learn : species._listLearn )
                {
                    if ( learn._level != level || inoutMonster.findMoveSlot( learn._moveID ) >= 0 )
                        continue;
                    MonsterGrowthEvent event;
                    event._id    = learn._moveID;
                    event._level = level;
                    if ( MonsterRules::learnMove( catalog, inoutMonster, learn._moveID ) )
                        event._kind = MonsterGrowthEvent::Kind::LearnedMove;
                    else
                        event._kind = MonsterGrowthEvent::Kind::MoveLearnBlocked;
                    outListEvent.push_back( event );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    int32 MonsterInstance::countMoves() const
    {
        int32 count = 0;
        for ( const MonsterMoveSlot& slot : _arrMove )
        {
            count += slot.isEmpty() ? 0 : 1;
        }
        return count;
    }

    int32 MonsterInstance::findMoveSlot( const hashed_string& moveID ) const
    {
        if ( moveID.empty() )
            return -1;
        for ( int32 slot = 0; slot < kMoveSlotCount; ++slot )
        {
            if ( _arrMove[slot]._moveID == moveID )
                return slot;
        }
        return -1;
    }

    void MonsterInstance::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, _speciesID );
        StateArchiveUtil::writeName( outArchive, _natureID );
        outArchive << string_view( _nickname );
        for ( const MonsterMoveSlot& slot : _arrMove )
        {
            StateArchiveUtil::writeName( outArchive, slot._moveID );
            outArchive << slot._pp;
            outArchive << slot._ppMax;
        }
        for ( int32 statIndex = 0; statIndex < kMonsterStatCount; ++statIndex )
        {
            outArchive << _arrIv[statIndex];
            outArchive << _arrEv[statIndex];
            outArchive << _arrStat[statIndex];
        }
        outArchive << _exp;
        outArchive << _level;
        outArchive << _hp;
        outArchive << _friendship;
        outArchive << _statusTurns;
        outArchive << static_cast<uint8>( _status );
    }

    bool MonsterInstance::readState( Archive& archive )
    {
        MonsterInstance restored;
        const bool      bNamesRead = StateArchiveUtil::readName( archive, restored._speciesID ) && StateArchiveUtil::readName( archive, restored._natureID );
        if ( bNamesRead == false )
            return false;
        archive >> restored._nickname;
        for ( MonsterMoveSlot& slot : restored._arrMove )
        {
            if ( StateArchiveUtil::readName( archive, slot._moveID ) == false )
                return false;
            archive >> slot._pp;
            archive >> slot._ppMax;
        }
        for ( int32 statIndex = 0; statIndex < kMonsterStatCount; ++statIndex )
        {
            archive >> restored._arrIv[statIndex];
            archive >> restored._arrEv[statIndex];
            archive >> restored._arrStat[statIndex];
        }
        uint8 status = 0;
        archive >> restored._exp;
        archive >> restored._level;
        archive >> restored._hp;
        archive >> restored._friendship;
        archive >> restored._statusTurns;
        archive >> status;
        const bool bValid = archive.isOk() && 1 <= restored._level && restored._level <= MonsterCollectorCatalog::kMaxLevel &&
                            status <= static_cast<uint8>( MonsterStatus::Freeze ) && 0 <= restored._exp;
        if ( bValid == false )
            return false;
        restored._status = static_cast<MonsterStatus>( status );
        *this            = std::move( restored );
        return true;
    }

    int32 MonsterRules::computeStat( MonsterStat stat, int32 baseStat, int32 iv, int32 ev, int32 level, int32 naturePercent )
    {
        const int32 clampedLevel = MathUtil::clamp( level, 1, MonsterCollectorCatalog::kMaxLevel );
        const int32 core         = ( 2 * baseStat + MathUtil::clamp( iv, 0, kMaxIv ) + MathUtil::clamp( ev, 0, kMaxEvPerStat ) / 4 ) * clampedLevel / 100;
        if ( stat == MonsterStat::Hp )
            return core + clampedLevel + 10;
        return ( core + 5 ) * naturePercent / 100;
    }

    void MonsterRules::recomputeStats( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster )
    {
        const MonsterSpeciesDef* pSpecies = catalog.findSpecies( inoutMonster._speciesID );
        if ( pSpecies == nullptr )
            return;
        const MonsterNatureDef* pNature  = catalog.findNature( inoutMonster._natureID );
        const int32             oldMaxHp = inoutMonster.getMaxHp();
        for ( int32 index = 0; index < kMonsterStatCount; ++index )
        {
            const MonsterStat stat          = static_cast<MonsterStat>( index );
            const int32       naturePercent = pNature != nullptr ? pNature->computePercent( stat ) : 100;
            inoutMonster._arrStat[index]    = computeStat( stat, pSpecies->_arrBaseStat[index], inoutMonster._arrIv[index], inoutMonster._arrEv[index],
                                                           inoutMonster._level, naturePercent );
        }
        if ( inoutMonster._hp > 0 )
            inoutMonster._hp = MathUtil::clamp( inoutMonster._hp + inoutMonster.getMaxHp() - oldMaxHp, 1, inoutMonster.getMaxHp() );
    }

    MonsterInstance MonsterRules::createMonster( const MonsterCollectorCatalog& catalog, const hashed_string& speciesID, int32 level, GameRandom& random )
    {
        MonsterInstance          monster;
        const MonsterSpeciesDef* pSpecies = catalog.findSpecies( speciesID );
        if ( pSpecies == nullptr )
            return monster;

        monster._speciesID = speciesID;
        monster._nickname  = pSpecies->_name;
        monster._level     = MathUtil::clamp( level, 1, MonsterCollectorCatalog::kMaxLevel );
        monster._exp       = MonsterCollectorCatalog::computeTotalExp( pSpecies->_expGroup, monster._level );
        for ( int32& iv : monster._arrIv )
        {
            iv = random.nextInt( 0, kMaxIv );
        }
        const vector<MonsterNatureDef>& listNature = catalog.getNatures();
        if ( listNature.empty() == false )
            monster._natureID = listNature[static_cast<size_t>( random.nextInt( 0, static_cast<int32>( listNature.size() ) - 1 ) )]._id;

        // 그 레벨까지 배운 것 중 마지막 넷 — 앞에서부터 채우고, 차면 가장 오래된 칸부터 밀어낸다.
        for ( const MonsterLearnEntry& learn : pSpecies->_listLearn )
        {
            if ( learn._level > monster._level )
                break;
            if ( monster.findMoveSlot( learn._moveID ) >= 0 )
                continue;
            const MonsterMoveDef* pMove = catalog.findMove( learn._moveID );
            if ( pMove == nullptr )
                continue;
            int32 targetSlot = monster.countMoves();
            if ( targetSlot >= MonsterInstance::kMoveSlotCount )
            {
                // 가장 오래된 칸을 빼고 앞으로 당긴다 — 칸 순서가 배운 순서를 유지한다.
                for ( int32 slot = 0; slot + 1 < MonsterInstance::kMoveSlotCount; ++slot )
                {
                    monster._arrMove[slot] = monster._arrMove[slot + 1];
                }
                targetSlot = MonsterInstance::kMoveSlotCount - 1;
            }
            monster._arrMove[targetSlot] = MonsterMoveSlot{ learn._moveID, pMove->_pp, pMove->_pp };
        }

        monster._hp = 1;
        recomputeStats( catalog, monster );
        monster._hp = monster.getMaxHp();
        return monster;
    }

    void MonsterRules::addEffortValues( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster, const MonsterSpeciesDef& defeated )
    {
        int32 total = 0;
        for ( const int32 ev : inoutMonster._arrEv )
        {
            total += ev;
        }
        for ( int32 index = 0; index < kMonsterStatCount; ++index )
        {
            const int32 room = MathUtil::min( kMaxEvPerStat - inoutMonster._arrEv[index], kMaxEvTotal - total );
            const int32 gain = MathUtil::clamp( defeated._arrEvYield[index], 0, MathUtil::max( 0, room ) );
            inoutMonster._arrEv[index] += gain;
            total += gain;
        }
        recomputeStats( catalog, inoutMonster );
    }

    int64 MonsterRules::computeExpYield( const MonsterSpeciesDef& defeated, int32 defeatedLevel, bool bTrainer )
    {
        const int64 base = static_cast<int64>( defeated._baseExp ) * MathUtil::max( 1, defeatedLevel ) / 7;
        return bTrainer ? base * 3 / 2 : base;
    }

    int32 MonsterRules::gainExp( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster, int64 amount, vector<MonsterGrowthEvent>& outListEvent )
    {
        const MonsterSpeciesDef* pSpecies = catalog.findSpecies( inoutMonster._speciesID );
        if ( pSpecies == nullptr || amount <= 0 || inoutMonster._level >= MonsterCollectorCatalog::kMaxLevel )
            return 0;

        const int64 maxExp = MonsterCollectorCatalog::computeTotalExp( pSpecies->_expGroup, MonsterCollectorCatalog::kMaxLevel );
        inoutMonster._exp  = MathUtil::min( maxExp, inoutMonster._exp + amount );
        const int32 target = MonsterCollectorCatalog::computeLevelForExp( pSpecies->_expGroup, inoutMonster._exp );
        int32       gained = 0;
        while ( inoutMonster._level < target )
        {
            ++inoutMonster._level;
            ++gained;
            inoutMonster._friendship = MathUtil::min( kMaxFriendship, inoutMonster._friendship + kFriendshipPerLevel );
            recomputeStats( catalog, inoutMonster );
            outListEvent.push_back( MonsterGrowthEvent{ hashed_string{}, inoutMonster._level, MonsterGrowthEvent::Kind::LevelUp } );
            MonsterInstanceInternal::learnLevelMoves( catalog, *pSpecies, inoutMonster, inoutMonster._level, outListEvent );
        }
        if ( gained > 0 )
        {
            const hashed_string evolution = findEvolution( catalog, inoutMonster, hashed_string{} );
            if ( evolution.empty() == false )
                outListEvent.push_back( MonsterGrowthEvent{ evolution, inoutMonster._level, MonsterGrowthEvent::Kind::CanEvolve } );
        }
        return gained;
    }

    bool MonsterRules::learnMove( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster, const hashed_string& moveID, int32 replaceSlot )
    {
        const MonsterMoveDef* pMove = catalog.findMove( moveID );
        if ( pMove == nullptr || inoutMonster.findMoveSlot( moveID ) >= 0 )
            return false;
        int32 targetSlot = -1;
        for ( int32 slot = 0; slot < MonsterInstance::kMoveSlotCount; ++slot )
        {
            if ( inoutMonster._arrMove[slot].isEmpty() )
            {
                targetSlot = slot;
                break;
            }
        }
        if ( targetSlot < 0 )
        {
            if ( replaceSlot < 0 || replaceSlot >= MonsterInstance::kMoveSlotCount )
                return false;
            targetSlot = replaceSlot;
        }
        inoutMonster._arrMove[targetSlot] = MonsterMoveSlot{ moveID, pMove->_pp, pMove->_pp };
        return true;
    }

    hashed_string MonsterRules::findEvolution( const MonsterCollectorCatalog& catalog, const MonsterInstance& monster, const hashed_string& itemID )
    {
        const MonsterSpeciesDef* pSpecies = catalog.findSpecies( monster._speciesID );
        if ( pSpecies == nullptr )
            return hashed_string{};
        for ( const MonsterEvolutionDef& evolution : pSpecies->_listEvolution )
        {
            if ( evolution._itemID != itemID )
                continue;
            if ( evolution._level > 0 && monster._level < evolution._level )
                continue;
            if ( evolution._friendship > 0 && monster._friendship < evolution._friendship )
                continue;
            if ( catalog.findSpecies( evolution._targetID ) == nullptr )
                continue;
            return evolution._targetID;
        }
        return hashed_string{};
    }

    bool MonsterRules::evolve( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster, const hashed_string& targetSpeciesID )
    {
        const MonsterSpeciesDef* pFrom = catalog.findSpecies( inoutMonster._speciesID );
        const MonsterSpeciesDef* pTo   = catalog.findSpecies( targetSpeciesID );
        if ( pFrom == nullptr || pTo == nullptr )
            return false;
        // 이름을 따로 붙이지 않았으면 새 종 이름을 따른다.
        if ( inoutMonster._nickname == pFrom->_name )
            inoutMonster._nickname = pTo->_name;
        inoutMonster._speciesID = targetSpeciesID;
        recomputeStats( catalog, inoutMonster );
        return true;
    }

    void MonsterRules::restore( MonsterInstance& inoutMonster )
    {
        inoutMonster._hp          = inoutMonster.getMaxHp();
        inoutMonster._status      = MonsterStatus::None;
        inoutMonster._statusTurns = 0;
        for ( MonsterMoveSlot& slot : inoutMonster._arrMove )
        {
            slot._pp = slot._ppMax;
        }
    }

    MonsterStorage::MonsterStorage()
        : _listParty{}
        , _listBox{}
        , _boxCapacity{ 30 }
    {
    }

    void MonsterStorage::initialize( int32 boxCapacity )
    {
        _listParty.clear();
        _listBox.clear();
        _boxCapacity = MathUtil::max( 0, boxCapacity );
    }

    MonsterStoragePlace MonsterStorage::add( const MonsterInstance& monster )
    {
        if ( static_cast<int32>( _listParty.size() ) < kPartySize )
        {
            _listParty.push_back( monster );
            return MonsterStoragePlace::Party;
        }
        if ( static_cast<int32>( _listBox.size() ) < _boxCapacity )
        {
            _listBox.push_back( monster );
            return MonsterStoragePlace::Box;
        }
        return MonsterStoragePlace::Full;
    }

    bool MonsterStorage::depositToBox( int32 partyIndex )
    {
        if ( partyIndex < 0 || partyIndex >= static_cast<int32>( _listParty.size() ) || static_cast<int32>( _listBox.size() ) >= _boxCapacity )
            return false;
        int32 usableOthers = 0;
        for ( int32 index = 0; index < static_cast<int32>( _listParty.size() ); ++index )
        {
            usableOthers += ( index != partyIndex && _listParty[static_cast<size_t>( index )].isFainted() == false ) ? 1 : 0;
        }
        if ( usableOthers == 0 )
            return false;
        _listBox.push_back( _listParty[static_cast<size_t>( partyIndex )] );
        _listParty.erase( _listParty.begin() + partyIndex );
        return true;
    }

    bool MonsterStorage::withdrawFromBox( int32 boxIndex )
    {
        if ( boxIndex < 0 || boxIndex >= static_cast<int32>( _listBox.size() ) || static_cast<int32>( _listParty.size() ) >= kPartySize )
            return false;
        _listParty.push_back( _listBox[static_cast<size_t>( boxIndex )] );
        _listBox.erase( _listBox.begin() + boxIndex );
        return true;
    }

    bool MonsterStorage::swapPartyOrder( int32 firstIndex, int32 secondIndex )
    {
        const int32 count = static_cast<int32>( _listParty.size() );
        if ( firstIndex < 0 || secondIndex < 0 || firstIndex >= count || secondIndex >= count )
            return false;
        std::swap( _listParty[static_cast<size_t>( firstIndex )], _listParty[static_cast<size_t>( secondIndex )] );
        return true;
    }

    void MonsterStorage::restoreParty()
    {
        for ( MonsterInstance& monster : _listParty )
        {
            MonsterRules::restore( monster );
        }
    }

    bool MonsterStorage::hasUsableMonster() const
    {
        for ( const MonsterInstance& monster : _listParty )
        {
            if ( monster.isFainted() == false )
                return true;
        }
        return false;
    }

    void MonsterStorage::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listParty.size() );
        for ( const MonsterInstance& monster : _listParty )
        {
            monster.writeState( outArchive );
        }
        outArchive << static_cast<uint32>( _listBox.size() );
        for ( const MonsterInstance& monster : _listBox )
        {
            monster.writeState( outArchive );
        }
    }

    bool MonsterStorage::readState( Archive& archive )
    {
        uint32 partyCount = 0;
        if ( StateArchiveUtil::readCount( archive, MonsterInstance::kStateMinBytes, partyCount ) == false || partyCount > static_cast<uint32>( kPartySize ) )
            return false;
        vector<MonsterInstance> listParty( partyCount );
        for ( MonsterInstance& monster : listParty )
        {
            if ( monster.readState( archive ) == false )
                return false;
        }
        uint32 boxCount = 0;
        if ( StateArchiveUtil::readCount( archive, MonsterInstance::kStateMinBytes, boxCount ) == false || boxCount > static_cast<uint32>( _boxCapacity ) )
            return false;
        vector<MonsterInstance> listBox( boxCount );
        for ( MonsterInstance& monster : listBox )
        {
            if ( monster.readState( archive ) == false )
                return false;
        }
        _listParty = std::move( listParty );
        _listBox   = std::move( listBox );
        return true;
    }
} // namespace sw
