#include "pch.h"

#include "Engine/Utility/TileMap/TileSetAsset.h"
#include "Engine/Utility/Xml/TileMapXml.h"

#include "TestFramework/TestFramework.h"

// TileSetTest — 타일셋(.tileset.xml): 규칙 타일의 이웃 해석 · 애니메이션 타일 · 맵의 타일 레이어 왕복 · 읽기 오류. 디바이스 없음(nogpu).

namespace
{
    /**
     * @brief 4 × 4 아틀라스 타일셋 — 땅(규칙 타일: 외톨이 0, 위가 비면 1, 기본 5), 물(애니메이션 12..15, 4 fps), 풀(그냥 타일 3).
     * @details 규칙은 앞에서부터 맞는다 — 외톨이가 "위가 빔" 보다 먼저라야 위도 빈 외톨이 칸이 외톨이 모습을 받는다.
     */
    constexpr const utf8* kTileSetXml = "<TileSet atlas=\"engine/textures/test/quadrants.dds\" columns=\"4\" rows=\"4\" tileSize=\"1\">"
                                        "  <RuleTile name=\"ground\" cell=\"5\" solid=\"true\">"
                                        "    <Rule pattern=\".x. x.x .x.\" cell=\"0\"/>"
                                        "    <Rule pattern=\".x. ... ...\" cell=\"1\"/>"
                                        "  </RuleTile>"
                                        "  <Tile name=\"water\" frames=\"12 13 14 15\" fps=\"4\" navCost=\"40\"/>"
                                        "  <Tile name=\"grass\" cell=\"3\" navCost=\"5\"/>"
                                        "</TileSet>";

    /** @brief 칸 하나의 모습이 보이는 아틀라스 칸입니다(빈 칸은 -1). */
    int32 displayedCell( const sw::TileSetAsset& tileSet, const sw::vector<uint16>& listBrushIndex, int32 width, int32 height, int32 x, int32 y,
                         float32 seconds )
    {
        const sw::TileVisual* pVisual = tileSet.resolveVisual( listBrushIndex, width, height, x, y );
        return ( pVisual != nullptr ) ? pVisual->computeCellAt( seconds ) : -1;
    }
} // namespace

/**
 * @brief [TileSetTest] 규칙 타일은 이웃에 따라 첫 번째로 맞는 규칙의 모습을 고르고, 이웃을 칠하면 모습이 따라 바뀐다
 * @details 유니티 RuleTile 의 This/NotThis · Godot terrain 과 같다. 맵(4 × 3):
 *          행 0 `. . . .` / 행 1 `G G G .` / 행 2 `G G G W`. (1, 1) 은 위가 비어 "위가 빔"(1), (1, 2) 는 위가 땅이라 기본(5). (1, 0) 에 땅을 칠하면
 *          (1, 1) 이 기본(5)이 되고 새 칸은 왼 · 오 · 위가 비고 아래만 땅이라 외톨이 규칙(위 · 왼 · 오 · 아래가 모두 비어야 함)이 안 맞아 "위가 빔"(1)이다.
 */
