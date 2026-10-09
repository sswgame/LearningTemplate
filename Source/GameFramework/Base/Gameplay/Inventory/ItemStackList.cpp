#include "pch.h"

#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

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
        getItemIds( listItem );
        std::sort( listItem.begin(), listItem.end(), HashedStringLexicalLess{} );
        outArchive << static_cast<uint32>( listItem.size() );
        for ( const hashed_string& itemId : listItem )
        {
            StateArchiveUtil::writeName( outArchive, itemId );
            outArchive << getItemCount( itemId );
        }
    }

    bool ItemStackList::readState( Archive& archive )
    {
        uint32 count = 0;
        if ( StateArchiveUtil::readCount( archive, sizeof( uint32 ) + sizeof( int32 ), count ) == false )
            return false;
        ItemStackList list;
        for ( uint32 index = 0; index < count; ++index )
        {
            hashed_string itemId;
            int32         itemCount = 0;
            if ( StateArchiveUtil::readName( archive, itemId ) == false )
                return false;
            archive >> itemCount;
            if ( archive.isError() || itemId.empty() || itemCount <= 0 )
                return false;
            list.addItem( itemId, itemCount );
        }
        _listStack = std::move( list._listStack );
        return true;
    }

    void ItemStackList::addItem( const hashed_string& itemId, int32 count )
    {
        if ( itemId.empty() || count <= 0 )
            return;
        ItemStack* pStack = findStack( itemId );
        if ( pStack != nullptr )
        {
            pStack->_count += count;
            return;
        }
        _listStack.push_back( ItemStack{ itemId, count } );
    }

    bool ItemStackList::removeItem( const hashed_string& itemId, int32 count )
    {
        ItemStack* pStack = findStack( itemId );
        if ( count <= 0 || pStack == nullptr || pStack->_count < count )
            return false;
        pStack->_count -= count;
        if ( pStack->_count == 0 )
            _listStack.erase( _listStack.begin() + ( pStack - _listStack.data() ) );
        return true;
    }

    bool ItemStackList::moveItemTo( ItemStackList& target, const hashed_string& itemId, int32 count )
    {
        if ( &target == this || removeItem( itemId, count ) == false )
            return false;
        target.addItem( itemId, count );
        return true;
    }

    int32 ItemStackList::getItemCount( const hashed_string& itemId ) const
    {
        for ( const ItemStack& stack : _listStack )
        {
            if ( stack._itemId == itemId )
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

    void ItemStackList::getItemIds( vector<hashed_string>& outListItem ) const
    {
        outListItem.clear();
        outListItem.reserve( _listStack.size() );
        for ( const ItemStack& stack : _listStack )
        {
            outListItem.push_back( stack._itemId );
        }
    }

    ItemStack* ItemStackList::findStack( const hashed_string& itemId )
    {
        for ( ItemStack& stack : _listStack )
        {
            if ( stack._itemId == itemId )
                return &stack;
        }
        return nullptr;
    }
} // namespace sw
