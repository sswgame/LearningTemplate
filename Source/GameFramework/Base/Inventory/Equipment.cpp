#include "pch.h"

#include "GameFramework/Base/Inventory/Equipment.h"

#include "GameFramework/Base/Data/GameDataXml.h"
#include "GameFramework/Base/Data/StatBlock.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        struct EquipmentInternal
        {
            /** @brief 칸 상태가 같은가입니다(판을 올릴지 고른다). */
            static bool isSameSlot( const EquipSlot& lhs, const EquipSlot& rhs )
            {
                const ItemStack& left  = lhs._item;
                const ItemStack& right = rhs._item;
                return lhs._bSuppressed == rhs._bSuppressed && left._itemId == right._itemId && left._count == right._count && left._durability == right._durability && left._damage == right._damage && left._listDetachedPart == right._listDetachedPart && left._customization.isEquivalent( right._customization );
            }

            /** @brief 정리는 칸 수의 두 배 안에 끝난다 — 넘으면 데이터 순환이다(로드에서 막히지만 방어). */
            static size_t computeSettleLimit( size_t slotCount ) { return slotCount * 2 + 2; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( EquipResult result )
    {
        switch ( result )
        {
            case EquipResult::Ok:
                return "Ok";
            case EquipResult::UnknownSlot:
                return "UnknownSlot";
            case EquipResult::UnknownItem:
                return "UnknownItem";
            case EquipResult::WrongSlot:
                return "WrongSlot";
            case EquipResult::InventoryFull:
                return "InventoryFull";
            case EquipResult::ConditionNotMet:
                return "ConditionNotMet";
            case EquipResult::RefusedByDependent:
                return "RefusedByDependent";
            case EquipResult::EmptySlot:
                return "EmptySlot";
        }
        return "Unknown";
    }

    Equipment::Equipment()
        : _listSlot{}
        , _context{}
        , _pCatalog{ nullptr }
        , _pSetLookup{ nullptr }
        , _revision{ 0 }
    {
    }

    void Equipment::initialize( const ItemCatalog* pCatalog, string_view slotLayout )
    {
        _pCatalog = pCatalog;
        _listSlot.clear();
        GameDataXml::forEachToken( slotLayout, ", ", [&]( string_view token )
        {
            EquipSlot         slot;
            const size_t      colon = token.find( ':' );
            const string_view name  = colon == string_view::npos ? token : token.substr( 0, colon );
            slot._name              = hashed_string( name );
            slot._accept            = hashed_string( colon == string_view::npos ? name : token.substr( colon + 1 ) );
            _listSlot.push_back( slot );
        } );
        ++_revision;
    }

    int32 Equipment::findSlotIndex( const hashed_string& slot ) const
    {
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index]._name == slot )
                return static_cast<int32>( index );
        }
        return -1;
    }

    bool Equipment::areConditionsMet( const ItemDef& def, const vector<EquipSlot>& listSlot, int32 slotIndex ) const
    {
        for ( const EquipCondition& condition : def._listEquipCondition )
        {
            if ( EquipConditionUtil::isMet( condition, listSlot, slotIndex, *_pCatalog, _pSetLookup, _context ) == false )
                return false;
        }
        return true;
    }

    EquipResult Equipment::settleConditions( vector<EquipSlot>& inoutListSlot, bool bAllowRefuse, vector<ItemStack>& outListRemoved ) const
    {
        if ( _pCatalog == nullptr )
            return EquipResult::Ok;
        const size_t limit = EquipmentInternal::computeSettleLimit( inoutListSlot.size() );
        for ( size_t pass = 0; pass < limit; ++pass )
        {
            bool bChanged = false;
            for ( size_t slotIndex = 0; slotIndex < inoutListSlot.size(); ++slotIndex )
            {
                EquipSlot& slot = inoutListSlot[slotIndex];
                if ( slot._item.isEmpty() )
                {
                    slot._bSuppressed = SW_FALSE;
                    continue;
                }
                const ItemDef* pDef = _pCatalog->findItem( slot._item._itemId );
                if ( pDef == nullptr || pDef->hasEquipConditions() == false )
                    continue;
                const bool bMet = areConditionsMet( *pDef, inoutListSlot, static_cast<int32>( slotIndex ) );
                if ( bMet )
                {
                    if ( slot._bSuppressed == SW_TRUE )
                    {
                        slot._bSuppressed = SW_FALSE;
                        bChanged          = true;
                    }
                    continue;
                }
                if ( slot._bSuppressed == SW_TRUE )
                    continue;
                if ( pDef->_breakPolicy == EquipBreakPolicy::RefuseUnequip && bAllowRefuse )
                    return EquipResult::RefusedByDependent;
                if ( pDef->_breakPolicy == EquipBreakPolicy::UnequipTogether )
                {
                    outListRemoved.push_back( slot._item );
                    slot._item = ItemStack{};
                }
                else
                {
                    slot._bSuppressed = SW_TRUE;
                }
                bChanged = true;
            }
            if ( bChanged == false )
                return EquipResult::Ok;
        }
        return EquipResult::Ok;
    }

    EquipResult Equipment::makeTrialEquip( int32 slotIndex, const ItemStack& item, vector<EquipSlot>& outListSlot, vector<ItemStack>& outListRemoved ) const
    {
        outListRemoved.clear();
        if ( slotIndex < 0 )
            return EquipResult::UnknownSlot;
        const ItemDef* pDef = _pCatalog != nullptr ? _pCatalog->findItem( item._itemId ) : nullptr;
        if ( pDef == nullptr || item.isEmpty() )
            return EquipResult::UnknownItem;
        if ( pDef->_equipSlot != _listSlot[static_cast<size_t>( slotIndex )]._accept )
            return EquipResult::WrongSlot;
        outListSlot       = _listSlot;
        EquipSlot& target = outListSlot[static_cast<size_t>( slotIndex )];
        if ( target._item.isEmpty() == false )
            outListRemoved.push_back( target._item );
        target._item        = item;
        target._item._count = 1;
        target._bSuppressed = SW_FALSE;
        if ( areConditionsMet( *pDef, outListSlot, slotIndex ) == false )
            return EquipResult::ConditionNotMet;
        return settleConditions( outListSlot, true, outListRemoved );
    }

    void Equipment::commitSlots( vector<EquipSlot>&& listSlot )
    {
        bool bChanged = listSlot.size() != _listSlot.size();
        for ( size_t index = 0; bChanged == false && index < listSlot.size(); ++index )
        {
            bChanged = EquipmentInternal::isSameSlot( listSlot[index], _listSlot[index] ) == false;
        }
        _listSlot = std::move( listSlot );
        if ( bChanged )
            ++_revision;
    }

    void Equipment::setCharacterContext( const EquipCharacterContext& context, vector<ItemStack>& outListRemoved )
    {
        outListRemoved.clear();
        _context                    = context;
        vector<EquipSlot> listTrial = _listSlot;
        // 캐릭터 쪽 변화는 거부할 수 없다 — 거부 정책도 숨김으로 둔다.
        (void)settleConditions( listTrial, false, outListRemoved );
        commitSlots( std::move( listTrial ) );
    }

    EquipResult Equipment::evaluateEquip( const hashed_string& slot, const hashed_string& itemId ) const
    {
        ItemStack item;
        item._itemId = itemId;
        item._count  = 1;
        vector<EquipSlot> listTrial;
        vector<ItemStack> listRemoved;
        return makeTrialEquip( findSlotIndex( slot ), item, listTrial, listRemoved );
    }

    hashed_string Equipment::findSlotFor( const hashed_string& itemId ) const
    {
        const ItemDef* pDef = _pCatalog != nullptr ? _pCatalog->findItem( itemId ) : nullptr;
        if ( pDef == nullptr || pDef->isEquipment() == false )
            return hashed_string{};
        hashed_string firstMatch{};
        for ( const EquipSlot& equipSlot : _listSlot )
        {
            if ( equipSlot._accept != pDef->_equipSlot )
                continue;
            if ( equipSlot._item.isEmpty() )
                return equipSlot._name;
            if ( firstMatch.empty() )
                firstMatch = equipSlot._name;
        }
        return firstMatch;
    }

    EquipResult Equipment::equip( const hashed_string& slot, const ItemStack& item, vector<ItemStack>& outListRemoved )
    {
        vector<EquipSlot> listTrial;
        const EquipResult result = makeTrialEquip( findSlotIndex( slot ), item, listTrial, outListRemoved );
        if ( result != EquipResult::Ok )
        {
            outListRemoved.clear();
            return result;
        }
        commitSlots( std::move( listTrial ) );
        return EquipResult::Ok;
    }

    EquipResult Equipment::unequip( const hashed_string& slot, vector<ItemStack>& outListRemoved )
    {
        outListRemoved.clear();
        const int32 slotIndex = findSlotIndex( slot );
        if ( slotIndex < 0 )
            return EquipResult::UnknownSlot;
        if ( _listSlot[static_cast<size_t>( slotIndex )]._item.isEmpty() )
            return EquipResult::EmptySlot;
        vector<EquipSlot> listTrial = _listSlot;
        EquipSlot&        target    = listTrial[static_cast<size_t>( slotIndex )];
        outListRemoved.push_back( target._item );
        target._item             = ItemStack{};
        target._bSuppressed      = SW_FALSE;
        const EquipResult result = settleConditions( listTrial, true, outListRemoved );
        if ( result != EquipResult::Ok )
        {
            outListRemoved.clear();
            return result;
        }
        commitSlots( std::move( listTrial ) );
        return EquipResult::Ok;
    }

    EquipResult Equipment::equipFromInventory( Inventory& inventory, int32 inventorySlot, const hashed_string& slot )
    {
        if ( inventorySlot < 0 || inventorySlot >= inventory.getSlotCount() || inventory.getSlot( inventorySlot ).isEmpty() )
            return EquipResult::UnknownItem;
        const ItemStack     source     = inventory.getSlot( inventorySlot );
        const hashed_string targetSlot = slot.empty() ? findSlotFor( source._itemId ) : slot;
        if ( targetSlot.empty() )
            return EquipResult::WrongSlot; // 그 아이템을 받는 칸이 없다
        const EquipResult evaluated = evaluateEquip( targetSlot, source._itemId );
        if ( evaluated != EquipResult::Ok )
            return evaluated;
        // 하나를 꺼내고, 벗은 것은 인벤토리로. 하나라도 자리가 없으면 두 쪽을 모두 되돌린다.
        const Inventory         inventoryBefore = inventory;
        const vector<EquipSlot> slotsBefore     = _listSlot;
        const uint32            revisionBefore  = _revision;
        const ItemStack         taken           = inventory.takeFromSlot( inventorySlot, 1 );
        vector<ItemStack>       listRemoved;
        (void)equip( targetSlot, taken, listRemoved );
        for ( const ItemStack& removed : listRemoved )
        {
            if ( inventory.addStack( removed ) == false )
            {
                inventory = inventoryBefore;
                _listSlot = slotsBefore;
                _revision = revisionBefore;
                return EquipResult::InventoryFull;
            }
        }
        return EquipResult::Ok;
    }

    bool Equipment::unequipToInventory( const hashed_string& slot, Inventory& inventory )
    {
        const Inventory         inventoryBefore = inventory;
        const vector<EquipSlot> slotsBefore     = _listSlot;
        const uint32            revisionBefore  = _revision;
        vector<ItemStack>       listRemoved;
        if ( unequip( slot, listRemoved ) != EquipResult::Ok )
            return false;
        for ( const ItemStack& removed : listRemoved )
        {
            if ( inventory.addStack( removed ) == false )
            {
                inventory = inventoryBefore;
                _listSlot = slotsBefore;
                _revision = revisionBefore;
                return false;
            }
        }
        return true;
    }

    bool Equipment::setEquippedInstance( const hashed_string& slot, const ItemStack& item )
    {
        const int32 slotIndex = findSlotIndex( slot );
        if ( slotIndex < 0 || _listSlot[static_cast<size_t>( slotIndex )]._item.isEmpty() )
            return false;
        vector<EquipSlot> listTrial = _listSlot;
        ItemStack&        target    = listTrial[static_cast<size_t>( slotIndex )]._item;
        target._customization       = item._customization;
        target._listDetachedPart    = item._listDetachedPart;
        target._durability          = item._durability;
        target._damage              = item._damage;
        commitSlots( std::move( listTrial ) );
        return true;
    }

    const ItemStack* Equipment::findEquipped( const hashed_string& slot ) const
    {
        const int32 slotIndex = findSlotIndex( slot );
        if ( slotIndex < 0 )
            return nullptr;
        const EquipSlot& equipSlot = _listSlot[static_cast<size_t>( slotIndex )];
        return equipSlot._item.isEmpty() ? nullptr : &equipSlot._item;
    }

    bool Equipment::isSuppressed( const hashed_string& slot ) const
    {
        const int32 slotIndex = findSlotIndex( slot );
        return slotIndex >= 0 && _listSlot[static_cast<size_t>( slotIndex )]._bSuppressed == SW_TRUE;
    }

    void Equipment::computeStats( StatBlock& outStats ) const
    {
        outStats.clear();
        for ( const EquipSlot& equipSlot : _listSlot )
        {
            if ( equipSlot._item.isEmpty() || equipSlot._bSuppressed == SW_TRUE )
                continue;
            const ItemDef* pDef = _pCatalog != nullptr ? _pCatalog->findItem( equipSlot._item._itemId ) : nullptr;
            if ( pDef != nullptr )
                outStats.merge( pDef->_stats );
        }
    }
} // namespace sw
