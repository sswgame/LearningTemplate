#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/Component/SceneTransformStorage.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

#include <thread>

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
    sw::RegisterMockComponents();

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

    SW_ASSERT_TRUE( childSc->attachToComponent( rootSc ) );
    SW_ASSERT_TRUE( grandChildSc->attachToComponent( childSc ) );

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

    SW_ASSERT_TRUE( pChildSc->attachToComponent( pParentSc ) );

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
    sw::RegisterMockComponents();

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
 * @brief [SceneComponentTest] 틱 안에서 태그를 모두 지우면 틱이 끝난 뒤에 지운다 — `removeTag` 와 같이
 * @details `clearTags` 만 미루지 않고 바로 지워, 병렬 틱의 다른 워커가 `hasTag` 로 같은 컨테이너를 읽는 동안 비웠다.
 */
SW_TEST_CASE( SceneComponentTest, ClearTagsInsideTickIsDeferredUntilAfterTheTick )
{
    constexpr TagID kProbeTag = "Status.Probe"_tag;

    sw::GameObjectManager manager;
    sw::RegisterMockComponents();
    sw::GameObject*             pObj  = manager.createGameObject( sw::hashed_string( "TaggedTicker" ) );
    sw::MockTickSceneComponent* pComp = pObj->addComponent<sw::MockTickSceneComponent>();
    SW_ASSERT_NOT_NULL( pComp );
    pObj->addTag( kProbeTag );
    SW_ASSERT_TRUE( pObj->hasTag( kProbeTag ) );

    pComp->_probeTag         = kProbeTag;
    pComp->_bClearTagsOnTick = SW_TRUE;
    manager.tick( 0.016f );

    SW_EXPECT_TRUE( pComp->_bProbeTagKeptInTick == SW_TRUE );
    SW_EXPECT_FALSE( pObj->hasTag( kProbeTag ) );
}

/**
 * @brief [SceneComponentTest] 틱 안에서 붙일 수 없는 부모에 붙이면 미루기 전에 false 다
 * @details 틱 중의 붙이기는 틱 뒤로 미뤄지는데, 미룬 일은 자기 매니저로 부모를 다시 찾는다. 다른 매니저의 부모는 그때 못 찾고 조용히
 *          버려졌고 호출은 이미 true 를 돌려준 뒤였다 — 부른 쪽은 붙었다고 믿는다. 거르는 것은 미루기 전이다.
 */
