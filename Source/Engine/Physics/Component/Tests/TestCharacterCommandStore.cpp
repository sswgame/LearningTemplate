// 캐릭터 컨트롤러 3D · 2D 의 입력이 `PhysicsPoseStore` 의 칸(`PhysicsCommandPage`)에 든 뒤에도 예전 컴포넌트 멤버 + `_commandLock` 때와 같은 값을 같은 단계에서 꺼낸다.
// 쓰는 쪽: 아무 틱 워커(`setMoveVelocity` · `jump` · `addRootMotionDisplacement` …, 칸의 잠금 아래). 읽는 쪽: 게임 스레드의 물리 프레임(`beginPhysicsFrame` · `prePhysicsStep`).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Component/CharacterController2DComponent.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

using namespace sw;

namespace
{
    struct TestCharacterCommandStoreInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief @p position 에 3D 캐릭터 컨트롤러 하나를 든 오브젝트를 만듭니다(바닥 없음 — 떨어지며 수평 입력만 본다). */
        static CharacterControllerComponent* addCharacter( GameObjectManager& manager, const utf8* pName, const float3& position )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            SW_EXPECT_NOT_NULL( pObject ); // REQUIRE 는 void 를 돌려주며 끝나 포인터를 돌려주는 이 함수에서 쓸 수 없다
            CharacterControllerComponent* pController = pObject != nullptr ? pObject->addComponent<CharacterControllerComponent>() : nullptr;
            SW_EXPECT_NOT_NULL( pController );
            if ( pController != nullptr )
                pController->setLocalPosition( position );
            return pController;
        }
    };
} // namespace

/**
 * @brief [CharacterCommandStoreTest] 입력은 컨트롤러마다 따로다 — 한 칸에 쓴 요청이 다른 칸에 보이지 않고, 수직 성분은 예전처럼 버린다
 */
SW_TEST_CASE( CharacterCommandStoreTest, MoveRequestStaysInItsOwnSlot )
{
    GameObjectManager             manager;
    CharacterControllerComponent* pFirst  = TestCharacterCommandStoreInternal::addCharacter( manager, "First", float3{ 0.0f, 10.0f, 0.0f } );
    CharacterControllerComponent* pSecond = TestCharacterCommandStoreInternal::addCharacter( manager, "Second", float3{ 0.0f, 10.0f, 20.0f } );
    pFirst->setMoveVelocity( float3{ 2.0f, 7.0f, -1.0f } );

    const float3 first  = pFirst->getRequestedMoveVelocity();
    const float3 second = pSecond->getRequestedMoveVelocity();
    SW_EXPECT_EQUAL( 2.0f, first._x );
    SW_EXPECT_EQUAL( 0.0f, first._y );
    SW_EXPECT_EQUAL( -1.0f, first._z );
    SW_EXPECT_EQUAL( 0.0f, second._x );
    SW_EXPECT_EQUAL( 0.0f, second._z );

    // 다시 지어도(속성 변경) 칸은 컴포넌트의 수명이라 요청이 남는다.
    pFirst->setRadius( 0.4f );
    SW_EXPECT_EQUAL( 2.0f, pFirst->getRequestedMoveVelocity()._x );
}

/**
 * @brief [CharacterCommandStoreTest] 놓은 칸을 다음 컨트롤러가 받으면 기본값으로 시작한다(앞 주인의 요청이 새지 않는다)
 */
SW_TEST_CASE( CharacterCommandStoreTest, ReusedSlotStartsEmpty )
{
    GameObjectManager             manager;
    CharacterControllerComponent* pOld = TestCharacterCommandStoreInternal::addCharacter( manager, "Old", float3{} );
    pOld->setMoveVelocity( float3{ 3.0f, 0.0f, 4.0f } );
    GameObject* pOwner = pOld->getOwner();
    SW_REQUIRE_TRUE( pOwner->removeComponent( pOld ) );

    CharacterControllerComponent* pNew = pOwner->addComponent<CharacterControllerComponent>();
    SW_REQUIRE_NOT_NULL( pNew );
    const float3 requested = pNew->getRequestedMoveVelocity();
    SW_EXPECT_EQUAL( 0.0f, requested._x );
    SW_EXPECT_EQUAL( 0.0f, requested._z );
}

/**
 * @brief [CharacterCommandStoreTest] 대기 입력은 예전 단계에서 한 번만 꺼낸다 — 루트 모션은 한 물리 프레임에 다 가고(두 번 가지 않는다), 점프는 다음 스텝에 든다
 * @details 같은 높이에서 떨어지는 셋을 나란히 두고 하나에는 루트 모션 1 m, 하나에는 점프를 준다. 바닥이 없어 막는 것이 없으므로 차이가 그 입력만큼이다.
 */
SW_TEST_CASE( CharacterCommandStoreTest, PendingInputIsConsumedOnceByThePhysicsStep )
{
    GameObjectManager             manager;
    CharacterControllerComponent* pIdle          = TestCharacterCommandStoreInternal::addCharacter( manager, "Idle", float3{ 0.0f, 50.0f, 0.0f } );
    CharacterControllerComponent* pRoot          = TestCharacterCommandStoreInternal::addCharacter( manager, "Root", float3{ 0.0f, 50.0f, 10.0f } );
    CharacterControllerComponent* pJumpCharacter = TestCharacterCommandStoreInternal::addCharacter( manager, "Jumper", float3{ 0.0f, 50.0f, 20.0f } );
    manager.beginPlay();
    manager.update( TestCharacterCommandStoreInternal::kFrame ); // 첫 물리 프레임이 캐릭터를 짓는다

    pRoot->addRootMotionDisplacement( float3{ 1.0f, 5.0f, 0.0f } ); // 수직은 버린다
    pJumpCharacter->jump( 6.0f );
    for ( uint32 frame = 0; frame < 20; ++frame )
    {
        manager.update( TestCharacterCommandStoreInternal::kFrame );
    }

    const float3 idle           = pIdle->getSimulatedPosition();
    const float3 root           = pRoot->getSimulatedPosition();
    const float3 jumpedPosition = pJumpCharacter->getSimulatedPosition();
    SW_EXPECT_NEAR( 1.0f, root._x - idle._x, 1.0e-3f );
    SW_EXPECT_NEAR( idle._y, root._y, 1.0e-4f );
    SW_EXPECT_TRUE( jumpedPosition._y > idle._y + 1.0f );
    SW_EXPECT_NEAR( idle._x, jumpedPosition._x, 1.0e-4f );
}

