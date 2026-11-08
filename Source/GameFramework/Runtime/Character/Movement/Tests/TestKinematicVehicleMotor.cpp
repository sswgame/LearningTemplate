// KinematicVehicleMotorTest — 관성 이동(경사 중력 · 구름 저항 · 공기 저항 · 질량)과 조작감 모듈(그립 · 부스트 · 공중 제어 · 호버 · 레일), 튜닝 검증. 물리 씬 없이 돈다(nogpu).
// 같은 언덕의 내리막 가속 · 오르막 감속, 빙판이 눈보다 멀리, 무거운 썰매가 더 빠름을 값으로 본다. 실제 씬은 시나리오 game.empty.inertia 가 본다.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Character/Movement/InertiaSettings.h"
#include "GameFramework/Runtime/Character/Movement/KinematicVehicleMotor.h"
#include "GameFramework/Runtime/World/Spline/SplinePath.h"

using namespace sw;

namespace
{
    /** @brief +Z 로 갈수록 `_rise` 비율로 오르는 평평한 비탈입니다. 표면은 하나입니다. */
    class KinematicVehicleTestGround final : public IVehicleGround
    {
    public:
        VehicleSurface _surface{};
        float32        _rise{ 0.0f };

        float32 sampleHeight( float32 x, float32 z ) const override
        {
            (void)x;
            return _rise * z;
        }
        VehicleSurface sampleSurface( float32 x, float32 z ) const override
        {
            (void)x;
            (void)z;
            return _surface;
        }
    };

    struct KinematicVehicleMotorTestUtil
    {
        static constexpr float32 kStep = 1.0f / 60.0f;

        /** @brief 구동 없는 썰매입니다(속도 상한 · 엔진 브레이크 없음, 그립 약함, 드리프트 · 부스트 · 점프 없음). */
        static KinematicVehicleSettings makeSled()
        {
            KinematicVehicleSettings sled;
            sled._maxSpeed              = 100.0f;
            sled._maxDriveForce         = 0.0f;
            sled._coastDrag             = 0.0f;
            sled._overSpeedDeceleration = 0.0f;
            sled._inertia._mass         = 80.0f;
            sled._inertia._airDrag      = 0.3f;
            sled._grip._lateralGrip     = 3.0f;
            sled._drift._bEnabled       = false;
            sled._boost._bEnabled       = false;
            sled._airControl._bEnabled  = false;
            return sled;
        }

        /** @brief @p seconds 동안 입력 없이 달립니다. */
        static void runCoasting( KinematicVehicleMotor& motor, float32 seconds )
        {
            const int32 stepCount = static_cast<int32>( seconds / kStep + 0.5f );
            for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
                motor.update( KinematicVehicleInput{}, kStep );
        }

        /** @brief @p rise 비탈 위에서 @p yaw 로 놓고 @p speed 로 밀어 @p seconds 뒤의 앞 속도입니다. */
        static float32 runOnSlope( const KinematicVehicleSettings& settings, float32 rise, float32 yaw, float32 speed, float32 seconds, float32 rolling = 0.0f )
        {
            KinematicVehicleTestGround ground;
            ground._rise                       = rise;
            ground._surface._rollingResistance = rolling;
            KinematicVehicleMotor motor;
            motor.setSettings( settings );
            motor.setGround( &ground );
            motor.reset( float3{}, yaw );
            motor.setVelocity( float3{ MathUtil::sin( yaw ) * speed, 0.0f, MathUtil::cos( yaw ) * speed } );
            runCoasting( motor, seconds );
            return motor.getForwardSpeed();
        }
    };
    using Util = KinematicVehicleMotorTestUtil;
} // namespace

