// ClimbMovementTest — 캐릭터 이동기가 오르기 구역에서 매달리고 · 올라 발판에 서고 · 놓지 않으면 머문다. 실제 3D 물리(Jolt) 위, 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Character/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/CharacterMovementComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Traversal/ClimbZoneComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

using namespace sw;

namespace
{
    /** @brief 윗면 y 0 인 바닥, 높이 3 m 발판(앞면 z 0.25), 그 앞의 사다리 구역(z -0.25 ~ 0.25, y 0 ~ 3.2)과 그 앞에 선 폰 하나입니다. */
    struct TestClimbMovementWorld
    {
        static constexpr float32 kFrame        = 1.0f / 60.0f;
        static constexpr float32 kPlatformTop  = 3.0f;
        static constexpr float32 kPlatformFace = 0.25f;

        GameObjectManager               _manager{};
        CharacterControllerComponent*   _pController{ nullptr };
        CharacterMovementComponent*     _pMovement{ nullptr };
        PawnComponent*                  _pPawn{ nullptr };
        IntentTrackControllerComponent* _pTrack{ nullptr };

        RigidBodyComponent* addBox( const utf8* pName, const float3& position, const float3& halfExtents, bool bTrigger )
        {
            RigidBodyComponent* pBody = _manager.createGameObject( hashed_string( pName ) )->addComponent<RigidBodyComponent>();
            PhysicsShapeDesc3D  box;
            box._type        = PhysicsShapeType3D::Box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Static );
            pBody->setLocalPosition( position );
            if ( bTrigger )
            {
                pBody->setTrigger( true );
                pBody->setLayer( hashed_string( "Trigger" ) );
            }
            return pBody;
        }

        void build()
        {
            (void)addBox( "Floor", float3{ 0.0f, -0.5f, 0.0f }, float3{ 20.0f, 0.5f, 20.0f }, false );
            (void)addBox( "Platform", float3{ 0.0f, 0.5f * kPlatformTop, kPlatformFace + 4.0f }, float3{ 4.0f, 0.5f * kPlatformTop, 4.0f }, false );
            GameObject* pLadder = addBox( "Ladder", float3{ 0.0f, 1.6f, 0.0f }, float3{ 0.5f, 1.6f, 0.25f }, true )->getOwner();
            (void)pLadder->addComponent<GimmickSensorComponent>();
            (void)pLadder->addComponent<ClimbZoneComponent>();
            GameObject* pWalker = _manager.createGameObject( hashed_string( "Climber" ) );
            _pController        = pWalker->addComponent<CharacterControllerComponent>();
            _pController->setLocalPosition( float3{ 0.0f, 0.05f, -1.0f } );
            _pPawn = pWalker->addComponent<PawnComponent>();
            _pPawn->setAutoPossess( PawnAutoPossess::None );
            _pMovement = pWalker->addComponent<CharacterMovementComponent>();
            _pTrack    = _manager.createGameObject( hashed_string( "Track" ) )->addComponent<IntentTrackControllerComponent>();
            _manager.beginPlay();
            _manager.update( kFrame ); // 첫 물리 프레임이 캐릭터 · 바디를 짓는다
        }

        /** @brief 의도 (@p moveForward, @p moveUp)를 @p frameCount 프레임 동안 냅니다(조종 요 0 — 앞은 +Z). */
        void drive( float32 moveForward, float32 moveUp, uint32 frameCount )
        {
            ControlIntent intent;
            intent._move   = float2{ 0.0f, moveForward };
            intent._moveUp = moveUp;
            _pTrack->play( *_pPawn, vector<ControlIntent>( frameCount, intent ), false );
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                _manager.update( kFrame );
            }
        }
    };
} // namespace

/** @brief [ClimbMovementTest] 사다리 쪽으로 계속 밀면 올라가 발판 위에 선다 — 이동 방식은 오르는 동안 Climbing, 선 뒤 Walking 이다 */
SW_TEST_CASE( ClimbMovementTest, PushingIntoALadderClimbsOntoThePlatform )
{
    TestClimbMovementWorld world;
    world.build();
    world.drive( 1.0f, 0.0f, 45 ); // 0.75 초 — 구역에 들어 오르는 중이다
    SW_EXPECT_TRUE( world._pMovement->isClimbing() );
    SW_EXPECT_TRUE( world._pController->getMovementMode() == CharacterMovementMode::Climbing );
    SW_EXPECT_TRUE( world._pController->getSimulatedPosition()._y > 0.3f );

    world.drive( 1.0f, 0.0f, 75 );
    const float3 position = world._pController->getSimulatedPosition();
    SW_EXPECT_NEAR( TestClimbMovementWorld::kPlatformTop, position._y, 0.1f );
    SW_EXPECT_TRUE( position._z > TestClimbMovementWorld::kPlatformFace );
    SW_EXPECT_FALSE( world._pMovement->isClimbing() );
    SW_EXPECT_TRUE( world._pController->getMovementMode() == CharacterMovementMode::Walking );
}

/** @brief [ClimbMovementTest] 매달린 채 의도를 놓으면 떨어지지 않고 머물고, 아래를 누르면 내려와 땅에 선다 */
SW_TEST_CASE( ClimbMovementTest, ReleasedIntentHoldsAndDownClimbsBack )
{
    TestClimbMovementWorld world;
    world.build();
    world.drive( 1.0f, 0.0f, 60 );
    SW_REQUIRE_TRUE( world._pMovement->isClimbing() );

    world.drive( 0.0f, 0.0f, 2 ); // 오르던 마지막 스텝을 넘긴다
    const float32 held = world._pController->getSimulatedPosition()._y;
    world.drive( 0.0f, 0.0f, 60 );
    SW_EXPECT_TRUE( world._pMovement->isClimbing() );
    SW_EXPECT_NEAR( held, world._pController->getSimulatedPosition()._y, 1.0e-3f );

    world.drive( 0.0f, -1.0f, 120 );
    SW_EXPECT_FALSE( world._pMovement->isClimbing() );
    SW_EXPECT_TRUE( world._pController->isGrounded() );
    SW_EXPECT_NEAR( 0.0f, world._pController->getSimulatedPosition()._y, 0.1f );
}

/** @brief [ClimbMovementTest] 같은 의도 열은 같은 궤적을 낸다(비트 동일) — 매달림 상태가 의도 밖에서 오지 않는다 */
SW_TEST_CASE( ClimbMovementTest, SameIntentsGiveTheSamePath )
{
    TestClimbMovementWorld first;
    TestClimbMovementWorld second;
    first.build();
    second.build();
    for ( const float32 moveForward : { 1.0f, 0.0f, 1.0f, -1.0f } )
    {
        first.drive( moveForward, 0.25f, 30 );
        second.drive( moveForward, 0.25f, 30 );
        const float3 a = first._pController->getSimulatedPosition();
        const float3 b = second._pController->getSimulatedPosition();
        SW_EXPECT_EQUAL( a._x, b._x );
        SW_EXPECT_EQUAL( a._y, b._y );
        SW_EXPECT_EQUAL( a._z, b._z );
    }
}