SW_TEST_CASE( TileSetTest, RuleTilesResolveByNeighborsAndFollowPainting )
{
    sw::TileSetAsset tileSet;
    SW_ASSERT_TRUE( tileSet.loadFromXmlText( kTileSetXml, "<test>" ) );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( tileSet.getBrushes().size() ) );
    const uint16 ground = static_cast<uint16>( tileSet.findBrush( sw::hashed_string( "ground" ) ) + 1 );
    const uint16 water  = static_cast<uint16>( tileSet.findBrush( sw::hashed_string( "water" ) ) + 1 );
    SW_ASSERT_TRUE( ground > 0 && water > 0 );

    constexpr int32    kWidth  = 4;
    constexpr int32    kHeight = 3;
    sw::vector<uint16> cells   = { 0, 0, 0, 0, ground, ground, ground, 0, ground, ground, ground, water };
    SW_EXPECT_EQUAL( -1, displayedCell( tileSet, cells, kWidth, kHeight, 0, 0, 0.0f ) );
    SW_EXPECT_EQUAL( 1, displayedCell( tileSet, cells, kWidth, kHeight, 1, 1, 0.0f ) );
    SW_EXPECT_EQUAL( 5, displayedCell( tileSet, cells, kWidth, kHeight, 1, 2, 0.0f ) );

    cells[1] = ground; // (1, 0)
    SW_EXPECT_EQUAL( 5, displayedCell( tileSet, cells, kWidth, kHeight, 1, 1, 0.0f ) );
    SW_EXPECT_EQUAL( 1, displayedCell( tileSet, cells, kWidth, kHeight, 1, 0, 0.0f ) );

    // 사방이 빈 땅 한 칸은 외톨이(0)다 — 앞 규칙이 먼저 맞는다.
    sw::vector<uint16> lonely = { 0, 0, 0, 0, ground, 0, 0, 0, 0 };
    SW_EXPECT_EQUAL( 0, displayedCell( tileSet, lonely, 3, 3, 1, 1, 0.0f ) );

    // 애니메이션 타일: 4 fps 로 12 → 13 → … 이고 한 바퀴(1 초) 뒤 다시 12.
    SW_EXPECT_EQUAL( 12, displayedCell( tileSet, cells, kWidth, kHeight, 3, 2, 0.0f ) );
    SW_EXPECT_EQUAL( 13, displayedCell( tileSet, cells, kWidth, kHeight, 3, 2, 0.26f ) );
    SW_EXPECT_EQUAL( 15, displayedCell( tileSet, cells, kWidth, kHeight, 3, 2, 0.76f ) );
    SW_EXPECT_EQUAL( 12, displayedCell( tileSet, cells, kWidth, kHeight, 3, 2, 1.01f ) );

    // UV 사각형은 4 × 4 아틀라스의 칸이다(행 우선).
    const sw::float4 uv = tileSet.computeCellUvRect( 6 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, uv._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, uv._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, uv._z, 1e-6f );
}

/**
 * @brief [TileSetTest] 맵의 타일 레이어는 브러시 이름(팔레트)으로 왕복하고, 타일셋의 순서가 바뀌어도 같은 브러시를 가리키며, 타일 레이어가 없는 맵은 예전 바이트 그대로다
 */
SW_TEST_CASE( TileSetTest, TileLayerRoundTripsByBrushName )
{
    sw::TileMapXmlData map;
    SW_ASSERT_TRUE( map.resetTiles( 3, 2 ) );
    const sw::string withoutLayer = map.toXml();
    SW_EXPECT_TRUE( withoutLayer.find( "tileLayer" ) == sw::string::npos );

    map._tileSetPath = "engine/tilesets/test.tileset.xml";
    SW_EXPECT_TRUE( map.setTileBrush( 0, 1, "ground" ) );
    SW_EXPECT_TRUE( map.setTileBrush( 2, 1, "water" ) );
    SW_EXPECT_TRUE( map.setTileBrush( 1, 1, "ground" ) );
    SW_EXPECT_FALSE( map.setTileBrush( 3, 0, "ground" ) ); // 맵 밖
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( map._listPaletteName.size() ) );

    sw::TileMapXmlData reread;
    SW_ASSERT_TRUE( reread.loadFromXml( map.toXml() ) );
    SW_EXPECT_STREQ( "engine/tilesets/test.tileset.xml", reread._tileSetPath.c_str() );
    SW_EXPECT_TRUE( reread.getTileBrushName( 0, 1 ) == "ground" );
    SW_EXPECT_TRUE( reread.getTileBrushName( 2, 1 ) == "water" );
    SW_EXPECT_TRUE( reread.getTileBrushName( 0, 0 ).empty() );

    // 타일셋 브러시 번호로 옮긴다 — 맵의 팔레트 순서(ground, water)와 타일셋 순서(ground, water, grass)가 달라도 이름이 같으면 같다.
    sw::TileSetAsset tileSet;
    SW_ASSERT_TRUE( tileSet.loadFromXmlText( kTileSetXml, "<test>" ) );
    sw::vector<uint16> listBrushIndex;
    SW_EXPECT_TRUE( reread.mapTileCells( tileSet, listBrushIndex ) );
    SW_ASSERT_EQUAL( 6u, static_cast<uint32>( listBrushIndex.size() ) );
    SW_EXPECT_EQUAL( static_cast<uint16>( tileSet.findBrush( sw::hashed_string( "water" ) ) + 1 ), listBrushIndex[5] );

    // 타일셋에 없는 이름은 데이터 오류다(그 칸은 빈다).
    SW_TEST_DEFENSIVE_SCOPE( "a palette name the tile set does not have" );
    SW_EXPECT_TRUE( reread.setTileBrush( 0, 0, "lava" ) );
    SW_EXPECT_FALSE( reread.mapTileCells( tileSet, listBrushIndex ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( listBrushIndex[0] ) );
}

