// 내비메시 벤치 — 쇼케이스 · Shooter3D 아레나의 베이크 시간, 경로 · 레이캐스트 · 가까운 점 1000 번, 군중 100 · 500 에이전트의 갱신 한 번. 값은 Release 로 읽는다.
#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneNavigation.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_LOG_CALLER( "NavMeshBench" );

namespace
{
    struct NavMeshBenchTestInternal
    {
        static constexpr const utf8* kArenaPath    = "game/shooter3d/maps/arena.scene.xml";
        static constexpr const utf8* kShowcasePath = "game/empty/maps/destructionshowcase.scene.xml";
        static constexpr uint32      kBakeRound    = 5;
        static constexpr uint32      kQueryCount   = 1000;

        /** @brief 같은 씨앗이면 같은 수열(시험이 기계마다 같은 점을 쓴다). */
        struct Random
        {
            uint64 _state{ sw::HashUtil::kGoldenRatio64 };

            float32 nextUnit()
            {
                _state = _state * 6364136223846793005ull + 1442695040888963407ull;
                return static_cast<float32>( ( _state >> 40 ) & 0xFFFFFF ) / static_cast<float32>( 0xFFFFFF );
            }
            sw::float3 nextPoint( float32 halfSize ) { return sw::float3{ ( nextUnit() * 2.0f - 1.0f ) * halfSize, 0.0f, ( nextUnit() * 2.0f - 1.0f ) * halfSize }; }
        };

        [[nodiscard]] static bool instantiate( sw::Scene& scene, const utf8* pPath )
        {
            sw::SceneDocument document;
            return document.loadXml( pPath ) && scene.instantiate( document );
        }

        /** @brief 씬 기하를 한 번 모으고 새 내비메시에 @p kBakeRound 번 베이크해 판마다 걸린 시간(us)을 모읍니다. */
        static bool measureBake( const utf8* pPath, sw::vector<int64>& outListSample, sw::NavMeshBakeStats& outStats )
        {
            sw::Scene scene{ "NavBench" };
            if ( instantiate( scene, pPath ) == false )
                return false;
            sw::SceneNavigation&       navigation = scene.getObjectManager()->getSceneNavigation();
            const sw::NavMeshSettings& settings   = navigation.getSettings();
            const sw::NavAgentTypeDef* pType      = settings.findAgentType( sw::hashed_string{} );
            if ( pType == nullptr )
                return false;
            sw::NavMeshGeometry geometry;
            sw::SceneNavigation::collectGeometry( *scene.getObjectManager(), navigation.findSurface( pType->_name ), settings, navigation.getGeometrySources(),
                                                  geometry );
            outListSample.clear();
            for ( uint32 round = 0; round < kBakeRound; ++round )
            {
                sw::unique_ptr<sw::INavMesh> pNavMesh = sw::NavMeshBackend::createNavMesh();
                if ( pNavMesh->initialize( *pType, settings, sw::NavMeshBakeUtil::computeBakeBounds( geometry ) ) == false )
                    return false;
                const sw::Stopwatch stopwatch;
                (void)sw::NavMeshBakeUtil::bakeAllTiles( *pNavMesh, geometry, nullptr, &outStats );
                outListSample.push_back( stopwatch.getElapsedMicroseconds() );
            }
            SW_LOG_INFO( "[Bench] %# : %# triangles in, %# of %# tiles, %# polygons", pPath, geometry.getTriangleCount(), outStats._filledTileCount,
                         outStats._tileCount, outStats._polygonCount );
            return true;
        }
    };
} // namespace

/**
 * @brief [NavMeshBenchTest] 전체 베이크 시간 — 파괴 쇼케이스 · Shooter3D 아레나(엔진 표의 Humanoid, 타일을 워커로 나눔)
 */
SW_TEST_CASE( NavMeshBenchTest, FullBakeOfTheShowcaseAndTheShooterArena )
{
    using Internal = NavMeshBenchTestInternal;
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::vector<int64>    listSample;
    sw::NavMeshBakeStats stats;
    SW_ASSERT_TRUE( Internal::measureBake( Internal::kShowcasePath, listSample, stats ) );
    SW_EXPECT_TRUE( stats._polygonCount > 0 );
    test::logBenchSamples( "NavMesh bake  destruction showcase (us)", listSample );
    SW_ASSERT_TRUE( Internal::measureBake( Internal::kArenaPath, listSample, stats ) );
    SW_EXPECT_TRUE( stats._polygonCount > 0 );
    test::logBenchSamples( "NavMesh bake  Shooter3D arena (us)", listSample );
}

/**
 * @brief [NavMeshBenchTest] Shooter3D 아레나의 질의 1000 번 — 경로(A* + 줄 당기기) · 레이캐스트 · 가까운 점, 고정 씨앗의 무작위 점
 */
