#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Character/Camera/CameraBlend.h"
#include "GameFramework/Runtime/Character/Camera/CameraCollisionProbe.h"
#include "GameFramework/Runtime/Character/Camera/CameraMode.h"

using namespace sw;

namespace
{
    /** @brief 정해 둔 거리에서 막히는 암 질의입니다(음수면 안 막힌다). */
    class FakeCameraProbe final : public ICameraCollisionProbe
    {
    public:
        float32 _hitDistance;

        explicit FakeCameraProbe( float32 hitDistance )
            : _hitDistance{ hitDistance }
        {
        }

        bool sweepSphere( const float3& /*from*/, const float3& /*to*/, float32 /*radius*/, float32& outDistance ) const override
        {
            if ( _hitDistance < 0.0f )
                return false;
            outDistance = _hitDistance;
            return true;
        }
    };

    struct CameraModeTestUtil
    {
        static constexpr float32 kNear = 1e-3f;

        [[nodiscard]] static CameraPresetDef makePreset( CameraPresetMode mode )
        {
            CameraPresetDef def;
            def._view._mode     = mode;
            def._view._distance = 10.0f;
            return def;
        }

        [[nodiscard]] static CameraTarget makeTarget( const float3& focus )
        {
            CameraTarget target;
            target._focus = focus;
            return target;
        }

        [[nodiscard]] static float3 computeForward( const CameraPose& pose ) { return float3::transform( float3{ 0.0f, 0.0f, 1.0f }, pose._rotation ); }

        static void expectPoint( const float3& actual, const float3& expected )
        {
            SW_EXPECT_NEAR( actual._x, expected._x, kNear );
            SW_EXPECT_NEAR( actual._y, expected._y, kNear );
            SW_EXPECT_NEAR( actual._z, expected._z, kNear );
        }
    };
} // namespace

// 각과 회전은 같은 규약이어야 한다: `computeLookAngles` 로 구한 각으로 돈 카메라는 그 점을 본다.
SW_TEST_CASE( CameraModeTest, LookAnglesPointTheCameraAtTheTarget )
{
    const float3 from{ 1.0f, 2.0f, 3.0f };
    const float3 to{ -4.0f, -1.0f, 9.0f };
    const float2 angles = computeLookAngles( from, to );
    float3       toward = to - from;
    toward.normalize();
    CameraModeTestUtil::expectPoint( float3::transform( float3{ 0.0f, 0.0f, 1.0f }, quaternion::makeFromYawPitchRoll( angles._x, angles._y, 0.0f ) ), toward );

    const float2 same = computeLookAngles( from, from );
    SW_EXPECT_EQUAL( same._x, 0.0f );
    SW_EXPECT_EQUAL( same._y, 0.0f );
}

