#include "pch.h"

#include "GameFramework/Kits/SurvivalHorror/HorrorGridInventory.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/SurvivalHorror/HorrorCatalog.h"

namespace sw
{
    HorrorGridInventory::HorrorGridInventory()
        : _listCell{}
        , _listItem{}
        , _pCatalog{ nullptr }
        , _width{ 0 }
        , _height{ 0 }
        , _nextInstanceId{ 1 }
        , _revision{ 0 }
    {
    }

    void HorrorGridInventory::initialize( const HorrorCatalog* pCatalog, int32 width, int32 height )
    {
        _pCatalog = pCatalog;
        _width    = MathUtil::max( 1, width );
        _height   = MathUtil::max( 1, height );
        _listItem.clear();
        _listCell.assign( static_cast<size_t>( _width * _height ), -1 );
        ++_revision;
    }

    bool HorrorGridInventory::growGrid( int32 width, int32 height )
    {
        if ( width < _width || height < _height )
            return false;
        _width  = width;
        _height = height;
        _listCell.assign( static_cast<size_t>( _width * _height ), -1 );
        for ( const HorrorGridItem& item : _listItem )
            stampItem( item, item._instanceId );
        ++_revision;
        return true;
    }

    void HorrorGridInventory::clear()
    {
        _listItem.clear();
        _listCell.assign( _listCell.size(), -1 );
        ++_revision;
    }

    bool HorrorGridInventory::canPlace( const hashed_string& itemId, int32 x, int32 y, bool bRotated, int32 ignoreInstanceId ) const
    {
        const HorrorItemDef* pDef = findDef( itemId );
        if ( pDef == nullptr )
            return false;
        int32 footWidth  = 0;
        int32 footHeight = 0;
        computeFootprint( *pDef, bRotated, footWidth, footHeight );
        if ( x < 0 || y < 0 || x + footWidth > _width || y + footHeight > _height )
            return false;
        for ( int32 cellY = y; cellY < y + footHeight; ++cellY )
        {
            for ( int32 cellX = x; cellX < x + footWidth; ++cellX )
            {
                const int32 occupant = _listCell[static_cast<size_t>( cellY * _width + cellX )];
                if ( occupant >= 0 && occupant != ignoreInstanceId )
                    return false;
            }
        }
        return true;
    }

