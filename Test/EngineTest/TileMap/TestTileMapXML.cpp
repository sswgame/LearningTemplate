#include "pch.h"

#include "Core/Container/string.h"

#include "Engine/TileMap/TileMapXML.h"

#include "TestFramework/TestFramework.h"

// TileMapXMLTest — 타일맵 XML 스키마의 왕복·기본값·방어. 파일을 만들지 않는다(문자열만 오간다).
//
// 이 스키마는 엔진이 소유하고 에디터(TileMapPanel · EditorToolAssetCommands)와 GameFramework 의
// Overworld 킷이 같이 읽고 쓴다. 한쪽이 쓴 것을 다른 쪽이 못 읽으면 증상은 "맵이 8×8 빈 칸으로
// 열린다" 라 데이터가 아니라 도구를 의심하게 된다.

namespace
{
    /** @brief 칸마다 값이 다른 3×2 맵을 만든다 — 왕복에서 자리가 밀리면 값이 어긋난다. */
    sw::TileMapXMLData makeSampleMap()
    {
        sw::TileMapXMLData data;
        data._name      = "SampleTown";
        data._scenePath = "game/empty/maps/sample.scene.xml";
        data._role      = "town";
        data._width     = 3;
        data._height    = 2;
        data._spawnX    = 2;
        data._spawnY    = 1;

        const size_t count = 6;
        data.getFlagLayer( sw::TileFlagLayer::Walkable ).assign( count, 1 );
        data.getFlagLayer( sw::TileFlagLayer::Encounter ).assign( count, 0 );
        data.getFlagLayer( sw::TileFlagLayer::PassThrough ).assign( count, 0 );
        data._listVisual.assign( count, sw::TileMapXMLData::Visual{} );

        for ( size_t tileIndex = 0; tileIndex < count; ++tileIndex )
        {
            data.getFlagLayer( sw::TileFlagLayer::Walkable )[tileIndex]    = ( tileIndex % 2 == 0 ) ? 1 : 0;
            data.getFlagLayer( sw::TileFlagLayer::Encounter )[tileIndex]   = ( tileIndex % 3 == 0 ) ? 1 : 0;
            data.getFlagLayer( sw::TileFlagLayer::PassThrough )[tileIndex] = ( tileIndex == 4 ) ? 1 : 0;

            sw::TileMapXMLData::Visual tileVisual;
            tileVisual._height          = static_cast<uint8>( tileIndex );
            tileVisual._tintR           = static_cast<uint8>( 10 + tileIndex );
            tileVisual._tintG           = static_cast<uint8>( 20 + tileIndex );
            tileVisual._tintB           = static_cast<uint8>( 30 + tileIndex );
            tileVisual._atlasId         = static_cast<uint8>( tileIndex % 4 );
            data._listVisual[tileIndex] = tileVisual;
        }

        sw::TileMapXMLData::Warp warp;
        warp._tileX       = 2;
        warp._tileY       = 0;
        warp._targetMap   = "game/empty/maps/cave.tilemap.xml";
        warp._targetTileX = 5;
        warp._targetTileY = 7;
        warp._pairId      = "cave-entrance";
        data._listWarp.push_back( warp );

        return data;
    }

