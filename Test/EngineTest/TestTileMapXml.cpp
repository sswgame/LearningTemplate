#include "pch.h"

#include "Core/Container/string.h"

#include "Engine/Utility/Xml/TileMapXml.h"

#include "TestFramework/TestFramework.h"

// TileMapXmlTest — 타일맵 XML 스키마의 왕복·기본값·방어. 파일을 만들지 않는다(문자열만 오간다).
//
// 이 스키마는 엔진이 소유하고 에디터(TileMapPanel · EditorToolAssetCommands)와 GameFramework 의
// Overworld 킷이 같이 읽고 쓴다. 한쪽이 쓴 것을 다른 쪽이 못 읽으면 증상은 "맵이 8×8 빈 칸으로
// 열린다" 라 데이터가 아니라 도구를 의심하게 된다.

namespace
{
    /** @brief 칸마다 값이 다른 3×2 맵을 만든다 — 왕복에서 자리가 밀리면 값이 어긋난다. */
    sw::TileMapXmlData makeSampleMap()
    {
        sw::TileMapXmlData data;
        data._name      = "SampleTown";
        data._scenePath = "game/empty/maps/sample.scene.xml";
        data._role      = "town";
        data._width     = 3;
        data._height    = 2;
        data._spawnX    = 2;
        data._spawnY    = 1;

        const size_t count = 6;
        data._listWalkable.assign( count, 1 );
        data._listEncounter.assign( count, 0 );
        data._listPassThrough.assign( count, 0 );
        data._listVisual.assign( count, sw::TileMapXmlData::Visual{} );

        for ( size_t tileIndex = 0; tileIndex < count; ++tileIndex )
        {
            data._listWalkable[tileIndex]    = ( tileIndex % 2 == 0 ) ? 1 : 0;
            data._listEncounter[tileIndex]   = ( tileIndex % 3 == 0 ) ? 1 : 0;
            data._listPassThrough[tileIndex] = ( tileIndex == 4 ) ? 1 : 0;

            sw::TileMapXmlData::Visual tileVisual;
            tileVisual._height          = static_cast<uint8>( tileIndex );
            tileVisual._tintR           = static_cast<uint8>( 10 + tileIndex );
            tileVisual._tintG           = static_cast<uint8>( 20 + tileIndex );
            tileVisual._tintB           = static_cast<uint8>( 30 + tileIndex );
            tileVisual._atlasId         = static_cast<uint8>( tileIndex % 4 );
            data._listVisual[tileIndex] = tileVisual;
        }

        sw::TileMapXmlData::Warp warp;
        warp._tileX       = 2;
        warp._tileY       = 0;
        warp._targetMap   = "game/empty/maps/cave.tilemap.xml";
        warp._targetTileX = 5;
        warp._targetTileY = 7;
        warp._pairId      = "cave-entrance";
        data._listWarp.push_back( warp );

        sw::TileMapXmlData::Encounter entry;
        entry._speciesId = "slime";
        entry._weight    = 2.25f;
        data._listEncounterEntry.push_back( entry );

        return data;
    }