SW_TEST_CASE( CameraModeTest, GroupTargetWrapsAllPoints )
{
    const float3 arrPoint[]{
        float3{-2.0f, 0.0f, 0.0f},
        float3{ 4.0f, 0.0f, 0.0f},
        float3{ 1.0f, 0.0f, 0.0f}
    };
    const CameraTarget target = makeGroupCameraTarget( arrPoint, 3 );
    CameraModeTestUtil::expectPoint( target._focus, float3{ 1.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR( target._groupRadius, 3.0f, CameraModeTestUtil::kNear );

    const CameraTarget empty = makeGroupCameraTarget( nullptr, 0 );
    SW_EXPECT_EQUAL( empty._groupRadius, 0.0f );
}

SW_TEST_CASE( CameraModeTest, OrbitStandsBackAlongItsViewAndLooksAtTheFocus )
{
    CameraPresetDef def = CameraModeTestUtil::makePreset( CameraPresetMode::Orbit );
    def._view._yaw      = 0.5f;
    def._view._pitch    = 0.4f;
    const float3     focus{ 1.0f, 2.0f, 3.0f };
    const CameraPose pose    = evaluatePreset( def, CameraModeTestUtil::makeTarget( focus ) );
    const float3     forward = CameraModeTestUtil::computeForward( pose );
    CameraModeTestUtil::expectPoint( pose._position + forward * 10.0f, focus );
    SW_EXPECT_TRUE( forward._y < 0.0f ); // 피치 + 는 아래를 본다
}

SW_TEST_CASE( CameraModeTest, LookInputNeedsTheHeldButtonWhenThePresetAsks )
{
    CameraPresetDef def         = CameraModeTestUtil::makePreset( CameraPresetMode::Orbit );
    def._input._lookSensitivity = 0.01f;
    def._input._bLookWhileHeld  = true;
    CameraModeState state;
    CameraModeInput input;
    input._lookDelta = float2{ 10.0f, 20.0f };

    applyCameraInput( def, input, 0.016f, state );
    SW_EXPECT_EQUAL( state._yawOffset, 0.0f );

    input._bLookHeld = SW_TRUE;
    applyCameraInput( def, input, 0.016f, state );
    SW_EXPECT_NEAR( state._yawOffset, 0.1f, CameraModeTestUtil::kNear );
    SW_EXPECT_NEAR( state._pitchOffset, 0.2f, CameraModeTestUtil::kNear );
}

// 피치는 프리셋 각과 더한 값이 제약 안이다. 넘친 입력을 쌓아 두면 되돌릴 때 그만큼 먹통이 된다.
SW_TEST_CASE( CameraModeTest, LookPitchStopsAtTheConfinerWithoutPilingUp )
{
    CameraPresetDef def         = CameraModeTestUtil::makePreset( CameraPresetMode::Orbit );
    def._view._pitch            = 0.2f;
    def._input._lookSensitivity = 0.01f;
    def._confiner._pitchMin     = -0.5f;
    def._confiner._pitchMax     = 0.5f;
    CameraModeState state;
    CameraModeInput input;
    input._lookDelta = float2{ 0.0f, 1000.0f };
    applyCameraInput( def, input, 0.016f, state );
    SW_EXPECT_NEAR( state._pitchOffset, 0.3f, CameraModeTestUtil::kNear );

    input._lookDelta = float2{ 0.0f, -10.0f };
    applyCameraInput( def, input, 0.016f, state );
    SW_EXPECT_NEAR( state._pitchOffset, 0.2f, CameraModeTestUtil::kNear );
}

SW_TEST_CASE( CameraModeTest, ZoomMultipliesPerNotchAndStaysInTheConfiner )
{
    CameraPresetDef def    = CameraModeTestUtil::makePreset( CameraPresetMode::Orbit );
    def._input._zoomStep   = 0.5f;
    def._confiner._zoomMin = 4.0f;
    def._confiner._zoomMax = 15.0f;
    CameraModeState state;
    CameraModeInput input;
    input._zoomNotches = 1.0f;
    applyCameraInput( def, input, 0.016f, state );
    SW_EXPECT_NEAR( state._zoom, 5.0f, CameraModeTestUtil::kNear );
    SW_EXPECT_NEAR( ( evaluateCameraMode( def, CameraTarget{}, 0.0f, state, nullptr )._position ).getLength(), 5.0f, CameraModeTestUtil::kNear );

    applyCameraInput( def, input, 0.016f, state );
    SW_EXPECT_NEAR( state._zoom, 4.0f, CameraModeTestUtil::kNear ); // 최소

    input._zoomNotches = -4.0f;
    applyCameraInput( def, input, 0.016f, state );
    SW_EXPECT_NEAR( state._zoom, 15.0f, CameraModeTestUtil::kNear ); // 최대
}

SW_TEST_CASE( CameraModeTest, OrthographicZoomChangesTheViewHeightNotTheDistance )
{
    CameraPresetDef def    = CameraModeTestUtil::makePreset( CameraPresetMode::OrthoTopDown );
    def._input._zoomStep   = 0.5f;
    def._lens._orthoHeight = 20.0f;
    CameraModeState state;
    CameraModeInput input;
    input._zoomNotches = 1.0f;
    applyCameraInput( def, input, 0.016f, state );
    const CameraPose pose = evaluateCameraMode( def, CameraTarget{}, 0.0f, state, nullptr );
    SW_EXPECT_NEAR( pose._orthoHeight, 10.0f, CameraModeTestUtil::kNear );
    SW_EXPECT_NEAR( pose._position.getLength(), 10.0f, CameraModeTestUtil::kNear );
}

SW_TEST_CASE( CameraModeTest, PanFollowsTheShownYawAndTheOrthoZoom )
{
    CameraPresetDef def  = CameraModeTestUtil::makePreset( CameraPresetMode::Orbit );
    def._input._panSpeed = 2.0f;
    def._view._yaw       = MathUtil::kHalfPi; // +X 를 본다
    CameraModeState state;
    CameraModeInput input;
    input._pan = float2{ 1.0f, 0.0f };
    applyCameraInput( def, input, 0.5f, state );
    CameraModeTestUtil::expectPoint( state._panOffset, float3{ 1.0f, 0.0f, 0.0f } ); // 앞이 +X
    state.reset();
    input._pan = float2{ 0.0f, 1.0f };
    applyCameraInput( def, input, 0.5f, state );
    CameraModeTestUtil::expectPoint( state._panOffset, float3{ 0.0f, 0.0f, -1.0f } ); // 오른쪽

    // 직교는 확대할수록 느리다 - 화면에서 보이는 빠르기가 같다.
    CameraPresetDef ortho    = CameraModeTestUtil::makePreset( CameraPresetMode::OrthoTopDown );
    ortho._input._panSpeed   = 2.0f;
    ortho._lens._orthoHeight = 20.0f;
    state.reset();
    state._zoom = 10.0f;
    input._pan  = float2{ 1.0f, 0.0f };
    applyCameraInput( ortho, input, 1.0f, state );
    SW_EXPECT_NEAR( state._panOffset.getLength(), 1.0f, CameraModeTestUtil::kNear );

    // 2D 는 화면 위가 +Y 다.
    CameraPresetDef planar  = CameraModeTestUtil::makePreset( CameraPresetMode::FollowXY );
    planar._input._panSpeed = 2.0f;
    state.reset();
    applyCameraInput( planar, input, 1.0f, state );
    CameraModeTestUtil::expectPoint( state._panOffset, float3{ 0.0f, 2.0f, 0.0f } );
}

SW_TEST_CASE( CameraModeTest, RotateStepIsShownOverTime )
{
    CameraPresetDef def    = CameraModeTestUtil::makePreset( CameraPresetMode::Orbit );
    def._input._rotateStep = 1.0f;
    def._input._rotateTime = 0.1f;
    CameraModeState state;
    CameraModeInput input;
    input._rotateSteps = 1;
    applyCameraInput( def, input, 0.016f, state );
    SW_EXPECT_EQUAL( state._rotateYaw, 1.0f );
    SW_EXPECT_EQUAL( state._rotateYawShown, 0.0f );

    (void)evaluateCameraMode( def, CameraTarget{}, 0.1f, state, nullptr );
    SW_EXPECT_TRUE( state._rotateYawShown > 0.5f && state._rotateYawShown < 1.0f );
    for ( uint32 step = 0; step < 100; ++step )
    {
        (void)evaluateCameraMode( def, CameraTarget{}, 0.1f, state, nullptr );
    }
    SW_EXPECT_NEAR( state._rotateYawShown, 1.0f, CameraModeTestUtil::kNear );
}

// 막히면 바로 당기고, 풀리면 회복 시간으로 돌아간다. 최소 거리 안으로는 당기지 않는다.
SW_TEST_CASE( CameraModeTest, SpringArmPullsInAtOnceAndEasesBack )
{
    CameraPresetDef def         = CameraModeTestUtil::makePreset( CameraPresetMode::Orbit );
    def._collision._bEnabled    = true;
    def._collision._minDistance = 1.0f;
    def._collision._recoverTime = 0.2f;
    const CameraTarget target   = CameraModeTestUtil::makeTarget( float3{} );
    CameraModeState    state;

    FakeCameraProbe clear( -1.0f );
    SW_EXPECT_NEAR( evaluateCameraMode( def, target, 0.016f, state, &clear )._position.getLength(), 10.0f, CameraModeTestUtil::kNear );
    SW_EXPECT_TRUE( state._bArmBlocked == SW_FALSE );

    FakeCameraProbe wall( 3.0f );
    SW_EXPECT_NEAR( evaluateCameraMode( def, target, 0.016f, state, &wall )._position.getLength(), 3.0f, CameraModeTestUtil::kNear );
    SW_EXPECT_TRUE( state._bArmBlocked == SW_TRUE );

    FakeCameraProbe nearWall( 0.2f );
    SW_EXPECT_NEAR( evaluateCameraMode( def, target, 0.016f, state, &nearWall )._position.getLength(), 1.0f, CameraModeTestUtil::kNear );

    const float32 recovering = evaluateCameraMode( def, target, 0.2f, state, &clear )._position.getLength();
    SW_EXPECT_TRUE( recovering > 1.0f && recovering < 10.0f );
    for ( uint32 step = 0; step < 100; ++step )
    {
        (void)evaluateCameraMode( def, target, 0.1f, state, &clear );
    }
    SW_EXPECT_NEAR( state._armLength, 10.0f, CameraModeTestUtil::kNear );
}

SW_TEST_CASE( CameraModeTest, SpringArmIgnoresTheProbeWhenCollisionIsOff )
{
    const CameraPresetDef def = CameraModeTestUtil::makePreset( CameraPresetMode::Orbit );
    CameraModeState       state;
    FakeCameraProbe       wall( 3.0f );
    SW_EXPECT_NEAR( evaluateCameraMode( def, CameraTarget{}, 0.016f, state, &wall )._position.getLength(), 10.0f, CameraModeTestUtil::kNear );
}

// 그룹 맞추기는 늘리기만 하고 줄이지 않는다.
SW_TEST_CASE( CameraModeTest, GroupFitBacksOffButNeverCloses )
{
    CameraPresetDef def        = CameraModeTestUtil::makePreset( CameraPresetMode::Orbit );
    def._framing._groupPadding = 1.0f;
    CameraTarget target;
    target._groupRadius = 0.1f;
    SW_EXPECT_NEAR( evaluatePreset( def, target )._position.getLength(), 10.0f, CameraModeTestUtil::kNear );

    target._groupRadius  = 20.0f;
    const float32 fitted = 20.0f / MathUtil::sin( def._lens._fieldOfViewY * 0.5f );
    SW_EXPECT_NEAR( evaluatePreset( def, target )._position.getLength(), fitted, 0.01f );

    def._lens._orthoHeight = 10.0f;
    def._view._mode        = CameraPresetMode::OrthoTopDown;
    SW_EXPECT_NEAR( evaluatePreset( def, target )._orthoHeight, 40.0f, CameraModeTestUtil::kNear );
}

SW_TEST_CASE( CameraModeTest, LookAheadLeadsAMovingTarget )
{
    CameraPresetDef def              = CameraModeTestUtil::makePreset( CameraPresetMode::Orbit );
    def._framing._lookAheadTime      = 0.5f;
    def._framing._lookAheadSmoothing = 0.0f; // 곧바로
    CameraModeState state;

    (void)evaluateCameraMode( def, CameraModeTestUtil::makeTarget( float3{} ), 0.1f, state, nullptr );
    CameraModeTestUtil::expectPoint( state._lookAhead, float3{} ); // 첫 평가에는 속도를 모른다

    (void)evaluateCameraMode( def, CameraModeTestUtil::makeTarget( float3{ 1.0f, 0.0f, 0.0f } ), 0.1f, state, nullptr );
    CameraModeTestUtil::expectPoint( state._lookAhead, float3{ 5.0f, 0.0f, 0.0f } ); // 초속 10 m × 0.5 초
}

SW_TEST_CASE( CameraModeTest, FixedCameraAimsByItsAimMode )
{
    CameraPresetDef def       = CameraModeTestUtil::makePreset( CameraPresetMode::Fixed );
    def._view._offset         = float3{ 0.0f, 5.0f, 0.0f };
    def._view._yaw            = 1.0f;
    const CameraTarget target = CameraModeTestUtil::makeTarget( float3{ 0.0f, 5.0f, 10.0f } );

    def._view._aim    = CameraAimMode::Angles;
    CameraPose angles = evaluatePreset( def, target );
    CameraModeTestUtil::expectPoint( angles._position, def._view._offset );
    CameraModeTestUtil::expectPoint( CameraModeTestUtil::computeForward( angles ), float3{ MathUtil::sin( 1.0f ), 0.0f, MathUtil::cos( 1.0f ) } );

    def._view._aim    = CameraAimMode::Point;
    def._view._lookAt = float3{ 10.0f, 5.0f, 0.0f };
    CameraModeTestUtil::expectPoint( CameraModeTestUtil::computeForward( evaluatePreset( def, target ) ), float3{ 1.0f, 0.0f, 0.0f } );

    def._view._aim = CameraAimMode::Target;
    CameraModeTestUtil::expectPoint( CameraModeTestUtil::computeForward( evaluatePreset( def, target ) ), float3{ 0.0f, 0.0f, 1.0f } );
}

SW_TEST_CASE( CameraModeTest, FixedCameraStaysInTheBounds )
{
    CameraPresetDef def      = CameraModeTestUtil::makePreset( CameraPresetMode::Fixed );
    def._view._offset        = float3{ 100.0f, 0.0f, 0.0f };
    def._confiner._bBounds   = true;
    def._confiner._boundsMin = float3{ -1.0f, -1.0f, -1.0f };
    def._confiner._boundsMax = float3{ 1.0f, 1.0f, 1.0f };
    CameraModeTestUtil::expectPoint( evaluatePreset( def, CameraTarget{} )._position, float3{ 1.0f, 0.0f, 0.0f } );
}

SW_TEST_CASE( CameraModeTest, FirstPersonRidesTheTargetViewAndItsYawedOffset )
{
    CameraPresetDef def   = CameraModeTestUtil::makePreset( CameraPresetMode::FirstPerson );
    def._view._offset     = float3{ 0.0f, 1.6f, 1.0f };
    CameraTarget target   = CameraModeTestUtil::makeTarget( float3{ 5.0f, 0.0f, 0.0f } );
    target._yaw           = MathUtil::kHalfPi;
    const CameraPose pose = evaluatePreset( def, target );
    CameraModeTestUtil::expectPoint( pose._position, float3{ 6.0f, 1.6f, 0.0f } ); // 앞이 +X 라 오프셋도 돈다
    CameraModeTestUtil::expectPoint( CameraModeTestUtil::computeForward( pose ), float3{ 1.0f, 0.0f, 0.0f } );
}

SW_TEST_CASE( CameraModeTest, ThirdPersonStandsBehindTheTargetShoulder )
{
    CameraPresetDef def   = CameraModeTestUtil::makePreset( CameraPresetMode::ThirdPerson );
    def._view._offset     = float3{ 1.0f, 2.0f, 0.0f };
    def._view._distance   = 4.0f;
    const CameraPose pose = evaluatePreset( def, CameraModeTestUtil::makeTarget( float3{} ) );
    CameraModeTestUtil::expectPoint( pose._position, float3{ 1.0f, 2.0f, -4.0f } );
}

// 팬은 상자 끝에서 멈춘다. 넘친 만큼 남겨 두면 되돌아올 때 그만큼 키를 눌러도 화면이 움직이지 않는다.
SW_TEST_CASE( CameraModeTest, OrthoBoundsStopThePanWithoutPilingUp )
{
    CameraPresetDef def      = CameraModeTestUtil::makePreset( CameraPresetMode::OrthoTopDown );
    def._confiner._bBounds   = true;
    def._confiner._boundsMin = float3{ -5.0f, 0.0f, -5.0f };
    def._confiner._boundsMax = float3{ 5.0f, 0.0f, 5.0f };
    CameraModeState state;
    state._panOffset = float3{ 100.0f, 0.0f, 0.0f };
    (void)evaluateCameraMode( def, CameraTarget{}, 0.016f, state, nullptr );
    SW_EXPECT_NEAR( state._panOffset._x, 5.0f, CameraModeTestUtil::kNear );
}

SW_TEST_CASE( CameraModeTest, FollowXYStaysOnItsCameraPlane )
{
    CameraPresetDef def   = CameraModeTestUtil::makePreset( CameraPresetMode::FollowXY );
    def._view._offset     = float3{ 1.0f, 2.0f, -20.0f };
    const CameraPose free = evaluatePreset( def, CameraModeTestUtil::makeTarget( float3{ 3.0f, 4.0f, 7.0f } ) );
    CameraModeTestUtil::expectPoint( free._position, float3{ 4.0f, 6.0f, -20.0f } );

    def._confiner._bBounds   = true;
    def._confiner._boundsMin = float3{ -1.0f, -1.0f, 0.0f };
    def._confiner._boundsMax = float3{ 1.0f, 1.0f, 0.0f };
    const CameraPose bounded = evaluatePreset( def, CameraModeTestUtil::makeTarget( float3{ 3.0f, 4.0f, 7.0f } ) );
    CameraModeTestUtil::expectPoint( bounded._position, float3{ 1.0f, 1.0f, -20.0f } ); // 평면 깊이는 상자가 건드리지 않는다
}

SW_TEST_CASE( CameraModeTest, DollyStandsOnTheRailNearestTheTarget )
{
    CameraPresetDef def   = CameraModeTestUtil::makePreset( CameraPresetMode::Dolly );
    def._dolly._listPoint = {
        float3{ 0.0f, 0.0f, 0.0f},
        float3{ 5.0f, 0.0f, 0.0f},
        float3{10.0f, 0.0f, 0.0f}
    };
    CameraModeState    state;
    const CameraTarget target = CameraModeTestUtil::makeTarget( float3{ 4.0f, 0.0f, 6.0f } );
    const CameraPose   pose   = evaluateCameraMode( def, target, 0.016f, state, nullptr );
    SW_EXPECT_NEAR( pose._position._x, 4.0f, 0.1f );
    SW_EXPECT_NEAR( pose._position._z, 0.0f, 0.1f );
    CameraModeTestUtil::expectPoint( CameraModeTestUtil::computeForward( pose ), float3{ ( 4.0f - pose._position._x ) / 6.0f, 0.0f, 1.0f } );
}

SW_TEST_CASE( CameraModeTest, DollyWithoutARailStandsAtItsOffset )
{
    CameraPresetDef def = CameraModeTestUtil::makePreset( CameraPresetMode::Dolly );
    def._view._offset   = float3{ 1.0f, 2.0f, 3.0f };
    CameraModeTestUtil::expectPoint( evaluatePreset( def, CameraTarget{} )._position, def._view._offset );
}

SW_TEST_CASE( CameraModeTest, SweepSwingsOverItsPeriod )
{
    CameraPresetDef def      = CameraModeTestUtil::makePreset( CameraPresetMode::Fixed );
    def._sweep._yawAmplitude = 0.5f;
    def._sweep._period       = 4.0f;
    CameraModeState state;
    const float3    quarter = CameraModeTestUtil::computeForward( evaluateCameraMode( def, CameraTarget{}, 1.0f, state, nullptr ) ); // 주기의 1/4
    CameraModeTestUtil::expectPoint( quarter, float3{ MathUtil::sin( 0.5f ), 0.0f, MathUtil::cos( 0.5f ) } );
    const float3 half = CameraModeTestUtil::computeForward( evaluateCameraMode( def, CameraTarget{}, 1.0f, state, nullptr ) );
    CameraModeTestUtil::expectPoint( half, float3{ 0.0f, 0.0f, 1.0f } );
}

// 데드존 안의 움직임은 카메라를 돌리지 않고, 소프트존 밖으로는 감쇠가 늦어도 나가지 않는다.
SW_TEST_CASE( CameraModeTest, FramingHoldsInTheDeadZoneAndNeverLeavesTheSoftZone )
{
    CameraPresetDef def    = CameraModeTestUtil::makePreset( CameraPresetMode::Dolly );
    def._view._offset      = float3{}; // 레일이 없어 원점에 선다
    def._framing._bCompose = true;
    def._framing._deadZone = float2{ 0.2f, 0.2f };
    def._framing._softZone = float2{ 0.4f, 0.4f };
    def._framing._damping  = 1000.0f; // 거의 따라가지 않는다
    CameraTarget target    = CameraModeTestUtil::makeTarget( float3{ 0.0f, 0.0f, 10.0f } );
    target._aspectRatio    = 1.0f;
    CameraModeState state;
    (void)evaluateCameraMode( def, target, 0.016f, state, nullptr );
    SW_EXPECT_TRUE( state._bAimValid == SW_TRUE );
    SW_EXPECT_NEAR( state._aimYaw, 0.0f, CameraModeTestUtil::kNear ); // 첫 평가는 곧바로 맞춘다

    const float32 tanHalf = MathUtil::tan( def._lens._fieldOfViewY * 0.5f );
    target._focus         = float3{ 10.0f * 0.1f * tanHalf, 0.0f, 10.0f }; // 데드존 안
    (void)evaluateCameraMode( def, target, 0.016f, state, nullptr );
    SW_EXPECT_NEAR( state._aimYaw, 0.0f, CameraModeTestUtil::kNear );

    target._focus = float3{ 10.0f, 0.0f, 0.0f }; // 소프트존 밖으로 크게 - 감쇠가 늦어도 소프트존 끝까지 곧바로 돈다
    (void)evaluateCameraMode( def, target, 0.016f, state, nullptr );
    const float32 softHalf = MathUtil::atan2( 0.4f * tanHalf, 1.0f );
    SW_EXPECT_NEAR( state._aimYaw, MathUtil::kHalfPi - softHalf, CameraModeTestUtil::kNear );
}

// 머리 흔들림 - 위상은 간 거리로 가고, 시간이 흐르지 않은 평가는 거리만 모은다. 한 주기보다 먼 이동은 순간 이동이다.
SW_TEST_CASE( CameraModeTest, HeadBobMovesWithDistanceNotEvaluations )
{
    CameraHeadBobDef headBob;
    headBob._verticalAmplitude = 0.1f;
    headBob._lateralAmplitude  = 0.05f;
    headBob._strideLength      = 2.0f;
    headBob._referenceSpeed    = 1.0f;
    headBob._blendTime         = 0.0f; // 곧바로
    CameraModeState state;

    (void)computeCameraHeadBob( headBob, float3{}, 0.1f, state ); // 첫 평가는 자리만 기억한다
    SW_EXPECT_EQUAL( state._bobPhase, 0.0f );

    (void)computeCameraHeadBob( headBob, float3{ 0.5f, 0.0f, 0.0f }, 0.0f, state ); // 시간이 흐르지 않았다
    SW_EXPECT_NEAR( state._bobPhase, MathUtil::kHalfPi, CameraModeTestUtil::kNear );
    SW_EXPECT_NEAR( state._bobDistance, 0.5f, CameraModeTestUtil::kNear );
    SW_EXPECT_EQUAL( state._bobWeight, 0.0f );

    const CameraShakeOffset moving = computeCameraHeadBob( headBob, float3{ 0.5f, 0.0f, 0.0f }, 0.1f, state ); // 0.5 m ÷ 0.1 초 - 기준 빠르기 위
    SW_EXPECT_EQUAL( state._bobDistance, 0.0f );
    SW_EXPECT_EQUAL( state._bobWeight, 1.0f );
    SW_EXPECT_NEAR( moving._position._x, 0.05f, CameraModeTestUtil::kNear ); // 위상 90 도 - 옆으로 가장 멀다
    SW_EXPECT_NEAR( moving._position._y, 0.0f, CameraModeTestUtil::kNear );

    const float32 phaseBefore = state._bobPhase;
    (void)computeCameraHeadBob( headBob, float3{ 100.0f, 0.0f, 0.0f }, 0.1f, state ); // 순간 이동
    SW_EXPECT_EQUAL( state._bobPhase, phaseBefore );
    SW_EXPECT_EQUAL( state._bobWeight, 0.0f );

    CameraModeState         untouched;
    const CameraShakeOffset off = computeCameraHeadBob( CameraHeadBobDef{}, float3{ 1.0f, 0.0f, 0.0f }, 0.1f, untouched );
    SW_EXPECT_EQUAL( off._position._x, 0.0f );
    SW_EXPECT_TRUE( untouched._bHasBobFocus == SW_FALSE );
}

SW_TEST_CASE( CameraModeTest, ScreenRayOfThePerspectiveAndOrthographicPose )
{
    CameraPose pose;
    pose._position     = float3{ 1.0f, 2.0f, 3.0f };
    pose._fieldOfViewY = MathUtil::kHalfPi;

    const Ray center = computeCameraScreenRay( pose, 2.0f, float2{ 0.5f, 0.5f } );
    CameraModeTestUtil::expectPoint( center._origin, pose._position );
    CameraModeTestUtil::expectPoint( center._direction, float3{ 0.0f, 0.0f, 1.0f } );

    // tan(45 도) = 1 이라 화면 위쪽 끝은 45 도 위, 오른쪽 끝은 가로 비율만큼 벌어진다.
    Ray top = computeCameraScreenRay( pose, 2.0f, float2{ 0.5f, 0.0f } );
    SW_EXPECT_NEAR( top._direction._y, top._direction._z, CameraModeTestUtil::kNear );
    SW_EXPECT_TRUE( top._direction._y > 0.0f );
    Ray right = computeCameraScreenRay( pose, 2.0f, float2{ 1.0f, 0.5f } );
    SW_EXPECT_NEAR( right._direction._x, right._direction._z * 2.0f, CameraModeTestUtil::kNear );

    pose._bOrthographic = SW_TRUE;
    pose._orthoHeight   = 10.0f;
    const Ray corner    = computeCameraScreenRay( pose, 2.0f, float2{ 1.0f, 0.0f } );
    CameraModeTestUtil::expectPoint( corner._direction, float3{ 0.0f, 0.0f, 1.0f } );
    CameraModeTestUtil::expectPoint( corner._origin, float3{ 11.0f, 7.0f, 3.0f } );
}

SW_TEST_CASE( CameraModeTest, LensValuesReachThePose )
{
    CameraPresetDef def      = CameraModeTestUtil::makePreset( CameraPresetMode::Orbit );
    def._lens._focalLength   = 24.0f;
    def._lens._sensorSize    = float2{ 36.0f, 24.0f };
    def._lens._focusDistance = 7.0f;
    def._lens._aperture      = 2.8f;
    def._lens._lensShift     = float2{ 0.1f, 0.2f };
    def._lens._dutch         = MathUtil::kHalfPi;
    const CameraPose pose    = evaluatePreset( def, CameraTarget{} );
    SW_EXPECT_NEAR( pose._fieldOfViewY, 2.0f * MathUtil::atan2( 12.0f, 24.0f ), CameraModeTestUtil::kNear );
    SW_EXPECT_EQUAL( pose._depthOfField._focusDistance, 7.0f );
    SW_EXPECT_EQUAL( pose._depthOfField._aperture, 2.8f );
    SW_EXPECT_EQUAL( pose._lensShift._y, 0.2f );
    // 더치는 앞축으로만 기울인다 - 앞은 그대로, 위가 옆으로 눕는다.
    CameraModeTestUtil::expectPoint( CameraModeTestUtil::computeForward( pose ), float3{ 0.0f, 0.0f, 1.0f } );
    SW_EXPECT_NEAR( MathUtil::abs( float3::transform( float3{ 0.0f, 1.0f, 0.0f }, pose._rotation )._x ), 1.0f, CameraModeTestUtil::kNear );
}

/**
 * @brief [CameraModeTest] 렌즈 · 후처리 블렌드가 리플렉션으로 모든 칸을 섞고, 칸마다 규칙(조리개 역수 · 한쪽이 없는 레터박스)만 덮어쓰는지 검증
 */
SW_TEST_CASE( CameraModeTest, LensAndPostProcessBlendEveryReflectedField )
{
    CameraPose from;
    CameraPose to;
    from._depthOfField._focusDistance        = 2.0f;
    to._depthOfField._focusDistance          = 6.0f;
    to._depthOfField._aperture               = 2.0f; // 핀홀(0)에서 f/2 로 — 역수 0 → 0.5 의 절반이 0.25 라 f/4
    from._postProcess._saturation            = 0.0f;
    to._postProcess._saturation              = 1.0f;
    from._postProcess._autoExposureSpeedDown = 1.0f;
    to._postProcess._autoExposureSpeedDown   = 3.0f;
    to._postProcess._letterboxAspectRatio    = 2.39f; // 한쪽만 있으면 절반에서 바꾼다
    const CameraPose half                    = blendPoses( from, to, 0.5f );
    SW_EXPECT_NEAR( half._depthOfField._focusDistance, 4.0f, CameraModeTestUtil::kNear );
    SW_EXPECT_NEAR( half._depthOfField._aperture, 4.0f, CameraModeTestUtil::kNear );
    SW_EXPECT_NEAR( half._postProcess._saturation, 0.5f, CameraModeTestUtil::kNear );
    SW_EXPECT_NEAR( half._postProcess._autoExposureSpeedDown, 2.0f, CameraModeTestUtil::kNear );
    SW_EXPECT_NEAR( half._postProcess._letterboxAspectRatio, 2.39f, CameraModeTestUtil::kNear );
    const CameraPose early = blendPoses( from, to, 0.25f );
    SW_EXPECT_EQUAL( early._postProcess._letterboxAspectRatio, 0.0f );
}

SW_TEST_CASE( CameraModeTest, ResetForgetsEverything )
{
    CameraModeState state;
    state._yawOffset = 1.0f;
    state._armLength = 3.0f;
    state._bAimValid = SW_TRUE;
    state.reset();
    SW_EXPECT_EQUAL( state._yawOffset, 0.0f );
    SW_EXPECT_EQUAL( state._armLength, -1.0f );
    SW_EXPECT_TRUE( state._bAimValid == SW_FALSE );
}