/** @brief [KinematicVehicleMotorTest] 바깥 가속도: 내리막은 g·sin 만큼 밀고, 공기 저항은 계수·v²/질량, 구름 저항은 멈춘 몸을 경사가 이길 때까지 붙잡는다 */
SW_TEST_CASE( KinematicVehicleMotorTest, AlongAccelerationIsSlopeMinusResistances )
{
    InertiaSettings inertia;
    inertia._mass    = 100.0f;
    inertia._gravity = 10.0f;
    SW_EXPECT_NEAR( 0.0f, inertia.computeAlongAcceleration( 5.0f, 0.0f, 0.0f, Util::kStep ), 1.0e-5f );
    SW_EXPECT_NEAR( 2.0f, inertia.computeAlongAcceleration( 0.0f, -0.2f, 0.0f, Util::kStep ), 1.0e-4f ); // 내리막
    SW_EXPECT_NEAR( -2.0f, inertia.computeAlongAcceleration( 0.0f, 0.2f, 0.0f, Util::kStep ), 1.0e-4f ); // 오르막은 뒤로

    inertia._airDrag = 2.0f;
    SW_EXPECT_NEAR( -2.0f, inertia.computeAlongAcceleration( 10.0f, 0.0f, 0.0f, Util::kStep ), 1.0e-4f ); // 2·10²/100

    inertia._airDrag = 0.0f;
    SW_EXPECT_NEAR( -1.0f, inertia.computeAlongAcceleration( 5.0f, 0.0f, 0.1f, Util::kStep ), 1.0e-4f );  // μg
    SW_EXPECT_NEAR( 0.0f, inertia.computeAlongAcceleration( 0.0f, -0.05f, 0.1f, Util::kStep ), 1.0e-4f ); // 경사 0.5 < 마찰 1 — 선다
    SW_EXPECT_TRUE( inertia.computeAlongAcceleration( 0.0f, -0.2f, 0.1f, Util::kStep ) > 0.9f );          // 경사 2 > 마찰 1 — 미끄러진다
    // 한 걸음 안에서 부호를 뒤집지 않는다 — 거의 멈춘 몸은 정확히 0 이 된다.
    const float32 slow = 0.005f;
    SW_EXPECT_NEAR( 0.0f, slow + inertia.computeAlongAcceleration( slow, 0.0f, 0.1f, Util::kStep ) * Util::kStep, 1.0e-6f );
}

/** @brief [KinematicVehicleMotorTest] 출력 한계: 구동 가속도는 P/(m·v) 까지, 0 이면 한계 없음, 서 있을 때는 kPowerMinSpeed 로 본다. 오르막에서 다리 · 페달로 미는 몸은 P/(m·g·sin) 에 선다 */
SW_TEST_CASE( KinematicVehicleMotorTest, PowerLimitCapsDriveAndSlowsClimbs )
{
    InertiaSettings inertia;
    inertia._mass    = 500.0f;
    inertia._gravity = 10.0f;
    SW_EXPECT_NEAR( 6.0f, inertia.limitDriveAcceleration( 6.0f, 10.0f ), 1.0e-6f ); // 한계 없음
    inertia._maxPower = 5000.0f;
    SW_EXPECT_NEAR( 1.0f, inertia.limitDriveAcceleration( 6.0f, 10.0f ), 1.0e-5f ); // 5000 / (500·10)
    SW_EXPECT_NEAR( 6.0f, inertia.limitDriveAcceleration( 6.0f, 0.5f ), 1.0e-5f );  // 10 / 1 m/s 로 본 한계 > 6 — 출발은 가속 한계
    SW_EXPECT_NEAR( 2.0f, inertia.limitDriveAcceleration( 6.0f, -5.0f ), 1.0e-5f ); // 뒤로 가도 속도 크기로 본다

    // 말과 같은 적분(경사 당김 → 걸음새 속도로 다가감) — 기울기 사인 0.2 에서 평형 5000 / (500·10·0.2) = 5 m/s, 목표 10 m/s 에 닿지 않는다.
    float32 speed = 0.0f;
    for ( int32 stepIndex = 0; stepIndex < 60 * 30; ++stepIndex )
    {
        speed += inertia.computeAlongAcceleration( speed, 0.2f, 0.0f, Util::kStep ) * Util::kStep;
        speed = MathUtil::moveToward( speed, 10.0f, inertia.limitDriveAcceleration( 6.0f, speed ) * Util::kStep );
    }
    SW_EXPECT_NEAR( 5.0f, speed, 0.05f );
    // 평지에서는 같은 말이 목표 속도에 닿는다(출력이 공기 저항 없는 평지 유지에는 모자라지 않다).
    float32 flat = 0.0f;
    for ( int32 stepIndex = 0; stepIndex < 60 * 30; ++stepIndex )
    {
        flat += inertia.computeAlongAcceleration( flat, 0.0f, 0.0f, Util::kStep ) * Util::kStep;
        flat = MathUtil::moveToward( flat, 10.0f, inertia.limitDriveAcceleration( 6.0f, flat ) * Util::kStep );
    }
    SW_EXPECT_NEAR( 10.0f, flat, 1.0e-3f );
}

