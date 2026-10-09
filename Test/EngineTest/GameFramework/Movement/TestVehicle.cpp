#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Movement/ArcadeVehicleMotor.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 아케이드 차량 — 가속 · 최고 속도 · 브레이크 · 후진, 속도에 따라 커지는 조향 반경, 드리프트 충전 단계와 미니터보, 오프로드 감속,
// 점프 · 턱에서 날기 · 착지 · 공중 조향 약화, 니트로 게이지 충전 · 사용, 같은 입력이면 같은 결과.

using namespace sw;

namespace
{
    constexpr float32 kVehicleStep = 1.0f / 60.0f;

    /** @brief x > 50 은 풀밭(속도 0.5), z 20 앞은 높이 2 의 언덕입니다. */
    struct TestVehicleGround final : public IVehicleGround
    {
        float32 sampleHeight( float32 x, float32 z ) const override
        {
            (void)x;
            return _bHill && z < 20.0f ? 2.0f : 0.0f;
        }

        float32 sampleSpeedScale( float32 x, float32 z ) const override
        {
            (void)z;
            return x > 50.0f ? 0.5f : 1.0f;
        }

        bool _bHill{ false };
    };

    ArcadeVehicleInput makeInput( float32 throttle, float32 steer, bool bDrift = false )
    {
        ArcadeVehicleInput input;
        input._throttle   = throttle;
        input._steer      = steer;
        input._bDriftHeld = bDrift ? SW_TRUE : SW_FALSE;
        return input;
    }

    void runSteps( ArcadeVehicleMotor& motor, const ArcadeVehicleInput& input, int32 stepCount )
    {
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            motor.update( input, kVehicleStep );
        }
    }

    int32 countEvents( const vector<ArcadeVehicleEvent>& listEvent, ArcadeVehicleEvent::Kind kind )
    {
        int32 count = 0;
        for ( const ArcadeVehicleEvent& event : listEvent )
        {
            if ( event._kind == kind )
                ++count;
        }
        return count;
    }

    const ArcadeVehicleEvent* findEvent( const vector<ArcadeVehicleEvent>& listEvent, ArcadeVehicleEvent::Kind kind )
    {
        for ( const ArcadeVehicleEvent& event : listEvent )
        {
            if ( event._kind == kind )
                return &event;
        }
        return nullptr;
    }

    /** @brief 최고 속도로 곧게 달리는 차입니다. */
    void startAtTopSpeed( ArcadeVehicleMotor& motor )
    {
        motor.reset( float3{ 0.0f, 0.0f, 0.0f }, 0.0f );
        runSteps( motor, makeInput( 1.0f, 0.0f ), 180 );
        vector<ArcadeVehicleEvent> listEvent;
        motor.drainEvents( listEvent );
    }
} // namespace

SW_TEST_CASE( VehicleTest, AcceleratesToTopSpeedCoastsAndBrakesIntoReverse )
{
    ArcadeVehicleMotor motor;
    motor.reset( float3{}, 0.0f );
    const ArcadeVehicleSettings& settings = motor.getSettings();

    runSteps( motor, makeInput( 1.0f, 0.0f ), 60 ); // 1 초 — 가속 18
    SW_EXPECT_NEAR_EQUAL( 18.0f, motor.getForwardSpeed(), 0.05f );
    SW_EXPECT_TRUE( motor.getPosition()._z > 8.0f && MathUtil::abs( motor.getPosition()._x ) < 1.0e-4f ); // yaw 0 = +Z
    runSteps( motor, makeInput( 1.0f, 0.0f ), 120 );
    SW_EXPECT_NEAR_EQUAL( settings._maxSpeed, motor.getForwardSpeed(), 1.0e-3f ); // 상한에서 멈춘다

    runSteps( motor, makeInput( 0.0f, 0.0f ), 60 ); // 관성 — 1 초에 6 줄어든다
    SW_EXPECT_NEAR_EQUAL( settings._maxSpeed - settings._coastDrag, motor.getForwardSpeed(), 0.05f );

    runSteps( motor, makeInput( -1.0f, 0.0f ), 30 ); // 브레이크 40 — 0.5 초에 20
    SW_EXPECT_NEAR_EQUAL( 4.0f, motor.getForwardSpeed(), 0.05f );
    runSteps( motor, makeInput( -1.0f, 0.0f ), 180 ); // 멈춘 뒤 후진 상한까지
    SW_EXPECT_NEAR_EQUAL( -settings._reverseMaxSpeed, motor.getForwardSpeed(), 1.0e-3f );
}

