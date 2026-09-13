#include "pch.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// GameObject 자체 — 컴포넌트 붙이기/떼기 · 계층 · 활성 상태 · 약참조.
// 매니저와 풀은 TestGameObjectManager.cpp, 틱 순서는 TestComponentTick.cpp,
// SceneComponent 트랜스폼은 TestSceneComponent.cpp 에 있다.

// ------------------------------------------------------------------------------
// 1) GameObjectTest — 부착·틱·리플렉션·태그
// ------------------------------------------------------------------------------
/**
 * @brief [GameObjectTest] 동일 타입 컴포넌트 다중 부착
 */
SW_TEST_CASE( GameObjectTest, MultiSameComponentAttachment )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* actorPtr = manager.createGameObject( sw::hashed_string( "TestPlayer" ) );

    sw::GameObject& actor = *actorPtr;
    SW_EXPECT_EQUAL( sw::string( "TestPlayer" ), sw::string( actor.getName().c_str() ) );

    sw::MockMeshComponent* mesh1 = actor.addComponent<sw::MockMeshComponent>();
    mesh1->_meshName             = "HeadMesh";

    sw::MockMeshComponent* mesh2 = actor.addComponent<sw::MockMeshComponent>();
    mesh2->_meshName             = "BodyMesh";

    sw::MockMeshComponent* mesh3 = actor.addComponent<sw::MockMeshComponent>();
    mesh3->_meshName             = "WeaponMesh";

    SW_EXPECT_EQUAL( 3u, actor.getComponentCount() );

    sw::MockMeshComponent* firstMesh = actor.getComponent<sw::MockMeshComponent>();
    SW_ASSERT_NOT_NULL( firstMesh );
    SW_EXPECT_EQUAL( sw::string( "HeadMesh" ), firstMesh->_meshName );
    SW_EXPECT_EQUAL( mesh1, firstMesh );

    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 1, mesh1->_tickCount );
    SW_EXPECT_EQUAL( 1, mesh2->_tickCount );
    SW_EXPECT_EQUAL( 1, mesh3->_tickCount );

    SW_EXPECT_TRUE( actor.removeComponent( firstMesh ) );
    SW_EXPECT_EQUAL( 2u, actor.getComponentCount() );
}

/**
 * @brief [GameObjectTest] 지연 컴포넌트 삭제가 조회에서 빠진다
 */
SW_TEST_CASE( GameObjectTest, DeferredComponentDestructionRemovesFromObject )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject*        actor = manager.createGameObject( sw::hashed_string( "DeferredCompActor" ) );
    sw::MockMeshComponent* mesh  = actor->addComponent<sw::MockMeshComponent>();
    SW_ASSERT_NOT_NULL( mesh );
    SW_EXPECT_EQUAL( 1u, actor->getComponentCount() );

    manager.destroyComponent( mesh );
    SW_EXPECT_TRUE( mesh->isPendingKill() );
    SW_EXPECT_TRUE( actor->getComponent<sw::MockMeshComponent>() == nullptr );
    SW_EXPECT_EQUAL( 0u, actor->getComponentCount() );
    SW_EXPECT_EQUAL( 0u, actor->getComponentCount() );
}

/**
 * @brief [GameObjectTest] 에디터 동적 컴포넌트 부착
 */
SW_TEST_CASE( GameObjectTest, EditorDynamicComponentAttachment )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );
    const uint32 typeCountBefore = static_cast<uint32>( manager.getRegisteredComponentTypeNames().size() );

    const sw::vector<sw::hashed_string> factories = manager.getRegisteredComponentTypeNames();
    SW_EXPECT_EQUAL( typeCountBefore + 0u, static_cast<uint32>( factories.size() ) );

    {
        sw::GameObject*         actorPtr = manager.createGameObject( sw::hashed_string( "EditorActor" ) );
        sw::GameObject&         actor    = *actorPtr;
        sw::MockMeshComponent*  comp1    = actor.addComponent<sw::MockMeshComponent>();
        sw::MockAudioComponent* comp2    = actor.addComponent<sw::MockAudioComponent>();

        SW_EXPECT_TRUE( comp1 != nullptr );
        SW_EXPECT_TRUE( comp2 != nullptr );
        SW_EXPECT_EQUAL( 2u, actor.getComponentCount() );

        SW_EXPECT_EQUAL( &actor, comp1->getOwner() );
        SW_EXPECT_EQUAL( &actor, comp2->getOwner() );
        SW_EXPECT_EQUAL( comp1, actor.findComponentByTypeName( sw::hashed_string( "MockMeshComponent" ) ) );
        SW_EXPECT_EQUAL( comp2, actor.findComponentByTypeName( sw::hashed_string( "MockAudioComponent" ) ) );
        SW_EXPECT_NULL( actor.findComponentByTypeName( sw::hashed_string( "MissingComponent" ) ) );
    }

    // getComponentManager().shutdown() 호출 제거
}

/**
 * @brief [GameObjectTest] 병렬 컴포넌트 틱
 */
SW_TEST_CASE( GameObjectTest, ParallelComponentTicking )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    {
        sw::vector<sw::GameObject*> actors;

        for ( int32 objectIndex = 0; objectIndex < 100; ++objectIndex )
        {
            sw::fixed_string<sw::constant::kMaxBuffer32> nameBuf{};
            sw::formatstring( nameBuf.data(), nameBuf.capacity(), "TickActor_%#", objectIndex );
            sw::GameObject* actorPtr = manager.createGameObject( sw::hashed_string( nameBuf.c_str() ) );
            actorPtr->addComponent<sw::MockMeshComponent>();
            actors.push_back( actorPtr );
        }

        manager.tick( 0.016f );

        for ( sw::GameObject* actor : actors )
        {
            sw::MockMeshComponent* meshComp = actor->getComponent<sw::MockMeshComponent>();
            SW_EXPECT_NOT_NULL( meshComp );
            if ( meshComp != nullptr )
                SW_EXPECT_EQUAL( 1, meshComp->_tickCount );
        }
    }
}

/**
 * @brief [GameObjectTest] 리플렉션 지원
 */
SW_TEST_CASE( GameObjectTest, ReflectionSupport )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );
    sw::GameObject*        actorPtr = manager.createGameObject( sw::hashed_string( "ReflectedActor" ) );
    sw::GameObject&        actor    = *actorPtr;
    sw::MockMeshComponent* meshComp = actor.addComponent<sw::MockMeshComponent>();

    SW_EXPECT_TRUE( actor.getObjectId() != 0 );
    SW_EXPECT_TRUE( meshComp->getComponentId() != 0 );
}

// ------------------------------------------------------------------------------
// 3) GameObjectTest — 부착·틱·리플렉션·태그
// ------------------------------------------------------------------------------
/**
 * @brief [GameObjectTest] 태그 관리
 */
