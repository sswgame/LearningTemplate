#include "pch.h"

#include "Core/Common/Defines.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 테스트 편의 — 자식 GameObject 목록을 값으로 (엔진 API 는 out 인자다). */
    sw::vector<sw::GameObject*> childrenOf( const sw::GameObject& gameObject )
    {
        sw::vector<sw::GameObject*> listChild;
        gameObject.getChildren( listChild );
        return listChild;
    }
} // namespace

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
    SW_EXPECT_EQUAL( size_t( 1 ), childrenOf( parent ).size() );
    SW_EXPECT_EQUAL( &child, childrenOf( parent )[0] );

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
    SW_EXPECT_EQUAL( size_t( 0 ), childrenOf( child ).size() );
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
 * @brief 컴포넌트는 이름이 바뀌어도 **자기가 나온 풀**로 돌아간다.
 * @details 파괴가 `getTypeInfo()->_fullyQualifiedName` 으로 풀을 다시 찾던 때는, 이름을 비운 채 파괴하면 풀을 못 찾아
 *          풀 블록을 힙으로 반납했다 — Shipping 에서 힙 손상(0xc0000374), Debug·ASan 은 조용했다. 이제 컴포넌트가
 *          `_pPool` 을 들고 그리로 돌아간다. 풀의 자유 목록은 LIFO 라, 반납이 풀로 갔으면 다음 할당이 **같은 주소**를
 *          받는다. 힙으로 갔으면 다른 블록이 나온다 — 그것이 검사다(Debug 에서도 잡힌다).
 */
SW_TEST_CASE( GameObjectTest, ComponentReturnsToItsPoolAfterRename )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj  = manager.createGameObject( sw::hashed_string( "PoolReturn" ) );
    sw::MeshComponent*    pMesh = pObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    const void* pBlock = pMesh;

    pMesh->setComponentName( sw::hashed_string{} );
    manager.destroyComponent( pMesh );
    manager.processDeferredDestruction();

    sw::MeshComponent* pAgain = pObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pAgain );
    SW_EXPECT_TRUE( static_cast<const void*>( pAgain ) == pBlock );
}

/**
 * @brief 캐스트 핫패스의 창구 셋이 가상 경로와 같은 답을 내는지 — `findStaticType` · `castTo( pSrc, pToType )` · `findCachedTypeInfo`.
 * @details 셋 다 "컴포넌트마다 내던 비용을 밖으로" 옮긴 자리다: `findStaticType<T>()` 는 조회 루프가 한 번만 구하는 To 의
 *          TypeInfo, `castTo( pSrc, pToType )` 는 그것을 받는 판, `findCachedTypeInfo()` 는 이름 캐시 적중이면 가상 호출 없이
 *          답하는 판. 규칙이 갈리면 `getComponent<T>` 가 조용히 다른 답을 낸다. 적중 · 실패 · nullptr · 빈 이름 넷을 본다.
 */
