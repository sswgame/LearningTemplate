#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Physics/CharacterController2DComponent.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/Physics/JointComponent.h"
#include "Engine/Object/Component/Physics/RigidBody2DComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    /** @brief 강체 물리 이벤트(막는 접촉 · 트리거)를 적는 컴포넌트입니다. */
    class MockCollisionListenerComponent : public Component
    {
    public:
        REFLECT_BODY();

        vector<uint64>  _listCollisionBeginOther;
        vector<float32> _listCollisionBeginImpulse;
        uint32          _collisionEndCount{ 0 };
        uint32          _overlapBeginCount{ 0 };
        uint32          _overlapStayCount{ 0 };
        uint32          _overlapEndCount{ 0 };

        const TypeInfo* getTypeInfo() const override { return StaticType(); }
        void            onCollisionBegin( const CollisionInfo& collision ) override
        {
            _listCollisionBeginOther.push_back( collision._pOther != nullptr ? collision._pOther->getObjectId() : 0 );
            _listCollisionBeginImpulse.push_back( collision._impulse );
        }
        void onCollisionEnd( const CollisionInfo& collision ) override
        {
            (void)collision;
            ++_collisionEndCount;
        }
        void onOverlapBegin( const OverlapInfo& overlap ) override
        {
            (void)overlap;
            ++_overlapBeginCount;
        }
        void onOverlapStay( const OverlapInfo& overlap ) override
        {
            (void)overlap;
            ++_overlapStayCount;
        }
        void onOverlapEnd( const OverlapInfo& overlap ) override
        {
            (void)overlap;
            ++_overlapEndCount;
        }
    };

    inline const TypeInfo* MockCollisionListenerComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockCollisionListenerComponent>, hashed_string( "MockCollisionListenerComponent" ),
                                          hashed_string( "sw::MockCollisionListenerComponent" ), sizeof( MockCollisionListenerComponent ) );
    }
} // namespace sw

// 강체 컴포넌트 — 씬의 물리(`ScenePhysics`)가 고정 스텝으로 돌고, 보간한 자세를 트랜스폼에 쓰고, 이벤트를 컴포넌트에 나눠 준다.

namespace
{
    constexpr float32 kFrame = 1.0f / 60.0f;

    sw::RigidBodyComponent* spawnBody( sw::GameObjectManager& manager, const utf8* pName, const sw::float3& position, const sw::float3& halfExtents,
                                       sw::PhysicsBodyType type )
    {
        sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        sw::RigidBodyComponent* pBody = pObject->addComponent<sw::RigidBodyComponent>();
        if ( pBody == nullptr )
            return nullptr;
        sw::PhysicsShapeDesc3D box;
        box._halfExtents = halfExtents;
        pBody->setShape( box );
        pBody->setBodyType( type );
        pBody->setLocalPosition( position );
        return pBody;
    }

    void tickFor( sw::GameObjectManager& manager, uint32 frameCount )
    {
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
            manager.tick( kFrame );
    }
} // namespace

/**
 * @brief [PhysicsComponentTest] 동적 강체 컴포넌트는 정적 바닥 위에 멈추고, 그 자리가 트랜스폼에 쓰이고, 부딪힘이 두 오브젝트에 간다
 */
SW_TEST_CASE( PhysicsComponentTest, DynamicBodyLandsAndReportsCollision )
{
    sw::GameObjectManager   manager;
    sw::RigidBodyComponent* pFloor = spawnBody( manager, "Floor", sw::float3{ 0.0f, -0.5f, 0.0f }, sw::float3{ 10.0f, 0.5f, 10.0f }, sw::PhysicsBodyType::Static );
    sw::RigidBodyComponent* pCrate = spawnBody( manager, "Crate", sw::float3{ 0.0f, 3.0f, 0.0f }, sw::float3{ 0.5f, 0.5f, 0.5f }, sw::PhysicsBodyType::Dynamic );
    SW_ASSERT_NOT_NULL( pFloor );
    SW_ASSERT_NOT_NULL( pCrate );
    sw::MockCollisionListenerComponent* pCrateListener = pCrate->getOwner()->addComponent<sw::MockCollisionListenerComponent>();
    sw::MockCollisionListenerComponent* pFloorListener = pFloor->getOwner()->addComponent<sw::MockCollisionListenerComponent>();
    SW_ASSERT_NOT_NULL( pCrateListener );
    SW_ASSERT_NOT_NULL( pFloorListener );

    // 시작 전에는 바디가 없다(편집 중에는 시뮬레이션하지 않는다).
    manager.tick( kFrame );
    SW_EXPECT_FALSE( pCrate->getBodyHandle().isValid() );

    manager.beginPlay();
    tickFor( manager, 240 );
    SW_EXPECT_TRUE( pCrate->getBodyHandle().isValid() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pCrate->getWorldPosition()._y, 0.03f );
    SW_EXPECT_NEAR_EQUAL( -0.5f, pFloor->getWorldPosition()._y, 1e-5f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), pCrateListener->_listCollisionBeginOther.size() );
    SW_EXPECT_EQUAL( pFloor->getOwner()->getObjectId(), pCrateListener->_listCollisionBeginOther[0] );
    SW_EXPECT_TRUE( pCrateListener->_listCollisionBeginImpulse[0] > 0.0f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), pFloorListener->_listCollisionBeginOther.size() );
    SW_EXPECT_EQUAL( pCrate->getOwner()->getObjectId(), pFloorListener->_listCollisionBeginOther[0] );
    SW_EXPECT_TRUE( manager.getScenePhysics().getStepCount() >= 240 );

    // 코드가 옮기면 순간이동이다 — 다시 떨어진다.
    pCrate->teleportTo( sw::float3{ 5.0f, 4.0f, 0.0f } );
    manager.tick( kFrame );
    SW_EXPECT_TRUE( pCrate->getWorldPosition()._y > 3.5f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pCrate->getWorldPosition()._x, 1e-3f );
    SW_EXPECT_EQUAL( 1u, pCrateListener->_collisionEndCount ); // 바닥을 떠났다
    tickFor( manager, 240 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pCrate->getWorldPosition()._y, 0.03f );

    // 끝나면 바디를 놓는다.
    manager.endPlay();
    manager.tick( kFrame );
    SW_EXPECT_FALSE( pCrate->getBodyHandle().isValid() );
}

