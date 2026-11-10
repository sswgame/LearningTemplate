// 캐릭터 컨트롤러 3D · 2D 의 입력이 `PhysicsPoseStore` 의 칸(`PhysicsCommandPage`)에 든 뒤에도 예전 컴포넌트 멤버 + `_commandLock` 때와 같은 값을 같은 단계에서 꺼낸다.
// 쓰는 쪽: 아무 틱 워커(`setMoveVelocity` · `jump` · `addRootMotionDisplacement` …, 칸의 잠금 아래). 읽는 쪽: 게임 스레드의 물리 프레임(`beginPhysicsFrame` · `prePhysicsStep`).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Component/CharacterController2DComponent.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

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
            SW_REQUIRE_NOT_NULL( pObject );
            CharacterControllerComponent* pController = pObject->addComponent<CharacterControllerComponent>();
            SW_REQUIRE_NOT_NULL( pController );
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
    CharacterControllerComponent* pIdle   = TestCharacterCommandStoreInternal::addCharacter( manager, "Idle", float3{ 0.0f, 50.0f, 0.0f } );
    CharacterControllerComponent* pRoot   = TestCharacterCommandStoreInternal::addCharacter( manager, "Root", float3{ 0.0f, 50.0f, 10.0f } );
    CharacterControllerComponent* pJumper = TestCharacterCommandStoreInternal::addCharacter( manager, "Jumper", float3{ 0.0f, 50.0f, 20.0f } );
    manager.beginPlay();
    manager.update( TestCharacterCommandStoreInternal::kFrame ); // 첫 물리 프레임이 캐릭터를 짓는다

    pRoot->addRootMotionDisplacement( float3{ 1.0f, 5.0f, 0.0f } ); // 수직은 버린다
    pJumper->jump( 6.0f );
    for ( uint32 frame = 0; frame < 20; ++frame )
    {
        manager.update( TestCharacterCommandStoreInternal::kFrame );
    }

    const float3 idle   = pIdle->getSimulatedPosition();
    const float3 root   = pRoot->getSimulatedPosition();
    const float3 jumper = pJumper->getSimulatedPosition();
    SW_EXPECT_NEAR( 1.0f, root._x - idle._x, 1.0e-3f );
    SW_EXPECT_NEAR( idle._y, root._y, 1.0e-4f );
    SW_EXPECT_TRUE( jumper._y > idle._y + 1.0f );
    SW_EXPECT_NEAR( idle._x, jumper._x, 1.0e-4f );
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
