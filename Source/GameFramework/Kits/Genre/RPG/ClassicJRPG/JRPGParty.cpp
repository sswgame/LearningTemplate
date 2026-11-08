#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGParty.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"

namespace sw
{
    namespace
    {
        /** @brief 틱마다 쓰는 이름입니다. 리터럴을 매번 intern 하지 않도록 한 번만 만듭니다. */
        struct JRPGPartyInternal
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
    bool JRPGMember::knowsSpell( const hashed_string& spellID ) const
    {
        return VectorUtil::contains( _listSpell, spellID );
    }

    int32 JRPGMember::findProficiency( const hashed_string& manualID ) const
    {
        for ( const JRPGManualProgress& progress : _listManual )
        {
            if ( progress._manualID == manualID )
                return progress._proficiency;
        }
        return constant::invalid_index::kInt32;
    }

    bool JRPGMember::isStateValid( const JRPGCatalog& catalog ) const
    {
        const bool bKnown = _id.empty() == false && catalog.findClass( _classID ) != nullptr && _level.getLevel() <= catalog.getCurve().getMaxLevel();
        if ( bKnown == false || VectorUtil::findDuplicate( _listSpell ) != nullptr || VectorUtil::findDuplicateBy<&JRPGManualProgress::_manualID>( _listManual ) != nullptr )
            return false;
        for ( const hashed_string& spellID : _listSpell )
        {
            if ( catalog.findSpell( spellID ) == nullptr )
                return false;
        }
        for ( const JRPGManualProgress& progress : _listManual )
        {
            if ( catalog.findManual( progress._manualID ) == nullptr || progress._proficiency < 0 )
                return false;
        }
        for ( const int32 stat : _arrStat )
        {
            if ( stat < 0 )
                return false;
        }
        return getStat( JRPGStat::MaxHP ) >= 1 && 0 <= _hp && _hp <= getStat( JRPGStat::MaxHP ) && 0 <= _mp && _mp <= getStat( JRPGStat::MaxMP ) && 0 <= _inner && _inner <= JRPGParty::kInnerMax;
    }

    JRPGParty::JRPGParty()
        : _listMember{}
        , _eventBuffer{}
        , _equipLayout{}
        , _pCatalog{ nullptr }
        , _pItemCatalog{ nullptr }
        , _pInventory{ nullptr }
        , _pWallet{ nullptr }
    {
    }

    void JRPGParty::initialize( const JRPGCatalog* pCatalog, const ItemCatalog* pItemCatalog, const SessionStateRefs& refs, string_view equipLayout )
    {
        _pCatalog     = pCatalog;
        _pItemCatalog = pItemCatalog;
        _equipLayout  = string( equipLayout );
        _listMember.clear();
        _eventBuffer.clear();
        _pInventory = refs._pInventory;
        _pWallet    = refs._pWallet;
    }

    int32 JRPGParty::addMember( const hashed_string& memberID, string_view name, const hashed_string& classID, int32 level )
    {
        const JRPGClassDef* pClass           = _pCatalog != nullptr ? _pCatalog->findClass( classID ) : nullptr;
        const bool          bCannotAddMember = pClass == nullptr || memberID.empty() || static_cast<int32>( _listMember.size() ) >= kMaxMembers || findMemberIndex( memberID ).has_value();
        if ( bCannotAddMember )
            return constant::invalid_index::kInt32;

        JRPGMember member;
        member._id      = memberID;
        member._classID = classID;
        member._name    = string( name );
        member._equipment.initialize( _pItemCatalog, _equipLayout );
        for ( int32 index = 0; index < kJRPGStatCount; ++index )
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
        JRPGMember& added = _listMember.back();
        added._hp         = added.getStat( JRPGStat::MaxHP );
        added._mp         = added.getStat( JRPGStat::MaxMP );
        _eventBuffer.clear(); // 처음 만든 멤버의 성장은 알리지 않는다
        return memberIndex;
    }