SW_TEST_CASE( SceneComponentTest, AttachInsideTickIsRejectedBeforeItIsDeferred )
{
    sw::GameObjectManager managerChild;
    sw::GameObjectManager managerParent;
    sw::RegisterMockComponents();

    sw::GameObject* pParent = managerParent.createGameObject( sw::hashed_string( "OtherSceneParent" ) );
    SW_ASSERT_NOT_NULL( pParent->addComponent<sw::SceneComponent>() );
    sw::GameObject*             pChild     = managerChild.createGameObject( sw::hashed_string( "TickChild" ) );
    sw::MockTickSceneComponent* pChildComp = pChild->addComponent<sw::MockTickSceneComponent>();
    SW_ASSERT_NOT_NULL( pChildComp );

    pChildComp->_pTryParentOnTick      = pParent;
    pChildComp->_bAttachAcceptedInTick = SW_TRUE;
    {
        test::ScopedDefensiveTestLog defensive( "attaching to another scene's object inside a tick" );
        managerChild.tick( 0.016f );
    }
    SW_EXPECT_FALSE( pChildComp->_bAttachAcceptedInTick == SW_TRUE );
    SW_EXPECT_TRUE( pChild->getParent() == nullptr );
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
        sw::RegisterMockComponents();
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

/**
 * @brief [SceneComponentTest] 트랜스폼 값은 저장소의 칸에 있고, 리플렉션 이름으로 찾은 자리도 그 칸이다
 * @details 로컬 TRS · 월드 행렬은 컴포넌트가 아니라 `SceneTransformStorage` 의 칸에 있다. 씬에 붙지 않은 컴포넌트도 칸이 있고(만들 때 받고
 *          소멸할 때 놓는다), 직렬화 키 `_localPosition` 의 값 자리는 그 칸이어야 인스펙터 · 직렬화 · 씬 파일이 그대로 돈다.
 */
SW_TEST_CASE( SceneComponentTest, TransformValuesLiveInTheStorageSlot )
{
    sw::SceneTransformStorage& storage    = sw::SceneTransformStorage::get();
    const uint32               liveBefore = storage.getLiveSlotCount();
    {
        sw::SceneComponent comp;
        SW_EXPECT_EQUAL( liveBefore + 1, storage.getLiveSlotCount() );
        const uint32            slot  = comp.getTransformSlot();
        sw::SceneTransformPage* pPage = storage.findPage( slot );
        SW_ASSERT_NOT_NULL( pPage );
        const uint32 pageIndex = slot & sw::SceneTransformPage::kSlotMask;
        SW_EXPECT_TRUE( pPage->_arrOwner[pageIndex] == &comp );

        comp.setLocalPosition( sw::float3( 1.0f, 2.0f, 3.0f ) );
        SW_EXPECT_NEAR_EQUAL( 2.0f, pPage->_arrLocalPosition[pageIndex]._y, 1e-6f );

        const sw::TypeInfo* pType = sw::SceneComponent::StaticType();
        SW_ASSERT_NOT_NULL( pType );
        const sw::PropertyInfo* pPositionProp = pType->findPropertyInHierarchy( sw::hashed_string( "_localPosition" ) );
        SW_ASSERT_NOT_NULL( pPositionProp );
        SW_EXPECT_TRUE( pPositionProp->hasValueAccessor() );
        SW_EXPECT_TRUE( pPositionProp->getRawPtr( &comp ) == &pPage->_arrLocalPosition[pageIndex] );

        // 리플렉션으로 쓰면 칸에 쓰이고, 알림(onPropertyChanged)을 거쳐 월드도 따라온다.
        pPositionProp->setValue( &comp, sw::float3( 4.0f, 5.0f, 6.0f ) );
        SW_EXPECT_NEAR_EQUAL( 5.0f, comp.getLocalPosition()._y, 1e-6f );
        SW_EXPECT_NEAR_EQUAL( 5.0f, comp.getWorldPosition()._y, 1e-6f );
        SW_EXPECT_NEAR_EQUAL( 6.0f, comp.getWorldMatrix()._43, 1e-6f );
    }
    // 소멸하면 칸을 놓는다.
    SW_EXPECT_EQUAL( liveBefore, storage.getLiveSlotCount() );
}

/**
 * @brief [SceneComponentTest] 회전 변환 캐시는 회전이 바뀔 때마다 따라가고, 결과는 캐시 없는 합성과 비트까지 같다
 * @details 칸은 쿼터니언과 그것을 만든 오일러를 들고 있다가 같으면 삼각 함수를 건너뛴다(언리얼 `FRotationConversionCache` 의 자리).
 *          회전을 바꾸고 · 0 으로 돌리고 · 처음 값으로 되돌리는 동안 한 번이라도 옛 쿼터니언을 쓰면 행렬이 틀린다.
 */
SW_TEST_CASE( SceneComponentTest, RotationCacheFollowsEveryRotationChange )
{
    sw::SceneComponent comp;
    const sw::float3   position( 1.0f, -2.0f, 3.0f );
    const sw::float3   scale( 2.0f, 1.0f, 0.5f );
    comp.setLocalPosition( position );
    comp.setLocalScale( scale );

    const sw::float3 arrRotation[] = {
        sw::float3( 0.3f, 0.0f, 0.0f ),
        sw::float3( 0.3f, 1.1f, 0.0f ),
        sw::float3( 0.0f, 0.0f, 0.0f ),
        sw::float3( 0.3f, 0.0f, 0.0f ),
        sw::float3( -0.7f, 0.2f, 2.5f ),
    };
    uint32 mismatchCount = 0;
    for ( const sw::float3& rotation : arrRotation )
    {
        comp.setLocalRotation( rotation );
        const sw::float4x4 world    = comp.getWorldMatrix();
        const sw::float4x4 expected = sw::float4x4::createTrs( position, rotation, scale );
        if ( sw::Memory::compare( &world, &expected, sizeof( sw::float4x4 ) ) != 0 )
            ++mismatchCount;
    }
    SW_EXPECT_EQUAL( 0u, mismatchCount );
}

/**
 * @brief [SceneComponentTest] 틱 안에서 자기 오브젝트의 칸에 쓴 값은 틱이 끝나야 보이고, 메시는 틱 뒤 렌더 더티가 된다
 * @details 틱 중의 세터는 자기 오브젝트를 틱하는 스레드면 칸의 대기 자리에 쓰고, 틱 뒤 적용이 칸 번호 목록을 따라 옮긴다. 그 사이에
 *          다른 오브젝트가 읽는 로컬 · 월드 값은 틱 전 값이어야 한다(예전 쓰기 큐와 같다). 메시는 훅 없이 칸의 프리미티브 번호로
 *          등록부에 더티가 찍혀야 렌더 수집이 움직임을 본다. 오브젝트가 16 개 미만이라 틱은 이 스레드가 만든 순서대로 돈다 — 쓰는 쪽이
 *          먼저 돌고 읽는 쪽이 나중에 돈다.
 */
SW_TEST_CASE( SceneComponentTest, TickWriteToOwnSlotIsHiddenUntilAfterTheTick )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::GameObject*             pMoverObj = manager.createGameObject( sw::hashed_string( "SlotMover" ) );
    sw::MockTickSceneComponent* pMover    = pMoverObj->addComponent<sw::MockTickSceneComponent>();
    sw::GameObject*             pWatchObj = manager.createGameObject( sw::hashed_string( "SlotWatcher" ) );
    sw::MockTickSceneComponent* pWatcher  = pWatchObj->addComponent<sw::MockTickSceneComponent>();
    sw::GameObject*             pMeshObj  = manager.createGameObject( sw::hashed_string( "SlotMesh" ) );
    sw::MeshComponent*          pMesh     = pMeshObj->addComponent<sw::MeshComponent>();
    sw::MockMeshComponent*      pMeshMove = pMeshObj->addComponent<sw::MockMeshComponent>();
    SW_ASSERT_NOT_NULL( pMover );
    SW_ASSERT_NOT_NULL( pWatcher );
    SW_ASSERT_NOT_NULL( pMesh );
    SW_ASSERT_NOT_NULL( pMeshMove );
    pWatcher->_pWatchedComp    = pMover;
    pMover->_bWriteLocalOnTick = SW_TRUE;
    pMover->_tickLocalPos      = sw::float3( 5.0f, 0.0f, 0.0f );
    pMeshMove->_pTickMoveComp  = pMesh;
    pMeshMove->_tickMovePos    = sw::float3( 0.0f, 3.0f, 0.0f );

    manager.tick( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pMover->getWorldPosition()._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pMesh->getWorldPosition()._y, 1e-6f );

    manager.getPrimitiveRegistry().clearDirty();
    pMover->_tickLocalPos   = sw::float3( 7.0f, 0.0f, 0.0f );
    pMeshMove->_tickMovePos = sw::float3( 0.0f, 4.0f, 0.0f );
    manager.tick( 0.016f );

    // 틱 중에 읽은 것은 틱 전 값(5)이다. 쓰는 쪽이 먼저 돌아 대기 자리에는 7 이 있었다.
    SW_EXPECT_NEAR_EQUAL( 5.0f, pWatcher->_watchedLocalPos._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pWatcher->_watchedWorldPos._x, 1e-6f );
    // 틱 뒤에는 새 값이고, 대기 목록은 비었고, 메시는 렌더 더티다.
    SW_EXPECT_NEAR_EQUAL( 7.0f, pMover->getLocalPosition()._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 7.0f, pMover->getWorldPosition()._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, pMesh->getWorldPosition()._y, 1e-6f );
    SW_EXPECT_FALSE( manager.getTransformHierarchy().hasQueuedWrites() );
    SW_EXPECT_TRUE( manager.getPrimitiveRegistry().hasDirty() );
}