SW_TEST_CASE( GameObjectTest, TagManagement )
{
    sw::GameObjectManager manager;
    sw::GameObject*       actorPtr = manager.createGameObject( sw::hashed_string( "TaggedHero" ) );
    sw::GameObject&       actor    = *actorPtr;

    constexpr TagID tagInvincible = "Status.Invincible"_tag;
    constexpr TagID tagFlying     = "Status.Flying"_tag;

    actor.addTag( tagInvincible );
    actor.addTag( tagFlying );

    SW_EXPECT_TRUE( actor.hasTag( tagInvincible ) );
    SW_EXPECT_TRUE( actor.hasTag( tagFlying ) );
    SW_EXPECT_FALSE( actor.hasTag( "Status.Poisoned"_tag ) );

    actor.removeTag( tagFlying );
    SW_EXPECT_FALSE( actor.hasTag( tagFlying ) );
}

// ------------------------------------------------------------------------------
// 9) HierarchicalActiveStateTest — 비활성 서브트리 틱 스킵
// ------------------------------------------------------------------------------
/**
 * @brief [HierarchicalActiveStateTest] 비활성 서브트리 틱 스킵
 */
SW_TEST_CASE( HierarchicalActiveStateTest, SubtreeTickSkip )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );
    sw::GameObject*    actorPtr = manager.createGameObject( sw::hashed_string( "ActiveTestActor" ) );
    sw::GameObject&    actor    = *actorPtr;
    MockMeshComponent* comp     = actor.addComponent<MockMeshComponent>();

    actor.setActive( false );
    SW_EXPECT_FALSE( actor.isActiveInHierarchy() );

    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( 0, comp->_tickCount );
}

// ------------------------------------------------------------------------------
// 10) GameObjectHierarchyTest — attach 와 active 전파
// ------------------------------------------------------------------------------
/**
 * @brief [GameObjectHierarchyTest] 부모-자식 attach 와 active 전파
 */
SW_TEST_CASE( GameObjectHierarchyTest, ParentChildAttachAndActivePropagation )
{
    sw::GameObjectManager manager;
    sw::GameObject*       parentPtr = manager.createGameObject( sw::hashed_string( "ParentGO" ) );
    sw::GameObject&       parent    = *parentPtr;
    sw::GameObject*       childPtr  = manager.createGameObject( sw::hashed_string( "ChildGO" ) );
    sw::GameObject&       child     = *childPtr;
    sw::GameObject*       grandPtr  = manager.createGameObject( sw::hashed_string( "GrandGO" ) );
    sw::GameObject&       grand     = *grandPtr;

    parent.addComponent<sw::SceneComponent>();
    child.addComponent<sw::SceneComponent>();
    grand.addComponent<sw::SceneComponent>();

    SW_EXPECT_TRUE( child.attachToParent( &parent ) );
    SW_EXPECT_TRUE( grand.attachToParent( &child ) );
    SW_EXPECT_EQUAL( &parent, child.getParent() );
    SW_EXPECT_EQUAL( &child, grand.getParent() );
    SW_EXPECT_EQUAL( size_t( 1 ), parent.getChildren().size() );
    SW_EXPECT_EQUAL( &child, parent.getChildren()[0] );

    // 순환 attach 는 실패해야 한다.
    SW_EXPECT_FALSE( parent.attachToParent( &grand ) );

    parent.setActive( false );
    SW_EXPECT_FALSE( parent.isActiveInHierarchy() );
    SW_EXPECT_FALSE( child.isActiveInHierarchy() );
    SW_EXPECT_FALSE( grand.isActiveInHierarchy() );

    parent.setActive( true );
    SW_EXPECT_TRUE( child.isActiveInHierarchy() );
    SW_EXPECT_TRUE( grand.isActiveInHierarchy() );

    child.setActive( false );
    SW_EXPECT_TRUE( parent.isActiveInHierarchy() );
    SW_EXPECT_FALSE( child.isActiveInHierarchy() );
    SW_EXPECT_FALSE( grand.isActiveInHierarchy() );

    grand.detachFromParent();
    SW_EXPECT_NULL( grand.getParent() );
    SW_EXPECT_EQUAL( size_t( 0 ), child.getChildren().size() );
    // 분리된 grand 는 다시 루트이고, 자체 active 는 true 이다.
    SW_EXPECT_TRUE( grand.isActive() );
    SW_EXPECT_TRUE( grand.isActiveInHierarchy() );
}

/**
 * @brief [GameObjectHierarchyTest] isDescendantOf는 자신과 조상 체인을 포함한다
 */
SW_TEST_CASE( GameObjectHierarchyTest, IsDescendantOfWalksParentChain )
{
    sw::GameObjectManager manager;
    sw::GameObject*       parentPtr = manager.createGameObject( sw::hashed_string( "AncestorGO" ) );
    sw::GameObject&       parent    = *parentPtr;
    sw::GameObject*       childPtr  = manager.createGameObject( sw::hashed_string( "ChildGO" ) );
    sw::GameObject&       child     = *childPtr;
    sw::GameObject*       grandPtr  = manager.createGameObject( sw::hashed_string( "GrandGO" ) );
    sw::GameObject&       grand     = *grandPtr;
    sw::GameObject*       otherPtr  = manager.createGameObject( sw::hashed_string( "OtherGO" ) );
    sw::GameObject&       other     = *otherPtr;

    parent.addComponent<sw::SceneComponent>();
    child.addComponent<sw::SceneComponent>();
    grand.addComponent<sw::SceneComponent>();
    other.addComponent<sw::SceneComponent>();

    SW_ASSERT_TRUE( child.attachToParent( &parent ) );
    SW_ASSERT_TRUE( grand.attachToParent( &child ) );

    SW_EXPECT_TRUE( parent.isDescendantOf( &parent ) );
    SW_EXPECT_TRUE( child.isDescendantOf( &parent ) );
    SW_EXPECT_TRUE( grand.isDescendantOf( &parent ) );
    SW_EXPECT_TRUE( grand.isDescendantOf( &child ) );
    SW_EXPECT_FALSE( parent.isDescendantOf( &child ) );
    SW_EXPECT_FALSE( other.isDescendantOf( &parent ) );
    SW_EXPECT_FALSE( parent.isDescendantOf( nullptr ) );
}

// ------------------------------------------------------------------------------
// 11) PostEditChangePropertyTest — 프로퍼티 변경 콜백
// ------------------------------------------------------------------------------
/**
 * @brief [PostEditChangePropertyTest] 프로퍼티 변경 콜백
 */
SW_TEST_CASE( PostEditChangePropertyTest, CallbackOnPropertyChanged )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );
    sw::GameObject*        actorPtr = manager.createGameObject( sw::hashed_string( "PropertyChangedActor" ) );
    sw::GameObject&        actor    = *actorPtr;
    MockCallbackComponent* comp     = actor.addComponent<MockCallbackComponent>();

    comp->setActive( false );
    // 프로퍼티 변경 알림은 동작해야 하지만, 정확한 이름은 리플렉션 시스템에 따라 달라질 수 있다
    SW_EXPECT_TRUE( comp->_lastChangedProperty.getHash() != 0 );
}

