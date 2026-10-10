#include "pch.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioSpatial.h"

#include "EngineTest/AudioTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioSpatialTest — 거리 감쇠 곡선 · 등전력 팬 · 도플러 · 가림 → 볼륨/로우패스 · 2D 화면 팬 · 여러 리스너
// ------------------------------------------------------------------------------

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

        /** @brief 감쇠 프리셋 하나(Inverse 1..30)와 sfx · master 만 있는 그래프입니다. 리버브 · 리미터가 없어 숫자가 그대로 나온다. */
        static sw::AudioMixerDesc makeMixer()
        {
            sw::AudioMixerDesc       desc = sw::AudioMixerDesc::makeDefault();
            sw::AudioAttenuationDesc attenuation;
            attenuation._name        = sw::hashed_string( "Test" );
            attenuation._curve       = sw::AudioAttenuationCurve::Inverse;
            attenuation._minDistance = 1.0f;
            attenuation._maxDistance = 30.0f;
            desc._listAttenuation.push_back( attenuation );
            desc._occlusion._volumeDb         = -12.0f;
            desc._occlusion._lowPassHz        = 900.0f;
            desc._occlusion._smoothingSeconds = 0.05f;
            return desc;
        }

        /** @brief 에미터 1 에서 클립을 공간화해 재생합니다. */
        static sw::AudioPlayingID playAtEmitter( sw::AudioEngine& engine, const utf8* pClip )
        {
            sw::AudioClipPlayParams params;
            params._emitterID   = 1;
            params._attenuation = sw::hashed_string( "Test" );
            params._bLoop       = true;
            return engine.playClip( sw::hashed_string( pClip ), sw::hashed_string( "sfx" ), params );
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
    SW_EXPECT_NEAR_EQUAL( 1.0f, inverse.computeGain( 0.5f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, inverse.computeGain( 2.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, inverse.computeGain( 4.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f / 30.0f, inverse.computeGain( 50.0f ), 1e-6f );

    sw::AudioAttenuationDesc inverseSquare = inverse;
    inverseSquare._curve                   = sw::AudioAttenuationCurve::InverseSquare;
    SW_EXPECT_NEAR_EQUAL( 0.25f, inverseSquare.computeGain( 2.0f ), 1e-6f );
    // 최소 거리가 곡선을 민다: min 2 면 4 m 에서 절반.
    inverse._minDistance = 2.0f;
    SW_EXPECT_NEAR_EQUAL( 0.5f, inverse.computeGain( 4.0f ), 1e-6f );

    sw::AudioAttenuationDesc linear;
    linear._curve       = sw::AudioAttenuationCurve::Linear;
    linear._minDistance = 1.0f;
    linear._maxDistance = 11.0f;
    SW_EXPECT_NEAR_EQUAL( 1.0f, linear.computeGain( 1.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, linear.computeGain( 6.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, linear.computeGain( 11.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, linear.computeGain( 20.0f ), 1e-6f );

    sw::AudioAttenuationDesc custom;
    custom._curve           = sw::AudioAttenuationCurve::Custom;
    custom._listCustomPoint = {
        { 0.0f,   0.0f},
        {10.0f,  -6.0f},
        {20.0f, -60.0f}
    };
    SW_EXPECT_NEAR_EQUAL( -3.0f, sw::AudioMath::linearToDb( custom.computeGain( 5.0f ) ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( -33.0f, sw::AudioMath::linearToDb( custom.computeGain( 15.0f ) ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( -60.0f, sw::AudioMath::linearToDb( custom.computeGain( 99.0f ) ), 1e-3f );

    // 공기 흡수 — 점 사이 직선.
    custom._listLowPassPoint = {
        {10.0f, 20000.0f},
        {30.0f,  4000.0f}
    };
    SW_EXPECT_NEAR_EQUAL( 12000.0f, custom.computeLowPassHz( 20.0f ), 1e-2f );
}

/**
 * @brief [AudioSpatialTest] 팬은 리스너의 오른쪽 축이 정하고, 모노 소스는 등전력(L² + R² 일정, 가운데 -3 dB)이다
 */
SW_TEST_CASE( AudioSpatialTest, EqualPowerPanFollowsListenerAxes )
{
    sw::AudioListenerState listener = AudioSpatialTestInternal::makeListener();
    SW_EXPECT_NEAR_EQUAL( 1.0f, sw::AudioSpatializer::computePan( listener, sw::float3( 10.0f, 0.0f, 0.0f ), 0.5f ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, sw::AudioSpatializer::computePan( listener, sw::float3( -10.0f, 0.0f, 0.0f ), 0.5f ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::AudioSpatializer::computePan( listener, sw::float3( 0.0f, 0.0f, 10.0f ), 0.5f ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.70710678f, sw::AudioSpatializer::computePan( listener, sw::float3( 10.0f, 0.0f, 10.0f ), 0.5f ), 1e-5f );
    // 고도는 팬에 쓰지 않는다 — 머리 위는 가운데.
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::AudioSpatializer::computePan( listener, sw::float3( 0.0f, 10.0f, 0.0f ), 0.5f ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, sw::AudioSpatializer::computePan( listener, sw::float3( 10.0f, 10.0f, 0.0f ), 0.5f ), 1e-5f );
    // 리스너가 오른쪽(+X)을 보면 +Z 의 소리는 왼쪽이다.
    listener._forward = sw::float3( 1.0f, 0.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, sw::AudioSpatializer::computePan( listener, sw::float3( 0.0f, 0.0f, 10.0f ), 0.5f ), 1e-5f );

    // 엔진에서: 오른쪽 2 m 의 모노 직류 → 오른쪽만 0.5(Inverse), 가운데 앞 2 m → 두 채널 0.5·cos 45°.
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( engine.initialize( AudioSpatialTestInternal::makeMixer() ) );
    engine.getClipStore().addClip( sw::hashed_string( "test/mono" ), test::AudioTestUtil::makeConstantClip( 1.0f, 48000, 1 ) );
    engine.setListener( 0, AudioSpatialTestInternal::makeListener() );
    engine.setEmitter( 1, sw::float3( 2.0f, 0.0f, 0.0f ), sw::float3( 0.0f, 0.0f, 0.0f ) );
    (void)AudioSpatialTestInternal::playAtEmitter( engine, "test/mono" );
    (void)test::AudioTestUtil::render( engine, 1024 );
    sw::vector<float32> listSample = test::AudioTestUtil::render( engine, 512 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, test::AudioTestUtil::computeMean( listSample, 0, 0, 512 ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, test::AudioTestUtil::computeMean( listSample, 1, 0, 512 ), 1e-3f );

    engine.setEmitter( 1, sw::float3( 0.0f, 0.0f, 2.0f ), sw::float3( 0.0f, 0.0f, 0.0f ) );
    (void)test::AudioTestUtil::render( engine, 512 );
    listSample          = test::AudioTestUtil::render( engine, 512 );
    const float32 left  = test::AudioTestUtil::computeMean( listSample, 0, 0, 512 );
    const float32 right = test::AudioTestUtil::computeMean( listSample, 1, 0, 512 );
    SW_EXPECT_NEAR_EQUAL( 0.5f * 0.70710678f, left, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( left, right, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, left * left + right * right, 1e-3f );
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
    SW_EXPECT_NEAR_EQUAL( 343.0f / ( 343.0f - 34.3f ), sw::AudioSpatializer::computeDopplerRatio( listener, emitter, 1.0f ), 1e-4f );
    emitter._velocity = sw::float3( 0.0f, 0.0f, 34.3f );
    SW_EXPECT_NEAR_EQUAL( 343.0f / ( 343.0f + 34.3f ), sw::AudioSpatializer::computeDopplerRatio( listener, emitter, 1.0f ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, sw::AudioSpatializer::computeDopplerRatio( listener, emitter, 0.0f ), 1e-6f );
    // 세기 0.5 는 차이의 절반.
    const float32 halfRatio = sw::AudioSpatializer::computeDopplerRatio( listener, emitter, 0.5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f + ( 343.0f / ( 343.0f + 34.3f ) - 1.0f ) * 0.5f, halfRatio, 1e-4f );
    // 소리보다 빨라도 2 배를 넘지 않는다.
    emitter._velocity = sw::float3( 0.0f, 0.0f, -2000.0f );
    SW_EXPECT_TRUE( sw::AudioSpatializer::computeDopplerRatio( listener, emitter, 4.0f ) <= sw::AudioSpatializer::kMaxDopplerRatio );
}

/**
 * @brief [AudioSpatialTest] 가림 — 막힌 에미터는 볼륨이 -12 dB 이고 고음이 깎인다(900 Hz 로우패스). 엔진이 부드럽게 따라간다
 */
SW_TEST_CASE( AudioSpatialTest, OcclusionDimsAndMuffles )
{
    float32       arrOpen[2]      = { 0.0f, 0.0f };
    float32       arrOccluded[2]  = { 0.0f, 0.0f };
    const float32 arrFrequency[2] = { 100.0f, 6000.0f };
    for ( uint32 toneIndex = 0; toneIndex < 2; ++toneIndex )
    {
        for ( uint32 caseIndex = 0; caseIndex < 2; ++caseIndex )
        {
            sw::AudioEngine engine;
            SW_ASSERT_TRUE( engine.initialize( AudioSpatialTestInternal::makeMixer() ) );
            engine.getClipStore().addClip( sw::hashed_string( "test/tone" ), test::AudioTestUtil::makeSineClip( arrFrequency[toneIndex], 0.5f, 48000 ) );
            engine.setListener( 0, AudioSpatialTestInternal::makeListener() );
            engine.setEmitter( 1, sw::float3( 0.0f, 0.0f, 1.0f ), sw::float3( 0.0f, 0.0f, 0.0f ) );
            engine.setEmitterOcclusion( 1, caseIndex == 1 ? 1.0f : 0.0f );
            (void)AudioSpatialTestInternal::playAtEmitter( engine, "test/tone" );
            const sw::vector<float32> listSample                  = test::AudioTestUtil::render( engine, 48000 );
            const float32             amplitude                   = test::AudioTestUtil::computeToneAmplitude( listSample, 0, 24000, 24000, arrFrequency[toneIndex] );
            ( caseIndex == 1 ? arrOccluded : arrOpen )[toneIndex] = amplitude;
        }
    }
    // 저음: 볼륨만 -12 dB(900 Hz 로우패스는 100 Hz 를 거의 건드리지 않는다).
    SW_EXPECT_NEAR_EQUAL( -12.0f, sw::AudioMath::linearToDb( arrOccluded[0] / arrOpen[0] ), 0.5f );
    // 고음: -12 dB 에 2 차 로우패스(6 kHz 에서 약 -33 dB)까지.
    SW_EXPECT_TRUE( sw::AudioMath::linearToDb( arrOccluded[1] / arrOpen[1] ) < -40.0f );
}

/**
 * @brief [AudioSpatialTest] 2D(화면 평면) — 팬은 가로 거리 / 화면 반폭, 세로는 팬에 쓰지 않고, 거리는 XY 거리다
 */
SW_TEST_CASE( AudioSpatialTest, Screen2DPansOnTheScreenPlane )
{
    sw::AudioListenerState listener = AudioSpatialTestInternal::makeListener();
    listener._mode                  = sw::AudioSpatialMode::Screen2D;
    listener._screenHalfWidth       = 10.0f;
    SW_EXPECT_NEAR_EQUAL( 0.5f, sw::AudioSpatializer::computePan( listener, sw::float3( 5.0f, 7.0f, 0.0f ), 0.5f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, sw::AudioSpatializer::computePan( listener, sw::float3( 5.0f, -3.0f, 0.0f ), 0.5f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::AudioSpatializer::computePan( listener, sw::float3( 0.0f, 8.0f, 0.0f ), 0.5f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, sw::AudioSpatializer::computePan( listener, sw::float3( -25.0f, 0.0f, 0.0f ), 0.5f ), 1e-6f );
    // Z(깊이 · 레이어)는 거리에 들지 않는다.
    SW_EXPECT_NEAR_EQUAL( 5.0f, sw::AudioSpatializer::computeDistance( listener, sw::float3( 3.0f, 4.0f, 100.0f ) ), 1e-5f );

    // 위에서 내려다보는 직교 카메라(XZ 세계 — RTS · 경영): 화면 가로는 +X, 시선(높이)은 거리에서 빠진다.
    listener._forward = sw::float3( 0.0f, -1.0f, 0.0f );
    listener._up      = sw::float3( 0.0f, 0.0f, 1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, sw::AudioSpatializer::computePan( listener, sw::float3( 5.0f, -20.0f, 7.0f ), 0.5f ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, sw::AudioSpatializer::computeDistance( listener, sw::float3( 3.0f, -30.0f, 4.0f ) ), 1e-5f );
}

/**
 * @brief [AudioSpatialTest] 리스너가 여럿이면(분할 화면) 보이스마다 가장 크게 들리는 리스너로 공간화한다
 */
SW_TEST_CASE( AudioSpatialTest, NearestListenerWins )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( engine.initialize( AudioSpatialTestInternal::makeMixer() ) );
    engine.getClipStore().addClip( sw::hashed_string( "test/stereo" ), test::AudioTestUtil::makeConstantClip( 1.0f, 48000, 2 ) );
    engine.setListener( 0, AudioSpatialTestInternal::makeListener( sw::float3( 0.0f, 0.0f, 0.0f ) ) );
    engine.setListener( 1, AudioSpatialTestInternal::makeListener( sw::float3( 100.0f, 0.0f, 0.0f ) ) );
    // 리스너 1 의 앞 2 m: 스테레오 소스(밸런스 팬 가운데 0 dB)라 두 채널 0.5.
    engine.setEmitter( 1, sw::float3( 100.0f, 0.0f, 2.0f ), sw::float3( 0.0f, 0.0f, 0.0f ) );
    (void)AudioSpatialTestInternal::playAtEmitter( engine, "test/stereo" );
    (void)test::AudioTestUtil::render( engine, 1024 );
    const sw::vector<float32> listSample = test::AudioTestUtil::render( engine, 512 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, test::AudioTestUtil::computeMean( listSample, 0, 0, 512 ), 1e-3f );

    // 리스너 1 을 끄면 리스너 0(100 m 밖, Inverse 1/30)으로 잰다.
    sw::AudioListenerState off = AudioSpatialTestInternal::makeListener();
    off._bActive               = false;
    engine.setListener( 1, off );
    (void)test::AudioTestUtil::render( engine, 512 );
    const sw::vector<float32> listFar = test::AudioTestUtil::render( engine, 512 );
    SW_EXPECT_TRUE( test::AudioTestUtil::computeMean( listFar, 1, 0, 512 ) < 0.04f );
}
