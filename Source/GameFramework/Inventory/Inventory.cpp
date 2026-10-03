#include "pch.h"

#include "GameFramework/Inventory/Inventory.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Data/ItemBag.h"
#include "GameFramework/Inventory/ItemCatalog.h"

#include <algorithm>

namespace sw
{
    Inventory::Inventory()
        : _listSlot{}
        , _pCatalog{ nullptr }
        , _maxWeight{ 0.0f }
        , _revision{ 0 }
    {
    }

    void Inventory::initialize( const ItemCatalog* pCatalog, int32 slotCount, float32 maxWeight )
    {
        _pCatalog = pCatalog;
        _listSlot.assign( static_cast<size_t>( MathUtil::max( 0, slotCount ) ), ItemStack{} );
        _maxWeight = MathUtil::max( 0.0f, maxWeight );
        ++_revision;
    }

    void Inventory::resize( int32 slotCount, vector<ItemStack>& outListOverflow )
    {
        outListOverflow.clear();
        const size_t newCount = static_cast<size_t>( MathUtil::max( 0, slotCount ) );
        if ( newCount < _listSlot.size() )
        {
            // 줄어드는 칸의 것은 남는 빈 칸으로 먼저 옮긴다.
            for ( size_t index = newCount; index < _listSlot.size(); ++index )
            {
                if ( _listSlot[index].isEmpty() )
                    continue;
                bool bPlaced = false;
                for ( size_t target = 0; target < newCount && bPlaced == false; ++target )
                {
                    if ( _listSlot[target].isEmpty() )
                    {
                        _listSlot[target] = _listSlot[index];
                        bPlaced           = true;
                    }
                }
                if ( bPlaced == false )
                    outListOverflow.push_back( _listSlot[index] );
            }
        }
        _listSlot.resize( newCount );
        ++_revision;
    }

    void Inventory::setMaxWeight( float32 maxWeight )
    {
        _maxWeight = MathUtil::max( 0.0f, maxWeight );
        ++_revision;
    }

    float32 Inventory::getItemWeight( const hashed_string& itemId ) const
    {
        const ItemDef* pDef = _pCatalog != nullptr ? _pCatalog->findItem( itemId ) : nullptr;
        return pDef != nullptr ? pDef->_weight : 0.0f;
    }

    int32 Inventory::computeWeightRoom( const hashed_string& itemId, int32 count ) const
    {
        const float32 weight = getItemWeight( itemId );
        if ( _maxWeight <= 0.0f || weight <= 0.0f )
            return count;
        const float32 room = _maxWeight - computeWeight();
        return MathUtil::clamp( static_cast<int32>( ( room + 1.0e-4f ) / weight ), 0, count );
    }

    int32 Inventory::addItem( const hashed_string& itemId, int32 count )
    {
        if ( itemId.empty() || count <= 0 )
            return 0;
        const ItemDef* pDef     = _pCatalog != nullptr ? _pCatalog->findItem( itemId ) : nullptr;
        const int32    maxStack = pDef != nullptr ? pDef->_maxStack : 1;
        int32          left     = computeWeightRoom( itemId, count );
        const int32    wanted   = left;
        for ( ItemStack& slot : _listSlot )
        {
            if ( left <= 0 )
                break;
            if ( slot.isEmpty() || slot._itemId != itemId || slot._count >= maxStack )
                continue;
            const int32 moved = MathUtil::min( left, maxStack - slot._count );
            slot._count += moved;
            left -= moved;
        }
        for ( ItemStack& slot : _listSlot )
        {
            if ( left <= 0 )
                break;
            if ( slot.isEmpty() == false )
                continue;
            const int32 moved = MathUtil::min( left, maxStack );
            slot._itemId      = itemId;
            slot._count       = moved;
            slot._durability  = pDef != nullptr ? pDef->_maxDurability : 0.0f;
            left -= moved;
        }
        const int32 added = wanted - left;
        if ( added > 0 )
            ++_revision;
        return added;
    }

