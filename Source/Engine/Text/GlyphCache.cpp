#include "pch.h"

#include "Engine/Text/GlyphCache.h"

#include "Core/Container/vector.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Text/IFontRasterizer.h"

namespace sw
{
    SW_LOG_CALLER( "GlyphCache" );
} // namespace sw

namespace sw
{
    GlyphCache::GlyphCache( IFontRasterizer& rasterizer, GlyphFieldKind kind )
        : _rasterizer{ rasterizer }
        , _atlas{ kind }
        , _mapGlyph{}
        , _rasterParams{}
        , _scratchBitmap{}
        , _rasterBudgetNanoseconds{ 0 }
        , _spentNanoseconds{ 0 }
        , _budgetFrameIndex{ invalid_index::kUint64 }
        , _frameIndex{ 0 }
    {
    }

    GlyphLookup GlyphCache::getOrCreateGlyph( FontFaceID face, uint32 glyphIndex, uint64 frameIndex )
    {
        const uint64 key  = makeKey( face, glyphIndex );
        const auto   iter = _mapGlyph.find( key );
        if ( iter != _mapGlyph.end() )
        {
            if ( iter->second._rect._width > 0 )
                _atlas.markPageUsed( iter->second._rect._page, frameIndex );
            return GlyphLookup{ &iter->second, GlyphLookupStatus::Ready };
        }

        if ( frameIndex != _budgetFrameIndex )
        {
            _budgetFrameIndex = frameIndex;
            _spentNanoseconds = 0;
        }
        if ( _rasterBudgetNanoseconds > 0 && _spentNanoseconds >= _rasterBudgetNanoseconds )
            return GlyphLookup{ nullptr, GlyphLookupStatus::Deferred };

        const Stopwatch    stopwatch;
        const CachedGlyph* pGlyph  = rasterizeAndAdd( face, glyphIndex, frameIndex );
        const int64        elapsed = stopwatch.getElapsedNanoseconds();
        _spentNanoseconds += elapsed > 0 ? elapsed : 1; // 시계 해상도보다 빨라도 센다 — 예산이 프레임 안에서 늘 닳는다
        return pGlyph != nullptr ? GlyphLookup{ pGlyph, GlyphLookupStatus::Ready } : GlyphLookup{};
    }

    const CachedGlyph* GlyphCache::rasterizeAndAdd( FontFaceID face, uint32 glyphIndex, uint64 frameIndex )
    {
        const uint64 key = makeKey( face, glyphIndex );

        const bool bRasterized = _atlas.getFieldKind() == GlyphFieldKind::MSDF ? _rasterizer.rasterizeMSDF( face, glyphIndex, _rasterParams, _scratchBitmap )
                                                                               : _rasterizer.rasterizeSDF( face, glyphIndex, _rasterParams, _scratchBitmap );
        if ( bRasterized == false )
            return nullptr;
        SW_PROFILE_COUNT( "Text.GlyphsRasterized", 1 );

        CachedGlyph glyph{};
        glyph._bearingPx       = _scratchBitmap._bearingPx;
        glyph._advancePx       = _scratchBitmap._advancePx;
        glyph._atlasGeneration = _atlas.getGeneration();
        const bool bHasBitmap  = _scratchBitmap._width > 0 && _scratchBitmap._height > 0;
        if ( bHasBitmap )
        {
            bool bAllocated = _atlas.allocate( _scratchBitmap._width, _scratchBitmap._height, glyph._rect );
            if ( bAllocated == false )
            {
                uint32 evictedPage = 0;
                if ( _atlas.evictLeastRecentlyUsedPage( frameIndex, evictedPage ) )
                {
                    forgetPage( evictedPage );
                    glyph._atlasGeneration = _atlas.getGeneration();
                    bAllocated             = _atlas.allocate( _scratchBitmap._width, _scratchBitmap._height, glyph._rect );
                }
            }
            if ( bAllocated == false )
            {
                SW_LOG_WARNING( "[Text] Glyph atlas is full with glyphs used this frame - glyph %# of face %# is not drawn this frame", glyphIndex, face );
                return nullptr;
            }
            _atlas.write( glyph._rect, _scratchBitmap._bytes.data() );
            _atlas.markPageUsed( glyph._rect._page, frameIndex );
        }
        const auto inserted = _mapGlyph.emplace( key, glyph );
        return &inserted.first->second;
    }

    void GlyphCache::forgetFace( FontFaceID face )
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

    void GlyphCache::forgetPage( uint32 page )
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
    }
} // namespace sw
