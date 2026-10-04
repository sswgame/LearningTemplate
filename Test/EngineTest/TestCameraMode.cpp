#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Camera/CameraBlend.h"
#include "GameFramework/Camera/CameraCollisionProbe.h"
#include "GameFramework/Camera/CameraDirector.h"
#include "GameFramework/Camera/CameraManagerComponent.h"
#include "GameFramework/Camera/CameraMode.h"
#include "GameFramework/Camera/CameraPreset.h"
#include "GameFramework/Camera/CameraShake.h"
#include "GameFramework/Components/OrthoCameraRigComponent.h"

#include "TestFramework/TestFramework.h"

// 카메라 모드 — 3인칭 스프링 암 · 궤도 입력 · 제약 · CCTV 훑기 · 프레이밍 · 흔들림(잡음 · 충격) · 뷰 타깃 블렌드 · 컷 신호.

using namespace sw;

namespace
{
    struct CameraModeTestInternal
    {
        static float3 computeForward( const quaternion& rotation ) { return float3::transform( float3{ 0.0f, 0.0f, 1.0f }, rotation ); }

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

        static CameraBlendSpec makeLinear( float32 duration )
        {
            CameraBlendSpec blend;
            blend._curve    = CameraBlendCurve::Linear;
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
        (void)evaluateCameraMode( def, target, 0.1f, state, &probe );
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
    target._yaw           = MathUtil::HalfPi;
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
    def._view._pitch            = 20.0f * MathUtil::DegreeToRadian;
    def._input._lookSensitivity = 0.01f;
    def._input._zoomStep        = 0.5f;
    def._confiner._pitchMin     = -10.0f * MathUtil::DegreeToRadian;
    def._confiner._pitchMax     = 60.0f * MathUtil::DegreeToRadian;
    def._confiner._zoomMin      = 2.0f;
    def._confiner._zoomMax      = 20.0f;
    const CameraTarget target;

    CameraModeState state;
    CameraModeInput input;
    input._lookDelta = float2{ 100.0f, 1000.0f }; // 요 +1 rad, 피치는 한계 너머로 끈다
    applyCameraInput( def, input, 1.0f / 60.0f, state );
    SW_EXPECT_NEAR_EQUAL( 1.0f, state._yawOffset, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 60.0f * MathUtil::DegreeToRadian, def._view._pitch + state._pitchOffset, 1.0e-5f );

    CameraModeInput zoom;
    zoom._zoomNotches = 3.0f; // 10 × 0.5³ = 1.25 → 아래 한계 2
    applyCameraInput( def, zoom, 1.0f / 60.0f, state );
    SW_EXPECT_NEAR_EQUAL( 2.0f, state._zoom, 1.0e-5f );
    zoom._zoomNotches = -10.0f; // 2 × 2¹⁰ → 위 한계 20
    applyCameraInput( def, zoom, 1.0f / 60.0f, state );
    SW_EXPECT_NEAR_EQUAL( 20.0f, state._zoom, 1.0e-4f );

    const CameraPose pose = evaluateCameraMode( def, target, 1.0f / 60.0f, state, nullptr );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pose._position.getLength(), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 20.0f * MathUtil::sin( 60.0f * MathUtil::DegreeToRadian ), pose._position._y, 1.0e-3f );
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
    ortho._view._pitch    = MathUtil::HalfPi;
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
    def._sweep._yawAmplitude = 30.0f * MathUtil::DegreeToRadian;
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
    SW_EXPECT_NEAR_EQUAL( 30.0f * MathUtil::DegreeToRadian, quarterAngle._x, 1.0e-3f );
    const CameraPose threeQuarter = evaluateCameraMode( def, target, 2.0f, state, nullptr ); // 주기 3/4 → −진폭
    const float2     backAngle    = computeLookAngles( threeQuarter._position,
                                                       threeQuarter._position + CameraModeTestInternal::computeForward( threeQuarter._rotation ) );
    SW_EXPECT_NEAR_EQUAL( -30.0f * MathUtil::DegreeToRadian, backAngle._x, 1.0e-3f );
}

/**
 * @brief [CameraModeTest] 프레이밍 — 데드존 안의 움직임은 조준을 돌리지 않고, 그 밖으로 나가면 돌리며, 소프트존 밖으로는 한 프레임도 나가지 않는다
 */
SW_TEST_CASE( CameraModeTest, FramingDeadZoneHoldsAndSoftZoneLimits )
{
    CameraPresetDef def;
    def._view._mode         = CameraPresetMode::Fixed;
    def._view._aim          = CameraAimMode::Target;
    def._lens._fieldOfViewY = 60.0f * MathUtil::DegreeToRadian;
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
    const float32 tanHalfX = MathUtil::tan( 30.0f * MathUtil::DegreeToRadian ) * ( 16.0f / 9.0f );
    const float32 softHalf = MathUtil::atan2( 0.5f * tanHalfX, 1.0f );
    SW_EXPECT_NEAR_EQUAL( MathUtil::Pi * 0.25f - softHalf, state._aimYaw, 1.0e-4f );
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
    group._lens._fieldOfViewY     = 60.0f * MathUtil::DegreeToRadian;
    const float3 arrPoint[]       = {
        float3{-4.0f, 0.0f, 0.0f},
        float3{ 4.0f, 0.0f, 0.0f},
        float3{ 0.0f, 0.0f, 3.0f}
    };
    const CameraTarget groupTarget = makeGroupCameraTarget( arrPoint, 3 );
    SW_EXPECT_NEAR_EQUAL( 1.5f, groupTarget._focus._z, 1.0e-5f );
    const CameraPose groupPose = evaluatePreset( group, groupTarget );
    const float32    expected  = groupTarget._groupRadius * 1.5f / MathUtil::sin( 30.0f * MathUtil::DegreeToRadian );
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
    CameraBlendSpec cut;
    cut._curve = CameraBlendCurve::Cut;
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
    CameraBlendSpec cut;
    cut._curve = CameraBlendCurve::Cut;

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
 * @brief [CameraModeTest] 직교 리그(직교 모드 + 디렉터)는 시작 요에서 바로 시작하고(0 에서 돌아 들어오지 않는다) 초점을 블렌드 없이 따라간다
 */
SW_TEST_CASE( CameraModeTest, OrthoRigStartsAtItsYawAndFollowsTheFocus )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "RigCamera" ) );
    SW_ASSERT_NOT_NULL( pObject );
    CameraComponent*         pCamera = pObject->addComponent<CameraComponent>();
    OrthoCameraRigComponent* pRig    = pObject->addComponent<OrthoCameraRigComponent>();
    SW_ASSERT_TRUE( pCamera != nullptr && pRig != nullptr );
    pRig->applyToCamera();
    const float32 startYaw = pRig->getShownYaw();
    SW_EXPECT_NEAR_EQUAL( pRig->getYaw(), startYaw, 1.0e-6f ); // 시작은 돌아 들어오지 않는다

    pRig->setFocus( float3{ 1.0f, 0.0f, 0.0f } ); // 같은 리그 · 다른 초점은 블렌드 없이 그대로 따라간다
    pRig->updateCamera( 0.016f );
    const OrthoCameraView view = OrthoCameraRigMath::computeView( float3{ 1.0f, 0.0f, 0.0f }, pRig->getYaw(), 30.0f * MathUtil::DegreeToRadian, 250.0f );
    SW_EXPECT_NEAR_EQUAL( view._position._x, pCamera->getWorldPosition()._x, 1.0e-2f );
}