    bool Inventory::addStack( const ItemStack& stack )
    {
        if ( stack.isEmpty() )
            return true;
        const ItemDef* pDef = _pCatalog != nullptr ? _pCatalog->findItem( stack._itemId ) : nullptr;
        if ( pDef == nullptr || pDef->_maxDurability <= 0.0f )
        {
            if ( hasRoomFor( stack._itemId, stack._count ) == false )
                return false;
            return addItem( stack._itemId, stack._count ) == stack._count;
        }
        // 닳는 아이템은 내구도를 지닌 채 빈 칸 하나에.
        if ( computeWeightRoom( stack._itemId, 1 ) < 1 )
            return false;
        for ( ItemStack& slot : _listSlot )
        {
            if ( slot.isEmpty() )
            {
                slot        = stack;
                slot._count = 1;
                ++_revision;
                return true;
            }
        }
        return false;
    }

    bool Inventory::removeItem( const hashed_string& itemId, int32 count )
    {
        if ( count <= 0 )
            return true;
        if ( getItemCount( itemId ) < count )
            return false;
        int32 left = count;
        for ( size_t index = _listSlot.size(); index > 0 && left > 0; --index )
        {
            ItemStack& slot = _listSlot[index - 1];
            if ( slot.isEmpty() || slot._itemId != itemId )
                continue;
            const int32 moved = MathUtil::min( left, slot._count );
            slot._count -= moved;
            left -= moved;
            if ( slot.isEmpty() )
                slot = ItemStack{};
        }
        ++_revision;
        return true;
    }

    ItemStack Inventory::takeFromSlot( int32 slot, int32 count )
    {
        if ( isValidSlot( slot ) == false || count <= 0 || _listSlot[static_cast<size_t>( slot )].isEmpty() )
            return ItemStack{};
        ItemStack& source = _listSlot[static_cast<size_t>( slot )];
        ItemStack  taken  = source;
        taken._count      = MathUtil::min( count, source._count );
        source._count -= taken._count;
        if ( source.isEmpty() )
            source = ItemStack{};
        ++_revision;
        return taken;
    }

    bool Inventory::moveSlot( int32 fromSlot, int32 toSlot )
    {
        if ( isValidSlot( fromSlot ) == false || isValidSlot( toSlot ) == false || fromSlot == toSlot )
            return false;
        ItemStack& source = _listSlot[static_cast<size_t>( fromSlot )];
        ItemStack& target = _listSlot[static_cast<size_t>( toSlot )];
        if ( source.isEmpty() )
            return false;
        const int32 maxStack = _pCatalog != nullptr ? _pCatalog->getMaxStack( source._itemId ) : 1;
        if ( target.isEmpty() == false && target._itemId == source._itemId && maxStack > 1 )
        {
            const int32 moved = MathUtil::min( source._count, maxStack - target._count );
            if ( moved <= 0 )
                return false;
            target._count += moved;
            source._count -= moved;
            if ( source.isEmpty() )
                source = ItemStack{};
        }
        else
        {
            std::swap( source, target );
        }
        ++_revision;
        return true;
    }

