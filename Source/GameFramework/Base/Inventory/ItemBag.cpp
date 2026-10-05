#include "pch.h"

#include "GameFramework/Base/Inventory/ItemBag.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Utility/StateArchiveUtil.h"

#include <algorithm>

namespace sw
{
    ItemBag::ItemBag()
        : _mapItem{}
    {
    }

    void ItemBag::writeState( Archive& outArchive ) const
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

    bool ItemBag::readState( Archive& archive )
    {
        uint32 count = 0;
        if ( StateArchiveUtil::readCount( archive, sizeof( uint32 ) + sizeof( int32 ), count ) == false )
            return false;
        unordered_map<hashed_string, int32> mapItem;
        for ( uint32 index = 0; index < count; ++index )
        {
            hashed_string itemId;
            int32         itemCount = 0;
            if ( StateArchiveUtil::readName( archive, itemId ) == false )
                return false;
            archive >> itemCount;
            if ( archive.isError() || itemId.empty() || itemCount <= 0 )
                return false;
            mapItem[itemId] += itemCount;
        }
        _mapItem = std::move( mapItem );
        return true;
    }

    void ItemBag::addItem( const hashed_string& itemId, int32 count )
    {
        if ( itemId.empty() || count <= 0 )
            return;
        _mapItem[itemId] += count;
    }

    bool ItemBag::removeItem( const hashed_string& itemId, int32 count )
    {
        const auto mapIter = _mapItem.find( itemId );
        if ( count <= 0 || mapIter == _mapItem.end() || mapIter->second < count )
            return false;
        mapIter->second -= count;
        if ( mapIter->second == 0 )
            _mapItem.erase( mapIter );
        return true;
    }

    bool ItemBag::moveItemTo( ItemBag& target, const hashed_string& itemId, int32 count )
    {
        if ( &target == this || removeItem( itemId, count ) == false )
            return false;
        target.addItem( itemId, count );
        return true;
    }

    int32 ItemBag::getItemCount( const hashed_string& itemId ) const
    {
        const auto mapIter = _mapItem.find( itemId );
        return mapIter != _mapItem.end() ? mapIter->second : 0;
    }

    int32 ItemBag::getTotalCount() const
    {
        int32 total = 0;
        for ( const auto& [itemId, count] : _mapItem )
            total += count;
        return total;
    }

    void ItemBag::getItemIds( vector<hashed_string>& outListItem ) const
    {
        outListItem.clear();
        outListItem.reserve( _mapItem.size() );
        for ( const auto& [itemId, count] : _mapItem )
            outListItem.push_back( itemId );
    }
} // namespace sw
