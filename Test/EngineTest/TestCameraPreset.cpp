#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/Events/RawInputEvent.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/Camera/CameraBlend.h"
#include "GameFramework/Camera/CameraDirector.h"
#include "GameFramework/Camera/CameraDirectorComponent.h"
#include "GameFramework/Camera/CameraPreset.h"
#include "GameFramework/Components/OrthoCameraRigComponent.h"
#include "GameFramework/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

// 카메라 프리셋 — 블렌드 곡선 · 포즈 섞기 · 프리셋 풀기 · 카탈로그 · 디렉터의 블렌드 이어 가기 · 컴포넌트.

using namespace sw;

namespace
{
    struct CameraPresetTestInternal
    {
        static BlendCurveSpec makeBlend( BlendCurve curve, float32 duration = 1.0f, float32 exponent = 2.0f )
        {
            BlendCurveSpec spec;
            spec._curve    = curve;
            spec._duration = duration;
            spec._exponent = exponent;
            return spec;
        }

        static BlendCurveSpec makeCustomBlend()
        {
            BlendCurveSpec spec = makeBlend( BlendCurve::Custom );
            spec._listCustomKey.push_back( BlendCurveKey{ 0.0f, 0.0f } );
            spec._listCustomKey.push_back( BlendCurveKey{ 0.5f, 0.8f } );
            spec._listCustomKey.push_back( BlendCurveKey{ 1.0f, 1.0f } );
            return spec;
        }

        static CameraPresetDef makeFixed( const utf8* pId, const float3& position )
        {
            CameraPresetDef def;
            def._id             = hashed_string( pId );
            def._view._mode     = CameraPresetMode::Fixed;
            def._view._offset   = position;
            def._blendIn        = makeBlend( BlendCurve::Linear );
            def._lens._farPlane = 200.0f;
            return def;
        }

        static float3 computeForward( const quaternion& rotation ) { return float3::transform( float3{ 0.0f, 0.0f, 1.0f }, rotation ); }
    };
} // namespace

/**
 * @brief [CameraPresetTest] 모든 곡선은 0 에서 0, 1 에서 1 이고 사이에서 내려가지 않는다 — 스프링도 넘치지 않는다
 */
SW_TEST_CASE( CameraPresetTest, EveryCurveRunsFromZeroToOneWithoutGoingBack )
{
    const BlendCurve arrCurve[] = { BlendCurve::Cut, BlendCurve::Linear, BlendCurve::EaseIn, BlendCurve::EaseOut,
                                    BlendCurve::EaseInOut, BlendCurve::Cubic, BlendCurve::Exponential, BlendCurve::Spring,
                                    BlendCurve::SmoothStep, BlendCurve::Custom };
    for ( const BlendCurve curve : arrCurve )
    {
        const BlendCurveSpec spec  = curve == BlendCurve::Custom ? CameraPresetTestInternal::makeCustomBlend() : CameraPresetTestInternal::makeBlend( curve );
        const utf8*          pName = engine::getTypeRegistry().enumToString( curve );
        SW_EXPECT_NEAR_EQUAL( 0.0f, evaluateBlendWeight( spec, 0.0f ), 1.0e-6f );
        SW_EXPECT_NEAR_EQUAL( 1.0f, evaluateBlendWeight( spec, 1.0f ), 1.0e-5f );
        float32 previous = 0.0f;
        for ( int32 sampleIndex = 1; sampleIndex <= 100; ++sampleIndex )
        {
            const float32 weight = evaluateBlendWeight( spec, static_cast<float32>( sampleIndex ) / 100.0f );
            SW_EXPECT_TRUE_MSG( weight >= previous - 1.0e-6f, pName );
            SW_EXPECT_TRUE_MSG( weight <= 1.0f + 1.0e-5f, pName );
            previous = weight;
        }
    }
}

/**
 * @brief [CameraPresetTest] 곡선마다 알려진 중간값 — 식을 다시 쓰지 않고 손으로 구한 값과 대조한다
 */
