#include "pch.h"

#include "Engine/Text/GlyphAtlas.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    GlyphAtlas::GlyphAtlas()
        : _listPage{}
        , _generation{ 0 }
    {
    }

    void GlyphAtlas::addPage()
    {
        Page page{};
        page._bytes.assign( static_cast<size_t>( kPageSize ) * kPageSize, static_cast<uint8>( 0 ) ); // 0 = 가장 먼 바깥(SDF)
        page._listSkyline.push_back( SkylineNode{ 0, 0, static_cast<uint16>( kPageSize ) } );
        page._bWholePageDirty = SW_TRUE;
        _listPage.push_back( std::move( page ) );
    }

    bool GlyphAtlas::allocate( uint32 width, uint32 height, GlyphAtlasRect& outRect )
    {
        const uint32 paddedWidth  = width + kPadding * 2;
        const uint32 paddedHeight = height + kPadding * 2;
        if ( paddedWidth > kPageSize || paddedHeight > kPageSize )
            return false;
        for ( uint32 pageIndex = 0; pageIndex <= static_cast<uint32>( _listPage.size() ); ++pageIndex )
        {
            if ( pageIndex == _listPage.size() )
            {
                if ( _listPage.size() >= kMaxPageCount )
                    return false;
                addPage();
            }
            uint16 x = 0;
            uint16 y = 0;
            if ( allocateInPage( _listPage[pageIndex], paddedWidth, paddedHeight, x, y ) )
            {
                outRect._page   = static_cast<uint16>( pageIndex );
                outRect._x      = static_cast<uint16>( x + kPadding );
                outRect._y      = static_cast<uint16>( y + kPadding );
                outRect._width  = static_cast<uint16>( width );
                outRect._height = static_cast<uint16>( height );
                return true;
            }
        }
        return false;
    }

    bool GlyphAtlas::findFitHeight( const Page& page, size_t nodeIndex, uint32 width, uint32 height, uint32& outY )
    {
        const SkylineNode& first = page._listSkyline[nodeIndex];
        if ( static_cast<uint32>( first._x ) + width > kPageSize )
            return false;
        uint32 widthLeft = width;
        uint32 y         = first._y;
        size_t index     = nodeIndex;
        while ( widthLeft > 0 )
        {
            if ( index >= page._listSkyline.size() )
                return false;
            const SkylineNode& node = page._listSkyline[index];
            y                       = MathUtil::max( y, static_cast<uint32>( node._y ) );
            if ( y + height > kPageSize )
                return false;
            widthLeft = node._width >= widthLeft ? 0u : widthLeft - node._width;
            ++index;
        }
        outY = y;
        return true;
    }

    bool GlyphAtlas::allocateInPage( Page& page, uint32 width, uint32 height, uint16& outX, uint16& outY )
    {
        // 1) 윗변(y + 높이)이 가장 낮은 자리, 같으면 마디가 좁은 자리(남는 틈이 작다).
        bool   bFound    = false;
        uint32 bestTop   = 0;
        uint32 bestWidth = 0;
        size_t bestIndex = 0;
        uint32 bestY     = 0;
        for ( size_t index = 0; index < page._listSkyline.size(); ++index )
        {
            uint32 y = 0;
            if ( findFitHeight( page, index, width, height, y ) == false )
                continue;
            const uint32 top       = y + height;
            const uint32 nodeWidth = page._listSkyline[index]._width;
            const bool   bBetter   = bFound == false || top < bestTop || ( top == bestTop && nodeWidth < bestWidth );
            if ( bBetter )
            {
                bFound    = true;
                bestTop   = top;
                bestWidth = nodeWidth;
                bestIndex = index;
                bestY     = y;
            }
        }
        if ( bFound == false )
            return false;

        // 2) 새 마디를 끼우고, 그 너비가 덮는 뒤 마디들을 줄이거나 지운다.
        const uint16 x = page._listSkyline[bestIndex]._x;
        page._listSkyline.insert( page._listSkyline.begin() + static_cast<ptrdiff_t>( bestIndex ),
                                  SkylineNode{ x, static_cast<uint16>( bestY + height ), static_cast<uint16>( width ) } );
        for ( size_t index = bestIndex + 1; index < page._listSkyline.size(); )
        {
            SkylineNode&       node        = page._listSkyline[index];
            const SkylineNode& previous    = page._listSkyline[index - 1];
            const uint32       previousEnd = static_cast<uint32>( previous._x ) + previous._width;
            if ( node._x >= previousEnd )
                break;
            const uint32 shrink = previousEnd - node._x;
            if ( node._width <= shrink )
            {
                page._listSkyline.erase( page._listSkyline.begin() + static_cast<ptrdiff_t>( index ) );
                continue;
            }
            node._x     = static_cast<uint16>( node._x + shrink );
            node._width = static_cast<uint16>( node._width - shrink );
            break;
        }
        // 3) 높이가 같은 이웃 마디를 합친다(마디 수가 늘어 찾기가 느려지지 않게).
        for ( size_t index = 0; index + 1 < page._listSkyline.size(); )
        {
            SkylineNode&       node = page._listSkyline[index];
            const SkylineNode& next = page._listSkyline[index + 1];
            if ( node._y == next._y )
            {
                node._width = static_cast<uint16>( node._width + next._width );
                page._listSkyline.erase( page._listSkyline.begin() + static_cast<ptrdiff_t>( index + 1 ) );
                continue;
            }
            ++index;
        }
        outX = x;
        outY = static_cast<uint16>( bestY );
        return true;
    }

    void GlyphAtlas::write( const GlyphAtlasRect& rect, const uint8* pBytes )
    {
        Page& page = _listPage[rect._page];
        for ( uint32 row = 0; row < rect._height; ++row )
        {
            uint8* pDest = page._bytes.data() + ( static_cast<size_t>( rect._y ) + row ) * kPageSize + rect._x;
            Memory::copy( pDest, pBytes + static_cast<size_t>( row ) * rect._width, rect._width );
        }
        if ( page._bWholePageDirty == SW_FALSE )
            page._listDirty.push_back( rect );
    }

    void GlyphAtlas::markPageUsed( uint32 page, uint64 frameIndex ) { _listPage[page]._lastUsedFrame = frameIndex; }

    bool GlyphAtlas::evictLeastRecentlyUsedPage( uint64 currentFrame, uint32& outPage )
    {
        bool   bFound      = false;
        uint64 oldestFrame = 0;
        size_t oldestIndex = 0;
        for ( size_t index = 0; index < _listPage.size(); ++index )
        {
            const uint64 lastUsed = _listPage[index]._lastUsedFrame;
            const bool   bOlder   = lastUsed < currentFrame && ( bFound == false || lastUsed < oldestFrame );
            if ( bOlder )
            {
                bFound      = true;
                oldestFrame = lastUsed;
                oldestIndex = index;
            }
        }
        if ( bFound == false )
            return false;
        Page& page = _listPage[oldestIndex];
        (void)Memory::set( page._bytes.data(), 0, page._bytes.size() );
        page._listSkyline.assign( 1, SkylineNode{ 0, 0, static_cast<uint16>( kPageSize ) } );
        page._listDirty.clear();
        page._bWholePageDirty = SW_TRUE;
        ++_generation;
        outPage = static_cast<uint32>( oldestIndex );
        return true;
    }

    void GlyphAtlas::takeUploads( vector<GlyphAtlasUpload>& outListUpload )
    {
        for ( size_t pageIndex = 0; pageIndex < _listPage.size(); ++pageIndex )
        {
            Page& page = _listPage[pageIndex];
            if ( page._bWholePageDirty == SW_TRUE )
            {
                GlyphAtlasUpload& upload = outListUpload.emplace_back();
                upload._page             = static_cast<uint16>( pageIndex );
                upload._width            = static_cast<uint16>( kPageSize );
                upload._height           = static_cast<uint16>( kPageSize );
                upload._bWholePage       = SW_TRUE;
                upload._bytes            = page._bytes;
                page._bWholePageDirty    = SW_FALSE;
                page._listDirty.clear();
                continue;
            }
            for ( const GlyphAtlasRect& rect : page._listDirty )
            {
                GlyphAtlasUpload& upload = outListUpload.emplace_back();
                upload._page             = rect._page;
                upload._x                = rect._x;
                upload._y                = rect._y;
                upload._width            = rect._width;
                upload._height           = rect._height;
                upload._bytes.resize( static_cast<size_t>( rect._width ) * rect._height );
                for ( uint32 row = 0; row < rect._height; ++row )
                {
                    const uint8* pSource = page._bytes.data() + ( static_cast<size_t>( rect._y ) + row ) * kPageSize + rect._x;
                    Memory::copy( upload._bytes.data() + static_cast<size_t>( row ) * rect._width, pSource, rect._width );
                }
            }
            page._listDirty.clear();
        }
    }
} // namespace sw