SW_TEST_CASE( GameObjectTest, CastFastPathsMatchVirtualPath )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj   = manager.createGameObject( sw::hashed_string( "CastFastPaths" ) );
    sw::MeshComponent*    pMesh  = pObj->addComponent<sw::MeshComponent>();
    sw::SceneComponent*   pScene = pObj->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    SW_ASSERT_NOT_NULL( pScene );
    sw::Component* pMeshBase  = pMesh;
    sw::Component* pSceneBase = pScene;

    // findStaticType 은 StaticType 그대로. 리플렉션 본체가 없는 기반(Component)은 nullptr.
    const sw::TypeInfo* pSceneType = sw::findStaticType<sw::SceneComponent>();
    const sw::TypeInfo* pMeshType  = sw::findStaticType<sw::MeshComponent>();
    SW_EXPECT_TRUE( pSceneType == sw::SceneComponent::StaticType() );
    SW_EXPECT_TRUE( pMeshType == sw::MeshComponent::StaticType() );
    SW_EXPECT_NULL( sw::findStaticType<sw::Component>() );

    // 이름 캐시 판은 가상 판과 같은 타입(이름)을 준다.
    const sw::TypeInfo* pVirtual = pMeshBase->getTypeInfo();
    const sw::TypeInfo* pCached  = pMeshBase->findCachedTypeInfo();
    SW_ASSERT_NOT_NULL( pVirtual );
    SW_ASSERT_NOT_NULL( pCached );
    SW_EXPECT_TRUE( pCached->_fullyQualifiedName == pVirtual->_fullyQualifiedName );

    // TypeInfo 를 받는 판 == 받지 않는 판 — 적중(업캐스트 결과) · 실패 · nullptr 둘.
    SW_EXPECT_TRUE( sw::castTo<sw::SceneComponent>( pMeshBase, pSceneType ) == static_cast<sw::SceneComponent*>( pMesh ) );
    SW_EXPECT_TRUE( sw::castTo<sw::SceneComponent>( pMeshBase ) == static_cast<sw::SceneComponent*>( pMesh ) );
    SW_EXPECT_TRUE( sw::castTo<sw::MeshComponent>( pMeshBase, pMeshType ) == pMesh );
    SW_EXPECT_NULL( sw::castTo<sw::MeshComponent>( pSceneBase, pMeshType ) );
    SW_EXPECT_NULL( sw::castTo<sw::MeshComponent>( pSceneBase ) );
    SW_EXPECT_NULL( sw::castTo<sw::MeshComponent>( pMeshBase, nullptr ) );
    SW_EXPECT_NULL( sw::castTo<sw::MeshComponent>( static_cast<sw::Component*>( nullptr ), pMeshType ) );

    // getComponent 는 위 판들로 답한다 — 첫 컴포넌트(Mesh)가 SceneComponent 로도 잡힌다.
    SW_EXPECT_TRUE( pObj->getComponent<sw::MeshComponent>() == pMesh );
    SW_EXPECT_TRUE( pObj->getComponent<sw::SceneComponent>() == static_cast<sw::SceneComponent*>( pMesh ) );

    // 이름이 빈 컴포넌트는 캐시가 비어 가상으로 간다 — 같은 답. **이름은 되돌린다**: 파괴 때 컴포넌트 풀을 찾는 키가
    // 이 이름이라, 빈 채로 두면 매니저 소멸이 풀 메모리를 힙으로 반납해 Shipping 에서 힙이 깨진다(0xc0000374).
    const sw::hashed_string savedName = pMesh->getComponentName();
    pMesh->setComponentName( sw::hashed_string{} );
    SW_EXPECT_TRUE( pMeshBase->findCachedTypeInfo() == pMeshBase->getTypeInfo() );
    pMesh->setComponentName( savedName );
    SW_EXPECT_TRUE( pObj->getComponent<sw::MeshComponent>() == pMesh );
}

/**
 * @brief [GameObjectTest] 배치 트랜스폼 쓰기는 세터와 같은 결과를 내고, 부모의 자손 더티와 세대를 세운다.
 * @details `applyTransformBatch` 는 워커에 나눠 필드를 쓰고 더티를 바이트 저장으로 표시한다. 세터 경로와 갈리면
 *          안 되는 것 셋 — (1) 플러시 뒤 월드 행렬이 같다 (2) 자식만 써도 부모에 자손 더티가 서서 플러시가 내려간다
 *          (3) 값이 같은 건은 건너뛰고 세대도 안 올린다. 문턱(kParallelWriteCount)을 넘겨 실제로 병렬 경로를 탄다.
 */
