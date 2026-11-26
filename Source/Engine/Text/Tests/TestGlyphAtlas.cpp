// GlyphAtlasTest — SDF 글리프 아틀라스(스카이라인 패킹 · 구간 업로드 · 오래된 페이지 비우기)와 글리프 캐시. 순수 CPU(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Text/GlyphAtlas.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/Text/Tests/FakeFontRasterizer.h"

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
    sw::GlyphAtlas                 atlasA{ sw::GlyphFieldKind::Sdf, sw::test::kTestGlyphAtlasLayout };
    sw::GlyphAtlas                 atlasB{ sw::GlyphFieldKind::Sdf, sw::test::kTestGlyphAtlasLayout };
    sw::vector<sw::GlyphAtlasRect> listRectA;
    for ( uint32 index = 0; index < 300; ++index )
    {
        const uint32       width  = 10 + ( index * 7 ) % 40;
        const uint32       height = 20 + ( index * 13 ) % 30;
        sw::GlyphAtlasRect rectA{};
        sw::GlyphAtlasRect rectB{};
        SW_REQUIRE_TRUE( atlasA.allocate( width, height, rectA ) );
        SW_REQUIRE_TRUE( atlasB.allocate( width, height, rectB ) );
        SW_EXPECT_EQUAL( rectA._x, rectB._x );
        SW_EXPECT_EQUAL( rectA._y, rectB._y );
        SW_EXPECT_EQUAL( rectA._page, rectB._page );
        SW_EXPECT_TRUE( static_cast<uint32>( rectA._x ) + rectA._width <= sw::test::kTestGlyphAtlasLayout._pageSize );
        SW_EXPECT_TRUE( static_cast<uint32>( rectA._y ) + rectA._height <= sw::test::kTestGlyphAtlasLayout._pageSize );
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
    sw::GlyphAtlas     atlas{ sw::GlyphFieldKind::Sdf, sw::test::kTestGlyphAtlasLayout };
    sw::GlyphAtlasRect first{};
    SW_REQUIRE_TRUE( atlas.allocate( 3, 2, first ) );
    sw::vector<sw::GlyphAtlasUpload> listUpload;
    atlas.takeUploads( listUpload );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listUpload.size() ) );
    SW_EXPECT_EQUAL( SW_TRUE, listUpload[0]._bWholePage );
    SW_EXPECT_EQUAL( static_cast<size_t>( sw::test::kTestGlyphAtlasLayout._pageSize ) * sw::test::kTestGlyphAtlasLayout._pageSize, listUpload[0]._bytes.size() );

    const uint8 arrFirstByte[6]  = { 1, 2, 3, 4, 5, 6 };
    const uint8 arrSecondByte[4] = { 9, 8, 7, 6 };
    atlas.write( first, arrFirstByte );
    sw::GlyphAtlasRect second{};
    SW_REQUIRE_TRUE( atlas.allocate( 2, 2, second ) );
    atlas.write( second, arrSecondByte );
    listUpload.clear();
    atlas.takeUploads( listUpload );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listUpload.size() ) );
    SW_EXPECT_EQUAL( SW_FALSE, listUpload[0]._bWholePage );
    SW_EXPECT_EQUAL( first._x, listUpload[0]._x );
    SW_EXPECT_EQUAL( first._y, listUpload[0]._y );
    SW_REQUIRE_EQUAL( 6u, static_cast<uint32>( listUpload[0]._bytes.size() ) );
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( listUpload[1]._bytes.size() ) );
    for ( uint32 index = 0; index < 6; ++index )
    {
        SW_EXPECT_EQUAL( arrFirstByte[index], listUpload[0]._bytes[index] );
    }
    for ( uint32 index = 0; index < 4; ++index )
    {
        SW_EXPECT_EQUAL( arrSecondByte[index], listUpload[1]._bytes[index] );
    }
    const uint8* pPage = atlas.getPageBytes( 0 );
    SW_EXPECT_EQUAL( static_cast<uint8>( 4 ), pPage[( static_cast<size_t>( first._y ) + 1 ) * sw::test::kTestGlyphAtlasLayout._pageSize + first._x] ); // 둘째 행 첫 바이트

    listUpload.clear();
    atlas.takeUploads( listUpload );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( listUpload.size() ) );
}

