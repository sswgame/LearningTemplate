#include "pch.h"

#include "Core/String/TagID.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"

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
    SW_EXPECT_TRUE( EditorTransaction::captureSnapshot( nullptr )._xml.empty() );
}

/**
 * @brief [EditorSceneCommandsTest] 복제는 서브트리 전체를 복제하고, 복제본 안의 부착(소켓 · 자식)은 복제본끼리 잇는다
 * @details 묶음이 원본 id 로 복제본끼리 잇고, 묶음 밖의 부모(원본과 같은 부모 · 같은 소켓)는 런타임 id 로 찾는다. 선택한 오브젝트 하나만 복제하면
 *          자식이 원본 밑에 남고(씬은 자식을 자기 엔티티로 저장한다), 복제본 안의 부착을 이름으로 풀면 이름이 유일하게 바뀐 복제본(`Rig` → `Rig_2`)의
 *          팔이 **원본**의 루트에 붙는다. 복제본의 루트를 원본 부모의 primary 에 다시 붙이면 소켓을 잃는다.
 */
SW_TEST_CASE( EditorSceneCommandsTest, DuplicateCopiesTheSubtreeAndKeepsItsAttachmentsInside )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "DuplicateProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    GameObjectManager*        pManager = pScene->getObjectManager();

    GameObject*     pHolder = pManager->createGameObject( hashed_string( "Holder" ) );
    SceneComponent* pBody   = pHolder->addComponent<SceneComponent>();
    SceneComponent* pSocket = pHolder->addComponent<SceneComponent>();
    SW_ASSERT_TRUE( pSocket->attachToComponent( pBody ) );

    GameObject*     pRig     = pManager->createGameObject( hashed_string( "Rig" ) );
    SceneComponent* pRigRoot = pRig->addComponent<SceneComponent>();
    SceneComponent* pRigArm  = pRig->addComponent<SceneComponent>();
    SW_ASSERT_TRUE( pRigArm->attachToComponent( pRigRoot ) );
    SW_ASSERT_TRUE( pRigRoot->attachToComponent( pSocket ) );
    GameObject*     pTool     = pManager->createGameObject( hashed_string( "Tool" ) );
    SceneComponent* pToolRoot = pTool->addComponent<SceneComponent>();
    SW_ASSERT_TRUE( pToolRoot->attachToComponent( pRigArm ) );
    pManager->mergePendingAdds();

    GameObject* pCopy = EditorSceneCommands::duplicate( pManager, pRig );
    SW_ASSERT_NOT_NULL( pCopy );
    pManager->mergePendingAdds();

    SceneComponent* pCopyRoot = pCopy->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pCopyRoot );
    SW_EXPECT_TRUE( pCopyRoot->getParent() == pSocket ); // 원본과 같은 소켓
    SceneComponent* pCopyArm = nullptr;
    for ( Component* pComp : pCopy->getComponents() )
    {
        if ( pComp != nullptr && pComp->isSceneComponent() && pComp != pCopyRoot )
            pCopyArm = static_cast<SceneComponent*>( pComp );
    }
    SW_ASSERT_NOT_NULL( pCopyArm );
    SW_EXPECT_TRUE( pCopyArm->getParent() == pCopyRoot ); // 원본의 루트가 아니다

    // 자식도 복제됐고, 복제된 팔에 붙었다. 원본의 자식은 그대로다.
    SW_ASSERT_EQUAL( size_t( 1 ), pCopyArm->getChildren().size() );
    SceneComponent* pCopyToolRoot = pCopyArm->getChildren().front();
    SW_EXPECT_TRUE( pCopyToolRoot->getOwner() != pTool );
    SW_EXPECT_TRUE( pToolRoot->getParent() == pRigArm );
    SW_EXPECT_EQUAL( size_t( 1 ), pRigArm->getChildren().size() );
    SW_EXPECT_EQUAL( size_t( 1 ), pRigRoot->getChildren().size() );
}

