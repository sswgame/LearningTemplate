#include "pch.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// GameObjectManager — 생성·검색·지연 파괴와 오브젝트/컴포넌트 풀의 재사용.

// ------------------------------------------------------------------------------
// 6) GameObjectManagerTest — 생성·검색·지연 파괴
// ------------------------------------------------------------------------------
/**
 * @brief [GameObjectManagerTest] 생성·검색·지연 파괴
 */
SW_TEST_CASE( GameObjectManagerTest, CreationSearchAndDeferredDestruction )
{
    sw::GameObjectManager manager;

    GameObject* hero  = manager.createGameObject( hashed_string( "Hero" ) );
    GameObject* enemy = manager.createGameObject( hashed_string( "Enemy" ) );
    SW_ASSERT_NOT_NULL( hero );
    SW_ASSERT_NOT_NULL( enemy );

    manager.tick( 0.0f );
    SW_EXPECT_EQUAL( size_t( 2 ), manager.getAllGameObjects().size() );
    SW_EXPECT_EQUAL( hero, manager.findGameObjectByName( hashed_string( "Hero" ) ) );
    SW_EXPECT_EQUAL( enemy, manager.findGameObjectById( enemy->getObjectId() ) );
    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Missing" ) ) );

    hero->setName( hashed_string( "HeroRenamed" ) );
    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Hero" ) ) );
    SW_EXPECT_EQUAL( hero, manager.findGameObjectByName( hashed_string( "HeroRenamed" ) ) );

    manager.destroyObject( enemy );
    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Enemy" ) ) );
    SW_EXPECT_NULL( manager.findGameObjectById( enemy->getObjectId() ) );
    SW_EXPECT_EQUAL( size_t( 2 ), manager.getAllGameObjects().size() );

    manager.tick( 0.0f );

    SW_EXPECT_EQUAL( size_t( 1 ), manager.getAllGameObjects().size() );
    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Enemy" ) ) );
    SW_EXPECT_NOT_NULL( manager.findGameObjectByName( hashed_string( "HeroRenamed" ) ) );

    manager.clear();
    SW_EXPECT_EQUAL( size_t( 0 ), manager.getAllGameObjects().size() );
}

/**
 * @brief [GameObjectManagerTest] 지연 파괴 후 이름·ID 조회가 비고 같은 이름을 다시 쓸 수 있다
 */
SW_TEST_CASE( GameObjectManagerTest, OwnershipReleasedAfterDeferredDestroy )
{
    sw::GameObjectManager manager;
    GameObject*           first = manager.createGameObject( hashed_string( "Owned" ) );
    SW_ASSERT_NOT_NULL( first );
    const uint64 firstId = first->getObjectId();

    manager.destroyObject( first );

    manager.tick( 0.0f );

    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Owned" ) ) );
    SW_EXPECT_NULL( manager.findGameObjectById( firstId ) );
    SW_EXPECT_EQUAL( size_t( 0 ), manager.getAllGameObjects().size() );

    GameObject* second = manager.createGameObject( hashed_string( "Owned" ) );
    SW_ASSERT_NOT_NULL( second );
    SW_EXPECT_EQUAL( second, manager.findGameObjectByName( hashed_string( "Owned" ) ) );
    SW_EXPECT_NOT_EQUAL( firstId, second->getObjectId() );
}

/**
 * @brief [GameObjectManagerTest] 순차·병렬 틱
 */
SW_TEST_CASE( GameObjectManagerTest, SequentialAndParallelTick )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* a = manager.createGameObject( hashed_string( "A" ) );
    GameObject* b = manager.createGameObject( hashed_string( "B" ) );
    a->addComponent<MockMeshComponent>();
    b->addComponent<MockMeshComponent>();

    MockMeshComponent* aMesh = a->getComponent<MockMeshComponent>();
    MockMeshComponent* bMesh = b->getComponent<MockMeshComponent>();

    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 1, aMesh->_tickCount );
    SW_EXPECT_EQUAL( 1, bMesh->_tickCount );

    manager.tick( 0.016f );
    aMesh = a->getComponent<MockMeshComponent>();
    bMesh = b->getComponent<MockMeshComponent>();
    SW_EXPECT_EQUAL( 2, aMesh->_tickCount );
    SW_EXPECT_EQUAL( 2, bMesh->_tickCount );

    manager.clear();
}

