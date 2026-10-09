// 내비메시 베이크 — 평평한 바닥 · 계단(오를 수 있는 높이와 없는 높이) · 경사(최대 경사 안과 밖) · 구멍, 쿠킹본 왕복 · 결정성, 설정 표 검사.
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"

#include "EngineTest/NavMeshTestUtil.h"

#include "TestFramework/TestFramework.h"

namespace
{
    struct NavMeshBakeTestInternal
    {
        /** @brief x 축으로 오르는 경사면(폭 4 m, 길이 @p length, 높이 @p height) — 아래 바닥 · 위 단과 이어진다. */
        static void addRampScene( sw::NavMeshGeometry& geometry, float32 length, float32 height )
        {
            navtest::addFloor( geometry, -6.0f, 0.0f, 0.0f, 4.0f );
            const sw::float3 arrPoint[4] = {
                sw::float3{  0.0f,   0.0f, 0.0f},
                sw::float3{  0.0f,   0.0f, 4.0f},
                sw::float3{length, height, 4.0f},
                sw::float3{length, height, 0.0f},
            };
            const uint32 arrIndex[6] = { 0, 1, 2, 0, 2, 3 };
            geometry.addTriangles( arrPoint, 4, arrIndex, 6, sw::float4x4{}, 0 );
            navtest::addBox( geometry, sw::float3{ length, height - 0.2f, 0.0f }, sw::float3{ length + 6.0f, height, 4.0f } );
        }
    };
} // namespace

/**
 * @brief [NavMeshBakeTest] 평평한 바닥 하나 — 걸을 폴리곤이 생기고, 바닥 위의 점을 찾고, 끝에서 끝까지 곧은 경로(점 둘)가 나며, 가장자리는 몸 반지름만큼 깎인다
 */
SW_TEST_CASE( NavMeshBakeTest, FlatFloorBakesOneConnectedSurface )
{
    const sw::NavMeshSettings settings = navtest::makeSettings();
    sw::NavMeshGeometry       geometry;
    navtest::addFloor( geometry, 0.0f, 0.0f, 20.0f, 20.0f );
    sw::NavMeshBakeStats         stats;
    sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry, nullptr, &stats );
    SW_ASSERT_NOT_NULL( pNavMesh.get() );
    SW_EXPECT_TRUE( stats._polygonCount > 0 );
    SW_EXPECT_EQUAL( 0u, stats._failedTileCount );
    SW_EXPECT_TRUE( stats._filledTileCount + 1 >= stats._tileCount ); // 모서리 타일은 깎이고 나면 작은 섬이라 빠질 수 있다

    const sw::NavQueryFilter filter = settings.makeDefaultFilter();
    sw::NavLocation          location;
    SW_ASSERT_TRUE( pNavMesh->findNearestPoint( sw::float3{ 10.0f, 1.0f, 10.0f }, navtest::makeExtent(), filter, location ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, location._position._y, 0.15f );

    // 가장자리 바로 위의 점은 몸 반지름(0.4)만큼 안쪽으로 붙는다.
    SW_ASSERT_TRUE( pNavMesh->findNearestPoint( sw::float3{ 0.05f, 0.0f, 10.0f }, navtest::makeExtent(), filter, location ) );
    SW_EXPECT_TRUE( location._position._x >= 0.3f );

    sw::NavPath path;
    SW_EXPECT_TRUE( pNavMesh->findPath( sw::float3{ 2.0f, 0.0f, 2.0f }, sw::float3{ 18.0f, 0.0f, 18.0f }, navtest::makeExtent(), filter, path ) ==
                    sw::NavPathStatus::Complete );
    SW_EXPECT_EQUAL( size_t{ 2 }, path._listPoint.size() );
    SW_EXPECT_NEAR_EQUAL( 16.0f * 1.41421356f, path.computeLength(), 0.2f );
}

/**
 * @brief [NavMeshBakeTest] 계단 — 0.3 m 단 넷은 오르고(계단 0.45), 바닥에서 1 m 턱은 오르지 못해 경로가 부분 경로다
 */
