// PhysicsShapeScaleTest — 켜진 채 트랜스폼 배율을 바꾸면 콜라이더 셰이프가 다음 물리 프레임에 새 배율을 따르고, 바디는 그대로라 닿아 있던 접촉이 이어진다(감사 S-4). 디바이스 없음(nogpu).
// 언리얼 `UpdateBodyScale`(트랜스폼 갱신마다 셰이프 배율)과 같은 계약이다 — 만들 때 크기로 남아 그림과 어긋나지 않는다.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Collision/PhysicsContact.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/CharacterController2DComponent.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

using namespace sw;

namespace
{
    struct TestPhysicsShapeScaleInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        static bool contains( const vector<PhysicsBodyHandle>& listBody, PhysicsBodyHandle body )
        {
            for ( const PhysicsBodyHandle& candidate : listBody )
            {
                if ( candidate == body )
                    return true;
            }
            return false;
        }

        /** @brief @p position 에 작은 상자를 두면 @p body 와 겹치는지입니다. */
        static bool isOverlapping3D( const IPhysicsScene3D& scene, const float3& position, PhysicsBodyHandle body )
        {
            PhysicsShapeDesc3D probe;
            probe._type        = PhysicsShapeType3D::Box;
            probe._halfExtents = float3{ 0.05f, 0.05f, 0.05f };
            vector<PhysicsBodyHandle> listBody;
            (void)scene.overlapShape( probe, position, quaternion{}, PhysicsQueryFilter{}, listBody );
            return contains( listBody, body );
        }

        static bool isOverlapping2D( const IPhysicsScene2D& scene, const float2& position, PhysicsBodyHandle body )
        {
            PhysicsShapeDesc2D probe;
            probe._type        = PhysicsShapeType2D::Box;
            probe._halfExtents = float2{ 0.05f, 0.05f };
            vector<PhysicsBodyHandle> listBody;
            (void)scene.overlapShape( probe, position, 0.0f, PhysicsQueryFilter{}, listBody );
            return contains( listBody, body );
        }

        /** @brief @p position 에 셰이프 @p shape 하나를 단 강체 오브젝트를 만듭니다. */
        template <typename TBody, typename TShape>
        static TBody* addBody( GameObjectManager& manager, const utf8* pName, const TShape& shape, PhysicsBodyType type, const float3& position )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            TBody* pBody = pObject->addComponent<TBody>();
            if ( pBody == nullptr )
                return nullptr;
            pBody->setShape( shape );
            pBody->setBodyType( type );
            pBody->setLocalPosition( position );
            return pBody;
        }

        /** @brief 이번 프레임 이벤트 가운데 바디 @p body 가 낀 @p phase 수입니다. */
        template <typename TEvent>
        static uint32 countPhase( const vector<TEvent>& listEvent, PhysicsBodyHandle body, PhysicsContactPhase phase )
        {
            uint32 count = 0;
            for ( const TEvent& event : listEvent )
            {
                const bool bInvolved = event._bodyA == body || event._bodyB == body;
                count += ( bInvolved && event._phase == phase ) ? 1u : 0u;
            }
            return count;
        }
    };
} // namespace

/**
 * @brief [PhysicsShapeScaleTest] 시작한 3D 강체의 배율을 2 로 바꾸면 다음 프레임 겹침 질의가 두 배 상자를 본다 — 거울(X 음수)은 셰이프 자리를 뒤집는다
 * @details 반 크기 0.5 정적 상자. 배율 1 에서 (0.8, 0, 0) 은 밖, 배율 2 뒤에는 안이다. 셰이프 자리 (1, 0, 0) 를 둔 바디는 배율 (-1, 1, 1) 에서 (-1, 0, 0) 에 있다.
 */
