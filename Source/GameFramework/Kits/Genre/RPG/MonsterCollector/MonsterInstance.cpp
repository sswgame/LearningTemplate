#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterInstance.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    namespace
    {
        /** @brief 레벨 @p level 의 기술을 배웁니다. 슬롯이 차면 막힘 알림을 냅니다. */
        void learnLevelMoves( const MonsterCollectorCatalog& catalog, const MonsterSpeciesDef& species, MonsterInstance& inoutMonster, int32 level,
                              vector<MonsterGrowthEvent>& outListEvent )
        {
            for ( const MonsterLearnEntry& learn : species._listLearn )
            {
                if ( learn._level != level || inoutMonster.findMoveSlot( learn._moveID ).has_value() )
                    continue;
                MonsterGrowthEvent event;
                event._id    = learn._moveID;
                event._level = level;
                if ( MonsterInstance::learnMove( catalog, inoutMonster, learn._moveID ) )
                    event._kind = MonsterGrowthEvent::Kind::LearnedMove;
                else
                    event._kind = MonsterGrowthEvent::Kind::MoveLearnBlocked;
                outListEvent.push_back( event );
            }
        }
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

    optional<int32> MonsterInstance::findMoveSlot( const hashed_string& moveID ) const
    {
        if ( moveID.empty() )
            return nullopt;
        for ( int32 slot = 0; slot < kMoveSlotCount; ++slot )
        {
            if ( _arrMove[slot]._moveID == moveID )
                return slot;
        }
        return nullopt;
    }

    void MonsterInstance::serializeState( StateIO& io )
    {
        io.value( _speciesID );
        io.value( _natureID );
        io.value( _nickname );
        for ( MonsterMoveSlot& slot : _arrMove )
        {
            io.value( slot._moveID );
            io.value( slot._pp );
            io.value( slot._ppMax );
        }
        for ( int32 statIndex = 0; statIndex < kMonsterStatCount; ++statIndex )
        {
            io.value( _arrIv[statIndex] );
            io.value( _arrEv[statIndex] );
            io.value( _arrStat[statIndex] );
        }
        io.value( _exp );
        io.value( _level );
        io.value( _hp );
        io.value( _friendship );
        io.value( _statusTurns );
        io.enumValue( _status, MonsterStatus::Freeze );
        if ( io.isWriting() || io.isOk() == false )
            return;
        io.check( 1 <= _level && _level <= MonsterCollectorCatalog::kMaxLevel && 0 <= _exp && 0 <= _friendship && _friendship <= MonsterInstance::kMaxFriendship && 0 <= _statusTurns && _statusTurns <= MonsterInstance::kMaxStatusTurns );
        for ( const MonsterMoveSlot& slot : _arrMove )
        {
            io.check( 0 <= slot._pp && slot._pp <= slot._ppMax );
        }
        int32 evTotal = 0;
        for ( int32 statIndex = 0; statIndex < kMonsterStatCount; ++statIndex )
        {
            const int32 ev = _arrEv[statIndex];
            io.check( 0 <= _arrIv[statIndex] && _arrIv[statIndex] <= MonsterInstance::kMaxIv && 0 <= ev && ev <= MonsterInstance::kMaxEvPerStat && _arrStat[statIndex] >= 1 );
            evTotal += ev;
        }
        if ( io.isOk() )
        {
            io.check( evTotal <= MonsterInstance::kMaxEvTotal && 0 <= _hp && _hp <= getMaxHP() );
        }
    }

    bool MonsterInstance::isKnownTo( const MonsterCollectorCatalog& catalog ) const
    {
        if ( catalog.findSpecies( _speciesID ) == nullptr || ( _natureID.empty() == false && catalog.findNature( _natureID ) == nullptr ) )
            return false;
        for ( const MonsterMoveSlot& slot : _arrMove )
        {
            if ( slot.isEmpty() == false && catalog.findMove( slot._moveID ) == nullptr )
                return false;
        }
        return true;
    }

    int32 MonsterInstance::computeStat( MonsterStat stat, int32 baseStat, int32 iv, int32 ev, int32 level, int32 naturePercent )
    {
        const int32 clampedLevel = MathUtil::clamp( level, 1, MonsterCollectorCatalog::kMaxLevel );
        const int32 core         = ( 2 * baseStat + MathUtil::clamp( iv, 0, kMaxIv ) + MathUtil::clamp( ev, 0, kMaxEvPerStat ) / 4 ) * clampedLevel / 100;
        if ( stat == MonsterStat::HP )
            return core + clampedLevel + 10;
        return ( core + 5 ) * naturePercent / 100;
    }

    void MonsterInstance::recomputeStats( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster )
    {
        const MonsterSpeciesDef* pSpecies = catalog.findSpecies( inoutMonster._speciesID );
        if ( pSpecies == nullptr )
            return;
        const MonsterNatureDef* pNature  = catalog.findNature( inoutMonster._natureID );
        const int32             oldMaxHP = inoutMonster.getMaxHP();
        for ( int32 index = 0; index < kMonsterStatCount; ++index )
        {
            const MonsterStat stat          = static_cast<MonsterStat>( index );
            const int32       naturePercent = pNature != nullptr ? pNature->computePercent( stat ) : 100;
            inoutMonster._arrStat[index]    = computeStat( stat, pSpecies->_arrBaseStat[index], inoutMonster._arrIv[index], inoutMonster._arrEv[index],
                                                           inoutMonster._level, naturePercent );
        }
        if ( inoutMonster._hp > 0 )
            inoutMonster._hp = MathUtil::clamp( inoutMonster._hp + inoutMonster.getMaxHP() - oldMaxHP, 1, inoutMonster.getMaxHP() );
    }

    MonsterInstance MonsterInstance::createMonster( const MonsterCollectorCatalog& catalog, const hashed_string& speciesID, int32 level, GameRandom& random )
    {
        MonsterInstance          monster;
        const MonsterSpeciesDef* pSpecies = catalog.findSpecies( speciesID );
        if ( pSpecies == nullptr )
            return monster;
        // 범위 밖 레벨은 맞추지 않고 종이 없을 때와 같이 빈 개체로 거절한다.
        if ( level < 1 || level > MonsterCollectorCatalog::kMaxLevel )
        {
            SW_LOG_ERROR_ID( "MonsterInstance.MonsterRulesRefusedLevelMust", "MonsterRules: refused level %# for %# - it must be within [1, %#]", level, speciesID.c_str(), MonsterCollectorCatalog::kMaxLevel );
            return monster;
        }

        monster._speciesID = speciesID;
        monster._nickname  = pSpecies->_name;
        monster._level     = level;
        monster._exp       = MonsterCollectorCatalog::computeTotalExp( pSpecies->_expGroup, monster._level );
        for ( int32& iv : monster._arrIv )
        {
            iv = random.nextInt( 0, kMaxIv );
        }
        const vector<MonsterNatureDef>& listNature = catalog.getNatures();
        if ( listNature.empty() == false )
            monster._natureID = listNature[static_cast<size_t>( random.nextInt( 0, static_cast<int32>( listNature.size() ) - 1 ) )]._id;

        // 그 레벨까지 배운 것 중 마지막 네 개를 앞에서부터 채우고, 슬롯이 차면 가장 오래된 슬롯부터 밀어낸다.
        for ( const MonsterLearnEntry& learn : pSpecies->_listLearn )
        {
            if ( learn._level > monster._level )
                break;
            if ( monster.findMoveSlot( learn._moveID ).has_value() )
                continue;
            const MonsterMoveDef* pMove = catalog.findMove( learn._moveID );
            if ( pMove == nullptr )
                continue;
            int32 targetSlot = monster.countMoves();
            if ( targetSlot >= MonsterInstance::kMoveSlotCount )
            {
                // 가장 오래된 슬롯을 빼고 앞으로 당긴다. 그러면 슬롯 순서가 배운 순서를 유지한다.
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
        monster._hp = monster.getMaxHP();
        return monster;
    }

    void MonsterInstance::addEffortValues( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster, const MonsterSpeciesDef& defeated )
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

    int64 MonsterInstance::computeExpYield( const MonsterSpeciesDef& defeated, int32 defeatedLevel, bool bTrainer )
    {
        const int64 base = static_cast<int64>( defeated._baseExp ) * MathUtil::max( 1, defeatedLevel ) / 7;
        return bTrainer ? base * 3 / 2 : base;
    }

    int32 MonsterInstance::gainExp( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster, int64 amount, vector<MonsterGrowthEvent>& outListEvent )
    {
        const MonsterSpeciesDef* pSpecies       = catalog.findSpecies( inoutMonster._speciesID );
        const bool               bCannotGainExp = pSpecies == nullptr || amount <= 0 || inoutMonster._level >= MonsterCollectorCatalog::kMaxLevel;
        if ( bCannotGainExp )
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
            learnLevelMoves( catalog, *pSpecies, inoutMonster, inoutMonster._level, outListEvent );
        }
        if ( gained > 0 )
        {
            const hashed_string evolution = findEvolution( catalog, inoutMonster, hashed_string{} );
            if ( evolution.empty() == false )
                outListEvent.push_back( MonsterGrowthEvent{ evolution, inoutMonster._level, MonsterGrowthEvent::Kind::CanEvolve } );
        }
        return gained;
    }

    bool MonsterInstance::learnMove( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster, const hashed_string& moveID, int32 replaceSlot )
    {
        const MonsterMoveDef* pMove = catalog.findMove( moveID );
        if ( pMove == nullptr || inoutMonster.findMoveSlot( moveID ).has_value() )
            return false;
        int32 targetSlot = constant::invalid_index::kInt32;
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

    hashed_string MonsterInstance::findEvolution( const MonsterCollectorCatalog& catalog, const MonsterInstance& monster, const hashed_string& itemID )
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

    bool MonsterInstance::evolve( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster, const hashed_string& targetSpeciesID )
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

    void MonsterInstance::restore( MonsterInstance& inoutMonster )
    {
        inoutMonster._hp          = inoutMonster.getMaxHP();
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
        , _pCatalog{ nullptr }
        , _boxCapacity{ 30 }
    {
    }

    bool MonsterStorage::initialize( const MonsterCollectorCatalog* pCatalog, int32 boxCapacity )
    {
        if ( pCatalog == nullptr || boxCapacity < 0 )
        {
            SW_LOG_ERROR_ID( "MonsterInstance.MonsterStorageRefusedBoxCapacity", "MonsterStorage: refused box capacity %# - it must not be negative and needs a catalog", boxCapacity );
            return false;
        }
        _listParty.clear();
        _listBox.clear();
        _pCatalog    = pCatalog;
        _boxCapacity = boxCapacity;
        return true;
    }

    MonsterStoragePlace MonsterStorage::add( const MonsterInstance& monster )
    {
        if ( _pCatalog == nullptr || monster.isKnownTo( *_pCatalog ) == false )
            return MonsterStoragePlace::Refused;
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
        const bool bCannotDeposit = partyIndex < 0 || partyIndex >= static_cast<int32>( _listParty.size() ) || static_cast<int32>( _listBox.size() ) >= _boxCapacity;
        if ( bCannotDeposit )
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
        const bool bCannotWithdraw = boxIndex < 0 || boxIndex >= static_cast<int32>( _listBox.size() ) || static_cast<int32>( _listParty.size() ) >= kPartySize;
        if ( bCannotWithdraw )
            return false;
        _listParty.push_back( _listBox[static_cast<size_t>( boxIndex )] );
        _listBox.erase( _listBox.begin() + boxIndex );
        return true;
    }

    bool MonsterStorage::swapPartyOrder( int32 firstIndex, int32 secondIndex )
    {
        const int32 count             = static_cast<int32>( _listParty.size() );
        const bool  bInvalidSwapIndex = firstIndex < 0 || secondIndex < 0 || firstIndex >= count || secondIndex >= count;
        if ( bInvalidSwapIndex )
            return false;
        std::swap( _listParty[static_cast<size_t>( firstIndex )], _listParty[static_cast<size_t>( secondIndex )] );
        return true;
    }

    void MonsterStorage::restoreParty()
    {
        for ( MonsterInstance& monster : _listParty )
        {
            MonsterInstance::restore( monster );
        }
    }

    bool MonsterStorage::hasUsableMonster() const
    {
        return VectorUtil::containsIf( _listParty, [&]( const MonsterInstance& monster )
        { return monster.isFainted() == false; } );
    }

    void MonsterStorage::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        const auto serializeMonster = [&]( MonsterInstance& monster )
        {
            io.value( monster );
            if ( io.isReading() && io.isOk() )
            {
                io.check( monster.isKnownTo( *_pCatalog ) );
            }
        };
        io.list( _listParty, MonsterInstance::kStateMinBytes, serializeMonster );
        io.check( _listParty.size() <= static_cast<size_t>( kPartySize ) );
        io.list( _listBox, MonsterInstance::kStateMinBytes, serializeMonster );
        io.check( _listBox.size() <= static_cast<size_t>( _boxCapacity ) );
    }

} // namespace sw