SW_TEST_CASE( NavMeshBenchTest, ThousandQueriesOnTheShooterArena )
{
    using Internal = NavMeshBenchTestInternal;
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::Scene scene{ "NavBenchQuery" };
    SW_ASSERT_TRUE( Internal::instantiate( scene, Internal::kArenaPath ) );
    sw::SceneNavigation& navigation = scene.getObjectManager()->getSceneNavigation();
    const sw::INavMesh*  pNavMesh   = navigation.ensureNavMesh( sw::hashed_string{} );
    SW_ASSERT_NOT_NULL( pNavMesh );
    const sw::NavQueryFilter filter = navigation.getSettings().makeDefaultFilter();
    const sw::float3         extent{ 2.0f, 2.0f, 2.0f };

    sw::vector<sw::float3> listStart;
    sw::vector<sw::float3> listEnd;
    Internal::Random       random;
    for ( uint32 index = 0; index < Internal::kQueryCount; ++index )
    {
        listStart.push_back( random.nextPoint( 18.0f ) );
        listEnd.push_back( random.nextPoint( 18.0f ) );
    }
    uint32        completeCount = 0;
    sw::NavPath   path;
    sw::Stopwatch stopwatch;
    for ( uint32 index = 0; index < Internal::kQueryCount; ++index )
        completeCount += pNavMesh->findPath( listStart[index], listEnd[index], extent, filter, path ) == sw::NavPathStatus::Complete ? 1u : 0u;
    [[maybe_unused]] const int64 pathMicroseconds = stopwatch.getElapsedMicroseconds();
    stopwatch.restart();
    uint32            hitCount = 0;
    sw::NavRaycastHit hit;
    for ( uint32 index = 0; index < Internal::kQueryCount; ++index )
        hitCount += pNavMesh->raycast( listStart[index], listEnd[index], extent, filter, hit ) && hit._bHit ? 1u : 0u;
    [[maybe_unused]] const int64 raycastMicroseconds = stopwatch.getElapsedMicroseconds();
    stopwatch.restart();
    uint32          nearestCount = 0;
    sw::NavLocation location;
    for ( uint32 index = 0; index < Internal::kQueryCount; ++index )
        nearestCount += pNavMesh->findNearestPoint( listStart[index], extent, filter, location ) ? 1u : 0u;
    [[maybe_unused]] const int64 nearestMicroseconds = stopwatch.getElapsedMicroseconds();
    SW_LOG_INFO( "[Bench] NavMesh queries x%# on the Shooter3D arena: findPath %# us (%# complete), raycast %# us (%# hit), findNearestPoint %# us (%# found)",
                 Internal::kQueryCount, pathMicroseconds, completeCount, raycastMicroseconds, hitCount, nearestMicroseconds, nearestCount );
    SW_EXPECT_TRUE( completeCount > Internal::kQueryCount / 2 );
    SW_EXPECT_TRUE( hitCount > 0 );
    SW_EXPECT_TRUE( nearestCount > Internal::kQueryCount / 2 );
}

/**
 * @brief [NavMeshBenchTest] Shooter3D 아레나의 군중 갱신 한 번 — 에이전트 100 · 500 이 저마다 무작위 목적지로 걷는 동안
 */
SW_TEST_CASE( NavMeshBenchTest, CrowdUpdateWithHundredAndFiveHundredAgents )
{
    using Internal = NavMeshBenchTestInternal;
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::Scene scene{ "NavBenchCrowd" };
    SW_ASSERT_TRUE( Internal::instantiate( scene, Internal::kArenaPath ) );
    sw::INavMesh* pNavMesh = scene.getObjectManager()->getSceneNavigation().ensureNavMesh( sw::hashed_string{} );
    SW_ASSERT_NOT_NULL( pNavMesh );
    for ( const uint32 agentCount : { 100u, 500u } )
    {
        sw::unique_ptr<sw::INavCrowd> pCrowd = pNavMesh->createCrowd( agentCount, 1.0f );
        SW_ASSERT_NOT_NULL( pCrowd.get() );
        pCrowd->setQueryFilter( scene.getObjectManager()->getSceneNavigation().getSettings().makeDefaultFilter() );
        Internal::Random        random;
        sw::NavCrowdAgentParams params;
        params._radius   = 0.4f;
        params._height   = 1.9f;
        params._maxSpeed = 3.0f;
        sw::vector<sw::NavCrowdAgentId> listAgent;
        for ( uint32 index = 0; index < agentCount; ++index )
        {
            const sw::NavCrowdAgentId agent = pCrowd->addAgent( random.nextPoint( 18.0f ), params );
            if ( agent == sw::NavigationConstant::kInvalidAgentId )
                continue;
            (void)pCrowd->requestMoveTarget( agent, random.nextPoint( 18.0f ) );
            listAgent.push_back( agent );
        }
        SW_EXPECT_EQUAL( agentCount, static_cast<uint32>( listAgent.size() ) );
        for ( uint32 frame = 0; frame < 30; ++frame )
            pCrowd->update( 1.0f / 60.0f );
        sw::vector<int64> listSample;
        for ( uint32 frame = 0; frame < 120; ++frame )
        {
            const sw::Stopwatch stopwatch;
            pCrowd->update( 1.0f / 60.0f );
            listSample.push_back( stopwatch.getElapsedMicroseconds() );
        }
        test::logBenchSamples( agentCount == 100 ? "NavMesh crowd update  100 agents (us)" : "NavMesh crowd update  500 agents (us)", listSample );
    }
}
