// AudioSpatialTest — 공간화 계산(믹서 없이): 거리 감쇠 곡선 · 리스너 축 팬 · 도플러 · 2D 화면 팬 · 여러 리스너 합치기.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/AudioPanner.h"
#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Audio/AudioTypes.h"

namespace
{
    struct AudioSpatialTestInternal
    {
        /** @brief +Z 를 보는 3D 리스너(원점)입니다. */
        static sw::AudioListenerState makeListener( const sw::float3& position = sw::float3( 0.0f, 0.0f, 0.0f ) )
        {
            sw::AudioListenerState listener;
            listener._position = position;
            listener._bActive  = true;
            return listener;
        }
    };
} // namespace

/**
 * @brief [AudioSpatialTest] 감쇠 곡선 — 거리마다 볼륨이 곡선의 값이다
 */
SW_TEST_CASE( AudioSpatialTest, AttenuationCurvesAtDistances )
{
    sw::AudioAttenuationDesc inverse;
    inverse._curve       = sw::AudioAttenuationCurve::Inverse;
    inverse._minDistance = 1.0f;
    inverse._maxDistance = 30.0f;
    SW_EXPECT_NEAR( 1.0f, inverse.computeGain( 0.5f ), 1e-6f );
    SW_EXPECT_NEAR( 0.5f, inverse.computeGain( 2.0f ), 1e-6f );
    SW_EXPECT_NEAR( 0.25f, inverse.computeGain( 4.0f ), 1e-6f );
    SW_EXPECT_NEAR( 1.0f / 30.0f, inverse.computeGain( 50.0f ), 1e-6f );

    sw::AudioAttenuationDesc inverseSquare = inverse;
    inverseSquare._curve                   = sw::AudioAttenuationCurve::InverseSquare;
    SW_EXPECT_NEAR( 0.25f, inverseSquare.computeGain( 2.0f ), 1e-6f );
    // 최소 거리가 곡선을 민다: min 2 면 4 m 에서 절반.
    inverse._minDistance = 2.0f;
    SW_EXPECT_NEAR( 0.5f, inverse.computeGain( 4.0f ), 1e-6f );

    sw::AudioAttenuationDesc linear;
    linear._curve       = sw::AudioAttenuationCurve::Linear;
    linear._minDistance = 1.0f;
    linear._maxDistance = 11.0f;
    SW_EXPECT_NEAR( 1.0f, linear.computeGain( 1.0f ), 1e-6f );
    SW_EXPECT_NEAR( 0.5f, linear.computeGain( 6.0f ), 1e-6f );
    SW_EXPECT_NEAR( 0.0f, linear.computeGain( 11.0f ), 1e-6f );
    SW_EXPECT_NEAR( 0.0f, linear.computeGain( 20.0f ), 1e-6f );

    sw::AudioAttenuationDesc custom;
    custom._curve           = sw::AudioAttenuationCurve::Custom;
    custom._listCustomPoint = {
        { 0.0f,   0.0f},
        {10.0f,  -6.0f},
        {20.0f, -60.0f}
    };
    SW_EXPECT_NEAR( -3.0f, sw::AudioMath::linearToDb( custom.computeGain( 5.0f ) ), 1e-3f );
    SW_EXPECT_NEAR( -33.0f, sw::AudioMath::linearToDb( custom.computeGain( 15.0f ) ), 1e-3f );
    SW_EXPECT_NEAR( -60.0f, sw::AudioMath::linearToDb( custom.computeGain( 99.0f ) ), 1e-3f );

    // 공기 흡수 — 점 사이 직선.
    custom._listLowPassPoint = {
        {10.0f, 20000.0f},
        {30.0f,  4000.0f}
    };
    SW_EXPECT_NEAR( 12000.0f, custom.computeLowPassHz( 20.0f ), 1e-2f );
}

/**
 * @brief [AudioSpatialTest] 팬은 리스너의 오른쪽 축이 정하고, 고도는 팬에 쓰지 않는다(엔진 렌더의 등전력 확인은 남긴다)
 */