    JRPGClassChangeResult JRPGParty::changeClass( int32 memberIndex, const hashed_string& classID, int32 minLevel )
    {
        if ( isValidIndex( memberIndex ) == false )
            return JRPGClassChangeResult::UnknownMember;
        const JRPGClassDef* pClass = _pCatalog != nullptr ? _pCatalog->findClass( classID ) : nullptr;
        if ( pClass == nullptr )
            return JRPGClassChangeResult::UnknownClass;
        JRPGMember& member = _listMember[static_cast<size_t>( memberIndex )];
        if ( member._classID == classID )
            return JRPGClassChangeResult::SameClass;
        if ( member.isAlive() == false )
            return JRPGClassChangeResult::Dead;
        if ( member._level.getLevel() < minLevel )
            return JRPGClassChangeResult::LevelTooLow;
        if ( pClass->_requiredItem.empty() == false && ( _pInventory == nullptr || _pInventory->hasItem( pClass->_requiredItem ) == false ) )
            return JRPGClassChangeResult::MissingItem;

        member._classID = classID;
        for ( int32 index = 0; index < kJRPGStatCount; ++index )
        {
            member._arrStat[index] /= 2;
        }
        member._arrStat[static_cast<size_t>( JRPGStat::MaxHP )] = MathUtil::max( 1, member.getStat( JRPGStat::MaxHP ) );
        member._hp                                              = MathUtil::clamp( member._hp, 1, member.getStat( JRPGStat::MaxHP ) );
        member._mp                                              = MathUtil::clamp( member._mp, 0, member.getStat( JRPGStat::MaxMP ) );
        member._level.setLevel( _pCatalog->getCurve(), 1 );
        pushEvent( JRPGPartyEvent::Kind::ClassChanged, memberIndex, 1, classID );
        learnSpellsAtLevel( memberIndex, *pClass, 1 );
        return JRPGClassChangeResult::Ok;
    }

    int32 JRPGParty::addExp( int32 memberIndex, int64 amount )
    {
        const bool bCannotAddExp = isValidIndex( memberIndex ) == false || amount <= 0 || _pCatalog == nullptr;
        if ( bCannotAddExp )
            return 0;
        const JRPGClassDef* pClass = _pCatalog->findClass( _listMember[static_cast<size_t>( memberIndex )]._classID );
        if ( pClass == nullptr )
            return 0;
        const int32 startLevel = _listMember[static_cast<size_t>( memberIndex )]._level.getLevel();
        const int32 gained     = _listMember[static_cast<size_t>( memberIndex )]._level.addXP( _pCatalog->getCurve(), amount );
        for ( int32 step = 1; step <= gained; ++step )
        {
            growOneLevel( memberIndex, *pClass );
            pushEvent( JRPGPartyEvent::Kind::LevelUp, memberIndex, startLevel + step );
            learnSpellsAtLevel( memberIndex, *pClass, startLevel + step );
        }
        return gained;
    }

    int64 JRPGParty::distributeRewards( int64 exp, int64 gold )
    {
        if ( gold > 0 && _pWallet != nullptr )
        {
            _pWallet->add( Wallet::getDefaultCurrency(), gold );
            pushEvent( JRPGPartyEvent::Kind::GoldGained, constant::invalid_index::kInt32, static_cast<int32>( gold ) );
        }
        const int32 aliveCount = countAlive();
        if ( aliveCount <= 0 || exp <= 0 )
            return 0;
        const int64 share = exp / aliveCount;
        for ( int32 memberIndex = 0; memberIndex < getMemberCount(); ++memberIndex )
        {
            if ( _listMember[static_cast<size_t>( memberIndex )].isAlive() == false )
                continue;
            pushEvent( JRPGPartyEvent::Kind::ExpGained, memberIndex, static_cast<int32>( share ) );
            (void)addExp( memberIndex, share );
        }
        return share;
    }

    bool JRPGParty::restAtInn( int64 pricePerMember )
    {
        const int64 price = MathUtil::max<int64>( 0, pricePerMember ) * countAlive();
        if ( price > 0 && ( _pWallet == nullptr || _pWallet->trySpend( Wallet::getDefaultCurrency(), price ) == false ) )
            return false;
        for ( JRPGMember& member : _listMember )
        {
            if ( member.isAlive() == false )
                continue;
            member._hp = member.getStat( JRPGStat::MaxHP );
            member._mp = member.getStat( JRPGStat::MaxMP );
        }
        pushEvent( JRPGPartyEvent::Kind::Rested, constant::invalid_index::kInt32, static_cast<int32>( price ) );
        return true;
    }

    bool JRPGParty::reviveAtChurch( int32 memberIndex, int64 pricePerLevel )
    {
        if ( isValidIndex( memberIndex ) == false )
            return false;
        JRPGMember& member = _listMember[static_cast<size_t>( memberIndex )];
        if ( member.isAlive() )
            return false;
        const int64 price = MathUtil::max<int64>( 0, pricePerLevel ) * member._level.getLevel();
        if ( price > 0 && ( _pWallet == nullptr || _pWallet->trySpend( Wallet::getDefaultCurrency(), price ) == false ) )
            return false;
        member._hp = member.getStat( JRPGStat::MaxHP );
        pushEvent( JRPGPartyEvent::Kind::Revived, memberIndex, static_cast<int32>( price ) );
        return true;
    }