SW_TEST_CASE( PhysicsShapeScaleTest, RigidBody3DFollowsScale )
{
    using Internal = TestPhysicsShapeScaleInternal;
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Crate" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
    SW_REQUIRE_NOT_NULL( pBody );
    PhysicsShapeDesc3D box;
    box._type        = PhysicsShapeType3D::Box;
    box._halfExtents = float3{ 0.5f, 0.5f, 0.5f };
    pBody->setShape( box );
    pBody->setBodyType( PhysicsBodyType::Static );
    manager.beginPlay();
    manager.update( Internal::kFrame );
    const IPhysicsScene3D* pScene = manager.getSubsystem<ScenePhysics>().findScene3D();
    SW_REQUIRE_NOT_NULL( pScene );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    SW_EXPECT_FALSE( Internal::isOverlapping3D( *pScene, float3{ 0.8f, 0.0f, 0.0f }, pBody->getBodyHandle() ) );

    pBody->setLocalScale( float3{ 2.0f, 2.0f, 2.0f } );
    manager.update( Internal::kFrame );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    SW_EXPECT_TRUE( Internal::isOverlapping3D( *pScene, float3{ 0.8f, 0.0f, 0.0f }, pBody->getBodyHandle() ) );
    SW_EXPECT_FALSE( Internal::isOverlapping3D( *pScene, float3{ 1.2f, 0.0f, 0.0f }, pBody->getBodyHandle() ) );

    // 거울: 크기는 절댓값, 자리는 부호를 따른다(타일맵 충돌 청크와 같은 모양).
    box._halfExtents   = float3{ 0.25f, 0.25f, 0.25f };
    box._localPosition = float3{ 1.0f, 0.0f, 0.0f };
    pBody->setShape( box ); // 속성 변경은 다음 프레임에 다시 짓는다 — 배율 비교 경로만 보려고 한 프레임 먼저 돈다
    manager.update( Internal::kFrame );
    pBody->setLocalScale( float3{ -1.0f, 1.0f, 1.0f } );
    manager.update( Internal::kFrame );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    SW_EXPECT_TRUE( Internal::isOverlapping3D( *pScene, float3{ -1.0f, 0.0f, 0.0f }, pBody->getBodyHandle() ) );
    SW_EXPECT_FALSE( Internal::isOverlapping3D( *pScene, float3{ 1.0f, 0.0f, 0.0f }, pBody->getBodyHandle() ) );
}

/**
 * @brief [PhysicsShapeScaleTest] 시작한 동적 3D 강체의 배율을 바꿔도 바디는 그대로(핸들 그대로 — 셰이프만 간다)이고 속도가 이어진다
 */
SW_TEST_CASE( PhysicsShapeScaleTest, RigidBody3DRescaleKeepsVelocity )
{
    using Internal = TestPhysicsShapeScaleInternal;
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Ball" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
    SW_REQUIRE_NOT_NULL( pBody );
    PhysicsShapeDesc3D sphere;
    sphere._type   = PhysicsShapeType3D::Sphere;
    sphere._radius = 0.5f;
    pBody->setShape( sphere );
    pBody->setGravityFactor( 0.0f );
    manager.beginPlay();
    manager.update( Internal::kFrame );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    pBody->setLinearVelocity( float3{ 3.0f, 0.0f, 0.0f } );
    manager.update( Internal::kFrame );
    const PhysicsBodyHandle unscaled = pBody->getBodyHandle();

    pBody->setLocalPosition( float3{ 0.0f, 1.0f, 0.0f } ); // 옮기기만 — 배율이 같으면 다시 짓지 않는다
    manager.update( Internal::kFrame );
    SW_EXPECT_TRUE( unscaled == pBody->getBodyHandle() );
    const float32 speedBefore = pBody->getLinearVelocity()._x;
    SW_REQUIRE_TRUE( speedBefore > 2.5f );

    pBody->setLocalScale( float3{ 2.0f, 2.0f, 2.0f } );
    manager.update( Internal::kFrame );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    SW_EXPECT_TRUE( unscaled == pBody->getBodyHandle() );                // 바디를 다시 짓지 않고 셰이프만 갈았다
    SW_EXPECT_NEAR( speedBefore, pBody->getLinearVelocity()._x, 0.05f ); // 감쇠 한 프레임 몫만 다르다
}