SW_TEST_CASE( AudioSpatialTest, PanFollowsListenerAxes )
{
    sw::AudioListenerState listener = AudioSpatialTestInternal::makeListener();
    SW_EXPECT_NEAR( 1.0f, sw::AudioSpatializer::computeDirection( listener, sw::float3( 10.0f, 0.0f, 0.0f ), 0.5f )._right, 1e-5f );
    SW_EXPECT_NEAR( -1.0f, sw::AudioSpatializer::computeDirection( listener, sw::float3( -10.0f, 0.0f, 0.0f ), 0.5f )._right, 1e-5f );
    SW_EXPECT_NEAR( 0.0f, sw::AudioSpatializer::computeDirection( listener, sw::float3( 0.0f, 0.0f, 10.0f ), 0.5f )._right, 1e-5f );
    SW_EXPECT_NEAR( 0.70710678f, sw::AudioSpatializer::computeDirection( listener, sw::float3( 10.0f, 0.0f, 10.0f ), 0.5f )._right, 1e-5f );
    // 고도는 팬에 쓰지 않는다 — 머리 위는 가운데.
    SW_EXPECT_NEAR( 0.0f, sw::AudioSpatializer::computeDirection( listener, sw::float3( 0.0f, 10.0f, 0.0f ), 0.5f )._right, 1e-5f );
    SW_EXPECT_NEAR( 1.0f, sw::AudioSpatializer::computeDirection( listener, sw::float3( 10.0f, 10.0f, 0.0f ), 0.5f )._right, 1e-5f );
    // 리스너가 오른쪽(+X)을 보면 +Z 의 소리는 왼쪽이다.
    listener._forward = sw::float3( 1.0f, 0.0f, 0.0f );
    SW_EXPECT_NEAR( -1.0f, sw::AudioSpatializer::computeDirection( listener, sw::float3( 0.0f, 0.0f, 10.0f ), 0.5f )._right, 1e-5f );
}

/**
 * @brief [AudioSpatialTest] 도플러 — 다가오면 (c)/(c − v), 멀어지면 (c)/(c + v), 세기 0 이면 1, 끝은 2 배로 묶인다
 */
SW_TEST_CASE( AudioSpatialTest, DopplerRatio )
{
    const sw::AudioListenerState listener = AudioSpatialTestInternal::makeListener();
    sw::AudioEmitterState        emitter;
    emitter._position = sw::float3( 0.0f, 0.0f, 50.0f );
    emitter._velocity = sw::float3( 0.0f, 0.0f, -34.3f ); // 리스너 쪽으로 34.3 m/s
    SW_EXPECT_NEAR( 343.0f / ( 343.0f - 34.3f ), sw::AudioSpatializer::computeDopplerRatio( listener, emitter, 1.0f ), 1e-4f );
    emitter._velocity = sw::float3( 0.0f, 0.0f, 34.3f );
    SW_EXPECT_NEAR( 343.0f / ( 343.0f + 34.3f ), sw::AudioSpatializer::computeDopplerRatio( listener, emitter, 1.0f ), 1e-4f );
    SW_EXPECT_NEAR( 1.0f, sw::AudioSpatializer::computeDopplerRatio( listener, emitter, 0.0f ), 1e-6f );
    // 세기 0.5 는 차이의 절반.
    const float32 halfRatio = sw::AudioSpatializer::computeDopplerRatio( listener, emitter, 0.5f );
    SW_EXPECT_NEAR( 1.0f + ( 343.0f / ( 343.0f + 34.3f ) - 1.0f ) * 0.5f, halfRatio, 1e-4f );
    // 소리보다 빨라도 2 배를 넘지 않는다.
    emitter._velocity = sw::float3( 0.0f, 0.0f, -2000.0f );
    SW_EXPECT_TRUE( sw::AudioSpatializer::computeDopplerRatio( listener, emitter, 4.0f ) <= sw::AudioSpatializer::kMaxDopplerRatio );
}

/**
 * @brief [AudioSpatialTest] 2D(화면 평면) — 팬은 가로 거리 / 화면 반폭, 세로는 팬에 쓰지 않고, 거리는 XY 거리다
 */