    /** @brief 두 맵의 필드가 모두 같은가 (직렬화되지 않는 _sourcePath 는 제외). */
    void expectSameMap( const sw::TileMapXMLData& expected, const sw::TileMapXMLData& actual )
    {
        SW_EXPECT_TRUE( expected._name == actual._name );
        SW_EXPECT_TRUE( expected._scenePath == actual._scenePath );
        SW_EXPECT_TRUE( expected._role == actual._role );
        SW_EXPECT_EQUAL( expected._width, actual._width );
        SW_EXPECT_EQUAL( expected._height, actual._height );
        SW_EXPECT_EQUAL( expected._spawnX, actual._spawnX );
        SW_EXPECT_EQUAL( expected._spawnY, actual._spawnY );

        for ( const sw::TileFlagLayerInfo& info : sw::kArrTileFlagLayerInfo )
        {
            const sw::vector<uint8>& listExpected = expected.getFlagLayer( info._layer );
            const sw::vector<uint8>& listActual   = actual.getFlagLayer( info._layer );
            SW_ASSERT_EQUAL( listExpected.size(), listActual.size() );
            for ( size_t tileIndex = 0; tileIndex < listExpected.size(); ++tileIndex )
            {
                SW_EXPECT_EQUAL( uint32( listExpected[tileIndex] ), uint32( listActual[tileIndex] ) );
            }
        }

        SW_ASSERT_EQUAL( expected._listVisual.size(), actual._listVisual.size() );
        for ( size_t tileIndex = 0; tileIndex < expected._listVisual.size(); ++tileIndex )
        {
            SW_EXPECT_EQUAL( uint32( expected._listVisual[tileIndex]._height ), uint32( actual._listVisual[tileIndex]._height ) );
            SW_EXPECT_EQUAL( uint32( expected._listVisual[tileIndex]._tintR ), uint32( actual._listVisual[tileIndex]._tintR ) );
            SW_EXPECT_EQUAL( uint32( expected._listVisual[tileIndex]._tintG ), uint32( actual._listVisual[tileIndex]._tintG ) );
            SW_EXPECT_EQUAL( uint32( expected._listVisual[tileIndex]._tintB ), uint32( actual._listVisual[tileIndex]._tintB ) );
            SW_EXPECT_EQUAL( uint32( expected._listVisual[tileIndex]._atlasId ), uint32( actual._listVisual[tileIndex]._atlasId ) );
        }

        SW_ASSERT_EQUAL( expected._listWarp.size(), actual._listWarp.size() );
        for ( size_t warpIndex = 0; warpIndex < expected._listWarp.size(); ++warpIndex )
        {
            SW_EXPECT_EQUAL( expected._listWarp[warpIndex]._tileX, actual._listWarp[warpIndex]._tileX );
            SW_EXPECT_EQUAL( expected._listWarp[warpIndex]._tileY, actual._listWarp[warpIndex]._tileY );
            SW_EXPECT_TRUE( expected._listWarp[warpIndex]._targetMap == actual._listWarp[warpIndex]._targetMap );
            SW_EXPECT_EQUAL( expected._listWarp[warpIndex]._targetTileX, actual._listWarp[warpIndex]._targetTileX );
            SW_EXPECT_EQUAL( expected._listWarp[warpIndex]._targetTileY, actual._listWarp[warpIndex]._targetTileY );
            SW_EXPECT_TRUE( expected._listWarp[warpIndex]._pairId == actual._listWarp[warpIndex]._pairId );
        }
    }
} // namespace

/**
 * @brief [TileMapXMLTest] 쓴 것을 그대로 읽는다 — 칸마다 다른 값으로
 * @details 칸 값이 전부 같으면 자리가 한 칸 밀려도 통과한다. 그래서 칸마다 다른 값을 넣는다.
 */
SW_TEST_CASE( TileMapXMLTest, RoundTripKeepsEveryField )
{
    const sw::TileMapXMLData source = makeSampleMap();
    const sw::string         xml    = source.toXML();
    SW_ASSERT_TRUE( xml.empty() == false );

    sw::TileMapXMLData loaded;
    SW_ASSERT_TRUE( loaded.loadFromXML( xml ) );
    expectSameMap( source, loaded );
}

/**
 * @brief [TileMapXMLTest] 같은 맵을 두 번 쓰면 같은 바이트가 나오고, 다시 읽어도 같다
 * @details 저장할 때마다 바이트가 달라지면 버전 관리가 매번 diff 를 만들고, 그 diff 를 보고
 *          "누가 맵을 건드렸다" 고 읽게 된다.
 */
SW_TEST_CASE( TileMapXMLTest, SavingTwiceProducesTheSameBytes )
{
    const sw::TileMapXMLData source = makeSampleMap();
    const sw::string         first  = source.toXML();
    const sw::string         second = source.toXML();
    SW_EXPECT_TRUE_MSG( first == second, "같은 맵을 두 번 썼는데 바이트가 달라졌다" );

    sw::TileMapXMLData loaded;
    SW_ASSERT_TRUE( loaded.loadFromXML( first ) );
    const sw::string reSaved = loaded.toXML();
    SW_EXPECT_TRUE_MSG( first == reSaved, "읽고 다시 쓰면 바이트가 달라졌다 — 왕복이 고정점이 아니다" );
}