/** @brief [KinematicVehicleMotorTest] 같은 언덕: 썰매는 내리막에서 멈춘 데서 빨라지고, 오르막에서는 평지보다 빨리 느려진다 */
SW_TEST_CASE( KinematicVehicleMotorTest, SameHillSpeedsUpDownhillAndSlowsUphill )
{
    const KinematicVehicleSettings sled = Util::makeSled();
    // 비탈은 +Z 로 오른다(기울기 0.2). 요 π 는 −Z(내리막), 요 0 은 +Z(오르막)이다.
    const float32 downhill = Util::runOnSlope( sled, 0.2f, MathUtil::kPi, 0.0f, 2.0f );
    SW_EXPECT_TRUE( downhill > 3.0f ); // g·sin ≈ 1.92 m/s² × 2 초에서 공기 저항을 뺀 값
    SW_EXPECT_TRUE( downhill < 3.9f );

    const float32 flat   = Util::runOnSlope( sled, 0.0f, 0.0f, 10.0f, 2.0f );
    const float32 uphill = Util::runOnSlope( sled, 0.2f, 0.0f, 10.0f, 2.0f );
    SW_EXPECT_TRUE( flat > 9.0f ); // 평지는 공기 저항만
    SW_EXPECT_TRUE( uphill < flat - 3.0f );
}

/** @brief [KinematicVehicleMotorTest] 같은 속도로 미끄러지면 빙판(구름 저항 낮음)이 눈(높음)보다 멀리 가고, 구동 없는 썰매는 결국 선다 */
SW_TEST_CASE( KinematicVehicleMotorTest, IceGlidesFartherThanSnow )
{
    const KinematicVehicleSettings sled = Util::makeSled();
    const float32                  ice  = Util::runOnSlope( sled, 0.0f, 0.0f, 8.0f, 3.0f, 0.005f );
    const float32                  snow = Util::runOnSlope( sled, 0.0f, 0.0f, 8.0f, 3.0f, 0.06f );
    SW_EXPECT_TRUE( ice > snow + 1.0f );
    SW_EXPECT_NEAR( 0.0f, Util::runOnSlope( sled, 0.0f, 0.0f, 2.0f, 10.0f, 0.15f ), 1.0e-4f ); // 모래: 2 / (0.15·9.81) ≈ 1.4 초면 선다
}

/** @brief [KinematicVehicleMotorTest] 무거운 썰매(질량 한 칸)는 공기 저항이 덜 걸려 내리막 끝속도가 높다 */
SW_TEST_CASE( KinematicVehicleMotorTest, HeavierSledRunsFasterDownhill )
{
    KinematicVehicleSettings light = Util::makeSled();
    KinematicVehicleSettings heavy = light;
    heavy._inertia._mass           = 240.0f;
    const float32 lightSpeed       = Util::runOnSlope( light, 0.3f, MathUtil::kPi, 0.0f, 8.0f );
    const float32 heavySpeed       = Util::runOnSlope( heavy, 0.3f, MathUtil::kPi, 0.0f, 8.0f );
    SW_EXPECT_TRUE( heavySpeed > lightSpeed + 1.0f );
}