/**
 * @brief [GameObjectManagerTest] 병렬 틱이 안정된 계층 트랜스폼을 읽음
 */
SW_TEST_CASE( GameObjectManagerTest, ParallelTickReadsStableHierarchyTransforms )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* parentObj = manager.createGameObject( hashed_string( "Parent" ) );
    GameObject* childObj  = manager.createGameObject( hashed_string( "Child" ) );

    parentObj->addComponent<MockTickSceneComponent>();
    childObj->addComponent<MockTickSceneComponent>();

    MockTickSceneComponent* parentComp = parentObj->getComponent<MockTickSceneComponent>();
    MockTickSceneComponent* childComp  = childObj->getComponent<MockTickSceneComponent>();

    parentComp->setLocalPosition( float3( 10.0f, 0.0f, 0.0f ) );
    childComp->setLocalPosition( float3( 5.0f, 0.0f, 0.0f ) );
    SW_ASSERT_TRUE( childComp->attachToComponent( parentComp ) );

    float3 observedDuringTick{ 0.0f, 0.0f, 0.0f };
    childComp->_pObservedWorld = &observedDuringTick;

    manager.tick( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, observedDuringTick._x, 1e-4f );

    // 틱 중 로컬 쓰기는 post-flush 이후에 보이며, 틱 중간 스냅샷에는 없을 수 있다.
    parentComp->_tickLocalPos      = float3( 20.0f, 0.0f, 0.0f );
    parentComp->_bWriteLocalOnTick = SW_TRUE;
    manager.tick( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 25.0f, childComp->getWorldPosition()._x, 1e-4f );

    manager.clear();
}

/**
 * @brief [GameObjectManagerTest] 틱이 트랜스폼을 깨끗이 두면 tick 두 번째 flush 를 건너뛴다
 */
SW_TEST_CASE( GameObjectManagerTest, ParallelTickLeavesTransformsCleanWhenUnchanged )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject*     obj  = manager.createGameObject( hashed_string( "CleanRoot" ) );
    SceneComponent* root = obj->addComponent<SceneComponent>();
    root->setLocalPosition( float3( 1.0f, 2.0f, 3.0f ) );

    manager.tick( 0.016f );
    SW_EXPECT_TRUE( manager.hasDirtySceneTransforms() == false );
    SW_EXPECT_NEAR_EQUAL( 1.0f, root->getWorldPosition()._x, 1e-4f );

    manager.tick( 0.016f );
    SW_EXPECT_TRUE( manager.hasDirtySceneTransforms() == false );

    manager.clear();
}

/**
 * @brief [GameObjectManagerTest] 틱 중 destroy는 같은 프레임에 pending-kill 후 제거된다
 */
SW_TEST_CASE( GameObjectManagerTest, DestroyDuringTickIsDeferredThenApplied )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* keeper = manager.createGameObject( hashed_string( "TickKeeper" ) );
    GameObject* victim = manager.createGameObject( hashed_string( "TickVictim" ) );
    SW_ASSERT_NOT_NULL( keeper );
    SW_ASSERT_NOT_NULL( victim );

    keeper->addComponent<MockMeshComponent>();
    victim->addComponent<MockMeshComponent>();
    MockMeshComponent* keeperMesh = keeper->getComponent<MockMeshComponent>();
    MockMeshComponent* victimMesh = victim->getComponent<MockMeshComponent>();
    SW_ASSERT_NOT_NULL( keeperMesh );
    SW_ASSERT_NOT_NULL( victimMesh );

    keeperMesh->_pTickDestroyManager = &manager;
    keeperMesh->_pTickDestroyObject  = victim;

    manager.tick( 0.016f );
    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "TickVictim" ) ) );
    SW_EXPECT_NOT_NULL( manager.findGameObjectByName( hashed_string( "TickKeeper" ) ) );
    SW_EXPECT_EQUAL( 1, keeperMesh->_tickCount );
}

