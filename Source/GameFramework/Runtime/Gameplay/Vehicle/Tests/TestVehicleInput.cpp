#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Physics/Component/WheeledVehicleComponent.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/KinematicVehicleComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/PhysicsCarComponent.h"

// VehicleInputTest — 의도(가고 싶은 월드 방향)를 페달 · 조향으로 바꾸는 규칙(물리 차 · 키네마틱 차(바디 없는 차))과 바퀴 차 서술자. 물리 씬 없이 돈다(nogpu). 실제 주행은 시나리오 game.empty.vehicles 가 본다.

using namespace sw;

namespace
{
    struct VehicleInputTestUtil
    {
        static ControlIntent makeMove( float32 right, float32 forward )
        {
            ControlIntent intent;
            intent._move = float2{ right, forward };
            return intent;
        }
    };
    using Util = VehicleInputTestUtil;
} // namespace

/** @brief [VehicleInputTest] 물리 차: 앞은 페달, 옆은 조향이다 */
SW_TEST_CASE( VehicleInputTest, PhysicsCarTurnsWorldMoveIntoPedalAndSteering )
{
    const PhysicsCarInput input = PhysicsCarComponent::toCarInput( Util::makeMove( 0.6f, 0.8f ), float3{ 0.0f, 0.0f, 1.0f }, 0.0f, 0.5f, sw::nullopt );
    SW_EXPECT_TRUE( MathUtil::abs( input._forward - 0.8f ) < 1.0e-4f );
    SW_EXPECT_TRUE( MathUtil::abs( input._right - 0.6f ) < 1.0e-4f );
    SW_EXPECT_EQUAL( 0.0f, input._brake );
}

/** @brief [VehicleInputTest] 물리 차: 앞으로 가는 중에 뒤를 원하면 브레이크, 거의 멈췄으면 후진이다 */
SW_TEST_CASE( VehicleInputTest, PhysicsCarBrakesBeforeReversing )
{
    const ControlIntent   back    = Util::makeMove( 0.0f, -1.0f );
    const PhysicsCarInput moving  = PhysicsCarComponent::toCarInput( back, float3{ 0.0f, 0.0f, 1.0f }, 5.0f, 0.5f, sw::nullopt );
    const PhysicsCarInput stopped = PhysicsCarComponent::toCarInput( back, float3{ 0.0f, 0.0f, 1.0f }, 0.1f, 0.5f, sw::nullopt );
    SW_EXPECT_EQUAL( 1.0f, moving._brake );
    SW_EXPECT_EQUAL( 0.0f, moving._forward );
    SW_EXPECT_EQUAL( 0.0f, stopped._brake );
    SW_EXPECT_EQUAL( -1.0f, stopped._forward );
}

/** @brief [VehicleInputTest] 물리 차: 차가 옆을 보면 같은 월드 방향이 조향이 된다 */
SW_TEST_CASE( VehicleInputTest, PhysicsCarProjectsOnTheCarHeading )
{
    const PhysicsCarInput input = PhysicsCarComponent::toCarInput( Util::makeMove( 0.0f, 1.0f ), float3{ 1.0f, 0.0f, 0.0f }, 0.0f, 0.5f, sw::nullopt );
    SW_EXPECT_TRUE( MathUtil::abs( input._forward ) < 1.0e-4f );
    SW_EXPECT_TRUE( MathUtil::abs( input._right + 1.0f ) < 1.0e-4f ); // +X 를 보는 차에 +Z 는 왼쪽이다
}

/** @brief [VehicleInputTest] 키네마틱 차(바디 없는 차): 요를 따라 앞 · 옆으로 투영하고 버튼을 옮긴다 */
SW_TEST_CASE( VehicleInputTest, KinematicCarProjectsOnYawAndCarriesButtons )
{
    ControlIntent intent = Util::makeMove( 0.0f, 1.0f );
    intent.setButton( 0, true, false );
    intent.setButton( 1, true, true );
    const KinematicVehicleInput straight = KinematicVehicleComponent::toVehicleInput( intent, 0.0f, 0, 1, sw::nullopt );
    SW_EXPECT_TRUE( MathUtil::abs( straight._throttle - 1.0f ) < 1.0e-4f );
    SW_EXPECT_TRUE( MathUtil::abs( straight._steer ) < 1.0e-4f );
    SW_EXPECT_EQUAL( SW_TRUE, static_cast<uint8>( straight._bDriftHeld ) );
    SW_EXPECT_EQUAL( SW_TRUE, static_cast<uint8>( straight._bBoostPressed ) );
    SW_EXPECT_EQUAL( SW_FALSE, static_cast<uint8>( straight._bJumpPressed ) );

    const KinematicVehicleInput turned = KinematicVehicleComponent::toVehicleInput( intent, MathUtil::kPi * 0.5f, 0, 1, sw::nullopt );
    SW_EXPECT_TRUE( MathUtil::abs( turned._throttle ) < 1.0e-4f );
    SW_EXPECT_TRUE( MathUtil::abs( turned._steer + 1.0f ) < 1.0e-4f );
}

/** @brief [VehicleInputTest] 바퀴 넷의 순서와 앞바퀴 조향, 뒷바퀴 핸드브레이크, 굴림 차축 */
SW_TEST_CASE( VehicleInputTest, WheeledDescHasFourWheelsInOrder )
{
    const WheeledVehicleComponent   vehicle;
    const PhysicsWheeledVehicleDesc desc = vehicle.makeVehicleDesc();
    SW_REQUIRE_EQUAL( size_t{ 4 }, desc._listWheel.size() );
    SW_EXPECT_TRUE( desc._listWheel[0]._position._x < 0.0f );
    SW_EXPECT_TRUE( desc._listWheel[0]._position._z > 0.0f );
    SW_EXPECT_TRUE( desc._listWheel[3]._position._x > 0.0f );
    SW_EXPECT_TRUE( desc._listWheel[3]._position._z < 0.0f );
    SW_EXPECT_TRUE( desc._listWheel[0]._maxSteerAngle > 0.0f );
    SW_EXPECT_EQUAL( 0.0f, desc._listWheel[2]._maxSteerAngle );
    SW_EXPECT_EQUAL( 0.0f, desc._listWheel[0]._maxHandBrakeTorque );
    SW_EXPECT_TRUE( desc._listWheel[2]._maxHandBrakeTorque > 0.0f );
    SW_REQUIRE_EQUAL( size_t{ 2 }, desc._listDrivenAxle.size() ); // 기본은 뒷바퀴 굴림
    SW_EXPECT_EQUAL( 2, static_cast<int32>( desc._listDrivenAxle[0] ) );
}
