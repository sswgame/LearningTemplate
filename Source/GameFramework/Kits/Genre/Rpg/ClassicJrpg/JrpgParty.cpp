#include "pch.h"

#include "GameFramework/Kits/Genre/Rpg/ClassicJrpg/JrpgParty.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Foundation/Framework/GameStateRefs.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        /** @brief 틱마다 쓰는 이름 — 리터럴을 매번 intern 하지 않게 한 번만 만든다. */
        struct JrpgPartyInternal
        {
            static const hashed_string& getAttackName()
            {
                static const hashed_string name( "attack" );
                return name;
            }
            static const hashed_string& getDefenseName()
            {
                static const hashed_string name( "defense" );
                return name;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool JrpgMember::knowsSpell( const hashed_string& spellID ) const
    {
        for ( const hashed_string& entry : _listSpell )
        {
            if ( entry == spellID )
                return true;
        }
        return false;
    }

    int32 JrpgMember::findProficiency( const hashed_string& manualID ) const
    {
        for ( const JrpgManualProgress& progress : _listManual )
        {
            if ( progress._manualID == manualID )
                return progress._proficiency;
        }
        return -1;
    }

    JrpgParty::JrpgParty()
        : _listMember{}
        , _eventBuffer{}
        , _equipLayout{}
        , _pCatalog{ nullptr }
        , _pItemCatalog{ nullptr }
        , _pInventory{ nullptr }
        , _pWallet{ nullptr }
    {
    }

    void JrpgParty::initialize( const JrpgCatalog* pCatalog, const ItemCatalog* pItemCatalog, const GameStateRefs& refs, string_view equipLayout )
    {
        _pCatalog     = pCatalog;
        _pItemCatalog = pItemCatalog;
        _equipLayout  = string( equipLayout );
        _listMember.clear();
        _eventBuffer.clear();
        _pInventory = refs._pInventory;
        _pWallet    = refs._pWallet;
    }

    int32 JrpgParty::addMember( const hashed_string& memberID, string_view name, const hashed_string& classID, int32 level )
    {
        const JrpgClassDef* pClass = _pCatalog != nullptr ? _pCatalog->findClass( classID ) : nullptr;
        if ( pClass == nullptr || static_cast<int32>( _listMember.size() ) >= kMaxMembers || findMemberIndex( memberID ) >= 0 )
            return -1;

        JrpgMember member;
        member._id      = memberID;
        member._classID = classID;
        member._name    = string( name );
        member._equipment.initialize( _pItemCatalog, _equipLayout );
        for ( int32 index = 0; index < kJrpgStatCount; ++index )
        {
            member._arrStat[index] = pClass->_arrBase[index];
        }
        const int32 targetLevel = MathUtil::clamp( level, 1, _pCatalog->getCurve().getMaxLevel() );
        member._level.setLevel( _pCatalog->getCurve(), targetLevel );
        _listMember.push_back( member );

        const int32 memberIndex = static_cast<int32>( _listMember.size() ) - 1;
        learnSpellsAtLevel( memberIndex, *pClass, 1 );
        for ( int32 grownLevel = 2; grownLevel <= targetLevel; ++grownLevel )
        {
            growOneLevel( memberIndex, *pClass );
            learnSpellsAtLevel( memberIndex, *pClass, grownLevel );
        }
        JrpgMember& added = _listMember.back();
        added._hp         = added.getStat( JrpgStat::MaxHp );
        added._mp         = added.getStat( JrpgStat::MaxMp );
        _eventBuffer.clear(); // 처음 만든 멤버의 성장은 알리지 않는다
        return memberIndex;
    }

    JrpgClassChangeResult JrpgParty::changeClass( int32 memberIndex, const hashed_string& classID, int32 minLevel )
    {
        if ( isValidIndex( memberIndex ) == false )
            return JrpgClassChangeResult::UnknownMember;
        const JrpgClassDef* pClass = _pCatalog != nullptr ? _pCatalog->findClass( classID ) : nullptr;
        if ( pClass == nullptr )
            return JrpgClassChangeResult::UnknownClass;
        JrpgMember& member = _listMember[static_cast<size_t>( memberIndex )];
        if ( member._classID == classID )
            return JrpgClassChangeResult::SameClass;
        if ( member.isAlive() == false )
            return JrpgClassChangeResult::Dead;
        if ( member._level.getLevel() < minLevel )
            return JrpgClassChangeResult::LevelTooLow;
        if ( pClass->_requiredItem.empty() == false && ( _pInventory == nullptr || _pInventory->hasItem( pClass->_requiredItem ) == false ) )
            return JrpgClassChangeResult::MissingItem;

        member._classID = classID;
        for ( int32 index = 0; index < kJrpgStatCount; ++index )
        {
            member._arrStat[index] /= 2;
        }
        member._arrStat[static_cast<size_t>( JrpgStat::MaxHp )] = MathUtil::max( 1, member.getStat( JrpgStat::MaxHp ) );
        member._hp                                              = MathUtil::clamp( member._hp, 1, member.getStat( JrpgStat::MaxHp ) );
        member._mp                                              = MathUtil::clamp( member._mp, 0, member.getStat( JrpgStat::MaxMp ) );
        member._level.setLevel( _pCatalog->getCurve(), 1 );
        pushEvent( JrpgPartyEvent::Kind::ClassChanged, memberIndex, 1, classID );
        learnSpellsAtLevel( memberIndex, *pClass, 1 );
        return JrpgClassChangeResult::Ok;
    }

    int32 JrpgParty::addExp( int32 memberIndex, int64 amount )
    {
        if ( isValidIndex( memberIndex ) == false || amount <= 0 || _pCatalog == nullptr )
            return 0;
        const JrpgClassDef* pClass = _pCatalog->findClass( _listMember[static_cast<size_t>( memberIndex )]._classID );
        if ( pClass == nullptr )
            return 0;
        const int32 startLevel = _listMember[static_cast<size_t>( memberIndex )]._level.getLevel();
        const int32 gained     = _listMember[static_cast<size_t>( memberIndex )]._level.addXp( _pCatalog->getCurve(), amount );
        for ( int32 step = 1; step <= gained; ++step )
        {
            growOneLevel( memberIndex, *pClass );
            pushEvent( JrpgPartyEvent::Kind::LevelUp, memberIndex, startLevel + step );
            learnSpellsAtLevel( memberIndex, *pClass, startLevel + step );
        }
        return gained;
    }

    int64 JrpgParty::distributeRewards( int64 exp, int64 gold )
    {
        if ( gold > 0 && _pWallet != nullptr )
        {
            _pWallet->add( Wallet::getDefaultCurrency(), gold );
            pushEvent( JrpgPartyEvent::Kind::GoldGained, -1, static_cast<int32>( gold ) );
        }
        const int32 aliveCount = countAlive();
        if ( aliveCount <= 0 || exp <= 0 )
            return 0;
        const int64 share = exp / aliveCount;
        for ( int32 memberIndex = 0; memberIndex < getMemberCount(); ++memberIndex )
        {
            if ( _listMember[static_cast<size_t>( memberIndex )].isAlive() == false )
                continue;
            pushEvent( JrpgPartyEvent::Kind::ExpGained, memberIndex, static_cast<int32>( share ) );
            (void)addExp( memberIndex, share );
        }
        return share;
    }

    bool JrpgParty::restAtInn( int64 pricePerMember )
    {
        const int64 price = MathUtil::max<int64>( 0, pricePerMember ) * countAlive();
        if ( price > 0 && ( _pWallet == nullptr || _pWallet->trySpend( Wallet::getDefaultCurrency(), price ) == false ) )
            return false;
        for ( JrpgMember& member : _listMember )
        {
            if ( member.isAlive() == false )
                continue;
            member._hp = member.getStat( JrpgStat::MaxHp );
            member._mp = member.getStat( JrpgStat::MaxMp );
        }
        pushEvent( JrpgPartyEvent::Kind::Rested, -1, static_cast<int32>( price ) );
        return true;
    }

    bool JrpgParty::reviveAtChurch( int32 memberIndex, int64 pricePerLevel )
    {
        if ( isValidIndex( memberIndex ) == false )
            return false;
        JrpgMember& member = _listMember[static_cast<size_t>( memberIndex )];
        if ( member.isAlive() )
            return false;
        const int64 price = MathUtil::max<int64>( 0, pricePerLevel ) * member._level.getLevel();
        if ( price > 0 && ( _pWallet == nullptr || _pWallet->trySpend( Wallet::getDefaultCurrency(), price ) == false ) )
            return false;
        member._hp = member.getStat( JrpgStat::MaxHp );
        pushEvent( JrpgPartyEvent::Kind::Revived, memberIndex, static_cast<int32>( price ) );
        return true;
    }

    void JrpgParty::learnManual( int32 memberIndex, const hashed_string& manualID )
    {
        if ( isValidIndex( memberIndex ) == false || _pCatalog == nullptr || _pCatalog->findManual( manualID ) == nullptr )
            return;
        JrpgMember& member = _listMember[static_cast<size_t>( memberIndex )];
        if ( member.findProficiency( manualID ) >= 0 )
            return;
        member._listManual.push_back( JrpgManualProgress{ manualID, 0 } );
        addProficiency( memberIndex, manualID, 0 );
    }

    void JrpgParty::addProficiency( int32 memberIndex, const hashed_string& manualID, int32 amount )
    {
        if ( isValidIndex( memberIndex ) == false || _pCatalog == nullptr )
            return;
        const JrpgManualDef* pManual = _pCatalog->findManual( manualID );
        if ( pManual == nullptr )
            return;
        for ( JrpgManualProgress& progress : _listMember[static_cast<size_t>( memberIndex )]._listManual )
        {
            if ( progress._manualID != manualID )
                continue;
            const int32 before    = amount == 0 ? -1 : progress._proficiency;
            progress._proficiency = MathUtil::max( 0, progress._proficiency + amount );
            for ( const JrpgManualStage& stage : pManual->_listStage )
            {
                if ( stage._proficiency > before && stage._proficiency <= progress._proficiency )
                    pushEvent( JrpgPartyEvent::Kind::TechniqueUnlocked, memberIndex, stage._proficiency, stage._techniqueID );
            }
            return;
        }
    }

    void JrpgParty::drainEvents( vector<JrpgPartyEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void JrpgParty::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listMember.size() );
        for ( const JrpgMember& member : _listMember )
        {
            StateArchiveUtil::writeName( outArchive, member._id );
            StateArchiveUtil::writeName( outArchive, member._classID );
            outArchive << string_view( member._name );
            outArchive << static_cast<uint32>( member._listSpell.size() );
            for ( const hashed_string& spellID : member._listSpell )
            {
                StateArchiveUtil::writeName( outArchive, spellID );
            }
            outArchive << static_cast<uint32>( member._listManual.size() );
            for ( const JrpgManualProgress& progress : member._listManual )
            {
                StateArchiveUtil::writeName( outArchive, progress._manualID );
                outArchive << progress._proficiency;
            }
            // 장비는 칸 순서대로 아이템 id 만(빈 칸은 빈 이름) — 칸 구성은 `initialize` 의 것이다.
            const vector<EquipSlot>& listSlot = member._equipment.getSlots();
            outArchive << static_cast<uint32>( listSlot.size() );
            for ( const EquipSlot& slot : listSlot )
            {
                StateArchiveUtil::writeName( outArchive, slot._item.isEmpty() ? hashed_string{} : slot._item._itemID );
            }
            member._level.writeState( outArchive );
            for ( const int32 stat : member._arrStat )
            {
                outArchive << stat;
            }
            outArchive << member._hp;
            outArchive << member._mp;
            outArchive << member._inner;
        }
    }

    bool JrpgParty::readState( Archive& archive )
    {
        uint32 memberCount = 0;
        // 멤버마다 id · 직업 · 이름(12) + 주문 · 비급 · 칸 수(12) + 레벨(20) + 능력치(28) + HP · MP · 내공(12) 이상
        if ( _pCatalog == nullptr || StateArchiveUtil::readCount( archive, 84, memberCount ) == false || memberCount > static_cast<uint32>( kMaxMembers ) )
            return false;
        vector<JrpgMember> listMember( memberCount );
        for ( JrpgMember& member : listMember )
        {
            uint32     count     = 0;
            const bool bHeadRead = StateArchiveUtil::readName( archive, member._id ) && StateArchiveUtil::readName( archive, member._classID );
            if ( bHeadRead == false || _pCatalog->findClass( member._classID ) == nullptr )
                return false;
            archive >> member._name;
            if ( StateArchiveUtil::readCount( archive, 4, count ) == false )
                return false;
            member._listSpell.resize( count );
            for ( hashed_string& spellID : member._listSpell )
            {
                if ( StateArchiveUtil::readName( archive, spellID ) == false )
                    return false;
            }
            // 비급마다 이름(4) + 숙련(4)
            if ( StateArchiveUtil::readCount( archive, 8, count ) == false )
                return false;
            member._listManual.resize( count );
            for ( JrpgManualProgress& progress : member._listManual )
            {
                if ( StateArchiveUtil::readName( archive, progress._manualID ) == false )
                    return false;
                archive >> progress._proficiency;
            }

            member._equipment.initialize( _pItemCatalog, _equipLayout );
            if ( StateArchiveUtil::readCount( archive, 4, count ) == false || count != member._equipment.getSlots().size() )
                return false;
            for ( uint32 slotIndex = 0; slotIndex < count; ++slotIndex )
            {
                hashed_string itemID;
                if ( StateArchiveUtil::readName( archive, itemID ) == false )
                    return false;
                if ( itemID.empty() )
                    continue;
                InventorySlot item;
                item._itemID = itemID;
                item._count  = 1;
                vector<InventorySlot> listRemoved;
                const hashed_string&  slotName = member._equipment.getSlots()[slotIndex]._name;
                if ( member._equipment.equip( slotName, item, listRemoved ) != EquipResult::Ok )
                    return false;
            }

            if ( member._level.readState( archive ) == false )
                return false;
            for ( int32& stat : member._arrStat )
            {
                archive >> stat;
            }
            archive >> member._hp;
            archive >> member._mp;
            archive >> member._inner;
            if ( archive.isError() )
                return false;
        }
        _listMember = std::move( listMember );
        _eventBuffer.clear();
        return true;
    }

    bool JrpgParty::canUseSpell( int32 memberIndex, const hashed_string& spellID ) const
    {
        if ( isValidIndex( memberIndex ) == false )
            return false;
        return _listMember[static_cast<size_t>( memberIndex )].knowsSpell( spellID ) || isTechniqueUnlocked( memberIndex, spellID );
    }

    bool JrpgParty::isTechniqueUnlocked( int32 memberIndex, const hashed_string& techniqueID ) const
    {
        if ( isValidIndex( memberIndex ) == false || _pCatalog == nullptr )
            return false;
        const JrpgSpellDef* pSpell = _pCatalog->findSpell( techniqueID );
        if ( pSpell == nullptr || pSpell->_manualID.empty() )
            return false;
        const JrpgManualDef* pManual     = _pCatalog->findManual( pSpell->_manualID );
        const int32          proficiency = _listMember[static_cast<size_t>( memberIndex )].findProficiency( pSpell->_manualID );
        if ( pManual == nullptr || proficiency < 0 )
            return false;
        for ( const JrpgManualStage& stage : pManual->_listStage )
        {
            if ( stage._techniqueID == techniqueID )
                return proficiency >= stage._proficiency;
        }
        return false;
    }

    int32 JrpgParty::computeAttack( int32 memberIndex ) const
    {
        if ( isValidIndex( memberIndex ) == false )
            return 0;
        const JrpgMember& member = _listMember[static_cast<size_t>( memberIndex )];
        StatBlock         stats;
        member._equipment.computeStats( stats );
        return member.getStat( JrpgStat::Strength ) + static_cast<int32>( stats.getValue( JrpgPartyInternal::getAttackName() ) );
    }

    int32 JrpgParty::computeDefense( int32 memberIndex ) const
    {
        if ( isValidIndex( memberIndex ) == false )
            return 0;
        const JrpgMember& member = _listMember[static_cast<size_t>( memberIndex )];
        StatBlock         stats;
        member._equipment.computeStats( stats );
        return member.getStat( JrpgStat::Vitality ) / 2 + static_cast<int32>( stats.getValue( JrpgPartyInternal::getDefenseName() ) );
    }

    int32 JrpgParty::countAlive() const
    {
        int32 count = 0;
        for ( const JrpgMember& member : _listMember )
        {
            count += member.isAlive() ? 1 : 0;
        }
        return count;
    }

    int32 JrpgParty::findMemberIndex( const hashed_string& memberID ) const
    {
        for ( int32 index = 0; index < static_cast<int32>( _listMember.size() ); ++index )
        {
            if ( _listMember[static_cast<size_t>( index )]._id == memberID )
                return index;
        }
        return -1;
    }

    void JrpgParty::growOneLevel( int32 memberIndex, const JrpgClassDef& classDef )
    {
        JrpgMember& member = _listMember[static_cast<size_t>( memberIndex )];
        for ( int32 index = 0; index < kJrpgStatCount; ++index )
        {
            member._arrStat[index] += classDef._arrGrowth[index];
        }
        // 오른 최대치만큼 지금 HP · MP 도 오른다(쓰러진 멤버는 그대로).
        if ( member.isAlive() )
        {
            member._hp = MathUtil::min( member.getStat( JrpgStat::MaxHp ), member._hp + classDef._arrGrowth[static_cast<size_t>( JrpgStat::MaxHp )] );
            member._mp = MathUtil::min( member.getStat( JrpgStat::MaxMp ), member._mp + classDef._arrGrowth[static_cast<size_t>( JrpgStat::MaxMp )] );
        }
    }

    void JrpgParty::learnSpellsAtLevel( int32 memberIndex, const JrpgClassDef& classDef, int32 level )
    {
        JrpgMember& member = _listMember[static_cast<size_t>( memberIndex )];
        for ( const JrpgLearnEntry& learn : classDef._listLearn )
        {
            if ( learn._level != level || member.knowsSpell( learn._spellID ) )
                continue;
            member._listSpell.push_back( learn._spellID );
            pushEvent( JrpgPartyEvent::Kind::LearnedSpell, memberIndex, level, learn._spellID );
        }
    }

    void JrpgParty::pushEvent( JrpgPartyEvent::Kind kind, int32 memberIndex, int32 value, const hashed_string& id )
    {
        JrpgPartyEvent event;
        event._kind        = kind;
        event._memberIndex = memberIndex;
        event._value       = value;
        event._id          = id;
        _eventBuffer.push( event );
    }
} // namespace sw
