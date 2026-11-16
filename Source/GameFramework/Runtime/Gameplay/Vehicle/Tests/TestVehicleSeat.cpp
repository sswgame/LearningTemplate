#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/DismountOnDeathComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/MountMovementComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

using namespace sw;

namespace
{
    /** @brief 조종자가 쥔 탑승자와 폰이 있는 탈것입니다. 물리 씬은 없습니다(빈 자리로 봅니다). */
    struct VehicleFixture
    {
        GameObjectManager               _manager;
        GameObject*                     _pRider{ nullptr };
        PawnComponent*                  _pRiderPawn{ nullptr };
        SceneComponent*                 _pRiderRoot{ nullptr };
        GameObject*                     _pVehicle{ nullptr };
        PawnComponent*                  _pVehiclePawn{ nullptr };
        SceneComponent*                 _pVehicleRoot{ nullptr };
        VehicleSeatComponent*           _pSeat{ nullptr };
        IntentTrackControllerComponent* _pController{ nullptr };

        VehicleFixture()
        {
            _pRider       = _manager.createGameObject( hashed_string( "Rider" ) );
            _pRiderRoot   = _pRider->addComponent<SceneComponent>();
            _pRiderPawn   = _pRider->addComponent<PawnComponent>();
            _pVehicle     = _manager.createGameObject( hashed_string( "Car" ) );
            _pVehicleRoot = _pVehicle->addComponent<SceneComponent>();
            _pVehiclePawn = _pVehicle->addComponent<PawnComponent>();
            _pSeat        = _pVehicle->addComponent<VehicleSeatComponent>();
            _pController  = _manager.createGameObject( hashed_string( "Controller" ) )->addComponent<IntentTrackControllerComponent>();
            _manager.beginPlay();
            _pController->possess( *_pRiderPawn );
        }

        void tick() { _manager.update( 1.0f / 60.0f ); }
    };
} // namespace

SW_TEST_CASE( VehicleSeatTest, DriverTakesTheVehicleAndGivesItBack )
{
    VehicleFixture rig;
    SW_EXPECT_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::Mounted );
    SW_EXPECT_FALSE( rig._pSeat->isFree() );
    SW_EXPECT_TRUE( rig._pSeat->getOccupant() == rig._pRider->getHandle() );
    SW_EXPECT_TRUE( VehicleSeatComponent::findSeatOf( *rig._pRiderPawn ) == rig._pSeat );
    SW_EXPECT_TRUE( rig._pVehiclePawn->getController() == rig._pController->getHandle() );
    SW_EXPECT_FALSE( rig._pRiderPawn->isPossessed() );
    SW_EXPECT_TRUE( rig._pRider->getComponent<DismountOnDeathComponent>() != nullptr );

    SW_EXPECT_TRUE( VehicleSeatComponent::dismount( *rig._pRiderPawn, false ) );
    SW_EXPECT_TRUE( rig._pSeat->isFree() );
    SW_EXPECT_TRUE( VehicleSeatComponent::findSeatOf( *rig._pRiderPawn ) == nullptr );
    SW_EXPECT_TRUE( rig._pRiderPawn->getController() == rig._pController->getHandle() );
    SW_EXPECT_FALSE( rig._pVehiclePawn->isPossessed() );
    SW_EXPECT_FALSE( VehicleSeatComponent::dismount( *rig._pRiderPawn, false ) ); // 앉아 있지 않다
}

SW_TEST_CASE( VehicleSeatTest, RiderSitsOnTheSeatOffsetAndFollowsTheVehicle )
{
    VehicleFixture rig;
    rig._pSeat->setSeatOffset( float3{ 0.0f, 2.0f, 0.0f } );
    SW_REQUIRE_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::Mounted );
    SW_EXPECT_NEAR( 2.0f, rig._pRiderRoot->getWorldPosition()._y, 1e-4f );
    rig._pVehicleRoot->setLocalPosition( float3{ 1.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR( 1.0f, rig._pRiderRoot->getWorldPosition()._x, 1e-4f );
}

SW_TEST_CASE( VehicleSeatTest, RiderLandsOnTheExitSpotAndUpright )
{
    VehicleFixture rig;
    rig._pSeat->setExitOffset( float3{ -2.0f, 0.0f, 0.0f } );
    rig._pVehicleRoot->setLocalPosition( float3{ 1.0f, 0.0f, 0.0f } ); // 타는 거리 안이다
    SW_REQUIRE_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::Mounted );
    rig._pVehicleRoot->setLocalPosition( float3{ 10.0f, 0.0f, 0.0f } );
    SW_REQUIRE_TRUE( VehicleSeatComponent::dismount( *rig._pRiderPawn, false ) );
    SW_EXPECT_NEAR( 8.0f, rig._pRiderRoot->getWorldPosition()._x, 1e-3f );
    const float3 rotation = rig._pRiderRoot->getLocalRotation();
    SW_EXPECT_NEAR( 0.0f, rotation._x, 1e-4f );
    SW_EXPECT_NEAR( 0.0f, rotation._z, 1e-4f );
}

