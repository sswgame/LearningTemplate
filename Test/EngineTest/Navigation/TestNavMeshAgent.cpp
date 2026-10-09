// 내비메시 에이전트 컴포넌트 — 씬의 내비게이션이 표면의 기하로 베이크하고, 에이전트가 상자를 돌아 목적지에 닿는다(오브젝트를 직접 옮기거나 캐릭터 컨트롤러로).
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/INavMover.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshAgentComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshModifierComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshSurfaceComponent.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneNavigation.h"

#include "EngineTest/NavMeshTestUtil.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestTick.h"

namespace
{
    struct NavMeshAgentTestInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 단위 큐브 메시를 @p center · @p scale 로 놓은 오브젝트입니다. */
        static sw::GameObject* spawnMeshBox( sw::GameObjectManager& manager, const utf8* pName, const sw::float3& center, const sw::float3& scale )
        {
            sw::GameObject*    pObject = manager.createGameObject( sw::hashed_string( pName ) );
            sw::MeshComponent* pMesh   = pObject->addComponent<sw::MeshComponent>();
            pMesh->setMeshId( "Cube" );
            pMesh->setLocalPosition( center );
            pMesh->setLocalScale( scale );
            return pObject;
        }

        /** @brief 바닥(30 × 30) · 가운데 상자 벽(x -1..1, z -4..4) — 메시로. */
        static void spawnCrateScene( sw::GameObjectManager& manager )
        {
            spawnMeshBox( manager, "Floor", sw::float3{ 0.0f, -0.5f, 0.0f }, sw::float3{ 30.0f, 1.0f, 30.0f } );
            spawnMeshBox( manager, "Crates", sw::float3{ 0.0f, 1.0f, 0.0f }, sw::float3{ 2.0f, 2.0f, 8.0f } );
        }
    };
} // namespace

/**
 * @brief [NavMeshAgentTest] 메시 표면에서 베이크한 내비메시로 에이전트가 상자 더미를 돌아(한 번도 상자 안에 들지 않고) 목적지에 닿고, 공통 창구(`INavMover`)로 돌아온다
 */
SW_TEST_CASE( NavMeshAgentTest, AgentWalksAroundCratesToTheDestination )
{
    using Internal = NavMeshAgentTestInternal;
    sw::GameObjectManager manager;
    manager.getSceneNavigation().setSettings( navtest::makeSettings() );
    Internal::spawnCrateScene( manager );
    navtest::spawnSurface( manager, sw::NavGeometrySource::RenderMeshes );
    sw::GameObject*     pWalker = manager.createGameObject( sw::hashed_string( "Walker" ) );
    sw::SceneComponent* pRoot   = pWalker->addComponent<sw::SceneComponent>();
    pRoot->setLocalPosition( sw::float3{ -6.0f, 0.0f, 0.0f } );
    sw::NavMeshAgentComponent* pAgent = pWalker->addComponent<sw::NavMeshAgentComponent>();
    pAgent->setMaxSpeed( 4.0f );

    manager.beginPlay();
    test::tickFrames( manager, 2 );
    const sw::INavMesh* pNavMesh = manager.getSceneNavigation().findNavMesh( sw::hashed_string( "TestHumanoid" ) );
    SW_ASSERT_NOT_NULL( pNavMesh );
    SW_EXPECT_TRUE( pNavMesh->getPolygonCount() > 0 );
    // 에이전트 · 손에 든 것은 베이크에 들지 않는다 — 에이전트 자리의 바닥이 뚫리지 않았다.
    SW_EXPECT_TRUE( pAgent->isOnNavMesh() );

    pAgent->setDestination( sw::float3{ 6.0f, 0.0f, 0.0f } );
    bool bEnteredCrates = false;
    for ( uint32 frame = 0; frame < 600 && pAgent->getMoveStatus() != sw::NavMoveStatus::Arrived; ++frame )
    {
        manager.tick( Internal::kFrame );
        bEnteredCrates = bEnteredCrates || navtest::isInsideCrates( pRoot->getWorldPosition() );
    }
    SW_EXPECT_TRUE( pAgent->getMoveStatus() == sw::NavMoveStatus::Arrived );
    SW_EXPECT_FALSE( bEnteredCrates );
    SW_EXPECT_TRUE( ( pRoot->getWorldPosition() - sw::float3{ 6.0f, 0.0f, 0.0f } ).getLength() < 0.6f );

    sw::INavMover& mover = pAgent->getMover();
    SW_EXPECT_TRUE( mover.moveTo( sw::float3{ -6.0f, 0.0f, 2.0f } ) );
    for ( uint32 frame = 0; frame < 600 && mover.getMoveStatus() != sw::NavMoveStatus::Arrived; ++frame )
    {
        manager.tick( Internal::kFrame );
    }
    SW_EXPECT_TRUE( mover.getMoveStatus() == sw::NavMoveStatus::Arrived );
    SW_EXPECT_TRUE( ( mover.getMovePosition() - sw::float3{ -6.0f, 0.0f, 2.0f } ).getLength() < 0.6f );
    manager.endPlay();
}

