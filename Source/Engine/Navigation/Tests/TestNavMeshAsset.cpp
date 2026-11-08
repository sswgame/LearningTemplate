#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"

using namespace sw;

namespace
{
    struct NavMeshAssetTestUtil
    {
        static NavTileData makeTile( int32 tileX, int32 tileZ, const vector<uint8>& listByte )
        {
            NavTileData tile;
            tile._tileX = tileX;
            tile._tileZ = tileZ;
            tile._bytes = listByte;
            return tile;
        }

        static NavMeshAssetEntry makeEntry( const hashed_string& agentType, uint64 hash )
        {
            NavMeshAssetEntry entry;
            entry._agentType    = agentType;
            entry._settingsHash = hash;
            entry._inputHash    = hash + 1;
            entry._bounds       = AABB{
                float3{-1.0f, -2.0f, -3.0f},
                float3{ 1.0f,  2.0f,  3.0f}
            };
            entry._listTile.push_back( makeTile( 1, -2, { 10, 20, 30 } ) );
            entry._listTile.push_back( makeTile( -3, 4, vector<uint8>{} ) ); // 바이트가 빈 타일은 "걸을 곳 없음" 이다
            return entry;
        }

        /** @brief 항목 둘의 쿠킹본 바이트입니다. 첫 항목의 종류 이름은 `Walker` 입니다. */
        static vector<uint8> makeBytes()
        {
            NavMeshAsset asset;
            asset.setEntry( makeEntry( hashed_string{ "Walker" }, 100 ) );
            asset.setEntry( makeEntry( hashed_string{ "Crawler" }, 200 ) );
            vector<uint8> listByte;
            asset.writeBytes( listByte );
            return listByte;
        }

        static void patchUint32( vector<uint8>& inoutBytes, size_t offset, uint32 value ) { Memory::copy( inoutBytes.data() + offset, &value, sizeof( value ) ); }

        // 바이트 자리: 매직(0) · 형식 버전(4) · 백엔드 이름 길이(8) · 이름 · 백엔드 버전 · 항목 수 · 첫 항목.
        static constexpr size_t kVersionOffset     = 4;
        static constexpr size_t kBackendNameOffset = 12;

        static size_t getBackendNameSize() { return string_view( NavMeshBackend::getBackendName() ).size(); }
        static size_t getBackendVersionOffset() { return kBackendNameOffset + getBackendNameSize(); }
        static size_t getEntryCountOffset() { return getBackendVersionOffset() + sizeof( uint32 ); }
        static size_t getFirstEntryOffset() { return getEntryCountOffset() + sizeof( uint32 ); }
        /** @brief 첫 항목의 타일 수 자리입니다(종류 이름 길이 · `Walker` · 두 해시 · 경계 뒤). */
        static size_t getFirstTileCountOffset() { return getFirstEntryOffset() + sizeof( uint32 ) + 6u + sizeof( uint64 ) * 2u + sizeof( AABB ); }
    };
    using Util = NavMeshAssetTestUtil;
} // namespace

/** @brief [NavMeshAssetTest] 쓴 바이트를 그대로 읽는다 — 해시 · 경계 · 음수 타일 좌표 · 빈 타일 바이트 포함 */
SW_TEST_CASE( NavMeshAssetTest, BytesRoundTrip )
{
    const vector<uint8> listByte = Util::makeBytes();
    NavMeshAsset        asset;
    SW_REQUIRE_TRUE( asset.readBytes( listByte.data(), listByte.size(), "test" ) );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( asset.getEntries().size() ) );

    const NavMeshAssetEntry* pEntry = asset.findEntry( hashed_string{ "Crawler" } );
    SW_REQUIRE_NOT_NULL( pEntry );
    SW_EXPECT_EQUAL( 200ull, pEntry->_settingsHash );
    SW_EXPECT_EQUAL( 201ull, pEntry->_inputHash );
    SW_EXPECT_NEAR( -2.0f, pEntry->_bounds._min._y, 0.0f );
    SW_EXPECT_NEAR( 3.0f, pEntry->_bounds._max._z, 0.0f );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( pEntry->_listTile.size() ) );
    SW_EXPECT_EQUAL( 1, pEntry->_listTile[0]._tileX );
    SW_EXPECT_EQUAL( -2, pEntry->_listTile[0]._tileZ );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( pEntry->_listTile[0]._bytes.size() ) );
    SW_EXPECT_EQUAL( 30u, static_cast<uint32>( pEntry->_listTile[0]._bytes[2] ) );
    SW_EXPECT_EQUAL( -3, pEntry->_listTile[1]._tileX );
    SW_EXPECT_TRUE( pEntry->_listTile[1]._bytes.empty() );
}

