// TriggerOverlapContractTest — 트리거 진입 · 이탈 계약을 실제 물리 씬(Jolt 3D · Box2D 2D)에서 본다(DesignBacklog "트리거 Enter · Stay · Exit 계약"). 디바이스 없음(nogpu).
// 안에서 생성 → 진입 한 번, 파괴 · 비활성 → 이탈 한 번, 트리거 끄고 켜기, 순간 이동(지나친 길은 이벤트 없음), 몸 둘인 오브젝트 한 번, 같은 프레임 이탈 먼저, 처리 안 파괴.
// 서버 단위(`Server --test`)에도 그대로 돈다 — 전용 서버도 Engine(물리)을 싣는다.
#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Physics/Scene/Tests/OverlapRecorderTestComponent.h"

using namespace sw;

namespace
{
    struct TestTriggerOverlapContractInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 반 크기 2 인 정적 3D 트리거와 기록 컴포넌트를 원점에 둡니다. */
        static OverlapRecorderTestComponent* addTrigger3D( GameObjectManager& manager, RigidBodyComponent** ppOutBody = nullptr )
        {
            GameObject*         pObject = manager.createGameObject( hashed_string( "Trigger" ) );
            RigidBodyComponent* pBody   = pObject->addComponent<RigidBodyComponent>();
            PhysicsShapeDesc3D  box;
            box._type        = PhysicsShapeType3D::Box;
            box._halfExtents = float3{ 2.0f, 2.0f, 2.0f };
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Static );
            pBody->setTrigger( true );
            pBody->setLayer( hashed_string( "Trigger" ) );
            if ( ppOutBody != nullptr )
                *ppOutBody = pBody;
            return pObject->addComponent<OverlapRecorderTestComponent>();
        }

        /** @brief 떠 있는(중력 0) 동적 상자 몸 하나를 @p pObject 에 답니다. */
        static RigidBodyComponent* addBody3D( GameObject& object, const float3& position )
        {
            RigidBodyComponent* pBody = object.addComponent<RigidBodyComponent>();
            PhysicsShapeDesc3D  box;
            box._type        = PhysicsShapeType3D::Box;
            box._halfExtents = float3{ 0.25f, 0.25f, 0.25f };
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Dynamic );
            pBody->setGravityFactor( 0.0f );
            pBody->setLocalPosition( position );
            return pBody;
        }

        static GameObject* addVisitor3D( GameObjectManager& manager, const utf8* pName, const float3& position )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            (void)addBody3D( *pObject, position );
            return pObject;
        }

        static OverlapRecorderTestComponent* addTrigger2D( GameObjectManager& manager, RigidBody2DComponent** ppOutBody = nullptr )
        {
            GameObject*           pObject = manager.createGameObject( hashed_string( "Trigger2D" ) );
            RigidBody2DComponent* pBody   = pObject->addComponent<RigidBody2DComponent>();
            PhysicsShapeDesc2D    box;
            box._type        = PhysicsShapeType2D::Box;
            box._halfExtents = float2{ 2.0f, 2.0f };
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Static );
            pBody->setTrigger( true );
            if ( ppOutBody != nullptr )
                *ppOutBody = pBody;
            return pObject->addComponent<OverlapRecorderTestComponent>();
        }

        static RigidBody2DComponent* addBody2D( GameObject& object, const float3& position )
        {
            RigidBody2DComponent* pBody = object.addComponent<RigidBody2DComponent>();
            PhysicsShapeDesc2D    box;
            box._type        = PhysicsShapeType2D::Box;
            box._halfExtents = float2{ 0.25f, 0.25f };
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Dynamic );
            pBody->setGravityFactor( 0.0f );
            pBody->setLocalPosition( position );
            return pBody;
        }

        static void run( GameObjectManager& manager, uint32 frameCount )
        {
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                manager.update( kFrame );
            }
        }
    };
} // namespace