    void JRPGParty::learnManual( int32 memberIndex, const hashed_string& manualID )
    {
        const bool bCannotLearnManual = isValidIndex( memberIndex ) == false || _pCatalog == nullptr || _pCatalog->findManual( manualID ) == nullptr;
        if ( bCannotLearnManual )
            return;
        JRPGMember& member = _listMember[static_cast<size_t>( memberIndex )];
        if ( member.findProficiency( manualID ) >= 0 )
            return;
        member._listManual.push_back( JRPGManualProgress{ manualID, 0 } );
        pushUnlockedTechniques( memberIndex, *_pCatalog->findManual( manualID ), -1, 0 );
    }

    void JRPGParty::addProficiency( int32 memberIndex, const hashed_string& manualID, int32 amount )
    {
        if ( isValidIndex( memberIndex ) == false || _pCatalog == nullptr || amount <= 0 )
            return;
        const JRPGManualDef* pManual = _pCatalog->findManual( manualID );
        if ( pManual == nullptr )
            return;
        for ( JRPGManualProgress& progress : _listMember[static_cast<size_t>( memberIndex )]._listManual )
        {
            if ( progress._manualID != manualID )
                continue;
            const int32 before = progress._proficiency;
            progress._proficiency += amount;
            pushUnlockedTechniques( memberIndex, *pManual, before, progress._proficiency );
            return;
        }
    }

    void JRPGParty::pushUnlockedTechniques( int32 memberIndex, const JRPGManualDef& manual, int32 before, int32 after )
    {
        for ( const JRPGManualStage& stage : manual._listStage )
        {
            if ( stage._proficiency > before && stage._proficiency <= after )
                pushEvent( JRPGPartyEvent::Kind::TechniqueUnlocked, memberIndex, stage._proficiency, stage._techniqueID );
        }
    }

    void JRPGParty::pollEvents( vector<JRPGPartyEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void JRPGParty::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        // 멤버마다 id, 직업, 이름(12) + 주문, 비급, 슬롯 수(12) + 레벨(20) + 능력치(28) + HP, MP, 내공(12) 바이트 이상
        io.list( _listMember, 84, [&]( JRPGMember& member )
        {
            io.value( member._id );
            io.value( member._classID );
            io.value( member._name );
            io.list( member._listSpell );
            // 비급마다 이름(4) + 숙련(4) 바이트
            io.list( member._listManual, 8, [&]( JRPGManualProgress& progress )
            {
                io.value( progress._manualID );
                io.value( progress._proficiency );
            } );
            // 장비는 슬롯 순서대로 아이템 id 만 쓴다(빈 슬롯은 빈 이름). 슬롯 구성은 `initialize` 에서 받은 것이다.
            if ( io.isReading() )
            {
                member._equipment.initialize( _pItemCatalog, _equipLayout );
            }
            const size_t slotTotal = member._equipment.getSlots().size();
            uint32       slotCount = static_cast<uint32>( slotTotal );
            io.count( 4, slotCount );
            if ( io.check( slotCount == slotTotal ) == false )
                return;
            for ( uint32 slotIndex = 0; slotIndex < slotCount; ++slotIndex )
            {
                const EquipmentSlot& slot   = member._equipment.getSlots()[slotIndex];
                hashed_string        itemID = slot._item.isEmpty() ? hashed_string{} : slot._item._itemID;
                io.value( itemID );
                if ( io.isReading() && io.isOk() && itemID.empty() == false )
                {
                    InventorySlot item;
                    item._itemID = itemID;
                    item._count  = 1;
                    vector<InventorySlot> listRemoved;
                    if ( io.check( member._equipment.equip( slot._name, item, listRemoved ) == EquipResult::Ok ) == false )
                        return;
                }
            }
            io.value( member._level );
            for ( int32& stat : member._arrStat )
            {
                io.value( stat );
            }
            io.value( member._hp );
            io.value( member._mp );
            io.value( member._inner );
            if ( io.isReading() && io.isOk() )
            {
                io.check( member.isStateValid( *_pCatalog ) );
            }
        } );
        if ( io.isWriting() || io.isOk() == false )
            return;
        io.check( _listMember.size() <= static_cast<size_t>( kMaxMembers ) && VectorUtil::findDuplicateBy<&JRPGMember::_id>( _listMember ) == nullptr );
        _eventBuffer.clear();
    }

