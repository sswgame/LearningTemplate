#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGParty.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

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
    bool JRPGMember::hasLearnedSpell( const hashed_string& spellID ) const
    {
        return VectorUtil::contains( _listSpell, spellID );
    }

    optional<int32> JRPGMember::findProficiency( const hashed_string& manualID ) const
    {
        for ( const JRPGManualProgress& progress : _listManual )
        {
            if ( progress._manualID == manualID )
                return progress._proficiency;
        }
        return nullopt;
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

    void JRPGParty::initialize( const JRPGCatalog& catalog, const ItemCatalog& itemCatalog, Inventory& inventory, Wallet& wallet, string_view equipLayout )
    {
        _pCatalog     = &catalog;
        _pItemCatalog = &itemCatalog;
        _equipLayout  = string( equipLayout );
        _listMember.clear();
        _eventBuffer.clear();
        _pInventory = &inventory;
        _pWallet    = &wallet;
    }

    expected<int32> JRPGParty::addMember( const hashed_string& memberID, string_view name, const hashed_string& classID, int32 level )
    {
        const JRPGClassDef* pClass           = _pCatalog != nullptr ? _pCatalog->findClass( classID ) : nullptr;
        const bool          bCannotAddMember = pClass == nullptr || memberID.empty() || static_cast<int32>( _listMember.size() ) >= kMaxMembers || findMemberIndex( memberID ).has_value();
        if ( bCannotAddMember )
            return unexpected( Error{ ErrorKind::InvalidArgument, "empty or duplicate member id, unknown class, or the party is full" } );

        JRPGMember member;
        member._id      = memberID;
        member._classID = classID;
        member._name    = string( name );
        member._equipment.initialize( *_pItemCatalog, _equipLayout, nullptr, nullptr );
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
        if ( pClass->_requiredItem.empty() == false && _pInventory->hasItem( pClass->_requiredItem.key() ) == false )
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
        if ( gold > 0 && _pWallet != nullptr ) // 열기 전의 파티는 멤버도 지갑도 없다
        {
            _pWallet->add( Wallet::getDefaultCurrency(), gold );
            pushEvent( JRPGPartyEvent::Kind::GoldGained, nullopt, static_cast<int32>( gold ) );
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
        if ( price > 0 && _pWallet->trySpend( Wallet::getDefaultCurrency(), price ) == false )
            return false;
        for ( JRPGMember& member : _listMember )
        {
            if ( member.isAlive() == false )
                continue;
            member._hp = member.getStat( JRPGStat::MaxHP );
            member._mp = member.getStat( JRPGStat::MaxMP );
        }
        pushEvent( JRPGPartyEvent::Kind::Rested, nullopt, static_cast<int32>( price ) );
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
        if ( price > 0 && _pWallet->trySpend( Wallet::getDefaultCurrency(), price ) == false )
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
        if ( member.findProficiency( manualID ).has_value() )
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
        io.reflected( *this );
        if ( io.isOk() == false )
            return;
        // 장비는 리플렉션 밖 값이다 — 멤버마다 슬롯 순서대로 아이템 인스턴스를 쓴다(id 만 쓰면 닳은 장비가 새것이 된다). 슬롯 구성은 `initialize` 에서 받은 것이다.
        for ( JRPGMember& member : _listMember )
        {
            if ( io.isReading() )
            {
                member._equipment.initialize( *_pItemCatalog, _equipLayout, nullptr, nullptr );
            }
            const size_t slotTotal = member._equipment.getSlots().size();
            uint32       slotCount = static_cast<uint32>( slotTotal );
            io.count( Inventory::kSlotStateMinBytes, slotCount );
            if ( io.check( slotCount == slotTotal ) == false )
                return;
            vector<EquipmentSlot> listWorn = member._equipment.getSlots();
            for ( EquipmentSlot& slot : listWorn )
            {
                Inventory::serializeSlot( io, slot._item );
            }
            if ( io.isReading() && io.isOk() )
            {
                VectorUtil::removeIf( listWorn, []( const EquipmentSlot& slot )
                { return slot._item.isEmpty(); } );
                Inventory refused; // 칸이 없는 가방 — 낄 수 없는 것이 하나라도 있으면 깨진 상태다
                member._equipment.restoreWorn( listWorn, refused );
                if ( io.check( refused.getOverflow().empty() ) == false )
                    return;
            }
            // 카탈로그 · 칸 사이 검사만 손으로 — 칸마다의 범위는 `PROPERTY( Min )` 이 지킨다(레벨 칸은 안쪽 구조체라 그 형식의 손 검사를 여기서 한다)
            if ( io.isReading() && io.isOk() )
            {
                io.check( member.isStateValid( *_pCatalog ) && member._level.getXP() <= member._level.getTotalXP() );
            }
        }
        if ( io.isWriting() || io.isOk() == false )
            return;
        io.check( _listMember.size() <= static_cast<size_t>( kMaxMembers ) && VectorUtil::findDuplicateBy<&JRPGMember::_id>( _listMember ) == nullptr );
        _eventBuffer.clear();
    }

    bool JRPGParty::canUseSpell( int32 memberIndex, const hashed_string& spellID ) const
    {
        if ( isValidIndex( memberIndex ) == false )
            return false;
        return _listMember[static_cast<size_t>( memberIndex )].hasLearnedSpell( spellID ) || isTechniqueUnlocked( memberIndex, spellID );
    }

    bool JRPGParty::isTechniqueUnlocked( int32 memberIndex, const hashed_string& techniqueID ) const
    {
        if ( isValidIndex( memberIndex ) == false || _pCatalog == nullptr )
            return false;
        const JRPGSpellDef* pSpell = _pCatalog->findSpell( techniqueID );
        if ( pSpell == nullptr || pSpell->_manualID.empty() )
            return false;
        const JRPGManualDef*  pManual     = _pCatalog->findManual( pSpell->_manualID );
        const optional<int32> proficiency = _listMember[static_cast<size_t>( memberIndex )].findProficiency( pSpell->_manualID );
        if ( pManual == nullptr || proficiency.has_value() == false )
            return false;
        for ( const JRPGManualStage& stage : pManual->_listStage )
        {
            if ( stage._techniqueID == techniqueID )
                return *proficiency >= stage._proficiency;
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
            if ( learn._level != level || member.hasLearnedSpell( learn._spellID ) || _pCatalog->findSpell( learn._spellID ) == nullptr )
                continue;
            member._listSpell.push_back( learn._spellID );
            pushEvent( JRPGPartyEvent::Kind::LearnedSpell, memberIndex, level, learn._spellID );
        }
    }

    void JRPGParty::pushEvent( JRPGPartyEvent::Kind kind, optional<int32> memberIndex, int32 value, const hashed_string& id )
    {
        JRPGPartyEvent event;
        event._kind        = kind;
        event._memberIndex = memberIndex;
        event._value       = value;
        event._id          = id;
        _eventBuffer.push( event );
    }
} // namespace sw