SW_TEST_CASE( NavMeshBakeTest, StepsWithinClimbHeightAreWalkableAndTallLedgesAreNot )
{
    const sw::NavMeshSettings settings = navtest::makeSettings();
    const sw::NavQueryFilter  filter   = settings.makeDefaultFilter();
    {
        sw::NavMeshGeometry geometry;
        navtest::addFloor( geometry, 0.0f, 0.0f, 10.0f, 10.0f );
        for ( uint32 stepIndex = 0; stepIndex < 4; ++stepIndex )
        {
            const float32 x0 = 10.0f + static_cast<float32>( stepIndex );
            navtest::addBox( geometry, sw::float3{ x0, -0.2f, 0.0f }, sw::float3{ x0 + 1.0f, 0.3f * static_cast<float32>( stepIndex + 1 ), 10.0f } );
        }
        navtest::addBox( geometry, sw::float3{ 14.0f, -0.2f, 0.0f }, sw::float3{ 20.0f, 1.2f, 10.0f } );
        sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
        SW_ASSERT_NOT_NULL( pNavMesh.get() );
        sw::NavPath path;
        SW_EXPECT_TRUE( pNavMesh->findPath( sw::float3{ 2.0f, 0.0f, 5.0f }, sw::float3{ 17.0f, 1.2f, 5.0f }, navtest::makeExtent(), filter, path ) ==
                        sw::NavPathStatus::Complete );
        SW_ASSERT_FALSE( path._listPoint.empty() );
        SW_EXPECT_NEAR_EQUAL( 1.2f, path._listPoint.back()._y, 0.15f );
    }
    {
        sw::NavMeshGeometry geometry;
        navtest::addFloor( geometry, 0.0f, 0.0f, 10.0f, 10.0f );
        navtest::addBox( geometry, sw::float3{ 10.0f, -0.2f, 0.0f }, sw::float3{ 16.0f, 1.0f, 10.0f } );
        sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
        SW_ASSERT_NOT_NULL( pNavMesh.get() );
        sw::NavPath path;
        SW_EXPECT_TRUE( pNavMesh->findPath( sw::float3{ 2.0f, 0.0f, 5.0f }, sw::float3{ 13.0f, 1.0f, 5.0f }, navtest::makeExtent(), filter, path ) ==
                        sw::NavPathStatus::Partial );
        SW_ASSERT_FALSE( path._listPoint.empty() );
        SW_EXPECT_TRUE( path._listPoint.back()._x < 10.0f );
    }
}

/**
 * @brief [NavMeshBakeTest] 경사 — 30 도 경사는 걸어 올라 위 단까지 가고, 60 도 경사(최대 45 도)는 걸을 면이 아니라 위 단에 닿지 못한다
 */
SW_TEST_CASE( NavMeshBakeTest, SlopesSteeperThanTheMaximumAreNotWalkable )
{
    using Internal                     = NavMeshBakeTestInternal;
    const sw::NavMeshSettings settings = navtest::makeSettings();
    const sw::NavQueryFilter  filter   = settings.makeDefaultFilter();
    const float32             gentle   = 4.0f * sw::MathUtil::tan( 30.0f * sw::MathUtil::kPi / 180.0f );
    const float32             steep    = 4.0f * sw::MathUtil::tan( 60.0f * sw::MathUtil::kPi / 180.0f );
    {
        sw::NavMeshGeometry geometry;
        Internal::addRampScene( geometry, 4.0f, gentle );
        sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
        SW_ASSERT_NOT_NULL( pNavMesh.get() );
        sw::NavPath path;
        SW_EXPECT_TRUE( pNavMesh->findPath( sw::float3{ -3.0f, 0.0f, 2.0f }, sw::float3{ 7.0f, gentle, 2.0f }, navtest::makeExtent(), filter, path ) ==
                        sw::NavPathStatus::Complete );
    }
    {
        sw::NavMeshGeometry geometry;
        Internal::addRampScene( geometry, 4.0f, steep );
        sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
        SW_ASSERT_NOT_NULL( pNavMesh.get() );
        sw::NavLocation onRamp;
        SW_EXPECT_FALSE( pNavMesh->findNearestPoint( sw::float3{ 2.0f, steep * 0.5f, 2.0f }, sw::float3{ 0.3f, 0.3f, 0.3f }, filter, onRamp ) );
        sw::NavPath path;
        SW_EXPECT_TRUE( pNavMesh->findPath( sw::float3{ -3.0f, 0.0f, 2.0f }, sw::float3{ 7.0f, steep, 2.0f }, navtest::makeExtent(), filter, path ) ==
                        sw::NavPathStatus::Partial );
    }
}

/**
 * @brief [NavMeshBakeTest] 바닥의 구멍(2 m × 2 m) — 구멍 위에는 내비메시가 없고, 구멍을 가로지르는 경로는 돌아간다
 */