/** @brief [GlyphAtlasTest] 비울 후보는 이번 프레임에 안 쓴 페이지를 오래된 순으로 준다 — 비우면 세대가 오르고 전체 업로드가 기록되며, 모두 이번 프레임이면 후보가 없다 */
SW_TEST_CASE( GlyphAtlasTest, FullAtlasEvictsOldestUnusedPage )
{
    sw::GlyphAtlas atlas{ sw::GlyphFieldKind::Sdf, sw::test::kTestGlyphAtlasLayout };
    const uint32   size = GlyphAtlasTestUtil::kPageFillSize;
    for ( uint32 index = 0; index < sw::test::kTestGlyphAtlasLayout._maxPageCount; ++index )
    {
        sw::GlyphAtlasRect rect{};
        SW_REQUIRE_TRUE( atlas.allocate( size, size, rect ) );
        SW_EXPECT_EQUAL( index, static_cast<uint32>( rect._page ) );
        atlas.markPageUsed( rect._page, index + 1 ); // 페이지 0 이 프레임 1(가장 오래)
    }
    sw::GlyphAtlasRect overflow{};
    SW_EXPECT_FALSE( atlas.allocate( size, size, overflow ) );
    sw::vector<sw::GlyphAtlasUpload> listUpload;
    atlas.takeUploads( listUpload );
    listUpload.clear();

    const uint32       generationBefore = atlas.getGeneration();
    sw::vector<uint32> listPage;
    atlas.listEvictablePages( 8, listPage ); // 프레임 8 에 쓴 페이지 7 은 빠진다
    SW_REQUIRE_EQUAL( sw::test::kTestGlyphAtlasLayout._maxPageCount - 1, static_cast<uint32>( listPage.size() ) );
    for ( uint32 index = 0; index < static_cast<uint32>( listPage.size() ); ++index )
    {
        SW_EXPECT_EQUAL( index, listPage[index] ); // 오래된 순
    }
    atlas.clearPage( listPage[0] );
    SW_EXPECT_EQUAL( generationBefore + 1, atlas.getGeneration() );
    atlas.takeUploads( listUpload );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listUpload.size() ) );
    SW_EXPECT_EQUAL( SW_TRUE, listUpload[0]._bWholePage );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( listUpload[0]._page ) );
    SW_EXPECT_TRUE( atlas.allocate( size, size, overflow ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( overflow._page ) );

    for ( uint32 page = 0; page < atlas.getPageCount(); ++page )
    {
        atlas.markPageUsed( page, 20 );
    }
    atlas.markPageUsed( 0, 3 ); // 더 이른 프레임은 사용 표시를 되돌리지 않는다
    atlas.listEvictablePages( 20, listPage );
    SW_EXPECT_TRUE( listPage.empty() );
}

/** @brief [GlyphAtlasTest] 캐시가 페이지를 비우면 그 페이지에 있던 글리프는 다음 조회에서 다시 래스터화된다 — 공백은 사각형 없이 들고, 있는 글리프는 다시 베이크하지 않는다 */
SW_TEST_CASE( GlyphAtlasTest, GlyphCacheReRasterizesAfterEviction )
{
    sw::test::FakeFontRasterizer rasterizer;
    rasterizer.setSDFSize( GlyphAtlasTestUtil::kPageFillSize ); // 글리프 하나가 페이지 하나
    const sw::FontFaceID face = rasterizer.loadFace( sw::vector<uint8>( 1, static_cast<uint8>( 1 ) ), 0, "fake" );
    sw::GlyphCache       cache( rasterizer, sw::GlyphFieldKind::Sdf, sw::test::kTestGlyphAtlasLayout );

    for ( uint32 index = 0; index < sw::test::kTestGlyphAtlasLayout._maxPageCount; ++index )
    {
        const sw::CachedGlyph* pGlyph = cache.getOrCreateGlyph( face, 'A' + index, index + 1 )._pGlyph;
        SW_REQUIRE_NOT_NULL( pGlyph );
        SW_EXPECT_EQUAL( index, static_cast<uint32>( pGlyph->_rect._page ) );
    }
    SW_EXPECT_EQUAL( sw::test::kTestGlyphAtlasLayout._maxPageCount, rasterizer.getRasterizeCount() );
    SW_EXPECT_NOT_NULL( cache.getOrCreateGlyph( face, 'A' + 1, 9 )._pGlyph ); // 있는 글리프 — 다시 베이크하지 않고 페이지 1 을 프레임 9 에 썼다고 적는다
    SW_EXPECT_EQUAL( sw::test::kTestGlyphAtlasLayout._maxPageCount, rasterizer.getRasterizeCount() );

    // 아홉째 글리프 — 프레임 9 에 안 쓴 가장 오래된 페이지 0('A')을 비우고 그 자리에 들어간다.
    const sw::CachedGlyph* pNinth = cache.getOrCreateGlyph( face, 'Z', 9 )._pGlyph;
    SW_REQUIRE_NOT_NULL( pNinth );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( pNinth->_rect._page ) );
    SW_EXPECT_EQUAL( 1u, pNinth->_atlasGeneration );
    SW_EXPECT_EQUAL( sw::test::kTestGlyphAtlasLayout._maxPageCount, cache.getGlyphCount() ); // 'A' 는 표에서 빠졌다

    const uint32 rasterizedBefore = rasterizer.getRasterizeCount();
    SW_EXPECT_NOT_NULL( cache.getOrCreateGlyph( face, 'A', 10 )._pGlyph );
    SW_EXPECT_EQUAL( rasterizedBefore + 1, rasterizer.getRasterizeCount() );

    const sw::CachedGlyph* pSpace = cache.getOrCreateGlyph( face, ' ', 10 )._pGlyph;
    SW_REQUIRE_NOT_NULL( pSpace );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( pSpace->_rect._width ) );
    SW_EXPECT_TRUE( pSpace->_advancePx > 0.0f );
}