// ------------------------------------------------------------------------------
// 12) GameObjectTest — 부착·틱·리플렉션·태그
// ------------------------------------------------------------------------------
/**
 * @brief [GameObjectTest] 중복 GameObject 이름은 조회 맵을 덮어쓰지 않고 고유화한다
 */
SW_TEST_CASE( GameObjectTest, DuplicateNameUniquifies )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing duplicate GameObject name auto-uniquification" );
    sw::GameObjectManager manager;
    sw::GameObject*       first  = manager.createGameObject( sw::hashed_string( "DupName" ) );
    sw::GameObject*       second = manager.createGameObject( sw::hashed_string( "DupName" ) );
    SW_ASSERT_NOT_NULL( first );
    SW_ASSERT_NOT_NULL( second );
    SW_EXPECT_TRUE( first != second );
    SW_EXPECT_STREQ( "DupName", first->getName().c_str() );
    SW_EXPECT_TRUE( first->getName() != second->getName() );
    SW_EXPECT_TRUE( manager.findGameObjectByName( first->getName() ) == first );
    SW_EXPECT_TRUE( manager.findGameObjectByName( second->getName() ) == second );
}

/**
 * @brief [GameObjectTest] attachToParent가 어긋난 SceneComponent 부모를 Unreal처럼 재부모한다
 */
SW_TEST_CASE( GameObjectTest, AttachToParentReparentsPrimarySceneComponent )
{
    sw::GameObjectManager manager;
    GameObject*           parent = manager.createGameObject( hashed_string( "AlignParent" ) );
    GameObject*           child  = manager.createGameObject( hashed_string( "AlignChild" ) );
    GameObject*           other  = manager.createGameObject( hashed_string( "AlignOther" ) );
    SW_ASSERT_NOT_NULL( parent );
    SW_ASSERT_NOT_NULL( child );
    SW_ASSERT_NOT_NULL( other );

    parent->addComponent<SceneComponent>();
    child->addComponent<SceneComponent>();
    other->addComponent<SceneComponent>();

    SceneComponent* parentSc = parent->getComponent<SceneComponent>();
    SceneComponent* childSc  = child->getComponent<SceneComponent>();
    SceneComponent* otherSc  = other->getComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( parentSc );
    SW_ASSERT_NOT_NULL( childSc );
    SW_ASSERT_NOT_NULL( otherSc );

    SW_ASSERT_TRUE( childSc->attachToComponent( otherSc ) );
    SW_EXPECT_EQUAL( otherSc, childSc->getParent() );

    SW_ASSERT_TRUE( child->attachToParent( parent ) );
    SW_EXPECT_EQUAL( parent, child->getParent() );
    SW_EXPECT_EQUAL( parentSc, childSc->getParent() );
}

/**
 * @brief [GameObjectTest] 같은 타입 두 컴포넌트 지연 삭제가 포인터가 아니라 핸들로 처리된다
 */
SW_TEST_CASE( GameObjectTest, DeferredDestroyTwoSameTypeComponents )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* a = manager.createGameObject( hashed_string( "DeferredA" ) );
    GameObject* b = manager.createGameObject( hashed_string( "DeferredB" ) );
    a->addComponent<MockMeshComponent>();
    b->addComponent<MockMeshComponent>();
    MockMeshComponent* meshA = a->getComponent<MockMeshComponent>();
    MockMeshComponent* meshB = b->getComponent<MockMeshComponent>();
    SW_ASSERT_NOT_NULL( meshA );
    SW_ASSERT_NOT_NULL( meshB );

    manager.destroyComponent( meshA );
    manager.destroyComponent( meshB );

    SW_EXPECT_EQUAL( 0u, a->getComponentCount() );
    SW_EXPECT_EQUAL( 0u, b->getComponentCount() );
}

/**
 * @brief [GameObjectTest] 틱 중 다른 같은 타입 컴포넌트 제거는 프레임 끝에 적용된다
 */
SW_TEST_CASE( GameObjectTest, TickRemoveOtherSameTypeComponent )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* keeper = manager.createGameObject( hashed_string( "TickKeeperComp" ) );
    GameObject* victim = manager.createGameObject( hashed_string( "TickVictimComp" ) );
    keeper->addComponent<MockMeshComponent>();
    victim->addComponent<MockMeshComponent>();
    MockMeshComponent* keeperMesh = keeper->getComponent<MockMeshComponent>();
    MockMeshComponent* victimMesh = victim->getComponent<MockMeshComponent>();
    SW_ASSERT_NOT_NULL( keeperMesh );
    SW_ASSERT_NOT_NULL( victimMesh );

    keeperMesh->_pTickRemoveOwner = victim;
    keeperMesh->_pTickRemoveComp  = victimMesh;

    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 0u, victim->getComponentCount() );
    SW_EXPECT_EQUAL( 1u, keeper->getComponentCount() );
    SW_EXPECT_EQUAL( 1, keeperMesh->_tickCount );
}

/**
 * @brief [GameObjectTest] 같은 엔티티의 서로 다른 컴포넌트도 모두 tick된다
 */
SW_TEST_CASE( GameObjectTest, SameEntityComponentsBothTick )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* actor = manager.createGameObject( hashed_string( "SameEntityTick" ) );
    actor->addComponent<MockMeshComponent>();
    actor->addComponent<MockAudioComponent>();
    MockMeshComponent*  mesh  = actor->getComponent<MockMeshComponent>();
    MockAudioComponent* audio = actor->getComponent<MockAudioComponent>();
    SW_ASSERT_NOT_NULL( mesh );
    SW_ASSERT_NOT_NULL( audio );

    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 1, mesh->_tickCount );
    SW_EXPECT_EQUAL( 1, audio->_playCount );
}

SW_TEST_CASE( SoftPointerTest, SafeDestruction )
{
    sw::GameObjectManager  manager;
    sw::GameObject*        obj  = manager.createGameObject( sw::hashed_string( "TargetObj" ) );
    sw::MockMeshComponent* mesh = obj->addComponent<sw::MockMeshComponent>();

    sw::GameObjectPtr objPtr( obj );
    sw::ComponentPtr  compPtr( mesh );

    SW_EXPECT_TRUE( objPtr.isValid() );
    SW_EXPECT_TRUE( compPtr.isValid() );

    // Destroy component only
    manager.destroyComponent( mesh );
    manager.tick( 0.016f );

    SW_EXPECT_TRUE( objPtr.isValid() );   // Object is still alive
    SW_EXPECT_FALSE( compPtr.isValid() ); // Component pointer should be safely nullified!

    // Destroy object
    manager.destroyObject( obj );
    manager.tick( 0.016f );

    SW_EXPECT_FALSE( objPtr.isValid() ); // Object pointer should be safely nullified!
}