SW_TEST_CASE( NavMeshBakeTest, HoleInTheFloorIsLeftOutAndPathsGoAround )
{
    const sw::NavMeshSettings settings = navtest::makeSettings();
    const sw::NavQueryFilter  filter   = settings.makeDefaultFilter();
    sw::NavMeshGeometry       geometry;
    navtest::addFloor( geometry, 0.0f, 0.0f, 20.0f, 9.0f );
    navtest::addFloor( geometry, 0.0f, 11.0f, 20.0f, 20.0f );
    navtest::addFloor( geometry, 0.0f, 9.0f, 9.0f, 11.0f );
    navtest::addFloor( geometry, 11.0f, 9.0f, 20.0f, 11.0f );
    sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
    SW_ASSERT_NOT_NULL( pNavMesh.get() );
    sw::NavLocation inHole;
    SW_EXPECT_FALSE( pNavMesh->findNearestPoint( sw::float3{ 10.0f, 0.0f, 10.0f }, sw::float3{ 0.3f, 1.0f, 0.3f }, filter, inHole ) );
    sw::NavPath path;
    SW_EXPECT_TRUE( pNavMesh->findPath( sw::float3{ 10.0f, 0.0f, 5.0f }, sw::float3{ 10.0f, 0.0f, 15.0f }, navtest::makeExtent(), filter, path ) ==
                    sw::NavPathStatus::Complete );
    SW_EXPECT_TRUE( path._listPoint.size() > 2 );
    SW_EXPECT_TRUE( path.computeLength() > 10.1f );
}

/**
 * @brief [NavMeshBakeTest] 쿠킹본 — 두 번 베이크한 타일은 바이트까지 같고, `.navmesh` 로 쓰고 읽어 끼운 내비메시는 같은 폴리곤 수 · 같은 경로를 낸다. 매직이 다르거나 잘린 바이트는 거절한다
 */
SW_TEST_CASE( NavMeshBakeTest, CookedAssetRoundTripsTilesByteForByte )
{
    const sw::NavMeshSettings settings = navtest::makeSettings();
    const sw::NavQueryFilter  filter   = settings.makeDefaultFilter();
    sw::NavMeshGeometry       geometry;
    navtest::addFloor( geometry, 0.0f, 0.0f, 20.0f, 20.0f );
    navtest::addBox( geometry, sw::float3{ 8.0f, 0.0f, 0.0f }, sw::float3{ 9.0f, 2.0f, 15.0f } );
    sw::unique_ptr<sw::INavMesh> pFirst  = navtest::bake( settings, geometry );
    sw::unique_ptr<sw::INavMesh> pSecond = navtest::bake( settings, geometry );
    SW_ASSERT_TRUE( pFirst != nullptr && pSecond != nullptr );
    sw::vector<sw::NavTileData> listFirst;
    sw::vector<sw::NavTileData> listSecond;
    pFirst->collectTiles( listFirst );
    pSecond->collectTiles( listSecond );
    SW_ASSERT_EQUAL( listFirst.size(), listSecond.size() );
    for ( size_t tileIndex = 0; tileIndex < listFirst.size(); ++tileIndex )
    {
        SW_EXPECT_TRUE( listFirst[tileIndex]._bytes == listSecond[tileIndex]._bytes );
    }

    sw::NavMeshAsset      asset;
    sw::NavMeshAssetEntry entry;
    entry._agentType    = settings._listAgentType.front()._name;
    entry._settingsHash = sw::NavMeshSettings::computeAgentTypeHash( settings._listAgentType.front() );
    entry._inputHash    = geometry.computeHash();
    entry._bounds       = sw::NavMeshBakeUtil::computeBakeBounds( geometry );
    entry._listTile     = listFirst;
    asset.setEntry( entry );
    sw::vector<uint8> bytes;
    asset.writeBytes( bytes );

    sw::NavMeshAsset readBack;
    SW_ASSERT_TRUE( readBack.readBytes( bytes.data(), bytes.size(), "memory" ) );
    const sw::NavMeshAssetEntry* pEntry = readBack.findEntry( entry._agentType );
    SW_ASSERT_NOT_NULL( pEntry );
    SW_EXPECT_EQUAL( entry._inputHash, pEntry->_inputHash );
    sw::unique_ptr<sw::INavMesh> pLoaded = sw::NavMeshBackend::createNavMesh();
    SW_ASSERT_TRUE( pLoaded->initialize( settings._listAgentType.front(), settings, pEntry->_bounds ) );
    SW_ASSERT_TRUE( sw::NavMeshBakeUtil::installTiles( *pLoaded, pEntry->_listTile ) );
    SW_EXPECT_EQUAL( pFirst->getPolygonCount(), pLoaded->getPolygonCount() );
    sw::NavPath original;
    sw::NavPath loaded;
    (void)pFirst->findPath( sw::float3{ 4.0f, 0.0f, 4.0f }, sw::float3{ 14.0f, 0.0f, 4.0f }, navtest::makeExtent(), filter, original );
    (void)pLoaded->findPath( sw::float3{ 4.0f, 0.0f, 4.0f }, sw::float3{ 14.0f, 0.0f, 4.0f }, navtest::makeExtent(), filter, loaded );
    SW_ASSERT_EQUAL( original._listPoint.size(), loaded._listPoint.size() );
    SW_EXPECT_NEAR_EQUAL( original.computeLength(), loaded.computeLength(), 1.0e-4f );

    {
        test::ScopedDefensiveTestLog expected( "a navmesh with the wrong magic and a truncated one are refused" );
        sw::vector<uint8>            broken = bytes;
        broken[0]                           = 0;
        SW_EXPECT_FALSE( readBack.readBytes( broken.data(), broken.size(), "broken" ) );
        SW_EXPECT_FALSE( readBack.readBytes( bytes.data(), bytes.size() / 2, "truncated" ) );
    }
    SW_EXPECT_TRUE( sw::NavMeshAsset::makeCookedPath( "game/shooter3d/maps/Arena.scene.xml" ) == "game/shooter3d/maps/arena.navmesh" );
    SW_EXPECT_TRUE( sw::NavMeshAsset::makeCookedPath( "game/shooter3d/maps/arena.scene.bin" ) == "game/shooter3d/maps/arena.navmesh" );
    SW_EXPECT_TRUE( sw::NavMeshAsset::makeCookedPath( "game/shooter3d/models/crate.mesh" ).empty() );
}

