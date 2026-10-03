#include "pch.h"

#include "GameFramework/Inventory/GridInventory.h"

#include "Core/Math/MathUtil.h"


namespace sw
{
    GridInventory::GridInventory()
        : _listCell{}
        , _listItem{}
        , _shapeLookup{}
        , _width{ 0 }
        , _height{ 0 }
        , _nextInstanceId{ 1 }
        , _revision{ 0 }
    {
    }

    void GridInventory::initialize( const ShapeDelegate& shapeLookup, int32 width, int32 height )
    {
        _shapeLookup = shapeLookup;
        _width    = MathUtil::max( 1, width );
        _height   = MathUtil::max( 1, height );
        _listItem.clear();
        _listCell.assign( static_cast<size_t>( _width * _height ), -1 );
        ++_revision;
    }

    bool GridInventory::growGrid( int32 width, int32 height )
    {
        if ( width < _width || height < _height )
            return false;
        _width  = width;
        _height = height;
        _listCell.assign( static_cast<size_t>( _width * _height ), -1 );
        for ( const GridItem& item : _listItem )
            stampItem( item, item._instanceId );
        ++_revision;
        return true;
    }

    void GridInventory::clear()
    {
        _listItem.clear();
        _listCell.assign( _listCell.size(), -1 );
        ++_revision;
    }

    bool GridInventory::canPlace( const hashed_string& itemId, int32 x, int32 y, bool bRotated, int32 ignoreInstanceId ) const
    {
        GridItemShape shape;
        if ( findShape( itemId, shape ) == false )
            return false;
        int32 footWidth  = 0;
        int32 footHeight = 0;
        computeFootprint( shape, bRotated, footWidth, footHeight );
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

    bool GridInventory::findFreeSpot( const hashed_string& itemId, int32& outX, int32& outY, bool& outRotated ) const
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

    int32 GridInventory::placeItem( const hashed_string& itemId, int32 count, int32 x, int32 y, bool bRotated )
    {
        GridItemShape shape;
        if ( findShape( itemId, shape ) == false || count <= 0 || count > shape._maxStack || canPlace( itemId, x, y, bRotated ) == false )
            return -1;
        GridItem item;
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

    int32 GridInventory::addItem( const hashed_string& itemId, int32 count )
    {
        GridItemShape shape;
        if ( findShape( itemId, shape ) == false || count <= 0 )
            return 0;
        int32 remaining = count;
        for ( GridItem& item : _listItem )
        {
            if ( remaining <= 0 )
                break;
            if ( item._itemId != itemId || item._count >= shape._maxStack )
                continue;
            const int32 moved = MathUtil::min( remaining, shape._maxStack - item._count );
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
            const int32 stackCount = MathUtil::min( remaining, shape._maxStack );
            if ( placeItem( itemId, stackCount, x, y, bRotated ) < 0 )
                break;
            remaining -= stackCount;
        }
        if ( remaining != count )
            ++_revision;
        return count - remaining;
    }

    bool GridInventory::removeItem( const hashed_string& itemId, int32 count )
    {
        if ( count <= 0 || getItemCount( itemId ) < count )
            return false;
        int32 remaining = count;
        for ( size_t itemIndex = _listItem.size(); itemIndex > 0 && remaining > 0; --itemIndex )
        {
            GridItem& item = _listItem[itemIndex - 1];
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

    int32 GridInventory::takeFromInstance( int32 instanceId, int32 count )
    {
        const int32 itemIndex = findItemIndex( instanceId );
        if ( itemIndex < 0 || count <= 0 )
            return 0;
        GridItem& item  = _listItem[static_cast<size_t>( itemIndex )];
        const int32     taken = MathUtil::min( count, item._count );
        item._count -= taken;
        if ( item._count <= 0 )
            eraseItemAt( static_cast<size_t>( itemIndex ) );
        ++_revision;
        return taken;
    }

    bool GridInventory::moveItem( int32 instanceId, int32 x, int32 y, bool bRotated )
    {
        const int32 itemIndex = findItemIndex( instanceId );
        if ( itemIndex < 0 )
            return false;
        GridItem& item = _listItem[static_cast<size_t>( itemIndex )];
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

    bool GridInventory::rotateItem( int32 instanceId )
    {
        const GridItem* pItem = findInstance( instanceId );
        if ( pItem == nullptr )
            return false;
        return moveItem( instanceId, pItem->_x, pItem->_y, pItem->_bRotated == SW_FALSE );
    }

    int32 GridInventory::findInstanceAt( int32 x, int32 y ) const
    {
        if ( x < 0 || y < 0 || x >= _width || y >= _height )
            return -1;
        return _listCell[static_cast<size_t>( y * _width + x )];
    }

    const GridItem* GridInventory::findInstance( int32 instanceId ) const
    {
        const int32 itemIndex = findItemIndex( instanceId );
        return itemIndex >= 0 ? &_listItem[static_cast<size_t>( itemIndex )] : nullptr;
    }

    int32 GridInventory::getItemCount( const hashed_string& itemId ) const
    {
        int32 total = 0;
        for ( const GridItem& item : _listItem )
        {
            if ( item._itemId == itemId )
                total += item._count;
        }
        return total;
    }

    int32 GridInventory::countFreeCells() const
    {
        int32 freeCount = 0;
        for ( const int32 occupant : _listCell )
        {
            if ( occupant < 0 )
                ++freeCount;
        }
        return freeCount;
    }

    bool GridInventory::findShape( const hashed_string& itemId, GridItemShape& outShape ) const
    {
        if ( _shapeLookup.isBound() == false || _shapeLookup( itemId, outShape ) == false )
            return false;
        outShape._width    = MathUtil::max( 1, outShape._width );
        outShape._height   = MathUtil::max( 1, outShape._height );
        outShape._maxStack = MathUtil::max( 1, outShape._maxStack );
        return true;
    }

    int32 GridInventory::findItemIndex( int32 instanceId ) const
    {
        for ( size_t itemIndex = 0; itemIndex < _listItem.size(); ++itemIndex )
        {
            if ( _listItem[itemIndex]._instanceId == instanceId )
                return static_cast<int32>( itemIndex );
        }
        return -1;
    }

    void GridInventory::computeFootprint( const GridItemShape& shape, bool bRotated, int32& outWidth, int32& outHeight ) const
    {
        outWidth  = bRotated ? shape._height : shape._width;
        outHeight = bRotated ? shape._width : shape._height;
    }

    void GridInventory::stampItem( const GridItem& item, int32 value )
    {
        GridItemShape shape;
        if ( findShape( item._itemId, shape ) == false )
            return;
        int32 footWidth  = 0;
        int32 footHeight = 0;
        computeFootprint( shape, item._bRotated == SW_TRUE, footWidth, footHeight );
        for ( int32 cellY = item._y; cellY < item._y + footHeight && cellY < _height; ++cellY )
        {
            for ( int32 cellX = item._x; cellX < item._x + footWidth && cellX < _width; ++cellX )
                _listCell[static_cast<size_t>( cellY * _width + cellX )] = value;
        }
    }

    void GridInventory::eraseItemAt( size_t itemIndex )
    {
        stampItem( _listItem[itemIndex], -1 );
        _listItem.erase( _listItem.begin() + static_cast<ptrdiff_t>( itemIndex ) );
    }
} // namespace sw
