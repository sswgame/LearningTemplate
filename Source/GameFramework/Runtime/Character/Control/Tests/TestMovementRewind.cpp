// MovementRewindTest — 네트가 움직이는 캐릭터의 걸음은 (상태, 의도, 세계)만의 함수다: 같은 입력 열이면 같은 상태, 되감아 다시 돌리면 이어 돌린 것과 비트까지 같다.
// 실제 3D 물리(Jolt CharacterVirtual), 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Name/hashed_string.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/CharacterMovementComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

using namespace sw;

namespace
{
    /** @brief 윗면 y 0 인 바닥, 시작 자리의 높이 1 m 발판(앞 끝 z 1.5), 길을 막는 벽(z 6)과 네트가 움직이는 폰 하나입니다. */
    struct TestMovementRewindWorld
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;
        static constexpr float32 kTick  = 1.0f / 30.0f;

        GameObjectManager             _manager{};
        CharacterControllerComponent* _pController{ nullptr };
        CharacterMovementComponent*   _pMovement{ nullptr };

        RigidBodyComponent* addBox( const utf8* pName, const float3& position, const float3& halfExtents, PhysicsBodyType type )
        {
            RigidBodyComponent* pBody = _manager.createGameObject( hashed_string( pName ) )->addComponent<RigidBodyComponent>();
            PhysicsShapeDesc3D  box;
            box._type        = PhysicsShapeType3D::Box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( type );
            pBody->setLocalPosition( position );
            return pBody;
        }

        void build()
        {
            (void)addBox( "Floor", float3{ 0.0f, -0.5f, 0.0f }, float3{ 20.0f, 0.5f, 20.0f }, PhysicsBodyType::Static );
            (void)addBox( "Platform", float3{ 0.0f, 0.5f, 0.0f }, float3{ 1.5f, 0.5f, 1.5f }, PhysicsBodyType::Static );
            (void)addBox( "Wall", float3{ 0.0f, 1.0f, 6.0f }, float3{ 12.0f, 1.0f, 0.25f }, PhysicsBodyType::Static );
            GameObject* pWalker = _manager.createGameObject( hashed_string( "Walker" ) );
            _pController        = pWalker->addComponent<CharacterControllerComponent>();
            _pController->setLocalPosition( float3{ 0.0f, 1.05f, 0.0f } );
            pWalker->addComponent<PawnComponent>()->setAutoPossess( PawnAutoPossess::None );
            _pMovement = pWalker->addComponent<CharacterMovementComponent>();
            _manager.beginPlay();
            _manager.update( kFrame ); // 첫 물리 프레임이 캐릭터 · 바디를 짓는다
            _pMovement->setNetDriven( true );
        }

        CharacterMoveState step( const CharacterMoveState& state, const ControlIntent& intent, CharacterMotionPass pass ) { return _pMovement->simulate( state, intent, kTick, pass ); }

        /** @brief 발판에서 걸어 내려와(떨어짐 · 착지) 벽에 비스듬히 부딪쳐 미끄러지고 돌아서는 의도 열입니다. */
        static vector<ControlIntent> makeIntents()
        {
            vector<ControlIntent> listIntent( 150 );
            for ( size_t tick = 0; tick < listIntent.size(); ++tick )
            {
                ControlIntent& intent = listIntent[tick];
                intent._controlYaw    = tick < 90 ? 0.0f : 2.0f;
                intent._move          = tick < 50 ? float2{ 0.0f, 1.0f } : ( tick < 90 ? float2{ 0.6f, 0.8f } : ( tick < 120 ? float2{ -0.5f, 0.5f } : float2{} ) );
                intent.quantize();
            }
            return listIntent;
        }

        static bool isSame( const CharacterMoveState& left, const CharacterMoveState& right )
        {
            const bool bMotion = left._motion._position == right._motion._position && left._motion._walkVelocity == right._motion._walkVelocity &&
                                 left._motion._fieldVelocity == right._motion._fieldVelocity && left._motion._airVelocity == right._motion._airVelocity &&
                                 left._motion._verticalSpeed == right._motion._verticalSpeed && left._motion._movementMode == right._motion._movementMode &&
                                 left._motion._bGrounded == right._motion._bGrounded;
            return bMotion && left._velocity == right._velocity && left._climbReleaseLeft == right._climbReleaseLeft && left._facingYaw == right._facingYaw && left._bClimbing == right._bClimbing;
        }
    };
} // namespace

/** @brief [MovementRewindTest] 두 월드가 같은 의도 열을 돌리면 틱마다 상태가 비트까지 같다(서버와 클라이언트) */
SW_TEST_CASE( MovementRewindTest, SameInputsGiveTheSameStates )
{
    TestMovementRewindWorld server;
    TestMovementRewindWorld client;
    server.build();
    client.build();
    CharacterMoveState serverState = server._pMovement->getState();
    CharacterMoveState clientState = client._pMovement->getState();
    bool               bFell       = false;
    for ( const ControlIntent& intent : TestMovementRewindWorld::makeIntents() )
    {
        serverState = server.step( serverState, intent, CharacterMotionPass::Live );
        clientState = client.step( clientState, intent, CharacterMotionPass::Live );
        SW_REQUIRE_TRUE( TestMovementRewindWorld::isSame( serverState, clientState ) );
        bFell = bFell || serverState._motion.getMovementMode() == CharacterMovementMode::Falling;
    }
    SW_EXPECT_TRUE( bFell );                                   // 발판에서 떨어지는 길을 지났다
    SW_EXPECT_TRUE( serverState._motion._position._y < 0.2f ); // 바닥에 내려섰다
    SW_EXPECT_TRUE( serverState._motion._position._z < 6.0f ); // 벽을 지나지 않았다
    SW_EXPECT_TRUE( serverState._motion._bGrounded == SW_TRUE );
}