/**
 * @brief [CharacterCommandStoreTest] 2D 컨트롤러도 칸이 따로이고 요청이 남는다(`CharacterCommand2D`)
 */
SW_TEST_CASE( CharacterCommandStoreTest, MoveRequest2DStaysInItsOwnSlot )
{
    GameObjectManager manager;
    GameObject*       pFirstObject  = manager.createGameObject( hashed_string( "First2D" ) );
    GameObject*       pSecondObject = manager.createGameObject( hashed_string( "Second2D" ) );
    SW_REQUIRE_NOT_NULL( pFirstObject );
    SW_REQUIRE_NOT_NULL( pSecondObject );
    CharacterController2DComponent* pFirst  = pFirstObject->addComponent<CharacterController2DComponent>();
    CharacterController2DComponent* pSecond = pSecondObject->addComponent<CharacterController2DComponent>();
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_NOT_NULL( pSecond );

    pFirst->setMoveVelocity( -1.5f );
    pFirst->jump( 4.0f );
    SW_EXPECT_EQUAL( -1.5f, pFirst->getRequestedMoveVelocity() );
    SW_EXPECT_EQUAL( 0.0f, pSecond->getRequestedMoveVelocity() );
    pFirst->setRadius( 0.5f );
    SW_EXPECT_EQUAL( -1.5f, pFirst->getRequestedMoveVelocity() );
}

/**
 * @brief [CharacterCommandStoreTest] `TeleportType::ResetPhysics` 순간이동은 "그 자리에 멈춰 놓기" 다 — 떨어지던 속도 · 발사의 공중 관성 · 강체 회전을 잇지 않는다(개발 명령 `object.place`)
 * @details 바닥 없이 캐릭터는 옆으로 쏘고 강체는 돌려 20 프레임 떨어뜨린 뒤(수직 약 -3 m/s, 수평 5 m/s) 위로 옮긴다. 다음 프레임 속도는 중력 한 프레임어치뿐이다.
 */
SW_TEST_CASE( CharacterCommandStoreTest, ResetPhysicsTeleportDropsInheritedMotion )
{
    GameObjectManager             manager;
    CharacterControllerComponent* pCharacter = TestCharacterCommandStoreInternal::addCharacter( manager, "Placed", float3{ 0.0f, 50.0f, 0.0f } );
    GameObject*                   pCrate     = manager.createGameObject( hashed_string( "PlacedCrate" ) );
    SW_REQUIRE_NOT_NULL( pCrate );
    RigidBodyComponent* pBody = pCrate->addComponent<RigidBodyComponent>();
    SW_REQUIRE_NOT_NULL( pBody );
    PhysicsShapeDesc3D box;
    box._type        = PhysicsShapeType3D::Box;
    box._halfExtents = float3{ 0.5f, 0.5f, 0.5f };
    pBody->setShape( box );
    pBody->setBodyType( PhysicsBodyType::Dynamic );
    pBody->setLocalPosition( float3{ 0.0f, 50.0f, 20.0f } );
    manager.beginPlay();
    manager.update( TestCharacterCommandStoreInternal::kFrame ); // 첫 물리 프레임이 캐릭터 · 바디를 짓는다

    pCharacter->launch( float3{ 5.0f, 0.0f, 0.0f } );
    pBody->setAngularVelocity( float3{ 0.0f, 3.0f, 0.0f } );
    for ( uint32 frame = 0; frame < 20; ++frame )
    {
        manager.update( TestCharacterCommandStoreInternal::kFrame );
    }
    SW_REQUIRE_TRUE( pCharacter->getVelocity()._y < -2.0f );
    SW_REQUIRE_TRUE( pCharacter->getVelocity()._x > 4.0f );
    SW_REQUIRE_TRUE( pBody->getLinearVelocity()._y < -2.0f );

    pCharacter->teleportTo( float3{ 0.0f, 60.0f, 0.0f }, TeleportType::ResetPhysics );
    pBody->teleportTo( float3{ 0.0f, 60.0f, 20.0f }, TeleportType::ResetPhysics );
    manager.update( TestCharacterCommandStoreInternal::kFrame );

    const float32 oneFrameOfGravity = 9.81f * TestCharacterCommandStoreInternal::kFrame * 2.0f; // 한 프레임 스텝 수 여유
    const float3  character         = pCharacter->getVelocity();
    SW_EXPECT_NEAR( 0.0f, character._x, 1.0e-4f );
    SW_EXPECT_NEAR( 0.0f, character._z, 1.0e-4f );
    SW_EXPECT_TRUE( character._y > -oneFrameOfGravity );
    SW_EXPECT_TRUE( pCharacter->getSimulatedPosition()._y > 59.0f );
    SW_EXPECT_TRUE( pBody->getLinearVelocity()._y > -oneFrameOfGravity );
    SW_EXPECT_NEAR( 0.0f, pBody->getAngularVelocity().getLength(), 1.0e-3f );
}
