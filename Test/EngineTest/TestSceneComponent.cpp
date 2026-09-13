#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// SceneComponent — 부모-자식 트랜스폼 전파와 더티 전파, 큰 월드 좌표.

// ------------------------------------------------------------------------------
// 4) SceneComponentTest — 계층·더티·카메라 상대
// ------------------------------------------------------------------------------
/**
 * @brief [SceneComponentTest] 부모-자식 계층과 dirty 전파
 */
SW_TEST_CASE( SceneComponentTest, ParentChildHierarchyAndDirtyPropagation )
{
    sw::GameObjectManager manager;
    sw::GameObject*       parentActorPtr = manager.createGameObject( sw::hashed_string( "ParentActor" ) );
    sw::GameObject&       parentActor    = *parentActorPtr;
    sw::GameObject*       childActorPtr  = manager.createGameObject( sw::hashed_string( "ChildActor" ) );
    sw::GameObject&       childActor     = *childActorPtr;

    parentActor.addComponent<SceneComponent>();
    childActor.addComponent<SceneComponent>();

    SceneComponent* parentComp = parentActor.getComponent<SceneComponent>();
    SceneComponent* childComp  = childActor.getComponent<SceneComponent>();

    parentComp->setLocalPosition( float3( 10.0f, 0.0f, 0.0f ) );
    childComp->setLocalPosition( float3( 5.0f, 2.0f, 0.0f ) );

    bool attachOk = childComp->attachToComponent( parentComp );
    SW_EXPECT_TRUE( attachOk );

    float3 childWorldPos = childComp->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 15.0f, childWorldPos._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, childWorldPos._y, 1e-4f );

    float4x4 childWorldMat = childComp->getWorldMatrix();
    SW_EXPECT_NEAR_EQUAL( 15.0f, childWorldMat._41, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, childWorldMat._42, 1e-4f );

    parentComp->setLocalPosition( float3( 20.0f, 0.0f, 0.0f ) );
    childWorldPos = childComp->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 25.0f, childWorldPos._x, 1e-4f );

    childWorldMat = childComp->getWorldMatrix();
    SW_EXPECT_NEAR_EQUAL( 25.0f, childWorldMat._41, 1e-4f );
}

/**
 * @brief [SceneComponentTest] 부모 회전·스케일이 자식에 전파
 */
