#include "pch.h"

#include "Core/Container/vector.h"

#include "Engine/Text/GlyphAtlas.h"
#include "Engine/Text/GlyphCache.h"

#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

// GlyphAtlasTest — SDF 글리프 아틀라스(스카이라인 패킹 · 구간 업로드 · 오래된 페이지 비우기)와 글리프 캐시. 순수 CPU(nogpu).

namespace
{
    struct GlyphAtlasTestUtil
    {
        /** @brief 아틀라스 한 페이지를 사각형 하나로 거의 채우는 크기입니다(여백을 더해도 페이지에 들어간다). */
        static constexpr uint32 kPageFillSize = 900;

        static bool overlaps( const sw::GlyphAtlasRect& a, const sw::GlyphAtlasRect& b )
        {
            const bool bSamePage = a._page == b._page;
            const bool bOverlapX = a._x < b._x + b._width && b._x < a._x + a._width;
            const bool bOverlapY = a._y < b._y + b._height && b._y < a._y + a._height;
            return bSamePage && bOverlapX && bOverlapY;
        }
    };
} // namespace

/** @brief [GlyphAtlasTest] 같은 순서로 넣으면 같은 자리 — 패킹은 결정적이고 서로 겹치지 않으며 페이지 안에 있다 */
SW_TEST_CASE( GlyphAtlasTest, PackingIsDeterministicAndDisjoint )
{
    sw::GlyphAtlas                 atlasA;
    sw::GlyphAtlas                 atlasB;
    sw::vector<sw::GlyphAtlasRect> listRectA;
    for ( uint32 index = 0; index < 300; ++index )
    {
        const uint32       width  = 10 + ( index * 7 ) % 40;
        const uint32       height = 20 + ( index * 13 ) % 30;
        sw::GlyphAtlasRect rectA{};
        sw::GlyphAtlasRect rectB{};
        SW_ASSERT_TRUE( atlasA.allocate( width, height, rectA ) );
        SW_ASSERT_TRUE( atlasB.allocate( width, height, rectB ) );
        SW_EXPECT_EQUAL( rectA._x, rectB._x );
        SW_EXPECT_EQUAL( rectA._y, rectB._y );
        SW_EXPECT_EQUAL( rectA._page, rectB._page );
        SW_EXPECT_TRUE( static_cast<uint32>( rectA._x ) + rectA._width <= sw::GlyphAtlas::kPageSize );
        SW_EXPECT_TRUE( static_cast<uint32>( rectA._y ) + rectA._height <= sw::GlyphAtlas::kPageSize );
        listRectA.push_back( rectA );
    }
    uint32 overlapCount = 0;
    for ( size_t first = 0; first < listRectA.size(); ++first )
    {
        for ( size_t second = first + 1; second < listRectA.size(); ++second )
        {
            if ( GlyphAtlasTestUtil::overlaps( listRectA[first], listRectA[second] ) )
                ++overlapCount;
        }
    }
    SW_EXPECT_EQUAL( 0u, overlapCount );
    SW_EXPECT_EQUAL( 1u, atlasA.getPageCount() ); // 300 개의 작은 사각형은 한 페이지에 들어간다
}

/** @brief [GlyphAtlasTest] 페이지 첫 업로드는 전체 한 건, 그 뒤에는 쓴 구간만 정확히 그 바이트로 넘어가고 다시 부르면 0 건 */
SW_TEST_CASE( GlyphAtlasTest, UploadsCarryOnlyWrittenRegions )
{
    sw::GlyphAtlas     atlas;
    sw::GlyphAtlasRect first{};
    SW_ASSERT_TRUE( atlas.allocate( 3, 2, first ) );
    sw::vector<sw::GlyphAtlasUpload> listUpload;
    atlas.takeUploads( listUpload );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listUpload.size() ) );
    SW_EXPECT_EQUAL( SW_TRUE, listUpload[0]._bWholePage );
    SW_EXPECT_EQUAL( static_cast<size_t>( sw::GlyphAtlas::kPageSize ) * sw::GlyphAtlas::kPageSize, listUpload[0]._bytes.size() );

    const uint8 arrFirstByte[6]  = { 1, 2, 3, 4, 5, 6 };
    const uint8 arrSecondByte[4] = { 9, 8, 7, 6 };
    atlas.write( first, arrFirstByte );
    sw::GlyphAtlasRect second{};
    SW_ASSERT_TRUE( atlas.allocate( 2, 2, second ) );
    atlas.write( second, arrSecondByte );
    listUpload.clear();
    atlas.takeUploads( listUpload );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listUpload.size() ) );
    SW_EXPECT_EQUAL( SW_FALSE, listUpload[0]._bWholePage );
    SW_EXPECT_EQUAL( first._x, listUpload[0]._x );
    SW_EXPECT_EQUAL( first._y, listUpload[0]._y );
    SW_ASSERT_EQUAL( 6u, static_cast<uint32>( listUpload[0]._bytes.size() ) );
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( listUpload[1]._bytes.size() ) );
    for ( uint32 index = 0; index < 6; ++index )
    {
        SW_EXPECT_EQUAL( arrFirstByte[index], listUpload[0]._bytes[index] );
    }
    for ( uint32 index = 0; index < 4; ++index )
    {
        SW_EXPECT_EQUAL( arrSecondByte[index], listUpload[1]._bytes[index] );
    }
    const uint8* pPage = atlas.getPageBytes( 0 );
    SW_EXPECT_EQUAL( static_cast<uint8>( 4 ), pPage[( static_cast<size_t>( first._y ) + 1 ) * sw::GlyphAtlas::kPageSize + first._x] ); // 둘째 행 첫 바이트

    listUpload.clear();
    atlas.takeUploads( listUpload );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( listUpload.size() ) );
}

