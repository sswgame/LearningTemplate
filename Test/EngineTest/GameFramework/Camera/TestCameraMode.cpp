#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Camera/CameraBlend.h"
#include "GameFramework/Base/Actor/Camera/CameraCollisionProbe.h"
#include "GameFramework/Base/Actor/Camera/CameraDirector.h"
#include "GameFramework/Base/Actor/Camera/CameraDirectorComponent.h"
#include "GameFramework/Base/Actor/Camera/CameraManagerComponent.h"
#include "GameFramework/Base/Actor/Camera/CameraMode.h"
#include "GameFramework/Base/Actor/Camera/CameraPreset.h"
#include "GameFramework/Base/Actor/Camera/CameraShake.h"
#include "GameFramework/Base/Actor/Camera/CameraUserSettings.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

// 카메라 모드 — 3인칭 스프링 암 · 궤도 입력 · 제약 · CCTV 훑기 · 프레이밍 · 흔들림(잡음 · 충격) · 뷰 타깃 블렌드 · 컷 신호.

using namespace sw;

namespace
{
    struct CameraModeTestInternal
    {
        static float3 computeForward( const quaternion& rotation ) { return float3::transform( float3{ 0.0f, 0.0f, 1.0f }, rotation ); }

        /** @brief 직교 포즈의 화면 점에서 쏜 광선이 땅(Y = 0)에 닿는 점입니다. */
        static float3 hitGround( const CameraPose& pose, float32 aspect, const float2& mouse )
        {
            const GameRay ray = computeCameraScreenRay( pose, aspect, mouse );
            return ray._origin + ray._direction * ( -ray._origin._y / ray._direction._y );
        }

        static CameraPresetDef makeThirdPerson()
        {
            CameraPresetDef def;
            def._id                     = hashed_string( "third" );
            def._view._mode             = CameraPresetMode::ThirdPerson;
            def._view._offset           = float3{ 0.5f, 1.6f, 0.0f };
            def._view._distance         = 5.0f;
            def._collision._bEnabled    = true;
            def._collision._radius      = 0.2f;
            def._collision._minDistance = 0.3f;
            def._collision._recoverTime = 0.3f;
            return def;
        }

        static BlendCurveDef makeLinear( float32 duration )
        {
            BlendCurveDef blend;
            blend._curve    = BlendCurve::Linear;
            blend._duration = duration;
            return blend;
        }

        static float32 computeShakeMagnitude( const CameraShakeOffset& offset ) { return offset._position.getLength(); }
    };
} // namespace

/**
 * @brief [CameraModeTest] 3인칭 암은 벽에 막히면 바로 그 앞으로 당기고, 막힘이 풀리면 회복 시간으로 천천히 돌아간다
 * @details 피벗(어깨 0.5, 1.6, 0)에서 뒤로 5 m 를 쓸면 z = −2.2 의 벽에 반지름 0.2 구가 2.0 m 에서 닿는다. 당김이 한 프레임 늦으면 벽 안이 보이고,
 *          돌아갈 때 한 번에 튀면 화면이 뛴다.
 */