/** @brief [KinematicVehicleMotorTest] 구동력은 질량으로 나뉜다: 같은 힘이면 두 배 무거운 차가 절반으로 가속한다 */
SW_TEST_CASE( KinematicVehicleMotorTest, DriveForceDividesByMass )
{
    KinematicVehicleSettings kart;
    SW_EXPECT_NEAR( 18.0f, kart.computeDriveAcceleration(), 1.0e-4f ); // 3600 N / 200 kg — 카트 기본값은 옛 가속 18 m/s² 와 같다
    SW_EXPECT_NEAR( 40.0f, kart.computeBrakeDeceleration(), 1.0e-4f );
    kart._inertia._mass = 400.0f;
    SW_EXPECT_NEAR( 9.0f, kart.computeDriveAcceleration(), 1.0e-4f );
}

/** @brief [KinematicVehicleMotorTest] 그립: 표면 그립 배율이 낮으면(빙판) 옆 미끄러짐이 오래 남고, 그립 모듈을 끄면 옆 속도가 그대로다 */
SW_TEST_CASE( KinematicVehicleMotorTest, GripScaleKeepsSideSlip )
{
    const auto runSide = []( float32 gripScale, bool bGrip )
    {
        KinematicVehicleSettings settings = Util::makeSled();
        settings._inertia._airDrag        = 0.0f;
        settings._grip._bEnabled          = bGrip;
        KinematicVehicleTestGround ground;
        ground._surface._gripScale = gripScale;
        KinematicVehicleMotor motor;
        motor.setSettings( settings );
        motor.setGround( &ground );
        motor.reset( float3{}, 0.0f );
        motor.setVelocity( float3{ 4.0f, 0.0f, 10.0f } ); // 옆(+X) 4, 앞 10
        Util::runCoasting( motor, 0.5f );
        return MathUtil::abs( motor.getLateralSpeed() );
    };
    const float32 dry = runSide( 1.0f, true );
    const float32 ice = runSide( 0.15f, true );
    SW_EXPECT_TRUE( ice > dry + 1.0f );
    SW_EXPECT_NEAR( 4.0f, runSide( 1.0f, false ), 1.0e-3f );
}

/** @brief [KinematicVehicleMotorTest] 모듈을 끄면 그 일이 없다: 부스트 모듈 없이 부스트가 걸리지 않고, 공중 제어 없이 점프하지 않는다 */
SW_TEST_CASE( KinematicVehicleMotorTest, DisabledModulesDoNothing )
{
    KinematicVehicleMotor motor;
    motor.setSettings( Util::makeSled() );
    motor.reset( float3{}, 0.0f );
    motor.startBoost( 2.0f );
    SW_EXPECT_FALSE( motor.isBoosting() );
    KinematicVehicleInput jump;
    jump._bJumpPressed = SW_TRUE;
    motor.update( jump, Util::kStep );
    SW_EXPECT_FALSE( motor.isAirborne() );
    SW_EXPECT_NEAR( 0.0f, motor.getPosition()._y, 1.0e-5f );
}