/**
 * @brief [GameObjectManagerTest] 틱 중 트랜스폼 지연과 destroy가 겹쳐도 resolve가 실패하면 건너뛴다
 */
SW_TEST_CASE( GameObjectManagerTest, DeferredTransformSkipsDestroyedComponent )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* keeper = manager.createGameObject( hashed_string( "TransformKeeper" ) );
    GameObject* victim = manager.createGameObject( hashed_string( "TransformVictim" ) );
    keeper->addComponent<MockMeshComponent>();
    victim->addComponent<SceneComponent>();
    MockMeshComponent* keeperMesh = keeper->getComponent<MockMeshComponent>();
    SceneComponent*    victimSc   = victim->getComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( keeperMesh );
    SW_ASSERT_NOT_NULL( victimSc );

    keeperMesh->_pTickMoveComp       = victimSc;
    keeperMesh->_tickMovePos         = float3( 1.0f, 2.0f, 3.0f );
    keeperMesh->_pTickDestroyManager = &manager;
    keeperMesh->_pTickDestroyObject  = victim;

    manager.tick( 0.016f );
    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "TransformVictim" ) ) );
    SW_EXPECT_NOT_NULL( manager.findGameObjectByName( hashed_string( "TransformKeeper" ) ) );
}

/**
 * @brief [GameObjectManagerTest] 틱 중 attach는 지연된 뒤 부모-자식 포인터를 연결한다
 */
SW_TEST_CASE( GameObjectManagerTest, DeferredAttachDuringTickApplies )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* parent = manager.createGameObject( hashed_string( "AttachParent" ) );
    GameObject* child  = manager.createGameObject( hashed_string( "AttachChild" ) );
    GameObject* ticker = manager.createGameObject( hashed_string( "AttachTicker" ) );
    parent->addComponent<SceneComponent>();
    child->addComponent<SceneComponent>();
    ticker->addComponent<MockMeshComponent>();
    SceneComponent*    parentSc   = parent->getComponent<SceneComponent>();
    SceneComponent*    childSc    = child->getComponent<SceneComponent>();
    MockMeshComponent* tickerMesh = ticker->getComponent<MockMeshComponent>();
    SW_ASSERT_NOT_NULL( parentSc );
    SW_ASSERT_NOT_NULL( childSc );
    SW_ASSERT_NOT_NULL( tickerMesh );

    tickerMesh->_pTickAttachChild  = childSc;
    tickerMesh->_pTickAttachParent = parentSc;

    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( parentSc, childSc->getParent() );
}

/**
 * @brief [GameObjectPoolTest] GameObject 파괴 후 재생성 시 TypedPoolAllocator 메모리 주소 재활용 및 내부 상태 초기화 검증
 */
