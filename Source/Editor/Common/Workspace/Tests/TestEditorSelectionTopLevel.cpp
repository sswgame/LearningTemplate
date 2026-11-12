/**
 * @file TestEditorSelectionTopLevel.cpp
 * @brief 다중 조작의 대상(`EditorSelection::filterTopLevel`)이 선택 순서와 상관없이 조상이 함께 고른 오브젝트를 빼는지 씬 밖의 지역 매니저로 봅니다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorSelection.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace
{
    struct EditorSelectionTopLevelTestInternal
    {
        static sw::GameObject* spawn( sw::GameObjectManager& manager, const utf8* pName )
        {
            sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( pName ) );
            if ( pObject != nullptr )
                (void)pObject->addComponent<sw::SceneComponent>(); // 부모 붙이기는 장면 컴포넌트끼리의 일이다
            return pObject;
        }
    };
} // namespace

/**
 * @brief [EditorSelectionTopLevelTest] 자식 · 손자를 부모보다 먼저 골라도 최상위 집합은 부모와 무관한 오브젝트뿐이고, 남은 것은 선택 순서를 지킨다(감사 E-4 — 그룹 기즈모 이중 변환)
 */
SW_TEST_CASE( EditorSelectionTopLevelTest, ChildPickedBeforeParentIsDropped )
{
    using Internal = EditorSelectionTopLevelTestInternal;

    sw::GameObjectManager manager;
    sw::GameObject*       pParent     = Internal::spawn( manager, "TopParent" );
    sw::GameObject*       pChild      = Internal::spawn( manager, "TopChild" );
    sw::GameObject*       pGrandchild = Internal::spawn( manager, "TopGrandchild" );
    sw::GameObject*       pOther      = Internal::spawn( manager, "TopOther" );
    SW_REQUIRE_NOT_NULL( pParent );
    SW_REQUIRE_NOT_NULL( pChild );
    SW_REQUIRE_NOT_NULL( pGrandchild );
    SW_REQUIRE_NOT_NULL( pOther );
    SW_REQUIRE_TRUE( pChild->attachToParent( pParent ) );
    SW_REQUIRE_TRUE( pGrandchild->attachToParent( pChild ) );

    const sw::vector<sw::GameObject*> listPicked{ pGrandchild, pOther, pChild, pParent };
    const sw::vector<sw::GameObject*> listTop = sw::editor::EditorSelection::filterTopLevel( listPicked );
    SW_REQUIRE_TRUE( listTop.size() == 2u );
    SW_EXPECT_TRUE( listTop[0] == pOther );
    SW_EXPECT_TRUE( listTop[1] == pParent );
}

/**
 * @brief [EditorSelectionTopLevelTest] 조상을 고르지 않은 자식은 남는다 — 중간 조상(자식)만 고르면 손자만 빠지고, 빈 칸(nullptr)은 버린다
 */
SW_TEST_CASE( EditorSelectionTopLevelTest, ChildWithoutPickedAncestorStays )
{
    using Internal = EditorSelectionTopLevelTestInternal;

    sw::GameObjectManager manager;
    sw::GameObject*       pParent     = Internal::spawn( manager, "KeepParent" );
    sw::GameObject*       pChild      = Internal::spawn( manager, "KeepChild" );
    sw::GameObject*       pGrandchild = Internal::spawn( manager, "KeepGrandchild" );
    SW_REQUIRE_NOT_NULL( pParent );
    SW_REQUIRE_NOT_NULL( pChild );
    SW_REQUIRE_NOT_NULL( pGrandchild );
    SW_REQUIRE_TRUE( pChild->attachToParent( pParent ) );
    SW_REQUIRE_TRUE( pGrandchild->attachToParent( pChild ) );

    const sw::vector<sw::GameObject*> listPicked{ pGrandchild, nullptr, pChild };
    const sw::vector<sw::GameObject*> listTop = sw::editor::EditorSelection::filterTopLevel( listPicked );
    SW_REQUIRE_TRUE( listTop.size() == 1u );
    SW_EXPECT_TRUE( listTop[0] == pChild );
}