    /** @brief 두 맵의 필드가 모두 같은가 (직렬화되지 않는 _sourcePath 는 제외). */
    void expectSameMap( const sw::TileMapXmlData& expected, const sw::TileMapXmlData& actual )
    {
        SW_EXPECT_TRUE( expected._name == actual._name );
        SW_EXPECT_TRUE( expected._scenePath == actual._scenePath );
        SW_EXPECT_TRUE( expected._role == actual._role );
        SW_EXPECT_EQUAL( expected._width, actual._width );
        SW_EXPECT_EQUAL( expected._height, actual._height );
        SW_EXPECT_EQUAL( expected._spawnX, actual._spawnX );
        SW_EXPECT_EQUAL( expected._spawnY, actual._spawnY );

        SW_ASSERT_EQUAL( expected._listWalkable.size(), actual._listWalkable.size() );
        SW_ASSERT_EQUAL( expected._listVisual.size(), actual._listVisual.size() );
        for ( size_t tileIndex = 0; tileIndex < expected._listWalkable.size(); ++tileIndex )
        {
            SW_EXPECT_EQUAL( uint32( expected._listWalkable[tileIndex] ), uint32( actual._listWalkable[tileIndex] ) );
            SW_EXPECT_EQUAL( uint32( expected._listEncounter[tileIndex] ), uint32( actual._listEncounter[tileIndex] ) );
            SW_EXPECT_EQUAL( uint32( expected._listPassThrough[tileIndex] ), uint32( actual._listPassThrough[tileIndex] ) );
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

        SW_ASSERT_EQUAL( expected._listEncounterEntry.size(), actual._listEncounterEntry.size() );
        for ( size_t entryIndex = 0; entryIndex < expected._listEncounterEntry.size(); ++entryIndex )
        {
            SW_EXPECT_TRUE( expected._listEncounterEntry[entryIndex]._speciesId == actual._listEncounterEntry[entryIndex]._speciesId );
            SW_EXPECT_NEAR_EQUAL( expected._listEncounterEntry[entryIndex]._weight,
                                  actual._listEncounterEntry[entryIndex]._weight, 0.001f );
        }
    }
} // namespace

/**
 * @brief [TileMapXmlTest] 쓴 것을 그대로 읽는다 — 칸마다 다른 값으로
 * @details 칸 값이 전부 같으면 자리가 한 칸 밀려도 통과한다. 그래서 칸마다 다른 값을 넣는다.
 */
SW_TEST_CASE( TileMapXmlTest, RoundTripKeepsEveryField )
{
    const sw::TileMapXmlData source = makeSampleMap();
    const sw::string         xml    = source.toXml();
    SW_ASSERT_TRUE( xml.empty() == false );

    sw::TileMapXmlData loaded;
    SW_ASSERT_TRUE( loaded.loadFromXml( xml ) );
    expectSameMap( source, loaded );
}

/**
 * @brief [TileMapXmlTest] 같은 맵을 두 번 쓰면 같은 바이트가 나오고, 다시 읽어도 같다
 * @details 입력 바인딩에서 실제로 났던 함정이다 — 저장할 때마다 바이트가 달라지면 버전 관리가
 *          매번 diff 를 만들고, 그 diff 를 보고 "누가 맵을 건드렸다" 고 읽게 된다.
 */
SW_TEST_CASE( TileMapXmlTest, SavingTwiceProducesTheSameBytes )
{
    const sw::TileMapXmlData source = makeSampleMap();
    const sw::string         first  = source.toXml();
    const sw::string         second = source.toXml();
    SW_EXPECT_TRUE_MSG( first == second, "같은 맵을 두 번 썼는데 바이트가 달라졌다" );

    sw::TileMapXmlData loaded;
    SW_ASSERT_TRUE( loaded.loadFromXml( first ) );
    const sw::string reSaved = loaded.toXml();
    SW_EXPECT_TRUE_MSG( first == reSaved, "읽고 다시 쓰면 바이트가 달라졌다 — 왕복이 고정점이 아니다" );
}

/**
 * @brief [TileMapXmlTest] 크기만 있고 칸 배열이 모자란 맵을 써도 배열 밖을 읽지 않는다
 * @details 이 구조체는 필드가 전부 공개라 `_width`·`_height` 만 바꾸고 네 배열을 안 늘린 채
 *          저장할 수 있다. 그때 `toXml` 이 `_width × _height` 만 믿으면 남의 메모리를 읽어
 *          파일에 적는다(Debug 에서는 vector 단언이 먼저 터진다).
 */
SW_TEST_CASE( TileMapXmlTest, SavingWithShortTileArraysStaysInsideTheArrays )
{
    sw::TileMapXmlData data;
    data._name   = "Resized";
    data._width  = 4;
    data._height = 4;
    // 배열은 비어 있다 — 크기만 바꾸고 칸을 채우지 않은 상태.

    const sw::string xml = data.toXml();
    SW_ASSERT_TRUE( xml.empty() == false );

    sw::TileMapXmlData loaded;
    SW_ASSERT_TRUE( loaded.loadFromXml( xml ) );
    SW_EXPECT_EQUAL( 4, loaded._width );
    SW_EXPECT_EQUAL( 4, loaded._height );
    SW_ASSERT_EQUAL( size_t( 16 ), loaded._listWalkable.size() );
    SW_ASSERT_EQUAL( size_t( 16 ), loaded._listVisual.size() );

    // 모자란 칸은 읽기 쪽 기본값(통행 가능 · 기본 틴트)과 같아야 왕복이 어긋나지 않는다.
    const sw::TileMapXmlData::Visual defaultVisual{};
    for ( size_t tileIndex = 0; tileIndex < loaded._listWalkable.size(); ++tileIndex )
    {
        SW_EXPECT_EQUAL( uint32( 1 ), uint32( loaded._listWalkable[tileIndex] ) );
        SW_EXPECT_EQUAL( uint32( 0 ), uint32( loaded._listEncounter[tileIndex] ) );
        SW_EXPECT_EQUAL( uint32( 0 ), uint32( loaded._listPassThrough[tileIndex] ) );
        SW_EXPECT_EQUAL( uint32( defaultVisual._tintR ), uint32( loaded._listVisual[tileIndex]._tintR ) );
        SW_EXPECT_EQUAL( uint32( defaultVisual._tintG ), uint32( loaded._listVisual[tileIndex]._tintG ) );
        SW_EXPECT_EQUAL( uint32( defaultVisual._tintB ), uint32( loaded._listVisual[tileIndex]._tintB ) );
    }
}

/**
 * @brief [TileMapXmlTest] 크기가 없거나 0 이하면 8×8 로 서고, 배열도 그 크기로 선다
 * @details 헤더가 아니라 구현에만 있던 규칙이다. 이게 없으면 뒤따르는 인덱싱이 전부 빈 배열을 민다.
 */
SW_TEST_CASE( TileMapXmlTest, MissingOrNegativeSizeFallsBackToEightByEight )
{
    sw::TileMapXmlData loaded;
    SW_ASSERT_TRUE( loaded.loadFromXml( "<TileMap><name>NoSize</name></TileMap>" ) );
    SW_EXPECT_EQUAL( 8, loaded._width );
    SW_EXPECT_EQUAL( 8, loaded._height );
    SW_EXPECT_EQUAL( size_t( 64 ), loaded._listWalkable.size() );
    SW_EXPECT_EQUAL( size_t( 64 ), loaded._listVisual.size() );

    SW_ASSERT_TRUE( loaded.loadFromXml( "<TileMap><width>-3</width><height>0</height></TileMap>" ) );
    SW_EXPECT_EQUAL( 8, loaded._width );
    SW_EXPECT_EQUAL( 8, loaded._height );
    SW_EXPECT_EQUAL( size_t( 64 ), loaded._listPassThrough.size() );
}

/**
 * @brief [TileMapXmlTest] 선언한 칸 수보다 많은 `<t>` 가 와도 그만큼만 읽는다
 * @details 손으로 고친 맵이나 크기를 줄이고 저장하다 만 파일이 이 모양이 된다.
 */
SW_TEST_CASE( TileMapXmlTest, ExtraTilesBeyondTheDeclaredCountAreIgnored )
{
    sw::string xml = "<TileMap><width>2</width><height>1</height><tiles>";
    for ( uint32 tileIndex = 0; tileIndex < 10; ++tileIndex )
        xml += "<t h=\"3\">0</t>";
    xml += "</tiles></TileMap>";

    sw::TileMapXmlData loaded;
    SW_ASSERT_TRUE( loaded.loadFromXml( xml ) );
    SW_EXPECT_EQUAL( size_t( 2 ), loaded._listWalkable.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), loaded._listVisual.size() );
    SW_EXPECT_EQUAL( uint32( 0 ), uint32( loaded._listWalkable[0] ) );
    SW_EXPECT_EQUAL( uint32( 3 ), uint32( loaded._listVisual[1]._height ) );
}

