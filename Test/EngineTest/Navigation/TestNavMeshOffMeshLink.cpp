// 내비메시 오프 메시 링크 — 틈으로 갈린 두 바닥을 링크가 연결하고, 경로 · 군중이 링크를 건너며 번호로 알리고, 막으면(닫힌 문) 건너지 않는다.
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Object/Component/Navigation/NavMeshAgentComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshLinkComponent.h"
#include "Engine/Object/Component/Navigation/SceneNavigation.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/NavMeshTestUtil.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestTick.h"

namespace
{
    struct TestNavMeshOffMeshLinkInternal
    {
        /** @brief 링크 번호입니다. */
        static constexpr uint32 kGapLinkID = 7;

        /** @brief 바닥 둘(x 0..6, x 9..15, z 0..4)과 3 m 틈을 연결하는 양방향 링크 하나입니다. @p bWithLink 가 거짓이면 링크 없이 바닥만 넣습니다. */
        static void makeGapGeometry( sw::NavMeshGeometry& outGeometry, bool bWithLink )
        {
            navtest::addFloor( outGeometry, 0.0f, 0.0f, 6.0f, 4.0f );
            navtest::addFloor( outGeometry, 9.0f, 0.0f, 15.0f, 4.0f );
            if ( bWithLink == false )
                return;
            sw::NavOffMeshLink link;
            link._start  = sw::float3{ 5.3f, 0.0f, 2.0f };
            link._end    = sw::float3{ 9.7f, 0.0f, 2.0f };
            link._radius = 0.5f;
            link._userID = kGapLinkID;
            outGeometry.addOffMeshLink( link );
        }
    };
} // namespace

/**
 * @brief [NavMeshLinkTest] 틈을 연결하는 링크가 있으면 경로가 끝까지 가고(링크를 건너는 점과 번호를 알린다), 링크를 막으면 부분 경로로 돌아가며, 풀면 다시 지난다
 */
SW_TEST_CASE( NavMeshLinkTest, LinkBridgesTheGapAndCanBeBlocked )
{
    using Internal                     = TestNavMeshOffMeshLinkInternal;
    const sw::NavMeshSettings settings = navtest::makeSettings();
    const sw::float3          start{ 1.0f, 0.0f, 2.0f };
    const sw::float3          goal{ 14.0f, 0.0f, 2.0f };

    sw::NavMeshGeometry plain;
    Internal::makeGapGeometry( plain, false );
    sw::unique_ptr<sw::INavMesh> pPlain = navtest::bake( settings, plain );
    SW_ASSERT_NOT_NULL( pPlain.get() );
    sw::NavPath path;
    SW_EXPECT_TRUE( pPlain->findPath( start, goal, navtest::makeExtent(), settings.makeDefaultFilter(), path ) == sw::NavPathStatus::Partial );
    SW_EXPECT_TRUE( path._listLinkCrossing.empty() );

    sw::NavMeshGeometry linked;
    Internal::makeGapGeometry( linked, true );
    SW_EXPECT_TRUE( linked.computeHash() != plain.computeHash() );
    sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, linked );
    SW_ASSERT_NOT_NULL( pNavMesh.get() );
    SW_ASSERT_TRUE( pNavMesh->findPath( start, goal, navtest::makeExtent(), settings.makeDefaultFilter(), path ) == sw::NavPathStatus::Complete );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( path._listLinkCrossing.size() ) );
    SW_EXPECT_EQUAL( Internal::kGapLinkID, path._listLinkCrossing.front()._linkID );
    const uint32 crossPoint = path._listLinkCrossing.front()._pointIndex;
    SW_ASSERT_TRUE( crossPoint + 1 < static_cast<uint32>( path._listPoint.size() ) );
    SW_EXPECT_TRUE( sw::MathUtil::abs( path._listPoint[crossPoint]._x - 5.3f ) < 0.6f );
    SW_EXPECT_TRUE( sw::MathUtil::abs( path._listPoint[crossPoint + 1]._x - 9.7f ) < 0.6f );

    // 양방향 — 거꾸로도 지난다.
    SW_EXPECT_TRUE( pNavMesh->findPath( goal, start, navtest::makeExtent(), settings.makeDefaultFilter(), path ) == sw::NavPathStatus::Complete );

    // 막으면(닫힌 문) 지나지 않고, 풀면 다시 지난다.
    const uint32 revision = pNavMesh->getRevision();
    pNavMesh->setOffMeshLinkEnabled( Internal::kGapLinkID, false );
    SW_EXPECT_FALSE( pNavMesh->isOffMeshLinkEnabled( Internal::kGapLinkID ) );
    SW_EXPECT_TRUE( pNavMesh->getRevision() != revision );
    SW_EXPECT_TRUE( pNavMesh->findPath( start, goal, navtest::makeExtent(), settings.makeDefaultFilter(), path ) == sw::NavPathStatus::Partial );

    // 막힌 상태는 다시 끼운 타일(재베이크 · 쿠킹본)에도 남는다.
    sw::vector<sw::NavTileData> listTile;
    pNavMesh->collectTiles( listTile );
    for ( const sw::NavTileData& tile : listTile )
    {
        SW_ASSERT_TRUE( pNavMesh->replaceTile( tile ) );
    }
    SW_EXPECT_TRUE( pNavMesh->findPath( start, goal, navtest::makeExtent(), settings.makeDefaultFilter(), path ) == sw::NavPathStatus::Partial );

    pNavMesh->setOffMeshLinkEnabled( Internal::kGapLinkID, true );
    SW_EXPECT_TRUE( pNavMesh->isOffMeshLinkEnabled( Internal::kGapLinkID ) );
    SW_EXPECT_TRUE( pNavMesh->findPath( start, goal, navtest::makeExtent(), settings.makeDefaultFilter(), path ) == sw::NavPathStatus::Complete );
}