namespace
{
    /** @brief 틱 큐 건 하나와 그것을 낸 틱의 주인 오브젝트 id(순서 키)입니다. */
    struct KeyedTickWrite
    {
        sw::SceneTransformWrite _write;
        uint64                  _writerId{ 0 };
    };

    /** @brief 컴포넌트 하나의 로컬 위치 X 를 쓰는 틱 큐 건입니다. */
    KeyedTickWrite makeTickWrite( sw::SceneComponent* pTarget, uint64 writerId, float32 positionX )
    {
        KeyedTickWrite keyed{};
        keyed._write.setValue( sw::SceneTransformPage::kLocalPosition, sw::float3( positionX, 0.0f, 0.0f ) );
        keyed._write._handle  = pTarget->getHandle();
        keyed._write._pTarget = pTarget;
        keyed._writerId       = writerId;
        return keyed;
    }

    /** @brief 이 스레드의 스크래치 슬롯에 건을 올립니다. */
    bool queueTickWrite( sw::SceneTransformHierarchy& hierarchy, const KeyedTickWrite& keyed )
    {
        return hierarchy.queueWriteParallel( keyed._write, keyed._writerId );
    }

    /** @brief 다른 스레드(다른 스크래치 슬롯)에서 건들을 틱 큐에 올리고, 받은 도우미 슬롯을 돌려줍니다. 모두 올렸으면 true 입니다. */
    bool queueFromAnotherThread( sw::SceneTransformHierarchy& hierarchy, const sw::vector<KeyedTickWrite>& listWrite )
    {
        bool        bAllQueued = true;
        std::thread other( [&]()
        {
            for ( const KeyedTickWrite& keyed : listWrite )
                bAllQueued = queueTickWrite( hierarchy, keyed ) && bAllQueued;
            sw::engine::getTaskManager().releaseCurrentThreadHelperSlot();
        } );
        other.join();
        return bAllQueued;
    }
} // namespace