/** @brief [TriggerOverlapContractTest] 3D: 트리거 안에서 생긴 대상은 진입 한 번(다음 물리 스텝), 안에서 지우면 이탈 한 번 — 언리얼 생성 시 겹침 갱신 · 유니티 함정(파괴에 Exit 없음)을 피한다 */
SW_TEST_CASE( TriggerOverlapContractTest, SpawnInsideAndDestroyInside3D )
{
    using Internal = TestTriggerOverlapContractInternal;
    GameObjectManager             manager;
    OverlapRecorderTestComponent* pRecorder = Internal::addTrigger3D( manager );
    SW_REQUIRE_NOT_NULL( pRecorder );
    manager.beginPlay();
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 0u, pRecorder->_beginCount );

    GameObject* pHero = Internal::addVisitor3D( manager, "Hero", float3{ 0.0f, 0.0f, 0.0f } ); // 플레이 중 안에서 생성(로그인 · 스폰)
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 1u, pRecorder->_beginCount );
    SW_EXPECT_EQUAL( 0u, pRecorder->_endCount );
    const ScenePhysics& physics = manager.getSubsystem<ScenePhysics>();
    SW_EXPECT_EQUAL( 1u, physics.getOverlapTracker().getOverlapCount( pHero->getObjectID() ) );

    manager.destroyGameObject( pHero ); // 안에서 파괴(로그아웃 · 죽음)
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 1u, pRecorder->_beginCount );
    SW_EXPECT_EQUAL( 1u, pRecorder->_endCount );
    SW_EXPECT_EQUAL( 0u, physics.getOverlapTracker().getOverlapCount( pRecorder->getOwner()->getObjectID() ) );
}

/** @brief [TriggerOverlapContractTest] 3D: 대상을 끄면 이탈, 다시 켜면 진입. 트리거를 끄면 안의 대상이 이탈, 다시 켜면 진입 — 한 번씩 */
SW_TEST_CASE( TriggerOverlapContractTest, DeactivateAndToggleTrigger3D )
{
    using Internal = TestTriggerOverlapContractInternal;
    GameObjectManager             manager;
    RigidBodyComponent*           pTriggerBody = nullptr;
    OverlapRecorderTestComponent* pRecorder    = Internal::addTrigger3D( manager, &pTriggerBody );
    GameObject*                   pHero        = Internal::addVisitor3D( manager, "Hero", float3{ 0.5f, 0.0f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pRecorder );
    SW_REQUIRE_NOT_NULL( pTriggerBody );
    manager.beginPlay();
    Internal::run( manager, 3 );
    SW_REQUIRE_EQUAL( 1u, pRecorder->_beginCount );

    pHero->setActive( false );
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 1u, pRecorder->_endCount );
    pHero->setActive( true );
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 2u, pRecorder->_beginCount );

    pTriggerBody->setActive( false ); // 트리거 콜리전 끔 — 기록 컴포넌트는 켜진 채 이탈을 받는다
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 2u, pRecorder->_endCount );
    pTriggerBody->setActive( true );
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 3u, pRecorder->_beginCount );
    SW_EXPECT_EQUAL( 2u, pRecorder->_endCount );
}

/** @brief [TriggerOverlapContractTest] 3D: 순간 이동으로 트리거를 건너뛰면 아무 이벤트도 없다(언리얼 Teleport · 유니티와 같다). 안으로 옮기면 진입, 밖으로 옮기면 이탈 */
SW_TEST_CASE( TriggerOverlapContractTest, TeleportAcrossAndInto3D )
{
    using Internal = TestTriggerOverlapContractInternal;
    GameObjectManager             manager;
    OverlapRecorderTestComponent* pRecorder = Internal::addTrigger3D( manager );
    GameObject*                   pHero     = Internal::addVisitor3D( manager, "Hero", float3{ -10.0f, 0.0f, 0.0f } );
    manager.beginPlay();
    Internal::run( manager, 2 );
    RigidBodyComponent* pBody = pHero->getComponent<RigidBodyComponent>();
    SW_REQUIRE_NOT_NULL( pBody );
    pBody->setLocalPosition( float3{ 10.0f, 0.0f, 0.0f } );
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 0u, pRecorder->_beginCount );
    SW_EXPECT_EQUAL( 0u, pRecorder->_endCount );
    pBody->setLocalPosition( float3{ 0.0f, 0.0f, 0.0f } );
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 1u, pRecorder->_beginCount );
    pBody->setLocalPosition( float3{ 10.0f, 0.0f, 0.0f } );
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 1u, pRecorder->_endCount );
}

