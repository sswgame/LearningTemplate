#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include <algorithm>

namespace sw
{
    ItemStackList::ItemStackList()
        : _listStack{}
    {
    }

    void ItemStackList::serializeState( StateIO& io )
    {
        vector<hashed_string> listItem;
        getItemIDs( listItem );
        VectorUtil::sort( listItem, HashedStringLexicalLess{} );
        uint32 itemCount = static_cast<uint32>( listItem.size() );
        io.count( sizeof( uint32 ) + sizeof( int32 ), itemCount );
        if ( io.isOk() == false )
            return;
        ItemStackList restored;
        for ( uint32 index = 0; index < itemCount; ++index )
        {
            hashed_string itemID = io.isWriting() ? listItem[index] : hashed_string{};
            int32         amount = io.isWriting() ? getItemCount( itemID ) : 0;
            io.value( itemID );
            io.value( amount );
            if ( io.isWriting() )
                continue;
            // 쓰는 쪽은 아이템마다 한 줄을 쓴다. 겹친 줄을 합치면 int32 를 넘겨 음수 개수가 된다.
            if ( io.check( itemID.empty() == false && amount > 0 && restored.getItemCount( itemID ) <= 0 ) == false )
                return;
            restored.addItem( itemID, amount );
        }
        if ( io.isReading() )
        {
            _listStack = std::move( restored._listStack );
        }
    }

    void ItemStackList::addItem( const hashed_string& itemID, int32 count )
    {
        if ( itemID.empty() || count <= 0 )
            return;
        ItemStack* pStack = findStack( itemID );
        if ( pStack != nullptr )
        {
            pStack->_count += count;
            return;
        }
        _listStack.push_back( ItemStack{ itemID, count } );
    }

    bool ItemStackList::removeItem( const hashed_string& itemID, int32 count )
    {
        ItemStack* pStack        = findStack( itemID );
        const bool bCannotRemove = count <= 0 || pStack == nullptr || pStack->_count < count;
        if ( bCannotRemove )
            return false;
        pStack->_count -= count;
        if ( pStack->_count == 0 )
            _listStack.erase( _listStack.begin() + ( pStack - _listStack.data() ) );
        return true;
    }

    bool ItemStackList::moveItemTo( ItemStackList& target, const hashed_string& itemID, int32 count )
    {
        if ( &target == this || removeItem( itemID, count ) == false )
            return false;
        target.addItem( itemID, count );
        return true;
    }

    int32 ItemStackList::getItemCount( const hashed_string& itemID ) const
    {
        for ( const ItemStack& stack : _listStack )
        {
            if ( stack._itemID == itemID )
                return stack._count;
        }
        return 0;
    }

    int32 ItemStackList::getTotalCount() const
    {
        int32 total = 0;
        for ( const ItemStack& stack : _listStack )
        {
            total += stack._count;
        }
        return total;
    }

    void ItemStackList::getItemIDs( vector<hashed_string>& outListItem ) const
    {
        outListItem.clear();
        outListItem.reserve( _listStack.size() );
        for ( const ItemStack& stack : _listStack )
        {
            outListItem.push_back( stack._itemID );
        }
    }

    ItemStack* ItemStackList::findStack( const hashed_string& itemID )
    {
        return VectorUtil::findBy<&ItemStack::_itemID>( _listStack, itemID );
    }
} // namespace sw
