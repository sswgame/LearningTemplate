#include "pch.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioMusic.h"

#include "EngineTest/AudioTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioMusicTest — 적응형 음악: 박 · 마디 · 구간 끝에 맞춘 샘플 단위 전환, 스팅어, 파라미터 레이어 페이드, 템포 자리
// ------------------------------------------------------------------------------

namespace
{
    struct AudioMusicTestInternal
    {
        /** @brief 120 BPM 4/4 — 한 박 24000 프레임, 한 마디 96000 프레임. 구간은 모두 한 마디입니다. */
        static constexpr const utf8* kMusicXML = R"(
<AudioMusicDesc _bus="music" _tempo="120" _beatsPerBar="4" _startSegment="A" _layerFadeSeconds="0.5">
	<_listSegment>
		<AudioMusicSegmentDesc _name="A" _bars="1"><_listLayer><AudioMusicLayerDesc _path="m/a" /></_listLayer></AudioMusicSegmentDesc>
		<AudioMusicSegmentDesc _name="B" _bars="1">
			<_listLayer>
				<AudioMusicLayerDesc _path="m/b" />
				<AudioMusicLayerDesc _path="m/drums" _parameter="Intensity">
					<_listPoint><AudioCurvePoint _x="0" _y="-96" /><AudioCurvePoint _x="1" _y="0" /></_listPoint>
				</AudioMusicLayerDesc>
			</_listLayer>
		</AudioMusicSegmentDesc>
		<AudioMusicSegmentDesc _name="C" _bars="1"><_listLayer><AudioMusicLayerDesc _path="m/b" /></_listLayer></AudioMusicSegmentDesc>
		<AudioMusicSegmentDesc _name="Intro" _bars="1" _bLoop="false" _next="A"><_listLayer><AudioMusicLayerDesc _path="m/intro" /></_listLayer></AudioMusicSegmentDesc>
	</_listSegment>
	<_listTransition>
		<AudioMusicTransitionDesc _from="A" _to="B" _sync="NextBeat" _stinger="m/sting" />
		<AudioMusicTransitionDesc _from="*" _to="C" _sync="NextBar" />
	</_listTransition>
</AudioMusicDesc>)";

        /** @brief 기본 그래프(music 버스, 이펙트 없음)와 직류 클립을 올린 엔진입니다. */
        static bool initializeEngine( sw::AudioEngine& engine )
        {
            if ( engine.initialize( sw::AudioMixerDesc::makeDefault() ) == false )
                return false;
            sw::AudioClipStore& store = engine.getClipStore();
            store.addClip( sw::hashed_string( "m/a" ), test::AudioTestUtil::makeConstantClip( 0.25f, 96000 ) );
            store.addClip( sw::hashed_string( "m/b" ), test::AudioTestUtil::makeConstantClip( 0.5f, 96000 ) );
            store.addClip( sw::hashed_string( "m/drums" ), test::AudioTestUtil::makeConstantClip( 0.3f, 96000 ) );
            store.addClip( sw::hashed_string( "m/intro" ), test::AudioTestUtil::makeConstantClip( 0.125f, 96000 ) );
            store.addClip( sw::hashed_string( "m/sting" ), test::AudioTestUtil::makeConstantClip( 0.1f, 4800 ) );
            return true;
        }

        /** @brief 음악 서술을 읽고 시작 구간을 바꿉니다. */
        static sw::shared_ptr<const sw::AudioMusicDesc> makeMusic( const utf8* pStartSegment )
        {
            sw::shared_ptr<sw::AudioMusicDesc> pMusic = sw::make_shared<sw::AudioMusicDesc>();
            if ( pMusic->loadFromXMLText( kMusicXML ) == false )
                return nullptr;
            pMusic->_startSegment = sw::hashed_string( pStartSegment );
            return pMusic;
        }

        /** @brief 이어 붙인 렌더 결과에 @p frameCount 프레임을 더 붙입니다. */
        static void appendRender( sw::AudioEngine& engine, sw::vector<float32>& inoutListSample, uint32 frameCount )
        {
            const sw::vector<float32> listMore = test::AudioTestUtil::render( engine, frameCount );
            inoutListSample.insert( inoutListSample.end(), listMore.begin(), listMore.end() );
        }

        /** @brief 왼쪽 채널 한 프레임입니다. */
        static float32 leftAt( const sw::vector<float32>& listSample, uint32 frameIndex ) { return listSample[static_cast<size_t>( frameIndex ) * 2]; }
    };
} // namespace

/**
 * @brief [AudioMusicTest] 전환이 다음 박(48000 프레임)에 샘플 단위로 떨어지고, 그 자리에서 스팅어가 난다 — 박 앞 샘플은 옛 구간, 박 샘플부터 새 구간
 */