SW_TEST_CASE( CameraPresetTest, CurvesHitTheirKnownMidpoints )
{
    using Internal = CameraPresetTestInternal;
    SW_EXPECT_NEAR_EQUAL( 0.5f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::Linear ), 0.5f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::SmoothStep ), 0.5f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.15625f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::SmoothStep ), 0.25f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::EaseIn ), 0.5f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.125f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::EaseIn, 1.0f, 3.0f ), 0.5f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::EaseOut ), 0.5f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.125f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::EaseInOut ), 0.25f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.875f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::EaseInOut ), 0.75f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0625f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::Cubic ), 0.25f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::Cubic ), 0.5f ), 1.0e-6f );
    // 지수 2: (1 − e^−1) / (1 − e^−2) = 1 / (1 + e^−1)
    SW_EXPECT_NEAR_EQUAL( 0.7310586f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::Exponential ), 0.5f ), 1.0e-5f );
    // 임계 감쇠 1 Hz · 1 s: x(t) = 1 − (1 + 2πt)e^(−2πt), 끝값으로 나눈다 — x(0.5) = 1 − (1 + π)e^−π = 0.821026, x(1) = 0.986399
    SW_EXPECT_NEAR_EQUAL( 0.821026f / 0.986399f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::Spring ), 0.5f ), 1.0e-4f );
    // 키 (0,0) (0.5,0.8) (1,1) 사이 직선
    SW_EXPECT_NEAR_EQUAL( 0.4f, evaluateBlendWeight( Internal::makeCustomBlend(), 0.25f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.9f, evaluateBlendWeight( Internal::makeCustomBlend(), 0.75f ), 1.0e-6f );
    // 키 없는 Custom 은 Linear
    SW_EXPECT_NEAR_EQUAL( 0.3f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::Custom ), 0.3f ), 1.0e-6f );
    // Cut 은 0 보다 크면 바로 1
    SW_EXPECT_NEAR_EQUAL( 1.0f, evaluateBlendWeight( Internal::makeBlend( BlendCurve::Cut ), 0.01f ), 1.0e-6f );
}

/**
 * @brief [CameraPresetTest] 포즈 섞기 — 자리 · 렌즈는 직선, 회전은 짧은 쪽, 투영은 가중치 0.5 에서 바뀐다
 * @details 요 170° 와 −170° 의 가운데는 180°(짧은 쪽 20°)다 — 긴 쪽(340°)으로 돌면 가운데가 0° 라 앞이 +Z 가 된다.
 */