    bool JRPGParty::canUseSpell( int32 memberIndex, const hashed_string& spellID ) const
    {
        if ( isValidIndex( memberIndex ) == false )
            return false;
        return _listMember[static_cast<size_t>( memberIndex )].knowsSpell( spellID ) || isTechniqueUnlocked( memberIndex, spellID );
    }

    bool JRPGParty::isTechniqueUnlocked( int32 memberIndex, const hashed_string& techniqueID ) const
    {
        if ( isValidIndex( memberIndex ) == false || _pCatalog == nullptr )
            return false;
        const JRPGSpellDef* pSpell = _pCatalog->findSpell( techniqueID );
        if ( pSpell == nullptr || pSpell->_manualID.empty() )
            return false;
        const JRPGManualDef* pManual     = _pCatalog->findManual( pSpell->_manualID );
        const int32          proficiency = _listMember[static_cast<size_t>( memberIndex )].findProficiency( pSpell->_manualID );
        if ( pManual == nullptr || proficiency < 0 )
            return false;
        for ( const JRPGManualStage& stage : pManual->_listStage )
        {
            if ( stage._techniqueID == techniqueID )
                return proficiency >= stage._proficiency;
        }
        return false;
    }

    int32 JRPGParty::computeAttack( int32 memberIndex ) const
    {
        if ( isValidIndex( memberIndex ) == false )
            return 0;
        const JRPGMember& member = _listMember[static_cast<size_t>( memberIndex )];
        StatBlock         stats;
        member._equipment.computeStats( stats );
        return member.getStat( JRPGStat::Strength ) + static_cast<int32>( stats.getValue( JRPGPartyInternal::getAttackName() ) );
    }

    int32 JRPGParty::computeDefense( int32 memberIndex ) const
    {
        if ( isValidIndex( memberIndex ) == false )
            return 0;
        const JRPGMember& member = _listMember[static_cast<size_t>( memberIndex )];
        StatBlock         stats;
        member._equipment.computeStats( stats );
        return member.getStat( JRPGStat::HealthStateMachine ) / 2 + static_cast<int32>( stats.getValue( JRPGPartyInternal::getDefenseName() ) );
    }

    int32 JRPGParty::countAlive() const
    {
        int32 count = 0;
        for ( const JRPGMember& member : _listMember )
        {
            count += member.isAlive() ? 1 : 0;
        }
        return count;
    }

    optional<int32> JRPGParty::findMemberIndex( const hashed_string& memberID ) const
    {
        for ( int32 index = 0; index < static_cast<int32>( _listMember.size() ); ++index )
        {
            if ( _listMember[static_cast<size_t>( index )]._id == memberID )
                return index;
        }
        return nullopt;
    }

    void JRPGParty::growOneLevel( int32 memberIndex, const JRPGClassDef& classDef )
    {
        JRPGMember& member = _listMember[static_cast<size_t>( memberIndex )];
        for ( int32 index = 0; index < kJRPGStatCount; ++index )
        {
            member._arrStat[index] += classDef._arrGrowth[index];
        }
        // 오른 최대치만큼 지금 HP 와 MP 도 오른다(쓰러진 멤버는 그대로 둔다).
        if ( member.isAlive() )
        {
            member._hp = MathUtil::min( member.getStat( JRPGStat::MaxHP ), member._hp + classDef._arrGrowth[static_cast<size_t>( JRPGStat::MaxHP )] );
            member._mp = MathUtil::min( member.getStat( JRPGStat::MaxMP ), member._mp + classDef._arrGrowth[static_cast<size_t>( JRPGStat::MaxMP )] );
        }
    }

    void JRPGParty::learnSpellsAtLevel( int32 memberIndex, const JRPGClassDef& classDef, int32 level )
    {
        JRPGMember& member = _listMember[static_cast<size_t>( memberIndex )];
        for ( const JRPGLearnEntry& learn : classDef._listLearn )
        {
            // 모르는 주문은 카탈로그 읽기가 이미 오류로 알렸다. 배우면 세이브가 읽히지 않는다.
            if ( learn._level != level || member.knowsSpell( learn._spellID ) || _pCatalog->findSpell( learn._spellID ) == nullptr )
                continue;
            member._listSpell.push_back( learn._spellID );
            pushEvent( JRPGPartyEvent::Kind::LearnedSpell, memberIndex, level, learn._spellID );
        }
    }

    void JRPGParty::pushEvent( JRPGPartyEvent::Kind kind, int32 memberIndex, int32 value, const hashed_string& id )
    {
        JRPGPartyEvent event;
        event._kind        = kind;
        event._memberIndex = memberIndex;
        event._value       = value;
        event._id          = id;
        _eventBuffer.push( event );
    }
} // namespace sw