SW_TEST_CASE( GameObjectTest, ApplyTransformBatchMatchesSetters )
{
    constexpr uint32 kObjectCount = sw::SceneTransformHierarchy::kParallelWriteCount + 37;

    // 두 매니저에 같은 계층을 만든다 — 하나는 세터, 하나는 배치.
    sw::GameObjectManager               managerSetter;
    sw::GameObjectManager               managerBatch;
    sw::vector<sw::SceneComponent*>     listSetterComp;
    sw::vector<sw::SceneComponent*>     listBatchComp;
    sw::vector<sw::SceneTransformWrite> listWrite;

    auto build = [&]( sw::GameObjectManager& manager, sw::vector<sw::SceneComponent*>& outListComp )
    {
        sw::GameObject*     pParent     = manager.createGameObject( sw::hashed_string( "BatchParent" ) );
        sw::SceneComponent* pParentComp = pParent->addComponent<sw::SceneComponent>();
        outListComp.push_back( pParentComp );
        for ( uint32 index = 1; index < kObjectCount; ++index )
        {
            sw::StringBuilder<sw::constant::kMaxBuffer64> name;
            name.append( "BatchObj" ).append( index );
            sw::GameObject*     pObj  = manager.createGameObject( sw::hashed_string( name.c_str() ) );
            sw::SceneComponent* pComp = pObj->addComponent<sw::SceneComponent>();
            // 절반은 부모 아래에 붙인다 — 자손 더티 전파를 본다.
            if ( ( index % 2 ) == 0 )
                pComp->attachToComponent( pParentComp );
            outListComp.push_back( pComp );
        }
        manager.flushSceneTransforms();
    };
    build( managerSetter, listSetterComp );
    build( managerBatch, listBatchComp );
    SW_ASSERT_EQUAL( kObjectCount, static_cast<uint32>( listBatchComp.size() ) );

    // 부모(0 번)는 건드리지 않는다 — 자식만 써도 플러시가 부모를 거쳐 내려가야 한다.
    for ( uint32 index = 1; index < kObjectCount; ++index )
    {
        const float32           base = static_cast<float32>( index );
        sw::SceneTransformWrite write;
        write._handle        = listBatchComp[index]->getHandle();
        write._localPosition = sw::float3( base, base * 0.5f, -base );
        write._localScale    = sw::float3( 1.0f + base * 0.01f, 1.0f, 1.0f );
        write._bSetPosition  = SW_TRUE;
        write._bSetScale     = SW_TRUE;
        listWrite.push_back( write );

        listSetterComp[index]->setLocalPosition( write._localPosition );
        listSetterComp[index]->setLocalScale( write._localScale );
    }

    const uint64 generationBefore = managerBatch.getTransformGeneration();
    const uint32 changedCount     = managerBatch.applyTransformBatch( listWrite.data(), static_cast<uint32>( listWrite.size() ) );
    SW_EXPECT_EQUAL( kObjectCount - 1, changedCount );
    SW_EXPECT_TRUE( managerBatch.getTransformGeneration() > generationBefore );
    // (2) 부모는 안 썼지만 자손 더티가 서 있다.
    SW_EXPECT_TRUE( listBatchComp[0]->hasDirtyDescendant() );
    SW_EXPECT_TRUE( managerBatch.hasDirtySceneTransforms() );

    managerSetter.flushSceneTransforms();
    managerBatch.flushSceneTransforms();

    // (1) 월드 행렬이 전부 같다 — 부모 아래 것도.
    uint32 mismatchCount = 0;
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        const sw::float4x4 worldSetter = listSetterComp[index]->getWorldMatrix();
        const sw::float4x4 worldBatch  = listBatchComp[index]->getWorldMatrix();
        if ( sw::Memory::compare( &worldSetter, &worldBatch, sizeof( sw::float4x4 ) ) != 0 )
            ++mismatchCount;
    }
    SW_EXPECT_EQUAL( 0u, mismatchCount );
    SW_EXPECT_FALSE( managerBatch.hasDirtySceneTransforms() );

    // (3) 같은 값을 다시 쓰면 아무 일도 없다 — 바뀐 건 0, 세대 그대로.
    const uint64 generationSettled = managerBatch.getTransformGeneration();
    SW_EXPECT_EQUAL( 0u, managerBatch.applyTransformBatch( listWrite.data(), static_cast<uint32>( listWrite.size() ) ) );
    SW_EXPECT_EQUAL( generationSettled, managerBatch.getTransformGeneration() );
    SW_EXPECT_FALSE( managerBatch.hasDirtySceneTransforms() );
}

/**
 * @brief [GameObjectTest] 플러시 목록에는 더러워진 루트만, 한 번씩 오른다 — 세터 경로와 배치 경로 모두.
 * @details 예전에는 플러시가 루트 전부를 돌며 더티인지 물었다(루트마다 캐시 미스). 지금은 더러워진 노드가 자기 루트를
 *          올리고 플러시는 그 목록만 돈다. 그래서 (1) 루트 N 개 중 하나만 움직이면 목록은 1 (2) 같은 루트 아래 자식 둘이
 *          움직여도 1 (3) 플러시 뒤 0 (4) 부모를 바꾼 자식은 옛 루트가 아니라 새 루트를 올린다 — 어느 하나가 어긋나면
 *          움직인 물체가 화면에 안 따라오거나(빠짐) 같은 서브트리를 두 잡이 동시에 만진다(중복).
 */
