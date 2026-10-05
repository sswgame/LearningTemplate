#include "pch.h"

#include "GameFramework/Kits/Rpg/ClassicJrpg/JrpgParty.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Data/StatBlock.h"
#include "GameFramework/Base/Framework/GameStateRefs.h"

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
    bool JrpgMember::knowsSpell( const hashed_string& spellId ) const
    {
        for ( const hashed_string& entry : _listSpell )
        {
            if ( entry == spellId )
                return true;
        }
        return false;
    }

    int32 JrpgMember::findProficiency( const hashed_string& manualId ) const
    {
        for ( const JrpgManualProgress& progress : _listManual )
        {
            if ( progress._manualId == manualId )
                return progress._proficiency;
        }
        return -1;
    }

    JrpgParty::JrpgParty()
        : _listMember{}
        , _eventBuffer{}
        , _wallet{}
        , _equipLayout{}
        , _pCatalog{ nullptr }
        , _pItemCatalog{ nullptr }
        , _pInventory{ nullptr }
    {
    }

    void JrpgParty::initialize( const JrpgCatalog* pCatalog, const ItemCatalog* pItemCatalog, const GameStateRefs& refs, string_view equipLayout )
    {
        _pCatalog     = pCatalog;
        _pItemCatalog = pItemCatalog;
        _equipLayout  = string( equipLayout );
        _listMember.clear();
        _eventBuffer.clear();
        _wallet.clear();
        _pInventory = refs._pInventory;
    }

    int32 JrpgParty::addMember( const hashed_string& memberId, string_view name, const hashed_string& classId, int32 level )
    {
        const JrpgClassDef* pClass = _pCatalog != nullptr ? _pCatalog->findClass( classId ) : nullptr;
        if ( pClass == nullptr || static_cast<int32>( _listMember.size() ) >= kMaxMembers || findMemberIndex( memberId ) >= 0 )
            return -1;

        JrpgMember member;
        member._id      = memberId;
        member._classId = classId;
        member._name    = string( name );
        member._equipment.initialize( _pItemCatalog, _equipLayout );
        for ( int32 index = 0; index < kJrpgStatCount; ++index )
            member._arrStat[index] = pClass->_arrBase[index];
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

    JrpgClassChangeResult JrpgParty::changeClass( int32 memberIndex, const hashed_string& classId, int32 minLevel )
    {
        if ( isValidIndex( memberIndex ) == false )
            return JrpgClassChangeResult::UnknownMember;
        const JrpgClassDef* pClass = _pCatalog != nullptr ? _pCatalog->findClass( classId ) : nullptr;
        if ( pClass == nullptr )
            return JrpgClassChangeResult::UnknownClass;
        JrpgMember& member = _listMember[static_cast<size_t>( memberIndex )];
        if ( member._classId == classId )
            return JrpgClassChangeResult::SameClass;
        if ( member.isAlive() == false )
            return JrpgClassChangeResult::Dead;
        if ( member._level.getLevel() < minLevel )
            return JrpgClassChangeResult::LevelTooLow;
        if ( pClass->_requiredItem.empty() == false && ( _pInventory == nullptr || _pInventory->hasItem( pClass->_requiredItem ) == false ) )
            return JrpgClassChangeResult::MissingItem;

        member._classId = classId;
        for ( int32 index = 0; index < kJrpgStatCount; ++index )
            member._arrStat[index] /= 2;
        member._arrStat[static_cast<size_t>( JrpgStat::MaxHp )] = MathUtil::max( 1, member.getStat( JrpgStat::MaxHp ) );
        member._hp                                              = MathUtil::clamp( member._hp, 1, member.getStat( JrpgStat::MaxHp ) );
        member._mp                                              = MathUtil::clamp( member._mp, 0, member.getStat( JrpgStat::MaxMp ) );
        member._level.setLevel( _pCatalog->getCurve(), 1 );
        pushEvent( JrpgPartyEvent::Kind::ClassChanged, memberIndex, 1, classId );
        learnSpellsAtLevel( memberIndex, *pClass, 1 );
        return JrpgClassChangeResult::Ok;
    }

    int32 JrpgParty::addExp( int32 memberIndex, int64 amount )
    {
        if ( isValidIndex( memberIndex ) == false || amount <= 0 || _pCatalog == nullptr )
            return 0;
        const JrpgClassDef* pClass = _pCatalog->findClass( _listMember[static_cast<size_t>( memberIndex )]._classId );
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
        if ( gold > 0 )
        {
            _wallet.add( Wallet::getDefaultCurrency(), gold );
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
        if ( price > 0 && _wallet.trySpend( Wallet::getDefaultCurrency(), price ) == false )
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
        if ( price > 0 && _wallet.trySpend( Wallet::getDefaultCurrency(), price ) == false )
            return false;
        member._hp = member.getStat( JrpgStat::MaxHp );
        pushEvent( JrpgPartyEvent::Kind::Revived, memberIndex, static_cast<int32>( price ) );
        return true;
    }

    void JrpgParty::learnManual( int32 memberIndex, const hashed_string& manualId )
    {
        if ( isValidIndex( memberIndex ) == false || _pCatalog == nullptr || _pCatalog->findManual( manualId ) == nullptr )
            return;
        JrpgMember& member = _listMember[static_cast<size_t>( memberIndex )];
        if ( member.findProficiency( manualId ) >= 0 )
            return;
        member._listManual.push_back( JrpgManualProgress{ manualId, 0 } );
        addProficiency( memberIndex, manualId, 0 );
    }

    void JrpgParty::addProficiency( int32 memberIndex, const hashed_string& manualId, int32 amount )
    {
        if ( isValidIndex( memberIndex ) == false || _pCatalog == nullptr )
            return;
        const JrpgManualDef* pManual = _pCatalog->findManual( manualId );
        if ( pManual == nullptr )
            return;
        for ( JrpgManualProgress& progress : _listMember[static_cast<size_t>( memberIndex )]._listManual )
        {
            if ( progress._manualId != manualId )
                continue;
            const int32 before    = amount == 0 ? -1 : progress._proficiency;
            progress._proficiency = MathUtil::max( 0, progress._proficiency + amount );
            for ( const JrpgManualStage& stage : pManual->_listStage )
            {
                if ( stage._proficiency > before && stage._proficiency <= progress._proficiency )
                    pushEvent( JrpgPartyEvent::Kind::TechniqueUnlocked, memberIndex, stage._proficiency, stage._techniqueId );
            }
            return;
        }
    }

    void JrpgParty::drainEvents( vector<JrpgPartyEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    bool JrpgParty::canUseSpell( int32 memberIndex, const hashed_string& spellId ) const
    {
        if ( isValidIndex( memberIndex ) == false )
            return false;
        return _listMember[static_cast<size_t>( memberIndex )].knowsSpell( spellId ) || isTechniqueUnlocked( memberIndex, spellId );
    }

    bool JrpgParty::isTechniqueUnlocked( int32 memberIndex, const hashed_string& techniqueId ) const
    {
        if ( isValidIndex( memberIndex ) == false || _pCatalog == nullptr )
            return false;
        const JrpgSpellDef* pSpell = _pCatalog->findSpell( techniqueId );
        if ( pSpell == nullptr || pSpell->_manualId.empty() )
            return false;
        const JrpgManualDef* pManual     = _pCatalog->findManual( pSpell->_manualId );
        const int32          proficiency = _listMember[static_cast<size_t>( memberIndex )].findProficiency( pSpell->_manualId );
        if ( pManual == nullptr || proficiency < 0 )
            return false;
        for ( const JrpgManualStage& stage : pManual->_listStage )
        {
            if ( stage._techniqueId == techniqueId )
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
            count += member.isAlive() ? 1 : 0;
        return count;
    }

    int32 JrpgParty::findMemberIndex( const hashed_string& memberId ) const
    {
        for ( int32 index = 0; index < static_cast<int32>( _listMember.size() ); ++index )
        {
            if ( _listMember[static_cast<size_t>( index )]._id == memberId )
                return index;
        }
        return -1;
    }

    void JrpgParty::growOneLevel( int32 memberIndex, const JrpgClassDef& classDef )
    {
        JrpgMember& member = _listMember[static_cast<size_t>( memberIndex )];
        for ( int32 index = 0; index < kJrpgStatCount; ++index )
            member._arrStat[index] += classDef._arrGrowth[index];
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
            if ( learn._level != level || member.knowsSpell( learn._spellId ) )
                continue;
            member._listSpell.push_back( learn._spellId );
            pushEvent( JrpgPartyEvent::Kind::LearnedSpell, memberIndex, level, learn._spellId );
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