/**
 * @brief [NavMeshLinkTest] 군중 에이전트가 링크를 건너 틈 건너편 목적지에 닿는다 — 건너는 동안 링크 번호 · 진행률을 알리고, 링크 위에서는 도착으로 읽히지 않는다
 */
SW_TEST_CASE( NavMeshLinkTest, CrowdAgentTraversesTheLinkAndReportsIt )
{
    using Internal                     = TestNavMeshOffMeshLinkInternal;
    const sw::NavMeshSettings settings = navtest::makeSettings();
    sw::NavMeshGeometry       geometry;
    Internal::makeGapGeometry( geometry, true );
    sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
    SW_ASSERT_NOT_NULL( pNavMesh.get() );
    sw::unique_ptr<sw::INavCrowd> pCrowd = pNavMesh->createCrowd( 4, 1.0f );
    SW_ASSERT_NOT_NULL( pCrowd.get() );
    pCrowd->setQueryFilter( settings.makeDefaultFilter() );

    sw::NavCrowdAgentParams params;
    params._radius   = 0.4f;
    params._height   = 1.8f;
    params._maxSpeed = 3.0f;
    const sw::float3          start{ 1.0f, 0.0f, 2.0f };
    const sw::float3          goal{ 14.0f, 0.0f, 2.0f };
    const sw::NavCrowdAgentID agent = pCrowd->addAgent( start, params );
    SW_ASSERT_TRUE( agent != sw::NavigationConstant::kInvalidAgentID );
    SW_ASSERT_TRUE( pCrowd->requestMoveTarget( agent, goal ) );

    sw::NavCrowdAgentState state;
    uint32                 linkFrameCount  = 0;
    uint32                 seenLinkID      = 0;
    float32                lastProgress    = -1.0f;
    bool                   bProgressSorted = true;
    bool                   bArrivedOnLink  = false;
    for ( uint32 frame = 0; frame < 900; ++frame )
    {
        pCrowd->update( 1.0f / 60.0f );
        SW_ASSERT_TRUE( pCrowd->findAgentState( agent, state ) );
        if ( state._bOnOffMeshLink == false )
            continue;
        ++linkFrameCount;
        seenLinkID = state._link._linkID;
        bProgressSorted &= state._link._progress >= lastProgress;
        lastProgress = state._link._progress;
        bArrivedOnLink |= state._moveState == sw::NavCrowdMoveState::Arrived;
    }
    SW_EXPECT_TRUE( linkFrameCount > 0 );
    SW_EXPECT_EQUAL( Internal::kGapLinkID, seenLinkID );
    SW_EXPECT_TRUE( bProgressSorted );
    SW_EXPECT_FALSE( bArrivedOnLink );
    SW_EXPECT_FALSE( state._bOnOffMeshLink );
    SW_EXPECT_TRUE_MSG( sw::float3::getHorizontalDistance( state._position, goal ) < 0.5f, ( "ended at x " + std::to_string( state._position._x ) ).c_str() );
}

/**
 * @brief [NavMeshLinkTest] 씬의 링크 컴포넌트가 베이크에 들고, 에이전트가 건너는 동안 링크 종류 이름(애니메이션 신호)을 알리며, 링크를 막으면 다시 베이크해도 막혀 있다
 */
