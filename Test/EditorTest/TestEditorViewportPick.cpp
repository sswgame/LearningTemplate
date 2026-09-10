#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/EditorViewportPick.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief -Z 를 바라보는 레이 (원점에서 z가 줄어드는 방향) */
    EditorPickRay makeForwardRay( float32 originX, float32 originY )
    {
        EditorPickRay ray{};
        ray._origin    = float3{ originX, originY, -10.0f };
        ray._direction = float3{ 0.0f, 0.0f, 1.0f };
        return ray;
    }

    /** @brief SceneComponent 하나를 가진 오브젝트를 지정 위치에 만듭니다. */
    GameObject* makeSceneObject( GameObjectManager& manager, const utf8* pName, const float3& position )
    {
        GameObject* pObj = manager.createGameObject( hashed_string( pName ) );
        if ( pObj == nullptr )
            return nullptr;

        SceneComponent* pScene = pObj->addComponent<SceneComponent>();
        if ( pScene != nullptr )
            pScene->setLocalPosition( position );
        return pObj;
    }
} // namespace

/**
 * @brief [EditorViewportPickTest] 레이-구 교차의 맞음·빗나감·뒤쪽 경우를 검증
 */
SW_TEST_CASE( EditorViewportPickTest, RaySphereIntersection )
{
    const float3 origin{ 0.0f, 0.0f, -10.0f };
    const float3 dir{ 0.0f, 0.0f, 1.0f };

    // 정면으로 반지름 1 구를 통과 — 앞면 거리는 10 - 1 = 9
    float32 hitT{ 0.0f };
    SW_EXPECT_TRUE( EditorViewportPick::rayHitsSphere( origin, dir, float3{ 0.0f, 0.0f, 0.0f }, 1.0f, hitT ) );
    SW_EXPECT_NEAR_EQUAL( 9.0f, hitT, 1e-3f );

    // 옆으로 빗나감
    SW_EXPECT_FALSE( EditorViewportPick::rayHitsSphere( origin, dir, float3{ 5.0f, 0.0f, 0.0f }, 1.0f, hitT ) );

    // 레이 뒤쪽의 구는 맞지 않는다
    SW_EXPECT_FALSE( EditorViewportPick::rayHitsSphere( origin, dir, float3{ 0.0f, 0.0f, -30.0f }, 1.0f, hitT ) );

    // 원점이 구 안에 있으면 뒷면까지의 거리를 낸다 (음수가 아니어야 한다)
    SW_EXPECT_TRUE( EditorViewportPick::rayHitsSphere( origin, dir, origin, 2.0f, hitT ) );
    SW_EXPECT_TRUE( 0.0f <= hitT );
}

/**
 * @brief [EditorViewportPickTest] 종류를 아는 제공자 표가 비어 있지 않은지 검증
 * @details 표가 비면 전용 경계가 모두 사라지고 전부 기본 반지름으로 떨어진다 — 조용한 회귀다.
 */
SW_TEST_CASE( EditorViewportPickTest, TypedProviderTableIsNotEmpty )
{
    SW_EXPECT_TRUE( EditorViewportPick::getTypedProviderCount() >= 3 );
}

/**
 * @brief [EditorViewportPickTest] 전용 종류가 없는 컴포넌트도 집히는지 검증
 * @details 이것이 예전에 안 되던 것이다. 예전 폴백은 `getPrimarySceneComponent()` **하나만** 봤고,
 *          종류별 함수는 엔진 타입 넷을 손으로 나열했다 — 그래서 게임이 만든 컴포넌트는 주
 *          컴포넌트가 아니면 뷰포트에서 클릭으로 집을 수 없었다.
 */