// 강제 하차(쓰러짐)는 지금 자리에서 떨어뜨린다.
SW_TEST_CASE( VehicleSeatTest, ForcedDismountStaysWhereTheRiderIs )
{
    VehicleFixture rig;
    rig._pVehicleRoot->setLocalPosition( float3{ 1.0f, 0.0f, 0.0f } );
    SW_REQUIRE_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::Mounted );
    rig._pVehicleRoot->setLocalPosition( float3{ 20.0f, 0.0f, 0.0f } );
    SW_REQUIRE_TRUE( VehicleSeatComponent::dismount( *rig._pRiderPawn, true ) );
    SW_EXPECT_NEAR( 20.0f, rig._pRiderRoot->getWorldPosition()._x, 1e-3f );
    SW_EXPECT_TRUE( rig._pSeat->isFree() );
}

SW_TEST_CASE( VehicleSeatTest, RefusalsNameTheReasonAndChangeNothing )
{
    VehicleFixture rig;
    rig._pVehicleRoot->setLocalPosition( float3{ 100.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::TooFar );
    SW_EXPECT_TRUE( rig._pSeat->isFree() );
    rig._pVehicleRoot->setLocalPosition( float3{} );

    rig._pController->unpossess(); // 넘겨줄 빙의가 없다
    SW_EXPECT_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::NotPossessed );
    SW_EXPECT_TRUE( rig._pSeat->isFree() );
    rig._pController->possess( *rig._pRiderPawn );

    rig._pSeat->setSocketName( hashed_string( "NoSuchSocket" ) ); // 외형이 없어 소켓을 못 찾는다
    SW_EXPECT_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::NoSocket );
    SW_EXPECT_TRUE( rig._pSeat->isFree() );
    rig._pSeat->setSocketName( hashed_string{} );

    SW_REQUIRE_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::Mounted );
    SW_EXPECT_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::SeatTaken );
}

SW_TEST_CASE( VehicleSeatTest, DriverSeatNeedsAVehiclePawn )
{
    GameObjectManager manager;
    GameObject*       pRider = manager.createGameObject( hashed_string( "Rider" ) );
    (void)pRider->addComponent<SceneComponent>();
    PawnComponent* pRiderPawn = pRider->addComponent<PawnComponent>();
    GameObject*    pCart      = manager.createGameObject( hashed_string( "Cart" ) );
    (void)pCart->addComponent<SceneComponent>();
    VehicleSeatComponent* pSeat = pCart->addComponent<VehicleSeatComponent>();
    SW_EXPECT_TRUE( VehicleSeatComponent::mount( *pRiderPawn, *pSeat ) == MountResult::NoVehiclePawn );
    pSeat->setDriverSeat( false );
    SW_EXPECT_TRUE( VehicleSeatComponent::mount( *pRiderPawn, *pSeat ) == MountResult::Mounted ); // 승객석은 폰이 없어도 된다
}

// 승객은 빙의를 옮기지 않는다.
SW_TEST_CASE( VehicleSeatTest, PassengerKeepsItsOwnController )
{
    VehicleFixture rig;
    rig._pSeat->setDriverSeat( false );
    SW_REQUIRE_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::Mounted );
    SW_EXPECT_TRUE( rig._pRiderPawn->getController() == rig._pController->getHandle() );
    SW_EXPECT_FALSE( rig._pVehiclePawn->isPossessed() );
    SW_REQUIRE_TRUE( VehicleSeatComponent::dismount( *rig._pRiderPawn, false ) );
    SW_EXPECT_TRUE( rig._pRiderPawn->getController() == rig._pController->getHandle() );
}

// 주인을 따라오던 말 AI 처럼 탈것을 쥐고 있던 조종자는 내리면 다시 쥔다.
SW_TEST_CASE( VehicleSeatTest, PreviousVehicleControllerGetsItBack )
{
    VehicleFixture                  rig;
    IntentTrackControllerComponent* pHorseAI = rig._manager.createGameObject( hashed_string( "HorseAI" ) )->addComponent<IntentTrackControllerComponent>();
    pHorseAI->possess( *rig._pVehiclePawn );
    SW_REQUIRE_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::Mounted );
    SW_EXPECT_TRUE( rig._pVehiclePawn->getController() == rig._pController->getHandle() );
    SW_EXPECT_TRUE( pHorseAI->findPawn() == nullptr );
    SW_REQUIRE_TRUE( VehicleSeatComponent::dismount( *rig._pRiderPawn, false ) );
    SW_EXPECT_TRUE( rig._pVehiclePawn->getController() == pHorseAI->getHandle() );
    SW_EXPECT_TRUE( rig._pRiderPawn->getController() == rig._pController->getHandle() );
}