SW_TEST_CASE( VehicleTest, TurningRadiusGrowsWithSpeed )
{
    ArcadeVehicleMotor motor;
    SW_EXPECT_NEAR_EQUAL( 0.0f, motor.computeSteerRate( 0.0f ), 1.0e-6f ); // 멈춰서는 돌지 않는다
    SW_EXPECT_TRUE( motor.computeSteerRate( 10.0f ) > motor.computeSteerRate( 30.0f ) );

    // 같은 조향으로 1 초 — 반경 = 간 거리 / 돈 각.
    const auto measureRadius = [&]( float32 throttle )
    {
        motor.reset( float3{}, 0.0f );
        runSteps( motor, makeInput( throttle, 0.0f ), 300 );
        const float32 startYaw = motor.getYaw();
        float32       distance = 0.0f;
        for ( int32 stepIndex = 0; stepIndex < 60; ++stepIndex )
        {
            const float3 before = motor.getPosition();
            motor.update( makeInput( throttle, 1.0f ), kVehicleStep );
            distance += float3::getDistance( before, motor.getPosition() );
        }
        const float32 turned = motor.getYaw() - startYaw;
        SW_EXPECT_TRUE( turned > 0.0f ); // 양의 조향 = 오른쪽(+yaw)
        return distance / turned;
    };
    const float32 slowRadius = measureRadius( 1.0f / 3.0f ); // 약 10
    const float32 fastRadius = measureRadius( 1.0f );        // 30
    SW_EXPECT_NEAR_EQUAL( 10.0f / motor.computeSteerRate( 10.0f ), slowRadius, 0.6f );
    SW_EXPECT_TRUE( fastRadius > slowRadius * 3.0f );
    // 돌면서 차가 옆으로 조금 미끄러진다(접지가 줄이지만 0 은 아니다).
    SW_EXPECT_TRUE( motor.getLateralSpeed() < -0.5f );
}

SW_TEST_CASE( VehicleTest, DriftChargesMiniTurboTiersAndReleasesBoost )
{
    ArcadeVehicleMotor motor;
    startAtTopSpeed( motor );
    const ArcadeVehicleSettings& settings = motor.getSettings();
    vector<ArcadeVehicleEvent>   listEvent;

    // 짧은 드리프트(0.5 초 < 1 단계 0.6 초)는 보상이 없다.
    runSteps( motor, makeInput( 1.0f, -1.0f, true ), 30 );
    SW_EXPECT_TRUE( motor.isDrifting() && motor.getDriftDirection() == -1 );
    runSteps( motor, makeInput( 1.0f, 0.0f ), 1 );
    listEvent.clear();
    motor.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, ArcadeVehicleEvent::Kind::DriftStarted ) );
    SW_EXPECT_EQUAL( 0, countEvents( listEvent, ArcadeVehicleEvent::Kind::MiniTurbo ) );
    SW_EXPECT_TRUE( motor.isBoosting() == false );

    // 1.5 초 — 1 · 2 단계. 조향을 풀어도(0) 드리프트 방향(오른쪽)으로 계속 돈다.
    startAtTopSpeed( motor );
    runSteps( motor, makeInput( 1.0f, 1.0f, true ), 1 );
    const float32 yawAtStart = motor.getYaw();
    runSteps( motor, makeInput( 1.0f, 0.0f, true ), 89 );
    SW_EXPECT_TRUE( motor.getYaw() > yawAtStart + 0.5f );
    SW_EXPECT_EQUAL( 2, motor.getDriftTier() );
    SW_EXPECT_TRUE( motor.getLateralSpeed() < -3.0f ); // 드리프트 접지가 낮아 크게 미끄러진다
    runSteps( motor, makeInput( 1.0f, 0.0f, false ), 1 );
    listEvent.clear();
    motor.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 2, countEvents( listEvent, ArcadeVehicleEvent::Kind::DriftTierReached ) );
    const ArcadeVehicleEvent* pMiniTurbo = findEvent( listEvent, ArcadeVehicleEvent::Kind::MiniTurbo );
    SW_ASSERT_NOT_NULL( pMiniTurbo );
    SW_EXPECT_EQUAL( 2, pMiniTurbo->_value );
    const ArcadeVehicleEvent* pBoost = findEvent( listEvent, ArcadeVehicleEvent::Kind::BoostStarted );
    SW_ASSERT_NOT_NULL( pBoost );
    SW_EXPECT_EQUAL( 2, pBoost->_value );
    SW_EXPECT_NEAR_EQUAL( settings._arrMiniTurboBoost[1] - kVehicleStep, motor.getBoostTime(), 1.0e-3f );

    // 부스트 중에는 최고 속도를 넘고, 끝나면 상한으로 돌아온다.
    runSteps( motor, makeInput( 1.0f, 0.0f ), 50 );
    SW_EXPECT_TRUE( motor.getForwardSpeed() > settings._maxSpeed + 3.0f );
    runSteps( motor, makeInput( 1.0f, 0.0f ), 120 );
    listEvent.clear();
    motor.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, ArcadeVehicleEvent::Kind::BoostEnded ) );
    SW_EXPECT_NEAR_EQUAL( settings._maxSpeed, motor.getForwardSpeed(), 1.0e-3f );

    // 3 단계, 그리고 너무 느리면 드리프트가 시작되지 않는다.
    startAtTopSpeed( motor );
    runSteps( motor, makeInput( 1.0f, 1.0f, true ), 140 );
    SW_EXPECT_EQUAL( 3, motor.getDriftTier() );
    motor.reset( float3{}, 0.0f );
    runSteps( motor, makeInput( 1.0f, 1.0f, true ), 20 );
    SW_EXPECT_TRUE( motor.isDrifting() == false );
}