SW_TEST_CASE( EditorViewportPickTest, PlainSceneComponentIsPickable )
{
    GameObjectManager manager;
    GameObject*       pObj = makeSceneObject( manager, "PlainObject", float3{ 0.0f, 0.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pObj );
    manager.flushSceneTransforms();

    EditorPickResult result{};
    SW_EXPECT_TRUE( EditorViewportPick::pick( &manager, makeForwardRay( 0.0f, 0.0f ), false, result ) );
    SW_EXPECT_EQUAL( pObj, result._pObject );
    SW_EXPECT_NOT_NULL( result._pComponent );

    // 기본 반지름 밖을 지나가는 레이는 아무것도 집지 않는다.
    EditorPickResult missResult{};
    SW_EXPECT_FALSE( EditorViewportPick::pick( &manager, makeForwardRay( 5.0f, 0.0f ), false, missResult ) );
    SW_EXPECT_NULL( missResult._pObject );
}

/**
 * @brief [EditorViewportPickTest] 주 컴포넌트가 아닌 SceneComponent 도 후보가 되는지 검증
 * @details 게임이 컴포넌트를 여럿 붙인 오브젝트에서, 클릭한 자리에 있는 컴포넌트가 잡혀야 한다.
 *          예전에는 주 컴포넌트만 봤으므로 떨어져 있는 두 번째 컴포넌트는 집히지 않았다.
 */
SW_TEST_CASE( EditorViewportPickTest, NonPrimarySceneComponentIsPickable )
{
    GameObjectManager manager;
    GameObject*       pObj = makeSceneObject( manager, "MultiComponent", float3{ 0.0f, 0.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pObj );

    SceneComponent* pPrimary = pObj->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pPrimary );

    // 주 컴포넌트에서 멀리 떨어진 두 번째 SceneComponent
    SceneComponent* pSecond = pObj->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pSecond );
    pSecond->setLocalPosition( float3{ 4.0f, 0.0f, 0.0f } );
    manager.flushSceneTransforms();

    // 두 번째 컴포넌트 쪽을 지나는 레이는 그 컴포넌트를 집는다.
    EditorPickResult result{};
    SW_EXPECT_TRUE( EditorViewportPick::pick( &manager, makeForwardRay( 4.0f, 0.0f ), false, result ) );
    SW_EXPECT_EQUAL( pObj, result._pObject );
    SW_EXPECT_EQUAL( static_cast<Component*>( pSecond ), result._pComponent );

    // 주 컴포넌트 쪽을 지나는 레이는 주 컴포넌트를 집는다.
    EditorPickResult primaryResult{};
    SW_EXPECT_TRUE( EditorViewportPick::pick( &manager, makeForwardRay( 0.0f, 0.0f ), false, primaryResult ) );
    SW_EXPECT_EQUAL( static_cast<Component*>( pPrimary ), primaryResult._pComponent );
}

/**
 * @brief [EditorViewportPickTest] 여러 오브젝트가 겹치면 가까운 쪽이 집히는지 검증
 */
SW_TEST_CASE( EditorViewportPickTest, NearestObjectWins )
{
    GameObjectManager manager;
    GameObject*       pFar  = makeSceneObject( manager, "Far", float3{ 0.0f, 0.0f, 5.0f } );
    GameObject*       pNear = makeSceneObject( manager, "Near", float3{ 0.0f, 0.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pFar );
    SW_ASSERT_NOT_NULL( pNear );
    manager.flushSceneTransforms();

    // 레이는 z = -10 에서 +z 로 간다 — z = 0 쪽이 먼저다.
    EditorPickResult result{};
    SW_EXPECT_TRUE( EditorViewportPick::pick( &manager, makeForwardRay( 0.0f, 0.0f ), false, result ) );
    SW_EXPECT_EQUAL( pNear, result._pObject );
}

/**
 * @brief [EditorViewportPickTest] 비활성 오브젝트·컴포넌트는 집히지 않는지 검증
 */
SW_TEST_CASE( EditorViewportPickTest, InactiveObjectIsNotPicked )
{
    GameObjectManager manager;
    GameObject*       pObj = makeSceneObject( manager, "Inactive", float3{ 0.0f, 0.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pObj );
    manager.flushSceneTransforms();

    pObj->setActive( false );

    EditorPickResult result{};
    SW_EXPECT_FALSE( EditorViewportPick::pick( &manager, makeForwardRay( 0.0f, 0.0f ), false, result ) );
}

/**
 * @brief [EditorViewportPickTest] 매니저가 없거나 빈 씬이면 조용히 실패하는지 검증
 */
SW_TEST_CASE( EditorViewportPickTest, EmptySceneAndNullManagerAreSafe )
{
    EditorPickResult result{};
    SW_EXPECT_FALSE( EditorViewportPick::pick( nullptr, makeForwardRay( 0.0f, 0.0f ), false, result ) );
    SW_EXPECT_NULL( result._pObject );

    GameObjectManager emptyManager;
    SW_EXPECT_FALSE( EditorViewportPick::pick( &emptyManager, makeForwardRay( 0.0f, 0.0f ), false, result ) );
    SW_EXPECT_NULL( result._pObject );
}