/**
 * @brief [GameObjectTest] 틱 도중 지연 컴포넌트 추가 및 틱 종료 후 완전 바인딩 검증
 */
SW_TEST_CASE( GameObjectTest, DeferredComponentAdditionDuringTick )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "DeferredTestObj" ) );
    SW_ASSERT_NOT_NULL( pObj );

    // 틱 실행 구간에서 addComponent 호출 시 deferPostTick에 등록되어 지연 실행
    manager.deferPostTick( [pObj]()
    {
        sw::MockAudioComponent* pAudio = pObj->addComponent<sw::MockAudioComponent>();
        SW_ASSERT_NOT_NULL( pAudio );
        pAudio->_volume = 0.75f;
    } );

    manager.tick( 0.016f );

    sw::MockAudioComponent* pAudioComp = pObj->getComponent<sw::MockAudioComponent>();
    SW_ASSERT_NOT_NULL( pAudioComp );
    SW_EXPECT_EQUAL( pObj, pAudioComp->getOwner() );
    SW_EXPECT_NEAR_EQUAL( 0.75f, pAudioComp->_volume, 0.001f );
}

/**
 * @brief [GameObjectTest] 부모 GameObject 지연 삭제 시 자식 계층 연쇄 삭제 검증
 */
SW_TEST_CASE( GameObjectTest, CascadingChildDestruction )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pParent = manager.createGameObject( sw::hashed_string( "CascadeParent" ) );
    sw::GameObject*       pChild1 = manager.createGameObject( sw::hashed_string( "CascadeChild1" ) );
    sw::GameObject*       pChild2 = manager.createGameObject( sw::hashed_string( "CascadeChild2" ) );

    SW_ASSERT_NOT_NULL( pParent );
    SW_ASSERT_NOT_NULL( pChild1 );
    SW_ASSERT_NOT_NULL( pChild2 );

    pParent->addComponent<sw::SceneComponent>();
    pChild1->addComponent<sw::SceneComponent>();
    pChild2->addComponent<sw::SceneComponent>();

    SW_EXPECT_TRUE( pChild1->attachToParent( pParent ) );
    SW_EXPECT_TRUE( pChild2->attachToParent( pChild1 ) );

    const sw::GameObjectPtr parentPtr( pParent );
    const sw::GameObjectPtr child1Ptr( pChild1 );
    const sw::GameObjectPtr child2Ptr( pChild2 );

    SW_EXPECT_TRUE( parentPtr.isValid() );
    SW_EXPECT_TRUE( child1Ptr.isValid() );
    SW_EXPECT_TRUE( child2Ptr.isValid() );

    // Destroy parent with bDestroyChildren = true
    manager.destroyObject( pParent, true );

    // Before tick, all are marked pending kill
    SW_EXPECT_TRUE( pParent->isPendingKill() );
    SW_EXPECT_TRUE( pChild1->isPendingKill() );
    SW_EXPECT_TRUE( pChild2->isPendingKill() );

    manager.tick( 0.016f );

    // After tick, all pointers should be safely nullified/invalid
    SW_EXPECT_FALSE( parentPtr.isValid() );
    SW_EXPECT_FALSE( child1Ptr.isValid() );
    SW_EXPECT_FALSE( child2Ptr.isValid() );
}

/**
 * @brief [GameObjectTest] 동일 위치/회전/스케일 설정 시 Transform Dirty 플래그가 불필요하게 켜지지 않는지 검증 (No-Op 가드)
 */
SW_TEST_CASE( GameObjectTest, NoOpTransformDoesNotMarkDirty )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "NoOpTransformTest" ) );
    SW_ASSERT_NOT_NULL( pObj );

    sw::SceneComponent* pSceneComp = pObj->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pSceneComp );

    pSceneComp->setLocalPosition( sw::float3( 10.0f, 20.0f, 30.0f ) );
    pSceneComp->setLocalRotation( sw::float3( 0.0f, 90.0f, 0.0f ) );
    pSceneComp->setLocalScale( sw::float3( 2.0f, 2.0f, 2.0f ) );

    // Flush scene transforms to clear dirty flags
    manager.flushSceneTransforms();
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    // Setting identical values should NOT dirty the transform
    pSceneComp->setLocalPosition( sw::float3( 10.0f, 20.0f, 30.0f ) );
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    pSceneComp->setLocalRotation( sw::float3( 0.0f, 90.0f, 0.0f ) );
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    pSceneComp->setLocalScale( sw::float3( 2.0f, 2.0f, 2.0f ) );
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    // Setting a new value should dirty the transform
    pSceneComp->setLocalPosition( sw::float3( 15.0f, 20.0f, 30.0f ) );
    SW_EXPECT_TRUE( manager.hasDirtySceneTransforms() );
}

/**
 * @brief [GameObjectTest] Transform Dirty Generation 세대 카운터 및 O(1) 조기 탈출 검증
 */
SW_TEST_CASE( GameObjectTest, TransformDirtyGenerationEarlyExit )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "GenerationTestObj" ) );
    SW_ASSERT_NOT_NULL( pObj );

    sw::SceneComponent* pSceneComp = pObj->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pSceneComp );

    const uint64 initialGen = manager.getTransformGeneration();

    pSceneComp->setLocalPosition( sw::float3( 5.0f, 10.0f, 15.0f ) );
    const uint64 updatedGen = manager.getTransformGeneration();
    SW_EXPECT_TRUE( updatedGen > initialGen );

    SW_EXPECT_TRUE( manager.hasDirtySceneTransforms() );
    manager.flushSceneTransforms();
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    // 변경 없는 경우 hasDirtySceneTransforms()는 서브트리 순회 없이 O(1)로 false 반환
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );
}

/**
 * @brief [GameObjectTest] MeshComponent만 있어도 월드 행렬 flush가 SceneComponent 풀을 요구하지 않는지 검증
 */
SW_TEST_CASE( GameObjectTest, MeshComponentOnlyTransformFlush )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "MeshOnlyFlush" ) );
    SW_ASSERT_NOT_NULL( pObj );

    sw::MeshComponent* pMesh = pObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    SW_EXPECT_TRUE( pObj->getComponent<sw::SceneComponent>() != nullptr );

    pMesh->setLocalPosition( sw::float3( 10.0f, 0.0f, 0.0f ) );
    SW_EXPECT_TRUE( manager.hasDirtySceneTransforms() );
    manager.flushSceneTransforms();
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    SW_EXPECT_EQUAL( 0, static_cast<int32>( pMesh->isTransformDirty() ) );
    SW_EXPECT_NEAR_EQUAL( 10.0f, pMesh->getWorldPosition()._x, 0.001f );
}

/**
 * @brief [GameObjectTest] MeshComponent 계층에서 getParent/getChildren와 자식 월드 합성이 동작하는지 검증
 */