/**
 * @brief [TileMapXMLTest] 크기만 있고 칸 배열이 모자란 맵을 써도 배열 밖을 읽지 않는다
 * @details 이 구조체는 필드가 전부 공개라 `_width`·`_height` 만 바꾸고 네 배열을 안 늘린 채
 *          저장할 수 있다. 그때 `toXML` 이 `_width × _height` 만 믿으면 남의 메모리를 읽어
 *          파일에 적는다(Debug 에서는 vector 단언이 먼저 터진다).
 */
SW_TEST_CASE( TileMapXMLTest, SavingWithShortTileArraysStaysInsideTheArrays )
{
    sw::TileMapXMLData data;
    data._name   = "Resized";
    data._width  = 4;
    data._height = 4;
    // 배열은 비어 있다 — 크기만 바꾸고 칸을 채우지 않은 상태.

    const sw::string xml = data.toXML();
    SW_ASSERT_TRUE( xml.empty() == false );

    sw::TileMapXMLData loaded;
    SW_ASSERT_TRUE( loaded.loadFromXML( xml ) );
    SW_EXPECT_EQUAL( 4, loaded._width );
    SW_EXPECT_EQUAL( 4, loaded._height );
    SW_ASSERT_EQUAL( size_t( 16 ), loaded.getFlagLayer( sw::TileFlagLayer::Walkable ).size() );
    SW_ASSERT_EQUAL( size_t( 16 ), loaded._listVisual.size() );

    // 모자란 칸은 읽기 쪽 기본값(통행 가능 · 기본 틴트)과 같아야 왕복이 어긋나지 않는다.
    const sw::TileMapXMLData::Visual defaultVisual{};
    for ( size_t tileIndex = 0; tileIndex < loaded.getFlagLayer( sw::TileFlagLayer::Walkable ).size(); ++tileIndex )
    {
        SW_EXPECT_EQUAL( uint32( 1 ), uint32( loaded.getFlagLayer( sw::TileFlagLayer::Walkable )[tileIndex] ) );
        SW_EXPECT_EQUAL( uint32( 0 ), uint32( loaded.getFlagLayer( sw::TileFlagLayer::Encounter )[tileIndex] ) );
        SW_EXPECT_EQUAL( uint32( 0 ), uint32( loaded.getFlagLayer( sw::TileFlagLayer::PassThrough )[tileIndex] ) );
        SW_EXPECT_EQUAL( uint32( defaultVisual._tintR ), uint32( loaded._listVisual[tileIndex]._tintR ) );
        SW_EXPECT_EQUAL( uint32( defaultVisual._tintG ), uint32( loaded._listVisual[tileIndex]._tintG ) );
        SW_EXPECT_EQUAL( uint32( defaultVisual._tintB ), uint32( loaded._listVisual[tileIndex]._tintB ) );
    }
}

/**
 * @brief [TileMapXMLTest] 크기가 없거나 0 이하면 8×8 로 서고, 배열도 그 크기로 선다
 * @details 헤더가 아니라 구현에만 있던 규칙이다. 이게 없으면 뒤따르는 인덱싱이 전부 빈 배열을 민다.
 */
SW_TEST_CASE( TileMapXMLTest, MissingOrNegativeSizeFallsBackToEightByEight )
{
    sw::TileMapXMLData loaded;
    SW_ASSERT_TRUE( loaded.loadFromXML( "<TileMap><name>NoSize</name></TileMap>" ) );
    SW_EXPECT_EQUAL( 8, loaded._width );
    SW_EXPECT_EQUAL( 8, loaded._height );
    SW_EXPECT_EQUAL( size_t( 64 ), loaded.getFlagLayer( sw::TileFlagLayer::Walkable ).size() );
    SW_EXPECT_EQUAL( size_t( 64 ), loaded._listVisual.size() );

    SW_ASSERT_TRUE( loaded.loadFromXML( "<TileMap><width>-3</width><height>0</height></TileMap>" ) );
    SW_EXPECT_EQUAL( 8, loaded._width );
    SW_EXPECT_EQUAL( 8, loaded._height );
    SW_EXPECT_EQUAL( size_t( 64 ), loaded.getFlagLayer( sw::TileFlagLayer::PassThrough ).size() );
}

/**
 * @brief [TileMapXMLTest] 선언한 칸 수보다 많은 `<t>` 가 와도 그만큼만 읽는다
 * @details 손으로 고친 맵이나 크기를 줄이고 저장하다 만 파일이 이 모양이 된다.
 */
