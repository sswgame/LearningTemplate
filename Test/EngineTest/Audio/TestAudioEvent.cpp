#include "pch.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioMixer.h"
#include "Engine/Audio/AudioMixerDesc.h"

#include "EngineTest/AudioTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioEventTest — 사운드 이벤트(컨테이너 · 범위 · 쿨다운 · 동시 재생 상한과 뺏기 · 우선순위 · 가상화 · 파라미터) · 스냅샷
// ------------------------------------------------------------------------------

namespace
{
    struct AudioEventTestInternal
    {
        static constexpr const utf8* kMixerXML = R"(
<AudioMixerDesc _maxRealVoiceCount="2" _inaudibleDb="-60">
	<_listBus>
		<AudioBusDesc _name="master">
			<_listEffect><AudioEffectDesc _type="LowPass" _name="Muffle" /></_listEffect>
		</AudioBusDesc>
		<AudioBusDesc _name="sfx" _parent="master" />
		<AudioBusDesc _name="music" _parent="master" />
	</_listBus>
	<_listAttenuation>
		<AudioAttenuationDesc _name="Near" _curve="Linear" _minDistance="1" _maxDistance="10" />
	</_listAttenuation>
	<_listSnapshot>
		<AudioSnapshotDesc _name="Underwater" _fadeInSeconds="0.5" _fadeOutSeconds="0.5">
			<_listBusVolume><AudioSnapshotBusDesc _bus="sfx" _volumeDb="-6" /></_listBusVolume>
			<_listEffectParameter><AudioSnapshotEffectDesc _bus="master" _effect="Muffle" _parameter="frequencyHz" _value="500" /></_listEffectParameter>
		</AudioSnapshotDesc>
		<AudioSnapshotDesc _name="Pause" _fadeInSeconds="0" _fadeOutSeconds="0">
			<_listBusVolume><AudioSnapshotBusDesc _bus="sfx" _volumeDb="-12" /></_listBusVolume>
		</AudioSnapshotDesc>
	</_listSnapshot>
</AudioMixerDesc>)";