/**
 * @brief [GlyphAtlasTest] 프레임당 래스터화 시간 예산이 차면 새 글리프는 `Deferred` 로 미루고 이미 있는 글리프는 그대로 준다 — 프레임마다 하나는 나아가고 다음 프레임에 예산이 되살아난다
 * @details 예산 1 ns 는 첫 글리프가 반드시 넘기는 값이다(경과가 시계 해상도보다 짧아도 1 ns 로 센다). 0 은 제한 없음이다.
 */
SW_TEST_CASE( GlyphAtlasTest, GlyphCacheDefersNewGlyphsOverTheFrameBudget )
{
    sw::test::FakeFontRasterizer rasterizer;
    const sw::FontFaceID         face = rasterizer.loadFace( sw::vector<uint8>( 1, static_cast<uint8>( 1 ) ), 0, "fake" );
    sw::GlyphCache               cache( rasterizer, sw::GlyphFieldKind::Sdf, sw::test::kTestGlyphAtlasLayout );

    for ( uint32 index = 0; index < 4; ++index ) // 예산 0 = 제한 없음
    {
        SW_EXPECT_TRUE( cache.getOrCreateGlyph( face, 'A' + index, 1 )._status == sw::GlyphLookupStatus::Ready );
    }
    SW_EXPECT_EQUAL( 4u, rasterizer.getRasterizeCount() );

    cache.setRasterBudgetNanoseconds( 1 );
    const sw::GlyphLookup first = cache.getOrCreateGlyph( face, 'a', 2 ); // 프레임의 첫 새 글리프는 만든다
    SW_EXPECT_TRUE( first._status == sw::GlyphLookupStatus::Ready );
    SW_EXPECT_NOT_NULL( first._pGlyph );
    const sw::GlyphLookup second = cache.getOrCreateGlyph( face, 'b', 2 ); // 예산이 찼다
    SW_EXPECT_TRUE( second._status == sw::GlyphLookupStatus::Deferred );
    SW_EXPECT_NULL( second._pGlyph );
    SW_EXPECT_EQUAL( 5u, rasterizer.getRasterizeCount() );

    SW_EXPECT_TRUE( cache.getOrCreateGlyph( face, 'a', 2 )._status == sw::GlyphLookupStatus::Ready ); // 있는 글리프는 예산과 무관하다
    SW_EXPECT_TRUE( cache.getOrCreateGlyph( face, 'b', 3 )._status == sw::GlyphLookupStatus::Ready ); // 다음 프레임에 예산이 되살아나 미룬 글리프가 만들어진다
    SW_EXPECT_EQUAL( 6u, rasterizer.getRasterizeCount() );
}

/**
 * @brief [GlyphAtlasTest] 예산이 아무리 작아도 글리프 수만큼의 프레임 뒤에는 모든 글리프가 만들어져 있다 — 프레임 시계는 캐시가 쥐고 `beginFrame` 으로만 오른다
 * @details 예산 1 ns 는 프레임마다 새 글리프 하나만 만든다. 변이: `beginFrame` 이 시계를 올리지 않으면(프로파일러가 꺼진 때의 프레임 수처럼 늘 같은 번호) 첫 프레임 뒤 아무 글리프도 안 만들어진다.
 */
