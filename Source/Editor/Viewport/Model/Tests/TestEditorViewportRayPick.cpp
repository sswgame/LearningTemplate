/**
 * @file TestEditorViewportRayPick.cpp
 * @brief 뷰포트 클릭 피킹(`EditorViewportRayPick::pick`)이 경계 구가 아니라 메시 상자 · 삼각형으로 고르는지 씬 밖의 지역 매니저로 봅니다(감사 E-10).
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace
{
    struct EditorViewportRayPickTestInternal
    {
        /** @brief 내장 도형 @p pMeshID 를 단 메시 오브젝트를 @p position · @p scale 에 둡니다. */
        static sw::GameObject* spawnMesh( sw::GameObjectManager& manager, const utf8* pName, const utf8* pMeshID, const sw::float3& position, const sw::float3& scale )
        {
            sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            sw::MeshComponent* pMesh = pObject->addComponent<sw::MeshComponent>();
            if ( pMesh == nullptr )
                return nullptr;
            pMesh->setMesh( sw::MeshUtil::acquirePrimitive( pMeshID ) );
            pMesh->setWorldPosition( position );
            pMesh->setLocalScale( scale );
            return pObject;
        }

        static sw::Ray makeRay( const sw::float3& origin, const sw::float3& target )
        {
            const sw::float3 toTarget = target - origin;
            sw::Ray          ray{};
            ray._origin    = origin;
            ray._direction = toTarget.normalize();
            return ray;
        }
    };
} // namespace

/**
 * @brief [EditorViewportRayPickTest] 길고 얇은 기둥 옆 빈 곳은 아무것도 집지 않는다 — 경계 구(반지름 약 5 m)나 피벗 둘레 기본 반지름(0.35 m)이 아니라 메시가 판정한다
 */
SW_TEST_CASE( EditorViewportRayPickTest, EmptySpaceBesidePillarPicksNothing )
{
    using Internal = EditorViewportRayPickTestInternal;

    sw::GameObjectManager manager;
    sw::GameObject*       pPillar = Internal::spawnMesh( manager, "Pillar", "Cube", sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.2f, 6.0f, 0.2f } );
    SW_REQUIRE_NOT_NULL( pPillar );
    manager.mergePendingAdds();
    manager.flushSceneTransforms();

    sw::editor::EditorPickResult result{};
    // 피벗에서 0.3 m — 기둥(반 폭 0.1 m) 밖이고 기본 반지름 안이다.
    SW_EXPECT_FALSE( sw::editor::EditorViewportRayPick::pick( &manager, Internal::makeRay( sw::float3{ 0.3f, 0.0f, -10.0f }, sw::float3{ 0.3f, 0.0f, 0.0f } ), false, result ) );
    SW_EXPECT_TRUE( result._pObject == nullptr );
    // 2 m 위 — 경계 구 안이고 기둥 밖이다.
    SW_EXPECT_FALSE( sw::editor::EditorViewportRayPick::pick( &manager, Internal::makeRay( sw::float3{ 1.0f, 2.0f, -10.0f }, sw::float3{ 1.0f, 2.0f, 0.0f } ), false, result ) );

    // 기둥 정면은 집고, 거리는 앞면(z = -0.1)까지다.
    SW_REQUIRE_TRUE( sw::editor::EditorViewportRayPick::pick( &manager, Internal::makeRay( sw::float3{ 0.0f, 2.0f, -10.0f }, sw::float3{ 0.0f, 2.0f, 0.0f } ), false, result ) );
    SW_EXPECT_TRUE( result._pObject == pPillar );
    SW_EXPECT_NEAR( 9.9f, result._distance, 1e-3f );
}

/**
 * @brief [EditorViewportRayPickTest] 큰 바닥 위 작은 큐브는 큐브를 집는다 — 바닥의 큰 경계 구가 먼저 맞아도 표면이 더 가까운 큐브가 이긴다. 큐브 밖은 바닥 표면까지의 거리로 바닥이다
 */
SW_TEST_CASE( EditorViewportRayPickTest, SmallCubeOnLargeFloorPicksCube )
{
    using Internal = EditorViewportRayPickTestInternal;

    sw::GameObjectManager manager;
    sw::GameObject*       pFloor = Internal::spawnMesh( manager, "Floor", "Plane", sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 40.0f, 1.0f, 40.0f } );
    sw::GameObject*       pCube  = Internal::spawnMesh( manager, "SmallCube", "Cube", sw::float3{ 0.0f, 0.5f, 0.0f }, sw::float3{ 1.0f, 1.0f, 1.0f } );
    SW_REQUIRE_NOT_NULL( pFloor );
    SW_REQUIRE_NOT_NULL( pCube );
    manager.mergePendingAdds();
    manager.flushSceneTransforms();

    sw::editor::EditorPickResult result{};
    SW_REQUIRE_TRUE( sw::editor::EditorViewportRayPick::pick( &manager, Internal::makeRay( sw::float3{ 0.0f, 10.0f, -10.0f }, sw::float3{ 0.0f, 0.5f, 0.0f } ), false, result ) );
    SW_EXPECT_TRUE( result._pObject == pCube );

    const sw::float3 eye{ 5.0f, 10.0f, -5.0f };
    SW_REQUIRE_TRUE( sw::editor::EditorViewportRayPick::pick( &manager, Internal::makeRay( eye, sw::float3{ 5.0f, 0.0f, 5.0f } ), false, result ) );
    SW_EXPECT_TRUE( result._pObject == pFloor );
    SW_EXPECT_NEAR( 10.0f * sw::MathUtil::sqrt( 2.0f ), result._distance, 1e-3f );
}

/**
 * @brief [EditorViewportRayPickTest] 경계가 없는 오브젝트(빈 트랜스폼)는 여전히 피벗 둘레 기본 반지름으로 집힌다
 */
SW_TEST_CASE( EditorViewportRayPickTest, BoundlessObjectUsesFallbackRadius )
{
    using Internal = EditorViewportRayPickTestInternal;

    sw::GameObjectManager manager;
    sw::GameObject*       pEmpty = manager.createGameObject( sw::hashed_string( "EmptyPivot" ) );
    SW_REQUIRE_NOT_NULL( pEmpty );
    sw::SceneComponent* pScene = pEmpty->addComponent<sw::SceneComponent>();
    SW_REQUIRE_NOT_NULL( pScene );
    pScene->setWorldPosition( sw::float3{ 3.0f, 0.0f, 0.0f } );
    manager.mergePendingAdds();
    manager.flushSceneTransforms();

    sw::editor::EditorPickResult result{};
    SW_REQUIRE_TRUE( sw::editor::EditorViewportRayPick::pick( &manager, Internal::makeRay( sw::float3{ 3.2f, 0.0f, -10.0f }, sw::float3{ 3.2f, 0.0f, 0.0f } ), false, result ) );
    SW_EXPECT_TRUE( result._pObject == pEmpty );
    SW_EXPECT_FALSE( sw::editor::EditorViewportRayPick::pick( &manager, Internal::makeRay( sw::float3{ 3.5f, 0.0f, -10.0f }, sw::float3{ 3.5f, 0.0f, 0.0f } ), false, result ) );
}