        static constexpr const utf8* kLibraryXML = R"(
<AudioEventLibrary>
	<_listParameter>
		<AudioParameterDesc _name="Speed" _minValue="0" _maxValue="1" _defaultValue="0" />
		<AudioParameterDesc _name="Slow" _minValue="0" _maxValue="1" _defaultValue="0" _seekSpeed="1" />
	</_listParameter>
	<_listEvent>
		<AudioEventDesc _name="Seq" _bus="sfx" _container="Sequence">
			<_listClip><AudioClipEntry _path="test/a" /><AudioClipEntry _path="test/b" /><AudioClipEntry _path="test/c" /></_listClip>
		</AudioEventDesc>
		<AudioEventDesc _name="Rand" _bus="sfx" _container="Random">
			<_listClip><AudioClipEntry _path="test/a" /><AudioClipEntry _path="test/b" /><AudioClipEntry _path="test/c" /></_listClip>
		</AudioEventDesc>
		<AudioEventDesc _name="Weighted" _bus="sfx" _container="Random" _bAvoidRepeat="false">
			<_listClip><AudioClipEntry _path="test/a" _weight="0" /><AudioClipEntry _path="test/b" _weight="1" /></_listClip>
		</AudioEventDesc>
		<AudioEventDesc _name="Layer" _bus="sfx" _container="Layer">
			<_listClip><AudioClipEntry _path="test/a" /><AudioClipEntry _path="test/b" /></_listClip>
		</AudioEventDesc>
		<AudioEventDesc _name="Ranged" _bus="sfx" _volumeDbMin="-6" _volumeDbMax="0" _pitchMin="-2" _pitchMax="2">
			<_listClip><AudioClipEntry _path="test/long" /></_listClip>
		</AudioEventDesc>
		<AudioEventDesc _name="Cool" _bus="sfx" _cooldownSeconds="0.1" _bLoop="true">
			<_listClip><AudioClipEntry _path="test/a" /></_listClip>
		</AudioEventDesc>
		<AudioEventDesc _name="Oldest" _bus="sfx" _maxInstances="2" _steal="Oldest" _fadeOutSeconds="0" _bLoop="true">
			<_listClip><AudioClipEntry _path="test/a" /></_listClip>
		</AudioEventDesc>
		<AudioEventDesc _name="Reject" _bus="sfx" _maxInstances="2" _steal="Reject" _bLoop="true">
			<_listClip><AudioClipEntry _path="test/a" /></_listClip>
		</AudioEventDesc>
		<AudioEventDesc _name="Farthest" _bus="sfx" _attenuation="Near" _maxInstances="2" _steal="Farthest" _fadeOutSeconds="0" _bLoop="true">
			<_listClip><AudioClipEntry _path="test/a" /></_listClip>
		</AudioEventDesc>
		<AudioEventDesc _name="High" _bus="sfx" _priority="80" _bLoop="true"><_listClip><AudioClipEntry _path="test/a" /></_listClip></AudioEventDesc>
		<AudioEventDesc _name="Mid" _bus="sfx" _priority="50" _bLoop="true"><_listClip><AudioClipEntry _path="test/b" /></_listClip></AudioEventDesc>
		<AudioEventDesc _name="Low" _bus="sfx" _priority="20"><_listClip><AudioClipEntry _path="test/ramp" /></_listClip></AudioEventDesc>
		<AudioEventDesc _name="FarVirtual" _bus="sfx" _attenuation="Near" _bLoop="true"><_listClip><AudioClipEntry _path="test/a" /></_listClip></AudioEventDesc>
		<AudioEventDesc _name="FarStop" _bus="sfx" _attenuation="Near" _virtual="Stop" _bLoop="true"><_listClip><AudioClipEntry _path="test/a" /></_listClip></AudioEventDesc>
		<AudioEventDesc _name="Engine" _bus="sfx" _bLoop="true">
			<_listClip><AudioClipEntry _path="test/one" /></_listClip>
			<_listParameterMap>
				<AudioParameterMapping _parameter="Speed" _target="Volume">
					<_listPoint><AudioCurvePoint _x="0" _y="-20" /><AudioCurvePoint _x="1" _y="0" /></_listPoint>
				</AudioParameterMapping>
			</_listParameterMap>
		</AudioEventDesc>
		<AudioEventDesc _name="Loop" _bus="sfx" _bLoop="true"><_listClip><AudioClipEntry _path="test/one" /></_listClip></AudioEventDesc>
	</_listEvent>
</AudioEventLibrary>)";

        /** @brief 그래프 · 라이브러리 · 직류 클립(0.1 · 0.2 · 0.3 · 1.0 · 긴 0.5 · 램프)을 올린 엔진입니다. */
        static bool initializeEngine( sw::AudioEngine& engine )
        {
            sw::AudioMixerDesc mixerDesc;
            if ( mixerDesc.loadFromXMLText( kMixerXML ) == false || engine.initialize( mixerDesc ) == false )
                return false;
            sw::AudioClipStore& store = engine.getClipStore();
            store.addClip( sw::hashed_string( "test/a" ), test::AudioTestUtil::makeConstantClip( 0.1f, 2048 ) );
            store.addClip( sw::hashed_string( "test/b" ), test::AudioTestUtil::makeConstantClip( 0.2f, 2048 ) );
            store.addClip( sw::hashed_string( "test/c" ), test::AudioTestUtil::makeConstantClip( 0.3f, 2048 ) );
            store.addClip( sw::hashed_string( "test/one" ), test::AudioTestUtil::makeConstantClip( 1.0f, 2048 ) );
            store.addClip( sw::hashed_string( "test/long" ), test::AudioTestUtil::makeConstantClip( 0.5f, 4800 ) );
            sw::shared_ptr<sw::AudioClipData> pRamp = sw::make_shared<sw::AudioClipData>();
            pRamp->_channelCount                    = 2;
            pRamp->_sampleRate                      = sw::audio::kSampleRate;
            pRamp->_frameCount                      = 48000;
            pRamp->_listSample.resize( 96000 );
            for ( uint32 frameIndex = 0; frameIndex < 48000; ++frameIndex )
            {
                pRamp->_listSample[frameIndex * 2]     = static_cast<float32>( frameIndex ) / 48000.0f;
                pRamp->_listSample[frameIndex * 2 + 1] = static_cast<float32>( frameIndex ) / 48000.0f;
            }
            store.addClip( sw::hashed_string( "test/ramp" ), pRamp );

            sw::AudioEventLibrary library;
            if ( library.loadFromXMLText( kLibraryXML ) == false )
                return false;
            engine.setRandomSeed( 1234 );
            return engine.loadEventLibrary( sw::hashed_string( "test/events" ), library );
        }

        /** @brief 두 블록(게인 램프)을 넘긴 뒤 한 블록의 왼쪽 평균입니다. */
        static float32 renderSteadyLeft( sw::AudioEngine& engine )
        {
            (void)test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount * 2 );
            const sw::vector<float32> listSample = test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount );
            return test::AudioTestUtil::computeMean( listSample, 0, 0, sw::audio::kBlockFrameCount );
        }

        /** @brief 이벤트를 내고, 그 소리의 정상 레벨을 잰 뒤 소리가 끝날 때까지 렌더합니다. */
        static float32 postAndMeasure( sw::AudioEngine& engine, const utf8* pEvent )
        {
            (void)engine.postEvent( sw::hashed_string( pEvent ) );
            const float32 level = renderSteadyLeft( engine );
            (void)test::AudioTestUtil::render( engine, 4096 );
            return level;
        }

        /** @brief 리스너를 원점에 둡니다. */
        static void placeListener( sw::AudioEngine& engine )
        {
            sw::AudioListenerState listener;
            listener._bActive = true;
            engine.setListener( 0, listener );
        }
    };
} // namespace

