// StreamingFloorHoldTest — 바닥 없는 스트리밍 셀(설계 "스트리밍 셀 경계 정합 · 저장" 3): 문서의 조각이 있는데 올라 있지 않은 셀 위 동적 바디는 몸 아래 받쳐 줄 충돌이 없을 때만 물리가 쥐어
// 떨어지지 않고(늘 올라 있는 땅 위 바디는 그대로 시뮬레이션), 셀이 오르면 풀린다.
#include "pch.h"

#include "Core/Concurrency/DevCounter.h"
#include "Core/Container/span.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

using namespace sw;

namespace
{
    struct StreamingFloorHoldTestUtil
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 셀 (0,0)(한 변 10 m, 로딩 반경 6 m)에 엔티티 하나만 든 스트리밍 문서입니다 — 그 셀에 문서의 조각이 있다. */
        static SceneDocument makeDocument()
        {
            SceneDocument doc{};
            doc._name                                        = "FloorHold";
            doc._sceneSettings._bStreaming                   = true;
            doc._sceneSettings._cellGrid._cellSize           = 10.0f;
            doc._sceneSettings._cellGrid._loadingRadius      = 6.0f;
            doc._sceneSettings._unloadMargin                 = 0.0f;
            doc._sceneSettings._maxCellLoadsPerFrame         = 4;
            doc._sceneSettings._addToWorldBudgetMicroseconds = 1'000'000'000u;
            doc._bValid                                      = true;
            SceneDocument::StreamingCell cell{};
            cell._cell = StreamingCellCoord{ 0, 0 };
            SceneDocument::SceneObjectNode node{};
            node._fileID      = 1;
            node._name        = "Marker";
            node._embeddedXML = "<GameObject _schemaVersion=\"0\" _name=\"Marker\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"2,0,2\" /></_listComponent></GameObject>";
            cell._listSceneObjectNode.push_back( node );
            doc._listStreamingCell.push_back( std::move( cell ) );
            return doc;
        }

        /** @brief 늘 올라 있는 동적 상자입니다(셀에 들지 않는다). */
        static RigidBodyComponent* spawnCrate( GameObjectManager& objects, const utf8* pName, const float3& position, const float3& halfExtents = float3{ 0.25f, 0.25f, 0.25f } )
        {
            GameObject* pObject = objects.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            pObject->setAlwaysLoaded( true );
            RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Dynamic );
            pBody->setMass( 1.0f );
            pBody->setLocalPosition( position );
            return pBody;
        }

        /** @brief 늘 올라 있는 정적 상자입니다 — 셀과 상관없이 받쳐 준다. */
        static RigidBodyComponent* spawnStatic( GameObjectManager& objects, const utf8* pName, const float3& position, const float3& halfExtents, const float3& rotation = float3{} )
        {
            GameObject* pObject = objects.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            pObject->setAlwaysLoaded( true );
            RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Static );
            pBody->setLocalPosition( position );
            pBody->setLocalRotation( rotation );
            return pBody;
        }

        /** @brief 늘 올라 있는 정적 땅(윗면 y 0, 셀 (0,0) 을 덮는다)입니다. */
        static RigidBodyComponent* spawnGround( GameObjectManager& objects )
        {
            return spawnStatic( objects, "Ground", float3{ 5.0f, -0.5f, 5.0f }, float3{ 8.0f, 0.5f, 8.0f } );
        }

        static void setSource( SceneStreamingSubsystem& streaming, const float3& position )
        {
            const StreamingSource source{ position, streaming.getLoadingRadius(), StreamingSourcePriority::Normal };
            SW_EXPECT_TRUE( streaming.setExternalSources( span<const StreamingSource>( &source, 1 ) ) );
        }

        static void advance( GameObjectManager& objects, uint32 frameCount )
        {
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                objects.update( kFrame );
            }
        }
    };
    using Util = StreamingFloorHoldTestUtil;
} // namespace

/**
 * @brief [StreamingFloorHoldTest] 셀이 내려가 있는 동안 받쳐 줄 것 없는 동적 바디는 쥐여 그 자리에 있고, 늘 올라 있는 땅 위 바디는 쥐지 않으며(문서의 조각이 없는 셀 위 바디도 그대로), 셀이 오르면 풀린다
 */