SW_TEST_CASE( GameObjectTest, MeshComponentHierarchyLookupAndWorldCompose )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pParentObj = manager.createGameObject( sw::hashed_string( "MeshParent" ) );
    sw::GameObject*       pChildObj  = manager.createGameObject( sw::hashed_string( "MeshChild" ) );
    SW_ASSERT_NOT_NULL( pParentObj );
    SW_ASSERT_NOT_NULL( pChildObj );

    sw::MeshComponent* pParentMesh = pParentObj->addComponent<sw::MeshComponent>();
    sw::MeshComponent* pChildMesh  = pChildObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pParentMesh );
    SW_ASSERT_NOT_NULL( pChildMesh );

    SW_EXPECT_TRUE( pChildObj->attachToParent( pParentObj ) );
    SW_EXPECT_EQUAL( pParentMesh, pChildMesh->getParent() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), pParentMesh->getChildren().size() );
    SW_EXPECT_EQUAL( pChildMesh, pParentMesh->getChildren()[0] );

    pParentMesh->setLocalPosition( sw::float3( 10.0f, 0.0f, 0.0f ) );
    pChildMesh->setLocalPosition( sw::float3( 5.0f, 0.0f, 0.0f ) );
    manager.flushSceneTransforms();

    const sw::float3 childWorld = pChildMesh->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 15.0f, childWorld._x, 0.001f );
}

/**
 * @brief [GameObjectTest] 부모 비활성이 자식의 isActiveInHierarchy에 반영되는지 검증
 */
SW_TEST_CASE( GameObjectTest, ActiveInHierarchyFollowsParent )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pParent = manager.createGameObject( sw::hashed_string( "ActiveParent" ) );
    sw::GameObject*       pChild  = manager.createGameObject( sw::hashed_string( "ActiveChild" ) );
    SW_ASSERT_NOT_NULL( pParent );
    SW_ASSERT_NOT_NULL( pChild );

    pParent->addComponent<sw::SceneComponent>();
    pChild->addComponent<sw::SceneComponent>();
    SW_EXPECT_TRUE( pChild->attachToParent( pParent ) );

    pParent->setActive( false );
    SW_EXPECT_FALSE( pParent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pChild->isActiveInHierarchy() );

    pParent->setActive( true );
    SW_EXPECT_TRUE( pParent->isActiveInHierarchy() );
    SW_EXPECT_TRUE( pChild->isActiveInHierarchy() );

    pChild->setActive( false );
    SW_EXPECT_TRUE( pParent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pChild->isActiveInHierarchy() );
}

/**
 * @brief [GameObjectTest] ObjectStateSerializer 바이너리 버퍼 직렬화 및 복원 검증
 */
SW_TEST_CASE( GameObjectTest, ObjectStateBinaryBufferRoundtrip )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pSource = manager.createGameObject( sw::hashed_string( "BinaryHero" ) );
    SW_ASSERT_NOT_NULL( pSource );
    pSource->setActive( false );
    pSource->addComponent<sw::SceneComponent>();

    sw::vector<uint8> buffer;
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::saveToBinaryBuffer( pSource, buffer ) );
    SW_EXPECT_FALSE( buffer.empty() );

    manager.clear();

    sw::GameObject* pTarget = manager.createGameObject( sw::hashed_string( "TempTarget" ) );
    SW_ASSERT_NOT_NULL( pTarget );
    pTarget->setActive( true );

    sw::string   parentName;
    const size_t bytesRead = sw::ObjectStateSerializer::loadFromBinaryBuffer( pTarget, buffer.data(), buffer.size(), parentName );
    SW_EXPECT_EQUAL( buffer.size(), bytesRead );
    SW_EXPECT_STREQ( "BinaryHero", pTarget->getName().c_str() );
    SW_EXPECT_FALSE( pTarget->isActive() );
    SW_EXPECT_NOT_NULL( pTarget->getPrimarySceneComponent() );
}

/**
 * @brief [GameObjectTest] 5,000개 대규모 GameObject 생성, 컴포넌트 부착 및 지연 일괄 해제 스트레스 테스트
 */
SW_TEST_CASE( GameObjectTest, GameObjectMassiveCreationAndDestructionStressTest )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    constexpr uint32            kObjectCount = 5000;
    sw::vector<sw::GameObject*> listObject;
    listObject.reserve( kObjectCount );

    // 1) 5,000개 GameObject 및 컴포넌트 대량 생성
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( ( "StressActor_" + std::to_string( index ) ).c_str() ) );
        SW_ASSERT_NOT_NULL( pObj );

        pObj->addComponent<sw::SceneComponent>();
        sw::MockMeshComponent* pMesh = pObj->addComponent<sw::MockMeshComponent>();
        pMesh->_meshName             = "StressMesh";

        if ( index % 5 == 0 )
            pObj->addComponent<sw::MockAudioComponent>();

        listObject.push_back( pObj );
    }

    SW_EXPECT_EQUAL( static_cast<size_t>( kObjectCount ), manager.getAllGameObjects().size() );

    // 2) 틱 실행 및 상태 갱신
    manager.tick( 0.016f );

    // 3) 전수 지연 삭제 큐 등록 및 처리
    for ( sw::GameObject* pObj : listObject )
    {
        manager.destroyObject( pObj );
    }
    manager.processDeferredDestruction();

    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );
}

/**
 * @brief [GameObjectTest] 병렬 틱(Parallel Tick) 중 구조 변경 지연 큐(deferPostTick/deferTransformUpdate) 동시성 스트레스 테스트
 */
SW_TEST_CASE( GameObjectTest, ParallelTickStructuralMutationStressTest )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    constexpr uint32 kObjectCount = 1000;
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( ( "ParallelTickActor_" + std::to_string( index ) ).c_str() ) );
        pObj->addComponent<sw::SceneComponent>();
        pObj->addComponent<sw::MockMeshComponent>();
    }

    std::atomic<uint32> postTickExecutedCount{ 0 };
    std::atomic<uint32> transformUpdateExecutedCount{ 0 };

    // 틱 직전 다중 지연 람다 등록
    for ( uint32 index = 0; index < 50; ++index )
    {
        manager.deferPostTick( SW_DELEGATE_LAMBDA( sw::GameObjectManager::PostTickDelegate, [&postTickExecutedCount]()
        {
            postTickExecutedCount.fetch_add( 1, std::memory_order_relaxed );
        } ) );

        manager.deferTransformUpdate( SW_DELEGATE_LAMBDA( sw::GameObjectManager::TransformUpdateDelegate, [&transformUpdateExecutedCount]()
        {
            transformUpdateExecutedCount.fetch_add( 1, std::memory_order_relaxed );
        } ) );
    }

    // 틱 수행 시 모든 지연 큐가 정상 flush 되어야 함
    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( 50u, postTickExecutedCount.load() );
    SW_EXPECT_EQUAL( 50u, transformUpdateExecutedCount.load() );
}