/**
 * @brief [PhysicsComponentTest] 그리는 자세는 마지막 두 스텝 사이의 보간이다 — 반 스텝짜리 프레임은 스텝을 돌지 않고 자세만 옮긴다
 */
SW_TEST_CASE( PhysicsComponentTest, PoseIsInterpolatedBetweenSteps )
{
    sw::GameObjectManager   manager;
    sw::RigidBodyComponent* pBall = spawnBody( manager, "Ball", sw::float3{ 0.0f, 50.0f, 0.0f }, sw::float3{ 0.5f, 0.5f, 0.5f }, sw::PhysicsBodyType::Dynamic );
    SW_ASSERT_NOT_NULL( pBall );
    manager.beginPlay();
    tickFor( manager, 30 );
    const uint64  stepsBefore  = manager.getScenePhysics().getStepCount();
    const float32 heightBefore = pBall->getWorldPosition()._y;
    manager.tick( kFrame * 0.5f );
    SW_EXPECT_EQUAL( stepsBefore, manager.getScenePhysics().getStepCount() );
    const float32 alpha = manager.getScenePhysics().getInterpolationAlpha();
    SW_EXPECT_TRUE( alpha > 0.3f && alpha < 0.7f );
    SW_EXPECT_TRUE_MSG( pBall->getWorldPosition()._y < heightBefore, "the drawn pose moved toward the last step without a new step" );
    manager.endPlay();
}

/**
 * @brief [PhysicsComponentTest] 트리거 컴포넌트를 지난 상자는 그 오브젝트에 겹침 시작 · 끝을 한 번씩, 그 사이 유지를 준다
 */
SW_TEST_CASE( PhysicsComponentTest, TriggerComponentReportsOverlaps )
{
    sw::GameObjectManager   manager;
    sw::RigidBodyComponent* pZone = spawnBody( manager, "Zone", sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 2.0f, 1.0f, 2.0f }, sw::PhysicsBodyType::Static );
    sw::RigidBodyComponent* pRock = spawnBody( manager, "Rock", sw::float3{ 0.0f, 4.0f, 0.0f }, sw::float3{ 0.25f, 0.25f, 0.25f }, sw::PhysicsBodyType::Dynamic );
    SW_ASSERT_NOT_NULL( pZone );
    SW_ASSERT_NOT_NULL( pRock );
    pZone->setTrigger( true );
    sw::MockCollisionListenerComponent* pListener = pZone->getOwner()->addComponent<sw::MockCollisionListenerComponent>();
    SW_ASSERT_NOT_NULL( pListener );
    manager.beginPlay();
    tickFor( manager, 180 );
    SW_EXPECT_EQUAL( 1u, pListener->_overlapBeginCount );
    SW_EXPECT_EQUAL( 1u, pListener->_overlapEndCount );
    SW_EXPECT_TRUE( pListener->_overlapStayCount > 0 );
    SW_EXPECT_TRUE( pListener->_listCollisionBeginOther.empty() );
    manager.endPlay();
}

/**
 * @brief [PhysicsComponentTest] 관절 컴포넌트는 자기 강체를 부모 오브젝트의 강체에 잇는다 — 부모가 정적이면 자식이 매달린다
 */