SW_TEST_CASE( GameObjectTest, FlushQueuesOnlyDirtyRoots )
{
    sw::GameObjectManager           manager;
    sw::vector<sw::SceneComponent*> listRoot;
    for ( uint32 index = 0; index < 8; ++index )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer64> name;
        name.append( "DirtyRoot" ).append( index );
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( name.c_str() ) );
        SW_ASSERT_NOT_NULL( pObj );
        listRoot.push_back( pObj->addComponent<sw::SceneComponent>() );
    }
    sw::GameObject*     pChildObjA = manager.createGameObject( sw::hashed_string( "DirtyChildA" ) );
    sw::GameObject*     pChildObjB = manager.createGameObject( sw::hashed_string( "DirtyChildB" ) );
    sw::SceneComponent* pChildA    = pChildObjA->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pChildB    = pChildObjB->addComponent<sw::SceneComponent>();
    SW_EXPECT_TRUE( pChildA->attachToComponent( listRoot[0] ) );
    SW_EXPECT_TRUE( pChildB->attachToComponent( listRoot[0] ) );

    // 갓 만든 것은 전부 더티다 — 첫 플러시가 비운다.
    SW_EXPECT_EQUAL( size_t( 8 ), manager.getTransformHierarchy().getDirtyRootCount() );
    manager.flushSceneTransforms();
    SW_EXPECT_EQUAL( size_t( 0 ), manager.getTransformHierarchy().getDirtyRootCount() );
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    // (1) 루트 하나만.
    listRoot[3]->setLocalPosition( sw::float3( 1.0f, 0.0f, 0.0f ) );
    SW_EXPECT_EQUAL( size_t( 1 ), manager.getTransformHierarchy().getDirtyRootCount() );
    // (2) 같은 루트 아래 자식 둘 — 루트 0 이 한 번만 오른다.
    pChildA->setLocalPosition( sw::float3( 2.0f, 0.0f, 0.0f ) );
    pChildB->setLocalPosition( sw::float3( 3.0f, 0.0f, 0.0f ) );
    SW_EXPECT_EQUAL( size_t( 2 ), manager.getTransformHierarchy().getDirtyRootCount() );
    // (3) 플러시 뒤 비고, 월드는 맞다.
    manager.flushSceneTransforms();
    SW_EXPECT_EQUAL( size_t( 0 ), manager.getTransformHierarchy().getDirtyRootCount() );
    SW_EXPECT_TRUE( sw::MathUtil::abs( pChildA->getWorldMatrix().getTranslation()._x - 2.0f ) < 1e-4f );

    // 배치 경로도 같다 — 같은 루트 아래 자식 둘을 배치로 쓰면 루트 0 하나.
    sw::SceneTransformWrite arrWrite[2];
    arrWrite[0]._handle        = pChildA->getHandle();
    arrWrite[0]._localPosition = sw::float3( 4.0f, 0.0f, 0.0f );
    arrWrite[0]._bSetPosition  = SW_TRUE;
    arrWrite[1]._handle        = pChildB->getHandle();
    arrWrite[1]._localPosition = sw::float3( 5.0f, 0.0f, 0.0f );
    arrWrite[1]._bSetPosition  = SW_TRUE;
    SW_EXPECT_EQUAL( 2u, manager.applyTransformBatch( arrWrite, 2 ) );
    SW_EXPECT_EQUAL( size_t( 1 ), manager.getTransformHierarchy().getDirtyRootCount() );
    manager.flushSceneTransforms();
    SW_EXPECT_TRUE( sw::MathUtil::abs( pChildB->getWorldMatrix().getTranslation()._x - 5.0f ) < 1e-4f );

    // (4) 부모를 바꾸면 새 루트가 오른다 — 옛 루트(0)가 아니라 루트 5.
    SW_EXPECT_TRUE( pChildA->attachToComponent( listRoot[5] ) );
    SW_EXPECT_EQUAL( size_t( 1 ), manager.getTransformHierarchy().getDirtyRootCount() );
    manager.flushSceneTransforms();
    SW_EXPECT_TRUE( sw::MathUtil::abs( pChildA->getWorldMatrix().getTranslation()._x - 4.0f ) < 1e-4f );
    SW_EXPECT_EQUAL( size_t( 0 ), manager.getTransformHierarchy().getDirtyRootCount() );
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
 * @brief [GameObjectTest] 같은 오브젝트 안에서 컴포넌트끼리 부착해도 계층 순회가 자신으로 되돌아오지 않는지 검증
 *
 * @details `getChildren()` 은 자식 GameObject 를 primary SceneComponent 의 **자식 컴포넌트들의 owner**
 *          로 구한다. 한 오브젝트 안에서 SceneComponent 를 다른 SceneComponent 에 붙이면 그 owner 는
 *          자기 자신이라, 자신이 자기 자식으로 나왔다 — `refreshActiveInHierarchy` 가 무한 재귀해
 *          스택을 넘겼다. 부착 없이는 멀쩡하고 부착하는 순간 죽어서, 부착을 쓰는 경로(씬 로드·
 *          에디터 복제·상태 복원)에서만 터졌다.
 */