/**
 * @brief [SceneComponentTest] 여러 오브젝트의 틱이 한 컴포넌트에 쓰면 이기는 값은 스레드 배정과 무관하다 — 쓴 오브젝트 id 가 큰 쪽
 * @details 틱 큐는 **쓴 스레드 슬롯** 단위로 적용돼 슬롯 번호가 큰 쪽이 이겼다. 같은 장면도 어느 워커가 그 오브젝트를 틱했느냐에 따라 실행마다
 *          다른 값이 남았고, 건수가 문턱(1024)을 넘으면 두 워커가 그 칸을 동시에 썼다. 이제 (대상, 쓴 오브젝트, 순번)으로 정렬해 대상 경계에서만
 *          잡을 나눈다(유니티 `EntityCommandBuffer.ParallelWriter` 의 sortKey). 두 스레드가 서로 다른 슬롯에 쓰게 하고 역할을 바꿔 두 번 돌린다.
 */
SW_TEST_CASE( SceneComponentTest, CrossObjectTickWritesResolveByWriterNotThread )
{
    auto runOnce = []( bool bThisThreadWritesHigh ) -> float32
    {
        sw::GameObjectManager manager;
        sw::GameObject*       pObj  = manager.createGameObject( sw::hashed_string( "WriteTarget" ) );
        sw::SceneComponent*   pComp = pObj->addComponent<sw::SceneComponent>();
        manager.flushSceneTransforms();

        sw::SceneTransformHierarchy& hierarchy = manager.getTransformHierarchy();
        hierarchy.beginTickWrites();
        const KeyedTickWrite highWrite = makeTickWrite( pComp, 20, 2.0f );
        const KeyedTickWrite lowWrite  = makeTickWrite( pComp, 10, 1.0f );
        SW_EXPECT_TRUE( queueTickWrite( hierarchy, bThisThreadWritesHigh ? highWrite : lowWrite ) );
        SW_EXPECT_TRUE( queueFromAnotherThread( hierarchy, { bThisThreadWritesHigh ? lowWrite : highWrite } ) );
        hierarchy.applyTickWrites( manager, manager.getPrimitiveRegistry() );
        return pComp->getLocalPosition()._x;
    };
    SW_EXPECT_NEAR_EQUAL( 2.0f, runOnce( true ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, runOnce( false ), 1e-6f );
}

/**
 * @brief [SceneComponentTest] 한 스레드가 서로 다른 오브젝트의 쓰기를 잇따라 올려도 합치지 않는다 — 합치면 순서 키가 틀어진다
 * @details 같은 슬롯의 직전 건이 같은 컴포넌트면 한 건으로 합친다. 쓴 오브젝트가 다른데 합치면, 합친 건은 앞 건의 키(30)를 들고 뒤 건의 값을
 *          싣는다 — 그 사이에 다른 스레드가 쓴 건(20)보다 뒤로 정렬돼 엉뚱한 값(10 의 값)이 이긴다. 기대: 키가 가장 큰 30 의 값.
 */
SW_TEST_CASE( SceneComponentTest, TickWritesFromDifferentWritersAreNotMerged )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj  = manager.createGameObject( sw::hashed_string( "MergeTarget" ) );
    sw::SceneComponent*   pComp = pObj->addComponent<sw::SceneComponent>();
    manager.flushSceneTransforms();

    sw::SceneTransformHierarchy& hierarchy = manager.getTransformHierarchy();
    hierarchy.beginTickWrites();
    SW_EXPECT_TRUE( queueTickWrite( hierarchy, makeTickWrite( pComp, 30, 3.0f ) ) );
    SW_EXPECT_TRUE( queueTickWrite( hierarchy, makeTickWrite( pComp, 10, 1.0f ) ) );
    SW_EXPECT_TRUE( queueFromAnotherThread( hierarchy, { makeTickWrite( pComp, 20, 2.0f ) } ) );
    hierarchy.applyTickWrites( manager, manager.getPrimitiveRegistry() );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pComp->getLocalPosition()._x, 1e-6f );
}

