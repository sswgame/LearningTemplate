// 지형 높이장을 내비메시 베이크 기하로 — 높이장 삼각형(구멍 셀 제외)과, 씬의 지형 컴포넌트가 메시 · 강체 없이도 기하 모으기에 드는 것.
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Object/Component/Navigation/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/NavMeshTestUtil.h"

#include "TestFramework/TestFramework.h"

namespace
{
    struct TestNavMeshTerrainInternal
    {
        /** @brief 높이장 한 변의 샘플 수(셀 16 개, 1 m)입니다. */
        static constexpr uint32 kResolution = 17;
        /** @brief 가운데 구멍 셀 범위(양 끝 포함)입니다. */
        static constexpr uint32 kHoleFirst = 6;
        static constexpr uint32 kHoleLast  = 9;

        static bool isHoleCell( uint32 cellX, uint32 cellZ )
        {
            return kHoleFirst <= cellX && cellX <= kHoleLast && kHoleFirst <= cellZ && cellZ <= kHoleLast;
        }
    };
} // namespace

/**
 * @brief [NavMeshTerrainTest] 평평한 높이장(16 m)을 베이크하면 걷는 면이 생기고, 구멍 셀(가운데 4 m)은 걷지 않으며, 구멍을 가로지르는 경로는 돌아간다
 */
SW_TEST_CASE( NavMeshTerrainTest, HeightfieldBakesGroundAndSkipsHoles )
{
    using Internal                     = TestNavMeshTerrainInternal;
    const sw::NavMeshSettings settings = navtest::makeSettings();
    sw::vector<float32>       listHeight( Internal::kResolution * Internal::kResolution, 0.0f );
    sw::vector<uint8>         listHole( ( Internal::kResolution - 1 ) * ( Internal::kResolution - 1 ), 0u );
    for ( uint32 cellZ = 0; cellZ + 1 < Internal::kResolution; ++cellZ )
    {
        for ( uint32 cellX = 0; cellX + 1 < Internal::kResolution; ++cellX )
        {
            listHole[cellZ * ( Internal::kResolution - 1 ) + cellX] = Internal::isHoleCell( cellX, cellZ ) ? 1u : 0u;
        }
    }
    sw::NavMeshGeometry geometry;
    geometry.addHeightfield( listHeight.data(), Internal::kResolution, sw::float3{}, sw::float2{ 1.0f, 1.0f }, listHole.data(), 0 );
    SW_EXPECT_EQUAL( ( 16u * 16u - 16u ) * 2u, geometry.getTriangleCount() );
    sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
    SW_ASSERT_NOT_NULL( pNavMesh.get() );
    SW_EXPECT_TRUE( pNavMesh->getPolygonCount() > 0 );

    const sw::float3         tightExtent{ 0.3f, 1.0f, 0.3f };
    const sw::NavQueryFilter filter = settings.makeDefaultFilter();
    sw::NavLocation          location;
    SW_EXPECT_TRUE( pNavMesh->findNearestPoint( sw::float3{ 2.0f, 0.0f, 2.0f }, tightExtent, filter, location ) );
    SW_EXPECT_FALSE( pNavMesh->findNearestPoint( sw::float3{ 8.0f, 0.0f, 8.0f }, tightExtent, filter, location ) );

    sw::NavPath path;
    SW_ASSERT_TRUE( pNavMesh->findPath( sw::float3{ 2.0f, 0.0f, 8.0f }, sw::float3{ 14.0f, 0.0f, 8.0f }, navtest::makeExtent(), filter, path ) ==
                    sw::NavPathStatus::Complete );
    SW_EXPECT_TRUE_MSG( path.computeLength() > 12.5f, ( "path length " + std::to_string( path.computeLength() ) ).c_str() );
}

/**
 * @brief [NavMeshTerrainTest] 씬의 지형 컴포넌트는 메시 · 강체가 없어도 기하 모으기에 들어 내비메시가 생긴다(등록 종류 `SW_NAV_GEOMETRY_CONTRIBUTOR`)
 */
SW_TEST_CASE( NavMeshTerrainTest, TerrainComponentIsCollectedWithoutMeshes )
{
    sw::HeightfieldData data;
    data._resolution = 33;
    data._listHeight.assign( 33 * 33, 0 );
    const sw::string path = test::makeTempPath( "navmesh.heightfield" );
    SW_ASSERT_TRUE( data.saveToFile( path ) );

    sw::GameObjectManager manager;
    sw::SceneNavigation&  navigation = manager.getSceneService<sw::SceneNavigation>();
    navigation.setSettings( navtest::makeSettings() );
    sw::GameObject*       pObject  = manager.createGameObject( sw::hashed_string( "Terrain" ) );
    sw::TerrainComponent* pTerrain = pObject->addComponent<sw::TerrainComponent>();
    pTerrain->setHeightfieldPath( path );
    pTerrain->setSize( sw::float2{ 32.0f, 32.0f } );
    pTerrain->setHeightRange( 0.0f, 4.0f );
    pTerrain->setChunkCells( 16 );
    pTerrain->setMaterialPath( "" );
    SW_ASSERT_TRUE( pTerrain->reloadTerrain() );
    navtest::spawnSurface( manager, sw::NavGeometrySource::RenderMeshes );

    sw::NavMeshGeometry geometry;
    sw::SceneNavigation::collectGeometry( manager, nullptr, navigation.getSettings(), navigation.getGeometrySources(), geometry );
    SW_EXPECT_EQUAL( 32u * 32u * 2u, geometry.getTriangleCount() );

    const sw::INavMesh* pNavMesh = navigation.ensureNavMesh( sw::hashed_string( "TestHumanoid" ) );
    SW_ASSERT_NOT_NULL( pNavMesh );
    SW_EXPECT_TRUE( pNavMesh->getPolygonCount() > 0 );
    sw::NavLocation location;
    SW_EXPECT_TRUE( navigation.findNearestPoint( sw::hashed_string( "TestHumanoid" ), sw::float3{ 16.0f, 0.0f, 16.0f }, location ) );
}