SW_TEST_CASE( PhysicsComponentTest, JointComponentConnectsToParentBody )
{
    sw::GameObjectManager   manager;
    sw::RigidBodyComponent* pAnchor = spawnBody( manager, "Anchor", sw::float3{ 0.0f, 5.0f, 0.0f }, sw::float3{ 0.2f, 0.2f, 0.2f }, sw::PhysicsBodyType::Static );
    sw::RigidBodyComponent* pBob    = spawnBody( manager, "Bob", sw::float3{ 0.0f, 3.0f, 0.0f }, sw::float3{ 0.2f, 0.2f, 0.2f }, sw::PhysicsBodyType::Dynamic );
    SW_ASSERT_NOT_NULL( pAnchor );
    SW_ASSERT_NOT_NULL( pBob );
    SW_ASSERT_TRUE( pBob->getOwner()->attachToParent( pAnchor->getOwner() ) );
    pBob->setLocalPosition( sw::float3{ 0.0f, -2.0f, 0.0f } );
    sw::JointComponent* pJoint = pBob->getOwner()->addComponent<sw::JointComponent>();
    SW_ASSERT_NOT_NULL( pJoint );
    pJoint->setJointType( sw::PhysicsJointType::Distance );
    pJoint->setLocalPosition( sw::float3{ 0.0f, 0.0f, 0.0f } );
    manager.beginPlay();
    tickFor( manager, 120 );
    SW_EXPECT_TRUE( pJoint->getJointHandle().isValid() );
    // 줄 길이 2 m 를 지킨다(관절이 없으면 바닥 없이 떨어진다).
    SW_EXPECT_NEAR_EQUAL( 2.0f, sw::float3::getDistance( pAnchor->getWorldPosition(), pBob->getWorldPosition() ), 0.05f );
    manager.endPlay();
}

/**
 * @brief [PhysicsComponentTest] 캐릭터 컨트롤러 컴포넌트는 중력으로 바닥에 서고 원하는 속도로 걷는다
 */
SW_TEST_CASE( PhysicsComponentTest, CharacterControllerWalksOnFloor )
{
    sw::GameObjectManager   manager;
    sw::RigidBodyComponent* pFloor = spawnBody( manager, "Floor", sw::float3{ 0.0f, -0.5f, 0.0f }, sw::float3{ 20.0f, 0.5f, 20.0f }, sw::PhysicsBodyType::Static );
    SW_ASSERT_NOT_NULL( pFloor );
    sw::GameObject*                   pHero      = manager.createGameObject( sw::hashed_string( "Hero" ) );
    sw::CharacterControllerComponent* pCharacter = pHero->addComponent<sw::CharacterControllerComponent>();
    SW_ASSERT_NOT_NULL( pCharacter );
    pCharacter->setLocalPosition( sw::float3{ 0.0f, 1.0f, 0.0f } );
    manager.beginPlay();
    tickFor( manager, 60 );
    SW_EXPECT_TRUE( pCharacter->isGrounded() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pCharacter->getWorldPosition()._y, 0.05f );
    pCharacter->setMoveVelocity( sw::float3{ 3.0f, 0.0f, 0.0f } );
    tickFor( manager, 60 );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pCharacter->getWorldPosition()._x, 0.2f );
    manager.endPlay();
}

/**
 * @brief [PhysicsComponentTest] 2D 강체 · 2D 캐릭터 컴포넌트도 같은 규칙이다 — 상자는 바닥에 서고 캐릭터는 걷는다
 */
SW_TEST_CASE( PhysicsComponentTest, Components2DLandAndWalk )
{
    sw::GameObjectManager     manager;
    sw::GameObject*           pFloorObject = manager.createGameObject( sw::hashed_string( "Floor2D" ) );
    sw::RigidBody2DComponent* pFloor       = pFloorObject->addComponent<sw::RigidBody2DComponent>();
    SW_ASSERT_NOT_NULL( pFloor );
    sw::PhysicsShapeDesc2D floorShape;
    floorShape._halfExtents = sw::float2{ 20.0f, 0.5f };
    pFloor->setShape( floorShape );
    pFloor->setBodyType( sw::PhysicsBodyType::Static );
    pFloor->setLocalPosition( sw::float3{ 0.0f, -0.5f, 3.0f } );

    sw::GameObject*           pCrateObject = manager.createGameObject( sw::hashed_string( "Crate2D" ) );
    sw::RigidBody2DComponent* pCrate       = pCrateObject->addComponent<sw::RigidBody2DComponent>();
    SW_ASSERT_NOT_NULL( pCrate );
    pCrate->setLocalPosition( sw::float3{ 2.0f, 3.0f, 3.0f } );

    sw::GameObject*                     pHeroObject = manager.createGameObject( sw::hashed_string( "Hero2D" ) );
    sw::CharacterController2DComponent* pHero       = pHeroObject->addComponent<sw::CharacterController2DComponent>();
    SW_ASSERT_NOT_NULL( pHero );
    pHero->setLocalPosition( sw::float3{ -3.0f, 1.0f, 3.0f } );

    manager.beginPlay();
    tickFor( manager, 180 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pCrate->getWorldPosition()._y, 0.03f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pCrate->getWorldPosition()._z, 1e-4f ); // 2D 물리는 Z 를 건드리지 않는다
    SW_EXPECT_TRUE( pHero->isGrounded() );
    pHero->setMoveVelocity( 2.0f );
    tickFor( manager, 60 );
    SW_EXPECT_NEAR_EQUAL( -1.0f, pHero->getWorldPosition()._x, 0.2f );
    manager.endPlay();
}