/**
 * @brief [EditorSceneCommandsTest] 컴포넌트 분포는 리플렉션으로 세고 인스턴스 단위로 센다
 * @details 타입 이름을 손으로 나열하고 `getComponent<T>()` 로 세면 (1) 게임이 만든 컴포넌트는 표에 안 나오고,
 *          (2) 한 오브젝트에 같은 타입이 여럿이어도 1 로 세어 "Active Instances" 라는 열 이름과 맞지 않는다.
 *          그 두 가지를 여기서 고정한다.
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

    // 같은 타입을 한 오브젝트에 둘 붙인다 — `getComponent<T>()` 로 세면 1 이 되는 자리다.
    SceneComponent* pExtraScene = pRoot->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pExtraScene );

    const EditorSceneCommands::SceneStatistics stats = EditorSceneCommands::collectSceneStatistics( &manager );

    SW_EXPECT_EQUAL( 2u, stats._objectCount );
    SW_EXPECT_EQUAL( 3u, stats._componentCount );
    SW_ASSERT_FALSE( stats._listDistribution.empty() );

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

/**
 * @brief [EditorSceneCommandsTest] 계층 패널의 "컴포넌트 추가" 는 되돌릴 수 있고, 붙인 메시는 바로 그려진다
 * @details `EditorSceneCommands::addComponent` 가 기록하고 `onPostLoad` 를 부른다. 매니저에 바로 붙이면 되돌리기 기록도 씬 dirty 도 없어
 *          Ctrl+Z 가 듣지 않고 저장을 묻지 않고 사라지며, 새 메시 컴포넌트가 메시를 풀지 않아 플레이 전까지 그려지지 않는다.
 */
SW_TEST_CASE( EditorSceneCommandsTest, AddComponentIsUndoableAndResolvesItsMesh )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "AddComponentProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    CommandStack              stack;
    ScopedCommandStackService scopedStack{ stack };
    GameObjectManager*        pManager = pScene->getObjectManager();
    GameObject*               pObj     = pManager->createGameObject( hashed_string( "Crate" ) );
    SW_ASSERT_NOT_NULL( pObj->addComponent<SceneComponent>() );
    pManager->mergePendingAdds();

    MeshComponent* pMesh = static_cast<MeshComponent*>( EditorSceneCommands::addComponent( pObj, hashed_string( "sw::MeshComponent" ) ) );
    SW_ASSERT_NOT_NULL( pMesh );
    SW_EXPECT_NOT_NULL( pMesh->getRawMesh() );
    SW_ASSERT_TRUE( stack.canUndo() );

    stack.undo();
    pManager->mergePendingAdds();
    GameObject* pRestored = pManager->findGameObjectById( pObj->getObjectId() );
    SW_ASSERT_NOT_NULL( pRestored );
    SW_EXPECT_NULL( pRestored->getComponent<MeshComponent>() );
}

/**
 * @brief [EditorSceneCommandsTest] 계층 창의 재부모 · 부모 떼기는 오브젝트를 놓인 자리에 둔다
 * @details 붙이기가 로컬을 지키면 끌어 놓은 오브젝트가 새 부모의 위치 · 회전 · 크기만큼 튄다(유니티 계층 창 · 언리얼 아웃라이너는 월드를 지킨다).
 */
SW_TEST_CASE( EditorSceneCommandsTest, ReparentAndUnparentKeepTheWorldPlace )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "ReparentProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    GameObjectManager*        pManager = pScene->getObjectManager();
    GameObject*               pShelf   = pManager->createGameObject( hashed_string( "Shelf" ) );
    GameObject*               pBook    = pManager->createGameObject( hashed_string( "Book" ) );
    SceneComponent*           pShelfSc = pShelf->addComponent<SceneComponent>();
    SceneComponent*           pBookSc  = pBook->addComponent<SceneComponent>();
    SW_ASSERT_TRUE( pShelfSc != nullptr && pBookSc != nullptr );
    pShelfSc->setLocalPosition( float3( 0.0f, 5.0f, 0.0f ) );
    pShelfSc->setLocalScale( float3( 3.0f, 3.0f, 3.0f ) );
    pBookSc->setLocalPosition( float3( 1.0f, 2.0f, 0.0f ) );
    pManager->mergePendingAdds();
    pManager->flushSceneTransforms();

    SW_ASSERT_TRUE( EditorSceneCommands::reparent( pBook, pShelf ) );
    pManager->flushSceneTransforms();
    SW_EXPECT_TRUE( pBook->getParent() == pShelf );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pBookSc->getWorldPosition()._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pBookSc->getWorldPosition()._y, 1e-3f );

    SW_ASSERT_TRUE( EditorSceneCommands::unparent( pBook ) );
    pManager->flushSceneTransforms();
    SW_EXPECT_TRUE( pBook->getParent() == nullptr );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pBookSc->getWorldPosition()._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pBookSc->getWorldPosition()._y, 1e-3f );
}

/**
 * @brief [EditorSceneCommandsTest] 표면 붙이기는 다른 오브젝트의 월드 윗면에 올린다 — 부모가 키운 바닥도
 * @details 다른 메시의 윗면을 **로컬** 스케일 × 단위 상자로 셈하면 부모가 두 배로 키운 바닥의 윗면을 절반 높이로 잡아 파묻힌다.
 */