SW_TEST_CASE( StreamingFloorHoldTest, BodyOverUnloadedCellIsHeldUntilTheCellLoads )
{
    Scene scene{ "FloorHold" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument() ) );
    GameObjectManager&       objects   = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming = objects.getSubsystem<SceneStreamingSubsystem>();
    Util::setSource( streaming, float3{ 500.0f, 0.0f, 500.0f } ); // 셀 (0,0) 은 반경 밖이다

    SW_REQUIRE_NOT_NULL( Util::spawnGround( objects ) );
    RigidBodyComponent* pOverCell    = Util::spawnCrate( objects, "OverCell", float3{ 5.0f, 5.0f, 5.0f } );      // 땅 위 4.75 m — 스윕(1 m)이 닿지 않는다
    RigidBodyComponent* pOnGround    = Util::spawnCrate( objects, "OnGround", float3{ 3.0f, 0.25f, 3.0f } );     // 늘 올라 있는 땅에 앉았다
    RigidBodyComponent* pOverNothing = Util::spawnCrate( objects, "OverNothing", float3{ 5.0f, 5.0f, 105.0f } ); // 셀 (0,10) — 문서의 조각이 없다
    SW_REQUIRE_NOT_NULL( pOverCell );
    SW_REQUIRE_NOT_NULL( pOverNothing );
    SW_REQUIRE_NOT_NULL( pOnGround );
    objects.beginPlay();
    Util::advance( objects, 60 );

    SW_EXPECT_TRUE( streaming.isFloorMissing( pOverCell->getWorldPosition() ) );
    SW_EXPECT_TRUE( pOverCell->isStreamingHeld() );
    SW_EXPECT_NEAR( 5.0f, pOverCell->getWorldPosition()._y, 1.0e-3f );
    SW_EXPECT_TRUE( streaming.isFloorMissing( pOnGround->getWorldPosition() ) ); // 셀은 내려갔지만
    SW_EXPECT_FALSE( pOnGround->isStreamingHeld() );                             // 받쳐 줄 땅이 몸 아래 있어 쥐지 않는다
    SW_EXPECT_NEAR( 0.25f, pOnGround->getWorldPosition()._y, 0.05f );
    SW_EXPECT_FALSE( streaming.isFloorMissing( pOverNothing->getWorldPosition() ) ); // 늘 올라 있는 지형만 있을 수 있는 셀은 바닥이 있다고 본다
    SW_EXPECT_FALSE( pOverNothing->isStreamingHeld() );
    SW_EXPECT_TRUE( pOverNothing->getWorldPosition()._y < 4.0f ); // 물리가 돌았다 — 쥐지 않은 바디는 떨어졌다

    Util::setSource( streaming, float3{ 5.0f, 0.0f, 5.0f } );
    Util::advance( objects, 60 );
    SW_EXPECT_TRUE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );
    SW_EXPECT_FALSE( streaming.isFloorMissing( pOverCell->getWorldPosition() ) );
    SW_EXPECT_FALSE( pOverCell->isStreamingHeld() );
    SW_EXPECT_TRUE( pOverCell->getWorldPosition()._y < 4.0f ); // 풀려 다시 시뮬레이션된다(셀에는 바닥 바디가 없어 늘 올라 있는 땅으로 떨어진다)
}

/**
 * @brief [StreamingFloorHoldTest] 바닥은 움직인 것과 셀이 오르내린 프레임에만 다시 묻는다 — 선 바디는 묻지 않고, 옮긴 바디는 새 자리에서 쥐이고 되돌리면 풀린다
 * @details 예전에는 바디 · 캐릭터 단계를 프레임마다 모두 물었다(묻는 수가 바디 수 × 프레임).
 */
SW_TEST_CASE( StreamingFloorHoldTest, FloorIsAskedOnlyForMovedBodies )
{
    Scene scene{ "FloorHold" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument() ) );
    GameObjectManager&       objects   = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming = objects.getSubsystem<SceneStreamingSubsystem>();
    const ScenePhysics&      physics   = objects.getSubsystem<ScenePhysics>();
    Util::setSource( streaming, float3{ 500.0f, 0.0f, 500.0f } ); // 셀 (0,0) 은 반경 밖이다

    constexpr uint32 kStaticCount = 32;
    for ( uint32 index = 0; index < kStaticCount; ++index )
    {
        SW_REQUIRE_NOT_NULL( Util::spawnStatic( objects, "Static", float3{ 200.0f + static_cast<float32>( index ) * 2.0f, 0.0f, 200.0f }, float3{ 0.5f, 0.5f, 0.5f } ) );
    }
    RigidBodyComponent* pCrate = Util::spawnCrate( objects, "Crate", float3{ 5.0f, 5.0f, 105.0f } ); // 셀 (0,10) — 문서의 조각이 없다
    SW_REQUIRE_NOT_NULL( pCrate );
    objects.beginPlay();
    Util::advance( objects, 4 );

    // 움직이는 것은 떨어지는 상자 하나다 — 선 바디는 묻지 않는다.
    constexpr uint32 kFrameCount = 8;
    const uint64     askedBefore = physics.getFloorCheckCount().get();
    Util::advance( objects, kFrameCount );
    SW_EXPECT_TRUE( physics.getFloorCheckCount().get() - askedBefore <= kFrameCount );
    SW_EXPECT_FALSE( pCrate->isStreamingHeld() );

    pCrate->teleportTo( float3{ 5.0f, 5.0f, 5.0f }, TeleportType::ResetPhysics ); // 내려간 셀 위
    Util::advance( objects, 2 );
    SW_EXPECT_TRUE( pCrate->isStreamingHeld() );
    // 쥔 동안에는 움직이지 않는다 — 셀이 오르내리지 않으면 자리를 다시 묻지 않는다.
    const uint64 heldBefore = physics.getFloorCheckCount().get();
    Util::advance( objects, kFrameCount );
    SW_EXPECT_EQUAL( heldBefore, physics.getFloorCheckCount().get() );
    SW_EXPECT_TRUE( pCrate->isStreamingHeld() );

    pCrate->teleportTo( float3{ 5.0f, 5.0f, 105.0f }, TeleportType::ResetPhysics );
    Util::advance( objects, 2 );
    SW_EXPECT_FALSE( pCrate->isStreamingHeld() );

    // 다시 내려간 셀 위에서 쥐인 뒤, 움직이지 않아도 셀이 오르면 풀린다.
    pCrate->teleportTo( float3{ 5.0f, 5.0f, 5.0f }, TeleportType::ResetPhysics );
    Util::advance( objects, 2 );
    SW_REQUIRE_TRUE( pCrate->isStreamingHeld() );
    Util::setSource( streaming, float3{ 5.0f, 0.0f, 5.0f } );
    Util::advance( objects, 10 );
    SW_EXPECT_TRUE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );
    SW_EXPECT_FALSE( pCrate->isStreamingHeld() );
}