/** @brief [TriggerOverlapContractTest] 3D: 몸이 둘인 오브젝트는 진입 한 번, 몸 하나를 떼어도 이탈 없음, 오브젝트를 지우면 이탈 한 번 */
SW_TEST_CASE( TriggerOverlapContractTest, TwoBodiesCountOnce3D )
{
    using Internal = TestTriggerOverlapContractInternal;
    GameObjectManager             manager;
    OverlapRecorderTestComponent* pRecorder = Internal::addTrigger3D( manager );
    GameObject*                   pCart     = manager.createGameObject( hashed_string( "Cart" ) );
    RigidBodyComponent*           pFront    = Internal::addBody3D( *pCart, float3{ 0.75f, 0.0f, 0.0f } );
    RigidBodyComponent*           pBack     = Internal::addBody3D( *pCart, float3{ -1.5f, 0.0f, 0.0f } ); // 둘째 씬 컴포넌트는 primary 에 붙는다 — 월드 x = -0.75
    SW_REQUIRE_NOT_NULL( pFront );
    SW_REQUIRE_NOT_NULL( pBack );
    manager.beginPlay();
    Internal::run( manager, 3 );
    const ScenePhysics& physics = manager.getSubsystem<ScenePhysics>();
    SW_EXPECT_EQUAL( 2u, physics.getOverlapTracker().getBodyPairCount( pCart->getObjectID(), pRecorder->getOwner()->getObjectID() ) );
    SW_EXPECT_EQUAL( 1u, pRecorder->_beginCount );

    pBack->setActive( false ); // 몸 하나가 빠진다 — 오브젝트는 아직 안이다
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 0u, pRecorder->_endCount );
    manager.destroyGameObject( pCart );
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 1u, pRecorder->_beginCount );
    SW_EXPECT_EQUAL( 1u, pRecorder->_endCount );
}

/** @brief [TriggerOverlapContractTest] 3D: 같은 프레임에 한 대상이 나가고 다른 대상이 들면 이탈이 먼저 온다 */
SW_TEST_CASE( TriggerOverlapContractTest, ExitBeforeEnterInOneFrame3D )
{
    using Internal = TestTriggerOverlapContractInternal;
    GameObjectManager             manager;
    OverlapRecorderTestComponent* pRecorder = Internal::addTrigger3D( manager );
    GameObject*                   pHero     = Internal::addVisitor3D( manager, "Hero", float3{ 0.0f, 0.0f, 0.0f } );
    GameObject*                   pCrate    = Internal::addVisitor3D( manager, "Crate", float3{ 10.0f, 0.0f, 0.0f } );
    manager.beginPlay();
    Internal::run( manager, 3 );
    SW_REQUIRE_EQUAL( 1u, pRecorder->_beginCount );
    pHero->getComponent<RigidBodyComponent>()->setLocalPosition( float3{ -10.0f, 0.0f, 0.0f } );
    pCrate->getComponent<RigidBodyComponent>()->setLocalPosition( float3{ 0.0f, 0.0f, 0.0f } );
    manager.update( Internal::kFrame ); // 한 프레임 — 둘이 같은 스텝에 바뀐다
    Internal::run( manager, 2 );
    SW_REQUIRE_EQUAL( 6u, static_cast<uint32>( pRecorder->_listOrder.size() ) );
    SW_EXPECT_EQUAL( static_cast<uint8>( 'E' ), pRecorder->_listOrder[2] );
    SW_EXPECT_EQUAL( static_cast<uint8>( pHero->getObjectID() & 0xFFu ), pRecorder->_listOrder[3] );
    SW_EXPECT_EQUAL( static_cast<uint8>( 'B' ), pRecorder->_listOrder[4] );
    SW_EXPECT_EQUAL( static_cast<uint8>( pCrate->getObjectID() & 0xFFu ), pRecorder->_listOrder[5] );
}