/**
 * @brief [AudioEventTest] 컨테이너 — Sequence 는 적은 순서대로 돌고, Random 은 바로 앞 클립을 되풀이하지 않고 가중치를 따르며, Layer 는 전부 함께 낸다
 */
SW_TEST_CASE( AudioEventTest, ContainersPickClipsByData )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioEventTestInternal::initializeEngine( engine ) );
    // 스테레오 직류 클립 · 가운데 팬 = 클립 값 그대로.
    const float32 arrExpected[6] = { 0.1f, 0.2f, 0.3f, 0.1f, 0.2f, 0.3f };
    for ( const float32 expected : arrExpected )
    {
        SW_EXPECT_NEAR_EQUAL( expected, AudioEventTestInternal::postAndMeasure( engine, "Seq" ), 1e-3f );
    }

    float32 previous = -1.0f;
    for ( uint32 postIndex = 0; postIndex < 30; ++postIndex )
    {
        const float32 level = AudioEventTestInternal::postAndMeasure( engine, "Rand" );
        SW_EXPECT_TRUE( sw::MathUtil::abs( level - previous ) > 0.05f );
        previous = level;
    }
    for ( uint32 postIndex = 0; postIndex < 5; ++postIndex )
    {
        SW_EXPECT_NEAR_EQUAL( 0.2f, AudioEventTestInternal::postAndMeasure( engine, "Weighted" ), 1e-3f );
    }
    SW_EXPECT_NEAR_EQUAL( 0.3f, AudioEventTestInternal::postAndMeasure( engine, "Layer" ), 1e-3f );

    // 모르는 이벤트는 0 이다.
    SW_TEST_DEFENSIVE_SCOPE( "Posting an unknown event returns 0" );
    SW_EXPECT_EQUAL( sw::AudioPlayingID{ 0 }, engine.postEvent( sw::hashed_string( "NoSuchEvent" ) ) );
}

/**
 * @brief [AudioEventTest] 볼륨 · 피치 범위 — 재생마다 범위 안에서 다르고(-6..0 dB · ±2 반음), 피치는 길이로 드러난다
 */