SW_TEST_CASE( GameObjectPoolTest, GameObjectPoolMemoryReuseAndStateReset )
{
    sw::GameObjectManager manager;

    // 1) 3개 액터 생성 및 메모리 주소 기록
    sw::GameObject* pObj0 = manager.createGameObject( sw::hashed_string( "Actor_0" ) );
    sw::GameObject* pObj1 = manager.createGameObject( sw::hashed_string( "Actor_1" ) );
    sw::GameObject* pObj2 = manager.createGameObject( sw::hashed_string( "Actor_2" ) );

    SW_ASSERT_NOT_NULL( pObj0 );
    SW_ASSERT_NOT_NULL( pObj1 );
    SW_ASSERT_NOT_NULL( pObj2 );

    void* pAddr0 = static_cast<void*>( pObj0 );
    void* pAddr1 = static_cast<void*>( pObj1 );
    void* pAddr2 = static_cast<void*>( pObj2 );

    // 서로 다른 메모리 주소 할당 확인
    SW_EXPECT_NOT_EQUAL( pAddr0, pAddr1 );
    SW_EXPECT_NOT_EQUAL( pAddr1, pAddr2 );

    const uint64 id0 = pObj0->getObjectId();
    const uint64 id1 = pObj1->getObjectId();
    const uint64 id2 = pObj2->getObjectId();

    // 2) 액터 지연 삭제 처리
    manager.destroyObject( pObj2 );
    manager.destroyObject( pObj1 );
    manager.destroyObject( pObj0 );
    manager.processDeferredDestruction();

    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );

    // 3) 새로운 액터 3개 생성 시 풀의 LIFO 프리리스트에 의해 이전 주소가 재활용되는지 검증
    sw::GameObject* pNewObj0 = manager.createGameObject( sw::hashed_string( "NewActor_0" ) );
    sw::GameObject* pNewObj1 = manager.createGameObject( sw::hashed_string( "NewActor_1" ) );
    sw::GameObject* pNewObj2 = manager.createGameObject( sw::hashed_string( "NewActor_2" ) );

    SW_ASSERT_NOT_NULL( pNewObj0 );
    SW_ASSERT_NOT_NULL( pNewObj1 );
    SW_ASSERT_NOT_NULL( pNewObj2 );

    void* pNewAddr0 = static_cast<void*>( pNewObj0 );
    void* pNewAddr1 = static_cast<void*>( pNewObj1 );
    void* pNewAddr2 = static_cast<void*>( pNewObj2 );

    // LIFO 순서에 따라 마지막에 반환된 pAddr0, pAddr1, pAddr2가 재사용됨
    SW_EXPECT_EQUAL( pAddr0, pNewAddr0 );
    SW_EXPECT_EQUAL( pAddr1, pNewAddr1 );
    SW_EXPECT_EQUAL( pAddr2, pNewAddr2 );

    // 새로운 고유 ID가 발급되었는지 확인 (이전 ID와 다름)
    SW_EXPECT_NOT_EQUAL( id0, pNewObj0->getObjectId() );
    SW_EXPECT_NOT_EQUAL( id1, pNewObj1->getObjectId() );
    SW_EXPECT_NOT_EQUAL( id2, pNewObj2->getObjectId() );

    // 이름 및 기본 상태가 깨끗하게 초기화되었는지 확인
    SW_EXPECT_STREQ( "NewActor_0", pNewObj0->getName().c_str() );
    SW_EXPECT_TRUE( pNewObj0->isActive() );
    SW_EXPECT_FALSE( pNewObj0->isPendingKill() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), pNewObj0->getComponentCount() );
}

/**
 * @brief [ComponentPoolTest] 서로 다른 컴포넌트 타입별 전용 PoolAllocator 격리 및 주소 재활용 검증
 */
