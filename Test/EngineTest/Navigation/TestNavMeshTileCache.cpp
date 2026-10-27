// 장애물 캐시(NavObstacleMode::TileCache) — 정적 기하를 높이 층으로 베이크하고, 장애물을 넣고 빼면 그 자리만 층에서 다시 짓는다.
#include "pch.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Object/Component/Navigation/NavMeshObstacleComponent.h"

#include "EngineTest/NavMeshTestUtil.h"

#include "TestFramework/TestFramework.h"

namespace
{
    struct TestNavMeshTileCacheInternal
    {
        /** @brief 테스트 표의 종류를 장애물 캐시로 바꾼 것입니다. */
        static sw::NavMeshSettings makeCacheSettings()
        {
            sw::NavMeshSettings settings                  = navtest::makeSettings();
            settings._listAgentType.front()._obstacleMode = sw::NavObstacleMode::TileCache;
            return settings;
        }

        /** @brief 쌓인 장애물 요청을 다 지을 때까지 돌립니다(갱신마다 타일 하나). */
        static void settle( sw::INavMesh& navMesh )
        {
            const int32 maxBuildCount = navMesh.getTileGrid().getTileCount() * static_cast<int32>( sw::NavigationConstant::kMaxCacheLayerCount ) + 1;
            int32       buildCount    = 0;
            while ( buildCount < maxBuildCount && navMesh.updateCacheObstacles( 0.0f ) )
            {
                ++buildCount;
            }
        }
    };
} // namespace

/**
 * @brief [NavMeshTileCacheTest] 장애물 캐시로 베이크한 바닥(20 m)도 걸을 수 있고, 가운데 상자 장애물을 넣으면 그 자리가 막히며, 빼면 다시 열린다
 */
SW_TEST_CASE( NavMeshTileCacheTest, ObstacleCarvesAndClearsWithoutRebaking )
{
    using Internal                     = TestNavMeshTileCacheInternal;
    const sw::NavMeshSettings settings = Internal::makeCacheSettings();
    sw::NavMeshGeometry       geometry;
    navtest::addFloor( geometry, -10.0f, -10.0f, 10.0f, 10.0f );
    sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
    SW_ASSERT_NOT_NULL( pNavMesh.get() );
    SW_ASSERT_TRUE( pNavMesh->getObstacleMode() == sw::NavObstacleMode::TileCache );
    SW_EXPECT_TRUE( pNavMesh->getPolygonCount() > 0 );

    const sw::float3         tightExtent{ 0.3f, 1.0f, 0.3f };
    const sw::NavQueryFilter filter = settings.makeDefaultFilter();
    sw::NavLocation          location;
    SW_ASSERT_TRUE( pNavMesh->findNearestPoint( sw::float3{ 0.0f, 0.0f, 0.0f }, tightExtent, filter, location ) );

    sw::NavCacheObstacleDesc box;
    box._center                       = sw::float3{ 0.0f, 0.5f, 0.0f };
    box._halfExtents                  = sw::float3{ 2.0f, 0.5f, 2.0f };
    const sw::NavObstacleRef obstacle = pNavMesh->addCacheObstacle( box );
    SW_ASSERT_TRUE( obstacle != 0 );
    const uint32 revisionBefore = pNavMesh->getRevision();
    Internal::settle( *pNavMesh );
    SW_EXPECT_TRUE( pNavMesh->getRevision() != revisionBefore );
    SW_EXPECT_FALSE( pNavMesh->findNearestPoint( sw::float3{ 0.0f, 0.0f, 0.0f }, tightExtent, filter, location ) );
    SW_EXPECT_TRUE( pNavMesh->findNearestPoint( sw::float3{ 6.0f, 0.0f, 6.0f }, tightExtent, filter, location ) );

    pNavMesh->removeCacheObstacle( obstacle );
    Internal::settle( *pNavMesh );
    SW_EXPECT_TRUE( pNavMesh->findNearestPoint( sw::float3{ 0.0f, 0.0f, 0.0f }, tightExtent, filter, location ) );
}

/**
 * @brief [NavMeshTileCacheTest] 장애물 캐시의 타일 바이트(층 묶음)를 베껴 새 내비메시에 끼우면 같은 걷는 면이 선다 — 쿠킹 · 읽기 길이 모드와 무관하게 같다
 */