SW_TEST_CASE( GlyphAtlasTest, GlyphCacheFrameClockRevivesTheBudgetEveryFrame )
{
    sw::test::FakeFontRasterizer rasterizer;
    const sw::FontFaceID         face = rasterizer.loadFace( sw::vector<uint8>( 1, static_cast<uint8>( 1 ) ), 0, "fake" );
    sw::GlyphCache               cache( rasterizer, sw::GlyphFieldKind::Sdf, sw::test::kTestGlyphAtlasLayout );
    cache.setRasterBudgetNanoseconds( 1 );
    SW_EXPECT_EQUAL( uint64{ 0 }, cache.getFrameIndex() );

    constexpr uint32 kGlyphCount = 10;
    uint32           readyCount  = 0;
    for ( uint32 frame = 1; frame <= kGlyphCount; ++frame )
    {
        cache.beginFrame();
        SW_REQUIRE_EQUAL( static_cast<uint64>( frame ), cache.getFrameIndex() );
        readyCount = 0;
        for ( uint32 index = 0; index < kGlyphCount; ++index ) // 한 프레임이 모든 글리프를 순서대로 요청한다 — 앞쪽이 이미 있어도 뒤쪽이 굶지 않는다
        {
            if ( cache.getOrCreateGlyph( face, 'A' + index, cache.getFrameIndex() )._status == sw::GlyphLookupStatus::Ready )
                ++readyCount;
        }
        SW_EXPECT_EQUAL( frame, readyCount ); // 프레임마다 정확히 하나씩 나아간다
        SW_EXPECT_EQUAL( frame, rasterizer.getRasterizeCount() );
    }
    SW_EXPECT_EQUAL( kGlyphCount, readyCount ); // 다 만들어졌다
}

/** @brief [GlyphAtlasTest] MSDF 아틀라스는 텍셀당 4 바이트다 — 페이지 바이트가 네 배이고, 쓴 구간의 업로드는 행마다 너비 × 4 바이트로 나가며 텍셀당 바이트가 업로드에 실린다 */
SW_TEST_CASE( GlyphAtlasTest, MSDFAtlasStoresFourBytesPerTexel )
{
    sw::GlyphAtlas atlas{ sw::GlyphFieldKind::MSDF, sw::test::kTestGlyphAtlasLayout };
    SW_EXPECT_EQUAL( 4u, atlas.getBytesPerTexel() );
    sw::GlyphAtlasRect rect{};
    SW_REQUIRE_TRUE( atlas.allocate( 3, 2, rect ) );
    sw::vector<sw::GlyphAtlasUpload> listUpload;
    atlas.takeUploads( listUpload ); // 새 페이지 전체 한 건
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listUpload.size() ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( sw::test::kTestGlyphAtlasLayout._pageSize ) * sw::test::kTestGlyphAtlasLayout._pageSize * 4, listUpload[0]._bytes.size() );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( listUpload[0]._bytesPerTexel ) );
    listUpload.clear();

    sw::vector<uint8> bytes( static_cast<size_t>( rect._width ) * rect._height * 4 );
    for ( size_t index = 0; index < bytes.size(); ++index )
    {
        bytes[index] = static_cast<uint8>( index + 1 );
    }
    atlas.write( rect, bytes.data() );
    const uint8* pPage = atlas.getPageBytes( 0 );
    // 둘째 행 첫 텍셀은 쓴 바이트의 (너비 × 4)번째부터다.
    SW_EXPECT_EQUAL( bytes[static_cast<size_t>( rect._width ) * 4], pPage[( ( static_cast<size_t>( rect._y ) + 1 ) * sw::test::kTestGlyphAtlasLayout._pageSize + rect._x ) * 4] );
    atlas.takeUploads( listUpload );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listUpload.size() ) );
    SW_EXPECT_EQUAL( bytes.size(), listUpload[0]._bytes.size() );
    SW_EXPECT_TRUE( listUpload[0]._bytes == bytes );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( listUpload[0]._bytesPerTexel ) );
}

/** @brief [GlyphAtlasTest] MSDF 캐시는 래스터라이저의 MSDF 길로 글리프를 베이크한다 — 가짜의 MSDF 는 같은 크기 · 베어링에 RGBA 128 이라 아틀라스 자리에 네 배 바이트가 쓰인다 */
SW_TEST_CASE( GlyphAtlasTest, MSDFCacheUsesTheMSDFRasterizer )
{
    sw::test::FakeFontRasterizer rasterizer;
    const sw::FontFaceID         face = rasterizer.loadFace( sw::vector<uint8>( 1, static_cast<uint8>( 1 ) ), 0, "fake" );
    sw::GlyphCache               cache( rasterizer, sw::GlyphFieldKind::MSDF, sw::test::kTestGlyphAtlasLayout );
    SW_EXPECT_TRUE( cache.getFieldKind() == sw::GlyphFieldKind::MSDF );
    const sw::CachedGlyph* pGlyph = cache.getOrCreateGlyph( face, 'A', 1 )._pGlyph;
    SW_REQUIRE_NOT_NULL( pGlyph );
    const sw::GlyphAtlasRect& rect = pGlyph->_rect;
    SW_REQUIRE_TRUE( rect._width > 0 );
    const uint8* pPage = cache.getAtlas().getPageBytes( rect._page );
    const size_t first = ( static_cast<size_t>( rect._y ) * sw::test::kTestGlyphAtlasLayout._pageSize + rect._x ) * 4;
    for ( size_t channel = 0; channel < 4; ++channel )
    {
        SW_EXPECT_EQUAL( static_cast<uint8>( 128 ), pPage[first + channel] );
    }
}

