// CanvasGlyphAtlasTextureTest — 글리프 아틀라스 텍스처의 업로드 구간 합치기(순수 함수)와 놓친 묶음의 회복(거울만, nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Canvas/CanvasGlyphAtlasTexture.h"
#include "Engine/Text/GlyphAtlas.h"

/** @brief [CanvasGlyphAtlasTextureTest] 한 페이지의 구간 업로드가 한도를 넘으면 경계 상자 하나로 합치고, 한도 안이면 그대로 둔다(업로드 비용은 호출 수가 지배한다) */
SW_TEST_CASE( CanvasGlyphAtlasTextureTest, ManySmallUploadsMergeToBoundingRect )
{
    sw::vector<sw::GlyphAtlasRect> listRegion;
    for ( uint16 index = 0; index < 3; ++index )
    {
        listRegion.push_back( sw::GlyphAtlasRect{ 2, static_cast<uint16>( 10 + index * 20 ), 5, 8, 8 } );
    }
    sw::CanvasGlyphAtlasTexture::mergeUploadRegions( listRegion, sw::CanvasGlyphAtlasTexture::kMaxRegionUploadPerPage );
    SW_EXPECT_EQUAL( size_t{ 3 }, listRegion.size() );

    listRegion.clear();
    for ( uint16 index = 0; index <= sw::CanvasGlyphAtlasTexture::kMaxRegionUploadPerPage; ++index )
    {
        listRegion.push_back( sw::GlyphAtlasRect{ 2, static_cast<uint16>( 100 + index * 10 ), static_cast<uint16>( 40 + index ), 6, 9 } );
    }
    sw::CanvasGlyphAtlasTexture::mergeUploadRegions( listRegion, sw::CanvasGlyphAtlasTexture::kMaxRegionUploadPerPage );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRegion.size() );
    const uint16 lastIndex = static_cast<uint16>( sw::CanvasGlyphAtlasTexture::kMaxRegionUploadPerPage );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listRegion[0]._page ) );
    SW_EXPECT_EQUAL( 100u, static_cast<uint32>( listRegion[0]._x ) );
    SW_EXPECT_EQUAL( 40u, static_cast<uint32>( listRegion[0]._y ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( lastIndex * 10 + 6 ), static_cast<uint32>( listRegion[0]._width ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( lastIndex + 9 ), static_cast<uint32>( listRegion[0]._height ) );
}

/**
 * @brief [CanvasGlyphAtlasTextureTest] 업로드 묶음 하나가 렌더러에 닿지 않으면(그리지 않고 버린 패킷) 다음 묶음은 번호가 건너뛰어 버려지고, 아틀라스에 전체 업로드를 청해
 *        다음 묶음이 모든 페이지를 통째로 담아 거울이 다시 맞는다 — 첫 프레임에 만든 글리프가 HUD 에서 빈칸으로 남던 결함.
 */
SW_TEST_CASE( CanvasGlyphAtlasTextureTest, MissedBatchHealsWithFullUpload )
{
    sw::GlyphAtlas     atlas{ sw::GlyphFieldKind::MSDF };
    sw::GlyphAtlasRect rect{};
    SW_REQUIRE_TRUE( atlas.allocate( 4, 4, rect ) );
    sw::vector<sw::GlyphAtlasUpload> listDropped;
    atlas.takeUploads( listDropped ); // 묶음 1 — 새 페이지 전체. 이 패킷은 렌더러에 닿지 않는다
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listDropped.size() ) );
    SW_EXPECT_EQUAL( uint64{ 1 }, listDropped[0]._sequence );

    const sw::vector<uint8> bytes( static_cast<size_t>( rect._width ) * rect._height * 4, static_cast<uint8>( 200 ) );
    atlas.write( rect, bytes.data() );
    sw::vector<sw::GlyphAtlasUpload> listUpload;
    atlas.takeUploads( listUpload ); // 묶음 2 — 구간 하나
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listUpload.size() ) );
    SW_EXPECT_EQUAL( uint64{ 2 }, listUpload[0]._sequence );

    sw::CanvasGlyphAtlasTexture texture;
    texture.applyUploads( listUpload, &atlas );
    SW_EXPECT_TRUE( listUpload.empty() );
    SW_EXPECT_EQUAL( 0u, texture.getPageCount() ); // 묶음 1 을 놓쳤으므로 구간을 빈 페이지에 얹지 않는다

    atlas.takeUploads( listUpload ); // 청한 전체 묶음
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listUpload.size() ) );
    SW_EXPECT_TRUE( listUpload[0]._bFullUpload == SW_TRUE && listUpload[0]._bWholePage == SW_TRUE );
    SW_EXPECT_EQUAL( bytes[0], listUpload[0]._bytes[( static_cast<size_t>( rect._y ) * sw::GlyphAtlas::kPageSize + rect._x ) * 4] );
    texture.applyUploads( listUpload, &atlas );
    SW_EXPECT_EQUAL( 1u, texture.getPageCount() );

    atlas.takeUploads( listUpload );
    SW_EXPECT_TRUE( listUpload.empty() ); // 바뀐 것이 없으면 묶음도 번호도 없다
}