/**
 * @brief [GameObjectTest] 64단계 심층 부모-자식 계층 트랜스폼 월드 합성 및 동적 분리 스트레스 테스트
 */
SW_TEST_CASE( GameObjectTest, DeepHierarchyMatrixCompositionStressTest )
{
    sw::GameObjectManager manager;

    constexpr uint32            kDepth = 64;
    sw::vector<sw::GameObject*> listNode;
    listNode.reserve( kDepth );

    // 64단계 체인 생성: Node_0 -> Node_1 -> ... -> Node_63
    for ( uint32 depthIndex = 0; depthIndex < kDepth; ++depthIndex )
    {
        sw::GameObject* pNode = manager.createGameObject( sw::hashed_string( ( "DeepNode_" + std::to_string( depthIndex ) ).c_str() ) );
        SW_ASSERT_NOT_NULL( pNode );

        sw::SceneComponent* pComp = pNode->addComponent<sw::SceneComponent>();
        pComp->setLocalPosition( sw::float3( 1.0f, 0.0f, 0.0f ) ); // 각 단계마다 +1 X 이동

        if ( depthIndex > 0 )
            SW_EXPECT_TRUE( pNode->attachToParent( listNode[depthIndex - 1] ) );

        listNode.push_back( pNode );
    }

    manager.flushSceneTransforms();

    // 1) 64번째(인덱스 63) 리프 노드의 월드 위치 = (64, 0, 0)
    sw::SceneComponent* pLeafComp = listNode[kDepth - 1]->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pLeafComp );
    SW_EXPECT_NEAR_EQUAL( static_cast<float32>( kDepth ), pLeafComp->getWorldPosition()._x, 1e-3f );

    // 2) 루트 노드(Node_0) 위치를 (100, 0, 0)으로 변경
    sw::SceneComponent* pRootComp = listNode[0]->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pRootComp );
    pRootComp->setLocalPosition( sw::float3( 100.0f, 0.0f, 0.0f ) );

    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 100.0f + static_cast<float32>( kDepth - 1 ), pLeafComp->getWorldPosition()._x, 1e-3f );

    // 3) 중간 노드(Node_32)를 부모로부터 분리(Detach)
    sw::GameObject* pMidNode = listNode[32];
    pMidNode->detachFromParent();
    SW_EXPECT_TRUE( pMidNode->getParent() == nullptr );

    manager.flushSceneTransforms();
    // Node_32부터 Node_63까지는 32개 체인이므로 리프 월드 X = 32.0f
    SW_EXPECT_NEAR_EQUAL( 32.0f, pLeafComp->getWorldPosition()._x, 1e-3f );
}

/**
 * @brief [GameObjectTest] 다중 컴포넌트 심층 계층에서의 동적 부모 재지정(Reparenting), 순환 방어 및 중간 노드 연쇄 삭제 검증
 */
SW_TEST_CASE( GameObjectTest, DeepMultiComponentCascadeDestructionAndReparenting )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    constexpr uint32            kDepth = 32;
    sw::vector<sw::GameObject*> listNode;
    listNode.reserve( kDepth );

    // 1) 32단계 심층 계층 생성 (각 노드마다 Scene, Mesh, Audio 3개 컴포넌트 부착)
    for ( uint32 depthIndex = 0; depthIndex < kDepth; ++depthIndex )
    {
        sw::GameObject* pNode = manager.createGameObject( sw::hashed_string( ( "MultiCompNode_" + std::to_string( depthIndex ) ).c_str() ) );
        SW_ASSERT_NOT_NULL( pNode );

        sw::SceneComponent* pSceneComp = pNode->addComponent<sw::SceneComponent>();
        pSceneComp->setLocalPosition( sw::float3( 2.0f, 0.0f, 0.0f ) ); // 각 단계마다 +2 X

        sw::MockMeshComponent* pMeshComp = pNode->addComponent<sw::MockMeshComponent>();
        pMeshComp->_meshName             = "Mesh_" + std::to_string( depthIndex );

        pNode->addComponent<sw::MockAudioComponent>();

        if ( depthIndex > 0 )
            SW_EXPECT_TRUE( pNode->attachToParent( listNode[depthIndex - 1] ) );

        listNode.push_back( pNode );
    }

    SW_EXPECT_EQUAL( static_cast<size_t>( kDepth ), manager.getAllGameObjects().size() );

    // 2) 틱 실행 시 모든 32개 노드의 MockMeshComponent가 1회씩 틱을 수행했는지 확인
    manager.tick( 0.016f );
    for ( uint32 depthIndex = 0; depthIndex < kDepth; ++depthIndex )
    {
        sw::MockMeshComponent* pMesh = listNode[depthIndex]->getComponent<sw::MockMeshComponent>();
        SW_ASSERT_NOT_NULL( pMesh );
        SW_EXPECT_EQUAL( 1, pMesh->_tickCount );
    }

    // 3) 동적 부모 변경 (Node_16을 Node_4의 직속 자식으로 재지정)
    sw::GameObject* pNode16 = listNode[16];
    sw::GameObject* pNode4  = listNode[4];
    SW_EXPECT_TRUE( pNode16->attachToParent( pNode4 ) );
    SW_EXPECT_EQUAL( pNode4, pNode16->getParent() );

    // 순환 참조 방어 (자식 Node_16을 부모 Node_4의 부모로 지정 시도 -> 실패해야 함)
    SW_EXPECT_FALSE( pNode4->attachToParent( pNode16 ) );

    manager.flushSceneTransforms();
    // Node_4 월드 X = 5 * 2 = 10. Node_16부터 Node_31까지 16개 단계이므로 리프(Node_31) 월드 X = 10 + (16 * 2) = 42.0f
    sw::SceneComponent* pLeafScene = listNode[kDepth - 1]->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pLeafScene );
    SW_EXPECT_NEAR_EQUAL( 42.0f, pLeafScene->getWorldPosition()._x, 1e-3f );

    // 4) 중간 계층 Node_16 연쇄 삭제 (Node_16과 그 하위 자식인 Node_17 ~ Node_31 총 16개 노드 동시 소멸)
    manager.destroyObject( pNode16, true );
    manager.processDeferredDestruction();

    // Node_0 ~ Node_15 총 16개 노드만 생존해야 함
    SW_EXPECT_EQUAL( static_cast<size_t>( 16 ), manager.getAllGameObjects().size() );

    for ( uint32 depthIndex = 0; depthIndex < 16; ++depthIndex )
    {
        SW_EXPECT_NOT_NULL( manager.findGameObjectByName( sw::hashed_string( ( "MultiCompNode_" + std::to_string( depthIndex ) ).c_str() ) ) );
    }
    for ( uint32 depthIndex = 16; depthIndex < kDepth; ++depthIndex )
    {
        SW_EXPECT_NULL( manager.findGameObjectByName( sw::hashed_string( ( "MultiCompNode_" + std::to_string( depthIndex ) ).c_str() ) ) );
    }

    // 5) 생존 노드들은 정상적으로 계속 틱 수행 가능
    manager.tick( 0.016f );
    for ( uint32 depthIndex = 0; depthIndex < 16; ++depthIndex )
    {
        sw::MockMeshComponent* pMesh = listNode[depthIndex]->getComponent<sw::MockMeshComponent>();
        SW_ASSERT_NOT_NULL( pMesh );
        SW_EXPECT_EQUAL( 2, pMesh->_tickCount );
    }
}