    bool HorrorGridInventory::findFreeSpot( const hashed_string& itemId, int32& outX, int32& outY, bool& outRotated ) const
    {
        for ( int32 rotation = 0; rotation < 2; ++rotation )
        {
            const bool bRotated = rotation == 1;
            for ( int32 y = 0; y < _height; ++y )
            {
                for ( int32 x = 0; x < _width; ++x )
                {
                    if ( canPlace( itemId, x, y, bRotated ) )
                    {
                        outX       = x;
                        outY       = y;
                        outRotated = bRotated;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    int32 HorrorGridInventory::placeItem( const hashed_string& itemId, int32 count, int32 x, int32 y, bool bRotated )
    {
        const HorrorItemDef* pDef = findDef( itemId );
        if ( pDef == nullptr || count <= 0 || count > pDef->_maxStack || canPlace( itemId, x, y, bRotated ) == false )
            return -1;
        HorrorGridItem item;
        item._itemId     = itemId;
        item._instanceId = _nextInstanceId++;
        item._count      = count;
        item._x          = x;
        item._y          = y;
        item._bRotated   = bRotated ? SW_TRUE : SW_FALSE;
        _listItem.push_back( item );
        stampItem( item, item._instanceId );
        ++_revision;
        return item._instanceId;
    }

    int32 HorrorGridInventory::addItem( const hashed_string& itemId, int32 count )
    {
        const HorrorItemDef* pDef = findDef( itemId );
        if ( pDef == nullptr || count <= 0 )
            return 0;
        int32 remaining = count;
        for ( HorrorGridItem& item : _listItem )
        {
            if ( remaining <= 0 )
                break;
            if ( item._itemId != itemId || item._count >= pDef->_maxStack )
                continue;
            const int32 moved = MathUtil::min( remaining, pDef->_maxStack - item._count );
            item._count += moved;
            remaining -= moved;
        }
        while ( remaining > 0 )
        {
            int32 x        = 0;
            int32 y        = 0;
            bool  bRotated = false;
            if ( findFreeSpot( itemId, x, y, bRotated ) == false )
                break;
            const int32 stackCount = MathUtil::min( remaining, pDef->_maxStack );
            if ( placeItem( itemId, stackCount, x, y, bRotated ) < 0 )
                break;
            remaining -= stackCount;
        }
        if ( remaining != count )
            ++_revision;
        return count - remaining;
    }

    bool HorrorGridInventory::removeItem( const hashed_string& itemId, int32 count )
    {
        if ( count <= 0 || getItemCount( itemId ) < count )
            return false;
        int32 remaining = count;
        for ( size_t itemIndex = _listItem.size(); itemIndex > 0 && remaining > 0; --itemIndex )
        {
            HorrorGridItem& item = _listItem[itemIndex - 1];
            if ( item._itemId != itemId )
                continue;
            const int32 taken = MathUtil::min( remaining, item._count );
            item._count -= taken;
            remaining -= taken;
            if ( item._count <= 0 )
                eraseItemAt( itemIndex - 1 );
        }
        ++_revision;
        return true;
    }

    int32 HorrorGridInventory::takeFromInstance( int32 instanceId, int32 count )
    {
        const int32 itemIndex = findItemIndex( instanceId );
        if ( itemIndex < 0 || count <= 0 )
            return 0;
        HorrorGridItem& item  = _listItem[static_cast<size_t>( itemIndex )];
        const int32     taken = MathUtil::min( count, item._count );
        item._count -= taken;
        if ( item._count <= 0 )
            eraseItemAt( static_cast<size_t>( itemIndex ) );
        ++_revision;
        return taken;
    }

    bool HorrorGridInventory::moveItem( int32 instanceId, int32 x, int32 y, bool bRotated )
    {
        const int32 itemIndex = findItemIndex( instanceId );
        if ( itemIndex < 0 )
            return false;
        HorrorGridItem& item = _listItem[static_cast<size_t>( itemIndex )];
        if ( canPlace( item._itemId, x, y, bRotated, instanceId ) == false )
            return false;
        stampItem( item, -1 );
        item._x        = x;
        item._y        = y;
        item._bRotated = bRotated ? SW_TRUE : SW_FALSE;
        stampItem( item, instanceId );
        ++_revision;
        return true;
    }

    bool HorrorGridInventory::rotateItem( int32 instanceId )
    {
        const HorrorGridItem* pItem = findInstance( instanceId );
        if ( pItem == nullptr )
            return false;
        return moveItem( instanceId, pItem->_x, pItem->_y, pItem->_bRotated == SW_FALSE );
    }

    int32 HorrorGridInventory::findInstanceAt( int32 x, int32 y ) const
    {
        if ( x < 0 || y < 0 || x >= _width || y >= _height )
            return -1;
        return _listCell[static_cast<size_t>( y * _width + x )];
    }

    const HorrorGridItem* HorrorGridInventory::findInstance( int32 instanceId ) const
    {
        const int32 itemIndex = findItemIndex( instanceId );
        return itemIndex >= 0 ? &_listItem[static_cast<size_t>( itemIndex )] : nullptr;
    }

    int32 HorrorGridInventory::getItemCount( const hashed_string& itemId ) const
    {
        int32 total = 0;
        for ( const HorrorGridItem& item : _listItem )
        {
            if ( item._itemId == itemId )
                total += item._count;
        }
        return total;
    }

    int32 HorrorGridInventory::countFreeCells() const
    {
        int32 freeCount = 0;
        for ( const int32 occupant : _listCell )
        {
            if ( occupant < 0 )
                ++freeCount;
        }
        return freeCount;
    }

    const HorrorItemDef* HorrorGridInventory::findDef( const hashed_string& itemId ) const
    {
        return _pCatalog != nullptr ? _pCatalog->findItem( itemId ) : nullptr;
    }

    int32 HorrorGridInventory::findItemIndex( int32 instanceId ) const
    {
        for ( size_t itemIndex = 0; itemIndex < _listItem.size(); ++itemIndex )
        {
            if ( _listItem[itemIndex]._instanceId == instanceId )
                return static_cast<int32>( itemIndex );
        }
        return -1;
    }

    void HorrorGridInventory::computeFootprint( const HorrorItemDef& def, bool bRotated, int32& outWidth, int32& outHeight ) const
    {
        outWidth  = bRotated ? def._height : def._width;
        outHeight = bRotated ? def._width : def._height;
    }

    void HorrorGridInventory::stampItem( const HorrorGridItem& item, int32 value )
    {
        const HorrorItemDef* pDef = findDef( item._itemId );
        if ( pDef == nullptr )
            return;
        int32 footWidth  = 0;
        int32 footHeight = 0;
        computeFootprint( *pDef, item._bRotated == SW_TRUE, footWidth, footHeight );
        for ( int32 cellY = item._y; cellY < item._y + footHeight && cellY < _height; ++cellY )
        {
            for ( int32 cellX = item._x; cellX < item._x + footWidth && cellX < _width; ++cellX )
                _listCell[static_cast<size_t>( cellY * _width + cellX )] = value;
        }
    }

    void HorrorGridInventory::eraseItemAt( size_t itemIndex )
    {
        stampItem( _listItem[itemIndex], -1 );
        _listItem.erase( _listItem.begin() + static_cast<ptrdiff_t>( itemIndex ) );
    }
} // namespace sw