SW_TEST_CASE( CameraPresetTest, BlendPosesTakesTheShortArcAndSwitchesProjectionAtHalf )
{
    CameraPose from;
    from._position      = float3{ 0.0f, 0.0f, 0.0f };
    from._rotation      = quaternion::createFromYawPitchRoll( 170.0f * MathUtil::DegreeToRadian, 0.0f, 0.0f );
    from._fieldOfViewY  = 1.0f;
    from._bOrthographic = SW_TRUE;
    CameraPose to;
    to._position      = float3{ 10.0f, 4.0f, -2.0f };
    to._rotation      = quaternion::createFromYawPitchRoll( -170.0f * MathUtil::DegreeToRadian, 0.0f, 0.0f );
    to._fieldOfViewY  = 2.0f;
    to._bOrthographic = SW_FALSE;

    const CameraPose middle  = blendPoses( from, to, 0.5f );
    const float3     forward = CameraPresetTestInternal::computeForward( middle._rotation );
    SW_EXPECT_NEAR_EQUAL( 0.0f, forward._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, forward._z, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, middle._position._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, middle._position._y, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.5f, middle._fieldOfViewY, 1.0e-6f );
    SW_EXPECT_TRUE( middle._bOrthographic == SW_FALSE );
    SW_EXPECT_TRUE( blendPoses( from, to, 0.49f )._bOrthographic == SW_TRUE );

    // 같은 회전의 부호 반대 쿼터니언 사이는 돌지 않는다.
    CameraPose negated = from;
    negated._rotation  = -from._rotation;
    const float3 still = CameraPresetTestInternal::computeForward( blendPoses( from, negated, 0.5f )._rotation );
    const float3 start = CameraPresetTestInternal::computeForward( from._rotation );
    SW_EXPECT_NEAR_EQUAL( start._x, still._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( start._z, still._z, 1.0e-4f );
}

/**
 * @brief [CameraPresetTest] 모드마다 알려진 포즈 — 궤도는 초점을 보고, 내려다보기는 직교 리그와 같은 자리, 따라가기는 대상 뒤, 1인칭은 대상 요로 돌린 눈
 */
SW_TEST_CASE( CameraPresetTest, PresetModesProduceKnownPoses )
{
    CameraTarget target;
    target._focus = float3{ 1.0f, 0.0f, 2.0f };
    target._yaw   = MathUtil::HalfPi; // +X 를 본다

    CameraPresetDef orbit;
    orbit._view._mode          = CameraPresetMode::Orbit;
    orbit._view._pitch         = 30.0f * MathUtil::DegreeToRadian;
    orbit._view._distance      = 10.0f;
    const CameraPose orbitPose = evaluatePreset( orbit, target );
    SW_EXPECT_NEAR_EQUAL( 1.0f, orbitPose._position._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, orbitPose._position._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f - 8.660254f, orbitPose._position._z, 1.0e-4f );
    const float3 toFocus      = ( target._focus - orbitPose._position ) * 0.1f;
    const float3 orbitLooksAt = CameraPresetTestInternal::computeForward( orbitPose._rotation );
    SW_EXPECT_NEAR_EQUAL( toFocus._y, orbitLooksAt._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( toFocus._z, orbitLooksAt._z, 1.0e-4f );
    SW_EXPECT_TRUE( orbitPose._bOrthographic == SW_FALSE );

    CameraPresetDef topDown           = orbit;
    topDown._view._mode               = CameraPresetMode::OrthoTopDown;
    topDown._view._yaw                = 45.0f * MathUtil::DegreeToRadian;
    topDown._view._distance           = 250.0f;
    const CameraPose      topDownPose = evaluatePreset( topDown, target );
    const OrthoCameraView rigView     = OrthoCameraRigMath::computeView( target._focus, topDown._view._yaw, topDown._view._pitch, 250.0f );
    SW_EXPECT_NEAR_EQUAL( rigView._position._x, topDownPose._position._x, 1.0e-2f );
    SW_EXPECT_NEAR_EQUAL( rigView._position._y, topDownPose._position._y, 1.0e-2f );
    SW_EXPECT_NEAR_EQUAL( rigView._position._z, topDownPose._position._z, 1.0e-2f );
    SW_EXPECT_TRUE( topDownPose._bOrthographic == SW_TRUE );

    CameraPresetDef follow;
    follow._view._mode          = CameraPresetMode::Follow;
    follow._view._distance      = 5.0f;
    const CameraPose followPose = evaluatePreset( follow, target );
    SW_EXPECT_NEAR_EQUAL( 1.0f - 5.0f, followPose._position._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, followPose._position._z, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, CameraPresetTestInternal::computeForward( followPose._rotation )._x, 1.0e-4f );

    CameraPresetDef firstPerson;
    firstPerson._view._mode   = CameraPresetMode::FirstPerson;
    firstPerson._view._offset = float3{ 0.0f, 1.7f, 0.5f };
    target._pitch             = 10.0f * MathUtil::DegreeToRadian;
    const CameraPose eyePose  = evaluatePreset( firstPerson, target );
    SW_EXPECT_NEAR_EQUAL( 1.5f, eyePose._position._x, 1.0e-4f ); // 앞 0.5 가 +X 로 돌았다
    SW_EXPECT_NEAR_EQUAL( 1.7f, eyePose._position._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, eyePose._position._z, 1.0e-4f );
    const float3 eyeForward = CameraPresetTestInternal::computeForward( eyePose._rotation );
    SW_EXPECT_NEAR_EQUAL( -MathUtil::sin( target._pitch ), eyeForward._y, 1.0e-4f );
    SW_EXPECT_TRUE( eyeForward._x > 0.9f );

    const CameraPose fixedPose = evaluatePreset( CameraPresetTestInternal::makeFixed( "fixed", float3{ 7.0f, 8.0f, 9.0f } ), target );
    SW_EXPECT_NEAR_EQUAL( 8.0f, fixedPose._position._y, 1.0e-6f );
}

/**
 * @brief [CameraPresetTest] 감쇠는 프레임 수와 상관없다 — 0.1 초 한 번과 0.01 초 열 번이 같은 자리다
 */
SW_TEST_CASE( CameraPresetTest, DampingIsFrameRateIndependent )
{
    CameraDampingDef damping;
    damping._positionTime    = 0.2f;
    damping._orientationTime = 0.2f;
    CameraPose start;
    CameraPose goal;
    goal._position = float3{ 10.0f, 0.0f, 0.0f };
    goal._rotation = quaternion::createFromYawPitchRoll( 1.0f, 0.0f, 0.0f );

    const CameraPose once  = dampPose( start, goal, damping, 0.1f );
    CameraPose       steps = start;
    for ( int32 stepIndex = 0; stepIndex < 10; ++stepIndex )
        steps = dampPose( steps, goal, damping, 0.01f );
    SW_EXPECT_NEAR_EQUAL( 10.0f * ( 1.0f - ::expf( -0.5f ) ), once._position._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( once._position._x, steps._position._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( quaternion::getAngleBetween( once._rotation, steps._rotation ), 0.0f, 1.0e-3f );
}

/**
 * @brief [CameraPresetTest] 카탈로그 — 섹션을 읽고(각은 도 → 라디안), 블렌드는 표(정확히 · from="*" · to="*") → 들어오기 → 기본 순, 모르는 이름은 경고한다
 */
SW_TEST_CASE( CameraPresetTest, CatalogReadsSectionsAndResolvesBlends )
{
    const utf8*         pXml = R"(<CameraPresets>
        <DefaultBlend curve="Linear" duration="2"/>
        <Preset id="a">
            <View mode="Orbit" pitch="30" yaw="90" distance="12" offset="0 1 0"/>
            <Lens fieldOfViewY="60" near="0.2" far="300"/>
            <Damping position="0.3" orientation="0.1"/>
            <BlendIn curve="EaseIn" duration="0.7" exponent="3"/>
        </Preset>
        <Preset id="b"><View mode="Fixed"/></Preset>
        <Preset id="c">
            <BlendIn curve="Custom" duration="1"><Key time="1" value="1"/><Key time="0" value="0"/></BlendIn>
        </Preset>
        <Blend from="a" to="b" curve="Cut"/>
        <Blend from="*" to="b" curve="Spring" duration="0.4"/>
        <Blend from="c" to="*" curve="Cubic" duration="0.9"/>
    </CameraPresets>)";
    CameraPresetCatalog catalog;
    {
        test::ScopedDefensiveTestLog expected( "unsorted custom keys are reported and sorted" );
        SW_ASSERT_TRUE( catalog.loadFromXmlText( pXml, "camera-test" ) );
    }
    SW_ASSERT_EQUAL( size_t{ 3 }, catalog.getPresets().size() );

    const CameraPresetDef* pA = catalog.findPreset( "a" );
    SW_ASSERT_NOT_NULL( pA );
    SW_EXPECT_TRUE( pA->_view._mode == CameraPresetMode::Orbit );
    SW_EXPECT_NEAR_EQUAL( 30.0f * MathUtil::DegreeToRadian, pA->_view._pitch, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( MathUtil::HalfPi, pA->_view._yaw, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 12.0f, pA->_view._distance, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pA->_view._offset._y, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 60.0f * MathUtil::DegreeToRadian, pA->_lens._fieldOfViewY, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 300.0f, pA->_lens._farPlane, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.3f, pA->_damping._positionTime, 1.0e-6f );
    SW_EXPECT_TRUE( pA->_blendIn._curve == BlendCurve::EaseIn );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pA->_blendIn._exponent, 1.0e-6f );

    const CameraPresetDef* pC = catalog.findPreset( "c" );
    SW_ASSERT_NOT_NULL( pC );
    SW_ASSERT_EQUAL( size_t{ 2 }, pC->_blendIn._listCustomKey.size() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pC->_blendIn._listCustomKey[0]._time, 1.0e-6f );

    SW_EXPECT_TRUE( catalog.getBlend( "a", "b" )._curve == BlendCurve::Cut );             // 정확히 맞는 줄
    SW_EXPECT_TRUE( catalog.getBlend( "c", "b" )._curve == BlendCurve::Spring );          // from="*" 가 to="*" 보다 먼저
    SW_EXPECT_TRUE( catalog.getBlend( "c", "a" )._curve == BlendCurve::Cubic );           // to="*"
    SW_EXPECT_TRUE( catalog.getBlend( "b", "a" )._curve == BlendCurve::EaseIn );          // a 의 들어오기
    SW_EXPECT_TRUE( catalog.getBlend( "a", "missing" )._curve == BlendCurve::Linear );    // 기본
    SW_EXPECT_NEAR_EQUAL( 2.0f, catalog.findPreset( "b" )->_blendIn._duration, 1.0e-6f ); // <BlendIn> 이 없으면 기본

    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "unknown camera preset names are reported" );
        CameraPresetCatalog          typo;
        SW_EXPECT_TRUE( typo.loadFromXmlText( R"(<CameraPresets><Preset id="x"><View mode="Orbitt" pich="3"/><Lense/></Preset></CameraPresets>)", "typo" ) );
    }
    SW_EXPECT_EQUAL( 1u, logs.countContaining( "unknown attribute 'pich'" ) );
    SW_EXPECT_EQUAL( 1u, logs.countContaining( "unknown mode 'Orbitt'" ) );
    SW_EXPECT_EQUAL( 1u, logs.countContaining( "unknown section <Lense>" ) );
}

/**
 * @brief [CameraPresetTest] 블렌드 도중 다시 켜면 그 순간의 섞인 포즈에서 이어 간다 — 화면이 튀지 않는다
 * @details a(원점) → b(+X 10) 를 1 초 직선으로 반쯤 섞은 자리(5, 0, 0)에서 c(+Y 10)를 켠다. 다음 1 ms 의 포즈가 (5, 0, 0) 곁이어야 한다 —
 *          출발점을 나가는 프리셋(b) · 처음 프리셋(a)으로 잡으면 10 m · 5 m 를 한 번에 뛴다.
 */
SW_TEST_CASE( CameraPresetTest, DirectorContinuesFromTheBlendedPoseWhenReactivatedMidBlend )
{
    CameraPresetCatalog catalog;
    catalog.addPreset( CameraPresetTestInternal::makeFixed( "a", float3{ 0.0f, 0.0f, 0.0f } ) );
    catalog.addPreset( CameraPresetTestInternal::makeFixed( "b", float3{ 10.0f, 0.0f, 0.0f } ) );
    catalog.addPreset( CameraPresetTestInternal::makeFixed( "c", float3{ 0.0f, 10.0f, 0.0f } ) );
    const CameraTarget target;

    CameraDirector director;
    SW_ASSERT_TRUE( director.activatePreset( catalog, "a" ) );
    (void)director.step( 0.016f, target );
    SW_EXPECT_FALSE( director.isBlending() ); // 처음 켠 프리셋은 바로 붙는다
    SW_EXPECT_FALSE( director.activatePreset( catalog, "missing" ) );
    SW_EXPECT_TRUE( director.getActivePresetId() == hashed_string( "a" ) );

    SW_ASSERT_TRUE( director.activatePreset( catalog, "b" ) );
    SW_EXPECT_TRUE( director.isBlending() );
    const CameraPose half = director.step( 0.5f, target );
    SW_EXPECT_NEAR_EQUAL( 5.0f, half._position._x, 1.0e-4f );

    SW_ASSERT_TRUE( director.activatePreset( catalog, "c" ) );
    const CameraPose next = director.step( 0.001f, target );
    SW_EXPECT_NEAR_EQUAL( 5.0f, next._position._x, 0.02f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, next._position._y, 0.02f );

    const CameraPose arrived = director.step( 1.0f, target );
    SW_EXPECT_FALSE( director.isBlending() );
    SW_EXPECT_NEAR_EQUAL( 10.0f, arrived._position._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, arrived._position._x, 1.0e-4f );
}

/**
 * @brief [CameraPresetTest] 블렌드가 없던 때 켜면 나가는 프리셋도 대상을 계속 따라간다
 * @details 따라가기 a 에서 고정 b 로 섞는 동안 대상이 움직이면, 가중치 0 쪽(a)의 자리도 움직인 대상을 따른다 — 켠 순간의 포즈로 얼려 두면
 *          가운데 자리가 대상이 옮긴 만큼의 절반을 놓친다.
 */
SW_TEST_CASE( CameraPresetTest, DirectorKeepsTheOutgoingPresetLive )
{
    CameraPresetDef follow;
    follow._id                  = hashed_string( "follow" );
    follow._view._mode          = CameraPresetMode::Orbit;
    follow._view._distance      = 0.0f;
    const CameraPresetDef fixed = CameraPresetTestInternal::makeFixed( "fixed", float3{ 0.0f, 0.0f, 0.0f } );

    CameraDirector director;
    CameraTarget   target;
    director.activatePreset( follow, CameraPresetTestInternal::makeBlend( BlendCurve::Cut ) );
    (void)director.step( 0.016f, target );
    director.activatePreset( fixed, CameraPresetTestInternal::makeBlend( BlendCurve::Linear ) );
    target._focus          = float3{ 20.0f, 0.0f, 0.0f };
    const CameraPose blend = director.step( 0.5f, target );
    SW_EXPECT_NEAR_EQUAL( 10.0f, blend._position._x, 1.0e-3f );
}

/**
 * @brief [CameraPresetTest] 컴포넌트는 시작 프리셋을 대상에 맞춰 같은 오브젝트의 카메라에 쓰고, 다른 프리셋으로 블렌드해 끝난다
 */
SW_TEST_CASE( CameraPresetTest, DirectorComponentDrivesItsCamera )
{
    GameObjectManager manager;
    GameObject*       pTargetObject = manager.createGameObject( hashed_string( "CameraTarget" ) );
    GameObject*       pCameraObject = manager.createGameObject( hashed_string( "DirectedCamera" ) );
    SW_ASSERT_TRUE( pTargetObject != nullptr && pCameraObject != nullptr );
    SceneComponent*          pTargetScene = pTargetObject->addComponent<SceneComponent>();
    CameraComponent*         pCamera      = pCameraObject->addComponent<CameraComponent>();
    CameraDirectorComponent* pDirector    = pCameraObject->addComponent<CameraDirectorComponent>();
    SW_ASSERT_TRUE( pTargetScene != nullptr && pCamera != nullptr && pDirector != nullptr );
    pTargetScene->setLocalPosition( float3{ 3.0f, 0.0f, 4.0f } );
    pTargetScene->setLocalRotation( float3{ 0.0f, MathUtil::HalfPi, 0.0f } );

    SW_ASSERT_TRUE( pDirector->getCatalog().loadFromXmlText( R"(<CameraPresets>
        <Preset id="behind"><View mode="Follow" pitch="0" distance="5" offset="0 2 0"/><Lens fieldOfViewY="60" far="400"/></Preset>
        <Preset id="above"><View mode="OrthoTopDown" pitch="90" distance="50"/><Lens orthoHeight="30"/><BlendIn curve="SmoothStep" duration="1"/></Preset>
    </CameraPresets>)",
                                                             "component-test" ) );
    pDirector->setTarget( pTargetObject->getHandle() );
    manager.beginPlay();

    SW_EXPECT_TRUE( pDirector->getActivePresetId() == hashed_string( "behind" ) );
    SW_EXPECT_FALSE( pCamera->isOrthographic() );
    SW_EXPECT_NEAR_EQUAL( 60.0f * MathUtil::DegreeToRadian, pCamera->getFieldOfViewY(), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 400.0f, pCamera->getFarPlane(), 1.0e-4f );
    const float3 behind = pCamera->getWorldPosition(); // 대상이 +X 를 보니 뒤는 −X
    SW_EXPECT_NEAR_EQUAL( 3.0f - 5.0f, behind._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, behind._y, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, behind._z, 1.0e-3f );

    SW_ASSERT_TRUE( pDirector->activatePreset( "above" ) );
    pDirector->onTick( 0.25f );
    SW_EXPECT_TRUE( pDirector->isBlending() );
    SW_EXPECT_FALSE( pCamera->isOrthographic() ); // 가중치 0.5 전에는 원근 그대로
    for ( int32 frameIndex = 0; frameIndex < 4; ++frameIndex )
        pDirector->onTick( 0.25f );
    SW_EXPECT_FALSE( pDirector->isBlending() );
    SW_EXPECT_TRUE( pCamera->isOrthographic() );
    SW_EXPECT_NEAR_EQUAL( 30.0f, pCamera->getOrthoHeight(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 50.0f, pCamera->getWorldPosition()._y, 1.0e-2f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pCamera->getWorldPosition()._x, 1.0e-2f );
}

/**
 * @brief [CameraPresetTest] 디렉터는 입력 맵 액션(돌리기 키 없이)이 발동한 프레임에 카탈로그 순서로 다음 프리셋을 켠다
 */
SW_TEST_CASE( CameraPresetTest, CycleActionSwitchesToTheNextPreset )
{
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    game::bindLocalService<InputManager>( &input );
    input.getInputMap().bind( "CycleCamera", Key::C );

    GameObjectManager manager;
    GameObject*       pCameraObject = manager.createGameObject( hashed_string( "DirectedCamera" ) );
    SW_ASSERT_NOT_NULL( pCameraObject );
    SW_ASSERT_NOT_NULL( pCameraObject->addComponent<CameraComponent>() );
    CameraDirectorComponent* pDirector = pCameraObject->addComponent<CameraDirectorComponent>();
    SW_ASSERT_NOT_NULL( pDirector );
    SW_ASSERT_TRUE( pDirector->getCatalog().loadFromXmlText( R"(<CameraPresets>
        <Preset id="first"><View mode="Orbit" distance="5"/></Preset>
        <Preset id="second"><View mode="Orbit" distance="9"/></Preset>
    </CameraPresets>)",
                                                             "cycle-action-test" ) );
    pDirector->setCycleAction( hashed_string( "CycleCamera" ) );
    manager.beginPlay();
    SW_EXPECT_TRUE( pDirector->getActivePresetId() == hashed_string( "first" ) );

    pDirector->onTick( 0.016f ); // 누르지 않았다 — 그대로
    SW_EXPECT_TRUE( pDirector->getActivePresetId() == hashed_string( "first" ) );
    input.postRawEvent( RawInputEvent::makeKeyDown( Key::C ) );
    input.beginFrame( 0.016f );
    pDirector->onTick( 0.016f );
    SW_EXPECT_TRUE( pDirector->getActivePresetId() == hashed_string( "second" ) );

    manager.endPlay();
    game::unbindLocalService<InputManager>();
    input.shutdown();
}