SW_TEST_CASE( VehicleTest, OffroadSlowsUnlessBoostingOrInsensitive )
{
    TestVehicleGround  ground;
    ArcadeVehicleMotor motor;
    motor.setGround( &ground );
    startAtTopSpeed( motor );
    motor.setPosition( float3{ 60.0f, 0.0f, motor.getPosition()._z } ); // 풀밭으로
    runSteps( motor, makeInput( 1.0f, 0.0f ), 30 );
    SW_EXPECT_TRUE( motor.getForwardSpeed() < 30.0f && motor.getForwardSpeed() > 15.0f ); // 갑자기 서지 않고 줄어든다
    runSteps( motor, makeInput( 1.0f, 0.0f ), 60 );
    SW_EXPECT_NEAR_EQUAL( 15.0f, motor.getForwardSpeed(), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, motor.computeSpeedCap(), 1.0e-4f );

    // 부스트(버섯)는 풀밭을 무시한다.
    motor.startBoost( 1.0f );
    runSteps( motor, makeInput( 1.0f, 0.0f ), 40 );
    SW_EXPECT_TRUE( motor.getForwardSpeed() > 30.0f );

    // 오프로드에 둔한 차(마차 · 4륜)는 줄지 않는다.
    ArcadeVehicleSettings settings = motor.getSettings();
    settings._offroadSensitivity   = 0.0f;
    motor.setSettings( settings );
    startAtTopSpeed( motor );
    motor.setPosition( float3{ 60.0f, 0.0f, motor.getPosition()._z } );
    runSteps( motor, makeInput( 1.0f, 0.0f ), 120 );
    SW_EXPECT_NEAR_EQUAL( 30.0f, motor.getForwardSpeed(), 1.0e-3f );
}

SW_TEST_CASE( VehicleTest, JumpsLandsAndFliesOffLedges )
{
    ArcadeVehicleMotor motor;
    startAtTopSpeed( motor );
    const ArcadeVehicleSettings& settings = motor.getSettings();
    vector<ArcadeVehicleEvent>   listEvent;

    ArcadeVehicleInput jump = makeInput( 1.0f, 0.0f );
    jump._bJumpPressed      = SW_TRUE;
    motor.update( jump, kVehicleStep );
    SW_EXPECT_TRUE( motor.isAirborne() );
    // 공중에서는 조향이 약하다 — 지면에서 같은 시간 꺾은 것과 비교.
    const float32 yawBefore = motor.getYaw();
    runSteps( motor, makeInput( 1.0f, 1.0f ), 10 );
    const float32 airTurn = motor.getYaw() - yawBefore;
    SW_EXPECT_NEAR_EQUAL( motor.computeSteerRate( motor.getForwardSpeed() ) * settings._airSteerScale * 10.0f * kVehicleStep, airTurn, 1.0e-3f );

    // 비행 시간 = 2 × 7 / 25 = 0.56 초(약 34 걸음).
    int32 airSteps = 11;
    while ( motor.isAirborne() && airSteps < 200 )
    {
        motor.update( makeInput( 1.0f, 0.0f ), kVehicleStep );
        ++airSteps;
    }
    SW_EXPECT_TRUE( airSteps >= 33 && airSteps <= 35 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, motor.getPosition()._y, 1.0e-6f );
    listEvent.clear();
    motor.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, ArcadeVehicleEvent::Kind::Jumped ) );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, ArcadeVehicleEvent::Kind::Landed ) );

    // 언덕(높이 2) 끝에서 땅이 꺼지면 떠올라 날다가 내려앉는다.
    TestVehicleGround ground;
    ground._bHill = true;
    motor.setGround( &ground );
    motor.reset( float3{ 0.0f, 2.0f, 0.0f }, 0.0f );
    runSteps( motor, makeInput( 1.0f, 0.0f ), 60 );
    SW_EXPECT_NEAR_EQUAL( 2.0f, motor.getPosition()._y, 1.0e-6f ); // 언덕 위를 따라간다
    runSteps( motor, makeInput( 1.0f, 0.0f ), 120 );
    listEvent.clear();
    motor.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, ArcadeVehicleEvent::Kind::LeftGround ) );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, ArcadeVehicleEvent::Kind::Landed ) );
    SW_EXPECT_TRUE( motor.isAirborne() == false && motor.getPosition()._y == 0.0f );
}