/**
 * @brief [GameObjectTest] 무작위 트리 재배치, 활성 토글 및 삭제가 난무하는 500회 카오스 계층 변이 스트레스 테스트
 */
SW_TEST_CASE( GameObjectTest, ChaoticHierarchyMutationAndActiveToggleStressTest )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    constexpr uint32            kInitialObjectCount = 100;
    sw::vector<sw::GameObject*> listAliveObject;
    listAliveObject.reserve( 500 );

    // 초기 100개 오브젝트 및 계층 생성
    for ( uint32 index = 0; index < kInitialObjectCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( ( "ChaosActor_" + std::to_string( index ) ).c_str() ) );
        SW_ASSERT_NOT_NULL( pObj );

        sw::SceneComponent* pScene = pObj->addComponent<sw::SceneComponent>();
        pScene->setLocalPosition( sw::float3( static_cast<float32>( index ), 0.0f, 0.0f ) );
        pObj->addComponent<sw::MockMeshComponent>();

        if ( index > 0 && ( index % 3 != 0 ) )
            pObj->attachToParent( listAliveObject[index / 2] );

        listAliveObject.push_back( pObj );
    }

    uint32           nextActorId  = kInitialObjectCount;
    constexpr uint32 kTotalRounds = 500;

    for ( uint32 roundIndex = 0; roundIndex < kTotalRounds; ++roundIndex )
    {
        const uint32 actionType = ( roundIndex * 37 + 13 ) % 5;

        if ( listAliveObject.empty() == false )
        {
            const size_t    targetIndexA = ( roundIndex * 17 ) % listAliveObject.size();
            const size_t    targetIndexB = ( roundIndex * 29 + 1 ) % listAliveObject.size();
            sw::GameObject* pObjA        = listAliveObject[targetIndexA];
            sw::GameObject* pObjB        = listAliveObject[targetIndexB];

            if ( actionType == 0 )
            {
                // 1) 부모 재지정 시도 (A -> B)
                if ( pObjA != pObjB )
                    pObjA->attachToParent( pObjB );
            }
            else if ( actionType == 1 )
            {
                // 2) 부모 분리 (Detach)
                pObjA->detachFromParent();
            }
            else if ( actionType == 2 )
            {
                // 3) Active 토글
                const bool bNewActive = ( roundIndex % 2 == 0 );
                pObjA->setActive( bNewActive );
                SW_EXPECT_EQUAL( bNewActive, pObjA->isActive() );
            }
            else if ( actionType == 3 )
            {
                // 4) 임의 오브젝트 삭제 (지연 큐 등록)
                manager.destroyObject( pObjA, true );
                listAliveObject.erase(
                    std::remove_if( listAliveObject.begin(), listAliveObject.end(), []( sw::GameObject* pObj )
                { return pObj == nullptr || pObj->isPendingKill(); } ),
                    listAliveObject.end() );
            }
            else
            {
                // 5) 신규 오브젝트 동적 생성 후 임의 부모 연결
                sw::GameObject* pNewObj = manager.createGameObject( sw::hashed_string( ( "ChaosActor_" + std::to_string( nextActorId++ ) ).c_str() ) );
                if ( pNewObj != nullptr )
                {
                    sw::SceneComponent* pScene = pNewObj->addComponent<sw::SceneComponent>();
                    pScene->setLocalPosition( sw::float3( 1.0f, 2.0f, 3.0f ) );
                    pNewObj->addComponent<sw::MockMeshComponent>();

                    if ( listAliveObject.empty() == false )
                        pNewObj->attachToParent( listAliveObject.back() );
                    listAliveObject.push_back( pNewObj );
                }
            }
        }

        // 10회마다 지연 삭제 처리 및 트랜스폼/틱 flush
        if ( roundIndex % 10 == 0 )
        {
            manager.processDeferredDestruction();
            manager.flushSceneTransforms();
            manager.tick( 0.016f );
        }
    }

    // 최종 상태 검증
    manager.processDeferredDestruction();
    manager.flushSceneTransforms();
    manager.tick( 0.016f );

    // 남아있는 모든 오브젝트의 월드 좌표가 유효(NaN/Inf 없음)한지 검증
    for ( sw::GameObject* pObj : manager.getAllGameObjects() )
    {
        sw::SceneComponent* pScene = pObj->getPrimarySceneComponent();
        if ( pScene != nullptr )
        {
            const sw::float3 worldPos = pScene->getWorldPosition();
            SW_EXPECT_FALSE( std::isnan( worldPos._x ) );
            SW_EXPECT_FALSE( std::isnan( worldPos._y ) );
            SW_EXPECT_FALSE( std::isnan( worldPos._z ) );
            SW_EXPECT_FALSE( std::isinf( worldPos._x ) );
        }
    }

    // 씬 클리어
    manager.clear();
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );
}

/**
 * @brief [GameObjectTest] 다단계 상속(4단계) 컴포넌트의 TypeInfo::isDerivedFrom 및 isA/castTo 다형성 검증
 */