SW_TEST_CASE( AudioEventTest, VolumeAndPitchRangesStayInside )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioEventTestInternal::initializeEngine( engine ) );
    float32 lowest         = 1.0f;
    float32 highest        = 0.0f;
    uint32  shortestLength = 0xFFFFFFFFu;
    uint32  longestLength  = 0;
    for ( uint32 postIndex = 0; postIndex < 24; ++postIndex )
    {
        (void)engine.postEvent( sw::hashed_string( "Ranged" ) );
        const sw::vector<float32> listSample = test::AudioTestUtil::render( engine, 6144 );
        const float32             level      = test::AudioTestUtil::computeMean( listSample, 0, 512, 256 );
        lowest                               = sw::MathUtil::min( lowest, level );
        highest                              = sw::MathUtil::max( highest, level );
        uint32 length                        = 0;
        for ( uint32 frameIndex = 0; frameIndex < 6144; ++frameIndex )
        {
            if ( listSample[frameIndex * 2] > 1e-4f )
                length = frameIndex + 1;
        }
        shortestLength = sw::MathUtil::min( shortestLength, length );
        longestLength  = sw::MathUtil::max( longestLength, length );
    }
    // 0.5 × [-6 dB, 0 dB] = [0.25, 0.5], 실제로 퍼졌다.
    SW_EXPECT_TRUE( lowest >= 0.25f - 1e-3f && highest <= 0.5f + 1e-3f );
    SW_EXPECT_TRUE( highest - lowest > 0.1f );
    // 4800 프레임 클립을 2^(±2/12) 로 = [4276, 5388] 프레임.
    SW_EXPECT_TRUE( shortestLength >= 4270u && longestLength <= 5395u );
    SW_EXPECT_TRUE( longestLength - shortestLength > 300u );
}

/**
 * @brief [AudioEventTest] 쿨다운 — 0.1 초 안에 다시 낸 것은 버려지고, 지나면 다시 난다
 */
SW_TEST_CASE( AudioEventTest, CooldownDropsTooCloseRepeats )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioEventTestInternal::initializeEngine( engine ) );
    const sw::AudioPlayingID first  = engine.postEvent( sw::hashed_string( "Cool" ) );
    const sw::AudioPlayingID second = engine.postEvent( sw::hashed_string( "Cool" ) );
    (void)test::AudioTestUtil::render( engine, 2400 );
    SW_EXPECT_TRUE( engine.isPlaying( first ) );
    SW_EXPECT_FALSE( engine.isPlaying( second ) );
    SW_EXPECT_EQUAL( 1u, engine.getStats()._instanceCount );

    (void)test::AudioTestUtil::render( engine, 4800 );
    const sw::AudioPlayingID third = engine.postEvent( sw::hashed_string( "Cool" ) );
    (void)test::AudioTestUtil::render( engine, 256 );
    SW_EXPECT_TRUE( engine.isPlaying( third ) );
    SW_EXPECT_EQUAL( 2u, engine.getStats()._instanceCount );
}

/**
 * @brief [AudioEventTest] 동시 재생 상한 — Oldest 는 가장 먼저 낸 것을, Farthest 는 가장 먼 것을 멈추고 새것을 내며, Reject 는 새것을 버린다
 */
