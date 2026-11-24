// RiderSuspensionTest — 걷기를 멈추는 까닭(좌석 · 래그돌)은 저마다 자기 것만 놓고, 멈춘 동안의 충격은 쌓이지 않으며, 운동을 지우는 순간이동은 이동기의 속도도 지운다.
// 실제 3D 물리(캐릭터 컨트롤러), 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "GameFramework/Runtime/Character/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/CharacterMovementComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

using namespace sw;

namespace
{
    constexpr float32 kRiderFrame = 1.0f / 60.0f;

    /** @brief 캐릭터 컨트롤러 · 이동기 · 래그돌을 단 탑승자와 좌석 하나입니다. */
    struct RiderSuspensionRig
    {
        GameObjectManager             _manager{};
        PawnComponent*                _pRiderPawn{ nullptr };
        CharacterControllerComponent* _pController{ nullptr };
        CharacterMovementComponent*   _pMovement{ nullptr };
        RagdollComponent*             _pRagdoll{ nullptr };
        VehicleSeatComponent*         _pSeat{ nullptr };

        RiderSuspensionRig()
        {
            GameObject* pRider = _manager.createGameObject( hashed_string( "Rider" ) );
            _pController       = pRider->addComponent<CharacterControllerComponent>();
            _pRiderPawn        = pRider->addComponent<PawnComponent>();
            _pRiderPawn->setAutoPossess( PawnAutoPossess::None );
            _pMovement           = pRider->addComponent<CharacterMovementComponent>();
            _pRagdoll            = pRider->addComponent<RagdollComponent>();
            GameObject* pVehicle = _manager.createGameObject( hashed_string( "Cart" ) );
            (void)pVehicle->addComponent<SceneComponent>();
            _pSeat = pVehicle->addComponent<VehicleSeatComponent>();
            _pSeat->setDriverSeat( false );
        }
    };
} // namespace

/** @brief [RiderSuspensionTest] 결함: 앉은 채 쓰러졌다 일어나면(래그돌이 캡슐을 켰다) 좌석의 멈춤이 풀리고, 쓰러진 채 내리면 시체의 캡슐이 켜졌다 — 까닭마다 자기 것만 놓는다 */
SW_TEST_CASE( RiderSuspensionTest, SeatAndRagdollReleaseOnlyTheirOwn )
{
    SW_EXPECT_LOG( "Ragdoll.RagdollNoSkeletalMeshComponentObject" );
    RiderSuspensionRig rig;
    rig._manager.beginPlay();
    SW_REQUIRE_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::Mounted );
    SW_EXPECT_TRUE( rig._pController->isSuspended() );
    rig._pRagdoll->startRagdoll( float3{}, nullopt );
    SW_EXPECT_TRUE( rig._pRagdoll->resetToSpawnState() == SpawnStateReset::Reset ); // 일어났다
    SW_EXPECT_TRUE( rig._pController->isSuspended() );                              // 아직 앉아 있다

    rig._pRagdoll->startRagdoll( float3{}, nullopt );
    SW_REQUIRE_TRUE( VehicleSeatComponent::dismount( *rig._pRiderPawn, true ) );
    SW_EXPECT_TRUE( rig._pController->isSuspended() ); // 쓰러져 내렸다
    (void)rig._pRagdoll->resetToSpawnState();
    SW_EXPECT_FALSE( rig._pController->isSuspended() );
    SW_EXPECT_TRUE( rig._pController->isActive() ); // 활성은 아무도 건드리지 않았다
    rig._manager.endPlay();
}

/** @brief [RiderSuspensionTest] 결함: 멈춘 동안 받은 발사 · 루트 모션이 쌓였다가 다시 걷는 첫 걸음에 몰아 나갔다 — 버린다 */
SW_TEST_CASE( RiderSuspensionTest, ImpulsesWhileSuspendedAreDropped )
{
    SW_EXPECT_LOG( "Ragdoll.RagdollNoSkeletalMeshComponentObject" );
    RiderSuspensionRig rig;
    rig._manager.beginPlay();
    rig._manager.update( kRiderFrame );
    {
        const Subscription suspension = rig._pController->suspend();
        for ( int32 frame = 0; frame < 30; ++frame )
        {
            rig._pController->launch( float3{ 0.0f, 20.0f, 0.0f } );
            rig._pController->addRootMotionDisplacement( float3{ 0.5f, 0.0f, 0.0f } );
            rig._manager.update( kRiderFrame );
        }
    }
    const float3 before = rig._pController->getWorldPosition();
    for ( int32 frame = 0; frame < 4; ++frame )
    {
        rig._manager.update( kRiderFrame );
    }
    const float3 after = rig._pController->getWorldPosition();
    SW_EXPECT_NEAR( before._x, after._x, 1.0e-3f );
    SW_EXPECT_TRUE( after._y <= before._y + 1.0e-3f ); // 튀어 오르지 않고 떨어지기 시작한다
    rig._manager.endPlay();
}

/** @brief [RiderSuspensionTest] 결함: 운동을 지우는 순간이동(리스폰 · 문) 뒤에도 이동기의 걷는 속도가 남아 새 자리에서 미끄러져 나갔다. 속도를 잇는 순간이동은 그대로다 */
SW_TEST_CASE( RiderSuspensionTest, MotionResettingTeleportClearsTheWalkVelocity )
{
    SW_EXPECT_LOG( "Ragdoll.RagdollNoSkeletalMeshComponentObject" );
    RiderSuspensionRig rig;
    rig._manager.beginPlay();
    rig._manager.update( kRiderFrame );
    CharacterMoveState state = rig._pMovement->getState();
    state._velocity          = float3{ 7.0f, 0.0f, 0.0f };
    rig._pMovement->setState( state );
    rig._pController->teleportTo( float3{ 10.0f, 0.0f, 0.0f } );
    rig._manager.update( kRiderFrame );
    SW_EXPECT_TRUE( rig._pMovement->getHorizontalVelocity()._x > 4.0f ); // 이어 간다(감속만 한다)
    rig._pController->teleportTo( float3{ 20.0f, 0.0f, 0.0f }, TeleportType::ResetPhysics );
    rig._manager.update( kRiderFrame );
    SW_EXPECT_NEAR( 0.0f, rig._pMovement->getHorizontalVelocity()._x, 1.0e-4f );
    rig._manager.endPlay();
}