SW_TEST_CASE( VehicleTest, DriftFillsNitroThatBoostsOnPress )
{
    ArcadeVehicleMotor motor;
    startAtTopSpeed( motor );
    const ArcadeVehicleSettings& settings = motor.getSettings();
    vector<ArcadeVehicleEvent>   listEvent;

    // 니트로가 없으면 눌러도 아무 일 없다.
    ArcadeVehicleInput press = makeInput( 1.0f, 0.0f );
    press._bBoostPressed     = SW_TRUE;
    motor.update( press, kVehicleStep );
    SW_EXPECT_TRUE( motor.isBoosting() == false );

    // 0.35 / 초 — 3 초 드리프트로 하나(게이지 0.05 남음).
    runSteps( motor, makeInput( 1.0f, 1.0f, true ), 180 );
    SW_EXPECT_EQUAL( 1, motor.getNitroCount() );
    SW_EXPECT_NEAR_EQUAL( 0.05f, motor.getNitroGauge(), 0.02f );
    runSteps( motor, makeInput( 1.0f, 0.0f ), 120 ); // 미니터보가 끝나게
    listEvent.clear();
    motor.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, ArcadeVehicleEvent::Kind::NitroCharged ) );

    motor.update( press, kVehicleStep );
    SW_EXPECT_EQUAL( 0, motor.getNitroCount() );
    SW_EXPECT_NEAR_EQUAL( settings._nitroBoostTime - kVehicleStep, motor.getBoostTime(), 1.0e-3f );
    listEvent.clear();
    motor.drainEvents( listEvent );
    const ArcadeVehicleEvent* pBoost = findEvent( listEvent, ArcadeVehicleEvent::Kind::BoostStarted );
    SW_ASSERT_NOT_NULL( pBoost );
    SW_EXPECT_EQUAL( 0, pBoost->_value ); // 0 = 니트로

    // 최대 두 개 — 더 드리프트해도 게이지가 가득에서 멈춘다.
    startAtTopSpeed( motor );
    runSteps( motor, makeInput( 1.0f, 1.0f, true ), 600 );
    SW_EXPECT_EQUAL( settings._maxNitroCount, motor.getNitroCount() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, motor.getNitroGauge(), 1.0e-6f );
}

SW_TEST_CASE( VehicleTest, SameInputsGiveTheSameRide )
{
    TestVehicleGround ground;
    ground._bHill   = true;
    const auto ride = [&]( bool bUseAdvance, float3& outPosition, float32& outYaw, int32& outEventCount )
    {
        ArcadeVehicleMotor motor;
        motor.setGround( &ground );
        motor.reset( float3{ 0.0f, 2.0f, 0.0f }, 0.0f );
        vector<ArcadeVehicleEvent> listEvent;
        outEventCount = 0;
        for ( int32 stepIndex = 0; stepIndex < 600; ++stepIndex )
        {
            ArcadeVehicleInput input = makeInput( 1.0f, MathUtil::sin( static_cast<float32>( stepIndex ) * 0.02f ), ( stepIndex / 90 ) % 2 == 1 );
            input._bBoostPressed     = stepIndex % 150 == 149 ? SW_TRUE : SW_FALSE;
            input._bJumpPressed      = stepIndex == 400 ? SW_TRUE : SW_FALSE;
            if ( bUseAdvance )
                (void)motor.advance( input, motor.getTimer().getStep() );
            else
                motor.update( input, kVehicleStep );
            listEvent.clear();
            motor.drainEvents( listEvent );
            outEventCount += static_cast<int32>( listEvent.size() );
        }
        outPosition = motor.getPosition();
        outYaw      = motor.getYaw();
    };
    float3  positionA, positionB, positionC;
    float32 yawA = 0.0f, yawB = 0.0f, yawC = 0.0f;
    int32   eventCountA = 0, eventCountB = 0, eventCountC = 0;
    ride( false, positionA, yawA, eventCountA );
    ride( false, positionB, yawB, eventCountB );
    ride( true, positionC, yawC, eventCountC );
    SW_EXPECT_TRUE( positionA._x == positionB._x && positionA._y == positionB._y && positionA._z == positionB._z && yawA == yawB );
    SW_EXPECT_EQUAL( eventCountA, eventCountB );
    SW_EXPECT_TRUE( eventCountA > 5 );
    // 고정 걸음 누적기를 거쳐도 같다(한 프레임 = 한 걸음).
    SW_EXPECT_TRUE( positionA._x == positionC._x && positionA._z == positionC._z && yawA == yawC );
    SW_EXPECT_EQUAL( eventCountA, eventCountC );
}
