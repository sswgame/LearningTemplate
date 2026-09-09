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

/**
 * @brief [EditorSceneCommandsTest] 컴포넌트 분포는 리플렉션으로 세고 인스턴스 단위로 센다
 * @details 예전에는 ProfilerPanel 이 타입 이름 5개를 손으로 나열하고 `getComponent<T>()` 로 셌다.
 *          그래서 (1) 게임이 만든 컴포넌트는 표에 안 나왔고, (2) 한 오브젝트에 같은 타입이 여럿이어도
 *          1 로 세서 "Active Instances" 라는 열 이름과 맞지 않았다. 그 두 가지를 여기서 고정한다.
 */
SW_TEST_CASE( EditorSceneCommandsTest, SceneStatisticsCountsEveryComponentInstance )
{
    GameObjectManager manager;

    // 빈 씬 — 0 이어야 하고 분포는 비어 있어야 한다.
    const EditorSceneCommands::SceneStatistics emptyStats = EditorSceneCommands::collectSceneStatistics( &manager );
    SW_EXPECT_EQUAL( 0u, emptyStats._objectCount );
    SW_EXPECT_EQUAL( 0u, emptyStats._componentCount );
    SW_EXPECT_TRUE( emptyStats._listDistribution.empty() );

    // nullptr 도 안전해야 한다 (패널이 씬 없이 부를 수 있다).
    const EditorSceneCommands::SceneStatistics nullStats = EditorSceneCommands::collectSceneStatistics( nullptr );
    SW_EXPECT_EQUAL( 0u, nullStats._objectCount );

    GameObject* pRoot  = manager.createGameObject( hashed_string( "Root" ) );
    GameObject* pChild = manager.createGameObject( hashed_string( "Child" ) );
    SW_ASSERT_NOT_NULL( pRoot );
    SW_ASSERT_NOT_NULL( pChild );

    SceneComponent* pRootScene  = pRoot->addComponent<SceneComponent>();
    SceneComponent* pChildScene = pChild->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pRootScene );
    SW_ASSERT_NOT_NULL( pChildScene );

    // 같은 타입을 한 오브젝트에 둘 붙인다 — 예전 방식이라면 1 로 셌을 자리다.
    SceneComponent* pExtraScene = pRoot->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pExtraScene );

    const EditorSceneCommands::SceneStatistics stats = EditorSceneCommands::collectSceneStatistics( &manager );

    SW_EXPECT_EQUAL( 2u, stats._objectCount );
    SW_EXPECT_EQUAL( 3u, stats._componentCount );
    SW_ASSERT_TRUE( stats._listDistribution.empty() == false );

    // 분포는 타입 이름으로 묶이고, 인스턴스 수(3)가 오브젝트 수(2)가 아니어야 한다.
    uint32 sceneComponentCount = 0;
    for ( const EditorSceneCommands::ComponentDistributionRow& row : stats._listDistribution )
    {
        if ( row._typeName == "SceneComponent" )
            sceneComponentCount = row._instanceCount;
    }
    SW_EXPECT_EQUAL( 3u, sceneComponentCount );

    // 많은 것부터 정렬된다 (같으면 이름순).
    for ( size_t index = 1; index < stats._listDistribution.size(); ++index )
    {
        const EditorSceneCommands::ComponentDistributionRow& prev = stats._listDistribution[index - 1];
        const EditorSceneCommands::ComponentDistributionRow& cur  = stats._listDistribution[index];
        SW_EXPECT_TRUE( prev._instanceCount > cur._instanceCount ||
                        ( prev._instanceCount == cur._instanceCount && prev._typeName <= cur._typeName ) );
    }
}