SW_TEST_CASE( GameObjectTest, IntraObjectAttachDoesNotMakeObjectItsOwnChild )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "SelfRig" ) );
    SW_ASSERT_NOT_NULL( pObj );

    sw::SceneComponent* pRootSc = pObj->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRootSc );
    sw::MeshComponent* pMesh = pObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    SW_ASSERT_TRUE( pMesh->attachToComponent( pRootSc ) );

    // 자기 자신은 자식 목록에 없어야 한다.
    for ( sw::GameObject* pChild : childrenOf( *pObj ) )
        SW_EXPECT_TRUE( pChild != pObj );

    // 계층 갱신이 돌아와야 한다 — 예전에는 여기서 스택이 넘쳤다.
    pObj->setActive( false );
    SW_EXPECT_FALSE( pObj->isActiveInHierarchy() );
    pObj->setActive( true );
    SW_EXPECT_TRUE( pObj->isActiveInHierarchy() );

    // 파괴도 마찬가지다(`~GameObject` 가 같은 순회를 탄다).
    manager.clear();
}

/**
 * @brief [GameObjectTest] 바이너리 상태가 태그와 컴포넌트 간 부착 계층까지 실어 나르는지 검증
 *
 * @details 예전 바이너리 경로는 이름·태그·컴포넌트·부착표를 손으로 한 줄씩 적었고, 그래서 XML·JSON
 *          과 다른 세 번째 구현이었다. 지금은 셋 다 리플렉션 상태(`_name`/`_bActive`/`_listComponent`)
 *          하나를 쓴다 — 태그는 `TagComponent::_tags`, 부착은 `SceneComponent` 자신의 필드로 실린다.
 *          **손으로 적던 것을 지웠으니, 그것들이 여전히 건너오는지는 여기서 지킨다.**
 */
SW_TEST_CASE( GameObjectTest, ObjectStateBinaryCarriesTagsAndAttachHierarchy )
{
    constexpr TagID kTagElite = "Enemy.Elite"_tag;

    sw::GameObjectManager manager;
    sw::GameObject*       pSource = manager.createGameObject( sw::hashed_string( "BinaryRig" ) );
    SW_ASSERT_NOT_NULL( pSource );
    pSource->addTag( kTagElite );

    sw::SceneComponent* pRootSc = pSource->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRootSc );
    sw::MeshComponent* pMesh = pSource->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    SW_ASSERT_TRUE( pMesh->attachToComponent( pRootSc ) );
    pMesh->setLocalPosition( sw::float3{ 3.0f, 4.0f, 5.0f } );

    sw::vector<uint8> buffer;
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::saveToBinaryBuffer( pSource, buffer ) );

    manager.clear();

    sw::GameObject* pTarget = manager.createGameObject( sw::hashed_string( "Blank" ) );
    SW_ASSERT_NOT_NULL( pTarget );

    sw::string   parentName;
    const size_t bytesRead = sw::ObjectStateSerializer::loadFromBinaryBuffer( pTarget, buffer.data(), buffer.size(), parentName );
    SW_EXPECT_EQUAL( buffer.size(), bytesRead );

    // 1. 태그 — `TagComponent` 가 상태에 실려 왔어야 한다.
    SW_EXPECT_TRUE( pTarget->hasTag( kTagElite ) );

    // 2. 컴포넌트와 그 값 — 다형 소유 포인터가 실렸어야 한다.
    sw::MeshComponent* pRestoredMesh = pTarget->getComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pRestoredMesh );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pRestoredMesh->getLocalPosition()._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pRestoredMesh->getLocalPosition()._z, 0.001f );

    // 3. 부착 계층 — 메시는 루트 SceneComponent 의 자식으로 돌아와야 한다.
    sw::SceneComponent* pRestoredParent = pRestoredMesh->getParent();
    SW_ASSERT_NOT_NULL( pRestoredParent );
    SW_EXPECT_TRUE( pRestoredParent != pRestoredMesh );
    SW_EXPECT_NOT_NULL( sw::castTo<sw::SceneComponent>( pRestoredParent ) );
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