SW_TEST_CASE( ComponentPoolTest, PolymorphicComponentPoolIsolationAndAddressRecycle )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* pActor = manager.createGameObject( sw::hashed_string( "TestActor" ) );
    SW_ASSERT_NOT_NULL( pActor );

    // 1) 크기가 다른 서로 다른 타입의 컴포넌트 추가
    sw::SceneComponent*     pScene1 = pActor->addComponent<sw::SceneComponent>();
    sw::MockMeshComponent*  pMesh1  = pActor->addComponent<sw::MockMeshComponent>();
    sw::MockAudioComponent* pAudio1 = pActor->addComponent<sw::MockAudioComponent>();

    SW_ASSERT_NOT_NULL( pScene1 );
    SW_ASSERT_NOT_NULL( pMesh1 );
    SW_ASSERT_NOT_NULL( pAudio1 );

    void* pMeshAddr1  = static_cast<void*>( pMesh1 );
    void* pAudioAddr1 = static_cast<void*>( pAudio1 );

    // 2) MockMeshComponent 만 삭제
    pActor->removeComponent( pMesh1 );

    // 3) 새로운 MockMeshComponent 추가 시 동일한 메모리 주소가 재활용되는지 검증
    sw::MockMeshComponent* pMesh2 = pActor->addComponent<sw::MockMeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh2 );
    void* pMeshAddr2 = static_cast<void*>( pMesh2 );
    SW_EXPECT_EQUAL( pMeshAddr1, pMeshAddr2 );

    // MockAudioComponent 는 기존 인스턴스가 유지되고 영향받지 않음
    SW_EXPECT_EQUAL( pAudio1, pActor->getComponent<sw::MockAudioComponent>() );

    // 4) MockAudioComponent 삭제 후 재추가 시 Audio 풀 주소 재활용 검증
    pActor->removeComponent( pAudio1 );
    sw::MockAudioComponent* pAudio2 = pActor->addComponent<sw::MockAudioComponent>();
    SW_ASSERT_NOT_NULL( pAudio2 );
    void* pAudioAddr2 = static_cast<void*>( pAudio2 );
    SW_EXPECT_EQUAL( pAudioAddr1, pAudioAddr2 );
}

/**
 * @brief [ComponentPoolTest] 컴포넌트 풀 할당 및 해제 시 생성자/소멸자 호출 1:1 매칭 정확도 검증
 */
SW_TEST_CASE( ComponentPoolTest, ComponentPoolConstructorAndDestructorExactTracking )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::MockPoolLifecycleComponent::s_ctorCount.store( 0, std::memory_order_relaxed );
    sw::MockPoolLifecycleComponent::s_dtorCount.store( 0, std::memory_order_relaxed );

    // 1) 100개 오브젝트에 각각 MockPoolLifecycleComponent 추가
    constexpr uint32            kCount = 100;
    sw::vector<sw::GameObject*> listObject;
    listObject.reserve( kCount );

    for ( uint32 index = 0; index < kCount; ++index )
    {
        sw::GameObject* pObj  = manager.createGameObject( sw::hashed_string( ( "LifeActor_" + std::to_string( index ) ).c_str() ) );
        auto*           pComp = pObj->addComponent<sw::MockPoolLifecycleComponent>();
        pComp->_customData    = static_cast<int32>( index );
        listObject.push_back( pObj );
    }

    SW_EXPECT_EQUAL( static_cast<int32>( kCount ), sw::MockPoolLifecycleComponent::s_ctorCount.load( std::memory_order_relaxed ) );
    SW_EXPECT_EQUAL( 0, sw::MockPoolLifecycleComponent::s_dtorCount.load( std::memory_order_relaxed ) );

    // 2) 50개 오브젝트에서 removeComponent 로 직접 해제
    for ( uint32 index = 0; index < 50; ++index )
    {
        auto* pComp = listObject[index]->getComponent<sw::MockPoolLifecycleComponent>();
        SW_ASSERT_NOT_NULL( pComp );
        listObject[index]->removeComponent( pComp );
    }

    SW_EXPECT_EQUAL( 50, sw::MockPoolLifecycleComponent::s_dtorCount.load( std::memory_order_relaxed ) );

    // 3) 남은 50개 오브젝트는 GameObjectManager::clear 로 일괄 정리
    manager.clear();

    SW_EXPECT_EQUAL( static_cast<int32>( kCount ), sw::MockPoolLifecycleComponent::s_dtorCount.load( std::memory_order_relaxed ) );
    SW_EXPECT_EQUAL( sw::MockPoolLifecycleComponent::s_ctorCount.load( std::memory_order_relaxed ),
                     sw::MockPoolLifecycleComponent::s_dtorCount.load( std::memory_order_relaxed ) );
}

