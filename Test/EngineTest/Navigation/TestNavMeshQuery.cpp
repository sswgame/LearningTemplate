// 내비메시 질의 — 벽을 돌아가는 경로, 닿을 수 없는 섬(부분 경로) · 내비메시 밖 끝(실패), 레이캐스트(시선), 영역 비용 · 막을 영역.
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"

#include "EngineTest/NavMeshTestUtil.h"

#include "TestFramework/TestFramework.h"

namespace
{
    struct NavMeshQueryTestInternal
    {
        /** @brief 20 × 20 바닥, x 9..10 에 높이 2 m 벽(z 0..15) — 위쪽 z 15..20 이 틈이다. */
        static void addWallScene( sw::NavMeshGeometry& geometry )
        {
            navtest::addFloor( geometry, 0.0f, 0.0f, 20.0f, 20.0f );
            navtest::addBox( geometry, sw::float3{ 9.0f, 0.0f, 0.0f }, sw::float3{ 10.0f, 2.0f, 15.0f } );
        }

        /** @brief 경로의 어느 선분도 벽 발자국(x 9..10, z 0..15)을 지나지 않으면 true 입니다(선분을 잘게 나눠 본다). */
        static bool isPathClearOfWall( const sw::NavPath& path )
        {
            for ( size_t pointIndex = 1; pointIndex < path._listPoint.size(); ++pointIndex )
            {
                const sw::float3& from = path._listPoint[pointIndex - 1];
                const sw::float3& to   = path._listPoint[pointIndex];
                for ( uint32 sample = 0; sample <= 32; ++sample )
                {
                    const sw::float3 point = from + ( to - from ) * ( static_cast<float32>( sample ) / 32.0f );
                    if ( 9.0f <= point._x && point._x <= 10.0f && point._z <= 15.0f )
                        return false;
                }
            }
            return true;
        }

        /** @brief 경로 점의 가장 큰 z 입니다. */
        static float32 computeMaxZ( const sw::NavPath& path )
        {
            float32 maxZ = -1.0e9f;
            for ( const sw::float3& point : path._listPoint )
            {
                maxZ = sw::MathUtil::max( maxZ, point._z );
            }
            return maxZ;
        }
    };
} // namespace

/**
 * @brief [NavMeshQueryTest] 벽 너머로 가는 경로는 틈으로 돌아가고(완전 경로), 다듬은 모퉁이 사이 선분이 벽을 지나지 않는다
 */
SW_TEST_CASE( NavMeshQueryTest, PathGoesAroundAWall )
{
    using Internal                     = NavMeshQueryTestInternal;
    const sw::NavMeshSettings settings = navtest::makeSettings();
    sw::NavMeshGeometry       geometry;
    Internal::addWallScene( geometry );
    sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
    SW_ASSERT_NOT_NULL( pNavMesh.get() );
    sw::NavPath path;
    SW_ASSERT_TRUE( pNavMesh->findPath( sw::float3{ 4.0f, 0.0f, 4.0f }, sw::float3{ 16.0f, 0.0f, 4.0f }, navtest::makeExtent(), settings.makeDefaultFilter(),
                                        path ) == sw::NavPathStatus::Complete );
    SW_EXPECT_TRUE( path._listPoint.size() >= 4 );
    SW_EXPECT_TRUE( path.computeLength() > 20.0f );
    SW_EXPECT_TRUE( Internal::computeMaxZ( path ) > 15.0f );
    SW_EXPECT_TRUE( Internal::isPathClearOfWall( path ) );
}

/**
 * @brief [NavMeshQueryTest] 4 m 틈 건너 섬으로 가는 경로는 닿을 수 있는 가장 가까운 곳까지의 부분 경로이고, 내비메시 밖의 끝은 실패다
 */
SW_TEST_CASE( NavMeshQueryTest, UnreachableIslandGivesAPartialPath )
{
    const sw::NavMeshSettings settings = navtest::makeSettings();
    const sw::NavQueryFilter  filter   = settings.makeDefaultFilter();
    sw::NavMeshGeometry       geometry;
    navtest::addFloor( geometry, 0.0f, 0.0f, 8.0f, 20.0f );
    navtest::addFloor( geometry, 12.0f, 0.0f, 20.0f, 20.0f );
    sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
    SW_ASSERT_NOT_NULL( pNavMesh.get() );
    sw::NavPath path;
    SW_EXPECT_TRUE( pNavMesh->findPath( sw::float3{ 2.0f, 0.0f, 10.0f }, sw::float3{ 16.0f, 0.0f, 10.0f }, navtest::makeExtent(), filter, path ) ==
                    sw::NavPathStatus::Partial );
    SW_ASSERT_FALSE( path._listPoint.empty() );
    SW_EXPECT_TRUE( path._listPoint.back()._x < 8.0f );
    SW_EXPECT_TRUE( path._listPoint.back()._x > 6.5f );
    SW_EXPECT_TRUE( pNavMesh->findPath( sw::float3{ 2.0f, 0.0f, 10.0f }, sw::float3{ 60.0f, 0.0f, 10.0f }, navtest::makeExtent(), filter, path ) ==
                    sw::NavPathStatus::Failed );
}