/** @brief [NavMeshAssetTest] 항목이 없는 쿠킹본도 올바른 파일이다 */
SW_TEST_CASE( NavMeshAssetTest, EmptyAssetRoundTrips )
{
    const NavMeshAsset empty;
    vector<uint8>      listByte;
    empty.writeBytes( listByte );
    NavMeshAsset asset;
    SW_EXPECT_TRUE( asset.readBytes( listByte.data(), listByte.size(), "test" ) );
    SW_EXPECT_TRUE( asset.getEntries().empty() );
    SW_EXPECT_NULL( asset.findEntry( hashed_string{ "Walker" } ) );
}

/** @brief [NavMeshAssetTest] 파일 형식은 신뢰 경계다 — 어느 길이로 잘려도 false 이고 푼 앞부분을 남기지 않는다 */
SW_TEST_CASE( NavMeshAssetTest, EveryTruncatedLengthIsRejected )
{
    SW_EXPECT_LOG( "NavMeshAsset.NotNavmeshFormat" );
    SW_EXPECT_LOG( "NavMeshAsset.Truncated" );
    const vector<uint8> listByte = Util::makeBytes();
    NavMeshAsset        asset;
    for ( size_t length = 0; length < listByte.size(); ++length )
    {
        SW_EXPECT_FALSE( asset.readBytes( listByte.data(), length, "test" ) );
        SW_EXPECT_TRUE( asset.getEntries().empty() );
    }
    SW_EXPECT_TRUE( asset.readBytes( listByte.data(), listByte.size(), "test" ) );
}

/** @brief [NavMeshAssetTest] 바이트가 없어도(nullptr) 오류일 뿐이다 */
SW_TEST_CASE( NavMeshAssetTest, NullBytesAreRejected )
{
    SW_EXPECT_LOG( "NavMeshAsset.NotNavmeshFormat" );
    NavMeshAsset asset;
    SW_EXPECT_FALSE( asset.readBytes( nullptr, 16, "test" ) );
}

/** @brief [NavMeshAssetTest] 매직과 형식 버전이 다르면 읽지 않는다 — 앞서 읽은 내용도 비운다 */
SW_TEST_CASE( NavMeshAssetTest, WrongMagicOrVersionClearsTheAsset )
{
    SW_EXPECT_LOG( "NavMeshAsset.NotNavmeshFormat" );
    const vector<uint8> listByte = Util::makeBytes();
    NavMeshAsset        asset;
    SW_REQUIRE_TRUE( asset.readBytes( listByte.data(), listByte.size(), "test" ) );

    vector<uint8> listMagic = listByte;
    listMagic[0]            = 'X';
    SW_EXPECT_FALSE( asset.readBytes( listMagic.data(), listMagic.size(), "test" ) );
    SW_EXPECT_TRUE( asset.getEntries().empty() );

    SW_REQUIRE_TRUE( asset.readBytes( listByte.data(), listByte.size(), "test" ) );
    vector<uint8> listVersion = listByte;
    Util::patchUint32( listVersion, Util::kVersionOffset, NavMeshAsset::kVersion + 1u );
    SW_EXPECT_FALSE( asset.readBytes( listVersion.data(), listVersion.size(), "test" ) );
    SW_EXPECT_TRUE( asset.getEntries().empty() );
}