/**
 * @brief [StreamingFloorHoldTest] 바닥은 자리의 광선이 아니라 몸 모양을 쓸어 찾는다 — 비탈에 앉은 큰 상자 · 선반 끝에 걸친 넓은 판은 쥐지 않고, 옆 벽에만 닿은 상자는 쥔다
 * @details 비탈: 45 도 아래 큰 상자의 중심은 비탈에서 1 m 넘게 떨어져 중심 광선이 닿지 않는다. 선반: 판의 중심 아래는 비어 있다.
 */
SW_TEST_CASE( StreamingFloorHoldTest, FloorIsFoundBySweepingTheBodyShape )
{
    Scene scene{ "FloorHold" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument() ) );
    GameObjectManager&       objects   = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming = objects.getSubsystem<SceneStreamingSubsystem>();
    Util::setSource( streaming, float3{ 500.0f, 0.0f, 500.0f } ); // 셀 (0,0) 은 반경 밖이다

    constexpr float32 kSlope = 0.7f; // 라디안(약 40 도)
    // 비탈 — 윗면이 (5, 20, 2) 를 지난다. 축에 맞춘 상자(반 크기 1)의 아래 모서리가 비탈에 닿는다(중심 높이 = 1 + tan).
    SW_REQUIRE_NOT_NULL( Util::spawnStatic( objects, "Slope", float3{ 5.0f + MathUtil::sin( kSlope ) * 0.5f, 20.0f - MathUtil::cos( kSlope ) * 0.5f, 2.0f }, float3{ 4.0f, 0.5f, 2.0f },
                                            float3{ 0.0f, 0.0f, kSlope } ) );
    RigidBodyComponent* pOnSlope = Util::spawnCrate( objects, "OnSlope", float3{ 5.0f, 20.0f + 1.0f + MathUtil::tan( kSlope ) + 0.02f, 2.0f }, float3{ 1.0f, 1.0f, 1.0f } );
    // 선반(x 1 ~ 3, 윗면 y 10) 끝에 걸친 판(x 2.5 ~ 6.5) — 중심(x 4.5) 아래는 빈 자리다.
    SW_REQUIRE_NOT_NULL( Util::spawnStatic( objects, "Shelf", float3{ 2.0f, 9.5f, 7.0f }, float3{ 1.0f, 0.5f, 1.0f } ) );
    RigidBodyComponent* pOnEdge = Util::spawnCrate( objects, "OnEdge", float3{ 4.5f, 10.22f, 7.0f }, float3{ 2.0f, 0.2f, 1.0f } );
    // 벽 옆면에 닿은 상자 — 아래에는 아무것도 없다.
    SW_REQUIRE_NOT_NULL( Util::spawnStatic( objects, "Wall", float3{ 8.0f, 30.0f, 5.0f }, float3{ 0.5f, 5.0f, 2.0f } ) );
    RigidBodyComponent* pByWall = Util::spawnCrate( objects, "ByWall", float3{ 8.74f, 30.0f, 5.0f } );
    SW_REQUIRE_NOT_NULL( pOnSlope );
    SW_REQUIRE_NOT_NULL( pOnEdge );
    SW_REQUIRE_NOT_NULL( pByWall );
    objects.beginPlay();
    Util::advance( objects, 2 ); // 첫 물리 프레임이 정적 바디를 짓고, 그다음 프레임이 바닥을 본다

    SW_EXPECT_TRUE( streaming.isFloorMissing( pOnSlope->getWorldPosition() ) );
    SW_EXPECT_FALSE( pOnSlope->isStreamingHeld() );
    SW_EXPECT_TRUE( streaming.isFloorMissing( pOnEdge->getWorldPosition() ) );
    SW_EXPECT_FALSE( pOnEdge->isStreamingHeld() );
    SW_EXPECT_TRUE( streaming.isFloorMissing( pByWall->getWorldPosition() ) );
    SW_EXPECT_TRUE( pByWall->isStreamingHeld() );
}
