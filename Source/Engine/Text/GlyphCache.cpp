#include "pch.h"

#include "Engine/Text/GlyphCache.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Text/IFontRasterizer.h"

namespace sw
{
    GlyphCache::GlyphCache( IFontRasterizer& rasterizer, GlyphFieldKind kind, const GlyphAtlasLayout& atlasLayout )
        : _rasterizer{ rasterizer }
        , _atlas{ kind, atlasLayout }
        , _mapGlyph{}
        , _rasterParams{}
        , _scratchBitmap{}
        , _listEvictablePage{}
        , _listCompactPage{}
        , _listRetainKey{}
        , _retainBytes{}
        , _retainFrameCount{ atlasLayout._retainFrameCount }
        , _rasterBudgetNanoseconds{ 0 }
        , _spentNanoseconds{ 0 }
        , _budgetFrameIndex{ constant::invalid_index::kUint64 }
        , _frameIndex{ 0 }
        , _bCompactRequested{ SW_FALSE }
        , _bAtlasFullWarned{ SW_FALSE }
    {
    }

    void GlyphCache::beginFrame()
    {
        ++_frameIndex;
        // 프레임 첫머리라 이번 프레임에 쓴 글리프가 아직 없다 — 옮겨도 이번 프레임에 이미 그린 사각형이 어긋나지 않는다(UI 는 바뀐 세대를 보고 다시 칠한다).
        if ( _bCompactRequested == SW_TRUE )
        {
            _bCompactRequested = SW_FALSE;
            compactStaleGlyphs( _frameIndex );
        }
    }

    GlyphLookup GlyphCache::getOrCreateGlyph( FontFaceID face, uint32 glyphIndex, uint64 frameIndex )
    {
        const uint64 key  = makeKey( face, glyphIndex );
        const auto   iter = _mapGlyph.find( key );
        if ( iter != _mapGlyph.end() )
        {
            iter->second._lastUsedFrame = frameIndex;
            if ( iter->second._rect._width > 0 )
                _atlas.markPageUsed( iter->second._rect._page, frameIndex );
            return GlyphLookup{ &iter->second, GlyphLookupStatus::Ready };
        }
        // 이번 프레임에 이미 자리가 없었다 — 압축(다음 `beginFrame`)까지 래스터화해 봐야 버린다.
        if ( _bCompactRequested == SW_TRUE )
            return GlyphLookup{ nullptr, GlyphLookupStatus::Deferred };

        if ( frameIndex != _budgetFrameIndex )
        {
            _budgetFrameIndex = frameIndex;
            _spentNanoseconds = 0;
        }
        if ( _rasterBudgetNanoseconds > 0 && _spentNanoseconds >= _rasterBudgetNanoseconds )
            return GlyphLookup{ nullptr, GlyphLookupStatus::Deferred };

        const Stopwatch   stopwatch;
        const GlyphLookup lookup  = rasterizeAndAdd( face, glyphIndex, frameIndex );
        const int64       elapsed = stopwatch.getElapsedNanoseconds();
        _spentNanoseconds += elapsed > 0 ? elapsed : 1; // 시계 해상도보다 빨라도 센다 — 예산이 프레임 안에서 늘 닳는다
        return lookup;
    }