/** @brief [KinematicVehicleMotorTest] 호버: 땅 위 높이에 용수철로 서고, 바퀴가 닿지 않아 구름 저항이 걸리지 않는다 */
SW_TEST_CASE( KinematicVehicleMotorTest, HoverHoldsHeightWithoutRollingResistance )
{
    KinematicVehicleSettings settings = Util::makeSled();
    settings._inertia._airDrag        = 0.0f;
    settings._hover._bEnabled         = true;
    settings._hover._height           = 1.0f;
    KinematicVehicleTestGround ground;
    ground._surface._rollingResistance = 0.3f; // 물 위
    KinematicVehicleMotor motor;
    motor.setSettings( settings );
    motor.setGround( &ground );
    motor.reset( float3{}, 0.0f );
    motor.setVelocity( float3{ 0.0f, 0.0f, 6.0f } );
    Util::runCoasting( motor, 3.0f );
    SW_EXPECT_NEAR( 1.0f, motor.getPosition()._y, 0.05f );
    SW_EXPECT_NEAR( 6.0f, motor.getForwardSpeed(), 1.0e-3f );
    SW_EXPECT_FALSE( motor.isAirborne() );
}

/** @brief [KinematicVehicleMotorTest] 레일: 기운 레일을 멈춘 데서 내려가며 빨라지고, 위치는 레일 위, 열린 끝에서 서며 레일 끝 알림을 낸다 */
SW_TEST_CASE( KinematicVehicleMotorTest, RailRollsDownAndStopsAtTheEnd )
{
    SplinePath rail;
    SW_REQUIRE_TRUE( rail.initialize( {
                                          float3{0.0f, 10.0f,  0.0f},
                                          float3{0.0f,  0.0f, 50.0f}
    },
                                      SplineType::Linear, false ) );
    KinematicVehicleSettings cart = Util::makeSled();
    cart._rail._bEnabled          = true;
    cart._grip._bEnabled          = false;
    KinematicVehicleMotor motor;
    motor.setSettings( cart );
    motor.reset( float3{ 0.0f, 10.0f, 0.0f }, 0.0f );
    motor.setRail( &rail );
    SW_EXPECT_NEAR( 0.0f, motor.getRailDistance(), 1.0e-3f );
    Util::runCoasting( motor, 2.0f );
    const float32 distance = motor.getRailDistance();
    SW_EXPECT_TRUE( distance > 2.0f );
    SW_EXPECT_NEAR( 10.0f - distance * 10.0f / rail.getLength(), motor.getPosition()._y, 1.0e-2f ); // 레일 위
    SW_EXPECT_NEAR( 0.0f, motor.getPosition()._x, 1.0e-4f );
    SW_EXPECT_NEAR( 0.0f, motor.getYaw(), 1.0e-4f );

    vector<KinematicVehicleEvent> listEvent;
    motor.pollEvents( listEvent );
    Util::runCoasting( motor, 20.0f );
    motor.pollEvents( listEvent );
    SW_EXPECT_NEAR( rail.getLength(), motor.getRailDistance(), 1.0e-3f );
    SW_EXPECT_NEAR( 0.0f, motor.getForwardSpeed(), 1.0e-4f );
    bool bEnded = false;
    for ( const KinematicVehicleEvent& event : listEvent )
        bEnded = bEnded || event._kind == KinematicVehicleEvent::Kind::RailEnded;
    SW_EXPECT_TRUE( bEnded );
}

/** @brief [KinematicVehicleMotorTest] 튜닝 검증: 미니터보 단계 시간이 내려가거나 호버와 레일을 함께 켜면 오류다 */
SW_TEST_CASE( KinematicVehicleMotorTest, ValidateRejectsConflictingModules )
{
    KinematicVehicleSettings settings;
    SW_EXPECT_TRUE( settings.validate( "ok" ) );
    settings._hover._bEnabled = true;
    settings._rail._bEnabled  = true;
    SW_EXPECT_LOG( "KinematicVehicle.HoverAndRail" );
    SW_EXPECT_FALSE( settings.validate( "both" ) );

    KinematicVehicleSettings tiers;
    tiers._drift._arrMiniTurboTime[2] = 0.1f;
    SW_EXPECT_LOG( "KinematicVehicle.TierTimeOrder" );
    SW_EXPECT_FALSE( tiers.validate( "tiers" ) );
}