SW_TEST_CASE( EditorSceneCommandsTest, SurfaceSnapLandsOnTheWorldTopOfAScaledFloor )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "SurfaceSnapProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    GameObjectManager*        pManager = pScene->getObjectManager();

    GameObject*     pStage   = pManager->createGameObject( hashed_string( "Stage" ) );
    SceneComponent* pStageSc = pStage->addComponent<SceneComponent>();
    pStageSc->setLocalScale( float3( 2.0f, 2.0f, 2.0f ) );
    GameObject*    pFloor     = pManager->createGameObject( hashed_string( "Floor" ) );
    MeshComponent* pFloorMesh = pFloor->addComponent<MeshComponent>();
    SW_ASSERT_TRUE( pStageSc != nullptr && pFloorMesh != nullptr );
    pFloorMesh->setLocalScale( float3( 5.0f, 1.0f, 5.0f ) ); // 단위 상자 — 월드 윗면은 1.0(부모 ×2)
    SW_ASSERT_TRUE( pFloor->attachToParent( pStage ) );
    GameObject*    pCrate     = pManager->createGameObject( hashed_string( "Crate" ) );
    MeshComponent* pCrateMesh = pCrate->addComponent<MeshComponent>();
    SW_ASSERT_NOT_NULL( pCrateMesh );
    pManager->mergePendingAdds();
    pManager->flushSceneTransforms();

    float3 translation( 1.0f, 4.0f, 1.0f );
    EditorSceneCommands::snapTranslationToSurface( pCrate, translation );
    SW_EXPECT_NEAR_EQUAL( 1.0f + 0.5f, translation._y, 1e-3f ); // 바닥 윗면 1.0 + 상자 반 높이 0.5
}

/**
 * @brief [EditorSceneCommandsTest] "같은 종류 · 같은 태그 모두 선택" 은 파생 컴포넌트와 아래 계층 태그까지 모은다
 * @details 메시 컴포넌트를 고르면 메시가 있는 오브젝트가 모두, `Enemy` 태그를 고르면 `Enemy.Boss` 도 같이 골라져야 한다(유니티 Select All of Type ·
 *          언리얼 Select All With Same Tag). 지울 표시가 된 오브젝트는 빠진다.
 */
SW_TEST_CASE( EditorSceneCommandsTest, CollectObjectsByComponentTypeAndTag )
{
    GameObjectManager manager;
    GameObject*       pMeshed = manager.createGameObject( hashed_string( "Meshed" ) );
    GameObject*       pPlain  = manager.createGameObject( hashed_string( "Plain" ) );
    GameObject*       pEmpty  = manager.createGameObject( hashed_string( "Empty" ) );
    GameObject*       pDoomed = manager.createGameObject( hashed_string( "Doomed" ) );
    SW_ASSERT_NOT_NULL( pMeshed->addComponent<MeshComponent>() );
    SW_ASSERT_NOT_NULL( pPlain->addComponent<SceneComponent>() );
    SW_ASSERT_NOT_NULL( pDoomed->addComponent<SceneComponent>() );
    pMeshed->addTag( TagID::request( "Enemy.Boss" ) );
    pPlain->addTag( TagID::request( "Enemy" ) );
    pEmpty->addTag( TagID::request( "Ally" ) );
    pDoomed->addTag( TagID::request( "Enemy" ) );
    manager.mergePendingAdds();
    manager.destroyObject( pDoomed );

    vector<GameObject*> listObject;
    EditorSceneCommands::collectObjectsWithComponent( manager, SceneComponent::StaticType(), listObject );
    SW_EXPECT_EQUAL( size_t( 2 ), listObject.size() ); // 메시 컴포넌트도 SceneComponent 다
    EditorSceneCommands::collectObjectsWithComponent( manager, MeshComponent::StaticType(), listObject );
    SW_ASSERT_EQUAL( size_t( 1 ), listObject.size() );
    SW_EXPECT_TRUE( listObject[0] == pMeshed );

    EditorSceneCommands::collectObjectsWithTag( manager, TagID::request( "Enemy" ), listObject );
    SW_EXPECT_EQUAL( size_t( 2 ), listObject.size() );
    EditorSceneCommands::collectObjectsWithTag( manager, TagID::request( "enemy.boss" ), listObject );
    SW_ASSERT_EQUAL( size_t( 1 ), listObject.size() );
    SW_EXPECT_TRUE( listObject[0] == pMeshed );
    EditorSceneCommands::collectObjectsWithTag( manager, TagID{}, listObject );
    SW_EXPECT_TRUE( listObject.empty() );
}
