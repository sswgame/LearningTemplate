// ContactStayListenerTest — 접촉 유지(Stay)는 올린 듣는 쪽만 받고, 물리 씬은 듣는 오브젝트가 낀 쌍만 유지를 만든다(감사 S-1). 디바이스 없음(nogpu).
// 언리얼 `bNotifyRigidBodyCollision` · Godot `contact_monitor` 처럼 켠 쪽만 알린다 — 유니티처럼 듣는 쪽이 없으면 만들지 않는다.
#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

using namespace sw;

namespace
{
    struct TestContactStayListenerInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 받은 유지 수입니다 — 듣는 함수에 문맥이 컴포넌트뿐이라 테스트 스레드 값에 센다(`thread_local` 은 숨은 싱글턴 규칙의 허용 칸). */
        static thread_local uint32 s_collisionStayCount;

        static void countCollisionStay( Component& self, const CollisionInfo& collision )
        {
            (void)self;
            (void)collision;
            ++s_collisionStayCount;
        }

        /** @brief @p position 에 반 크기 0.5 상자 강체를 둔 오브젝트를 만듭니다. */
        static RigidBodyComponent* addBox( GameObjectManager& manager, const utf8* pName, const float3& position, PhysicsBodyType type )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._type        = PhysicsShapeType3D::Box;
            box._halfExtents = float3{ 0.5f, 0.5f, 0.5f };
            pBody->setShape( box );
            pBody->setBodyType( type );
            pBody->setLocalPosition( position );
            return pBody;
        }

        /** @brief 이번 프레임 3D 이벤트 가운데 바디 @p body 가 낀 유지 수입니다. */
        static uint32 countStay( const ScenePhysics& physics, PhysicsBodyHandle body )
        {
            uint32 count = 0;
            for ( const PhysicsContactEvent3D& event : physics.getFrameEvents3D() )
            {
                const bool bInvolved = event._bodyA == body || event._bodyB == body;
                count += ( bInvolved && event._phase == PhysicsContactPhase::Stay ) ? 1u : 0u;
            }
            return count;
        }
    };

    thread_local uint32 TestContactStayListenerInternal::s_collisionStayCount = 0;
} // namespace

/**
 * @brief [ContactStayListenerTest] 바닥에 놓인 상자 둘 가운데 듣는 쪽을 올린 상자만 유지를 받고, 듣지 않는 상자의 쌍은 유지 이벤트 자체가 없다 — 내리면 멈춘다
 * @details 두 상자는 서로 멀리 떨어진 정적 바닥 위에 닿게 둔다. 시작 · 끝은 듣기와 상관없이 지금처럼 `Component::onCollision*` 으로 간다.
 */
SW_TEST_CASE( ContactStayListenerTest, OnlyListeningObjectGetsStay )
{
    using Internal                 = TestContactStayListenerInternal;
    Internal::s_collisionStayCount = 0;
    GameObjectManager   manager;
    RigidBodyComponent* pFloorA = Internal::addBox( manager, "FloorA", float3{ 0.0f, 0.0f, 0.0f }, PhysicsBodyType::Static );
    RigidBodyComponent* pFloorB = Internal::addBox( manager, "FloorB", float3{ 10.0f, 0.0f, 0.0f }, PhysicsBodyType::Static );
    RigidBodyComponent* pHeard  = Internal::addBox( manager, "Heard", float3{ 0.0f, 0.99f, 0.0f }, PhysicsBodyType::Dynamic );
    RigidBodyComponent* pQuiet  = Internal::addBox( manager, "Quiet", float3{ 10.0f, 0.99f, 0.0f }, PhysicsBodyType::Dynamic );
    SW_REQUIRE_NOT_NULL( pFloorA );
    SW_REQUIRE_NOT_NULL( pFloorB );
    SW_REQUIRE_NOT_NULL( pHeard );
    SW_REQUIRE_NOT_NULL( pQuiet );
    ScenePhysics& physics = manager.getSubsystem<ScenePhysics>();
    physics.addContactStayListener( *pHeard, ContactStayListener{ &Internal::countCollisionStay, nullptr } ); // 씬을 만들기 전에 올려도 씬이 받는다
    SW_EXPECT_EQUAL( 1u, physics.getContactStayListenerCount( pHeard->getOwner()->getObjectID() ) );
    manager.beginPlay();

    uint32 heardStay = 0;
    uint32 quietStay = 0;
    for ( uint32 frame = 0; frame < 30; ++frame )
    {
        manager.update( Internal::kFrame );
        heardStay += Internal::countStay( physics, pHeard->getBodyHandle() );
        quietStay += Internal::countStay( physics, pQuiet->getBodyHandle() );
    }
    SW_REQUIRE_NOT_NULL( physics.findScene3D() );
    SW_EXPECT_TRUE( heardStay > 0 );
    SW_EXPECT_EQUAL( 0u, quietStay );                             // 듣지 않는 쌍은 유지를 만들지 않는다
    SW_EXPECT_EQUAL( heardStay, Internal::s_collisionStayCount ); // 듣는 쪽은 쌍마다 한 번 받는다(바닥은 듣지 않는다)

    physics.removeContactStayListener( *pHeard );
    SW_EXPECT_EQUAL( 0u, physics.getContactStayListenerCount( pHeard->getOwner()->getObjectID() ) );
    const uint32 receivedBefore = Internal::s_collisionStayCount;
    uint32       staleStay      = 0;
    for ( uint32 frame = 0; frame < 5; ++frame )
    {
        manager.update( Internal::kFrame );
        staleStay += Internal::countStay( physics, pHeard->getBodyHandle() );
    }
    SW_EXPECT_EQUAL( 0u, staleStay );
    SW_EXPECT_EQUAL( receivedBefore, Internal::s_collisionStayCount );
}