/**
 * @brief [NavMeshQueryTest] 레이캐스트(시선)는 벽 앞(몸 반지름만큼 깎인 경계)에서 막히고, 틈을 지나는 선은 막히지 않는다
 */
SW_TEST_CASE( NavMeshQueryTest, RaycastStopsAtTheWall )
{
    const sw::NavMeshSettings settings = navtest::makeSettings();
    const sw::NavQueryFilter  filter   = settings.makeDefaultFilter();
    sw::NavMeshGeometry       geometry;
    NavMeshQueryTestInternal::addWallScene( geometry );
    sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
    SW_ASSERT_NOT_NULL( pNavMesh.get() );
    sw::NavRaycastHit hit;
    SW_ASSERT_TRUE( pNavMesh->raycast( sw::float3{ 4.0f, 0.0f, 4.0f }, sw::float3{ 16.0f, 0.0f, 4.0f }, navtest::makeExtent(), filter, hit ) );
    SW_EXPECT_TRUE( hit._bHit );
    SW_EXPECT_NEAR_EQUAL( 8.6f, hit._position._x, 0.35f );
    SW_EXPECT_TRUE( hit._normal._x < -0.5f );
    SW_ASSERT_TRUE( pNavMesh->raycast( sw::float3{ 4.0f, 0.0f, 17.5f }, sw::float3{ 16.0f, 0.0f, 17.5f }, navtest::makeExtent(), filter, hit ) );
    SW_EXPECT_FALSE( hit._bHit );
    SW_EXPECT_NEAR_EQUAL( 16.0f, hit._position._x, 1.0e-3f );
}

/**
 * @brief [NavMeshQueryTest] 영역 비용 — 물(비용 10) 띠를 가로지르지 않고 돌아가고, 물 비용을 1 로 두면 곧게 가고, 물을 막으면 다시 돌아간다
 */
SW_TEST_CASE( NavMeshQueryTest, AreaCostsAndExcludedAreasShapeThePath )
{
    using Internal                     = NavMeshQueryTestInternal;
    const sw::NavMeshSettings settings = navtest::makeSettings();
    uint8                     water    = 0;
    SW_ASSERT_TRUE( settings.findAreaIndex( sw::hashed_string( "Water" ), water ) );
    sw::NavMeshGeometry geometry;
    navtest::addFloor( geometry, 0.0f, 0.0f, 8.0f, 20.0f );
    navtest::addFloor( geometry, 12.0f, 0.0f, 20.0f, 20.0f );
    navtest::addFloor( geometry, 8.0f, 0.0f, 12.0f, 14.0f, water );
    navtest::addFloor( geometry, 8.0f, 14.0f, 12.0f, 20.0f );
    sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
    SW_ASSERT_NOT_NULL( pNavMesh.get() );
    const sw::float3 start{ 4.0f, 0.0f, 4.0f };
    const sw::float3 end{ 16.0f, 0.0f, 4.0f };

    sw::NavPath detour;
    SW_ASSERT_TRUE( pNavMesh->findPath( start, end, navtest::makeExtent(), settings.makeDefaultFilter(), detour ) == sw::NavPathStatus::Complete );
    SW_EXPECT_TRUE( Internal::computeMaxZ( detour ) > 13.5f );

    sw::NavQueryFilter cheapWater  = settings.makeDefaultFilter();
    cheapWater._arrAreaCost[water] = 1.0f;
    sw::NavPath straight;
    SW_ASSERT_TRUE( pNavMesh->findPath( start, end, navtest::makeExtent(), cheapWater, straight ) == sw::NavPathStatus::Complete );
    SW_EXPECT_NEAR_EQUAL( 12.0f, straight.computeLength(), 0.1f );

    sw::NavQueryFilter noWater = cheapWater;
    noWater.setAreaAllowed( water, false );
    sw::NavPath blocked;
    SW_ASSERT_TRUE( pNavMesh->findPath( start, end, navtest::makeExtent(), noWater, blocked ) == sw::NavPathStatus::Complete );
    SW_EXPECT_TRUE( Internal::computeMaxZ( blocked ) > 13.5f );
}
