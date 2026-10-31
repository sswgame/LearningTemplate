// TileSetTest — 타일셋(.tileset.xml): 규칙 타일의 이웃 해석 · 애니메이션 타일 · 맵의 타일 레이어 왕복 · 읽기 오류. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/String/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/TileMap/TileMapXML.h"
#include "Engine/TileMap/TileSetAsset.h"

namespace
{
    /**
     * @brief 4 × 4 아틀라스 타일셋 — 땅(규칙 타일: 외톨이 0, 위가 비면 1, 기본 5), 물(애니메이션 12..15, 4 fps), 풀(그냥 타일 3).
     * @details 규칙은 앞에서부터 맞는다 — 외톨이가 "위가 빔" 보다 먼저라야 위도 빈 외톨이 칸이 외톨이 모습을 받는다.
     */
    constexpr const utf8* kTileSetXML = "<TileSet atlas=\"engine/textures/test/quadrants.dds\" columns=\"4\" rows=\"4\" tileSize=\"1\">"
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
    SW_REQUIRE_TRUE( tileSet.loadFromXMLText( kTileSetXML, "<test>" ) );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( tileSet.getBrushes().size() ) );
    const uint16 ground = static_cast<uint16>( tileSet.findBrush( sw::hashed_string( "ground" ) ) + 1 );
    const uint16 water  = static_cast<uint16>( tileSet.findBrush( sw::hashed_string( "water" ) ) + 1 );
    SW_REQUIRE_TRUE( ground > 0 && water > 0 );

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
    SW_EXPECT_NEAR( 0.5f, uv._x, 1e-6 );
    SW_EXPECT_NEAR( 0.25f, uv._y, 1e-6 );
    SW_EXPECT_NEAR( 0.25f, uv._z, 1e-6 );
}

/**
 * @brief [TileSetTest] 맵의 타일 레이어는 브러시 이름(팔레트)으로 왕복하고, 타일셋의 순서가 바뀌어도 같은 브러시를 가리키며, 타일 레이어가 없는 맵은 예전 바이트 그대로다
 */
SW_TEST_CASE( TileSetTest, TileLayerRoundTripsByBrushName )
{
    sw::TileMapXMLData map;
    SW_REQUIRE_TRUE( map.resetTiles( 3, 2 ) );
    const sw::string withoutLayer = map.toXML();
    SW_EXPECT_TRUE( withoutLayer.find( "tileLayer" ) == sw::string::npos );

    map._tileSetPath = "engine/tilesets/test.tileset.xml";
    SW_EXPECT_TRUE( map.setTileBrush( 0, 1, "ground" ) );
    SW_EXPECT_TRUE( map.setTileBrush( 2, 1, "water" ) );
    SW_EXPECT_TRUE( map.setTileBrush( 1, 1, "ground" ) );
    SW_EXPECT_FALSE( map.setTileBrush( 3, 0, "ground" ) ); // 맵 밖
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( map._listPaletteName.size() ) );

    sw::TileMapXMLData reread;
    SW_REQUIRE_TRUE( reread.loadFromXML( map.toXML() ) );
    SW_EXPECT_EQUAL( sw::string( "engine/tilesets/test.tileset.xml" ), reread._tileSetPath );
    SW_EXPECT_TRUE( reread.getTileBrushName( 0, 1 ) == "ground" );
    SW_EXPECT_TRUE( reread.getTileBrushName( 2, 1 ) == "water" );
    SW_EXPECT_TRUE( reread.getTileBrushName( 0, 0 ).empty() );

    // 타일셋 브러시 번호로 옮긴다 — 맵의 팔레트 순서(ground, water)와 타일셋 순서(ground, water, grass)가 달라도 이름이 같으면 같다.
    sw::TileSetAsset tileSet;
    SW_REQUIRE_TRUE( tileSet.loadFromXMLText( kTileSetXML, "<test>" ) );
    sw::vector<uint16> listBrushIndex;
    SW_EXPECT_TRUE( reread.mapTileCells( tileSet, listBrushIndex ) );
    SW_REQUIRE_EQUAL( 6u, static_cast<uint32>( listBrushIndex.size() ) );
    SW_EXPECT_EQUAL( static_cast<uint16>( tileSet.findBrush( sw::hashed_string( "water" ) ) + 1 ), listBrushIndex[5] );

    // 타일셋에 없는 이름은 데이터 오류다(그 칸은 빈다).
    SW_EXPECT_TRUE( reread.setTileBrush( 0, 0, "lava" ) );
    SW_EXPECT_FALSE( reread.mapTileCells( tileSet, listBrushIndex ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( listBrushIndex[0] ) );
}