SW_TEST_CASE( CameraModeTest, ThirdPersonArmPullsInOnCollisionAndEasesBack )
{
    const CameraPresetDef   def = CameraModeTestInternal::makeThirdPerson();
    CameraBoxCollisionProbe probe;
    probe.addBox( AABB{
        float3{-10.0f, -10.0f, -2.5f},
        float3{ 10.0f,  10.0f, -2.2f}
    } );
    CameraTarget target; // 원점에서 +Z 를 본다

    CameraModeState  state;
    const CameraPose free = evaluateCameraMode( def, target, 0.0f, state, nullptr );
    SW_EXPECT_NEAR_EQUAL( -5.0f, free._position._z, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, free._position._x, 1.0e-4f );

    state.reset();
    const CameraPose blocked = evaluateCameraMode( def, target, 1.0f / 60.0f, state, &probe );
    SW_EXPECT_TRUE( state._bArmBlocked == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( -2.0f, blocked._position._z, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.6f, blocked._position._y, 1.0e-4f );

    // 벽이 사라지면 한 프레임에 다 돌아가지 않는다 — 회복 시간 상수(0.3 s)로 다가간다.
    probe.clear();
    const CameraPose easing = evaluateCameraMode( def, target, 0.1f, state, &probe );
    SW_EXPECT_TRUE( state._bArmBlocked == SW_FALSE );
    const float32 expected = 2.0f + 3.0f * ( 1.0f - ::expf( -0.1f / 0.3f ) );
    SW_EXPECT_NEAR_EQUAL( -expected, easing._position._z, 1.0e-3f );
    for ( int32 stepIndex = 0; stepIndex < 60; ++stepIndex )
    {
        (void)evaluateCameraMode( def, target, 0.1f, state, &probe );
    }
    SW_EXPECT_NEAR_EQUAL( 5.0f, state._armLength, 1.0e-3f );
}

/**
 * @brief [CameraModeTest] 3인칭은 대상의 시점을 따른다 — 대상이 +X 를 보면 카메라는 −X 쪽 뒤, 어깨 오프셋도 대상 요로 돈다
 */
SW_TEST_CASE( CameraModeTest, ThirdPersonFollowsTheTargetLook )
{
    CameraPresetDef def      = CameraModeTestInternal::makeThirdPerson();
    def._collision._bEnabled = false;
    CameraTarget target;
    target._focus         = float3{ 10.0f, 0.0f, 0.0f };
    target._yaw           = MathUtil::kHalfPi;
    const CameraPose pose = evaluatePreset( def, target );
    // 어깨(0.5 오른쪽)는 +X 를 보는 대상에게 −Z 쪽이다.
    SW_EXPECT_NEAR_EQUAL( 10.0f - 5.0f, pose._position._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( -0.5f, pose._position._z, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, CameraModeTestInternal::computeForward( pose._rotation )._x, 1.0e-4f );
}

/**
 * @brief [CameraModeTest] 궤도 모드는 마우스로 돌고 휠로 당기며, 제약이 피치 · 거리를 자른다
 */
SW_TEST_CASE( CameraModeTest, OrbitInputTurnsZoomsAndTheConfinerClamps )
{
    CameraPresetDef def;
    def._view._mode             = CameraPresetMode::Orbit;
    def._view._distance         = 10.0f;
    def._view._pitch            = 20.0f * MathUtil::kDegreeToRadian;
    def._input._lookSensitivity = 0.01f;
    def._input._zoomStep        = 0.5f;
    def._confiner._pitchMin     = -10.0f * MathUtil::kDegreeToRadian;
    def._confiner._pitchMax     = 60.0f * MathUtil::kDegreeToRadian;
    def._confiner._zoomMin      = 2.0f;
    def._confiner._zoomMax      = 20.0f;
    const CameraTarget target;

    CameraModeState state;
    CameraModeInput input;
    input._lookDelta = float2{ 100.0f, 1000.0f }; // 요 +1 rad, 피치는 한계 너머로 끈다
    applyCameraInput( def, input, 1.0f / 60.0f, state );
    SW_EXPECT_NEAR_EQUAL( 1.0f, state._yawOffset, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 60.0f * MathUtil::kDegreeToRadian, def._view._pitch + state._pitchOffset, 1.0e-5f );

    CameraModeInput zoom;
    zoom._zoomNotches = 3.0f; // 10 × 0.5³ = 1.25 → 아래 한계 2
    applyCameraInput( def, zoom, 1.0f / 60.0f, state );
    SW_EXPECT_NEAR_EQUAL( 2.0f, state._zoom, 1.0e-5f );
    zoom._zoomNotches = -10.0f; // 2 × 2¹⁰ → 위 한계 20
    applyCameraInput( def, zoom, 1.0f / 60.0f, state );
    SW_EXPECT_NEAR_EQUAL( 20.0f, state._zoom, 1.0e-4f );

    const CameraPose pose = evaluateCameraMode( def, target, 1.0f / 60.0f, state, nullptr );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pose._position.getLength(), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 20.0f * MathUtil::sin( 60.0f * MathUtil::kDegreeToRadian ), pose._position._y, 1.0e-3f );
    const float2 angles = computeLookAngles( pose._position, target._focus );
    SW_EXPECT_NEAR_EQUAL( 1.0f, angles._x, 1.0e-4f );
}

/**
 * @brief [CameraModeTest] 제약 상자는 카메라 자리를 가두고, 직교 시점에서는 초점의 X · Z 를 가둔다
 */
SW_TEST_CASE( CameraModeTest, ConfinerBoundsKeepTheCameraInTheBox )
{
    CameraPresetDef orbit;
    orbit._view._mode          = CameraPresetMode::Orbit;
    orbit._view._distance      = 30.0f;
    orbit._confiner._bBounds   = true;
    orbit._confiner._boundsMin = float3{ -10.0f, 0.0f, -10.0f };
    orbit._confiner._boundsMax = float3{ 10.0f, 20.0f, 10.0f };
    const CameraPose orbitPose = evaluatePreset( orbit, CameraTarget{} );
    SW_EXPECT_NEAR_EQUAL( -10.0f, orbitPose._position._z, 1.0e-4f ); // 30 m 뒤 → 상자 벽

    CameraPresetDef ortho = orbit;
    ortho._view._mode     = CameraPresetMode::OrthoTopDown;
    ortho._view._pitch    = MathUtil::kHalfPi;
    ortho._view._distance = 100.0f;
    CameraTarget farAway;
    farAway._focus             = float3{ 50.0f, 0.0f, -40.0f };
    const CameraPose orthoPose = evaluatePreset( ortho, farAway );
    SW_EXPECT_NEAR_EQUAL( 10.0f, orthoPose._position._x, 1.0e-3f ); // 초점이 상자 모서리로 묶이고 카메라는 그 위 100 m
    SW_EXPECT_NEAR_EQUAL( -10.0f, orthoPose._position._z, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, orthoPose._position._y, 1.0e-3f );
}

/**
 * @brief [CameraModeTest] CCTV(고정 · 점 보기)는 그 점을 보고, 훑기는 요를 사인으로 ±진폭 오간다
 */
SW_TEST_CASE( CameraModeTest, FixedCameraLooksAtItsPointAndSweeps )
{
    CameraPresetDef def;
    def._view._mode          = CameraPresetMode::Fixed;
    def._view._offset        = float3{ 0.0f, 5.0f, 0.0f };
    def._view._aim           = CameraAimMode::Point;
    def._view._lookAt        = float3{ 0.0f, 0.0f, 10.0f };
    def._sweep._yawAmplitude = 30.0f * MathUtil::kDegreeToRadian;
    def._sweep._period       = 4.0f;
    const CameraTarget target;
    CameraModeState    state;

    const CameraPose start      = evaluateCameraMode( def, target, 0.0f, state, nullptr );
    const float2     startAngle = computeLookAngles( start._position, start._position + CameraModeTestInternal::computeForward( start._rotation ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, startAngle._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( MathUtil::atan2( 5.0f, 10.0f ), startAngle._y, 1.0e-4f ); // 아래를 본다
    SW_EXPECT_NEAR_EQUAL( 5.0f, start._position._y, 1.0e-6f );

    const CameraPose quarter      = evaluateCameraMode( def, target, 1.0f, state, nullptr ); // 주기 1/4 → +진폭
    const float2     quarterAngle = computeLookAngles( quarter._position, quarter._position + CameraModeTestInternal::computeForward( quarter._rotation ) );
    SW_EXPECT_NEAR_EQUAL( 30.0f * MathUtil::kDegreeToRadian, quarterAngle._x, 1.0e-3f );
    const CameraPose threeQuarter = evaluateCameraMode( def, target, 2.0f, state, nullptr ); // 주기 3/4 → −진폭
    const float2     backAngle    = computeLookAngles( threeQuarter._position,
                                                       threeQuarter._position + CameraModeTestInternal::computeForward( threeQuarter._rotation ) );
    SW_EXPECT_NEAR_EQUAL( -30.0f * MathUtil::kDegreeToRadian, backAngle._x, 1.0e-3f );
}

/**
 * @brief [CameraModeTest] 프레이밍 — 데드존 안의 움직임은 조준을 돌리지 않고, 그 밖으로 나가면 돌리며, 소프트존 밖으로는 한 프레임도 나가지 않는다
 */
SW_TEST_CASE( CameraModeTest, FramingDeadZoneHoldsAndSoftZoneLimits )
{
    CameraPresetDef def;
    def._view._mode         = CameraPresetMode::Fixed;
    def._view._aim          = CameraAimMode::Target;
    def._lens._fieldOfViewY = 60.0f * MathUtil::kDegreeToRadian;
    def._framing._bCompose  = true;
    def._framing._deadZone  = float2{ 0.2f, 0.2f };
    def._framing._softZone  = float2{ 0.5f, 0.5f };
    def._framing._damping   = 10.0f; // 아주 느리게 — 소프트존이 막는지 본다
    CameraTarget target;
    target._focus = float3{ 0.0f, 0.0f, 10.0f };
    CameraModeState state;
    (void)evaluateCameraMode( def, target, 0.0f, state, nullptr );
    SW_EXPECT_NEAR_EQUAL( 0.0f, state._aimYaw, 1.0e-5f );

    target._focus = float3{ 0.5f, 0.0f, 10.0f }; // 약 2.9° — 데드존(약 ±11.5°) 안
    (void)evaluateCameraMode( def, target, 0.1f, state, nullptr );
    SW_EXPECT_NEAR_EQUAL( 0.0f, state._aimYaw, 1.0e-5f );

    target._focus = float3{ 10.0f, 0.0f, 10.0f }; // 45° — 소프트존(약 ±26°) 밖
    (void)evaluateCameraMode( def, target, 0.1f, state, nullptr );
    const float32 tanHalfX = MathUtil::tan( 30.0f * MathUtil::kDegreeToRadian ) * ( 16.0f / 9.0f );
    const float32 softHalf = MathUtil::atan2( 0.5f * tanHalfX, 1.0f );
    SW_EXPECT_NEAR_EQUAL( MathUtil::kPi * 0.25f - softHalf, state._aimYaw, 1.0e-4f );
}

/**
 * @brief [CameraModeTest] 프레이밍의 가로 존은 대상이 실어 온 화면 비율로 센다 — 정사각형 화면은 16:9 보다 가로 소프트존이 좁다
 * @details 비율을 16:9 로 고정하면 세로로 긴 화면(휴대폰 세로, 렌더 텍스처)에서 대상이 화면 밖으로 나가도 카메라가 돌지 않는다.
 */
SW_TEST_CASE( CameraModeTest, FramingHorizontalZonesFollowTheAspectRatio )
{
    CameraPresetDef def;
    def._view._mode         = CameraPresetMode::Fixed;
    def._view._aim          = CameraAimMode::Target;
    def._lens._fieldOfViewY = 60.0f * MathUtil::kDegreeToRadian;
    def._framing._bCompose  = true;
    def._framing._deadZone  = float2{ 0.0f, 0.0f };
    def._framing._softZone  = float2{ 0.5f, 0.5f };
    def._framing._damping   = 10.0f;
    for ( const float32 aspect : { 1.0f, 2.0f } )
    {
        CameraTarget target;
        target._aspectRatio = aspect;
        target._focus       = float3{ 0.0f, 0.0f, 10.0f };
        CameraModeState state;
        (void)evaluateCameraMode( def, target, 0.0f, state, nullptr );
        target._focus = float3{ 10.0f, 0.0f, 10.0f }; // 45°
        (void)evaluateCameraMode( def, target, 0.1f, state, nullptr );
        const float32 softHalf = MathUtil::atan2( 0.5f * MathUtil::tan( 30.0f * MathUtil::kDegreeToRadian ) * aspect, 1.0f );
        SW_EXPECT_NEAR_EQUAL( MathUtil::kPi * 0.25f - softHalf, state._aimYaw, 1.0e-4f );
    }
}

/**
 * @brief [CameraModeTest] look-ahead 는 대상이 가는 쪽으로 조준을 미리 옮기고, 그룹 맞추기는 묶음이 화면에 들어올 만큼 물러난다
 */
SW_TEST_CASE( CameraModeTest, LookAheadLeadsAndGroupFramingBacksOff )
{
    CameraPresetDef def;
    def._view._mode                  = CameraPresetMode::Orbit;
    def._view._distance              = 5.0f;
    def._framing._lookAheadTime      = 0.5f;
    def._framing._lookAheadSmoothing = 0.0f;
    CameraTarget    target;
    CameraModeState state;
    for ( int32 stepIndex = 0; stepIndex < 10; ++stepIndex )
    {
        target._focus = float3{ static_cast<float32>( stepIndex ) * 0.1f, 0.0f, 0.0f }; // +X 로 1 m/s
        (void)evaluateCameraMode( def, target, 0.1f, state, nullptr );
    }
    SW_EXPECT_NEAR_EQUAL( 0.5f, state._lookAhead._x, 1.0e-3f ); // 속도 1 × 0.5 s

    CameraPresetDef group         = def;
    group._framing._lookAheadTime = 0.0f;
    group._framing._groupPadding  = 1.5f;
    group._lens._fieldOfViewY     = 60.0f * MathUtil::kDegreeToRadian;
    const float3 arrPoint[]       = {
        float3{-4.0f, 0.0f, 0.0f},
        float3{ 4.0f, 0.0f, 0.0f},
        float3{ 0.0f, 0.0f, 3.0f}
    };
    const CameraTarget groupTarget = makeGroupCameraTarget( arrPoint, 3 );
    SW_EXPECT_NEAR_EQUAL( 1.5f, groupTarget._focus._z, 1.0e-5f );
    const CameraPose groupPose = evaluatePreset( group, groupTarget );
    const float32    expected  = groupTarget._groupRadius * 1.5f / MathUtil::sin( 30.0f * MathUtil::kDegreeToRadian );
    SW_EXPECT_NEAR_EQUAL( expected, ( groupPose._position - groupTarget._focus ).getLength(), 1.0e-3f );
}

/**
 * @brief [CameraModeTest] 손떨림 잡음은 같은 시드 · 같은 시간이면 같고, 시드가 다르면 다르며, 진폭 안에서 매끄럽게 움직인다
 */
SW_TEST_CASE( CameraModeTest, NoiseIsDeterministicBoundedAndSmooth )
{
    CameraNoiseDef noise;
    noise._positionAmplitude = float3{ 0.1f, 0.2f, 0.0f };
    noise._rotationAmplitude = float3{ 0.05f, 0.05f, 0.02f };
    noise._frequency         = 2.0f;
    noise._seed              = 42;
    CameraNoiseDef other     = noise;
    other._seed              = 43;

    bool    bAnyDifferent = false;
    float32 maxStep       = 0.0f;
    float32 maxAbs        = 0.0f;
    for ( int32 sampleIndex = 0; sampleIndex < 600; ++sampleIndex )
    {
        const float32           time  = static_cast<float32>( sampleIndex ) / 120.0f;
        const CameraShakeOffset first = computeCameraNoise( noise, time );
        const CameraShakeOffset again = computeCameraNoise( noise, time );
        SW_EXPECT_NEAR_EQUAL( first._position._x, again._position._x, 0.0f );
        SW_EXPECT_NEAR_EQUAL( first._rotation._z, again._rotation._z, 0.0f );
        if ( MathUtil::abs( first._position._y - computeCameraNoise( other, time )._position._y ) > 1.0e-3f )
            bAnyDifferent = true;
        const CameraShakeOffset next = computeCameraNoise( noise, time + 1.0f / 120.0f );
        maxStep                      = MathUtil::max( maxStep, MathUtil::abs( next._position._y - first._position._y ) );
        maxAbs                       = MathUtil::max( maxAbs, MathUtil::abs( first._position._y ) );
        SW_EXPECT_NEAR_EQUAL( 0.0f, first._position._z, 0.0f ); // 진폭 0 인 채널은 움직이지 않는다
    }
    SW_EXPECT_TRUE_MSG( bAnyDifferent, "시드가 달라도 같은 흔들림입니다" );
    SW_EXPECT_TRUE( maxAbs <= 0.2f + 1.0e-4f );
    SW_EXPECT_TRUE_MSG( maxAbs > 0.05f, "흔들림이 거의 없습니다" );
    SW_EXPECT_TRUE_MSG( maxStep < 0.02f, "잡음이 한 프레임에 튑니다(매끄럽지 않다)" );
}

/**
 * @brief [CameraModeTest] 충격은 시간에 따라 0 으로 잦아들어 길이 끝에 정확히 0 이고, 원점에서 멀수록 약하며 반지름 밖에서는 0 이다
 */
SW_TEST_CASE( CameraModeTest, ImpulseDecaysAndFallsOffWithDistance )
{
    CameraImpulseDef def;
    def._amplitude     = 1.0f;
    def._duration      = 1.0f;
    def._decayTime     = 0.3f;
    def._falloffRadius = 10.0f;
    def._frequency     = 40.0f;

    CameraImpulse impulse;
    impulse._def             = def;
    float32 previousEnvelope = impulse.computeEnvelope();
    SW_EXPECT_NEAR_EQUAL( 1.0f, previousEnvelope, 1.0e-6f );
    for ( int32 stepIndex = 1; stepIndex <= 20; ++stepIndex )
    {
        impulse._elapsed       = static_cast<float32>( stepIndex ) * 0.05f;
        const float32 envelope = impulse.computeEnvelope();
        SW_EXPECT_TRUE( envelope <= previousEnvelope );
        previousEnvelope = envelope;
    }
    SW_EXPECT_NEAR_EQUAL( 0.0f, previousEnvelope, 1.0e-6f );
    impulse._elapsed = 0.5f; // 남은 비율 0.5 × e^(−0.5/0.3)
    SW_EXPECT_NEAR_EQUAL( 0.5f * ::expf( -0.5f / 0.3f ), impulse.computeEnvelope(), 1.0e-5f );

    CameraImpulseListener listener;
    listener.addImpulse( def, float3{ 0.0f, 0.0f, 0.0f } );
    listener.step( 0.05f );
    float32 nearPeak = 0.0f;
    float32 midPeak  = 0.0f;
    for ( int32 stepIndex = 0; stepIndex < 10; ++stepIndex )
    {
        nearPeak = MathUtil::max( nearPeak, CameraModeTestInternal::computeShakeMagnitude( listener.computeOffset( float3{ 0.0f, 0.0f, 0.0f } ) ) );
        midPeak  = MathUtil::max( midPeak, CameraModeTestInternal::computeShakeMagnitude( listener.computeOffset( float3{ 5.0f, 0.0f, 0.0f } ) ) );
        SW_EXPECT_NEAR_EQUAL( 0.0f, CameraModeTestInternal::computeShakeMagnitude( listener.computeOffset( float3{ 15.0f, 0.0f, 0.0f } ) ), 0.0f );
        listener.step( 0.01f );
    }
    SW_EXPECT_TRUE( nearPeak > 0.1f );
    SW_EXPECT_NEAR_EQUAL( nearPeak * 0.5f, midPeak, nearPeak * 0.05f ); // 반지름의 반 → 반
    listener.step( 2.0f );
    SW_EXPECT_FALSE( listener.isActive() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, CameraModeTestInternal::computeShakeMagnitude( listener.computeOffset( float3{} ) ), 0.0f );
}

/**
 * @brief [CameraModeTest] 뷰 타깃 블렌드는 튀지 않는다 — 블렌드 도중 다른 타깃으로 바꾸면 그 순간의 화면에서 이어 가고, 컷은 카메라에 컷 표시를 남긴다
 * @details 매니저 카메라가 A(원점) → B(+X 10) 를 1 초 직선으로 반쯤 섞은 자리(5, 0, 0)에서 C(+Y 10)로 바꾼다. 다음 1 ms 의 자리가 (5, 0, 0) 곁이어야 한다 —
 *          출발점을 B · A 로 잡으면 5 m 를 한 번에 뛴다.
 */
SW_TEST_CASE( CameraModeTest, ViewTargetBlendContinuesFromTheCurrentPose )
{
    GameObjectManager manager;
    GameObject*       pA      = manager.createGameObject( hashed_string( "ViewA" ) );
    GameObject*       pB      = manager.createGameObject( hashed_string( "ViewB" ) );
    GameObject*       pC      = manager.createGameObject( hashed_string( "ViewC" ) );
    GameObject*       pPlayer = manager.createGameObject( hashed_string( "PlayerCamera" ) );
    SW_ASSERT_TRUE( pA != nullptr && pB != nullptr && pC != nullptr && pPlayer != nullptr );
    CameraComponent* pCameraA = pA->addComponent<CameraComponent>();
    CameraComponent* pCameraB = pB->addComponent<CameraComponent>();
    CameraComponent* pCameraC = pC->addComponent<CameraComponent>();
    CameraComponent* pOutput  = pPlayer->addComponent<CameraComponent>();
    SW_ASSERT_TRUE( pCameraA != nullptr && pCameraB != nullptr && pCameraC != nullptr && pOutput != nullptr );
    pCameraA->setRole( CameraRole::Custom );
    pCameraB->setRole( CameraRole::Custom );
    pCameraC->setRole( CameraRole::Custom );
    pCameraB->setLocalPosition( float3{ 10.0f, 0.0f, 0.0f } );
    pCameraC->setLocalPosition( float3{ 0.0f, 10.0f, 0.0f } );
    pCameraC->setFieldOfViewY( 1.2f );
    CameraManagerComponent* pCameraManager = pPlayer->addComponent<CameraManagerComponent>();
    SW_ASSERT_NOT_NULL( pCameraManager );
    pCameraManager->setViewTarget( pA->getHandle() );
    pCameraManager->updateCamera( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pOutput->getWorldPosition()._x, 1.0e-5f );
    SW_EXPECT_FALSE( pCameraManager->isBlending() ); // 처음 타깃은 바로 붙는다
    (void)pOutput->consumeCut();

    pCameraManager->setViewTarget( pB->getHandle(), CameraModeTestInternal::makeLinear( 1.0f ) );
    pCameraManager->updateCamera( 0.5f );
    SW_EXPECT_TRUE( pCameraManager->isBlending() );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pOutput->getWorldPosition()._x, 1.0e-3f );

    pCameraManager->setViewTarget( pC->getHandle(), CameraModeTestInternal::makeLinear( 1.0f ) );
    pCameraManager->updateCamera( 0.001f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pOutput->getWorldPosition()._x, 0.02f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pOutput->getWorldPosition()._y, 0.02f );
    SW_EXPECT_FALSE( pOutput->consumeCut() );

    pCameraManager->updateCamera( 1.0f );
    SW_EXPECT_FALSE( pCameraManager->isBlending() );
    SW_EXPECT_NEAR_EQUAL( 10.0f, pOutput->getWorldPosition()._y, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.2f, pOutput->getFieldOfViewY(), 1.0e-5f ); // 렌즈도 타깃의 것

    // 블렌드 없는 전환은 렌더러에 컷을 알린다(TAA 기록 버리기).
    BlendCurveDef cut;
    cut._curve = BlendCurve::Cut;
    pCameraManager->setViewTarget( pA->getHandle(), cut );
    pCameraManager->updateCamera( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pOutput->getWorldPosition()._y, 1.0e-4f );
    SW_EXPECT_TRUE( pOutput->consumeCut() );
    SW_EXPECT_FALSE( pOutput->consumeCut() );

    // 용도별 목록 — 보조 셋, 플레이어 시점 하나.
    vector<CameraComponent*> listAuxiliary;
    CameraManagerComponent::findCamerasByRole( manager, CameraRole::Custom, listAuxiliary );
    SW_EXPECT_EQUAL( size_t{ 3 }, listAuxiliary.size() );
    SW_EXPECT_TRUE( CameraManagerComponent::findForPlayer( manager, 0 ) == pCameraManager );
    SW_EXPECT_TRUE( pCameraManager->cycleViewTarget( CameraRole::Custom ) == pB->getHandle() );
}

/**
 * @brief [CameraModeTest] 디렉터는 포즈를 내던 중의 컷(블렌드 없는 전환)을 한 번 알리고, 블렌드 전환 · 처음 켠 프리셋은 알리지 않는다
 */
SW_TEST_CASE( CameraModeTest, DirectorReportsCutsOnce )
{
    CameraPresetDef a;
    a._id             = hashed_string( "a" );
    a._view._mode     = CameraPresetMode::Fixed;
    CameraPresetDef b = a;
    b._id             = hashed_string( "b" );
    b._view._offset   = float3{ 3.0f, 0.0f, 0.0f };
    BlendCurveDef cut;
    cut._curve = BlendCurve::Cut;

    CameraDirector director;
    director.activatePreset( a, cut );
    (void)director.step( 0.016f, CameraTarget{} );
    SW_EXPECT_FALSE( director.consumeCut() ); // 처음 붙는 것은 컷이 아니다(지난 화면이 없다)
    director.activatePreset( b, CameraModeTestInternal::makeLinear( 0.5f ) );
    (void)director.step( 0.016f, CameraTarget{} );
    SW_EXPECT_FALSE( director.consumeCut() );
    (void)director.step( 1.0f, CameraTarget{} );
    director.activatePreset( a, cut );
    (void)director.step( 0.016f, CameraTarget{} );
    SW_EXPECT_TRUE( director.consumeCut() );
    SW_EXPECT_FALSE( director.consumeCut() );
}

/**
 * @brief [CameraModeTest] XY 평면 따라가기(2D)는 초점의 X · Y 를 따르고 Z 는 카메라 평면에 두며, 이동 키는 화면 위(+Y) · 오른쪽(+X)으로 민다
 * @details 땅(XZ) 기준 모드로 2D 씬을 풀면 카메라가 초점 위(+Y)로 올라가 평면을 옆에서 본다.
 */
SW_TEST_CASE( CameraModeTest, FollowXYTracksThePlaneAndKeepsTheCameraPlane )
{
    CameraPresetDef def;
    def._view._mode          = CameraPresetMode::FollowXY;
    def._view._offset        = float3{ 0.0f, 1.0f, -10.0f };
    def._input._panSpeed     = 2.0f;
    def._confiner._bBounds   = true;
    def._confiner._boundsMin = float3{ -100.0f, -100.0f, -100.0f };
    def._confiner._boundsMax = float3{ 8.0f, 100.0f, 100.0f };
    CameraTarget target;
    target._focus = float3{ 5.0f, 3.0f, 0.0f };

    CameraModeState  state;
    const CameraPose pose = evaluateCameraMode( def, target, 0.0f, state, nullptr );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pose._position._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, pose._position._y, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( -10.0f, pose._position._z, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, CameraModeTestInternal::computeForward( pose._rotation )._z, 1.0e-5f ); // 평면을 본다

    CameraModeInput input;
    input._pan = float2{ 1.0f, 0.0f }; // 앞(화면 위) 1 초
    applyCameraInput( def, input, 1.0f, state );
    SW_EXPECT_NEAR_EQUAL( 2.0f, state._panOffset._y, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, state._panOffset._z, 1.0e-5f );

    target._focus            = float3{ 20.0f, 3.0f, 0.0f }; // 상자 밖 — X 만 묶인다
    const CameraPose bounded = evaluateCameraMode( def, target, 0.0f, state, nullptr );
    SW_EXPECT_NEAR_EQUAL( 8.0f, bounded._position._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 6.0f, bounded._position._y, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( -10.0f, bounded._position._z, 1.0e-5f );
}

/**
 * @brief [CameraModeTest] 2D 디렉터 — 감쇠로 대상을 따라가고, 충격은 실제로 떨리다가(부호가 바뀐다) 끝나면 따라가던 자리로 정확히 돌아온다
 * @details 흔들림은 포즈 위에 마지막으로 더하는 오프셋이라 다음 프레임 포즈에 남지 않는다. 트랜스폼에 흔들림을 더해 두고 다음 틱에 다시 빼는 방식은
 *          상태를 다시 읽으면(핫 리로드) 그 오프셋이 영영 남는다.
 */
SW_TEST_CASE( CameraModeTest, FollowXYDirectorDampsAndShakesBackToItsPlace )
{
    CameraPresetDef def;
    def._id                    = hashed_string( "side" );
    def._view._mode            = CameraPresetMode::FollowXY;
    def._view._offset          = float3{ 0.0f, 0.0f, -10.0f };
    def._lens._bOrthographic   = true;
    def._damping._positionTime = 0.1f;
    BlendCurveDef cut;
    cut._curve = BlendCurve::Cut;

    CameraDirector director;
    director.activatePreset( def, cut );
    CameraTarget target;
    target._focus          = float3{ 5.0f, 3.0f, 0.0f };
    const CameraPose start = director.step( 1.0f / 60.0f, target );
    SW_EXPECT_NEAR_EQUAL( 5.0f, start._position._x, 1.0e-5f ); // 새로 켠 프리셋은 그 자리에서 시작한다
    SW_EXPECT_TRUE( start._bOrthographic == SW_TRUE );

    target._focus            = float3{ 20.0f, 3.0f, 0.0f };
    const CameraPose lagging = director.step( 1.0f / 60.0f, target );
    SW_EXPECT_TRUE( lagging._position._x > 5.0f && lagging._position._x < 20.0f ); // 감쇠 — 한 프레임에 붙지 않는다
    for ( int32 frameIndex = 0; frameIndex < 120; ++frameIndex )
    {
        (void)director.step( 1.0f / 60.0f, target );
    }
    SW_EXPECT_NEAR_EQUAL( 20.0f, director.getPose()._position._x, 1.0e-3f );

    CameraImpulseDef impulse;
    impulse._amplitude = 2.0f;
    impulse._duration  = 0.5f;
    impulse._frequency = 40.0f;
    director.addImpulse( impulse, target._focus );
    bool bSawPositive = false;
    bool bSawNegative = false;
    for ( int32 frameIndex = 0; frameIndex < 20; ++frameIndex )
    {
        const float32 offsetX = director.step( 1.0f / 60.0f, target )._position._x - 20.0f;
        bSawPositive          = bSawPositive || offsetX > 0.05f;
        bSawNegative          = bSawNegative || offsetX < -0.05f;
    }
    SW_EXPECT_TRUE_MSG( bSawPositive && bSawNegative, "흔들리지 않고 한쪽으로 밀려 있기만 합니다" );
    for ( int32 frameIndex = 0; frameIndex < 30; ++frameIndex )
    {
        (void)director.step( 1.0f / 60.0f, target );
    }
    SW_EXPECT_NEAR_EQUAL( 20.0f, director.getPose()._position._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, director.getPose()._position._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( -10.0f, director.getPose()._position._z, 1.0e-4f );
}

/**
 * @brief [CameraModeTest] 렌즈 물리값 — 초점 길이가 시야각을 정하고, 더치는 화면을 기울이며, 초점 · 조리개 · 시프트 · 후처리는 포즈에 실려 섞인다
 */
SW_TEST_CASE( CameraModeTest, LensPhysicalValuesReachThePose )
{
    CameraPresetDef def;
    def._view._mode                        = CameraPresetMode::Fixed;
    def._lens._focalLength                 = 50.0f;
    def._lens._sensorSize                  = float2{ 36.0f, 24.0f };
    def._lens._dutch                       = 20.0f * MathUtil::kDegreeToRadian;
    def._lens._focusDistance               = 3.5f;
    def._lens._aperture                    = 1.4f;
    def._lens._lensShift                   = float2{ 0.0f, 0.1f };
    def._postProcess._exposureCompensation = 1.0f;
    def._postProcess._letterboxAspectRatio = 2.39f;
    const CameraPose pose                  = evaluatePreset( def, CameraTarget{} );
    SW_EXPECT_NEAR_EQUAL( 2.0f * MathUtil::atan2( 12.0f, 50.0f ), pose._fieldOfViewY, 1.0e-5f ); // 50 mm 풀 프레임 ≈ 27°
    SW_EXPECT_NEAR_EQUAL( 3.5f, pose._focusDistance, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.4f, pose._aperture, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.1f, pose._lensShift._y, 1.0e-6f );
    const float3 up = float3::transform( float3{ 0.0f, 1.0f, 0.0f }, pose._rotation );
    SW_EXPECT_NEAR_EQUAL( MathUtil::cos( 20.0f * MathUtil::kDegreeToRadian ), up._y, 1.0e-4f ); // 앞축으로 기울었다
    SW_EXPECT_NEAR_EQUAL( 1.0f, CameraModeTestInternal::computeForward( pose._rotation )._z, 1.0e-4f );

    CameraPose       plain;
    const CameraPose half = blendPoses( plain, pose, 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, half._postProcess._exposureCompensation, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 2.39f, half._postProcess._letterboxAspectRatio, 1.0e-6f ); // 한쪽에만 있으면 절반에서 바꾼다
    SW_EXPECT_NEAR_EQUAL( 0.0f, blendPoses( plain, pose, 0.4f )._postProcess._letterboxAspectRatio, 1.0e-6f );
}

/**
 * @brief [CameraModeTest] 시야각 킥은 오르고 버틴 뒤 0 으로 내려가며(끝에서 정확히 1 배), 줌 펀치는 좁힌다
 */
SW_TEST_CASE( CameraModeTest, FieldOfViewKickRisesHoldsAndReleases )
{
    CameraFieldOfViewKickDef kick;
    kick._amount      = 0.2f;
    kick._attackTime  = 0.1f;
    kick._holdTime    = 0.1f;
    kick._releaseTime = 0.2f;
    CameraFieldOfViewKickListener listener;
    listener.addKick( kick );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listener.computeScale(), 1.0e-6f );
    listener.step( 0.05f );
    SW_EXPECT_NEAR_EQUAL( 1.1f, listener.computeScale(), 1.0e-4f ); // 오르기 절반 — SmoothStep 은 0.5
    listener.step( 0.1f );
    SW_EXPECT_NEAR_EQUAL( 1.2f, listener.computeScale(), 1.0e-5f ); // 버티기
    CameraPose pose;
    pose._fieldOfViewY = 1.0f;
    SW_EXPECT_NEAR_EQUAL( 1.2f, listener.applyToPose( pose )._fieldOfViewY, 1.0e-5f );
    listener.step( 0.3f );
    SW_EXPECT_FALSE( listener.isActive() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listener.computeScale(), 1.0e-6f );

    kick._amount = -0.5f; // 줌 펀치
    listener.addKick( kick );
    listener.step( 0.15f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listener.computeScale(), 1.0e-5f );
}

/**
 * @brief [CameraModeTest] 돌리 레일 — 카메라는 레일 위에서 대상에 가장 가까운 점에 서서 대상을 보고, 경로 오프셋만큼 레일을 따라 옮긴다
 */
SW_TEST_CASE( CameraModeTest, DollyRailFollowsTheNearestPoint )
{
    CameraPresetDef def;
    def._view._mode       = CameraPresetMode::Dolly;
    def._dolly._type      = SplineType::Linear;
    def._dolly._listPoint = {
        float3{-10.0f, 2.0f, -5.0f},
        float3{ 10.0f, 2.0f, -5.0f}
    };
    CameraTarget target;
    target._focus = float3{ 3.0f, 0.0f, 0.0f };
    CameraModeState  state;
    const CameraPose pose = evaluateCameraMode( def, target, 0.0f, state, nullptr );
    SW_EXPECT_TRUE( state._bDollyPathBuilt == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pose._position._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pose._position._y, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( -5.0f, pose._position._z, 1.0e-3f );
    const float3 forward = CameraModeTestInternal::computeForward( pose._rotation );
    float3       toFocus = target._focus - pose._position;
    toFocus.normalize();
    SW_EXPECT_NEAR_EQUAL( toFocus._z, forward._z, 1.0e-3f ); // 대상을 본다

    def._dolly._pathOffset = -2.0f;
    state.reset();
    target._focus = float3{ 50.0f, 0.0f, 0.0f }; // 레일 끝 너머 — 끝에서 오프셋만큼 뒤
    SW_EXPECT_NEAR_EQUAL( 8.0f, evaluateCameraMode( def, target, 0.0f, state, nullptr )._position._x, 1.0e-3f );
}

/**
 * @brief [CameraModeTest] 직교 포즈의 화면 광선 — 가운데는 초점에, 오른쪽 끝은 화면 반너비만큼 오른쪽, 위쪽 끝은 반높이 ÷ sin(피치)만큼 앞의 땅에 닿는다.
 *        원근 포즈는 화면 가운데가 앞 방향이다
 * @details 요 0(앞이 +Z)에서 화면 위쪽으로 h 옮긴 광선은 땅에서 h / sin(피치) 만큼 멀리 닿는다(위 벡터 (0, cos p, sin p) · 앞 (0, −sin p, cos p) 로 손으로 푼 값).
 *          요 90° 면 앞이 +X 이고 오른쪽은 −Z 다.
 */
SW_TEST_CASE( CameraModeTest, ScreenRayFromAnOrthoPoseHitsTheGroundUnderTheScreenPoint )
{
    using Internal                = CameraModeTestInternal;
    constexpr float32 kAspect     = 1.5f;
    constexpr float32 kHalfHeight = 20.0f;
    const float32     pitch       = 40.0f * MathUtil::kDegreeToRadian;
    CameraPresetDef   def;
    def._view._mode        = CameraPresetMode::OrthoTopDown;
    def._view._yaw         = 0.0f;
    def._view._pitch       = pitch;
    def._view._distance    = 100.0f;
    def._lens._orthoHeight = kHalfHeight * 2.0f;
    CameraTarget target;
    target._focus          = float3{ 3.0f, 0.0f, -2.0f };
    const CameraPose ortho = evaluatePreset( def, target );

    const float3 center = Internal::hitGround( ortho, kAspect, float2{ 0.5f, 0.5f } );
    SW_EXPECT_NEAR_EQUAL( 3.0f, center._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, center._z, 1.0e-3f );
    const float3 rightEdge = Internal::hitGround( ortho, kAspect, float2{ 1.0f, 0.5f } );
    SW_EXPECT_NEAR_EQUAL( 3.0f + kHalfHeight * kAspect, rightEdge._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, rightEdge._z, 1.0e-3f );
    const float3 topEdge = Internal::hitGround( ortho, kAspect, float2{ 0.5f, 0.0f } );
    SW_EXPECT_NEAR_EQUAL( 3.0f, topEdge._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( -2.0f + kHalfHeight / MathUtil::sin( pitch ), topEdge._z, 1.0e-3f );

    def._view._yaw                = MathUtil::kHalfPi;
    const CameraPose turned       = evaluatePreset( def, target );
    const float3     turnedCenter = Internal::hitGround( turned, kAspect, float2{ 0.5f, 0.5f } );
    SW_EXPECT_NEAR_EQUAL( 3.0f, turnedCenter._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, turnedCenter._z, 1.0e-3f );
    const float3 turnedRight = Internal::hitGround( turned, kAspect, float2{ 1.0f, 0.5f } );
    SW_EXPECT_NEAR_EQUAL( -2.0f - kHalfHeight * kAspect, turnedRight._z, 1.0e-3f );

    CameraPose perspective;
    perspective._rotation     = quaternion::makeFromYawPitchRoll( 0.0f, 0.0f, 0.0f );
    perspective._fieldOfViewY = MathUtil::kHalfPi;
    const GameRay centerRay   = computeCameraScreenRay( perspective, 1.0f, float2{ 0.5f, 0.5f } );
    SW_EXPECT_NEAR_EQUAL( 1.0f, centerRay._direction._z, 1.0e-5f );
    const GameRay rightEdgeRay = computeCameraScreenRay( perspective, 1.0f, float2{ 1.0f, 0.5f } );
    SW_EXPECT_NEAR_EQUAL( rightEdgeRay._direction._x, rightEdgeRay._direction._z, 1.0e-5f ); // 시야각 90° 의 가장자리는 45°
}

/**
 * @brief [CameraModeTest] 디렉터 컴포넌트의 땅 고르기와 시점 덮어쓰기 — 직교 시점 게임이 마우스 고르기 · 탑승 시점에 쓰는 함수
 * @details 위에서 내려다보는 직교 프리셋에서 화면 가운데는 초점 아래 땅이고, 덮어쓴 원근 시점은 그 시점으로 고르며, 덮어쓰기를 풀면 원래 프리셋으로 돌아간다.
 */
SW_TEST_CASE( CameraModeTest, DirectorComponentPicksTheGroundAndHonoursTheOverride )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "DirectedRig" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SW_ASSERT_NOT_NULL( pObject->addComponent<CameraComponent>() );
    CameraDirectorComponent* pDirector = pObject->addComponent<CameraDirectorComponent>();
    SW_ASSERT_NOT_NULL( pDirector );
    SW_ASSERT_TRUE( pDirector->getCatalog().loadFromXMLText( R"(<CameraPresets>
        <Preset id="topdown"><View mode="OrthoTopDown" pitch="90" yaw="0" distance="50"/><Lens orthographic="true" orthoHeight="20" far="200"/></Preset>
    </CameraPresets>)",
                                                             "ground-pick-test" ) );
    pDirector->setInputEnabled( false );
    BlendCurveDef cut;
    cut._curve = BlendCurve::Cut;
    pDirector->setOverrideBlend( cut );
    manager.beginPlay();

    float3 point{};
    SW_ASSERT_TRUE( pDirector->findGroundPoint( float2{ 0.5f, 0.5f }, 0.0f, point ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, point._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, point._z, 1.0e-3f );
    SW_ASSERT_TRUE( pDirector->findGroundPoint( float2{ 0.5f, 0.0f }, 0.0f, point ) ); // 화면 위 가장자리 — 요 0 이면 +Z 쪽 반 높이
    SW_EXPECT_NEAR_EQUAL( 10.0f, point._z, 1.0e-2f );

    pDirector->setViewOverride( float3{ 0.0f, 10.0f, -10.0f }, float3{ 45.0f * MathUtil::kDegreeToRadian, 0.0f, 0.0f }, 1.0f, 300.0f );
    SW_EXPECT_TRUE( pDirector->isViewOverridden() );
    pDirector->updateCamera( 0.016f );
    SW_ASSERT_TRUE( pDirector->findGroundPoint( float2{ 0.5f, 0.5f }, 0.0f, point ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, point._z, 1.0e-2f ); // (0, 10, −10) 에서 45° 아래 → 원점
    pDirector->setViewOverride( float3{ 0.0f, 10.0f, -20.0f }, float3{ 45.0f * MathUtil::kDegreeToRadian, 0.0f, 0.0f }, 1.0f, 300.0f );
    SW_EXPECT_FALSE( pDirector->isBlending() ); // 덮어쓰는 동안은 값만 바꾼다(블렌드를 새로 시작하지 않는다)

    pDirector->clearViewOverride();
    SW_EXPECT_FALSE( pDirector->isViewOverridden() );
    SW_EXPECT_TRUE( pDirector->getActivePresetID() == hashed_string( "topdown" ) );
    manager.endPlay();
}

/**
 * @brief [CameraModeTest] 직교 프리셋의 디렉터는 프리셋의 요에서 바로 시작하고(0 에서 돌아 들어오지 않는다) 초점 점(`setFocusPoint`)을 블렌드 없이 따라간다
 */
SW_TEST_CASE( CameraModeTest, OrthoDirectorStartsAtItsYawAndFollowsTheFocusPoint )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "TopDownCamera" ) );
    SW_ASSERT_NOT_NULL( pObject );
    CameraComponent*         pCamera   = pObject->addComponent<CameraComponent>();
    CameraDirectorComponent* pDirector = pObject->addComponent<CameraDirectorComponent>();
    SW_ASSERT_TRUE( pCamera != nullptr && pDirector != nullptr );
    SW_ASSERT_TRUE( pDirector->getCatalog().loadFromXMLText( R"(<CameraPresets>
        <Preset id="park"><View mode="OrthoTopDown" pitch="30" yaw="45" distance="250"/><Lens orthographic="true" orthoHeight="110" far="625"/></Preset>
    </CameraPresets>)",
                                                             "ortho-follow-test" ) );
    pDirector->setInputEnabled( false );
    manager.beginPlay();
    // 카메라는 초점의 반대쪽 위 — 요 45° · 피치 30° 로 보는 방향의 반대쪽으로 250 m.
    const float32 backX = -MathUtil::sin( 45.0f * MathUtil::kDegreeToRadian ) * MathUtil::cos( 30.0f * MathUtil::kDegreeToRadian ) * 250.0f;
    SW_EXPECT_NEAR_EQUAL( backX, pCamera->getWorldPosition()._x, 1.0e-2f ); // 시작은 돌아 들어오지 않는다

    pDirector->setFocusPoint( float3{ 1.0f, 0.0f, 0.0f } ); // 같은 프리셋 · 다른 초점은 블렌드 없이 그대로 따라간다
    pDirector->updateCamera( 0.016f );
    SW_EXPECT_FALSE( pDirector->isBlending() );
    SW_EXPECT_NEAR_EQUAL( 1.0f + backX, pCamera->getWorldPosition()._x, 1.0e-2f );
    manager.endPlay();
}

/**
 * @brief [CameraModeTest] 머리 흔들림은 걸은 거리로 위상이 가고(한 주기에 위아래 두 번), 빠르기에 따라 커지며, 멈추면 0 으로 돌아오고 순간 이동은 넣지 않는다
 * @details 4 m/s(기준 빠르기)로 2 초 걸으면 크기 비율이 1 에 붙어 위아래 흔들림의 최대가 진폭이다. 시간이 흐르지 않은 평가(틱 밖에서 눈 자리를 넣음)를 끼워도
 *          위상은 간 거리만큼만 간다 — 위상이 평가 횟수로 가면 같은 프레임에 두 번 부를 때 두 배로 흔들린다.
 */
SW_TEST_CASE( CameraModeTest, HeadBobFollowsTheDistanceWalkedAndSettlesWhenStopped )
{
    CameraHeadBobDef bob;
    bob._verticalAmplitude  = 0.05f;
    bob._lateralAmplitude   = 0.02f;
    bob._strideLength       = 1.6f;
    bob._referenceSpeed     = 4.0f;
    bob._blendTime          = 0.1f;
    constexpr float32 kStep = 1.0f / 60.0f;

    CameraModeState state;
    float3          focus{};
    float32         peakVertical = 0.0f;
    (void)computeCameraHeadBob( bob, focus, 0.0f, state );
    for ( int32 frameIndex = 0; frameIndex < 120; ++frameIndex )
    {
        focus._z += 4.0f * kStep;
        const CameraShakeOffset offset = computeCameraHeadBob( bob, focus, kStep, state );
        peakVertical                   = MathUtil::max( peakVertical, offset._position._y );
        SW_EXPECT_TRUE( MathUtil::abs( offset._position._x ) <= bob._lateralAmplitude + 1.0e-5f );
    }
    SW_EXPECT_NEAR_EQUAL( 1.0f, state._bobWeight, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( bob._verticalAmplitude, peakVertical, 2.0e-3f );
    // 8 m 를 걸었다 — 위상은 8 / 1.6 = 다섯 주기, 곧 0(감싼 각) 곁이다.
    SW_EXPECT_NEAR_EQUAL( 0.0f, MathUtil::sin( state._bobPhase ), 1.0e-3f );

    // 같은 거리를 시간 없는 평가 둘 + 시간 있는 평가 하나로 나눠도 위상은 같다.
    CameraModeState split      = state;
    CameraModeState whole      = state;
    float3          splitFocus = focus;
    splitFocus._z += 0.03f;
    (void)computeCameraHeadBob( bob, splitFocus, 0.0f, split );
    splitFocus._z += 0.03f;
    (void)computeCameraHeadBob( bob, splitFocus, kStep, split );
    (void)computeCameraHeadBob( bob, focus + float3{ 0.0f, 0.0f, 0.06f }, kStep, whole );
    SW_EXPECT_NEAR_EQUAL( whole._bobPhase, split._bobPhase, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( whole._bobWeight, split._bobWeight, 1.0e-5f );

    // 순간 이동(한 주기보다 먼 한 걸음)은 위상을 옮기지 않는다.
    const float32 phaseBefore = state._bobPhase;
    focus._z += 50.0f;
    (void)computeCameraHeadBob( bob, focus, 0.0f, state );
    SW_EXPECT_NEAR_EQUAL( phaseBefore, state._bobPhase, 1.0e-6f );

    // 멈추면 1 초 안에 0 으로 돌아온다.
    CameraShakeOffset settled{};
    for ( int32 frameIndex = 0; frameIndex < 60; ++frameIndex )
    {
        settled = computeCameraHeadBob( bob, focus, kStep, state );
    }
    SW_EXPECT_TRUE( state._bobWeight < 1.0e-3f );
    SW_EXPECT_TRUE( settled._position.getLength() < 1.0e-4f );

    // 진폭이 0 이면(기본) 흔들림도 상태 변화도 없다.
    CameraModeState untouched;
    SW_EXPECT_TRUE( computeCameraHeadBob( CameraHeadBobDef{}, float3{ 1.0f, 0.0f, 0.0f }, kStep, untouched )._position.getLength() == 0.0f );
    SW_EXPECT_TRUE( untouched._bHasBobFocus == SW_FALSE );
}

/**
 * @brief [CameraModeTest] 사용자 설정 — 시야각은 1인칭 · 3인칭에만 기본 시야각 대비 반각의 tan 비율로 걸리고, 흔들림 배율 0 은 손떨림을, 머리 흔들림 끔은 걷기 흔들림을 없앤다
 * @details 디렉터는 전역 변수를 읽지 않고 넣은 값(`setUserSettings`)만 쓴다. 궤도 · 고정 같은 저작 카메라의 렌즈는 그대로다.
 */
SW_TEST_CASE( CameraModeTest, DirectorAppliesTheUserFieldOfViewShakeAndHeadBobSettings )
{
    CameraUserSettings wide;
    wide._fieldOfViewScale = 1.5f;
    const float32 authored = 0.8f;
    const float32 expected = 2.0f * MathUtil::atan2( MathUtil::tan( authored * 0.5f ) * 1.5f, 1.0f );

    CameraPresetDef third      = CameraModeTestInternal::makeThirdPerson();
    third._collision._bEnabled = false;
    third._lens._fieldOfViewY  = authored;
    CameraPresetDef orbit      = third;
    orbit._id                  = hashed_string( "orbit" );
    orbit._view._mode          = CameraPresetMode::Orbit;
    BlendCurveDef cut;
    cut._curve = BlendCurve::Cut;

    CameraDirector director;
    director.setUserSettings( wide );
    director.activatePreset( third, cut );
    SW_EXPECT_NEAR_EQUAL( expected, director.step( 0.016f, CameraTarget{} )._fieldOfViewY, 1.0e-5f );
    director.activatePreset( orbit, cut );
    SW_EXPECT_NEAR_EQUAL( authored, director.step( 0.016f, CameraTarget{} )._fieldOfViewY, 1.0e-5f );

    // 흔들림 배율 0 — 손떨림이 있는 프리셋도 감쇠한 자리 그대로다.
    CameraPresetDef shaky           = orbit;
    shaky._id                       = hashed_string( "shaky" );
    shaky._noise._positionAmplitude = float3{ 0.5f, 0.5f, 0.5f };
    shaky._noise._frequency         = 2.0f;
    CameraUserSettings calm;
    calm._shakeScale = 0.0f;
    CameraDirector still;
    still.setUserSettings( calm );
    still.activatePreset( shaky, cut );
    const CameraPose calmPose = still.step( 0.3f, CameraTarget{} );
    SW_EXPECT_NEAR_EQUAL( 0.0f, ( calmPose._position - evaluatePreset( shaky, CameraTarget{} )._position ).getLength(), 1.0e-4f );

    // 머리 흔들림 끔 — 걷는 1인칭 프리셋이 눈 자리에서 움직이지 않는다.
    CameraPresetDef walker;
    walker._id                         = hashed_string( "walker" );
    walker._view._mode                 = CameraPresetMode::FirstPerson;
    walker._headBob._verticalAmplitude = 0.05f;
    CameraUserSettings noBob;
    noBob._bHeadBob = false;
    CameraDirector bobbing;
    CameraDirector steady;
    steady.setUserSettings( noBob );
    bobbing.activatePreset( walker, cut );
    steady.activatePreset( walker, cut );
    CameraTarget target;
    float32      maxBob    = 0.0f;
    float32      maxSteady = 0.0f;
    for ( int32 frameIndex = 0; frameIndex < 60; ++frameIndex )
    {
        target._focus._z += 4.0f / 60.0f;
        maxBob    = MathUtil::max( maxBob, MathUtil::abs( bobbing.step( 1.0f / 60.0f, target )._position._y ) );
        maxSteady = MathUtil::max( maxSteady, MathUtil::abs( steady.step( 1.0f / 60.0f, target )._position._y ) );
    }
    SW_EXPECT_TRUE( maxBob > 0.03f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, maxSteady, 1.0e-6f );
}

/**
 * @brief [CameraModeTest] 사용자 설정은 엔진 전역 변수(`gv_cameraFieldOfView` · `gv_cameraShakeScale` · `gv_cameraHeadBob`)에서 읽고, 시야각 비율의 기준은 변수의 기본값이다
 * @details 게임 서비스에 엔진의 전역 변수 관리자를 걸어 게임 모듈이 보는 길로 읽는다. 기본값 그대로면 비율이 1 이라 저작한 화면 그대로다.
 */
SW_TEST_CASE( CameraModeTest, UserSettingsAreReadFromTheEngineGlobalVariables )
{
    GlobalVariableManager& variables = engine::getSubsystem<GlobalVariableManager>();
    GlobalVariableInfo*    pFov      = variables.findVariable( "gv_cameraFieldOfView" );
    GlobalVariableInfo*    pShake    = variables.findVariable( "gv_cameraShakeScale" );
    GlobalVariableInfo*    pBob      = variables.findVariable( "gv_cameraHeadBob" );
    SW_ASSERT_TRUE( pFov != nullptr && pShake != nullptr && pBob != nullptr );
    SubsystemScope scope;
    (void)scope.bind( &variables ); // 새 범위 — 겹치지 않는다
    const game::ScopedGameScope guard( scope );

    const CameraUserSettings neutral = CameraUserSettings::makeFromGlobalVariables();
    SW_EXPECT_NEAR_EQUAL( 1.0f, neutral._fieldOfViewScale, 1.0e-6f );

    const float32 reference = std::get<float32>( pFov->_defaultValue ) * MathUtil::kDegreeToRadian;
    SW_ASSERT_TRUE( pFov->setValueAsFloat( 90.0f ) );
    SW_ASSERT_TRUE( pShake->setValueAsFloat( 0.25f ) );
    SW_ASSERT_TRUE( pBob->setValueAsBool( false ) );
    const CameraUserSettings chosen = CameraUserSettings::makeFromGlobalVariables();
    pFov->resetToDefault();
    pShake->resetToDefault();
    pBob->resetToDefault();
    SW_EXPECT_NEAR_EQUAL( MathUtil::tan( 45.0f * MathUtil::kDegreeToRadian ) / MathUtil::tan( reference * 0.5f ), chosen._fieldOfViewScale, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, chosen._shakeScale, 1.0e-6f );
    SW_EXPECT_FALSE( chosen._bHeadBob );
    // 기본 시야각의 렌즈를 사용자 90 도로 넓히면 정확히 90 도다.
    CameraPose pose;
    pose._fieldOfViewY = reference;
    SW_EXPECT_NEAR_EQUAL( 90.0f * MathUtil::kDegreeToRadian, chosen.applyFieldOfView( CameraPresetMode::FirstPerson, pose )._fieldOfViewY, 1.0e-4f );
}
