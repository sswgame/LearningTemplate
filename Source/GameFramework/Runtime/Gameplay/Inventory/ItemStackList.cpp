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

    void ItemStackList::writeState( Archive& outArchive ) const
    {
        vector<hashed_string> listItem;
        getItemIDs( listItem );
        VectorUtil::sort( listItem, HashedStringLexicalLess{} );
        outArchive << static_cast<uint32>( listItem.size() );
        for ( const hashed_string& itemID : listItem )
        {
            StateIO::writeName( outArchive, itemID );
            outArchive << getItemCount( itemID );
        }
    }

    bool ItemStackList::readState( Archive& archive )
    {
        uint32 count = 0;
        if ( archive.readCount( sizeof( uint32 ) + sizeof( int32 ), count ) == false )
            return false;
        ItemStackList list;
        for ( uint32 index = 0; index < count; ++index )
        {
            hashed_string itemID;
            int32         itemCount = 0;
            if ( StateIO::readName( archive, itemID ) == false )
                return false;
            archive >> itemCount;
            // `writeState` 는 아이템마다 한 줄을 쓴다. 겹친 줄을 합치면 int32 를 넘겨 음수 개수가 된다.
            const bool bInvalidStackRecord = archive.isError() || itemID.empty() || itemCount <= 0 || list.getItemCount( itemID ) > 0;
            if ( bInvalidStackRecord )
                return false;
            list.addItem( itemID, itemCount );
        }
        _listStack = std::move( list._listStack );
        return true;
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
