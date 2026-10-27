#include "pch.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/Graph/AudioGraph.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"

#include "EngineTest/AudioTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioGraphTest — 소리 그래프(E5): 데이터 검사, 오실레이터 · 엔벨로프 · 게인 렌더, 게임 파라미터 연결, 그래뉼러의 결정성
// ------------------------------------------------------------------------------

namespace
{
    struct AudioGraphTestInternal
    {
        /** @brief 440 Hz 사인 → 게인(Level 이 0..1 → -96..0 dB) 그래프입니다. */
        static constexpr const utf8* kToneGraphXML = R"(
<AudioGraphDef _output="Amp" _durationSeconds="1">
	<_listNode>
		<AudioGraphNodeDesc _name="Osc" _type="Sine"><_listParameter><AudioGraphParameterDesc _name="frequencyHz" _value="440" /><AudioGraphParameterDesc _name="amplitude" _value="0.5" /></_listParameter></AudioGraphNodeDesc>
		<AudioGraphNodeDesc _name="Amp" _type="Gain">
			<_listInput><item>Osc</item></_listInput>
			<_listParameter>
				<AudioGraphParameterDesc _name="volumeDb" _parameter="Level"><_listPoint><AudioCurvePoint _x="0" _y="-96" /><AudioCurvePoint _x="1" _y="0" /></_listPoint></AudioGraphParameterDesc>
			</_listParameter>
		</AudioGraphNodeDesc>
	</_listNode>
</AudioGraphDef>)";

        static constexpr const utf8* kLibraryXML = R"(
<AudioEventLibrary>
	<_listParameter><AudioParameterDesc _name="Level" _minValue="0" _maxValue="1" _defaultValue="1" /></_listParameter>
	<_listEvent>
		<AudioEventDesc _name="Tone" _bus="sfx"><_listClip><AudioClipEntry _path="test/tone.audiograph.xml" /></_listClip></AudioEventDesc>
		<AudioEventDesc _name="Grains" _bus="sfx"><_listClip><AudioClipEntry _path="test/grains.audiograph.xml" /></_listClip></AudioEventDesc>
		<AudioEventDesc _name="Orphan" _bus="sfx"><_listClip><AudioClipEntry _path="test/orphan.audiograph.xml" /></_listClip></AudioEventDesc>
	</_listEvent>
</AudioEventLibrary>)";

        /** @brief 데이터를 굳혀 클립으로 만듭니다(클립 캐시의 `decodeGraph` 와 같은 모양). */
        static sw::shared_ptr<const sw::AudioClipData> makeGraphClip( const utf8* pXML, const sw::vector<sw::shared_ptr<const sw::AudioClipData>>& listNodeClip )
        {
            sw::AudioGraphDef def;
            if ( def.loadFromXMLText( pXML ) == false )
                return nullptr;
            sw::shared_ptr<const sw::AudioGraph> pGraph = sw::AudioGraph::create( def, listNodeClip, "<test>" );
            if ( pGraph == nullptr )
                return nullptr;
            sw::shared_ptr<sw::AudioClipData> pClip = sw::make_shared<sw::AudioClipData>();
            pClip->_frameCount                      = pGraph->getFrameCount();
            pClip->_sampleRate                      = sw::audio::kSampleRate;
            pClip->_channelCount                    = 1;
            pClip->_pGraph                          = pGraph;
            return pClip;
        }

        /** @brief 그래뉼러 그래프입니다(소스는 노드 0 의 클립). */
        static constexpr const utf8* kGrainGraphXML = R"(
<AudioGraphDef _output="Grains" _durationSeconds="0.5">
	<_listNode>
		<AudioGraphNodeDesc _name="Grains" _type="Granular" _clip="test/source.wav">
			<_listParameter>
				<AudioGraphParameterDesc _name="positionSeconds" _value="0.2" /><AudioGraphParameterDesc _name="positionJitterSeconds" _value="0.1" />
				<AudioGraphParameterDesc _name="pitchJitterSemitones" _value="3" /><AudioGraphParameterDesc _name="grainsPerSecond" _value="60" />
			</_listParameter>
		</AudioGraphNodeDesc>
	</_listNode>