SW_TEST_CASE( AudioEventTest, InstanceLimitStealsOrRejects )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioEventTestInternal::initializeEngine( engine ) );
    sw::AudioPlayingID arrOldest[3] = {};
    for ( sw::AudioPlayingID& playingID : arrOldest )
    {
        playingID = engine.postEvent( sw::hashed_string( "Oldest" ) );
        (void)test::AudioTestUtil::render( engine, 256 );
    }
    (void)test::AudioTestUtil::render( engine, 512 );
    SW_EXPECT_FALSE( engine.isPlaying( arrOldest[0] ) );
    SW_EXPECT_TRUE( engine.isPlaying( arrOldest[1] ) );
    SW_EXPECT_TRUE( engine.isPlaying( arrOldest[2] ) );

    sw::AudioPlayingID arrReject[3] = {};
    for ( sw::AudioPlayingID& playingID : arrReject )
    {
        playingID = engine.postEvent( sw::hashed_string( "Reject" ) );
        (void)test::AudioTestUtil::render( engine, 256 );
    }
    SW_EXPECT_TRUE( engine.isPlaying( arrReject[0] ) );
    SW_EXPECT_TRUE( engine.isPlaying( arrReject[1] ) );
    SW_EXPECT_FALSE( engine.isPlaying( arrReject[2] ) );

    // 가장 먼 것(8 m)을 뺏는다 — 가까운 2 m 는 남는다.
    AudioEventTestInternal::placeListener( engine );
    engine.setEmitter( 11, sw::float3( 2.0f, 0.0f, 0.0f ), sw::float3( 0.0f, 0.0f, 0.0f ) );
    engine.setEmitter( 12, sw::float3( 8.0f, 0.0f, 0.0f ), sw::float3( 0.0f, 0.0f, 0.0f ) );
    engine.setEmitter( 13, sw::float3( 4.0f, 0.0f, 0.0f ), sw::float3( 0.0f, 0.0f, 0.0f ) );
    const sw::AudioPlayingID nearID = engine.postEvent( sw::hashed_string( "Farthest" ), 11 );
    const sw::AudioPlayingID farID  = engine.postEvent( sw::hashed_string( "Farthest" ), 12 );
    (void)test::AudioTestUtil::render( engine, 512 );
    const sw::AudioPlayingID newID = engine.postEvent( sw::hashed_string( "Farthest" ), 13 );
    (void)test::AudioTestUtil::render( engine, 512 );
    SW_EXPECT_TRUE( engine.isPlaying( nearID ) );
    SW_EXPECT_FALSE( engine.isPlaying( farID ) );
    SW_EXPECT_TRUE( engine.isPlaying( newID ) );
}

/**
 * @brief [AudioEventTest] 실제 보이스 상한(2) — 우선순위가 낮은 것이 가상이 되어 시간만 흐르고, 자리가 나면 그 시각의 자리에서 이어진다.
 *        들리지 않는 보이스는 가상(Virtualize) · 정지(Stop)다
 */
SW_TEST_CASE( AudioEventTest, VoiceLimitVirtualizesAndKeepsTime )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioEventTestInternal::initializeEngine( engine ) );
    // Low 를 먼저 낸다 — 먼저 낸 순서가 아니라 우선순위가 고르는지 본다.
    const sw::AudioPlayingID lowID  = engine.postEvent( sw::hashed_string( "Low" ) );
    const sw::AudioPlayingID highID = engine.postEvent( sw::hashed_string( "High" ) );
    const sw::AudioPlayingID midID  = engine.postEvent( sw::hashed_string( "Mid" ) );
    (void)test::AudioTestUtil::render( engine, 512 );
    sw::AudioEngineStats stats = engine.getStats();
    SW_EXPECT_EQUAL( 2u, stats._realVoiceCount );
    SW_EXPECT_EQUAL( 1u, stats._virtualVoiceCount );
    // 섞인 것은 High(0.1) + Mid(0.2) — Low 의 램프는 들리지 않는다.
    SW_EXPECT_NEAR_EQUAL( 0.3f, AudioEventTestInternal::renderSteadyLeft( engine ), 1e-3f );

    // 0.5 초 동안 가상으로 흐른 뒤 자리가 나면 램프의 0.5 근처에서 이어진다(처음부터가 아니다).
    (void)test::AudioTestUtil::render( engine, 24000 - 512 - 768 );
    engine.stop( highID, 0.0f );
    engine.stop( midID, 0.0f );
    const float32 resumed = AudioEventTestInternal::renderSteadyLeft( engine );
    SW_EXPECT_TRUE( 0.5f < resumed && resumed < 0.56f );
    SW_EXPECT_TRUE( engine.isPlaying( lowID ) );

    // 들리지 않는 자리(Linear 10 m 밖)는 가상이거나 정지다.
    AudioEventTestInternal::placeListener( engine );
    engine.setEmitter( 21, sw::float3( 50.0f, 0.0f, 0.0f ), sw::float3( 0.0f, 0.0f, 0.0f ) );
    const sw::AudioPlayingID virtualID = engine.postEvent( sw::hashed_string( "FarVirtual" ), 21 );
    const sw::AudioPlayingID stopID    = engine.postEvent( sw::hashed_string( "FarStop" ), 21 );
    (void)test::AudioTestUtil::render( engine, 512 );
    SW_EXPECT_TRUE( engine.isPlaying( virtualID ) );
    SW_EXPECT_FALSE( engine.isPlaying( stopID ) );
    stats = engine.getStats();
    SW_EXPECT_TRUE( stats._virtualVoiceCount >= 1u );
}

