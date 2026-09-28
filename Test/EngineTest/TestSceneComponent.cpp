#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
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

    double3 lwcPos = comp->getWorldPositionLwc();
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

    const double3 rootLWC = rootSc->getWorldPositionLwc();
    SW_EXPECT_NEAR_EQUAL( 100.0, rootLWC._x, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 200.0, rootLWC._y, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 300.0, rootLWC._z, 0.0001 );

    const double3 childLWC = childSc->getWorldPositionLwc();
    SW_EXPECT_NEAR_EQUAL( 110.0, childLWC._x, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 220.0, childLWC._y, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 330.0, childLWC._z, 0.0001 );

    const double3 grandChildLWC = grandChildSc->getWorldPositionLwc();
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

/**
 * @brief [SceneComponentTest] 느리게 움직이는 물체가 제자리에 얼어붙지 않는지 검증
 * @details 세 setter 가 `getDistanceSquared(...) <= MathUtil::Epsilon` 로 "안 바뀌었다" 를
 *          판정했다. **제곱 거리를 제곱 안 한 허용치와 비교**한 것이라, 실제 거리로는 1e-3
 *          까지가 변화 없음으로 삼켜진다 — 의도한 부동소수 허용치보다 1000배 크다.
 *
 *          그리고 비교 기준이 **매번 현재 값**이라 오차가 쌓이지 않는다. 한 프레임에 1e-3
 *          보다 조금씩 움직이는 물체는 몇 초를 가도 **한 번도 움직이지 않는다.** 165Hz 에서
 *          0.08 유닛/초면 그 구간이다 — 천천히 도는 포탑이나 흘러가는 구름처럼 흔한 속도다.
 */
SW_TEST_CASE( SceneComponentTest, SlowMotionIsNotSwallowedByTheChangeThreshold )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "SlowMover" ) );
    SW_ASSERT_NOT_NULL( pObj );
    manager.mergePendingAdds();

    sw::SceneComponent* pSceneComp = pObj->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pSceneComp );

    // 165Hz 에서 0.08 유닛/초로 움직이는 물체가 한 프레임에 가는 거리.
    constexpr float32 kStepPerFrame = 0.0005f;
    constexpr uint32  kFrameCount   = 200;

    pSceneComp->setLocalPosition( sw::float3{ 0.0f, 0.0f, 0.0f } );
    for ( uint32 frame = 0; frame < kFrameCount; ++frame )
    {
        sw::float3 next = pSceneComp->getLocalPosition();
        next._x += kStepPerFrame;
        pSceneComp->setLocalPosition( next );
    }

    // 200 프레임(약 1.2초) 이면 0.1 만큼 가 있어야 한다.
    SW_EXPECT_NEAR_EQUAL( 0.1f, pSceneComp->getLocalPosition()._x, 0.005f );
}

/**
 * @brief [SceneComponentTest] 틱 안에서 부모에서 떼면 그 자리에서 떼지 않고 틱이 끝난 뒤에 뗀다
 * @details 틱 중에는 트랜스폼이 읽기 전용이라(다른 워커가 부모 사슬을 읽는다) `detachFromComponent` 는 자기 핸들로
 *          나중에 다시 불리도록 미룬다(`deferSelfCall`). 뗀 직후에는 부모가 그대로여야 하고, `tick()` 이 돌아오면
 *          떨어져 있어야 한다. `markTransformDirty` 도 같은 미루기를 쓴다.
 */
SW_TEST_CASE( SceneComponentTest, DetachInsideTickIsDeferredUntilAfterTheTick )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject*     pParent     = manager.createGameObject( sw::hashed_string( "DeferParent" ) );
    sw::SceneComponent* pParentComp = pParent->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pParentComp );

    sw::GameObject*             pChild     = manager.createGameObject( sw::hashed_string( "DeferChild" ) );
    sw::MockTickSceneComponent* pChildComp = pChild->addComponent<sw::MockTickSceneComponent>();
    SW_ASSERT_NOT_NULL( pChildComp );
    SW_ASSERT_TRUE( pChildComp->attachToComponent( pParentComp ) );
    SW_ASSERT_TRUE( pChildComp->getParent() == pParentComp );

    pChildComp->_bDetachOnTick = SW_TRUE;
    manager.tick( 0.016f );

    SW_EXPECT_TRUE( pChildComp->_bParentKeptInTick == SW_TRUE );
    SW_EXPECT_TRUE( pChildComp->getParent() == nullptr );
}

/**
 * @brief [SceneComponentTest] 플러시 전에 월드 값을 읽어도 렌더 프리미티브는 더티로 남는다.
 * @details 지연 합성(`getWorldPosition` 등)이 월드 캐시를 채우며 더티를 지웠지만 갱신 훅은 부르지 않았다. 그 노드는 이제 더티가
 *          아니라 플러시가 건너뛰고, 렌더 더티는 훅에서만 찍히므로 **아무도 찍지 않았다** — 인스펙터가 메시 위치를 세팅하고 곧장
 *          월드 위치를 읽는 경로에서, 화면의 메시가 옛 자리에 멈춰 있었다. 부모를 움직이고 자식을 읽는 경우도 같다.
 */