/** @brief [GlyphAtlasTest] 페이지 모양은 넘긴 값(카탈로그)을 따른다 — 작은 한 변은 업로드 · UV 기준도 작고, 페이지 수 상한을 넘으면 자리를 못 잡는다 */
SW_TEST_CASE( GlyphAtlasTest, LayoutComesFromCatalogValues )
{
    const sw::GlyphAtlasLayout layout{ 256, 2, 4 };
    sw::GlyphAtlas             atlas{ sw::GlyphFieldKind::Sdf, layout };
    SW_EXPECT_EQUAL( 256u, atlas.getPageSize() );
    sw::GlyphAtlasRect rect{};
    SW_EXPECT_FALSE( atlas.allocate( 300, 10, rect ) ); // 한 변보다 크다
    SW_REQUIRE_TRUE( atlas.allocate( 200, 200, rect ) );
    SW_REQUIRE_TRUE( atlas.allocate( 200, 200, rect ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( rect._page ) );
    SW_EXPECT_FALSE( atlas.allocate( 200, 200, rect ) ); // 두 페이지가 상한
    sw::vector<sw::GlyphAtlasUpload> listUpload;
    atlas.takeUploads( listUpload );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listUpload.size() ) );
    SW_EXPECT_EQUAL( 256u, static_cast<uint32>( listUpload[0]._pageSize ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 256 ) * 256, listUpload[0]._bytes.size() );
}

/** @brief [GlyphAtlasTest] 카탈로그에 아틀라스 모양이 없거나 범위 밖이면 검사가 진다(기본값으로 대신하지 않는다) */
SW_TEST_CASE( GlyphAtlasTest, CatalogRejectsMissingAtlasLayout )
{
    sw::FontCatalogDef catalog = sw::test::FakeFontSubsystemFixture::makeCatalog( "Latin" );
    sw::test::FakeFontSubsystemFixture::addFamily( catalog, "Latin", "test/fonts/latin.ttf" );
    SW_EXPECT_TRUE( catalog.validate() );
    SW_EXPECT_LOG( "FontCatalog.GlyphAtlasLayoutOutOfRange" ); // 아래 세 거절마다 한 줄
    catalog._glyphAtlasPageSize = 0;
    SW_EXPECT_FALSE( catalog.validate() );
    catalog._glyphAtlasPageSize = sw::GlyphAtlasLayout::kMaxPageSize + 1;
    SW_EXPECT_FALSE( catalog.validate() );
    catalog._glyphAtlasPageSize     = 1024;
    catalog._glyphAtlasMaxPageCount = 0;
    SW_EXPECT_FALSE( catalog.validate() );
    catalog._glyphAtlasMaxPageCount = 8;
    catalog._glyphRetainFrameCount  = 0;
    SW_EXPECT_FALSE( catalog.validate() );
}

namespace
{
    /** @brief 한 페이지(64²)에 글리프 16 개(14² + 여백 = 16²)가 꼭 차는 캐시 장치입니다 — 비우기 · 압축 길을 글리프 단위로 본다. 프레임은 캐시 시계(`beginFrame`)로 센다. */
    struct GlyphRetainFixture
    {
        static constexpr uint32               kGlyphSize        = 14;
        static constexpr uint32               kGlyphsPerPage    = 16;
        static constexpr uint32               kRetainFrameCount = 2;
        static constexpr sw::GlyphAtlasLayout kLayout{ 64, 1, kRetainFrameCount };

        sw::test::FakeFontRasterizer _rasterizer;
        sw::FontFaceID               _face;
        sw::GlyphCache               _cache;

        GlyphRetainFixture()
            : _rasterizer{}
            , _face{ _rasterizer.loadFace( sw::vector<uint8>( 1, static_cast<uint8>( 1 ) ), 0, "fake" ) }
            , _cache{ _rasterizer, sw::GlyphFieldKind::Sdf, kLayout }
        {
            _rasterizer.setSDFSize( kGlyphSize );
        }

        /** @brief 캐시 시계를 @p frame 까지 올립니다(프레임마다 `beginFrame` — 압축은 여기서 돈다). */
        void advanceTo( uint64 frame )
        {
            while ( _cache.getFrameIndex() < frame )
            {
                _cache.beginFrame();
            }
        }

        /** @brief 지금 프레임에 글리프를 찾습니다. */
        sw::GlyphLookup find( uint32 glyphIndex ) { return _cache.getOrCreateGlyph( _face, glyphIndex, _cache.getFrameIndex() ); }