/**
 * @brief [SceneComponentTest] 배치에 같은 핸들이 두 번 있으면 병렬로 나눠도 배열에서 뒤의 것이 이긴다
 * @details 배치는 배열을 연속 구간으로 잘라 워커에 줬다. 같은 핸들이 여러 구간에 있으면 워커 여럿이 한 칸을 동시에 썼고, 마지막에 끝난 워커의
 *          값이 남았다(배열의 마지막 값이 아니다). 이제 대상 버킷(componentId)으로 나눠 한 대상은 한 워커가 배열 순서대로 쓴다. 반복 핸들을 배열
 *          전체에 흩어 두어(61 칸마다) 나눔이 어긋나면 거의 확실히 진다. 월드도 이긴 로컬과 맞아야 한다(잎 루트는 적용 자리에서 합성한다).
 */
SW_TEST_CASE( SceneComponentTest, BatchWithARepeatedHandleKeepsTheLastWrite )
{
    constexpr uint32 kCount = sw::SceneTransformHierarchy::kParallelWriteCount * 2;

    sw::GameObjectManager           manager;
    sw::vector<sw::SceneComponent*> listComp;
    for ( uint32 index = 0; index < kCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "BatchRepeat" ) );
        listComp.push_back( pObj->addComponent<sw::SceneComponent>() );
    }
    manager.flushSceneTransforms();

    constexpr uint32 kRepeatedComp = 0;
    constexpr uint32 kRepeatStride = 61; // 소수 — 버킷 수의 배수면 배열 자리로 나눠도 반복 건이 한 버킷에 모인다
    for ( uint32 round = 1; round <= 8; ++round )
    {
        sw::vector<sw::SceneTransformWrite> listWrite( kCount );
        float32                             lastRepeatedValue = 0.0f;
        for ( uint32 index = 0; index < kCount; ++index )
        {
            const bool    bRepeated  = ( index % kRepeatStride ) == kRepeatStride - 1;
            const float32 value      = static_cast<float32>( round * 10000 + index );
            listWrite[index]._handle = listComp[bRepeated ? kRepeatedComp : index]->getHandle();
            listWrite[index].setValue( sw::SceneTransformPage::kLocalPosition, sw::float3( value, 0.0f, 0.0f ) );
            if ( bRepeated )
                lastRepeatedValue = value;
        }

        manager.applyTransformBatch( listWrite.data(), kCount );
        manager.flushSceneTransforms();
        SW_EXPECT_NEAR_EQUAL( lastRepeatedValue, listComp[kRepeatedComp]->getLocalPosition()._x, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( lastRepeatedValue, listComp[kRepeatedComp]->getWorldPosition()._x, 1e-3f );
    }
}