/**
 * @brief [TileMapXmlTest] 깨진 문서는 실패로 끝나고, 반쯤 채운 상태를 남기지 않는다
 * @details 로드는 **먼저 비우고** 읽는다. 그래서 실패한 로드 뒤의 객체는 빈 맵이지 이전 맵이
 *          아니다 — 부르는 쪽이 그 사실을 알아야 "왜 맵이 사라졌나" 를 여기서 찾는다.
 */
SW_TEST_CASE( TileMapXmlTest, MalformedDocumentFailsAndLeavesAnEmptyMap )
{
    sw::TileMapXmlData loaded = makeSampleMap();
    SW_ASSERT_EQUAL( size_t( 6 ), loaded._listWalkable.size() );

    SW_EXPECT_FALSE( loaded.loadFromXml( "<TileMap><width>2</width>" ) );
    SW_EXPECT_TRUE( loaded._listWalkable.empty() );
    SW_EXPECT_TRUE( loaded._name.empty() );
    SW_EXPECT_EQUAL( 0, loaded._width );

    // 루트 이름이 다르면 파싱은 되지만 타일맵이 아니다.
    SW_EXPECT_FALSE( loaded.loadFromXml( "<Level><width>2</width><height>2</height></Level>" ) );
    SW_EXPECT_TRUE( loaded._listWalkable.empty() );

    // 빈 경로는 파일을 뒤지지 않고 거절한다.
    SW_EXPECT_FALSE( loaded.load( "" ) );
}