        /** @brief 지금 프레임에 찾아 사각형을, 없으면(미룸 · 실패) 빈 사각형을 돌려줍니다. */
        sw::GlyphAtlasRect findRect( uint32 glyphIndex )
        {
            const sw::CachedGlyph* pGlyph = find( glyphIndex )._pGlyph;
            return pGlyph != nullptr ? pGlyph->_rect : sw::GlyphAtlasRect{};
        }

        /** @brief 사각형 자리의 아틀라스 바이트가 모두 가짜 래스터 값(128)인지 봅니다(옮긴 바이트가 새 자리에 있는지). */
        bool hasRasterBytes( const sw::GlyphAtlasRect& rect ) const
        {
            const uint8* pPage = _cache.getAtlas().getPageBytes( rect._page );
            for ( uint32 row = 0; row < rect._height; ++row )
            {
                for ( uint32 column = 0; column < rect._width; ++column )
                {
                    if ( pPage[( static_cast<size_t>( rect._y ) + row ) * kLayout._pageSize + rect._x + column] != 128 )
                        return false;
                }
            }
            return true;
        }
    };
} // namespace

/**
 * @brief [GlyphAtlasTest] 아틀라스가 차면 새 글리프를 미루고 다음 프레임 첫머리에 압축한다 — 오래된 글리프만 버리고 최근 글리프는 바이트를 옮겨 살리며(다시 래스터화하지 않는다),
 *        옮긴 글리프의 사각형(UV)은 바이트가 있는 새 자리를 가리킨다
 * @details 앞 8 개는 프레임 1(오래됨), 뒤 8 개는 프레임 5(최근 2 프레임 안)에 쓰고 프레임 5 에 새 글리프를 찾는다. 변이: 페이지를 통째로 비우면 뒤 8 개 조회가 다시 래스터화한다.
 */
SW_TEST_CASE( GlyphAtlasTest, GlyphCacheEvictsOnlyStaleGlyphsAndRelocatesRecentOnes )
{
    GlyphRetainFixture fixture;
    const uint32       half = GlyphRetainFixture::kGlyphsPerPage / 2;
    fixture.advanceTo( 1 );
    for ( uint32 index = 0; index < half; ++index )
    {
        SW_REQUIRE_TRUE( fixture.findRect( 'A' + index )._width > 0 );
    }
    fixture.advanceTo( 5 );
    sw::vector<sw::GlyphAtlasRect> listBefore;
    for ( uint32 index = half; index < GlyphRetainFixture::kGlyphsPerPage; ++index )
    {
        listBefore.push_back( fixture.findRect( 'A' + index ) );
        SW_REQUIRE_TRUE( listBefore.back()._width > 0 );
    }
    SW_EXPECT_TRUE( fixture.find( 'a' )._status == sw::GlyphLookupStatus::Deferred ); // 페이지가 찼다 — 이번 프레임은 아무것도 옮기거나 버리지 않는다
    SW_EXPECT_EQUAL( GlyphRetainFixture::kGlyphsPerPage, fixture._cache.getGlyphCount() );
    const uint32 generationBefore = fixture._cache.getAtlas().getGeneration();

    fixture.advanceTo( 6 ); // 프레임 첫머리 압축
    SW_EXPECT_EQUAL( generationBefore + 1, fixture._cache.getAtlas().getGeneration() );
    SW_EXPECT_EQUAL( half, fixture._cache.getGlyphCount() ); // 오래된 8 개가 빠졌다
    const uint32             rasterizedBefore = fixture._rasterizer.getRasterizeCount();
    const sw::GlyphAtlasRect fresh            = fixture.findRect( 'a' );
    SW_REQUIRE_TRUE( fresh._width > 0 );
    SW_EXPECT_EQUAL( rasterizedBefore + 1, fixture._rasterizer.getRasterizeCount() );

    sw::vector<sw::GlyphAtlasRect> listAfter;
    bool                           bAnyMoved = false;
    for ( uint32 index = half; index < GlyphRetainFixture::kGlyphsPerPage; ++index )
    {
        const sw::GlyphAtlasRect rect = fixture.findRect( 'A' + index );
        SW_REQUIRE_TRUE( rect._width > 0 );
        SW_EXPECT_TRUE( fixture.hasRasterBytes( rect ) ); // UV 가 바이트가 있는 자리를 가리킨다
        SW_EXPECT_FALSE( GlyphAtlasTestUtil::overlaps( rect, fresh ) );
        for ( const sw::GlyphAtlasRect& other : listAfter )
        {
            SW_EXPECT_FALSE( GlyphAtlasTestUtil::overlaps( rect, other ) );
        }
        const sw::GlyphAtlasRect& before = listBefore[index - half];
        bAnyMoved                        = bAnyMoved || before._x != rect._x || before._y != rect._y;
        listAfter.push_back( rect );
    }
    SW_EXPECT_TRUE( bAnyMoved );                                                      // 비운 페이지 앞쪽으로 옮겨졌다
    SW_EXPECT_EQUAL( rasterizedBefore + 1, fixture._rasterizer.getRasterizeCount() ); // 살린 글리프는 다시 래스터화하지 않는다

    SW_EXPECT_TRUE( fixture.findRect( 'A' )._width > 0 ); // 버린 오래된 글리프는 다시 래스터화된다
    SW_EXPECT_EQUAL( rasterizedBefore + 2, fixture._rasterizer.getRasterizeCount() );
}