    GlyphLookup GlyphCache::rasterizeAndAdd( FontFaceID face, uint32 glyphIndex, uint64 frameIndex )
    {
        const uint64 key = makeKey( face, glyphIndex );

        const bool bRasterized = _atlas.getFieldKind() == GlyphFieldKind::MSDF ? _rasterizer.rasterizeMSDF( face, glyphIndex, _rasterParams, _scratchBitmap )
                                                                               : _rasterizer.rasterizeSDF( face, glyphIndex, _rasterParams, _scratchBitmap );
        if ( bRasterized == false )
            return GlyphLookup{};
        SW_PROFILE_COUNT( "Text.GlyphsRasterized", 1 );

        CachedGlyph glyph{};
        glyph._bearingPx       = _scratchBitmap._bearingPx;
        glyph._advancePx       = _scratchBitmap._advancePx;
        glyph._atlasGeneration = _atlas.getGeneration();
        glyph._lastUsedFrame   = frameIndex;
        const bool bHasBitmap  = _scratchBitmap._width > 0 && _scratchBitmap._height > 0;
        if ( bHasBitmap )
        {
            const uint32 paddedWidth  = _scratchBitmap._width + GlyphAtlas::kPadding * 2;
            const uint32 paddedHeight = _scratchBitmap._height + GlyphAtlas::kPadding * 2;
            if ( paddedWidth > _atlas.getPageSize() || paddedHeight > _atlas.getPageSize() )
            {
                SW_LOG_WARNING_ID( "GlyphCache.GlyphLargerThanAtlasPage", "[Text] Glyph %# of face %# (%#x%#) is larger than a glyph atlas page (%#) - it is not drawn", glyphIndex, face,
                                   _scratchBitmap._width, _scratchBitmap._height, _atlas.getPageSize() );
                return GlyphLookup{};
            }
            if ( allocateOrEvictStalePage( _scratchBitmap._width, _scratchBitmap._height, frameIndex, glyph._rect ) == false )
            {
                _bCompactRequested = SW_TRUE;
                return GlyphLookup{ nullptr, GlyphLookupStatus::Deferred };
            }
            glyph._atlasGeneration = _atlas.getGeneration();
            _atlas.write( glyph._rect, _scratchBitmap._bytes.data() );
            _atlas.markPageUsed( glyph._rect._page, frameIndex );
        }
        const auto inserted = _mapGlyph.emplace( key, glyph );
        return GlyphLookup{ &inserted.first->second, GlyphLookupStatus::Ready };
    }

    void GlyphCache::removeFace( FontFaceID face )
    {
        vector<uint64> listKey;
        for ( const auto& [key, glyph] : _mapGlyph )
        {
            (void)glyph;
            if ( static_cast<FontFaceID>( key >> 32 ) == face )
                listKey.push_back( key );
        }
        for ( const uint64 key : listKey )
        {
            _mapGlyph.erase( key );
        }
    }

    bool GlyphCache::allocateOrEvictStalePage( uint32 width, uint32 height, uint64 frameIndex, GlyphAtlasRect& outRect )
    {
        if ( _atlas.allocate( width, height, outRect ) )
            return true;
        // 페이지의 사용 프레임은 그 페이지 글리프의 가장 늦은 사용이다 — 이보다 이르면 페이지의 모든 글리프가 최근 창 밖이라 통째로 비워도 최근 글리프를 잃지 않는다.
        const uint64 staleBefore = frameIndex > _retainFrameCount ? frameIndex - _retainFrameCount : 0;
        _atlas.listEvictablePages( staleBefore, _listEvictablePage );
        if ( _listEvictablePage.empty() )
            return false;
        removePage( _listEvictablePage.front() );
        return _atlas.allocate( width, height, outRect );
    }

    bool GlyphCache::isStale( const CachedGlyph& glyph, uint64 frameIndex ) const
    {
        return glyph._lastUsedFrame + _retainFrameCount < frameIndex;
    }