SW_TEST_CASE( AudioSpatialTest, Screen2DPansOnTheScreenPlane )
{
    sw::AudioListenerState listener = AudioSpatialTestInternal::makeListener();
    listener._mode                  = sw::AudioSpatialMode::Screen2D;
    listener._screenHalfWidth       = 10.0f;
    SW_EXPECT_NEAR( 0.5f, sw::AudioSpatializer::computeDirection( listener, sw::float3( 5.0f, 7.0f, 0.0f ), 0.5f )._right, 1e-6f );
    SW_EXPECT_NEAR( 0.5f, sw::AudioSpatializer::computeDirection( listener, sw::float3( 5.0f, -3.0f, 0.0f ), 0.5f )._right, 1e-6f );
    SW_EXPECT_NEAR( 0.0f, sw::AudioSpatializer::computeDirection( listener, sw::float3( 0.0f, 8.0f, 0.0f ), 0.5f )._right, 1e-6f );
    SW_EXPECT_NEAR( -1.0f, sw::AudioSpatializer::computeDirection( listener, sw::float3( -25.0f, 0.0f, 0.0f ), 0.5f )._right, 1e-6f );
    // Z(깊이 · 레이어)는 거리에 들지 않는다.
    SW_EXPECT_NEAR( 5.0f, sw::AudioSpatializer::computeDistance( listener, sw::float3( 3.0f, 4.0f, 100.0f ) ), 1e-5f );

    // 위에서 내려다보는 직교 카메라(XZ 세계 — RTS · 경영): 화면 가로는 +X, 시선(높이)은 거리에서 빠진다.
    listener._forward = sw::float3( 0.0f, -1.0f, 0.0f );
    listener._up      = sw::float3( 0.0f, 0.0f, 1.0f );
    SW_EXPECT_NEAR( 0.5f, sw::AudioSpatializer::computeDirection( listener, sw::float3( 5.0f, -20.0f, 7.0f ), 0.5f )._right, 1e-5f );
    SW_EXPECT_NEAR( 5.0f, sw::AudioSpatializer::computeDistance( listener, sw::float3( 3.0f, -30.0f, 4.0f ) ), 1e-5f );
}

/**
 * @brief [AudioSpatialTest] 리스너 결과 합치기 — Loudest 는 가장 큰 하나, PowerSum 은 √(Σ게인²) · 전력 가중 팬 · 가장 열린 컷오프 · 가장 가까운 거리
 */
SW_TEST_CASE( AudioSpatialTest, CombineListenersByPower )
{
    sw::AudioSpatialResult arrResult[2];
    arrResult[0]._gain             = 0.5f;
    arrResult[0]._direction._right = -1.0f;
    arrResult[0]._lowPassHz        = 900.0f;
    arrResult[0]._distance         = 4.0f;
    arrResult[1]._gain             = 0.5f;
    arrResult[1]._direction._right = 1.0f;
    arrResult[1]._lowPassHz        = 5000.0f;
    arrResult[1]._distance         = 2.0f;

    const sw::AudioSpatialResult loudest = sw::AudioSpatializer::combineListeners( arrResult, 2, sw::AudioListenerMixMode::Loudest );
    SW_EXPECT_NEAR( 0.5f, loudest._gain, 1e-6f );
    SW_EXPECT_NEAR( -1.0f, loudest._direction._right, 1e-6f ); // 같은 크기면 앞의 리스너

    const sw::AudioSpatialResult summed = sw::AudioSpatializer::combineListeners( arrResult, 2, sw::AudioListenerMixMode::PowerSum );
    SW_EXPECT_NEAR( 0.70710678f, summed._gain, 1e-5f );
    SW_EXPECT_NEAR( 0.0f, summed._direction._right, 1e-6f );
    SW_EXPECT_NEAR( 5000.0f, summed._lowPassHz, 1e-3f );
    SW_EXPECT_NEAR( 2.0f, summed._distance, 1e-6f );

    // 한쪽이 더 크면 팬이 그쪽으로 기울고, 들리지 않는 리스너(게인 0)는 컷오프에 끼지 않는다.
    arrResult[0]._gain                  = 0.8f;
    arrResult[1]._gain                  = 0.0f;
    const sw::AudioSpatialResult skewed = sw::AudioSpatializer::combineListeners( arrResult, 2, sw::AudioListenerMixMode::PowerSum );
    SW_EXPECT_NEAR( 0.8f, skewed._gain, 1e-6f );
    SW_EXPECT_NEAR( -1.0f, skewed._direction._right, 1e-6f );
    SW_EXPECT_NEAR( 900.0f, skewed._lowPassHz, 1e-3f );

    // 합이 1 을 넘지 않는다.
    arrResult[0]._gain = 1.0f;
    arrResult[1]._gain = 1.0f;
    SW_EXPECT_NEAR( 1.0f, sw::AudioSpatializer::combineListeners( arrResult, 2, sw::AudioListenerMixMode::PowerSum )._gain, 1e-6f );

    SW_EXPECT_NEAR( 0.0f, sw::AudioSpatializer::combineListeners( arrResult, 0, sw::AudioListenerMixMode::PowerSum )._direction._right, 1e-6f );
}