SW_TEST_CASE( TileMapXMLTest, ExtraTilesBeyondTheDeclaredCountAreIgnored )
{
    sw::string xml = "<TileMap><width>2</width><height>1</height><tiles>";
    for ( uint32 tileIndex = 0; tileIndex < 10; ++tileIndex )
    {
        xml += "<t h=\"3\">0</t>";
    }
    xml += "</tiles></TileMap>";

    sw::TileMapXMLData loaded;
    SW_ASSERT_TRUE( loaded.loadFromXML( xml ) );
    SW_EXPECT_EQUAL( size_t( 2 ), loaded.getFlagLayer( sw::TileFlagLayer::Walkable ).size() );
    SW_EXPECT_EQUAL( size_t( 2 ), loaded._listVisual.size() );
    SW_EXPECT_EQUAL( uint32( 0 ), uint32( loaded.getFlagLayer( sw::TileFlagLayer::Walkable )[0] ) );
    SW_EXPECT_EQUAL( uint32( 3 ), uint32( loaded._listVisual[1]._height ) );
}

/**
 * @brief [TileMapXMLTest] 깨진 문서는 실패로 끝나고, 반쯤 채운 상태를 남기지 않는다
 * @details 로드는 **먼저 비우고** 읽는다. 그래서 실패한 로드 뒤의 객체는 빈 맵이지 이전 맵이
 *          아니다 — 부르는 쪽이 그 사실을 알아야 "왜 맵이 사라졌나" 를 여기서 찾는다.
 */
SW_TEST_CASE( TileMapXMLTest, MalformedDocumentFailsAndLeavesAnEmptyMap )
{
    sw::TileMapXMLData loaded = makeSampleMap();
    SW_ASSERT_EQUAL( size_t( 6 ), loaded.getFlagLayer( sw::TileFlagLayer::Walkable ).size() );

    SW_EXPECT_FALSE( loaded.loadFromXML( "<TileMap><width>2</width>" ) );
    SW_EXPECT_TRUE( loaded.getFlagLayer( sw::TileFlagLayer::Walkable ).empty() );
    SW_EXPECT_TRUE( loaded._name.empty() );
    SW_EXPECT_EQUAL( 0, loaded._width );

    // 루트 이름이 다르면 파싱은 되지만 타일맵이 아니다.
    SW_EXPECT_FALSE( loaded.loadFromXML( "<Level><width>2</width><height>2</height></Level>" ) );
    SW_EXPECT_TRUE( loaded.getFlagLayer( sw::TileFlagLayer::Walkable ).empty() );

    // 빈 경로는 파일을 뒤지지 않고 거절한다.
    SW_EXPECT_FALSE( loaded.load( "" ) );
}

/**
 * @brief [TileMapXMLTest] 감당할 수 없는 크기는 거절한다 — 그만큼을 잡으려다 죽지 않는다
 * @details `<width>` 와 `<height>` 는 파일이 주는 int32 이고, 바로 그 곱만큼의 칸을 배열 넷에
 *          잡는다. 상한이 없으면 `100000 x 100000` 한 줄이 10^10 칸 요청이 되어 그 자리에서
 *          죽는다. 에디터의 Width/Height 칸도 같은 경로라 `TileMapPanel::resize` 가 같은
 *          `isSizeSupported` 를 본다.
 */
SW_TEST_CASE( TileMapXMLTest, SizeBeyondTheTileLimitIsRejected )
{
    SW_EXPECT_TRUE( sw::TileMapXMLData::isSizeSupported( 2048, 2048 ) );
    SW_EXPECT_FALSE( sw::TileMapXMLData::isSizeSupported( 2048, 2049 ) );
    // 곱을 int32 로 내면 접혀서 "작다" 로 읽히는 조합이다 (65536 x 65536 == 2^32).
    SW_EXPECT_FALSE( sw::TileMapXMLData::isSizeSupported( 65536, 65536 ) );
    SW_EXPECT_FALSE( sw::TileMapXMLData::isSizeSupported( 2147483647, 2147483647 ) );

    sw::TileMapXMLData loaded;
    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_FALSE( loaded.loadFromXML( "<TileMap><width>100000</width><height>100000</height></TileMap>" ) );
    }
    SW_EXPECT_TRUE( loaded.getFlagLayer( sw::TileFlagLayer::Walkable ).empty() );
    SW_EXPECT_EQUAL( 0, loaded._width );

    // 상한 안쪽은 그대로 열린다.
    SW_ASSERT_TRUE( loaded.loadFromXML( "<TileMap><width>64</width><height>64</height></TileMap>" ) );
    SW_EXPECT_EQUAL( size_t( 4096 ), loaded.getFlagLayer( sw::TileFlagLayer::Walkable ).size() );
}