/**
 * @brief [TileMapXmlTest] 감당할 수 없는 크기는 거절한다 — 그만큼을 잡으려다 죽지 않는다
 * @details `<width>` 와 `<height>` 는 파일이 주는 int32 이고, 바로 그 곱만큼의 칸을 배열 넷에
 *          잡는다. 상한이 없으면 `100000 x 100000` 한 줄이 10^10 칸 요청이 되어 그 자리에서
 *          죽는다. 에디터의 Width/Height 칸도 같은 경로라 `TileMapPanel::resize` 가 같은
 *          `isSizeSupported` 를 본다.
 */
SW_TEST_CASE( TileMapXmlTest, SizeBeyondTheTileLimitIsRejected )
{
    SW_EXPECT_TRUE( sw::TileMapXmlData::isSizeSupported( 2048, 2048 ) );
    SW_EXPECT_FALSE( sw::TileMapXmlData::isSizeSupported( 2048, 2049 ) );
    // 곱을 int32 로 내면 접혀서 "작다" 로 읽히는 조합이다 (65536 x 65536 == 2^32).
    SW_EXPECT_FALSE( sw::TileMapXmlData::isSizeSupported( 65536, 65536 ) );
    SW_EXPECT_FALSE( sw::TileMapXmlData::isSizeSupported( 2147483647, 2147483647 ) );

    sw::TileMapXmlData loaded;
    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_FALSE( loaded.loadFromXml( "<TileMap><width>100000</width><height>100000</height></TileMap>" ) );
    }
    SW_EXPECT_TRUE( loaded._listWalkable.empty() );
    SW_EXPECT_EQUAL( 0, loaded._width );

    // 상한 안쪽은 그대로 열린다.
    SW_ASSERT_TRUE( loaded.loadFromXml( "<TileMap><width>64</width><height>64</height></TileMap>" ) );
    SW_EXPECT_EQUAL( size_t( 4096 ), loaded._listWalkable.size() );
}