/**
 * @brief [PhysicsShapeScaleTest] 시작한 2D 강체의 배율을 2 로 바꾸면 다음 프레임 겹침 질의가 두 배 상자를 본다
 */
SW_TEST_CASE( PhysicsShapeScaleTest, RigidBody2DFollowsScale )
{
    using Internal = TestPhysicsShapeScaleInternal;
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Crate2D" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    RigidBody2DComponent* pBody = pObject->addComponent<RigidBody2DComponent>();
    SW_REQUIRE_NOT_NULL( pBody );
    PhysicsShapeDesc2D box;
    box._type        = PhysicsShapeType2D::Box;
    box._halfExtents = float2{ 0.5f, 0.5f };
    pBody->setShape( box );
    pBody->setBodyType( PhysicsBodyType::Static );
    manager.beginPlay();
    manager.update( Internal::kFrame );
    const IPhysicsScene2D* pScene = manager.getSubsystem<ScenePhysics>().findScene2D();
    SW_REQUIRE_NOT_NULL( pScene );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    SW_EXPECT_FALSE( Internal::isOverlapping2D( *pScene, float2{ 0.8f, 0.0f }, pBody->getBodyHandle() ) );

    pBody->setLocalScale( float3{ 2.0f, 2.0f, 1.0f } );
    manager.update( Internal::kFrame );
    SW_REQUIRE_TRUE( pBody->getBodyHandle().isValid() );
    SW_EXPECT_TRUE( Internal::isOverlapping2D( *pScene, float2{ 0.8f, 0.0f }, pBody->getBodyHandle() ) );
    SW_EXPECT_FALSE( Internal::isOverlapping2D( *pScene, float2{ 1.2f, 0.0f }, pBody->getBodyHandle() ) );
}

/**
 * @brief [PhysicsShapeScaleTest] 바닥에 놓인 3D 상자의 배율을 바꿔도 접촉은 끝 → 시작을 내지 않는다(셰이프만 간다 — 감사 S-4) — 줄여 떨어지면 그때만 끝난다
 * @details 상자는 바닥에 0.01 들어가 닿은 채 놓인다. 가로(XZ)만 1.5 배로 키우면 높이가 그대로라 계속 닿아 있다. 이어서 세로를 0.5 배로 줄이면 바닥과
 *          0.24 벌어져 다음 프레임에 끝이 난다.
 */