/**
 * @brief [NavMeshBakeTest] 설정 표 — 이름 · 영역 비용 · 기본 종류를 읽고, 겹친 영역 · 모르는 기본 종류 · 범위 밖 값 · 모르는 키는 거절한다
 */
SW_TEST_CASE( NavMeshBakeTest, SettingsTableValidatesNamesAndRanges )
{
    sw::NavMeshSettings settings;
    SW_ASSERT_TRUE( settings.loadFromXmlText( "<NavMeshSettings _defaultAgentType=\"Small\"><_listAgentType>"
                                              "<NavAgentTypeDef _name=\"Big\" _radius=\"1\" /><NavAgentTypeDef _name=\"Small\" _radius=\"0.3\" />"
                                              "</_listAgentType><_listArea><NavAreaDef _name=\"Default\" /><NavAreaDef _name=\"Mud\" _cost=\"5\" />"
                                              "</_listArea></NavMeshSettings>" ) );
    const sw::NavAgentTypeDef* pDefault = settings.findAgentType( sw::hashed_string{} );
    SW_ASSERT_NOT_NULL( pDefault );
    SW_EXPECT_TRUE( pDefault->_name == sw::hashed_string( "Small" ) );
    uint8 mudIndex = 0;
    SW_ASSERT_TRUE( settings.findAreaIndex( sw::hashed_string( "Mud" ), mudIndex ) );
    SW_EXPECT_EQUAL( uint8{ 1 }, mudIndex );
    SW_EXPECT_NEAR_EQUAL( 5.0f, settings.makeDefaultFilter()._arrAreaCost[1], 1.0e-6f );
    SW_EXPECT_TRUE( sw::NavMeshSettings::computeAgentTypeHash( settings._listAgentType[0] ) !=
                    sw::NavMeshSettings::computeAgentTypeHash( settings._listAgentType[1] ) );

    test::ScopedDefensiveTestLog expected( "broken navigation tables are refused" );
    sw::NavMeshSettings          broken;
    SW_EXPECT_FALSE( broken.loadFromXmlText( "<NavMeshSettings><_listArea><NavAreaDef _name=\"A\" /><NavAreaDef _name=\"A\" /></_listArea></NavMeshSettings>" ) );
    SW_EXPECT_FALSE( broken.loadFromXmlText( "<NavMeshSettings _defaultAgentType=\"Nobody\" />" ) );
    SW_EXPECT_FALSE( broken.loadFromXmlText( "<NavMeshSettings><_listAgentType><NavAgentTypeDef _name=\"Odd\" _height=\"0\" /></_listAgentType></NavMeshSettings>" ) );
    SW_EXPECT_FALSE( broken.loadFromXmlText( "<NavMeshSettings _noSuchKey=\"1\" />" ) );
}