SW_TEST_CASE( NavMeshTileCacheTest, LayerPacksRoundTripThroughInstall )
{
    using Internal                     = TestNavMeshTileCacheInternal;
    const sw::NavMeshSettings settings = Internal::makeCacheSettings();
    sw::NavMeshGeometry       geometry;
    navtest::addFloor( geometry, -10.0f, -10.0f, 10.0f, 10.0f );
    sw::unique_ptr<sw::INavMesh> pBaked = navtest::bake( settings, geometry );
    SW_ASSERT_NOT_NULL( pBaked.get() );
    sw::vector<sw::NavTileData> listPack;
    pBaked->collectTiles( listPack );
    SW_ASSERT_TRUE( listPack.empty() == false );

    sw::unique_ptr<sw::INavMesh> pLoaded = sw::NavMeshBackend::createNavMesh();
    SW_ASSERT_TRUE( pLoaded->initialize( settings._listAgentType.front(), settings, sw::NavMeshBakeUtil::computeBakeBounds( geometry ) ) );
    SW_ASSERT_TRUE( sw::NavMeshBakeUtil::installTiles( *pLoaded, listPack ) );
    SW_EXPECT_EQUAL( pBaked->getPolygonCount(), pLoaded->getPolygonCount() );
}

/**
 * @brief [NavMeshTileCacheTest] Y 축으로 돌린 오브젝트의 긴 상자 장애물은 오브젝트 +X 축 쪽을 막고 그와 직각인 쪽은 열어 둔다 — 요 부호(`computeBoxYaw` ↔ Detour yRadians)가 맞다
 * @details 회전 +40° 와 -40° 를 둘 다 본다(부호가 뒤집히면 막는 쪽이 거울로 바뀌어 둘 중 하나가 틀린다). 바닥 20 m, 상자는 반 길이 7 m · 반 폭 0.6 m.
 */
SW_TEST_CASE( NavMeshTileCacheTest, RotatedBoxFollowsObjectAxis )
{
    using Internal                     = TestNavMeshTileCacheInternal;
    const sw::NavMeshSettings settings = Internal::makeCacheSettings();
    sw::NavMeshGeometry       geometry;
    navtest::addFloor( geometry, -10.0f, -10.0f, 10.0f, 10.0f );
    const sw::float3         tightExtent{ 0.3f, 1.0f, 0.3f };
    const sw::NavQueryFilter filter = settings.makeDefaultFilter();

    for ( const float32 degrees : { 40.0f, -40.0f } )
    {
        sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
        SW_ASSERT_NOT_NULL( pNavMesh.get() );
        const sw::float4x4       world = sw::float4x4::makeRotationY( sw::MathUtil::toRadian( degrees ) );
        sw::NavCacheObstacleDesc box;
        box._center      = sw::float3{ 0.0f, 0.5f, 0.0f };
        box._halfExtents = sw::float3{ 7.0f, 0.5f, 0.6f };
        box._yaw         = sw::NavMeshObstacleComponent::computeBoxYaw( world );
        SW_ASSERT_TRUE( pNavMesh->addCacheObstacle( box ) != 0 );
        Internal::settle( *pNavMesh );

        // 오브젝트 +X 축 위 4 m 는 상자 안이라 막히고, 그 축에 직각인 4 m 는 상자 밖이라 걸을 수 있다.
        const sw::float3 axisX = sw::float3::transformVector( sw::float3{ 1.0f, 0.0f, 0.0f }, world );
        const sw::float3 alongAxis{ axisX._x * 4.0f, 0.0f, axisX._z * 4.0f };
        const sw::float3 acrossAxis{ -axisX._z * 4.0f, 0.0f, axisX._x * 4.0f };
        sw::NavLocation  location;
        SW_EXPECT_FALSE( pNavMesh->findNearestPoint( alongAxis, tightExtent, filter, location ) );
        SW_EXPECT_TRUE( pNavMesh->findNearestPoint( acrossAxis, tightExtent, filter, location ) );
    }
}

/**
 * @brief [NavMeshTileCacheTest] 다시 베이크 종류에 캐시 장애물을 넣으면 오류이고 0 이다 — 방식이 섞이지 않는다
 */
SW_TEST_CASE( NavMeshTileCacheTest, RebakeModeRejectsCacheObstacles )
{
    const sw::NavMeshSettings settings = navtest::makeSettings();
    sw::NavMeshGeometry       geometry;
    navtest::addFloor( geometry, -5.0f, -5.0f, 5.0f, 5.0f );
    sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
    SW_ASSERT_NOT_NULL( pNavMesh.get() );
    SW_EXPECT_EQUAL( sw::NavObstacleRef{ 0 }, pNavMesh->addCacheObstacle( sw::NavCacheObstacleDesc{} ) );
    SW_EXPECT_FALSE( pNavMesh->updateCacheObstacles( 0.0f ) );
}
