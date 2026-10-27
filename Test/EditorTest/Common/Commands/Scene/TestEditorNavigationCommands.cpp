#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorNavigationCommands.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Object/Component/Navigation/NavMeshSurfaceComponent.h"
#include "Engine/Object/Component/Navigation/SceneNavigation.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

// EditorNavigationCommands — `navigation.bake` 의 동작(ImGui 없음). 편집 중인 씬(플레이 전)의 기하로 내비메시를 짓는다.

namespace
{
    struct EditorNavigationCommandsTestInternal
    {
        /** @brief 에이전트 종류 하나(반지름 0.4 · 높이 1.8 · 셀 0.2 · 타일 32 셀)와 영역 하나의 설정 표입니다. */
        static sw::NavMeshSettings makeSettings()
        {
            sw::NavMeshSettings settings;
            sw::NavAgentTypeDef agentType;
            agentType._name       = sw::hashed_string( "EditorTestHumanoid" );
            agentType._radius     = 0.4f;
            agentType._height     = 1.8f;
            agentType._maxClimb   = 0.45f;
            agentType._maxSlope   = 0.785398f;
            agentType._cellSize   = 0.2f;
            agentType._cellHeight = 0.1f;
            agentType._tileSize   = 32;
            settings._listAgentType.push_back( agentType );
            sw::NavAreaDef area;
            area._name = sw::hashed_string( "Default" );
            area._cost = 1.0f;
            settings._listArea.push_back( area );
            return settings;
        }

        static void spawnStaticBox( sw::GameObjectManager& manager, const utf8* pName, const sw::float3& center, const sw::float3& halfExtents )
        {
            sw::GameObject*         pObject = manager.createGameObject( sw::hashed_string( pName ) );
            sw::RigidBodyComponent* pBody   = pObject->addComponent<sw::RigidBodyComponent>();
            sw::PhysicsShapeDesc3D  box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( sw::PhysicsBodyType::Static );
            pBody->setLocalPosition( center );
        }

        static void spawnSurface( sw::GameObjectManager& manager )
        {
            sw::GameObject*              pObject  = manager.createGameObject( sw::hashed_string( "NavSurface" ) );
            sw::NavMeshSurfaceComponent* pSurface = pObject->addComponent<sw::NavMeshSurfaceComponent>();
            pSurface->setAgentTypes( { sw::hashed_string( "EditorTestHumanoid" ) } );
            pSurface->setGeometrySource( sw::NavGeometrySource::PhysicsColliders );
        }
    };
} // namespace

/**
 * @brief [EditorNavigationCommandsTest] 기하가 없는 씬은 베이크할 것이 없고, 바닥을 놓으면 에이전트 종류의 내비메시가 선다
 */
SW_TEST_CASE( EditorNavigationCommandsTest, BakesTheEditedSceneBeforePlay )
{
    SW_TEST_DEFENSIVE_SCOPE( "a scene without geometry has nothing to bake and says so" ); // 거절 · 실패 로그가 이 테스트가 보려는 동작이다
    using Internal = EditorNavigationCommandsTestInternal;
    sw::GameObjectManager manager;
    manager.getSubsystem<sw::SceneNavigation>().setSettings( Internal::makeSettings() );
    SW_EXPECT_EQUAL( 0u, sw::editor::EditorNavigationCommands::bakeAll( manager ) );
    SW_EXPECT_EQUAL( 0u, sw::editor::EditorNavigationCommands::countBakedMeshes( manager ) );

    Internal::spawnStaticBox( manager, "Floor", sw::float3{ 0.0f, -0.5f, 0.0f }, sw::float3{ 10.0f, 0.5f, 10.0f } );
    Internal::spawnSurface( manager );
    SW_EXPECT_EQUAL( 1u, sw::editor::EditorNavigationCommands::bakeAll( manager ) );
    SW_EXPECT_EQUAL( 1u, sw::editor::EditorNavigationCommands::countBakedMeshes( manager ) );
    const sw::INavMesh* pNavMesh = manager.getSubsystem<sw::SceneNavigation>().findNavMesh( sw::hashed_string( "EditorTestHumanoid" ) );
    SW_ASSERT_NOT_NULL( pNavMesh );
    const uint32 openFloorPolygonCount = pNavMesh->getPolygonCount();
    SW_EXPECT_TRUE( openFloorPolygonCount > 0 );

    // 벽을 세우고 다시 베이크하면 마련해 둔 메시를 버리고 새 기하로 짓는다 — 벽이 바닥을 가르므로 폴리곤이 달라진다.
    Internal::spawnStaticBox( manager, "Wall", sw::float3{ 0.0f, 1.0f, 0.0f }, sw::float3{ 0.5f, 1.0f, 8.0f } );
    SW_EXPECT_EQUAL( 1u, sw::editor::EditorNavigationCommands::bakeAll( manager ) );
    const sw::INavMesh* pRebaked = manager.getSubsystem<sw::SceneNavigation>().findNavMesh( sw::hashed_string( "EditorTestHumanoid" ) );
    SW_ASSERT_NOT_NULL( pRebaked );
    SW_EXPECT_TRUE( pRebaked->getPolygonCount() != openFloorPolygonCount );
}
