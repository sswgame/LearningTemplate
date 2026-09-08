#include "pch.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorSceneCommandsTest] 부모-자식 순환 참조 감지 (wouldCreateParentCycle) 전수 검증
 */
SW_TEST_CASE( EditorSceneCommandsTest, ParentCycleDetection )
{
    GameObjectManager manager;
    GameObject*       pRoot       = manager.createGameObject( hashed_string( "Root" ) );
    GameObject*       pChild      = manager.createGameObject( hashed_string( "Child" ) );
    GameObject*       pGrandChild = manager.createGameObject( hashed_string( "GrandChild" ) );
    GameObject*       pOther      = manager.createGameObject( hashed_string( "Other" ) );

    SW_ASSERT_NOT_NULL( pRoot );
    SW_ASSERT_NOT_NULL( pChild );
    SW_ASSERT_NOT_NULL( pGrandChild );
    SW_ASSERT_NOT_NULL( pOther );

    // 부모-자식 관계는 SceneComponent 사이에서 맺어진다 — GameObject::attachToParent 는
    // 양쪽에 SceneComponent 가 없으면 아무 것도 하지 않고 false 를 돌려준다.
    pRoot->addComponent<SceneComponent>();
    pChild->addComponent<SceneComponent>();
    pGrandChild->addComponent<SceneComponent>();
    pOther->addComponent<SceneComponent>();

    SW_ASSERT_TRUE( pChild->attachToParent( pRoot ) );
    SW_ASSERT_TRUE( pGrandChild->attachToParent( pChild ) );

    // 1. 자기 자신을 부모로 지정하는 경우 방어
    SW_EXPECT_TRUE( EditorSceneCommands::wouldCreateParentCycle( pRoot, pRoot ) );
    SW_EXPECT_TRUE( EditorSceneCommands::wouldCreateParentCycle( pChild, pChild ) );

    // 2. 자식/손자 객체를 부모로 지정하여 사이클을 형성하려는 경우 방어
    SW_EXPECT_TRUE( EditorSceneCommands::wouldCreateParentCycle( pRoot, pChild ) );
    SW_EXPECT_TRUE( EditorSceneCommands::wouldCreateParentCycle( pRoot, pGrandChild ) );
    SW_EXPECT_TRUE( EditorSceneCommands::wouldCreateParentCycle( pChild, pGrandChild ) );

    // 3. 정상적인 상향/비관계 부모 지정은 허용
    SW_EXPECT_FALSE( EditorSceneCommands::wouldCreateParentCycle( pGrandChild, pRoot ) );
    SW_EXPECT_FALSE( EditorSceneCommands::wouldCreateParentCycle( pOther, pRoot ) );
    SW_EXPECT_FALSE( EditorSceneCommands::wouldCreateParentCycle( pOther, pGrandChild ) );

    // 4. nullptr 안전성 검증
    SW_EXPECT_TRUE( EditorSceneCommands::wouldCreateParentCycle( nullptr, pRoot ) );
    SW_EXPECT_TRUE( EditorSceneCommands::wouldCreateParentCycle( pRoot, nullptr ) );
    SW_EXPECT_TRUE( EditorSceneCommands::wouldCreateParentCycle( nullptr, nullptr ) );
}

/**
 * @brief [EditorSceneCommandsTest] 로컬 트랜스폼 적용 및 스냅샷 캡처 안전성 검증
 */
SW_TEST_CASE( EditorSceneCommandsTest, ApplyTransformAndSnapshotSafety )
{
    GameObjectManager manager;
    GameObject*       pObj = manager.createGameObject( hashed_string( "TestActor" ) );
    SW_ASSERT_NOT_NULL( pObj );

    pObj->addComponent<SceneComponent>();
    SceneComponent* pComp = pObj->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pComp );

    float3 targetPos{ 10.0f, 20.0f, 30.0f };
    float3 targetRot{ 0.1f, 0.2f, 0.3f };
    float3 targetScale{ 2.0f, 2.0f, 2.0f };

    EditorSceneCommands::applyLocalTransform( pObj, targetPos, targetRot, targetScale );

    SW_EXPECT_NEAR_EQUAL( 10.0f, pComp->getLocalPosition()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pComp->getLocalPosition()._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, pComp->getLocalPosition()._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pComp->getLocalScale()._x, 1e-4f );

    // nullptr 안전성
    EditorSceneCommands::applyLocalTransform( nullptr, targetPos, targetRot, targetScale );
    SW_EXPECT_TRUE( EditorSceneCommands::captureSnapshot( nullptr ).empty() );
}