/**
 * @brief [ComponentPoolTest] 초고빈도(High-Frequency Churn) 컴포넌트 추가/삭제 스트레스 테스트
 */
SW_TEST_CASE( ComponentPoolTest, ComponentPoolHighFrequencyChurnStress )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* pActor = manager.createGameObject( sw::hashed_string( "ChurnActor" ) );
    SW_ASSERT_NOT_NULL( pActor );

    constexpr uint32 kIterations = 10000;
    for ( uint32 iterIndex = 0; iterIndex < kIterations; ++iterIndex )
    {
        sw::MockMeshComponent* pMesh = pActor->addComponent<sw::MockMeshComponent>();
        pMesh->_meshName             = "DynamicMesh";
        SW_ASSERT_NOT_NULL( pMesh );

        sw::MockAudioComponent* pAudio = pActor->addComponent<sw::MockAudioComponent>();
        pAudio->_volume                = 0.5f;
        SW_ASSERT_NOT_NULL( pAudio );

        pActor->removeComponent( pMesh );
        pActor->removeComponent( pAudio );
    }

    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), pActor->getComponentCount() );
}

/**
 * @brief [GameObjectManagerPoolTest] Scene clear 후 풀 완전 초기화 및 재사용 라이프사이클 검증
 */
SW_TEST_CASE( GameObjectManagerPoolTest, SceneClearAndPoolReuseLifecycle )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    // 1) 씬 1 생성: 200개 액터 및 복합 컴포넌트 구성
    constexpr uint32 kObjectCount = 200;
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( ( "Scene1_Obj_" + std::to_string( index ) ).c_str() ) );
        pObj->addComponent<sw::SceneComponent>();
        pObj->addComponent<sw::MockMeshComponent>();
        pObj->addComponent<sw::MockAudioComponent>();
    }

    SW_EXPECT_EQUAL( static_cast<size_t>( kObjectCount ), manager.getAllGameObjects().size() );
    manager.tick( 0.016f );

    // 2) 씬 클리어 (풀 완전 초기화)
    manager.clear();
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );

    // 3) 씬 2 생성: 클리어된 매니저에서 다시 200개 액터 정상 생성 및 틱 검증
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( ( "Scene2_Obj_" + std::to_string( index ) ).c_str() ) );
        pObj->addComponent<sw::SceneComponent>();
        sw::MockMeshComponent* pMesh = pObj->addComponent<sw::MockMeshComponent>();
        pMesh->_meshName             = "Scene2Mesh";
    }

    SW_EXPECT_EQUAL( static_cast<size_t>( kObjectCount ), manager.getAllGameObjects().size() );
    manager.tick( 0.016f );

    // 씬 2의 모든 메시 컴포넌트가 1회 틱되었는지 검증
    for ( sw::GameObject* pObj : manager.getAllGameObjects() )
    {
        sw::MockMeshComponent* pMesh = pObj->getComponent<sw::MockMeshComponent>();
        SW_ASSERT_NOT_NULL( pMesh );
        SW_EXPECT_EQUAL( 1, pMesh->_tickCount );
    }

    manager.clear();
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );
}

#if !defined( SW_SHIPPING )
// 모듈 언로드는 Dev 에만 있다 — Shipping 은 모듈을 정적 링크해 내리지 않으므로
// destroyComponentsOfModule 자체가 그 빌드에 없다. 없는 기능의 테스트도 없다.
/**
 * @brief 모듈이 내려가기 전에 그 모듈 타입의 **살아 있는 컴포넌트**가 걷히는지.
 * @details 씬은 엔진이 소유해 모듈(SWGame·EditorModule)보다 오래 산다. 예전에는 언로드가 팩토리·타입·전역 변수만
 *          걷어서, 모듈이 정의한 컴포넌트의 인스턴스가 vtable 없는 객체로 씬에 남을 수 있었다(지금 씬은 엔진
 *          컴포넌트만 써서 드러나지 않았을 뿐이다). 여기서는 엔진 타입을 모듈 이름으로 삼아 같은 기계를 검증한다 —
 *          이름이 맞는 컴포넌트만 사라지고, 소유 오브젝트와 다른 컴포넌트는 남아야 한다.
 */