/** @brief [TriggerOverlapContractTest] 3D: 진입 처리 안에서 상대를 지워도 안전하고, 그 대상의 이탈은 다음 프레임에 한 번 온다(상대는 이미 없다) */
SW_TEST_CASE( TriggerOverlapContractTest, DestroyInsideCallback3D )
{
    using Internal = TestTriggerOverlapContractInternal;
    GameObjectManager             manager;
    OverlapRecorderTestComponent* pRecorder = Internal::addTrigger3D( manager );
    pRecorder->_bDestroyOtherOnBegin        = true;
    (void)Internal::addVisitor3D( manager, "Coin", float3{ 0.0f, 0.0f, 0.0f } );
    (void)Internal::addVisitor3D( manager, "Gem", float3{ 1.0f, 1.0f, 0.0f } );
    manager.beginPlay();
    Internal::run( manager, 5 );
    SW_EXPECT_EQUAL( 2u, pRecorder->_beginCount );
    SW_EXPECT_EQUAL( 2u, pRecorder->_endCount );
    SW_EXPECT_EQUAL( 0u, manager.getSubsystem<ScenePhysics>().getOverlapTracker().getOverlapCount( pRecorder->getOwner()->getObjectID() ) );
}

/** @brief [TriggerOverlapContractTest] 2D(Box2D 센서): 안에서 생성 → 진입 한 번, 몸 둘 → 한 번, 끄면 이탈, 파괴 → 이탈 한 번 */
SW_TEST_CASE( TriggerOverlapContractTest, SpawnToggleAndDestroy2D )
{
    using Internal = TestTriggerOverlapContractInternal;
    GameObjectManager             manager;
    OverlapRecorderTestComponent* pRecorder = Internal::addTrigger2D( manager );
    SW_REQUIRE_NOT_NULL( pRecorder );
    manager.beginPlay();
    Internal::run( manager, 2 );
    GameObject* pCart = manager.createGameObject( hashed_string( "Cart2D" ) );
    (void)Internal::addBody2D( *pCart, float3{ 0.75f, 0.0f, 0.0f } );
    RigidBody2DComponent* pBack = Internal::addBody2D( *pCart, float3{ -1.5f, 0.0f, 0.0f } ); // primary 에 붙는다 — 월드 x = -0.75
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 1u, pRecorder->_beginCount );

    pBack->setActive( false );
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 0u, pRecorder->_endCount );
    pCart->setActive( false );
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 1u, pRecorder->_endCount );
    pCart->setActive( true );
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 2u, pRecorder->_beginCount );
    manager.destroyGameObject( pCart );
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 2u, pRecorder->_endCount );
}

/**
 * @brief [TriggerOverlapContractTest] 막는 접촉은 바디 쌍마다다 — 몸이 둘인 오브젝트가 바닥에 떨어지면 바닥은 접촉 시작을 둘 받는다(충격 단위: 조각마다 피해 · 소리)
 * @details 겹침(있음 · 없음)은 오브젝트 쌍으로 모으지만 접촉은 부딪힌 바디마다의 충격이라 모으지 않는다(언리얼 OnComponentHit · 유니티 콜라이더 쌍과 같다, DesignBacklog 트리거 계약 7).
 */
SW_TEST_CASE( TriggerOverlapContractTest, CollisionStaysPerBodyPair3D )
{
    using Internal = TestTriggerOverlapContractInternal;
    GameObjectManager   manager;
    GameObject*         pFloor = manager.createGameObject( hashed_string( "Floor" ) );
    RigidBodyComponent* pBody  = pFloor->addComponent<RigidBodyComponent>();
    PhysicsShapeDesc3D  box;
    box._type        = PhysicsShapeType3D::Box;
    box._halfExtents = float3{ 10.0f, 0.5f, 10.0f };
    pBody->setShape( box );
    pBody->setBodyType( PhysicsBodyType::Static );
    pBody->setLocalPosition( float3{ 0.0f, -0.5f, 0.0f } );
    OverlapRecorderTestComponent* pRecorder = pFloor->addComponent<OverlapRecorderTestComponent>();
    GameObject*                   pCart     = manager.createGameObject( hashed_string( "Cart" ) );
    RigidBodyComponent*           pFront    = Internal::addBody3D( *pCart, float3{ 1.0f, 0.5f, 0.0f } );
    RigidBodyComponent*           pBack     = Internal::addBody3D( *pCart, float3{ -2.0f, 0.0f, 0.0f } ); // primary 에 붙는다 — 월드 x = -1
    SW_REQUIRE_NOT_NULL( pRecorder );
    pFront->setGravityFactor( 1.0f );
    pBack->setGravityFactor( 1.0f );
    manager.beginPlay();
    Internal::run( manager, 60 );
    SW_EXPECT_EQUAL( 2u, pRecorder->_collisionBeginCount );
    SW_EXPECT_EQUAL( 0u, pRecorder->_beginCount );
}