/** @brief [MovementRewindTest] 아무 틱에서나 되감아 남은 입력을 다시 돌리면 이어 돌린 상태와 비트까지 같다 — 앞 이동의 접촉이 남아 있어도 */
SW_TEST_CASE( MovementRewindTest, ReplayEqualsContinuing )
{
    TestMovementRewindWorld world;
    world.build();
    const vector<ControlIntent> listIntent = TestMovementRewindWorld::makeIntents();
    vector<CharacterMoveState>  listState;
    listState.push_back( world._pMovement->getState() );
    for ( const ControlIntent& intent : listIntent )
    {
        listState.push_back( world.step( listState.back(), intent, CharacterMotionPass::Live ) );
    }
    for ( const size_t rewindTick : { size_t{ 0 }, size_t{ 12 }, size_t{ 47 }, size_t{ 88 }, size_t{ 140 } } )
    {
        CharacterMoveState state = listState[rewindTick];
        for ( size_t tick = rewindTick; tick < listIntent.size(); ++tick )
        {
            state = world.step( state, listIntent[tick], CharacterMotionPass::Replay );
            SW_REQUIRE_TRUE( TestMovementRewindWorld::isSame( state, listState[tick + 1u] ) );
        }
    }
}

/** @brief [MovementRewindTest] 다시 돌리는 걸음은 동적 바디를 밀지 않는다 — 보정 한 번이 상자를 여러 번 밀지 않게. 실제 걸음은 민다 */
SW_TEST_CASE( MovementRewindTest, ReplayDoesNotPushTheWorld )
{
    for ( const CharacterMotionPass pass : { CharacterMotionPass::Replay, CharacterMotionPass::Live } )
    {
        TestMovementRewindWorld world;
        world.build();
        RigidBodyComponent* pCrate = world.addBox( "Crate", float3{ 5.0f, 0.5f, 0.0f }, float3{ 0.5f, 0.5f, 0.5f }, PhysicsBodyType::Dynamic );
        pCrate->setMass( 5.0f ); // 캐릭터가 미는 힘(100 N)이 마찰을 이긴다
        for ( int32 frame = 0; frame < 30; ++frame )
        {
            world._manager.update( TestMovementRewindWorld::kFrame ); // 상자가 선다
        }
        const float3       rest  = pCrate->getWorldPosition();
        CharacterMoveState state = world._pMovement->getState();
        state._motion._position  = float3{ 3.9f, 0.02f, 0.0f };
        ControlIntent intent;
        intent._move = float2{ 1.0f, 0.0f };
        for ( int32 tick = 0; tick < 20; ++tick )
        {
            state = world.step( state, intent, pass );
        }
        for ( int32 frame = 0; frame < 30; ++frame )
        {
            world._manager.update( TestMovementRewindWorld::kFrame );
        }
        const float32 moved = ( pCrate->getWorldPosition() - rest ).getLength();
        if ( pass == CharacterMotionPass::Replay )
            SW_EXPECT_TRUE( moved < 5.0e-3f );
        else
            SW_EXPECT_TRUE( moved > 2.0e-2f );
    }
}

/** @brief [MovementRewindTest] 네트가 움직이는 몸은 틱 · 물리 스텝이 옮기지 않고, 되돌린 자리가 그려지는 자리다 */
SW_TEST_CASE( MovementRewindTest, FixedStepsLeaveANetDrivenBodyAlone )
{
    TestMovementRewindWorld world;
    world.build();
    CharacterMoveState state = world._pMovement->getState();
    state._motion._position  = float3{ -5.0f, 3.0f, -5.0f }; // 공중 — 물리 스텝이 돌면 떨어진다
    state._facingYaw         = 1.0f;
    world._pMovement->setState( state );
    world._pController->setMotionDisplay( state._motion._position, 1.0f, float3{ 0.25f, 0.0f, 0.0f } );
    for ( int32 frame = 0; frame < 20; ++frame )
    {
        world._manager.update( TestMovementRewindWorld::kFrame );
    }
    SW_EXPECT_TRUE( world._pMovement->getState()._motion._position == state._motion._position );
    const float3 shown = world._pController->getWorldPosition();
    SW_EXPECT_NEAR( -4.75f, shown._x, 1.0e-4f ); // 시뮬레이션 자리 + 녹이는 오프셋
    SW_EXPECT_NEAR( 3.0f, shown._y, 1.0e-4f );

    // 코드가 옮기면(순간이동) 그 자리가 시뮬레이션 자리가 된다.
    world._pController->teleportTo( float3{ 2.0f, 4.0f, 2.0f } );
    world._manager.update( TestMovementRewindWorld::kFrame );
    SW_EXPECT_NEAR( 2.0f, world._pMovement->getState()._motion._position._x, 1.0e-4f );
    SW_EXPECT_NEAR( 4.0f, world._pMovement->getState()._motion._position._y, 1.0e-4f );
}