    void GlyphCache::compactStaleGlyphs( uint64 frameIndex )
    {
        // 오래된 글리프가 하나라도 있는 페이지만 비운다 — 나머지 페이지는 업로드도 없다.
        _listCompactPage.assign( _atlas.getPageCount(), SW_FALSE );
        bool bAnyStale = false;
        for ( const auto& [key, glyph] : _mapGlyph )
        {
            (void)key;
            if ( glyph._rect._width > 0 && isStale( glyph, frameIndex ) )
            {
                _listCompactPage[glyph._rect._page] = SW_TRUE;
                bAnyStale                           = true;
            }
        }
        if ( bAnyStale == false )
        {
            if ( _bAtlasFullWarned == SW_FALSE )
                SW_LOG_WARNING_ID( "GlyphCache.GlyphAtlasFullGlyphs",
                                   "[Text] Glyph atlas is full with glyphs used in the last %# frames - new glyphs are deferred. Raise _glyphAtlasMaxPageCount or _glyphAtlasPageSize in the font catalog",
                                   _retainFrameCount );
            _bAtlasFullWarned = SW_TRUE;
            return;
        }
        _bAtlasFullWarned = SW_FALSE;

        _listRetainKey.clear();
        _retainBytes.clear();
        vector<uint64> listDropKey;
        for ( const auto& [key, glyph] : _mapGlyph )
        {
            if ( glyph._rect._width == 0 || _listCompactPage[glyph._rect._page] == SW_FALSE )
                continue;
            if ( isStale( glyph, frameIndex ) )
                listDropKey.push_back( key );
            else
                _listRetainKey.push_back( key );
        }
        for ( const uint64 key : listDropKey )
        {
            _mapGlyph.erase( key );
        }
        // 표 순서는 결정적이지 않다 — 높이 큰 것부터, 같으면 키 순으로 다시 잡는다(스카이라인이 덜 조각나고 결과가 기계마다 같다).
        VectorUtil::sort( _listRetainKey, [this]( uint64 a, uint64 b )
        {
            const uint16 heightA = _mapGlyph.find( a )->second._rect._height;
            const uint16 heightB = _mapGlyph.find( b )->second._rect._height;
            return heightA != heightB ? heightA > heightB : a < b;
        } );
        const size_t texelBytes = _atlas.getBytesPerTexel();
        for ( const uint64 key : _listRetainKey )
        {
            const GlyphAtlasRect& rect   = _mapGlyph.find( key )->second._rect;
            const size_t          offset = _retainBytes.size();
            _retainBytes.resize( offset + static_cast<size_t>( rect._width ) * rect._height * texelBytes );
            _atlas.read( rect, _retainBytes.data() + offset );
        }
        for ( uint32 page = 0; page < static_cast<uint32>( _listCompactPage.size() ); ++page )
        {
            if ( _listCompactPage[page] == SW_TRUE )
                _atlas.clearPage( page );
        }

        size_t offset = 0;
        for ( const uint64 key : _listRetainKey )
        {
            CachedGlyph&   glyph     = _mapGlyph.find( key )->second;
            const size_t   byteCount = static_cast<size_t>( glyph._rect._width ) * glyph._rect._height * texelBytes;
            const uint8*   pBytes    = _retainBytes.data() + offset;
            GlyphAtlasRect moved{};
            offset += byteCount;
            if ( _atlas.allocate( glyph._rect._width, glyph._rect._height, moved ) == false )
            {
                _mapGlyph.erase( key ); // 다시 잡은 자리가 앞보다 덜 촘촘하다 — 프레임 첫머리라 이번 프레임 글리프가 아니고, 다음 조회가 다시 래스터화한다
                continue;
            }
            _atlas.write( moved, pBytes ); // 다시 래스터화하지 않고 바이트를 옮긴다 — 업로드는 비운 페이지 전체나 새 자리의 구간으로 나간다
            _atlas.markPageUsed( moved._page, glyph._lastUsedFrame );
            glyph._rect            = moved;
            glyph._atlasGeneration = _atlas.getGeneration();
            SW_PROFILE_COUNT( "Text.GlyphsRelocated", 1 );
        }
    }

    void GlyphCache::removePage( uint32 page )
    {
        vector<uint64> listKey;
        for ( const auto& [key, glyph] : _mapGlyph )
        {
            if ( glyph._rect._width > 0 && glyph._rect._page == page )
                listKey.push_back( key );
        }
        for ( const uint64 key : listKey )
        {
            _mapGlyph.erase( key );
        }
        _atlas.clearPage( page );
    }
} // namespace sw