/**
 * @brief [AudioEventTest] 파라미터(RTPC) — 곡선이 볼륨을 정하고, seek 속도로 따라가며, 에미터 값이 전역 값보다 앞선다
 */
SW_TEST_CASE( AudioEventTest, ParametersDriveTheMix )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioEventTestInternal::initializeEngine( engine ) );
    (void)engine.postEvent( sw::hashed_string( "Engine" ), 31 );
    engine.setParameter( sw::hashed_string( "Speed" ), 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.1f, AudioEventTestInternal::renderSteadyLeft( engine ), 1e-3f );
    engine.setParameter( sw::hashed_string( "Speed" ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( sw::AudioMath::dbToLinear( -10.0f ), AudioEventTestInternal::renderSteadyLeft( engine ), 1e-3f );
    engine.setEmitterParameter( 31, sw::hashed_string( "Speed" ), 1.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, AudioEventTestInternal::renderSteadyLeft( engine ), 1e-3f );

    // 범위 밖은 묶이고, seek 속도 1/초면 0.5 초 뒤 0.5.
    engine.setParameter( sw::hashed_string( "Slow" ), 7.0f );
    (void)test::AudioTestUtil::render( engine, 24064 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, engine.getParameterValue( sw::hashed_string( "Slow" ) ), 0.02f );
    (void)test::AudioTestUtil::render( engine, 48000 );
    SW_EXPECT_NEAR_EQUAL( 1.0f, engine.getParameterValue( sw::hashed_string( "Slow" ) ), 1e-6f );
}

/**
 * @brief [AudioEventTest] 스냅샷 — 시간에 걸쳐 버스 볼륨 · 이펙트 파라미터를 바꾸고(세기 0.5 에서 -3 dB), 겹치면 dB 가 더해지고, 끄면 돌아온다
 */
SW_TEST_CASE( AudioEventTest, SnapshotsBlendOverTime )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioEventTestInternal::initializeEngine( engine ) );
    (void)engine.postEvent( sw::hashed_string( "Loop" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, AudioEventTestInternal::renderSteadyLeft( engine ), 1e-3f );

    engine.startSnapshot( sw::hashed_string( "Underwater" ) );
    (void)test::AudioTestUtil::render( engine, 12000 - 768 );
    const float32 halfway = AudioEventTestInternal::renderSteadyLeft( engine );
    SW_EXPECT_NEAR_EQUAL( 0.5f, engine.getSnapshotIntensity( sw::hashed_string( "Underwater" ) ), 0.02f );
    SW_EXPECT_NEAR_EQUAL( -3.0f, sw::AudioMath::linearToDb( halfway ), 0.3f );
    (void)test::AudioTestUtil::render( engine, 24000 );
    SW_EXPECT_NEAR_EQUAL( -6.0f, sw::AudioMath::linearToDb( AudioEventTestInternal::renderSteadyLeft( engine ) ), 0.05f );
    // 이펙트 파라미터: master 의 Muffle 컷오프가 500 Hz 로 내려왔다(직류는 그대로 지나간다).
    const sw::IAudioEffect* pMuffle = engine.getMixer()->findEffect( 0, sw::hashed_string( "Muffle" ) );
    SW_ASSERT_TRUE( pMuffle != nullptr );
    SW_EXPECT_NEAR_EQUAL( 500.0f, pMuffle->getParameter( 0 ), 1.0f );

    // 둘째 스냅샷(Pause, 페이드 없음)과 겹치면 dB 가 더해진다: -6 + -12.
    engine.startSnapshot( sw::hashed_string( "Pause" ) );
    SW_EXPECT_NEAR_EQUAL( -18.0f, sw::AudioMath::linearToDb( AudioEventTestInternal::renderSteadyLeft( engine ) ), 0.05f );

    // 끄면 데이터 값으로 돌아온다.
    engine.stopSnapshot( sw::hashed_string( "Pause" ) );
    engine.stopSnapshot( sw::hashed_string( "Underwater" ) );
    (void)test::AudioTestUtil::render( engine, 30000 );
    SW_EXPECT_NEAR_EQUAL( 1.0f, AudioEventTestInternal::renderSteadyLeft( engine ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 20000.0f, pMuffle->getParameter( 0 ), 1.0f );

    // 세기를 직접 정하면 페이드 없이 바로 그 세기다(리버브 존).
    engine.setSnapshotIntensity( sw::hashed_string( "Underwater" ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( -3.0f, sw::AudioMath::linearToDb( AudioEventTestInternal::renderSteadyLeft( engine ) ), 0.05f );
}

/**
 * @brief [AudioEventTest] 라이브러리 검사 — 모르는 버스 · 감쇠 · 파라미터, 클립 없는 이벤트, 다른 라이브러리와 겹친 이벤트 이름은 올리기 오류다
 */
SW_TEST_CASE( AudioEventTest, LibraryNamesAreValidated )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioEventTestInternal::initializeEngine( engine ) );
    SW_TEST_DEFENSIVE_SCOPE( "Event libraries with unknown names are rejected" );
    sw::AudioEventLibrary library;
    SW_EXPECT_FALSE( library.loadFromXMLText( R"(<AudioEventLibrary><_listEvent><AudioEventDesc _name="Empty" _bus="sfx" /></_listEvent></AudioEventLibrary>)" ) );
    SW_EXPECT_FALSE( library.loadFromXMLText( R"(<AudioEventLibrary><_listEvent><AudioEventDesc _name="P" _bus="sfx">
        <_listClip><AudioClipEntry _path="test/a" /></_listClip>
        <_listParameterMap><AudioParameterMapping _parameter="Nope"><_listPoint><AudioCurvePoint /></_listPoint></AudioParameterMapping></_listParameterMap>
        </AudioEventDesc></_listEvent></AudioEventLibrary>)" ) );

    SW_ASSERT_TRUE( library.loadFromXMLText( R"(<AudioEventLibrary><_listEvent><AudioEventDesc _name="Bad" _bus="nowhere">
        <_listClip><AudioClipEntry _path="test/a" /></_listClip></AudioEventDesc></_listEvent></AudioEventLibrary>)" ) );
    SW_EXPECT_FALSE( engine.loadEventLibrary( sw::hashed_string( "test/bad" ), library ) );
    SW_ASSERT_TRUE( library.loadFromXMLText( R"(<AudioEventLibrary><_listEvent><AudioEventDesc _name="Bad" _bus="sfx" _attenuation="Nope">
        <_listClip><AudioClipEntry _path="test/a" /></_listClip></AudioEventDesc></_listEvent></AudioEventLibrary>)" ) );
    SW_EXPECT_FALSE( engine.loadEventLibrary( sw::hashed_string( "test/bad" ), library ) );
    SW_ASSERT_TRUE( library.loadFromXMLText( R"(<AudioEventLibrary><_listEvent><AudioEventDesc _name="Seq" _bus="sfx">
        <_listClip><AudioClipEntry _path="test/a" /></_listClip></AudioEventDesc></_listEvent></AudioEventLibrary>)" ) );
    SW_EXPECT_FALSE( engine.loadEventLibrary( sw::hashed_string( "test/other" ), library ) );
    SW_EXPECT_FALSE( engine.hasEvent( sw::hashed_string( "Bad" ) ) );
    SW_EXPECT_TRUE( engine.hasEvent( sw::hashed_string( "Seq" ) ) );
}