SW_TEST_CASE( NavMeshLinkTest, LinkComponentBakesSignalsAndBlocks )
{
    sw::GameObjectManager manager;
    sw::SceneNavigation&  navigation = manager.getSubsystem<sw::SceneNavigation>();
    navigation.setSettings( navtest::makeSettings() );
    navtest::spawnStaticBody( manager, "FloorWest", sw::float3{ 3.0f, -0.5f, 2.0f }, sw::float3{ 3.0f, 0.5f, 2.0f } );
    navtest::spawnStaticBody( manager, "FloorEast", sw::float3{ 12.0f, -0.5f, 2.0f }, sw::float3{ 3.0f, 0.5f, 2.0f } );
    navtest::spawnSurface( manager, sw::NavGeometrySource::PhysicsColliders );

    sw::GameObject*     pLinkObject = manager.createGameObject( sw::hashed_string( "GapJump" ) );
    sw::SceneComponent* pLinkRoot   = pLinkObject->addComponent<sw::SceneComponent>();
    pLinkRoot->setLocalPosition( sw::float3{ 5.3f, 0.0f, 2.0f } );
    sw::NavMeshLinkComponent* pLink = pLinkObject->addComponent<sw::NavMeshLinkComponent>();
    pLink->setEndOffset( sw::float3{ 4.4f, 0.0f, 0.0f } );
    pLink->setLinkType( sw::hashed_string( "Jump" ) );

    sw::GameObject*     pWalker = manager.createGameObject( sw::hashed_string( "Walker" ) );
    sw::SceneComponent* pRoot   = pWalker->addComponent<sw::SceneComponent>();
    pRoot->setLocalPosition( sw::float3{ 1.0f, 0.0f, 2.0f } );
    sw::NavMeshAgentComponent* pAgent = pWalker->addComponent<sw::NavMeshAgentComponent>();
    pAgent->setMaxSpeed( 3.0f );

    manager.beginPlay();
    test::tickFrames( manager, 2 );
    const sw::hashed_string agentType( "TestHumanoid" );
    SW_ASSERT_NOT_NULL( navigation.findNavMesh( agentType ) );
    const sw::float3 goal{ 14.0f, 0.0f, 2.0f };
    pAgent->setDestination( goal );
    bool bSawJump = false;
    for ( uint32 frame = 0; frame < 900 && pAgent->getMoveStatus() != sw::NavMoveStatus::Arrived; ++frame )
    {
        manager.tick( 1.0f / 60.0f );
        bSawJump = bSawJump || ( pAgent->isOnOffMeshLink() && pAgent->getOffMeshLinkType() == sw::hashed_string( "Jump" ) );
    }
    SW_EXPECT_TRUE( bSawJump );
    SW_EXPECT_TRUE( pAgent->getMoveStatus() == sw::NavMoveStatus::Arrived );
    SW_EXPECT_TRUE( sw::float3::getHorizontalDistance( pRoot->getWorldPosition(), goal ) < 0.6f );
    SW_EXPECT_FALSE( pAgent->isOnOffMeshLink() );

    // 막으면(닫힌 문) 다음 갱신부터 경로가 건너지 않고, 편집기 베이크 버튼 함수로 다시 베이크해도 막혀 있다.
    const sw::float3 west{ 1.0f, 0.0f, 2.0f };
    sw::NavPath      path;
    SW_EXPECT_TRUE( navigation.findPath( agentType, west, goal, path ) == sw::NavPathStatus::Complete );
    pLink->setLinkEnabled( false );
    manager.tick( 1.0f / 60.0f );
    SW_EXPECT_TRUE( navigation.findPath( agentType, west, goal, path ) == sw::NavPathStatus::Partial );
    SW_EXPECT_EQUAL( 1u, navigation.rebakeAllNavMeshes() );
    SW_EXPECT_FALSE( navigation.isLoadedFromCooked( agentType ) );
    SW_EXPECT_TRUE( navigation.findPath( agentType, west, goal, path ) == sw::NavPathStatus::Partial );
    pLink->setLinkEnabled( true );
    manager.tick( 1.0f / 60.0f );
    SW_EXPECT_TRUE( navigation.findPath( agentType, west, goal, path ) == sw::NavPathStatus::Complete );
    SW_EXPECT_EQUAL( pLink->computeCurrentLinkID(), path._listLinkCrossing.empty() ? 0u : path._listLinkCrossing.front()._linkID );
    manager.endPlay();
}