/** @brief [GlyphAtlasTest] 다 차면 이번 프레임에 안 쓴 가장 오래된 페이지를 비우고 세대가 오르며 전체 업로드가 기록된다 — 모두 이번 프레임이면 비우지 않는다 */
SW_TEST_CASE( GlyphAtlasTest, FullAtlasEvictsOldestUnusedPage )
{
    sw::GlyphAtlas atlas;
    const uint32   size = GlyphAtlasTestUtil::kPageFillSize;
    for ( uint32 index = 0; index < sw::GlyphAtlas::kMaxPageCount; ++index )
    {
        sw::GlyphAtlasRect rect{};
        SW_ASSERT_TRUE( atlas.allocate( size, size, rect ) );
        SW_EXPECT_EQUAL( index, static_cast<uint32>( rect._page ) );
        atlas.markPageUsed( rect._page, index + 1 ); // 페이지 0 이 프레임 1(가장 오래)
    }
    sw::GlyphAtlasRect overflow{};
    SW_EXPECT_FALSE( atlas.allocate( size, size, overflow ) );
    sw::vector<sw::GlyphAtlasUpload> listUpload;
    atlas.takeUploads( listUpload );
    listUpload.clear();

    const uint32 generationBefore = atlas.getGeneration();
    uint32       evictedPage      = 99;
    SW_ASSERT_TRUE( atlas.evictLeastRecentlyUsedPage( 9, evictedPage ) );
    SW_EXPECT_EQUAL( 0u, evictedPage );
    SW_EXPECT_EQUAL( generationBefore + 1, atlas.getGeneration() );
    atlas.takeUploads( listUpload );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listUpload.size() ) );
    SW_EXPECT_EQUAL( SW_TRUE, listUpload[0]._bWholePage );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( listUpload[0]._page ) );
    SW_EXPECT_TRUE( atlas.allocate( size, size, overflow ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( overflow._page ) );

    for ( uint32 page = 0; page < atlas.getPageCount(); ++page )
    {
        atlas.markPageUsed( page, 20 );
    }
    SW_EXPECT_FALSE( atlas.evictLeastRecentlyUsedPage( 20, evictedPage ) );
}

/** @brief [GlyphAtlasTest] 캐시가 페이지를 비우면 그 페이지에 있던 글리프는 다음 조회에서 다시 래스터화된다 — 공백은 사각형 없이 들고, 있는 글리프는 다시 굽지 않는다 */
SW_TEST_CASE( GlyphAtlasTest, GlyphCacheReRasterizesAfterEviction )
{
    sw::test::FakeFontRasterizer rasterizer;
    rasterizer.setSdfSize( GlyphAtlasTestUtil::kPageFillSize ); // 글리프 하나가 페이지 하나
    const sw::FontFaceID face = rasterizer.loadFace( sw::vector<uint8>( 1, static_cast<uint8>( 1 ) ), 0, "fake" );
    sw::GlyphCache       cache( rasterizer );

    for ( uint32 index = 0; index < sw::GlyphAtlas::kMaxPageCount; ++index )
    {
        const sw::CachedGlyph* pGlyph = cache.findOrAddGlyph( face, 'A' + index, index + 1 );
        SW_ASSERT_NOT_NULL( pGlyph );
        SW_EXPECT_EQUAL( index, static_cast<uint32>( pGlyph->_rect._page ) );
    }
    SW_EXPECT_EQUAL( sw::GlyphAtlas::kMaxPageCount, rasterizer.getRasterizeCount() );
    SW_EXPECT_NOT_NULL( cache.findOrAddGlyph( face, 'A' + 1, 9 ) ); // 있는 글리프 — 다시 굽지 않고 페이지 1 을 프레임 9 에 썼다고 적는다
    SW_EXPECT_EQUAL( sw::GlyphAtlas::kMaxPageCount, rasterizer.getRasterizeCount() );

    // 아홉째 글리프 — 프레임 9 에 안 쓴 가장 오래된 페이지 0('A')을 비우고 그 자리에 들어간다.
    const sw::CachedGlyph* pNinth = cache.findOrAddGlyph( face, 'Z', 9 );
    SW_ASSERT_NOT_NULL( pNinth );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( pNinth->_rect._page ) );
    SW_EXPECT_EQUAL( 1u, pNinth->_atlasGeneration );
    SW_EXPECT_EQUAL( sw::GlyphAtlas::kMaxPageCount, cache.getGlyphCount() ); // 'A' 는 표에서 빠졌다

    const uint32 rasterizedBefore = rasterizer.getRasterizeCount();
    SW_EXPECT_NOT_NULL( cache.findOrAddGlyph( face, 'A', 10 ) );
    SW_EXPECT_EQUAL( rasterizedBefore + 1, rasterizer.getRasterizeCount() );

    const sw::CachedGlyph* pSpace = cache.findOrAddGlyph( face, ' ', 10 );
    SW_ASSERT_NOT_NULL( pSpace );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( pSpace->_rect._width ) );
    SW_EXPECT_TRUE( pSpace->_advancePx > 0.0f );
}