SW_TEST_CASE( AudioMusicTest, TransitionLandsOnTheBeat )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioMusicTestInternal::initializeEngine( engine ) );
    SW_ASSERT_TRUE( engine.startMusic( AudioMusicTestInternal::makeMusic( "A" ), 0.0f ) != 0 );
    sw::vector<float32> listSample;
    AudioMusicTestInternal::appendRender( engine, listSample, 30000 );
    engine.setMusicSegment( sw::hashed_string( "B" ) );
    AudioMusicTestInternal::appendRender( engine, listSample, 30000 );

    SW_EXPECT_NEAR_EQUAL( 0.25f, AudioMusicTestInternal::leftAt( listSample, 40000 ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, AudioMusicTestInternal::leftAt( listSample, 47999 ), 1e-5f );
    // 새 구간(0.5) + 스팅어(0.1). 드럼 레이어는 파라미터 0 이라 -96 dB(0).
    SW_EXPECT_NEAR_EQUAL( 0.6f, AudioMusicTestInternal::leftAt( listSample, 48000 ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.6f, AudioMusicTestInternal::leftAt( listSample, 52799 ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, AudioMusicTestInternal::leftAt( listSample, 52800 ), 1e-5f );

    // 템포 자리: B 는 48000 에서 시작했다.
    const sw::AudioMusicStatus status = engine.getMusicStatus();
    SW_EXPECT_TRUE( status._bPlaying );
    SW_EXPECT_TRUE( status._segment == sw::hashed_string( "B" ) );
    SW_EXPECT_NEAR_EQUAL( 120.0f, status._tempo, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( ( static_cast<float64>( engine.getStats()._renderedFrameCount ) - 48000.0 ) / 24000.0, status._beat, 1e-6 );
}

/**
 * @brief [AudioMusicTest] 다음 마디(96000) 맞춤 · 루프하지 않는 구간의 끝에서 다음 구간으로 잇기 · 기다리는 동안 목적지를 바꾸면 같은 경계에 새 목적지
 */
SW_TEST_CASE( AudioMusicTest, BarAndSegmentEndBoundaries )
{
    {
        sw::AudioEngine engine;
        SW_ASSERT_TRUE( AudioMusicTestInternal::initializeEngine( engine ) );
        (void)engine.startMusic( AudioMusicTestInternal::makeMusic( "A" ), 0.0f );
        sw::vector<float32> listSample;
        AudioMusicTestInternal::appendRender( engine, listSample, 30000 );
        engine.setMusicSegment( sw::hashed_string( "C" ) );
        AudioMusicTestInternal::appendRender( engine, listSample, 70000 );
        SW_EXPECT_NEAR_EQUAL( 0.25f, AudioMusicTestInternal::leftAt( listSample, 95999 ), 1e-5f );
        SW_EXPECT_NEAR_EQUAL( 0.5f, AudioMusicTestInternal::leftAt( listSample, 96000 ), 1e-5f );
    }
    {
        // Intro(한 마디, 루프 없음, 다음 A) — 96000 에서 A 로.
        sw::AudioEngine engine;
        SW_ASSERT_TRUE( AudioMusicTestInternal::initializeEngine( engine ) );
        (void)engine.startMusic( AudioMusicTestInternal::makeMusic( "Intro" ), 0.0f );
        sw::vector<float32> listSample;
        AudioMusicTestInternal::appendRender( engine, listSample, 100000 );
        SW_EXPECT_NEAR_EQUAL( 0.125f, AudioMusicTestInternal::leftAt( listSample, 95999 ), 1e-5f );
        SW_EXPECT_NEAR_EQUAL( 0.25f, AudioMusicTestInternal::leftAt( listSample, 96000 ), 1e-5f );
        SW_EXPECT_TRUE( engine.getMusicStatus()._segment == sw::hashed_string( "A" ) );
    }
    {
        // A → B(다음 박 48000)를 기다리는 동안 C 로 바꾸면 C 가 같은 경계(48000)에 온다.
        sw::AudioEngine engine;
        SW_ASSERT_TRUE( AudioMusicTestInternal::initializeEngine( engine ) );
        (void)engine.startMusic( AudioMusicTestInternal::makeMusic( "A" ), 0.0f );
        sw::vector<float32> listSample;
        AudioMusicTestInternal::appendRender( engine, listSample, 30000 );
        engine.setMusicSegment( sw::hashed_string( "B" ) );
        AudioMusicTestInternal::appendRender( engine, listSample, 5000 );
        engine.setMusicSegment( sw::hashed_string( "C" ) );
        AudioMusicTestInternal::appendRender( engine, listSample, 20000 );
        SW_EXPECT_NEAR_EQUAL( 0.25f, AudioMusicTestInternal::leftAt( listSample, 47999 ), 1e-5f );
        // C(0.5) + 처음 전환의 스팅어(0.1).
        SW_EXPECT_NEAR_EQUAL( 0.6f, AudioMusicTestInternal::leftAt( listSample, 48000 ), 1e-5f );
        SW_EXPECT_TRUE( engine.getMusicStatus()._segment == sw::hashed_string( "C" ) );
    }
}

/**
 * @brief [AudioMusicTest] 세로 레이어 — 파라미터가 레이어 볼륨을 정하고, `_layerFadeSeconds`(0.5 초) 동안 옮긴다
 */
SW_TEST_CASE( AudioMusicTest, LayersFadeWithParameter )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioMusicTestInternal::initializeEngine( engine ) );
    (void)engine.startMusic( AudioMusicTestInternal::makeMusic( "B" ), 0.0f );
    sw::vector<float32> listSample;
    AudioMusicTestInternal::appendRender( engine, listSample, 4800 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, AudioMusicTestInternal::leftAt( listSample, 4000 ), 1e-4f );

    engine.setParameter( sw::hashed_string( "Intensity" ), 1.0f );
    listSample.clear();
    AudioMusicTestInternal::appendRender( engine, listSample, 12000 );
    // 0.25 초 뒤: 드럼(0.3)의 절반쯤.
    SW_EXPECT_NEAR_EQUAL( 0.5f + 0.15f, AudioMusicTestInternal::leftAt( listSample, 11900 ), 0.02f );
    listSample.clear();
    AudioMusicTestInternal::appendRender( engine, listSample, 24000 );
    SW_EXPECT_NEAR_EQUAL( 0.8f, AudioMusicTestInternal::leftAt( listSample, 23000 ), 1e-4f );

    // 세기를 내리면 다시 빠진다.
    engine.setParameter( sw::hashed_string( "Intensity" ), 0.0f );
    listSample.clear();
    AudioMusicTestInternal::appendRender( engine, listSample, 36000 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, AudioMusicTestInternal::leftAt( listSample, 35000 ), 1e-4f );

    // 멈추면 조용하다.
    engine.stopMusic( 0.0f );
    listSample.clear();
    AudioMusicTestInternal::appendRender( engine, listSample, 1024 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, AudioMusicTestInternal::leftAt( listSample, 1000 ), 1e-6f );
    SW_EXPECT_FALSE( engine.getMusicStatus()._bPlaying );
}

/**
 * @brief [AudioMusicTest] 음악 데이터 검사 — 모르는 다음 구간 · 전환 대상 · 시작 구간, 레이어 없는 구간은 읽기 오류다
 */
SW_TEST_CASE( AudioMusicTest, MusicDataIsValidated )
{
    SW_TEST_DEFENSIVE_SCOPE( "Music with unknown segment names is rejected" );
    sw::AudioMusicDesc music;
    SW_EXPECT_FALSE( music.loadFromXMLText( R"(<AudioMusicDesc><_listSegment><AudioMusicSegmentDesc _name="A" _bLoop="false" _next="Z">
        <_listLayer><AudioMusicLayerDesc _path="m/a" /></_listLayer></AudioMusicSegmentDesc></_listSegment></AudioMusicDesc>)" ) );
    SW_EXPECT_FALSE( music.loadFromXMLText( R"(<AudioMusicDesc><_listSegment><AudioMusicSegmentDesc _name="A">
        <_listLayer><AudioMusicLayerDesc _path="m/a" /></_listLayer></AudioMusicSegmentDesc></_listSegment>
        <_listTransition><AudioMusicTransitionDesc _to="Nope" /></_listTransition></AudioMusicDesc>)" ) );
    SW_EXPECT_FALSE( music.loadFromXMLText( R"(<AudioMusicDesc _startSegment="Nope"><_listSegment><AudioMusicSegmentDesc _name="A">
        <_listLayer><AudioMusicLayerDesc _path="m/a" /></_listLayer></AudioMusicSegmentDesc></_listSegment></AudioMusicDesc>)" ) );
    SW_EXPECT_FALSE( music.loadFromXMLText( R"(<AudioMusicDesc><_listSegment><AudioMusicSegmentDesc _name="A" /></_listSegment></AudioMusicDesc>)" ) );
    SW_EXPECT_TRUE( music.loadFromXMLText( AudioMusicTestInternal::kMusicXML ) );
    SW_EXPECT_EQUAL( uint64{ 96000 }, music.computeSegmentFrames( 0 ) );
}