SW_TEST_CASE( SceneComponentTest, ParentRotationScalePropagatesToChild )
{
    sw::GameObjectManager manager;
    sw::GameObject*       parentActorPtr = manager.createGameObject( sw::hashed_string( "RotatedScaledParent" ) );
    sw::GameObject&       parentActor    = *parentActorPtr;
    sw::GameObject*       childActorPtr  = manager.createGameObject( sw::hashed_string( "RotatedScaledChild" ) );
    sw::GameObject&       childActor     = *childActorPtr;

    parentActor.addComponent<SceneComponent>();
    childActor.addComponent<SceneComponent>();

    SceneComponent* parentComp = parentActor.getComponent<SceneComponent>();
    SceneComponent* childComp  = childActor.getComponent<SceneComponent>();

    // yaw 90도: 로컬 +X 가 월드 -Z 가 된다(오른손 Y-up).
    parentComp->setLocalPosition( float3( 10.0f, 0.0f, 0.0f ) );
    parentComp->setLocalRotation( float3( 0.0f, MathUtil::HalfPi, 0.0f ) );
    parentComp->setLocalScale( float3( 2.0f, 2.0f, 2.0f ) );

    childComp->setLocalPosition( float3( 1.0f, 0.0f, 0.0f ) );
    SW_ASSERT_TRUE( childComp->attachToComponent( parentComp ) );

    const float3 childWorldPos = childComp->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 10.0f, childWorldPos._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, childWorldPos._y, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, childWorldPos._z, 1e-3f ); // 스케일 1*2 후 yaw 90

    const float4x4 childWorldMat = childComp->getWorldMatrix();
    SW_EXPECT_NEAR_EQUAL( childWorldPos._x, childWorldMat._41, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( childWorldPos._y, childWorldMat._42, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( childWorldPos._z, childWorldMat._43, 1e-3f );

    const double3  cameraPos( 10.0, 0.0, 0.0 );
    const float4x4 cameraRel = childComp->getCameraRelativeWorldMatrix( cameraPos );
    SW_EXPECT_NEAR_EQUAL( 0.0f, cameraRel._41, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, cameraRel._42, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, cameraRel._43, 1e-3f );
    // 계층 스케일은 카메라 상대 행렬에 남아야 한다(상단 3x3 이 단위행렬이면 안 됨).
    const float3 camScale = cameraRel.getScale();
    SW_EXPECT_NEAR_EQUAL( 2.0f, camScale._x, 1e-3f );
}

/**
 * @brief [SceneComponentTest] 대형 월드 좌표와 카메라 상대 렌더링
 */
SW_TEST_CASE( SceneComponentTest, LargeWorldCoordinatesAndCameraRelativeRendering )
{
    sw::GameObjectManager manager;
    sw::GameObject*       actorPtr = manager.createGameObject( sw::hashed_string( "LWCActor" ) );
    sw::GameObject&       actor    = *actorPtr;

    SceneComponent* comp = actor.addComponent<SceneComponent>();

    comp->setLocalPosition( float3( 1000000.5f, 500000.25f, 0.0f ) );

    double3 lwcPos = comp->getWorldPositionLWC();
    SW_EXPECT_NEAR_EQUAL( 1000000.5, lwcPos._x, 1e-6 );
    SW_EXPECT_NEAR_EQUAL( 500000.25, lwcPos._y, 1e-6 );

    double3  cameraWorldPos( 1000000.0, 500000.0, 0.0 );
    float4x4 cameraRelMat = comp->getCameraRelativeWorldMatrix( cameraWorldPos );

    SW_EXPECT_NEAR_EQUAL( 0.5f, cameraRelMat._41, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, cameraRelMat._42, 1e-4f );
}

/**
 * @brief [SceneComponentTest] 64비트 정밀도 대규모 월드 좌표계 (LWC) 및 부모-자식 합성 검증
 */
SW_TEST_CASE( SceneComponentTest, LargeWorldCoordinatesHierarchy )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* root       = manager.createGameObject( hashed_string( "LWCRoot" ) );
    GameObject* child      = manager.createGameObject( hashed_string( "LWCChild" ) );
    GameObject* grandChild = manager.createGameObject( hashed_string( "LWCGandChild" ) );

    root->addComponent<SceneComponent>();
    child->addComponent<SceneComponent>();
    grandChild->addComponent<SceneComponent>();

    SceneComponent* rootSc       = root->getComponent<SceneComponent>();
    SceneComponent* childSc      = child->getComponent<SceneComponent>();
    SceneComponent* grandChildSc = grandChild->getComponent<SceneComponent>();

    SW_ASSERT_NOT_NULL( rootSc );
    SW_ASSERT_NOT_NULL( childSc );
    SW_ASSERT_NOT_NULL( grandChildSc );

    childSc->attachToComponent( rootSc );
    grandChildSc->attachToComponent( childSc );

    // 로컬 좌표 설정
    rootSc->setLocalPosition( float3( 100.0f, 200.0f, 300.0f ) );
    childSc->setLocalPosition( float3( 10.0f, 20.0f, 30.0f ) );
    grandChildSc->setLocalPosition( float3( 1.0f, 2.0f, 3.0f ) );

    manager.tick( 0.016f );

    const double3 rootLWC = rootSc->getWorldPositionLWC();
    SW_EXPECT_NEAR_EQUAL( 100.0, rootLWC._x, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 200.0, rootLWC._y, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 300.0, rootLWC._z, 0.0001 );

    const double3 childLWC = childSc->getWorldPositionLWC();
    SW_EXPECT_NEAR_EQUAL( 110.0, childLWC._x, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 220.0, childLWC._y, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 330.0, childLWC._z, 0.0001 );

    const double3 grandChildLWC = grandChildSc->getWorldPositionLWC();
    SW_EXPECT_NEAR_EQUAL( 111.0, grandChildLWC._x, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 222.0, grandChildLWC._y, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 333.0, grandChildLWC._z, 0.0001 );

    // 카메라 상대 4x4 월드 변환 행렬 검증
    const double3  cameraPos( 110.0, 220.0, 330.0 );
    const float4x4 relMatrix = grandChildSc->getCameraRelativeWorldMatrix( cameraPos );
    // 상대 위치는 (111-110, 222-220, 333-330) = (1, 2, 3)
    SW_EXPECT_NEAR_EQUAL( 1.0f, relMatrix._41, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, relMatrix._42, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, relMatrix._43, 0.001f );
}

/**
 * @brief [SceneHierarchyTest] 트랜스폼 더티 전파: 자식 이동 시 부모의 bIsTransformDirty는 0 유지, bHasDirtyDescendant는 1
 */
SW_TEST_CASE( SceneHierarchyTest, TransformDirtyPropagationAndEarlyOut )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pParentObj = manager.createGameObject( sw::hashed_string( "Parent" ) );
    sw::GameObject*       pChildObj  = manager.createGameObject( sw::hashed_string( "Child" ) );

    sw::SceneComponent* pParentSc = pParentObj->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pChildSc  = pChildObj->addComponent<sw::SceneComponent>();

    SW_ASSERT_NOT_NULL( pParentSc );
    SW_ASSERT_NOT_NULL( pChildSc );

    pChildSc->attachToComponent( pParentSc );

    pParentSc->setLocalPosition( sw::float3( 10.0f, 0.0f, 0.0f ) );
    pChildSc->setLocalPosition( sw::float3( 5.0f, 0.0f, 0.0f ) );

    // Force update world matrix to clear dirty flags
    pParentSc->getWorldMatrix();
    pChildSc->getWorldMatrix();

    SW_EXPECT_EQUAL( 0, static_cast<int32>( pParentSc->isTransformDirty() ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( pChildSc->isTransformDirty() ) );

    // Move child only
    pChildSc->setLocalPosition( sw::float3( 20.0f, 0.0f, 0.0f ) );

    // Child must be dirty
    SW_EXPECT_EQUAL( 1, static_cast<int32>( pChildSc->isTransformDirty() ) );
    // Parent transform must NOT be dirty (child move does not dirty parent)
    SW_EXPECT_EQUAL( 0, static_cast<int32>( pParentSc->isTransformDirty() ) );
    // Parent must have bHasDirtyDescendant == 1
    SW_EXPECT_EQUAL( 1, static_cast<int32>( pParentSc->hasDirtyDescendant() ) );

    // When child world matrix is queried, it composes correctly from parent
    const sw::float3 childPos = pChildSc->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 30.0f, childPos._x, 0.001f );
}