</AudioGraphDef>)";

        /** @brief 엔진을 기본 그래프 · 테스트 라이브러리로 만들고 그래프 클립을 넣습니다. */
        [[nodiscard]] static bool initializeEngine( sw::AudioEngine& engine )
        {
            if ( engine.initialize( sw::AudioMixerDesc::makeDefault(), sw::AudioChannelLayout::Stereo ) == false )
                return false;
            engine.setRandomSeed( 7 );
            const sw::shared_ptr<const sw::AudioClipData> pSource = test::AudioTestUtil::makeSineClip( 300.0f, 0.5f, 48000 );
            const sw::shared_ptr<const sw::AudioClipData> pTone   = makeGraphClip( kToneGraphXML, { nullptr, nullptr } );
            const sw::shared_ptr<const sw::AudioClipData> pGrains = makeGraphClip( kGrainGraphXML, { pSource } );
            if ( pTone == nullptr || pGrains == nullptr )
                return false;
            engine.getClipStore().addClip( sw::hashed_string( "test/tone.audiograph.xml" ), pTone );
            engine.getClipStore().addClip( sw::hashed_string( "test/grains.audiograph.xml" ), pGrains );
            sw::AudioEventLibrary library;
            return library.loadFromXMLText( kLibraryXML ) && engine.loadEventLibrary( sw::hashed_string( "graphtest" ), library );
        }
    };
} // namespace

/**
 * @brief [AudioGraphTest] 고리 · 없는 입력 · 입력을 받는 소스 · 곡선 없는 파라미터 연결 · 모르는 값 이름 · 길이 0 은 읽기 오류다
 */
SW_TEST_CASE( AudioGraphTest, ValidationRejectsBrokenGraphs )
{
    sw::AudioGraphDef def;
    SW_EXPECT_TRUE( def.loadFromXMLText( AudioGraphTestInternal::kToneGraphXML ) );
    SW_EXPECT_FALSE( def.loadFromXMLText( R"(<AudioGraphDef _output="A" _durationSeconds="1"><_listNode>
        <AudioGraphNodeDesc _name="A" _type="Gain"><_listInput><item>B</item></_listInput></AudioGraphNodeDesc>
        <AudioGraphNodeDesc _name="B" _type="Gain"><_listInput><item>A</item></_listInput></AudioGraphNodeDesc></_listNode></AudioGraphDef>)" ) );
    SW_EXPECT_FALSE( def.loadFromXMLText( R"(<AudioGraphDef _output="A" _durationSeconds="1"><_listNode>
        <AudioGraphNodeDesc _name="A" _type="LowPass"><_listInput><item>Missing</item></_listInput></AudioGraphNodeDesc></_listNode></AudioGraphDef>)" ) );
    SW_EXPECT_FALSE( def.loadFromXMLText( R"(<AudioGraphDef _output="B" _durationSeconds="1"><_listNode><AudioGraphNodeDesc _name="A" _type="Noise" />
        <AudioGraphNodeDesc _name="B" _type="Sine"><_listInput><item>A</item></_listInput></AudioGraphNodeDesc></_listNode></AudioGraphDef>)" ) );
    SW_EXPECT_FALSE( def.loadFromXMLText( R"(<AudioGraphDef _output="A" _durationSeconds="1"><_listNode><AudioGraphNodeDesc _name="A" _type="Sine">
        <_listParameter><AudioGraphParameterDesc _name="frequencyHz" _parameter="Rpm" /></_listParameter></AudioGraphNodeDesc></_listNode></AudioGraphDef>)" ) );
    SW_EXPECT_FALSE( def.loadFromXMLText( R"(<AudioGraphDef _output="A" _durationSeconds="1"><_listNode><AudioGraphNodeDesc _name="A" _type="Sine">
        <_listParameter><AudioGraphParameterDesc _name="cutoff" _value="1" /></_listParameter></AudioGraphNodeDesc></_listNode></AudioGraphDef>)" ) );
    SW_EXPECT_FALSE( def.loadFromXMLText( R"(<AudioGraphDef _output="A"><_listNode><AudioGraphNodeDesc _name="A" _type="Sine" /></_listNode></AudioGraphDef>)" ) );
    SW_EXPECT_FALSE( def.loadFromXMLText( R"(<AudioGraphDef _output="A" _durationSeconds="1"><_listNode><AudioGraphNodeDesc _name="A" _type="Granular" /></_listNode></AudioGraphDef>)" ) );
}

/**
 * @brief [AudioGraphTest] 이벤트가 고른 그래프가 440 Hz 를 내고, 게임 파라미터(Level)가 게인 노드를 블록마다 움직인다
 * @details 진폭 0.5 의 모노 사인은 2D 가운데 팬(등전력, 채널마다 ×0.7071)으로 왼쪽 진폭 약 0.35 다. Level 0 이면 -96 dB 라 소리가 없다.
 */
SW_TEST_CASE( AudioGraphTest, ToneFollowsGameParameter )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioGraphTestInternal::initializeEngine( engine ) );
    const sw::AudioPlayingID playingID = engine.postEvent( sw::hashed_string( "Tone" ) );
    SW_ASSERT_TRUE( playingID != 0 );
    (void)test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount * 4 );
    const sw::vector<float32> listLoud = test::AudioTestUtil::render( engine, 4800 );
    SW_EXPECT_NEAR_EQUAL( 0.5f * 0.70710678f, test::AudioTestUtil::computeToneAmplitude( listLoud, 0, 0, 4800, 440.0f ), 0.02f );

    engine.setParameter( sw::hashed_string( "Level" ), 0.0f );
    (void)test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount * 4 );
    const sw::vector<float32> listQuiet = test::AudioTestUtil::render( engine, 4800 );
    SW_EXPECT_TRUE( test::AudioTestUtil::computePeak( listQuiet, 0, 0, 4800 ) < 1e-3f );
    SW_EXPECT_TRUE( engine.isPlaying( playingID ) );
}

/**
 * @brief [AudioGraphTest] 그래뉼러는 같은 시드면 같은 샘플을 내고(결정성), 소리가 난다. 그래프 길이(0.5 초)가 지나면 재생이 끝난다
 */
SW_TEST_CASE( AudioGraphTest, GranularIsDeterministic )
{
    sw::vector<float32> arrRendered[2];
    for ( sw::vector<float32>& listRendered : arrRendered )
    {
        sw::AudioEngine engine;
        SW_ASSERT_TRUE( AudioGraphTestInternal::initializeEngine( engine ) );
        const sw::AudioPlayingID playingID = engine.postEvent( sw::hashed_string( "Grains" ) );
        listRendered                       = test::AudioTestUtil::render( engine, 12000 );
        (void)test::AudioTestUtil::render( engine, 24000 );
        SW_EXPECT_FALSE( engine.isPlaying( playingID ) );
    }
    SW_EXPECT_TRUE( test::AudioTestUtil::computeRms( arrRendered[0], 0, 2400, 9600 ) > 1e-3f );
    SW_EXPECT_TRUE( arrRendered[0] == arrRendered[1] );
}

/**
 * @brief [AudioGraphTest] 로드한 라이브러리에 없는 게임 파라미터를 읽는 그래프는 0 으로 읽지 않고 그 재생을 멈춘다
 */
SW_TEST_CASE( AudioGraphTest, UnknownGameParameterStopsTheVoice )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioGraphTestInternal::initializeEngine( engine ) );
    const sw::shared_ptr<const sw::AudioClipData> pOrphan = AudioGraphTestInternal::makeGraphClip( R"(<AudioGraphDef _output="A" _durationSeconds="1"><_listNode>
        <AudioGraphNodeDesc _name="A" _type="Sine"><_listParameter><AudioGraphParameterDesc _name="frequencyHz" _parameter="NotDeclared">
        <_listPoint><AudioCurvePoint _x="0" _y="100" /><AudioCurvePoint _x="1" _y="200" /></_listPoint></AudioGraphParameterDesc></_listParameter></AudioGraphNodeDesc>
        </_listNode></AudioGraphDef>)",
                                                                                                   { nullptr } );
    SW_ASSERT_TRUE( pOrphan != nullptr );
    engine.getClipStore().addClip( sw::hashed_string( "test/orphan.audiograph.xml" ), pOrphan );
    const sw::AudioPlayingID  playingID  = engine.postEvent( sw::hashed_string( "Orphan" ) );
    const sw::vector<float32> listOutput = test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount * 4 );
    SW_EXPECT_FALSE( engine.isPlaying( playingID ) );
    SW_EXPECT_TRUE( test::AudioTestUtil::computePeak( listOutput, 0, 0, sw::audio::kBlockFrameCount * 4 ) < 1e-6f );
}