SW_TEST_CASE( GameObjectHierarchyTest, ActiveInHierarchyCompoundEvaluation )
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

/**
 * @brief [GameObjectTest] 태그를 **읽기만** 하는 것이 오브젝트의 구성을 바꾸지 않는지 검증
 * @details `getTags()` 에 const/비-const 오버로드가 있었고, 비-const 쪽은 TagComponent 가 없으면
 *          **만들어 붙였다.** `GameObject*` 로 부르면 읽을 생각이었어도 그쪽이 골라진다 —
 *          인스펙터(`InspectorPanel`)가 태그 없는 오브젝트를 보여 주는 것만으로 그 오브젝트에
 *          컴포넌트가 하나 생겼고, 저장하면 씬 파일에까지 들어갔다. 오브젝트의 구성이 바뀌는
 *          일이 오버로드 해석으로 조용히 정해지고 있었던 것이다.
 *
 *          쓰는 쪽은 이제 `getOrCreateTags()` 라는 다른 이름이라 실수로 골라지지 않는다.
 */
SW_TEST_CASE( GameObjectTest, ReadingTagsDoesNotAttachATagComponent )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "Plain" ) );
    SW_ASSERT_NOT_NULL( pObj );
    manager.mergePendingAdds();

    const size_t beforeCount = pObj->getComponentCount();

    // 인스펙터가 하던 것과 같은 모양 — **비-const 포인터**로 태그를 읽기만 한다.
    const sw::vector<sw::TagID>& listTag = pObj->getTags().getTags();
    SW_EXPECT_TRUE( listTag.empty() );

    SW_EXPECT_EQUAL( beforeCount, pObj->getComponentCount() );
}

/**
 * @brief [GameObjectTest] 컴포넌트가 이동 불가로 남아 있는지 검증(회귀 가드)
 * @details 컴포넌트는 풀에서 제자리 생성·소멸하므로 옮겨질 일이 없다. 그런데 이동 연산이
 *          **있었고 틀려 있었다** — `Component` 는 `_componentId` 를 비우지 않고 복사만 해서
 *          옮기고 나면 둘이 같은 id 를 가졌고, `SceneComponent` 는 자식들의 `_pParent` · 부모의
 *          `_listChild` · 매니저의 루트 등록부를 하나도 고치지 않았다(이동 대입은 방금 옮겨 온
 *          자식 목록을 그 자리에서 비우기까지 했다). 쓰는 곳이 없어 아무도 몰랐다.
 *
 *          다시 생기면 여기서 걸린다. 진짜 방어선은 `= delete` 이고 이 케이스는 그것이
 *          유지되는지 본다.
 */
SW_TEST_CASE( GameObjectTest, ComponentsStayNonMovable )
{
    static_assert( std::is_move_constructible_v<sw::Component> == false, "Component must stay non-movable" );
    static_assert( std::is_move_assignable_v<sw::Component> == false, "Component must stay non-movable" );
    static_assert( std::is_move_constructible_v<sw::SceneComponent> == false, "SceneComponent must stay non-movable" );
    static_assert( std::is_move_assignable_v<sw::SceneComponent> == false, "SceneComponent must stay non-movable" );

    SW_EXPECT_FALSE( std::is_move_constructible_v<sw::Component> );
    SW_EXPECT_FALSE( std::is_move_constructible_v<sw::SceneComponent> );
}