/**
 * @brief [GlyphAtlasTest] 이번 프레임 · 최근 프레임에 쓴 글리프는 버리지 않는다 — 그런 글리프로만 차면 새 글리프는 미뤄지고 압축은 한 번 경고만 하며,
 *        글리프가 모두 최근 창 밖으로 나간 페이지만 프레임 중간에 통째로 비운다
 */
SW_TEST_CASE( GlyphAtlasTest, GlyphCacheNeverEvictsGlyphsUsedThisFrame )
{
    GlyphRetainFixture fixture;
    fixture.advanceTo( 1 );
    for ( uint32 index = 0; index < GlyphRetainFixture::kGlyphsPerPage; ++index )
    {
        SW_REQUIRE_TRUE( fixture.findRect( 'A' + index )._width > 0 );
    }
    SW_EXPECT_TRUE( fixture.find( 'a' )._status == sw::GlyphLookupStatus::Deferred );
    SW_EXPECT_TRUE( fixture.find( 'b' )._status == sw::GlyphLookupStatus::Deferred ); // 압축 전까지 래스터화하지 않는다
    SW_EXPECT_EQUAL( GlyphRetainFixture::kGlyphsPerPage + 1, fixture._rasterizer.getRasterizeCount() );
    SW_EXPECT_EQUAL( GlyphRetainFixture::kGlyphsPerPage, fixture._cache.getGlyphCount() );

    SW_EXPECT_LOG( "GlyphCache.GlyphAtlasFullGlyphs" ); // 프레임 2 압축 — 모두 최근(1 프레임 전)이라 비울 것이 없다
    fixture.advanceTo( 2 );
    SW_EXPECT_EQUAL( GlyphRetainFixture::kGlyphsPerPage, fixture._cache.getGlyphCount() );
    const uint32 rasterizedBefore = fixture._rasterizer.getRasterizeCount();
    for ( uint32 index = 0; index < GlyphRetainFixture::kGlyphsPerPage; ++index )
    {
        SW_EXPECT_TRUE( fixture.findRect( 'A' + index )._width > 0 ); // 이번 프레임에도 쓴다
    }
    SW_EXPECT_EQUAL( rasterizedBefore, fixture._rasterizer.getRasterizeCount() );

    // 프레임 5 — 16 개 모두 프레임 2 뒤로 안 썼다(최근 2 프레임 밖). 그 페이지만 통째로 비우고 새 글리프가 들어간다.
    fixture.advanceTo( 5 );
    SW_EXPECT_TRUE( fixture.find( 'a' )._status == sw::GlyphLookupStatus::Ready );
    SW_EXPECT_EQUAL( 1u, fixture._cache.getGlyphCount() );
}

/**
 * @brief [GlyphAtlasTest] 한글 채팅 창 장면의 새 래스터화 수(`Text.GlyphsRasterized` 와 같은 값)와 이번 프레임에 못 그린 글자 수를 로그로 남깁니다 — 문턱은 두지 않는다
 * @details 장면: 서로 다른 한글 음절 3000 자 대화 기록을 600 자 창으로 보여 주며 프레임마다 40 자씩 내리고(먼저 그린다), 그 뒤에 고정 한글 패널 1200 자를 그린다.
 *          글리프는 실제 크기(48 px + 퍼짐 6 px 양쪽 = 60 px)라 엔진 카탈로그 모양(1024² × 8)에 2048 자만 든다 — 한 프레임의 1800 자는 들고 전체 4200 자는 안 든다.
 *          하한(`minimum`)은 처음 창 · 패널과 창에 새로 든 글자 수다. 페이지를 프레임 중간에 통째로 비우면 뒤에 그릴 패널 글리프가 섞인 페이지가 비워져
 *          매 프레임 다시 래스터화된다(감사 RN-14). 재는 법: Release `App --test --test-filter=GlyphAtlasTest.HangulScrollGlyphsRasterized`,
 *          로그 `GlyphAtlasTest.HangulScrollGlyphsRasterized` 의 `rasterized` 가 `minimum` 에 가까울수록, `missing`(미룬 · 못 그린 글자)이 작을수록 좋다.
 */