SW_TEST_CASE( SceneComponentTest, LazyWorldReadKeepsRenderDirty )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pParentObj  = manager.createGameObject( sw::hashed_string( "LazyParent" ) );
    sw::MeshComponent*    pParentMesh = pParentObj->addComponent<sw::MeshComponent>();
    sw::GameObject*       pChildObj   = manager.createGameObject( sw::hashed_string( "LazyChild" ) );
    sw::MeshComponent*    pChildMesh  = pChildObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pParentMesh );
    SW_ASSERT_NOT_NULL( pChildMesh );
    manager.flushSceneTransforms();

    // 잎 루트 하나 — 세팅 → 읽기 → 플러시.
    sw::GameObject*    pLeafObj  = manager.createGameObject( sw::hashed_string( "LazyLeaf" ) );
    sw::MeshComponent* pLeafMesh = pLeafObj->addComponent<sw::MeshComponent>();
    manager.flushSceneTransforms();
    manager.getPrimitiveRegistry().clearDirty();
    pLeafMesh->setLocalPosition( sw::float3( 5.0f, 0.0f, 0.0f ) );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pLeafMesh->getWorldPosition()._x, 1e-4f );
    manager.flushSceneTransforms();
    SW_EXPECT_TRUE( manager.getPrimitiveRegistry().hasDirty() );

    // 부모 · 자식 — 부모를 움직이고 자식을 읽는다.
    SW_ASSERT_TRUE( pChildMesh->attachToComponent( pParentMesh ) );
    manager.flushSceneTransforms();
    manager.getPrimitiveRegistry().clearDirty();
    pParentMesh->setLocalPosition( sw::float3( 0.0f, 9.0f, 0.0f ) );
    SW_EXPECT_NEAR_EQUAL( 9.0f, pChildMesh->getWorldPosition()._y, 1e-4f );
    manager.flushSceneTransforms();
    SW_EXPECT_TRUE( manager.getPrimitiveRegistry().hasDirty() );
}

/**
 * @brief [SceneComponentTest] 부모를 움직이고 자식 하나를 먼저 읽어도, 플러시는 그 형제까지 갱신하고 훅은 노드마다 한 번이다.
 * @details R → N → {C1, C2}. N 을 움직이고 C1 의 월드 위치를 읽으면 N · C1 사슬은 깨끗해진다. 플러시는 깨끗한 노드 아래로
 *          "자손 더티" 가 있을 때만 내려가므로 N 에 그것이 서 있어야 C2 에 닿는다. 직렬 표시는 세웠지만 배치(워커) 쪽 복사본은
 *          자손에게 더티만 세워, 배치로 N 을 옮기면 C2 가 옛 자리에 남았다. 두 경로를 다 본다.
 */
SW_TEST_CASE( SceneComponentTest, LazyReadDoesNotStrandDirtySiblings )
{
    for ( uint32 pass = 0; pass < 2; ++pass )
    {
        const bool            bBatch = ( pass == 0 );
        sw::GameObjectManager manager;
        sw::RegisterMockComponents( manager );
        sw::MockTickSceneComponent* arrNode[4] = {};
        const utf8*                 arrName[4] = { "R", "N", "C1", "C2" };
        for ( uint32 index = 0; index < 4; ++index )
        {
            sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( arrName[index] ) );
            arrNode[index]       = pObj->addComponent<sw::MockTickSceneComponent>();
            SW_ASSERT_NOT_NULL( arrNode[index] );
        }
        sw::MockTickSceneComponent* pRoot   = arrNode[0];
        sw::MockTickSceneComponent* pMiddle = arrNode[1];
        sw::MockTickSceneComponent* pFirst  = arrNode[2];
        sw::MockTickSceneComponent* pSecond = arrNode[3];
        SW_ASSERT_TRUE( pMiddle->attachToComponent( pRoot ) );
        SW_ASSERT_TRUE( pFirst->attachToComponent( pMiddle ) );
        SW_ASSERT_TRUE( pSecond->attachToComponent( pMiddle ) );
        pSecond->setLocalPosition( sw::float3( 0.0f, 0.0f, 1.0f ) );
        manager.flushSceneTransforms();
        for ( sw::MockTickSceneComponent* pNode : arrNode )
            pNode->_worldUpdateCount = 0;

        const sw::float3 movedPos( 4.0f, 0.0f, 0.0f );
        if ( bBatch )
        {
            sw::SceneTransformWrite write{};
            write._handle        = pMiddle->getHandle();
            write._localPosition = movedPos;
            write._bSetPosition  = SW_TRUE;
            SW_EXPECT_EQUAL( 1u, manager.applyTransformBatch( &write, 1 ) );
        }
        else
            pMiddle->setLocalPosition( movedPos );

        SW_EXPECT_NEAR_EQUAL( 4.0f, pFirst->getWorldPosition()._x, 1e-4f );
        manager.flushSceneTransforms();

        SW_EXPECT_FALSE( pSecond->isTransformDirty() );
        SW_EXPECT_NEAR_EQUAL( 4.0f, pSecond->getWorldPosition()._x, 1e-4f );
        SW_EXPECT_EQUAL( 0, pRoot->_worldUpdateCount );
        SW_EXPECT_EQUAL( 1, pMiddle->_worldUpdateCount );
        SW_EXPECT_EQUAL( 1, pFirst->_worldUpdateCount );
        SW_EXPECT_EQUAL( 1, pSecond->_worldUpdateCount );
    }
}