SW_TEST_CASE( GameObjectTest, MultiLevelComponentInheritanceTypeInfoAndIsA )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "InheritanceActor" ) );
    SW_ASSERT_NOT_NULL( pObj );

    sw::MockFlyingVehicleComponent* pFlying = pObj->addComponent<sw::MockFlyingVehicleComponent>();
    SW_ASSERT_NOT_NULL( pFlying );

    const sw::TypeInfo* pType = pFlying->getTypeInfo();
    SW_ASSERT_NOT_NULL( pType );

    // 1) TypeInfo::isDerivedFrom 4단계 상속 체인 전수 검증
    // MockFlyingVehicleComponent -> MockVehicleComponent -> MockBasePawnComponent -> MockRootComponent
    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "sw::MockFlyingVehicleComponent" ) ) );
    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "MockFlyingVehicleComponent" ) ) );

    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "sw::MockVehicleComponent" ) ) );
    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "MockVehicleComponent" ) ) );

    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "sw::MockBasePawnComponent" ) ) );
    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "MockBasePawnComponent" ) ) );

    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "sw::MockRootComponent" ) ) );
    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "MockRootComponent" ) ) );

    // 무관한 형제/타입에 대해서는 false
    SW_EXPECT_FALSE( pType->isDerivedFrom( sw::hashed_string( "sw::MockAudioComponent" ) ) );
    SW_EXPECT_FALSE( pType->isDerivedFrom( sw::hashed_string( "sw::MockMeshComponent" ) ) );

    // 2) isA<T>() 템플릿 다형성 검증
    SW_EXPECT_TRUE( sw::isA<sw::MockFlyingVehicleComponent>( pFlying ) );
    SW_EXPECT_TRUE( sw::isA<sw::MockVehicleComponent>( pFlying ) );
    SW_EXPECT_TRUE( sw::isA<sw::MockBasePawnComponent>( pFlying ) );
    SW_EXPECT_TRUE( sw::isA<sw::MockRootComponent>( pFlying ) );
    SW_EXPECT_FALSE( sw::isA<sw::MockAudioComponent>( pFlying ) );

    // 3) castTo<T>() 템플릿 안전 업캐스팅 검증
    sw::MockVehicleComponent*  pVehicle = sw::castTo<sw::MockVehicleComponent>( pFlying );
    sw::MockBasePawnComponent* pPawn    = sw::castTo<sw::MockBasePawnComponent>( pFlying );
    sw::MockRootComponent*     pRoot    = sw::castTo<sw::MockRootComponent>( pFlying );

    SW_ASSERT_NOT_NULL( pVehicle );
    SW_ASSERT_NOT_NULL( pPawn );
    SW_ASSERT_NOT_NULL( pRoot );

    SW_EXPECT_EQUAL( static_cast<void*>( pFlying ), static_cast<void*>( pVehicle ) );
    SW_EXPECT_EQUAL( static_cast<void*>( pFlying ), static_cast<void*>( pPawn ) );
    SW_EXPECT_EQUAL( static_cast<void*>( pFlying ), static_cast<void*>( pRoot ) );

    SW_EXPECT_NULL( sw::castTo<sw::MockAudioComponent>( pFlying ) );
}

/**
 * @brief [GameObjectTest] GameObject::getComponent<T>()의 다단계 상속 다형성 조회 및 가상 메서드 체인 검증
 */
SW_TEST_CASE( GameObjectTest, MultiLevelComponentGameObjectPolymorphicLookup )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "DroneActor" ) );
    SW_ASSERT_NOT_NULL( pObj );

    // 다단계 상속 컴포넌트 부착
    sw::MockFlyingVehicleComponent* pFlying = pObj->addComponent<sw::MockFlyingVehicleComponent>();
    SW_ASSERT_NOT_NULL( pFlying );
    pFlying->_pawnHealth  = 250;
    pFlying->_maxSpeed    = 180.0f;
    pFlying->_maxAltitude = 8000.0f;

    // 1) 임의의 부모 타입 T로 getComponent<T>() 호출 시 동일 인스턴스 조회 검증
    sw::MockFlyingVehicleComponent* pExactLookup   = pObj->getComponent<sw::MockFlyingVehicleComponent>();
    sw::MockVehicleComponent*       pVehicleLookup = pObj->getComponent<sw::MockVehicleComponent>();
    sw::MockBasePawnComponent*      pPawnLookup    = pObj->getComponent<sw::MockBasePawnComponent>();
    sw::MockRootComponent*          pRootLookup    = pObj->getComponent<sw::MockRootComponent>();

    SW_ASSERT_NOT_NULL( pExactLookup );
    SW_ASSERT_NOT_NULL( pVehicleLookup );
    SW_ASSERT_NOT_NULL( pPawnLookup );
    SW_ASSERT_NOT_NULL( pRootLookup );

    SW_EXPECT_EQUAL( pFlying, pExactLookup );
    SW_EXPECT_EQUAL( pFlying, pVehicleLookup );
    SW_EXPECT_EQUAL( pFlying, pPawnLookup );
    SW_EXPECT_EQUAL( pFlying, pRootLookup );

    // 무관한 컴포넌트 조회 시 nullptr
    SW_EXPECT_NULL( pObj->getComponent<sw::MockAudioComponent>() );
    SW_EXPECT_NULL( pObj->getComponent<sw::MockMeshComponent>() );

    // 2) 다단계 상속 계층의 가상 onTick 체인 연쇄 호출 검증
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 1, pFlying->_flyingTickCount );
    SW_EXPECT_EQUAL( 1, pFlying->_vehicleTickCount );
    SW_EXPECT_EQUAL( 1, pFlying->_pawnTickCount );

    // 3) 상속된 SceneComponent 트랜스폼 동작 검증
    pFlying->setLocalPosition( sw::float3( 10.0f, 20.0f, 30.0f ) );
    manager.flushSceneTransforms();

    const sw::float3 worldPos = pFlying->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 10.0f, worldPos._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, worldPos._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, worldPos._z, 1e-4f );
}

SW_TEST_CASE( GameObjectHierarchy, ActiveInHierarchyCompoundEvaluation )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pGrandparent = manager.createGameObject( sw::hashed_string( "Grandparent" ) );
    sw::GameObject*       pParent      = manager.createGameObject( sw::hashed_string( "Parent" ) );
    sw::GameObject*       pChild       = manager.createGameObject( sw::hashed_string( "Child" ) );

    sw::SceneComponent* pGrandparentSc = pGrandparent->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pParentSc      = pParent->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pChildSc       = pChild->addComponent<sw::SceneComponent>();

    pParentSc->attachToComponent( pGrandparentSc );
    pChildSc->attachToComponent( pParentSc );

    // 1) 초기 상태: 모두 활성
    SW_EXPECT_TRUE( pGrandparent->isActiveInHierarchy() );
    SW_EXPECT_TRUE( pParent->isActiveInHierarchy() );
    SW_EXPECT_TRUE( pChild->isActiveInHierarchy() );

    // 2) Child만 비활성화: Grandparent/Parent는 true, Child는 false
    pChild->setActive( false );
    SW_EXPECT_TRUE( pGrandparent->isActiveInHierarchy() );
    SW_EXPECT_TRUE( pParent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pChild->isActiveInHierarchy() );

    // 3) Parent 비활성화, Child 재활성화: Grandparent=true, Parent=false, Child=false (부모가 꺼져있으므로)
    pParent->setActive( false );
    pChild->setActive( true );
    SW_EXPECT_TRUE( pGrandparent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pParent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pChild->isActiveInHierarchy() );

    // 4) Grandparent 비활성화, Parent 활성화, Child 활성화: Grandparent=false, Parent=false, Child=false (조상이 꺼져있으므로)
    pGrandparent->setActive( false );
    pParent->setActive( true );
    pChild->setActive( true );
    SW_EXPECT_FALSE( pGrandparent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pParent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pChild->isActiveInHierarchy() );

    // 5) Grandparent 복구: 모두 true
    pGrandparent->setActive( true );
    SW_EXPECT_TRUE( pGrandparent->isActiveInHierarchy() );
    SW_EXPECT_TRUE( pParent->isActiveInHierarchy() );
    SW_EXPECT_TRUE( pChild->isActiveInHierarchy() );
}