SW_TEST_CASE( VehicleSeatTest, NearestFreeSeatPrefersTheDriverThenTheClosest )
{
    VehicleFixture rig;
    rig._pSeat->setDriverSeat( false );
    rig._pVehicleRoot->setLocalPosition( float3{ 1.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( VehicleSeatComponent::findNearestFreeSeat( *rig._pRiderPawn ) == rig._pSeat );

    GameObject* pFar = rig._manager.createGameObject( hashed_string( "FarBus" ) );
    pFar->addComponent<SceneComponent>()->setLocalPosition( float3{ 2.0f, 0.0f, 0.0f } );
    VehicleSeatComponent* pDriver = pFar->addComponent<VehicleSeatComponent>();
    SW_EXPECT_TRUE( VehicleSeatComponent::findNearestFreeSeat( *rig._pRiderPawn ) == pDriver ); // 멀어도 운전석이 먼저다

    pDriver->setDriverSeat( false );
    SW_EXPECT_TRUE( VehicleSeatComponent::findNearestFreeSeat( *rig._pRiderPawn ) == rig._pSeat ); // 같은 종류면 가까운 쪽

    pFar->getPrimarySceneComponent()->setLocalPosition( float3{ 100.0f, 0.0f, 0.0f } );
    rig._pVehicleRoot->setLocalPosition( float3{ 100.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( VehicleSeatComponent::findNearestFreeSeat( *rig._pRiderPawn ) == nullptr ); // 타는 거리 밖
}

// 탈것이 사라지면 앉은 쪽은 지금 자리에서 내려 다시 자기 폰을 쥔다.
SW_TEST_CASE( VehicleSeatTest, RiderSurvivesTheVehicle )
{
    VehicleFixture rig;
    SW_REQUIRE_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::Mounted );
    rig._pVehicleRoot->setLocalPosition( float3{ 2.0f, 0.0f, 0.0f } );
    const GameObjectHandle rider = rig._pRider->getHandle();
    rig._manager.destroyGameObject( rig._pVehicle );
    rig.tick();
    rig.tick();
    SW_REQUIRE_TRUE( rig._manager.resolveGameObject( rider ) == rig._pRider ); // 탑승자는 탈것의 일부가 아니다
    SW_EXPECT_NEAR( 2.0f, rig._pRiderRoot->getWorldPosition()._x, 1e-3f );     // 지금 자리에 남는다
    SW_EXPECT_TRUE( rig._pRiderRoot->getParent() == nullptr );
    SW_EXPECT_TRUE( VehicleSeatComponent::findSeatOf( *rig._pRiderPawn ) == nullptr );
    SW_EXPECT_TRUE( rig._pRiderPawn->getController() == rig._pController->getHandle() );
}

// 내린 탑승자는 다시 평범한 자식이다. 표시가 남으면 다음에 붙은 부모가 사라져도 살아남는다.
SW_TEST_CASE( VehicleSeatTest, DismountClearsTheKeptMark )
{
    VehicleFixture rig;
    SW_REQUIRE_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::Mounted );
    SW_EXPECT_TRUE( rig._pRider->isKeptWhenParentDestroyed() );
    SW_REQUIRE_TRUE( VehicleSeatComponent::dismount( *rig._pRiderPawn, false ) );
    SW_EXPECT_FALSE( rig._pRider->isKeptWhenParentDestroyed() );
}

// 탑승자가 사라지면 좌석은 빈다. 끊긴 핸들을 든 채 "사람이 있다" 고 하면 그 탈것은 다시 탈 수 없다.
SW_TEST_CASE( VehicleSeatTest, SeatFreesWhenTheRiderIsGone )
{
    VehicleFixture                  rig;
    IntentTrackControllerComponent* pHorseAI = rig._manager.createGameObject( hashed_string( "HorseAI" ) )->addComponent<IntentTrackControllerComponent>();
    pHorseAI->possess( *rig._pVehiclePawn );
    SW_REQUIRE_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::Mounted );
    rig._manager.destroyGameObject( rig._pRider );
    rig.tick();
    rig.tick();
    SW_EXPECT_TRUE( rig._pSeat->isFree() );
    SW_EXPECT_TRUE( rig._pVehiclePawn->getController() == pHorseAI->getHandle() ); // 사라진 탑승자의 조종자가 탈것을 들고 있지 않는다

    GameObject* pNext = rig._manager.createGameObject( hashed_string( "NextRider" ) );
    (void)pNext->addComponent<SceneComponent>();
    PawnComponent* pNextPawn = pNext->addComponent<PawnComponent>();
    rig._pController->possess( *pNextPawn );
    SW_EXPECT_TRUE( VehicleSeatComponent::mount( *pNextPawn, *rig._pSeat ) == MountResult::Mounted );
}

// ---------------------------------------------------------------------- 걸음새

SW_TEST_CASE( MountMovementTest, InputAmountPicksTheGait )
{
    SW_EXPECT_TRUE( MountMovementComponent::computeRequestedGait( 0.0f, false ) == MountGait::Idle );
    SW_EXPECT_TRUE( MountMovementComponent::computeRequestedGait( 0.05f, true ) == MountGait::Idle ); // 스틱 떨림
    SW_EXPECT_TRUE( MountMovementComponent::computeRequestedGait( 0.3f, false ) == MountGait::Walk );
    SW_EXPECT_TRUE( MountMovementComponent::computeRequestedGait( 0.3f, true ) == MountGait::Walk ); // 느리면 질주하지 않는다
    SW_EXPECT_TRUE( MountMovementComponent::computeRequestedGait( 0.7f, false ) == MountGait::Trot );
    SW_EXPECT_TRUE( MountMovementComponent::computeRequestedGait( 1.0f, false ) == MountGait::Canter );
    SW_EXPECT_TRUE( MountMovementComponent::computeRequestedGait( 0.7f, true ) == MountGait::Gallop );
}

/** @brief [VehicleSeatTest] 탑승 가속감 곡선: 가속도를 거슬러 기운다(급정거 → 앞, 가속 → 뒤, 오른쪽 가속 → 왼쪽), 최대 기울기에서 자른다 */
SW_TEST_CASE( VehicleSeatTest, RideFeelLeansAgainstTheAcceleration )
{
    RideFeelSettings feel;
    feel._leanPerAcceleration = 0.02f;
    feel._maxLean             = 0.3f;
    const float2 braking      = feel.computeLean( float3{ 0.0f, 0.0f, -10.0f } );
    SW_EXPECT_NEAR( 0.2f, braking._x, 1e-5f ); // 앞으로 숙인다
    SW_EXPECT_NEAR( 0.0f, braking._y, 1e-5f );
    SW_EXPECT_NEAR( -0.2f, feel.computeLean( float3{ 0.0f, 0.0f, 10.0f } )._x, 1e-5f );  // 가속은 뒤로
    SW_EXPECT_NEAR( 0.2f, feel.computeLean( float3{ 10.0f, 0.0f, 0.0f } )._y, 1e-5f );   // 오른쪽으로 돌면 왼쪽으로
    SW_EXPECT_NEAR( 0.3f, feel.computeLean( float3{ 0.0f, 0.0f, -100.0f } )._x, 1e-5f ); // 자른다
}

/** @brief [VehicleSeatTest] 좌석이 탈것의 급정거를 재어(앞 가속 −) 탑승자를 앞으로 기울이고, 내리면 잰 값을 비운다 */
SW_TEST_CASE( VehicleSeatTest, SeatMeasuresBrakingAndLeansTheRiderForward )
{
    VehicleFixture rig;
    SW_REQUIRE_TRUE( VehicleSeatComponent::mount( *rig._pRiderPawn, *rig._pSeat ) == MountResult::Mounted );
    constexpr float32 kStep    = 1.0f / 60.0f;
    float32           position = 0.0f;
    float32           speed    = 10.0f;
    for ( int32 stepIndex = 0; stepIndex < 30; ++stepIndex ) // 10 m/s 로 +Z 를 달린다
    {
        position += speed * kStep;
        rig._pVehicleRoot->setLocalPosition( float3{ 0.0f, 0.0f, position } );
        rig.tick();
    }
    SW_EXPECT_NEAR( 0.0f, rig._pSeat->getRideAcceleration()._z, 0.5f );
    for ( int32 stepIndex = 0; stepIndex < 24; ++stepIndex ) // −20 m/s² 로 선다
    {
        speed = MathUtil::max( 0.0f, speed - 20.0f * kStep );
        position += speed * kStep;
        rig._pVehicleRoot->setLocalPosition( float3{ 0.0f, 0.0f, position } );
        rig.tick();
    }
    SW_EXPECT_TRUE( rig._pSeat->getRideAcceleration()._z < -15.0f );
    SW_EXPECT_TRUE( rig._pSeat->getRiderLean()._x > 0.2f ); // 앞으로
    SW_EXPECT_NEAR( 0.0f, rig._pSeat->getRiderLean()._y, 1e-3f );

    SW_REQUIRE_TRUE( VehicleSeatComponent::dismount( *rig._pRiderPawn, false ) );
    rig.tick();
    SW_EXPECT_NEAR( 0.0f, rig._pSeat->getRideAcceleration()._z, 1e-6f );
}