    int32 Inventory::splitSlot( int32 slot, int32 count )
    {
        if ( isValidSlot( slot ) == false || count <= 0 || _listSlot[static_cast<size_t>( slot )]._count <= count )
            return -1;
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index].isEmpty() )
            {
                _listSlot[index]        = _listSlot[static_cast<size_t>( slot )];
                _listSlot[index]._count = count;
                _listSlot[static_cast<size_t>( slot )]._count -= count;
                ++_revision;
                return static_cast<int32>( index );
            }
        }
        return -1;
    }

    void Inventory::sortSlots()
    {
        // 먼저 겹칠 수 있는 것을 합친다.
        vector<ItemStack> listStack;
        for ( const ItemStack& slot : _listSlot )
        {
            if ( slot.isEmpty() )
                continue;
            const int32 maxStack = _pCatalog != nullptr ? _pCatalog->getMaxStack( slot._itemId ) : 1;
            int32       left     = slot._count;
            for ( ItemStack& merged : listStack )
            {
                if ( left > 0 && maxStack > 1 && merged._itemId == slot._itemId && merged._count < maxStack )
                {
                    const int32 moved = MathUtil::min( left, maxStack - merged._count );
                    merged._count += moved;
                    left -= moved;
                }
            }
            if ( left > 0 )
            {
                ItemStack rest = slot;
                rest._count    = left;
                listStack.push_back( rest );
            }
        }
        const ItemCatalog* pCatalog = _pCatalog;
        std::stable_sort( listStack.begin(), listStack.end(), [pCatalog]( const ItemStack& lhs, const ItemStack& rhs )
        {
            const ItemDef*    pLhs        = pCatalog != nullptr ? pCatalog->findItem( lhs._itemId ) : nullptr;
            const ItemDef*    pRhs        = pCatalog != nullptr ? pCatalog->findItem( rhs._itemId ) : nullptr;
            const string_view lhsCategory = pLhs != nullptr ? string_view( pLhs->_category.c_str() ) : string_view();
            const string_view rhsCategory = pRhs != nullptr ? string_view( pRhs->_category.c_str() ) : string_view();
            if ( lhsCategory != rhsCategory )
                return lhsCategory < rhsCategory;
            const int32 lhsRarity = pLhs != nullptr ? pLhs->_rarity : 0;
            const int32 rhsRarity = pRhs != nullptr ? pRhs->_rarity : 0;
            if ( lhsRarity != rhsRarity )
                return lhsRarity > rhsRarity;
            return string_view( lhs._itemId.c_str() ) < string_view( rhs._itemId.c_str() );
        } );
        for ( size_t index = 0; index < _listSlot.size(); ++index )
            _listSlot[index] = index < listStack.size() ? listStack[index] : ItemStack{};
        ++_revision;
    }

    bool Inventory::wearSlot( int32 slot, float32 amount )
    {
        if ( isValidSlot( slot ) == false || amount <= 0.0f )
            return false;
        ItemStack& stack = _listSlot[static_cast<size_t>( slot )];
        if ( stack.isEmpty() || stack._durability <= 0.0f )
            return false;
        stack._durability -= amount;
        ++_revision;
        if ( stack._durability > 0.0f )
            return false;
        stack = ItemStack{};
        return true;
    }

    void Inventory::clear()
    {
        for ( ItemStack& slot : _listSlot )
            slot = ItemStack{};
        ++_revision;
    }

    int32 Inventory::getItemCount( const hashed_string& itemId ) const
    {
        int32 count = 0;
        for ( const ItemStack& slot : _listSlot )
            count += slot.isEmpty() == false && slot._itemId == itemId ? slot._count : 0;
        return count;
    }

    bool Inventory::hasItems( const ItemBag& bag ) const
    {
        for ( const auto& item : bag.getItems() )
        {
            if ( getItemCount( item.first ) < item.second )
                return false;
        }
        return true;
    }

    bool Inventory::hasRoomFor( const hashed_string& itemId, int32 count ) const
    {
        if ( computeWeightRoom( itemId, count ) < count )
            return false;
        const int32 maxStack = _pCatalog != nullptr ? _pCatalog->getMaxStack( itemId ) : 1;
        int32       room     = 0;
        for ( const ItemStack& slot : _listSlot )
        {
            if ( slot.isEmpty() )
                room += maxStack;
            else if ( slot._itemId == itemId )
                room += MathUtil::max( 0, maxStack - slot._count );
            if ( room >= count )
                return true;
        }
        return room >= count;
    }

    int32 Inventory::findFirstSlot( const hashed_string& itemId ) const
    {
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index].isEmpty() == false && _listSlot[index]._itemId == itemId )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 Inventory::countEmptySlots() const
    {
        int32 count = 0;
        for ( const ItemStack& slot : _listSlot )
            count += slot.isEmpty() ? 1 : 0;
        return count;
    }

    float32 Inventory::computeWeight() const
    {
        float32 weight = 0.0f;
        for ( const ItemStack& slot : _listSlot )
        {
            if ( slot.isEmpty() == false )
                weight += getItemWeight( slot._itemId ) * static_cast<float32>( slot._count );
        }
        return weight;
    }

    void Inventory::fillItemBag( ItemBag& outBag ) const
    {
        for ( const ItemStack& slot : _listSlot )
        {
            if ( slot.isEmpty() == false )
                outBag.addItem( slot._itemId, slot._count );
        }
    }
} // namespace sw