SW_TEST_CASE( GlyphAtlasTest, HangulScrollGlyphsRasterized, ._tags = "bench" )
{
    static constexpr uint32 kTextLength     = 3000;
    static constexpr uint32 kWindowLength   = 600;
    static constexpr uint32 kScrollPerFrame = 40;
    static constexpr uint32 kPanelLength    = 1200;
    static constexpr uint32 kFrameCount     = 300;
    static constexpr uint32 kHangulFirst    = 0xAC00; // '가'
    static constexpr uint32 kGlyphSizePx    = 60;

    sw::test::FakeFontRasterizer rasterizer;
    rasterizer.setSDFSize( kGlyphSizePx );
    const sw::FontFaceID face = rasterizer.loadFace( sw::vector<uint8>( 1, static_cast<uint8>( 1 ) ), 0, "fake" );
    sw::GlyphCache       cache( rasterizer, sw::GlyphFieldKind::Sdf, sw::test::kTestGlyphAtlasLayout );

    uint32 missingCount = 0;
    for ( uint32 frame = 0; frame < kFrameCount; ++frame )
    {
        cache.beginFrame();
        const uint32 windowStart = ( frame * kScrollPerFrame ) % kTextLength;
        for ( uint32 index = 0; index < kWindowLength + kPanelLength; ++index )
        {
            // 앞은 대화 창(기록 안에서 돈다), 뒤는 기록과 겹치지 않는 고정 패널 글자다.
            const uint32 glyphIndex = index < kWindowLength ? kHangulFirst + ( windowStart + index ) % kTextLength : kHangulFirst + kTextLength + ( index - kWindowLength );
            if ( cache.getOrCreateGlyph( face, glyphIndex, cache.getFrameIndex() )._status != sw::GlyphLookupStatus::Ready )
                ++missingCount;
        }
    }
    const uint32 minimumCount = kWindowLength + kPanelLength + kScrollPerFrame * ( kFrameCount - 1 );
    SW_EXPECT_TRUE( rasterizer.getRasterizeCount() >= kWindowLength + kPanelLength );
    SW_LOG_INFO_ID( "GlyphAtlasTest.HangulScrollGlyphsRasterized", "Hangul chat %# frames: rasterized %# (minimum %#), missing %#", kFrameCount, rasterizer.getRasterizeCount(),
                    minimumCount, missingCount );
}

/**
 * @brief [GlyphAtlasTest] 면 하나를 닫으면 그 면의 글리프만 표에서 빠진다 — 다른 면의 글리프는 남고, 닫은 면의 번호로 다시 물으면 새로 래스터화한다
 * @details 면 번호는 다시 쓰일 수 있다 — 닫은 면의 글리프가 표에 남으면 새 면이 옛 글리프 모양을 받는다.
 */
SW_TEST_CASE( GlyphAtlasTest, GlyphCacheRemoveFaceDropsOnlyThatFace )
{
    sw::test::FakeFontRasterizer rasterizer;
    const sw::FontFaceID         closedFace = rasterizer.loadFace( sw::vector<uint8>( 1, static_cast<uint8>( 1 ) ), 0, "closed" );
    const sw::FontFaceID         keptFace   = rasterizer.loadFace( sw::vector<uint8>( 1, static_cast<uint8>( 1 ) ), 0, "kept" );
    SW_REQUIRE_TRUE( closedFace != keptFace );
    sw::GlyphCache cache( rasterizer, sw::GlyphFieldKind::Sdf, sw::test::kTestGlyphAtlasLayout );

    SW_REQUIRE_NOT_NULL( cache.getOrCreateGlyph( closedFace, 'A', 1 )._pGlyph );
    SW_REQUIRE_NOT_NULL( cache.getOrCreateGlyph( closedFace, 'B', 1 )._pGlyph );
    SW_REQUIRE_NOT_NULL( cache.getOrCreateGlyph( keptFace, 'A', 1 )._pGlyph );
    SW_REQUIRE_EQUAL( 3u, cache.getGlyphCount() );

    cache.removeFace( closedFace );
    SW_EXPECT_EQUAL( 1u, cache.getGlyphCount() );

    const uint32 rasterizedBefore = rasterizer.getRasterizeCount();
    SW_EXPECT_NOT_NULL( cache.getOrCreateGlyph( keptFace, 'A', 2 )._pGlyph ); // 남은 면 — 다시 베이크하지 않는다
    SW_EXPECT_EQUAL( rasterizedBefore, rasterizer.getRasterizeCount() );
    SW_EXPECT_NOT_NULL( cache.getOrCreateGlyph( closedFace, 'A', 2 )._pGlyph ); // 닫은 면의 번호 — 새로 만든다
    SW_EXPECT_EQUAL( rasterizedBefore + 1, rasterizer.getRasterizeCount() );
}
