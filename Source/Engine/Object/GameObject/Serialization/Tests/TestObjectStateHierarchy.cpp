// ObjectStateHierarchyTest — 부모의 상태를 다시 읽어도 자식의 로컬 변환은 그대로다(에디터 되돌리기가 부모 스냅샷을 다시 읽는다, nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"

using namespace sw;

/**
 * @brief [ObjectStateHierarchyTest] 부모를 옮긴 뒤 옮기기 전 상태를 다시 읽으면 부모는 제자리로, 자식은 로컬 그대로 부모를 따라온다
 * @details 에디터 되돌리기 전수(editor.undocoverage)에서 부모 · 자식이 있는 씬의 정렬 · 바닥 맞추기를 되돌리면 자식 로컬이 0 → +0.5 로 바뀌었다.
 *          자식 로컬이 바뀌면 이 테스트의 마지막 단언이 진다.
 */
SW_TEST_CASE( ObjectStateHierarchyTest, ReloadingTheParentKeepsTheChildLocal )
{
    GameObjectManager manager;
    GameObject*       pParent = manager.createGameObject( hashed_string( "Parent" ) );
    GameObject*       pChild  = manager.createGameObject( hashed_string( "Child" ) );
    SW_REQUIRE_NOT_NULL( pParent );
    SW_REQUIRE_NOT_NULL( pChild );
    SceneComponent* pParentScene = pParent->addComponent<SceneComponent>();
    SceneComponent* pChildScene  = pChild->addComponent<SceneComponent>();
    SW_REQUIRE_NOT_NULL( pParentScene );
    SW_REQUIRE_NOT_NULL( pChildScene );
    pParentScene->setLocalPosition( float3{ 0.0f, 0.5f, 0.0f } );
    SW_REQUIRE_TRUE( pChild->attachToParent( pParent ) );
    SW_REQUIRE_TRUE( pChildScene->getLocalPosition() == float3( 0.0f, 0.0f, 0.0f ) );

    const string before = ObjectStateSerializer::saveToXMLString( pParent, ObjectSaveOptions{ SerializePurpose::Authoring } );
    pParentScene->setWorldPosition( float3{ 0.0f, 2.0f, 0.0f } ); // 옮기기 — 자식은 로컬 그대로 따라간다(월드와 로컬이 달라야 월드 유지 분리를 잡는다)
    SW_REQUIRE_TRUE( pChildScene->getLocalPosition() == float3( 0.0f, 0.0f, 0.0f ) );

    SW_REQUIRE_TRUE( ObjectStateSerializer::loadFromXMLString( pParent, before, ObjectLoadContext{ SerializePurpose::Authoring } ) );
    const SceneComponent* pReloadedScene = pParent->getPrimarySceneComponent(); // 다시 읽기가 컴포넌트를 새로 만든다 — 옛 포인터는 지워졌다
    SW_REQUIRE_NOT_NULL( pReloadedScene );
    SW_EXPECT_TRUE( pReloadedScene->getLocalPosition() == float3( 0.0f, 0.5f, 0.0f ) );
    SW_EXPECT_TRUE( pChild->getParent() == pParent );
    SW_EXPECT_TRUE( pChildScene->getLocalPosition() == float3( 0.0f, 0.0f, 0.0f ) );
    SW_EXPECT_TRUE( pChildScene->getWorldPosition() == float3( 0.0f, 0.5f, 0.0f ) );
}