SW_TEST_CASE( PhysicsShapeScaleTest, RigidBody3DRescaleKeepsContact )
{
    using Internal = TestPhysicsShapeScaleInternal;
    GameObjectManager  manager;
    PhysicsShapeDesc3D box;
    box._type                  = PhysicsShapeType3D::Box;
    box._halfExtents           = float3{ 0.5f, 0.5f, 0.5f };
    RigidBodyComponent* pFloor = Internal::addBody<RigidBodyComponent>( manager, "Floor", box, PhysicsBodyType::Static, float3{ 0.0f, 0.0f, 0.0f } );
    RigidBodyComponent* pCrate = Internal::addBody<RigidBodyComponent>( manager, "Crate", box, PhysicsBodyType::Dynamic, float3{ 0.0f, 0.99f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pFloor );
    SW_REQUIRE_NOT_NULL( pCrate );
    const ScenePhysics& physics = manager.getSubsystem<ScenePhysics>();
    manager.beginPlay();
    uint32 begin = 0;
    for ( uint32 frame = 0; frame < 30; ++frame )
    {
        manager.update( Internal::kFrame );
        begin += Internal::countPhase( physics.getFrameEvents3D(), pCrate->getBodyHandle(), PhysicsContactPhase::Begin );
    }
    SW_REQUIRE_EQUAL( 1u, begin ); // 바닥에 닿았다
    const PhysicsBodyHandle crate = pCrate->getBodyHandle();

    pCrate->setLocalScale( float3{ 1.5f, 1.0f, 1.5f } );
    uint32 restart = 0;
    for ( uint32 frame = 0; frame < 10; ++frame )
    {
        manager.update( Internal::kFrame );
        restart += Internal::countPhase( physics.getFrameEvents3D(), crate, PhysicsContactPhase::Begin );
        restart += Internal::countPhase( physics.getFrameEvents3D(), crate, PhysicsContactPhase::End );
    }
    SW_EXPECT_TRUE( crate == pCrate->getBodyHandle() );
    SW_EXPECT_EQUAL( 0u, restart );
    const IPhysicsScene3D* pScene = physics.findScene3D();
    SW_REQUIRE_NOT_NULL( pScene );
    SW_EXPECT_TRUE( Internal::isOverlapping3D( *pScene, float3{ 0.65f, 0.99f, 0.0f }, crate ) ); // 셰이프는 새 배율이다

    pCrate->setLocalScale( float3{ 1.5f, 0.5f, 1.5f } );
    manager.update( Internal::kFrame );
    SW_EXPECT_EQUAL( 1u, Internal::countPhase( physics.getFrameEvents3D(), crate, PhysicsContactPhase::End ) );
}

/**
 * @brief [PhysicsShapeScaleTest] 바닥에 놓인 2D 상자의 배율을 바꿔도 접촉은 끝 → 시작을 내지 않는다 — 줄여 떨어지면 그때만 끝난다(3D 와 같은 계약)
 * @details Box2D 는 셰이프를 떼고 다시 붙인다 — 옛 셰이프의 끝은 버리고 추적기가 쌍을 이어 둔다(`PhysicsContactTracker::resetSubContacts`).
 */
SW_TEST_CASE( PhysicsShapeScaleTest, RigidBody2DRescaleKeepsContact )
{
    using Internal = TestPhysicsShapeScaleInternal;
    GameObjectManager  manager;
    PhysicsShapeDesc2D box;
    box._type                    = PhysicsShapeType2D::Box;
    box._halfExtents             = float2{ 0.5f, 0.5f };
    RigidBody2DComponent* pFloor = Internal::addBody<RigidBody2DComponent>( manager, "Floor2D", box, PhysicsBodyType::Static, float3{ 0.0f, 0.0f, 0.0f } );
    RigidBody2DComponent* pCrate = Internal::addBody<RigidBody2DComponent>( manager, "Crate2D", box, PhysicsBodyType::Dynamic, float3{ 0.0f, 0.99f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pFloor );
    SW_REQUIRE_NOT_NULL( pCrate );
    const ScenePhysics& physics = manager.getSubsystem<ScenePhysics>();
    manager.beginPlay();
    uint32 begin = 0;
    for ( uint32 frame = 0; frame < 30; ++frame )
    {
        manager.update( Internal::kFrame );
        begin += Internal::countPhase( physics.getFrameEvents2D(), pCrate->getBodyHandle(), PhysicsContactPhase::Begin );
    }
    SW_REQUIRE_EQUAL( 1u, begin );
    const PhysicsBodyHandle crate = pCrate->getBodyHandle();

    pCrate->setLocalScale( float3{ 1.5f, 1.0f, 1.0f } );
    uint32 restart = 0;
    for ( uint32 frame = 0; frame < 10; ++frame )
    {
        manager.update( Internal::kFrame );
        restart += Internal::countPhase( physics.getFrameEvents2D(), crate, PhysicsContactPhase::Begin );
        restart += Internal::countPhase( physics.getFrameEvents2D(), crate, PhysicsContactPhase::End );
    }
    SW_EXPECT_TRUE( crate == pCrate->getBodyHandle() );
    SW_EXPECT_EQUAL( 0u, restart );
    const IPhysicsScene2D* pScene = physics.findScene2D();
    SW_REQUIRE_NOT_NULL( pScene );
    SW_EXPECT_TRUE( Internal::isOverlapping2D( *pScene, float2{ 0.65f, 0.99f }, crate ) );

    pCrate->setLocalScale( float3{ 1.5f, 0.5f, 1.0f } );
    manager.update( Internal::kFrame );
    SW_EXPECT_EQUAL( 1u, Internal::countPhase( physics.getFrameEvents2D(), crate, PhysicsContactPhase::End ) );
}

/**
 * @brief [PhysicsShapeScaleTest] 배율 축이 0 이면 바디를 만들지 않고 오류를 남긴다(예전에는 배율 1 로 덮어 그림과 다른 크기로 시뮬레이션했다) — 돌아오면 만든다
 * @details 시뮬레이션 중에 0 이 되면 바디를 놓는다. 오류는 오브젝트마다 한 번이다(`PhysicsComponent::readWorldPose`).
 */
SW_TEST_CASE( PhysicsShapeScaleTest, ZeroScaleSkipsBody )
{
    using Internal = TestPhysicsShapeScaleInternal;
    SW_EXPECT_LOG( "PhysicsComponent.ZeroWorldScale" );
    GameObjectManager  manager;
    PhysicsShapeDesc3D box;
    box._type                  = PhysicsShapeType3D::Box;
    box._halfExtents           = float3{ 0.5f, 0.5f, 0.5f };
    RigidBodyComponent* pCrate = Internal::addBody<RigidBodyComponent>( manager, "Flat", box, PhysicsBodyType::Static, float3{ 0.0f, 0.0f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pCrate );
    pCrate->setLocalScale( float3{ 1.0f, 0.0f, 1.0f } );
    manager.beginPlay();
    manager.update( Internal::kFrame );
    SW_EXPECT_FALSE( pCrate->getBodyHandle().isValid() );

    pCrate->setLocalScale( float3{ 1.0f, 1.0f, 1.0f } );
    manager.update( Internal::kFrame );
    SW_EXPECT_TRUE( pCrate->getBodyHandle().isValid() );

    pCrate->setLocalScale( float3{ 0.0f, 1.0f, 1.0f } ); // 시뮬레이션 중에 0 — 바디를 놓는다
    manager.update( Internal::kFrame );
    SW_EXPECT_FALSE( pCrate->getBodyHandle().isValid() );
}

/**
 * @brief [PhysicsShapeScaleTest] 시작한 캐릭터 컨트롤러(2D · 3D)의 배율을 바꿔도 캐릭터를 다시 만들지 않는다(핸들 그대로 — `setCharacterSize`, 감사 S-4)
 * @details 다시 만들면 3D 속 바디의 접촉 쌍마다 끝 → 시작이 났다. 크기만 가면 캐릭터 · 속 바디 핸들이 그대로다.
 */
SW_TEST_CASE( PhysicsShapeScaleTest, CharacterRescaleKeepsCharacter )
{
    using Internal = TestPhysicsShapeScaleInternal;
    GameObjectManager manager;
    GameObject*       pWalker   = manager.createGameObject( hashed_string( "Walker" ) );
    GameObject*       pWalker2D = manager.createGameObject( hashed_string( "Walker2D" ) );
    SW_REQUIRE_NOT_NULL( pWalker );
    SW_REQUIRE_NOT_NULL( pWalker2D );
    CharacterControllerComponent*   pCharacter   = pWalker->addComponent<CharacterControllerComponent>();
    CharacterController2DComponent* pCharacter2D = pWalker2D->addComponent<CharacterController2DComponent>();
    SW_REQUIRE_NOT_NULL( pCharacter );
    SW_REQUIRE_NOT_NULL( pCharacter2D );
    manager.beginPlay();
    manager.update( Internal::kFrame );
    const PhysicsCharacterHandle character   = pCharacter->getCharacterHandle();
    const PhysicsCharacterHandle character2D = pCharacter2D->getCharacterHandle();
    SW_REQUIRE_TRUE( character.isValid() );
    SW_REQUIRE_TRUE( character2D.isValid() );

    pCharacter->setLocalScale( float3{ 2.0f, 2.0f, 2.0f } );
    pCharacter2D->setLocalScale( float3{ 2.0f, 2.0f, 1.0f } );
    manager.update( Internal::kFrame );
    SW_EXPECT_TRUE( character == pCharacter->getCharacterHandle() );
    SW_EXPECT_TRUE( character2D == pCharacter2D->getCharacterHandle() );
}