/**
 * @brief [TileMapXMLTest] 0~255 칸(높이 · 색)이 범위를 넘으면 묶는다 — 잘라 넣지 않는다
 * @details `int32` 로 읽어 그대로 잘라 넣으면 색 "300" 이 44 · "-1" 이 255 가 된다. 유니티 `Color32` 처럼 0 · 255 로 묶는다.
 */
SW_TEST_CASE( TileMapXMLTest, OutOfRangeByteAttributesAreClamped )
{
    sw::TileMapXMLData loaded;
    {
        test::ScopedDefensiveTestLog expected( "tile attributes outside 0..255" );
        SW_ASSERT_TRUE( loaded.loadFromXML( "<TileMap><width>1</width><height>1</height><tiles>"
                                            "<t h=\"999\" tr=\"300\" tg=\"-1\" tb=\"128\">1</t></tiles></TileMap>" ) );
    }
    SW_ASSERT_EQUAL( size_t( 1 ), loaded._listVisual.size() );
    const sw::TileMapXMLData::Visual& visual = loaded._listVisual[0];
    SW_EXPECT_EQUAL( 255, static_cast<int32>( visual._height ) );
    SW_EXPECT_EQUAL( 255, static_cast<int32>( visual._tintR ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( visual._tintG ) );
    SW_EXPECT_EQUAL( 128, static_cast<int32>( visual._tintB ) );
}

// ------------------------------------------------------------------------------
// 플래그 레이어 표 — 레이어마다 쓰고 읽기, 기존 파일 바이트 유지
// ------------------------------------------------------------------------------

/**
 * @brief [TileMapXMLTest] 레이어 표가 레이어마다 한 줄이고, XML 속성 이름이 기존 파일 형식 그대로인지
 * @details 속성 이름은 파일 형식이다 — 바뀌면 기존 맵의 그 레이어가 조용히 기본값으로 읽힌다.
 */
SW_TEST_CASE( TileMapXMLTest, FlagLayerTableKeepsTheFileFormat )
{
    static_assert( SW_COUNT_OF( sw::kArrTileFlagLayerInfo ) == sw::kTileFlagLayerCount, "레이어마다 한 줄" );
    for ( size_t layerIndex = 0; layerIndex < sw::kTileFlagLayerCount; ++layerIndex )
    {
        SW_EXPECT_TRUE( sw::kArrTileFlagLayerInfo[layerIndex]._layer == static_cast<sw::TileFlagLayer>( layerIndex ) );
    }

    const sw::TileFlagLayerInfo& walkable = sw::kArrTileFlagLayerInfo[static_cast<size_t>( sw::TileFlagLayer::Walkable )];
    SW_EXPECT_NULL( walkable._pXMLAttribute );
    SW_EXPECT_EQUAL( uint32( 1 ), uint32( walkable._defaultValue ) );
    SW_EXPECT_EQUAL( sw::string_view( "enc" ), sw::string_view( sw::kArrTileFlagLayerInfo[static_cast<size_t>( sw::TileFlagLayer::Encounter )]._pXMLAttribute ) );
    SW_EXPECT_EQUAL( sw::string_view( "pt" ), sw::string_view( sw::kArrTileFlagLayerInfo[static_cast<size_t>( sw::TileFlagLayer::PassThrough )]._pXMLAttribute ) );
}

/**
 * @brief [TileMapXMLTest] 레이어마다 한 칸만 바꿔 쓰고 읽으면 그 레이어의 그 칸만 바뀐다
 * @details 레이어 둘이 같은 속성 · 같은 배열을 보면(표 줄을 복사하다 이름만 고친 경우) 다른 레이어가 같이 바뀐다.
 */
SW_TEST_CASE( TileMapXMLTest, EveryFlagLayerRoundTripsAlone )
{
    for ( const sw::TileFlagLayerInfo& info : sw::kArrTileFlagLayerInfo )
    {
        sw::TileMapXMLData source;
        SW_ASSERT_TRUE( source.resetTiles( 3, 2 ) );
        const uint8 flipped                   = info._defaultValue != 0 ? 0 : 1;
        source.getFlagLayer( info._layer )[4] = flipped;

        sw::TileMapXMLData loaded;
        SW_ASSERT_TRUE( loaded.loadFromXML( source.toXML() ) );
        for ( const sw::TileFlagLayerInfo& other : sw::kArrTileFlagLayerInfo )
        {
            const sw::vector<uint8>& listFlag = loaded.getFlagLayer( other._layer );
            SW_ASSERT_EQUAL( size_t( 6 ), listFlag.size() );
            for ( size_t tileIndex = 0; tileIndex < listFlag.size(); ++tileIndex )
            {
                const bool  bFlippedTile = other._layer == info._layer && tileIndex == 4;
                const uint8 expected     = bFlippedTile ? flipped : other._defaultValue;
                SW_EXPECT_EQUAL( uint32( expected ), uint32( listFlag[tileIndex] ) );
            }
        }
    }
}

/**
 * @brief [TileMapXMLTest] 레이어 표로 하는 쓰기가 기준 형식과 같은 바이트를 낸다
 * @details 아래 두 문서는 기준 출력이다(저장소에 타일맵 파일이 없어 시험 안에 둔다). 하나는 칸마다 다른 맵,
 *          하나는 높이 · 틴트 없이 `enc` · `pt` 만 적힌 손글 맵을 읽어 다시 쓴 것이다.
 */
SW_TEST_CASE( TileMapXMLTest, SavedBytesMatchTheExistingFormat )
{
    const sw::string_view kSampleXML = R"(<TileMap>
	<name>SampleTown</name>
	<width>3</width>
	<height>2</height>
	<scene>game/empty/maps/sample.scene.xml</scene>
	<role>town</role>
	<spawn x="2" y="1" />
	<tiles>
		<t h="0" enc="1" tr="10" tg="20" tb="30">1</t>
		<t h="1" atlas="1" tr="11" tg="21" tb="31">0</t>
		<t h="2" atlas="2" tr="12" tg="22" tb="32">1</t>
		<t h="3" enc="1" atlas="3" tr="13" tg="23" tb="33">0</t>
		<t h="4" pt="1" tr="14" tg="24" tb="34">1</t>
		<t h="5" atlas="1" tr="15" tg="25" tb="35">0</t>
	</tiles>
	<warps>
		<warp x="2" y="0" map="game/empty/maps/cave.tilemap.xml" tx="5" ty="7" pair="cave-entrance" />
	</warps>
</TileMap>
)";
    SW_EXPECT_TRUE_MSG( makeSampleMap().toXML() == kSampleXML, "레이어 표로 쓴 바이트가 기존 형식과 다르다" );

    sw::TileMapXMLData sample;
    SW_ASSERT_TRUE( sample.loadFromXML( kSampleXML ) );
    SW_EXPECT_TRUE_MSG( sample.toXML() == kSampleXML, "기존 형식 파일을 읽고 다시 쓰면 바이트가 달라진다" );

    const sw::string_view kLegacyInput = "<TileMap><name>Legacy</name><width>3</width><height>2</height><spawn x=\"1\" y=\"0\"/><tiles>"
                                         "<t>1</t><t enc=\"1\">1</t><t>0</t><t pt=\"1\">1</t><t enc=\"1\" pt=\"1\">0</t><t atlas=\"3\">1</t>"
                                         "</tiles></TileMap>";
    const sw::string_view kLegacyXML   = R"(<TileMap>
	<name>Legacy</name>
	<width>3</width>
	<height>2</height>
	<spawn x="1" y="0" />
	<tiles>
		<t h="1" tr="180" tg="200" tb="160">1</t>
		<t h="2" enc="1" tr="120" tg="190" tb="90">1</t>
		<t h="0" tr="80" tg="80" tb="90">0</t>
		<t h="1" pt="1" tr="160" tg="170" tb="200">1</t>
		<t h="2" enc="1" pt="1" tr="120" tg="190" tb="90">0</t>
		<t h="1" atlas="3" tr="180" tg="200" tb="160">1</t>
	</tiles>
	<warps />
</TileMap>
)";
    sw::TileMapXMLData    legacy;
    SW_ASSERT_TRUE( legacy.loadFromXML( kLegacyInput ) );
    SW_EXPECT_TRUE_MSG( legacy.toXML() == kLegacyXML, "옛 손글 맵을 읽어 쓴 바이트가 기존 형식과 다르다" );
}