/** @brief [NavMeshAssetTest] 다른 백엔드(이름 · 버전)의 쿠킹본은 읽지 않는다 — 낡은 타일 바이트를 라이브러리에 넣지 않는다 */
SW_TEST_CASE( NavMeshAssetTest, OtherBackendIsRejected )
{
    SW_EXPECT_LOG( "NavMeshAsset.CookedNavmeshBackendV" );
    SW_REQUIRE_TRUE( Util::getBackendNameSize() > 0u );
    const vector<uint8> listByte = Util::makeBytes();
    NavMeshAsset        asset;

    vector<uint8> listName             = listByte;
    listName[Util::kBackendNameOffset] = static_cast<uint8>( listName[Util::kBackendNameOffset] ^ 0x20u );
    SW_EXPECT_FALSE( asset.readBytes( listName.data(), listName.size(), "test" ) );
    SW_EXPECT_TRUE( asset.getEntries().empty() );

    vector<uint8> listVersion = listByte;
    Util::patchUint32( listVersion, Util::getBackendVersionOffset(), NavMeshBackend::getBackendFormatVersion() + 1u );
    SW_EXPECT_FALSE( asset.readBytes( listVersion.data(), listVersion.size(), "test" ) );
    SW_EXPECT_TRUE( asset.getEntries().empty() );
}

/** @brief [NavMeshAssetTest] 깨진 항목 수 · 타일 수 · 길이는 잘림으로 끝난다 — 머리말의 수만 믿고 크게 잡지 않는다(잡으면 이 테스트가 메모리에서 멈춘다) */
SW_TEST_CASE( NavMeshAssetTest, HugeCountsAreRejectedBeforeAllocating )
{
    SW_EXPECT_LOG( "NavMeshAsset.Truncated" );
    const vector<uint8> listByte = Util::makeBytes();
    NavMeshAsset        asset;

    vector<uint8> listEntryCount = listByte;
    Util::patchUint32( listEntryCount, Util::getEntryCountOffset(), 0xFFFFFFFFu );
    SW_EXPECT_FALSE( asset.readBytes( listEntryCount.data(), listEntryCount.size(), "test" ) );
    SW_EXPECT_TRUE( asset.getEntries().empty() );

    vector<uint8> listTileCount = listByte;
    Util::patchUint32( listTileCount, Util::getFirstTileCountOffset(), 0xFFFFFFFFu );
    SW_EXPECT_FALSE( asset.readBytes( listTileCount.data(), listTileCount.size(), "test" ) );
    SW_EXPECT_TRUE( asset.getEntries().empty() );

    // 첫 타일의 바이트 길이 자리: 타일 수 뒤의 x · z 다음이다.
    vector<uint8> listTileLength = listByte;
    Util::patchUint32( listTileLength, Util::getFirstTileCountOffset() + sizeof( uint32 ) * 3u, 0xFFFFFFFFu );
    SW_EXPECT_FALSE( asset.readBytes( listTileLength.data(), listTileLength.size(), "test" ) );
    SW_EXPECT_TRUE( asset.getEntries().empty() );

    // 종류 이름 길이도 같다.
    vector<uint8> listNameLength = listByte;
    Util::patchUint32( listNameLength, Util::getFirstEntryOffset(), 0xFFFFFFFFu );
    SW_EXPECT_FALSE( asset.readBytes( listNameLength.data(), listNameLength.size(), "test" ) );
    SW_EXPECT_TRUE( asset.getEntries().empty() );
}

/** @brief [NavMeshAssetTest] 항목 뒤에 남는 바이트와 NaN 경계는 읽지 않는다 */
SW_TEST_CASE( NavMeshAssetTest, TrailingBytesAndNonFiniteBoundsAreRejected )
{
    SW_EXPECT_LOG( "NavMeshAsset.Truncated" );
    const vector<uint8> listByte = Util::makeBytes();
    NavMeshAsset        asset;

    vector<uint8> listLonger = listByte;
    listLonger.push_back( 0 );
    SW_EXPECT_FALSE( asset.readBytes( listLonger.data(), listLonger.size(), "test" ) );
    SW_EXPECT_TRUE( asset.getEntries().empty() );

    vector<uint8> listBounds = listByte;
    Util::patchUint32( listBounds, Util::getFirstTileCountOffset() - sizeof( AABB ), 0x7FC00000u );
    SW_EXPECT_FALSE( asset.readBytes( listBounds.data(), listBounds.size(), "test" ) );
    SW_EXPECT_TRUE( asset.getEntries().empty() );
}