/**
 * @brief [SceneComponentTest] 월드 세터는 돌고 커진 부모 아래에서도 그 월드 자리 · 회전에 놓는다 — 세 축이 섞인 회전도 그대로
 * @details 월드 세터가 없어 에디터 다섯 곳이 각자 바꿨고 셋이 틀렸다(월드 값을 로컬 칸에 · 월드 축 차이를 로컬 축에). 기즈모는 ImGuizmo 의 XYZ
 *          오일러로 분해해 넣어 엔진의 요 · 피치 · 롤 순서와 달라, 두 축 이상이 섞인 회전이 다른 회전으로 들어갔다. 언리얼 `SetWorldLocation` ·
 *          `SetWorldTransform` 처럼 엔진이 부모 기준으로 분해한다.
 */
SW_TEST_CASE( SceneComponentTest, WorldSettersRespectARotatedScaledParent )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pParentObj = manager.createGameObject( sw::hashed_string( "TurnedParent" ) );
    sw::SceneComponent*   pParent    = pParentObj->addComponent<sw::SceneComponent>();
    sw::GameObject*       pChildObj  = manager.createGameObject( sw::hashed_string( "WorldChild" ) );
    sw::SceneComponent*   pChild     = pChildObj->addComponent<sw::SceneComponent>();
    pParent->setLocalPosition( sw::float3( 10.0f, 0.0f, 0.0f ) );
    pParent->setLocalRotation( sw::float3( 0.2f, sw::MathUtil::HalfPi, -0.1f ) );
    pParent->setLocalScale( sw::float3( 2.0f, 2.0f, 2.0f ) );
    SW_ASSERT_TRUE( pChild->attachToComponent( pParent ) );
    manager.flushSceneTransforms();

    pChild->setWorldPosition( sw::float3( 5.0f, 1.0f, 2.0f ) );
    manager.flushSceneTransforms();
    const sw::float3 world = pChild->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 5.0f, world._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, world._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, world._z, 1e-4f );

    // 세 축이 모두 섞인 회전 · 균등 스케일(기울임 없음) — 행렬이 그대로 돌아와야 한다.
    const sw::float4x4 target = sw::float4x4::createTrs( sw::float3( 3.0f, 4.0f, 5.0f ), sw::float3( 0.3f, 0.7f, -0.4f ), sw::float3( 1.5f, 1.5f, 1.5f ) );
    pChild->setWorldTransform( target );
    manager.flushSceneTransforms();
    const sw::float4x4 result    = pChild->getWorldMatrix();
    const float32*     pExpected = target.data();
    const float32*     pActual   = result.data();
    for ( uint32 index = 0; index < 16; ++index )
        SW_EXPECT_NEAR_EQUAL( pExpected[index], pActual[index], 1e-4f );
}