/**
 * @brief [AudioGraphTest] 그래프 창이 저장하는 XML(`toXMLText`)을 다시 읽으면 같은 그래프다(노드 · 연결 · 파라미터 곡선)
 */
SW_TEST_CASE( AudioGraphTest, XMLRoundTrip )
{
    sw::AudioGraphDef def;
    SW_ASSERT_TRUE( def.loadFromXMLText( AudioGraphTestInternal::kToneGraphXML ) );
    sw::AudioGraphDef again;
    SW_ASSERT_TRUE( again.loadFromXMLText( def.toXMLText() ) );
    SW_ASSERT_EQUAL( def._listNode.size(), again._listNode.size() );
    SW_EXPECT_TRUE( again._output == def._output );
    SW_EXPECT_NEAR_EQUAL( def._durationSeconds, again._durationSeconds, 1e-6f );
    SW_ASSERT_EQUAL( size_t( 1 ), again._listNode[1]._listInput.size() );
    SW_EXPECT_TRUE( again._listNode[1]._listInput[0] == def._listNode[0]._name );
    SW_ASSERT_EQUAL( size_t( 1 ), again._listNode[1]._listParameter.size() );
    SW_EXPECT_TRUE( again._listNode[1]._listParameter[0]._parameter == sw::hashed_string( "Level" ) );
    SW_EXPECT_EQUAL( size_t( 2 ), again._listNode[1]._listParameter[0]._listPoint.size() );
}
