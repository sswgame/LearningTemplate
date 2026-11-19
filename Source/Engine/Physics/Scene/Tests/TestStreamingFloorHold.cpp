// StreamingFloorHoldTest — 바닥 없는 스트리밍 셀(설계 "스트리밍 셀 경계 정합 · 저장" 3): 문서의 조각이 있는데 올라 있지 않은 셀 위 동적 바디는 몸 아래 받쳐 줄 충돌이 없을 때만 물리가 쥐어
// 떨어지지 않고(늘 올라 있는 땅 위 바디는 그대로 시뮬레이션), 셀이 오르면 풀린다.
#include "pch.h"

#include "Core/Container/span.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
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
        static RigidBodyComponent* spawnCrate( GameObjectManager& objects, const utf8* pName, const float3& position )
        {
            GameObject* pObject = objects.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            pObject->setAlwaysLoaded( true );
            RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._halfExtents = float3{ 0.25f, 0.25f, 0.25f };
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Dynamic );
            pBody->setMass( 1.0f );
            pBody->setLocalPosition( position );
            return pBody;
        }

        /** @brief 늘 올라 있는 정적 땅(윗면 y 0, 셀 (0,0) 을 덮는다)입니다 — 셀과 상관없이 받쳐 준다. */
        static RigidBodyComponent* spawnGround( GameObjectManager& objects )
        {
            GameObject* pObject = objects.createGameObject( hashed_string( "Ground" ) );
            if ( pObject == nullptr )
                return nullptr;
            pObject->setAlwaysLoaded( true );
            RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._halfExtents = float3{ 8.0f, 0.5f, 8.0f };
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Static );
            pBody->setLocalPosition( float3{ 5.0f, -0.5f, 5.0f } );
            return pBody;
        }

        static void setSource( SceneStreamingSubsystem& streaming, const float3& source ) { streaming.setExternalSources( span<const float3>( &source, 1 ) ); }

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
    RigidBodyComponent* pOverCell    = Util::spawnCrate( objects, "OverCell", float3{ 5.0f, 5.0f, 5.0f } );      // 땅 위 4.75 m — 광선(1 m)이 닿지 않는다
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