/**
 * @brief [TileSetTest] 틀린 타일셋 · 타일 레이어는 읽기 오류다 — 모르는 요소 · 속성, 같은 이름 둘, 아틀라스 밖 칸, 아홉 글자가 아닌 패턴, 규칙 없는 규칙 타일,
 *        fps 없는 애니메이션, 칸 수가 맞지 않는 레이어 · 팔레트 밖 번호
 */
SW_TEST_CASE( TileSetTest, MalformedDataIsALoadError )
{
    SW_TEST_DEFENSIVE_SCOPE( "each malformed tile set logs its load error" );
    const utf8* arrBadTileSet[] = {
        "<TileSet columns=\"2\" rows=\"2\"><Tile name=\"a\" cell=\"4\"/></TileSet>",
        "<TileSet columns=\"2\" rows=\"2\"><Tile name=\"a\" cell=\"0\"/><Tile name=\"a\" cell=\"1\"/></TileSet>",
        "<TileSet columns=\"2\" rows=\"2\"><Tile name=\"a\" cell=\"0\" color=\"red\"/></TileSet>",
        "<TileSet columns=\"2\" rows=\"2\"><Sprite name=\"a\" cell=\"0\"/></TileSet>",
        "<TileSet columns=\"2\" rows=\"2\"><RuleTile name=\"a\" cell=\"0\"><Rule pattern=\"oo\" cell=\"1\"/></RuleTile></TileSet>",
        "<TileSet columns=\"2\" rows=\"2\"><RuleTile name=\"a\" cell=\"0\"><Rule pattern=\"... .z. ...\" cell=\"1\"/></RuleTile></TileSet>",
        "<TileSet columns=\"2\" rows=\"2\"><RuleTile name=\"a\" cell=\"0\"/></TileSet>",
        "<TileSet columns=\"2\" rows=\"2\"><Tile name=\"a\" frames=\"0 1\"/></TileSet>",
        "<TileSet columns=\"2\" rows=\"2\"><Tile name=\"a\"/></TileSet>",
    };
    for ( const utf8* pXml : arrBadTileSet )
    {
        sw::TileSetAsset tileSet;
        SW_EXPECT_FALSE_MSG( tileSet.loadFromXmlText( pXml, "<bad>" ), pXml );
    }

    const utf8* arrBadMap[] = {
        "<TileMap><width>2</width><height>1</height><tileLayer tileSet=\"t\"><palette><b name=\"a\"/></palette><cells>1</cells></tileLayer></TileMap>",
        "<TileMap><width>2</width><height>1</height><tileLayer tileSet=\"t\"><palette><b name=\"a\"/></palette><cells>1 2</cells></tileLayer></TileMap>",
        "<TileMap><width>2</width><height>1</height><tileLayer tileSet=\"t\"><palette/><cells>0 a</cells></tileLayer></TileMap>",
    };
    for ( const utf8* pXml : arrBadMap )
    {
        sw::TileMapXmlData map;
        SW_EXPECT_FALSE_MSG( map.loadFromXml( pXml ), pXml );
    }
}