namespace
{
    /** @brief 틀린 타일셋 한 줄과 그 읽기가 남길 로그 id 입니다. */
    struct BadTileSet
    {
        const utf8* _pXML;
        const utf8* _pExpectedID;
    };

    const BadTileSet kArrBadTileSet[] = {
        {                                                        "<TileSet columns=\"2\" rows=\"2\"><Tile name=\"a\" cell=\"4\"/></TileSet>",      "TileSet.Invalid"}, // 아틀라스 밖 칸
        {                           "<TileSet columns=\"2\" rows=\"2\"><Tile name=\"a\" cell=\"0\"/><Tile name=\"a\" cell=\"1\"/></TileSet>",      "TileSet.Invalid"}, // 같은 이름 둘
        {                                          "<TileSet columns=\"2\" rows=\"2\"><Tile name=\"a\" cell=\"0\" color=\"red\"/></TileSet>", "Xml.UnknownAttribute"}, // 모르는 속성
        {                                                      "<TileSet columns=\"2\" rows=\"2\"><Sprite name=\"a\" cell=\"0\"/></TileSet>",      "TileSet.Invalid"}, // 모르는 요소
        {         "<TileSet columns=\"2\" rows=\"2\"><RuleTile name=\"a\" cell=\"0\"><Rule pattern=\"oo\" cell=\"1\"/></RuleTile></TileSet>",      "TileSet.Invalid"}, // 아홉 글자가 아닌 패턴
        {"<TileSet columns=\"2\" rows=\"2\"><RuleTile name=\"a\" cell=\"0\"><Rule pattern=\"... .z. ...\" cell=\"1\"/></RuleTile></TileSet>",      "TileSet.Invalid"}, // 모르는 패턴 글자
        {                                                    "<TileSet columns=\"2\" rows=\"2\"><RuleTile name=\"a\" cell=\"0\"/></TileSet>",      "TileSet.Invalid"}, // 규칙 없는 규칙 타일
        {                                                    "<TileSet columns=\"2\" rows=\"2\"><Tile name=\"a\" frames=\"0 1\"/></TileSet>",      "TileSet.Invalid"}, // fps 없는 애니메이션
        {                                                                   "<TileSet columns=\"2\" rows=\"2\"><Tile name=\"a\"/></TileSet>",      "TileSet.Invalid"}, // 모습 없음
    };

    const utf8* const kArrBadTileLayer[] = {
        "<TileMap><width>2</width><height>1</height><tileLayer tileSet=\"t\"><palette><b name=\"a\"/></palette><cells>1</cells></tileLayer></TileMap>",   // 칸 수가 맞지 않다
        "<TileMap><width>2</width><height>1</height><tileLayer tileSet=\"t\"><palette><b name=\"a\"/></palette><cells>1 2</cells></tileLayer></TileMap>", // 팔레트 밖 번호
        "<TileMap><width>2</width><height>1</height><tileLayer tileSet=\"t\"><palette/><cells>0 a</cells></tileLayer></TileMap>",                         // 숫자가 아니다
    };
} // namespace

/**
 * @brief [TileSetTest] 틀린 타일셋은 읽기 오류다 — 모르는 요소 · 속성, 같은 이름 둘, 아틀라스 밖 칸, 아홉 글자가 아닌 패턴, 규칙 없는 규칙 타일, fps 없는 애니메이션
 */
SW_TEST_CASE_EACH( TileSetTest, MalformedTileSetIsALoadError, kArrBadTileSet )
{
    SW_EXPECT_LOG( param._pExpectedID );
    sw::TileSetAsset tileSet;
    SW_EXPECT_FALSE_MSG( tileSet.loadFromXMLText( param._pXML, "<bad>" ), param._pXML );
}

/**
 * @brief [TileSetTest] 칸 수가 맞지 않는 타일 레이어 · 팔레트 밖 번호 · 숫자가 아닌 칸은 맵째 읽기 오류다
 */
SW_TEST_CASE_EACH( TileSetTest, MalformedTileLayerIsALoadError, kArrBadTileLayer )
{
    SW_EXPECT_LOG( "TileMap.InvalidTileLayer" );
    sw::TileMapXMLData map;
    SW_EXPECT_FALSE_MSG( map.loadFromXML( param ), param );
}
