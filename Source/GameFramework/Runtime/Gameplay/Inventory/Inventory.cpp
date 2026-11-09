#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"

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

    bool Inventory::initialize( const ItemCatalog* pCatalog, int32 slotCount, float32 maxWeight )
    {
        if ( slotCount < 0 || maxWeight < 0.0f )
        {
            SW_LOG_ERROR_ID( "Inventory.NegativeLimit", "Inventory: refused %# slots and max weight %# - neither may be negative", slotCount, maxWeight );
            return false;
        }
        _pCatalog = pCatalog;
        _listSlot.assign( static_cast<size_t>( slotCount ), InventorySlot{} );
        _maxWeight = maxWeight;
        ++_revision;
        return true;
    }

    void Inventory::resize( int32 slotCount, vector<InventorySlot>& outListOverflow )
    {
        outListOverflow.clear();
        const size_t newCount = static_cast<size_t>( MathUtil::max( 0, slotCount ) );
        if ( newCount < _listSlot.size() )
        {
            // 줄어드는 슬롯의 아이템은 남는 빈 슬롯으로 먼저 옮긴다.
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

    bool Inventory::setMaxWeight( float32 maxWeight )
    {
        if ( maxWeight < 0.0f )
        {
            SW_LOG_ERROR_ID( "Inventory.NegativeLimit", "Inventory: refused max weight %# - it must not be negative", maxWeight );
            return false;
        }
        _maxWeight = maxWeight;
        ++_revision;
        return true;
    }

    float32 Inventory::getItemWeight( const hashed_string& itemID ) const
    {
        const ItemDef* pDef = _pCatalog != nullptr ? _pCatalog->find( itemID ) : nullptr;
        return pDef != nullptr ? pDef->_weight : 0.0f;
    }

    int32 Inventory::computeWeightRoom( const hashed_string& itemID, int32 count ) const
    {
        const float32 weight = getItemWeight( itemID );
        if ( _maxWeight <= 0.0f || weight <= 0.0f )
            return count;
        const float32 room = _maxWeight - computeWeight();
        return MathUtil::clamp( static_cast<int32>( ( room + 1.0e-4f ) / weight ), 0, count );
    }

    int32 Inventory::addItem( const hashed_string& itemID, int32 count )
    {
        if ( itemID.empty() || count <= 0 )
            return 0;
        const ItemDef* pDef     = _pCatalog != nullptr ? _pCatalog->find( itemID ) : nullptr;
        const int32    maxStack = pDef != nullptr ? pDef->_maxStack : 1;
        int32          left     = computeWeightRoom( itemID, count );
        const int32    wanted   = left;
        for ( InventorySlot& slot : _listSlot )
        {
            if ( left <= 0 )
                break;
            const bool bSkipStack = slot.isEmpty() || slot._itemID != itemID || slot._count >= maxStack;
            if ( bSkipStack )
                continue;
            const int32 moved = MathUtil::min( left, maxStack - slot._count );
            slot._count += moved;
            left -= moved;
        }
        for ( InventorySlot& slot : _listSlot )
        {
            if ( left <= 0 )
                break;
            if ( slot.isEmpty() == false )
                continue;
            const int32 moved = MathUtil::min( left, maxStack );
            slot._itemID      = itemID;
            slot._count       = moved;
            slot._durability  = pDef != nullptr ? pDef->_maxDurability : 0.0f;
            left -= moved;
        }
        const int32 added = wanted - left;
        if ( added > 0 )
            ++_revision;
        return added;
    }

    bool Inventory::addStack( const InventorySlot& stack )
    {
        if ( stack.isEmpty() )
            return true;
        const ItemDef* pDef = _pCatalog != nullptr ? _pCatalog->find( stack._itemID ) : nullptr;
        if ( ( pDef == nullptr || pDef->_maxDurability <= 0.0f ) && stack.hasInstanceState() == false )
        {
            if ( hasRoomFor( stack._itemID, stack._count ) == false )
                return false;
            return addItem( stack._itemID, stack._count ) == stack._count;
        }
        // 닳는 아이템과 인스턴스 상태가 있는 아이템은 그 상태를 유지한 채 빈 슬롯 하나에 넣는다.
        if ( computeWeightRoom( stack._itemID, 1 ) < 1 )
            return false;
        for ( InventorySlot& slot : _listSlot )
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

    bool Inventory::removeItem( const hashed_string& itemID, int32 count )
    {
        if ( count <= 0 )
            return true;
        if ( getItemCount( itemID ) < count )
            return false;
        int32 left = count;
        for ( size_t index = _listSlot.size(); index > 0 && left > 0; --index )
        {
            InventorySlot& slot = _listSlot[index - 1];
            if ( slot.isEmpty() || slot._itemID != itemID )
                continue;
            const int32 moved = MathUtil::min( left, slot._count );
            slot._count -= moved;
            left -= moved;
            if ( slot.isEmpty() )
                slot = InventorySlot{};
        }
        ++_revision;
        return true;
    }

    InventorySlot Inventory::takeFromSlot( int32 slot, int32 count )
    {
        const bool bInvalidTakeRequest = isValidSlot( slot ) == false || count <= 0 || _listSlot[static_cast<size_t>( slot )].isEmpty();
        if ( bInvalidTakeRequest )
            return InventorySlot{};
        InventorySlot& source = _listSlot[static_cast<size_t>( slot )];
        InventorySlot  taken  = source;
        taken._count          = MathUtil::min( count, source._count );
        source._count -= taken._count;
        if ( source.isEmpty() )
            source = InventorySlot{};
        ++_revision;
        return taken;
    }

    bool Inventory::moveSlot( int32 fromSlot, int32 toSlot )
    {
        const bool bInvalidMoveRequest = isValidSlot( fromSlot ) == false || isValidSlot( toSlot ) == false || fromSlot == toSlot;
        if ( bInvalidMoveRequest )
            return false;
        InventorySlot& source = _listSlot[static_cast<size_t>( fromSlot )];
        InventorySlot& target = _listSlot[static_cast<size_t>( toSlot )];
        if ( source.isEmpty() )
            return false;
        const int32 maxStack         = _pCatalog != nullptr ? _pCatalog->getMaxStack( source._itemID ) : 1;
        const bool  bStackIntoTarget = target.isEmpty() == false && target._itemID == source._itemID && maxStack > 1;
        if ( bStackIntoTarget )
        {
            const int32 moved = MathUtil::min( source._count, maxStack - target._count );
            if ( moved <= 0 )
                return false;
            target._count += moved;
            source._count -= moved;
            if ( source.isEmpty() )
                source = InventorySlot{};
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
        const bool bInvalidSplitRequest = isValidSlot( slot ) == false || count <= 0 || _listSlot[static_cast<size_t>( slot )]._count <= count;
        if ( bInvalidSplitRequest )
            return constant::invalid_index::kInt32;
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
        return constant::invalid_index::kInt32;
    }

    void Inventory::sortSlots()
    {
        // 먼저 겹칠 수 있는 것을 합친다.
        vector<InventorySlot> listStack;
        for ( const InventorySlot& slot : _listSlot )
        {
            if ( slot.isEmpty() )
                continue;
            const int32 maxStack = _pCatalog != nullptr ? _pCatalog->getMaxStack( slot._itemID ) : 1;
            int32       left     = slot._count;
            for ( InventorySlot& merged : listStack )
            {
                const bool bMergeIntoStack = left > 0 && maxStack > 1 && merged._itemID == slot._itemID && merged._count < maxStack;
                if ( bMergeIntoStack )
                {
                    const int32 moved = MathUtil::min( left, maxStack - merged._count );
                    merged._count += moved;
                    left -= moved;
                }
            }
            if ( left > 0 )
            {
                InventorySlot rest = slot;
                rest._count        = left;
                listStack.push_back( rest );
            }
        }
        const ItemCatalog* pCatalog = _pCatalog;
        VectorUtil::stableSort( listStack, [pCatalog]( const InventorySlot& lhs, const InventorySlot& rhs )
        {
            const ItemDef*    pLhs        = pCatalog != nullptr ? pCatalog->find( lhs._itemID ) : nullptr;
            const ItemDef*    pRhs        = pCatalog != nullptr ? pCatalog->find( rhs._itemID ) : nullptr;
            const string_view lhsCategory = pLhs != nullptr ? string_view( pLhs->_category.c_str() ) : string_view();
            const string_view rhsCategory = pRhs != nullptr ? string_view( pRhs->_category.c_str() ) : string_view();
            if ( lhsCategory != rhsCategory )
                return lhsCategory < rhsCategory;
            const int32 lhsRarity = pLhs != nullptr ? pLhs->_rarity : 0;
            const int32 rhsRarity = pRhs != nullptr ? pRhs->_rarity : 0;
            if ( lhsRarity != rhsRarity )
                return lhsRarity > rhsRarity;
            return string_view( lhs._itemID.c_str() ) < string_view( rhs._itemID.c_str() );
        } );
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            _listSlot[index] = index < listStack.size() ? listStack[index] : InventorySlot{};
        }
        ++_revision;
    }

    bool Inventory::wearSlot( int32 slot, float32 amount )
    {
        if ( isValidSlot( slot ) == false || amount <= 0.0f )
            return false;
        InventorySlot& stack = _listSlot[static_cast<size_t>( slot )];
        if ( stack.isEmpty() || stack._durability <= 0.0f )
            return false;
        const bool bDepleted = DepletingValue::take( stack._durability, amount )._bDepleted == SW_TRUE;
        ++_revision;
        if ( bDepleted == false )
            return false;
        stack = InventorySlot{};
        return true;
    }

    void Inventory::clear()
    {
        for ( InventorySlot& slot : _listSlot )
        {
            slot = InventorySlot{};
        }
        ++_revision;
    }

    int32 Inventory::getItemCount( const hashed_string& itemID ) const
    {
        int32 count = 0;
        for ( const InventorySlot& slot : _listSlot )
        {
            count += slot.isEmpty() == false && slot._itemID == itemID ? slot._count : 0;
        }
        return count;
    }

    bool Inventory::hasItems( const ItemStackList& items ) const
    {
        for ( const auto& item : items.getItems() )
        {
            if ( getItemCount( item._itemID ) < item._count )
                return false;
        }
        return true;
    }

    bool Inventory::hasRoomFor( const hashed_string& itemID, int32 count ) const
    {
        if ( computeWeightRoom( itemID, count ) < count )
            return false;
        const int32 maxStack = _pCatalog != nullptr ? _pCatalog->getMaxStack( itemID ) : 1;
        int32       room     = 0;
        for ( const InventorySlot& slot : _listSlot )
        {
            if ( slot.isEmpty() )
                room += maxStack;
            else if ( slot._itemID == itemID )
                room += MathUtil::max( 0, maxStack - slot._count );
            if ( room >= count )
                return true;
        }
        return room >= count;
    }

    optional<int32> Inventory::findFirstSlot( const hashed_string& itemID ) const
    {
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index].isEmpty() == false && _listSlot[index]._itemID == itemID )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    int32 Inventory::countEmptySlots() const
    {
        int32 count = 0;
        for ( const InventorySlot& slot : _listSlot )
        {
            count += slot.isEmpty() ? 1 : 0;
        }
        return count;
    }

    float32 Inventory::computeWeight() const
    {
        float32 weight = 0.0f;
        for ( const InventorySlot& slot : _listSlot )
        {
            if ( slot.isEmpty() == false )
                weight += getItemWeight( slot._itemID ) * static_cast<float32>( slot._count );
        }
        return weight;
    }

    void Inventory::fillItemStackList( ItemStackList& outItems ) const
    {
        for ( const InventorySlot& slot : _listSlot )
        {
            if ( slot.isEmpty() == false )
                outItems.addItem( slot._itemID, slot._count );
        }
    }

    void Inventory::serializeState( StateIO& io )
    {
        io.value( _maxWeight );
        io.check( 0.0f <= _maxWeight );
        // 슬롯 수는 늘 같다. 슬롯마다 이름(4) + 개수, 내구도, 피해(12) + 꾸미기 수, 부품 수(8)
        io.expect( static_cast<uint32>( _listSlot.size() ) );
        if ( io.isOk() == false )
            return;
        for ( InventorySlot& slot : _listSlot )
        {
            if ( io.isReading() )
            {
                slot = InventorySlot{};
            }
            io.value( slot._itemID );
            io.value( slot._count );
            io.value( slot._durability );
            io.value( slot._damage );
            // 값마다 이름 두 개(8) + 숫자 네 개(16)
            uint32 valueCount = static_cast<uint32>( slot._customization.getValues().size() );
            io.count( 24, valueCount );
            for ( uint32 valueIndex = 0; valueIndex < valueCount && io.isOk(); ++valueIndex )
            {
                CustomizationValue value = io.isWriting() ? slot._customization.getValues()[valueIndex] : CustomizationValue{};
                io.value( value._parameter );
                io.value( value._option );
                io.value( value._number );
                if ( io.isReading() )
                {
                    slot._customization.setValue( value );
                }
            }
            io.list( slot._listDetachedPart );
            if ( io.check( 0 <= slot._count && ( slot._count == 0 || slot._itemID.empty() == false ) ) == false )
                return;
        }
        if ( io.isReading() )
        {
            ++_revision;
        }
    }

} // namespace sw