/**
 * @brief [NavMeshAgentTest] 캐릭터 컨트롤러가 있는 에이전트 — 내비게이션은 원하는 속도만 넘기고 물리가 옮긴다. Static 강체 표면에서 베이크해 상자를 돌아 목적지에 닿는다
 */
SW_TEST_CASE( NavMeshAgentTest, AgentDrivesACharacterController )
{
    using Internal = NavMeshAgentTestInternal;
    sw::GameObjectManager manager;
    navtest::spawnPhysicsCrateScene( manager );
    sw::GameObject*                   pWalker     = manager.createGameObject( sw::hashed_string( "Walker" ) );
    sw::CharacterControllerComponent* pController = pWalker->addComponent<sw::CharacterControllerComponent>();
    pController->setLocalPosition( sw::float3{ -6.0f, 0.05f, 0.0f } );
    sw::NavMeshAgentComponent* pAgent = pWalker->addComponent<sw::NavMeshAgentComponent>();
    pAgent->setMaxSpeed( 4.0f );
    pAgent->setDriveMode( sw::NavAgentDriveMode::CharacterController );

    manager.beginPlay();
    test::tickFrames( manager, 10 );
    pAgent->setDestination( sw::float3{ 6.0f, 0.0f, 0.0f } );
    bool bEnteredCrates = false;
    for ( uint32 frame = 0; frame < 900 && pAgent->getMoveStatus() != sw::NavMoveStatus::Arrived; ++frame )
    {
        manager.tick( Internal::kFrame );
        bEnteredCrates = bEnteredCrates || navtest::isInsideCrates( pController->getWorldPosition() );
    }
    SW_EXPECT_TRUE( pAgent->getMoveStatus() == sw::NavMoveStatus::Arrived );
    SW_EXPECT_FALSE( bEnteredCrates );
    const sw::float3 feet = pController->getWorldPosition();
    SW_EXPECT_TRUE_MSG( ( sw::float3{ feet._x, 0.0f, feet._z } - sw::float3{ 6.0f, 0.0f, 0.0f } ).getLength() < 0.8f,
                        ( "controller ended at " + std::to_string( feet._x ) + ", " + std::to_string( feet._z ) ).c_str() );
    manager.endPlay();
}

/**
 * @brief [NavMeshAgentTest] 수정자 — 베이크에서 뺀 오브젝트는 막지 않고(`_bIgnoreFromBuild`), 영역을 준 바닥은 그 영역으로 베이크한다(막으면 경로가 없다)
 */
SW_TEST_CASE( NavMeshAgentTest, ModifierIgnoresObjectsAndAssignsAreas )
{
    using Internal = NavMeshAgentTestInternal;
    sw::GameObjectManager manager;
    manager.getSceneNavigation().setSettings( navtest::makeSettings() );
    Internal::spawnCrateScene( manager );
    sw::GameObject* pCrates = manager.findGameObjectByName( sw::hashed_string( "Crates" ) );
    SW_ASSERT_NOT_NULL( pCrates );
    pCrates->addComponent<sw::NavMeshModifierComponent>()->setIgnoreFromBuild( true );
    sw::GameObject* pFloor = manager.findGameObjectByName( sw::hashed_string( "Floor" ) );
    SW_ASSERT_NOT_NULL( pFloor );
    pFloor->addComponent<sw::NavMeshModifierComponent>()->setAreaName( sw::hashed_string( "Mud" ) );
    navtest::spawnSurface( manager, sw::NavGeometrySource::RenderMeshes );
    sw::SceneNavigation& navigation = manager.getSceneNavigation();
    SW_ASSERT_NOT_NULL( navigation.ensureNavMesh( sw::hashed_string( "TestHumanoid" ) ) );

    sw::NavPath path;
    SW_ASSERT_TRUE( navigation.findPath( sw::hashed_string( "TestHumanoid" ), sw::float3{ -6.0f, 0.0f, 0.0f }, sw::float3{ 6.0f, 0.0f, 0.0f }, path ) ==
                    sw::NavPathStatus::Complete );
    SW_EXPECT_NEAR_EQUAL( 12.0f, path.computeLength(), 0.1f );

    sw::NavQueryFilter noMud = navtest::makeSettings().makeDefaultFilter();
    noMud.setAreaAllowed( 2, false );
    SW_EXPECT_TRUE( navigation.findPath( sw::hashed_string( "TestHumanoid" ), sw::float3{ -6.0f, 0.0f, 0.0f }, sw::float3{ 6.0f, 0.0f, 0.0f }, noMud, path ) ==
                    sw::NavPathStatus::Failed );
}
