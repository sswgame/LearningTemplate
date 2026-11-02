// CanvasGlyphAtlasTextureTest — 글리프 아틀라스 텍스처의 업로드 구간 합치기(순수 함수, nogpu).
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