/** @brief [NavMeshAssetTest] 남은 바이트로 꼭 맞는 타일 수는 받는다 — 빈 타일(12 바이트)들이 거름의 경계다 */
SW_TEST_CASE( NavMeshAssetTest, TileCountExactlyFittingTheRestIsAccepted )
{
    NavMeshAssetEntry entry = Util::makeEntry( hashed_string{ "Walker" }, 1 );
    entry._listTile.clear();
    for ( int32 tileIndex = 0; tileIndex < 4; ++tileIndex )
    {
        entry._listTile.push_back( Util::makeTile( tileIndex, 0, vector<uint8>{} ) );
    }
    NavMeshAsset source;
    source.setEntry( entry );
    vector<uint8> listByte;
    source.writeBytes( listByte );

    NavMeshAsset asset;
    SW_REQUIRE_TRUE( asset.readBytes( listByte.data(), listByte.size(), "test" ) );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( asset.getEntries()[0]._listTile.size() ) );
}

/** @brief [NavMeshAssetTest] 같은 종류의 항목은 바꾸고, 다른 종류는 더한다 */
SW_TEST_CASE( NavMeshAssetTest, SetEntryReplacesTheSameAgentType )
{
    NavMeshAsset asset;
    asset.setEntry( Util::makeEntry( hashed_string{ "Walker" }, 1 ) );
    asset.setEntry( Util::makeEntry( hashed_string{ "Crawler" }, 2 ) );
    asset.setEntry( Util::makeEntry( hashed_string{ "Walker" }, 3 ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( asset.getEntries().size() ) );
    const NavMeshAssetEntry* pEntry = asset.findEntry( hashed_string{ "Walker" } );
    SW_REQUIRE_NOT_NULL( pEntry );
    SW_EXPECT_EQUAL( 3ull, pEntry->_settingsHash );
    SW_EXPECT_NULL( asset.findEntry( hashed_string{ "Flyer" } ) );
}

/** @brief [NavMeshAssetTest] 쿠킹본 경로는 씬 저작본 · 쿠킹본 접미사만 받고(대소문자 무시) 소문자다. 씬이 아니면 빈 글이다 */
SW_TEST_CASE( NavMeshAssetTest, CookedPathComesFromTheSceneSuffix )
{
    SW_EXPECT_TRUE( NavMeshAsset::makeCookedPath( "Game/Scenes/Level.scene.xml" ) == "game/scenes/level.navmesh" );
    SW_EXPECT_TRUE( NavMeshAsset::makeCookedPath( "Game/Level.SCENE.BIN" ) == "game/level.navmesh" );
    SW_EXPECT_TRUE( NavMeshAsset::makeCookedPath( "level.scene" ).empty() );
    SW_EXPECT_TRUE( NavMeshAsset::makeCookedPath( "level.xml" ).empty() );
    SW_EXPECT_TRUE( NavMeshAsset::makeCookedPath( "" ).empty() );
}

/** @brief [NavMeshAssetTest] 저장은 없는 폴더를 만들고 같은 바이트를 쓴다 */
SW_TEST_CASE( NavMeshAssetTest, SaveToFileCreatesTheFolder )
{
    NavMeshAsset asset;
    asset.setEntry( Util::makeEntry( hashed_string{ "Walker" }, 1 ) );

    const optional<AbsolutePath> absPath = AbsolutePath::tryMake( test::makeScratchPath( "nested/folder/level.navmesh" ) );
    SW_REQUIRE_TRUE( absPath.has_value() );
    SW_REQUIRE_TRUE( asset.saveToFile( *absPath ) );

    vector<uint8> listRead;
    SW_REQUIRE_TRUE( FileUtil::readFile( absPath->str(), listRead ) );
    vector<uint8> listExpected;
    asset.writeBytes( listExpected );
    SW_EXPECT_TRUE( listRead == listExpected );
}

/** @brief [NavMeshAssetTest] 베이크 경계는 입력 경계이고, 입력이 비면 원점의 작은 상자다 */
SW_TEST_CASE( NavMeshAssetTest, BakeBoundsFollowTheGeometry )
{
    NavMeshGeometry geometry;
    const AABB      emptyBounds = NavMeshAsset::computeBakeBounds( geometry );
    SW_EXPECT_NEAR( -1.0f, emptyBounds._min._x, 0.0f );
    SW_EXPECT_NEAR( 1.0f, emptyBounds._max._z, 0.0f );

    geometry.addBox( float3{ 2.0f, 3.0f, 4.0f }, float4x4{}, 0 );
    const AABB bounds = NavMeshAsset::computeBakeBounds( geometry );
    SW_EXPECT_NEAR( -2.0f, bounds._min._x, 0.0001f );
    SW_EXPECT_NEAR( 3.0f, bounds._max._y, 0.0001f );
    SW_EXPECT_NEAR( 4.0f, bounds._max._z, 0.0001f );
}

/** @brief [NavMeshAssetTest] 실제 백엔드로 베이크 → 쿠킹본 → 읽기 → 끼우기: 끼운 쪽의 타일 · 폴리곤 수가 베이크한 쪽과 같다(쿠킹과 런타임 베이크가 같은 내비메시를 낸다) */
SW_TEST_CASE( NavMeshAssetTest, BakedTilesSurviveTheCookedFile )
{
    NavAgentTypeDef agentType;
    agentType._name = hashed_string{ "Walker" };
    NavAreaDef area;
    area._name = hashed_string{ "Walkable" };
    NavMeshSettings settings;
    settings._listAgentType.push_back( agentType );
    settings._listArea.push_back( area );
    settings._defaultAgentType = agentType._name;

    NavMeshGeometry geometry;
    geometry.addBox( float3{ 8.0f, 0.5f, 8.0f }, float4x4{}, 0 );
    const AABB bounds = NavMeshAsset::computeBakeBounds( geometry );

    unique_ptr<INavMesh> pBaked = NavMeshBackend::createNavMesh();
    SW_REQUIRE_NOT_NULL( pBaked );
    SW_REQUIRE_TRUE( pBaked->initialize( agentType, settings, bounds ) );
    NavMeshBakeStats stats;
    SW_REQUIRE_TRUE( NavMeshAsset::bakeAllTiles( *pBaked, geometry, nullptr, &stats ) );
    SW_EXPECT_EQUAL( 0u, stats._failedTileCount );
    SW_REQUIRE_TRUE( stats._filledTileCount > 0u );
    SW_REQUIRE_TRUE( pBaked->getPolygonCount() > 0u );
    SW_EXPECT_EQUAL( stats._polygonCount, pBaked->getPolygonCount() );

    NavMeshAssetEntry entry;
    entry._agentType = agentType._name;
    entry._bounds    = bounds;
    pBaked->collectTiles( entry._listTile );
    SW_EXPECT_EQUAL( stats._filledTileCount, static_cast<uint32>( entry._listTile.size() ) );
    NavMeshAsset cooked;
    cooked.setEntry( entry );
    vector<uint8> listByte;
    cooked.writeBytes( listByte );

    NavMeshAsset loaded;
    SW_REQUIRE_TRUE( loaded.readBytes( listByte.data(), listByte.size(), "test" ) );
    const NavMeshAssetEntry* pEntry = loaded.findEntry( agentType._name );
    SW_REQUIRE_NOT_NULL( pEntry );

    unique_ptr<INavMesh> pInstalled = NavMeshBackend::createNavMesh();
    SW_REQUIRE_NOT_NULL( pInstalled );
    SW_REQUIRE_TRUE( pInstalled->initialize( agentType, settings, pEntry->_bounds ) );
    SW_EXPECT_TRUE( NavMeshAsset::installTiles( *pInstalled, pEntry->_listTile ) );
    SW_EXPECT_EQUAL( pBaked->getLoadedTileCount(), pInstalled->getLoadedTileCount() );
    SW_EXPECT_EQUAL( pBaked->getPolygonCount(), pInstalled->getPolygonCount() );
}