SW_TEST_CASE( GameObjectManagerPoolTest, ModuleComponentsPurgedBeforeUnload )
{
    sw::GameObjectManager manager;

    sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( "ModulePurgeTarget" ) );
    SW_ASSERT_NOT_NULL( pObject );
    sw::MeshComponent* pMesh = pObject->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );

    const sw::TypeInfo* pTypeInfo = pMesh->getTypeInfo();
    SW_ASSERT_NOT_NULL( pTypeInfo );
    const sw::hashed_string moduleName = pTypeInfo->_moduleName;
    SW_ASSERT_FALSE( moduleName.empty() );

    // 1) 남의 모듈 이름으로는 아무것도 걷히지 않는다.
    SW_EXPECT_EQUAL( 0u, manager.destroyComponentsOfModule( "NotThisModule" ) );
    SW_EXPECT_NOT_NULL( pObject->getComponent<sw::MeshComponent>() );

    // 2) 자기 모듈 이름이면 인스턴스가 사라진다 — 오브젝트 자체는 남는다(컴포넌트만 모듈 소유다).
    SW_EXPECT_EQUAL( 1u, manager.destroyComponentsOfModule( moduleName.c_str() ) );
    SW_EXPECT_NULL( pObject->getComponent<sw::MeshComponent>() );
    SW_EXPECT_NOT_NULL( manager.findGameObjectByName( sw::hashed_string( "ModulePurgeTarget" ) ) );

    // 3) 멱등이다 — 이미 걷힌 뒤 다시 불러도 0.
    SW_EXPECT_EQUAL( 0u, manager.destroyComponentsOfModule( moduleName.c_str() ) );
}
#endif

/**
 * @brief 파괴 대기(pending kill) 오브젝트의 이름은 비어 있는 것으로 본다.
 * @details 모듈 리로드 · RHI 교체는 새 인스턴스를 만든 뒤 상태를 복원하는데, 그 사이 옛 오브젝트가 아직 지연 파괴
 *          대기열에 있다. 이름 맵만 보고 판단하면 새 오브젝트가 `BenchMesh_0_2` 같은 이름을 받고 `Duplicate name`
 *          경고가 뜬다 — 실제로 교체 로그에 매번 둘씩 찍혔다. 이름은 **살아 있는** 오브젝트만 차지한다.
 */
SW_TEST_CASE( GameObjectManagerPoolTest, PendingKillNameIsFreeForReuse )
{
    sw::GameObjectManager manager;

    sw::GameObject* pFirst = manager.createGameObject( sw::hashed_string( "Recycled" ) );
    SW_ASSERT_NOT_NULL( pFirst );
    SW_EXPECT_TRUE( pFirst->getName() == sw::hashed_string( "Recycled" ) );

    manager.destroyObject( pFirst );

    // 아직 실제 파괴 전이다(지연 파괴 대기). 그래도 이름은 비어 있어야 한다.
    sw::GameObject* pSecond = manager.createGameObject( sw::hashed_string( "Recycled" ) );
    SW_ASSERT_NOT_NULL( pSecond );
    SW_EXPECT_TRUE_MSG( pSecond->getName() == sw::hashed_string( "Recycled" ),
                        ( sw::string( "파괴 대기 이름이 재사용되지 않았다 — 받은 이름: " ) + pSecond->getName().c_str() ).c_str() );

    // 실제 파괴가 지나가도 살아 있는 쪽의 이름 항목을 지우지 않는다(같은 이름이므로 덮어쓰기 주의).
    manager.processDeferredDestruction();
    SW_EXPECT_TRUE( manager.findGameObjectByName( sw::hashed_string( "Recycled" ) ) == pSecond );
}
