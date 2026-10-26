#include "pch.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixer.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/NullAudioBackend.h"

#include "EngineTest/AudioTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioMixerTest — 버스 그래프(계층 · 페이더 · 사용자 볼륨 · 음소거/솔로 · 센드)와 장치 없는 결정적 렌더
// ------------------------------------------------------------------------------

namespace
{
    struct AudioMixerTestInternal
    {
        /** @brief master 아래 music · sfx, sfx 는 fx 로 센드(-6 dB), fx 도 master 아래입니다. */
        static constexpr const utf8* kSendMixerXML = R"(
<AudioMixerDesc>
	<_listBus>
		<AudioBusDesc _name="master" />
		<AudioBusDesc _name="music" _parent="master" />
		<AudioBusDesc _name="sfx" _parent="master" _volumeDb="-6.0206">
			<_listSend><AudioSendDesc _bus="fx" _levelDb="-6.0206" /></_listSend>
		</AudioBusDesc>
		<AudioBusDesc _name="fx" _parent="master" />
	</_listBus>
</AudioMixerDesc>)";

        /** @brief 직류 클립 둘(0.5 · 0.25)을 넣은 엔진을 기본 그래프로 만듭니다. */
        [[nodiscard]] static bool initializeEngine( sw::AudioEngine& engine, const sw::AudioMixerDesc& desc )
        {
            if ( engine.initialize( desc, sw::AudioChannelLayout::Stereo ) == false )
                return false;
            engine.getClipStore().addClip( sw::hashed_string( "test/dc_half" ), test::AudioTestUtil::makeConstantClip( 0.5f, 48000 ) );
            engine.getClipStore().addClip( sw::hashed_string( "test/dc_quarter" ), test::AudioTestUtil::makeConstantClip( 0.25f, 48000 ) );
            return true;
        }

        /** @brief 두 블록을 버리고(보이스 게인 램프) 그 뒤 한 블록의 왼쪽 평균입니다. */
        static float32 renderSteadyLeft( sw::AudioEngine& engine )
        {
            (void)test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount * 2 );
            const sw::vector<float32> listSample = test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount );
            return test::AudioTestUtil::computeMean( listSample, 0, 0, sw::audio::kBlockFrameCount );
        }
    };
} // namespace

/**
 * @brief [AudioMixerTest] 버스 페이더(데이터 dB) · 사용자 볼륨이 곱으로 출력에 닿는다
 * @details 0.5 직류를 sfx(-6.02 dB = ×0.5)로 보내면 0.25, 사용자 볼륨 0.5 를 더하면 0.125 다. 스테레오 클립은 가운데 팬에서 두 채널 그대로다.
 */
SW_TEST_CASE( AudioMixerTest, FaderAndUserVolumeMultiply )
{
    sw::AudioMixerDesc desc;
    SW_ASSERT_TRUE( desc.loadFromXMLText( AudioMixerTestInternal::kSendMixerXML ) );
    // 센드는 이 케이스에서 끈다(레벨 바닥).
    desc._listBus[2]._listSend[0]._levelDb = sw::audio::kSilenceDb;
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioMixerTestInternal::initializeEngine( engine, desc ) );

    (void)engine.playClip( sw::hashed_string( "test/dc_half" ), sw::hashed_string( "sfx" ), sw::AudioClipPlayParams{} );
    SW_EXPECT_NEAR_EQUAL( 0.25f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-3f );

    engine.setBusUserVolume( sw::hashed_string( "sfx" ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.125f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-3f );

    // master 의 사용자 볼륨도 곱해진다.
    engine.setBusUserVolume( sw::hashed_string( "master" ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.0625f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-3f );
}

/**
 * @brief [AudioMixerTest] 음소거는 그 버스(와 아래)를, 솔로는 솔로 경로 밖의 버스를 조용히 한다
 */
SW_TEST_CASE( AudioMixerTest, MuteAndSoloSilenceTheRightBuses )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioMixerTestInternal::initializeEngine( engine, sw::AudioMixerDesc::makeDefault() ) );
    (void)engine.playClip( sw::hashed_string( "test/dc_half" ), sw::hashed_string( "music" ), sw::AudioClipPlayParams{} );
    (void)engine.playClip( sw::hashed_string( "test/dc_quarter" ), sw::hashed_string( "sfx" ), sw::AudioClipPlayParams{} );
    SW_EXPECT_NEAR_EQUAL( 0.75f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-3f );

    engine.setBusMuted( sw::hashed_string( "music" ), true );
    SW_EXPECT_NEAR_EQUAL( 0.25f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-3f );
    engine.setBusMuted( sw::hashed_string( "music" ), false );

    engine.setBusSolo( sw::hashed_string( "music" ), true );
    SW_EXPECT_NEAR_EQUAL( 0.5f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-3f );
    engine.setBusSolo( sw::hashed_string( "music" ), false );
    SW_EXPECT_NEAR_EQUAL( 0.75f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-3f );

    // master 음소거는 전부를 끈다.
    engine.setBusMuted( sw::hashed_string( "master" ), true );
    SW_EXPECT_NEAR_EQUAL( 0.0f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-4f );
}

/**
 * @brief [AudioMixerTest] 센드는 페이더 뒤 신호에 레벨을 곱해 다른 버스로 보낸다 — 프리 페이더면 페이더와 무관하다
 */
SW_TEST_CASE( AudioMixerTest, SendsCarryTheSignalToTheTargetBus )
{
    sw::AudioMixerDesc desc;
    SW_ASSERT_TRUE( desc.loadFromXMLText( AudioMixerTestInternal::kSendMixerXML ) );
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioMixerTestInternal::initializeEngine( engine, desc ) );
    (void)engine.playClip( sw::hashed_string( "test/dc_half" ), sw::hashed_string( "sfx" ), sw::AudioClipPlayParams{} );

    // sfx 출력 0.25(페이더 ×0.5), fx 는 그 ×0.5 = 0.125, master = 0.375.
    SW_EXPECT_NEAR_EQUAL( 0.375f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.125f, engine.getBusPeak( sw::hashed_string( "fx" ) ), 1e-3f );

    // 포스트 페이더: sfx 를 음소거하면 센드도 끊긴다.
    engine.setBusMuted( sw::hashed_string( "sfx" ), true );
    SW_EXPECT_NEAR_EQUAL( 0.0f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-4f );

    // 프리 페이더 센드는 음소거된 sfx 의 페이더 전 신호(0.5)를 ×0.5 로 보낸다.
    desc._listBus[2]._listSend[0]._bPreFader = true;
    SW_ASSERT_TRUE( engine.loadMixer( desc ) );
    SW_EXPECT_NEAR_EQUAL( 0.25f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-3f );
}

/**
 * @brief [AudioMixerTest] 부모 · 센드가 고리를 만들거나 루트가 master 가 아니면 읽기 오류다
 */
SW_TEST_CASE( AudioMixerTest, CyclesAndBadRootsAreLoadErrors )
{
    SW_TEST_DEFENSIVE_SCOPE( "Mixer graphs with cycles / unknown names are rejected" );
    sw::AudioMixerDesc desc;
    SW_EXPECT_FALSE( desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus>
        <AudioBusDesc _name="master" />
        <AudioBusDesc _name="a" _parent="master"><_listSend><AudioSendDesc _bus="b" /></_listSend></AudioBusDesc>
        <AudioBusDesc _name="b" _parent="a" /></_listBus></AudioMixerDesc>)" ) );
    SW_EXPECT_FALSE( desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus><AudioBusDesc _name="main" /></_listBus></AudioMixerDesc>)" ) );
    SW_EXPECT_FALSE( desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus>
        <AudioBusDesc _name="master" /><AudioBusDesc _name="sfx" _parent="nowhere" /></_listBus></AudioMixerDesc>)" ) );

    // 처리 순서는 보내는 쪽이 먼저다.
    SW_ASSERT_TRUE( desc.loadFromXMLText( AudioMixerTestInternal::kSendMixerXML ) );
    sw::vector<uint32> listOrder;
    SW_ASSERT_TRUE( desc.makeProcessingOrder( listOrder ) );
    sw::vector<uint32> listPosition( listOrder.size() );
    for ( uint32 position = 0; position < listOrder.size(); ++position )
    {
        listPosition[listOrder[position]] = position;
    }
    SW_EXPECT_TRUE( listPosition[2] < listPosition[3] ); // sfx → fx
    SW_EXPECT_TRUE( listPosition[3] < listPosition[0] ); // fx → master
}

/**
 * @brief [AudioMixerTest] 같은 명령이면 렌더가 샘플 단위로 같다(장치 없는 결정적 렌더)
 */
SW_TEST_CASE( AudioMixerTest, OfflineRenderIsDeterministic )
{
    sw::vector<float32> arrRun[2];
    for ( sw::vector<float32>& listSample : arrRun )
    {
        sw::AudioEngine engine;
        SW_ASSERT_TRUE( AudioMixerTestInternal::initializeEngine( engine, sw::AudioMixerDesc::makeDefault() ) );
        engine.getClipStore().addClip( sw::hashed_string( "test/sine" ), test::AudioTestUtil::makeSineClip( 440.0f, 0.5f, 24000, 44100 ) );
        engine.setRandomSeed( 7 );
        sw::AudioClipPlayParams params;
        params._pitchSemitones = 3.0f;
        params._pan            = -0.4f;
        (void)engine.playClip( sw::hashed_string( "test/sine" ), sw::hashed_string( "sfx" ), params );
        listSample = test::AudioTestUtil::render( engine, 12000 );
    }
    SW_ASSERT_EQUAL( arrRun[0].size(), arrRun[1].size() );
    SW_EXPECT_TRUE( arrRun[0] == arrRun[1] );
    // 소리가 실제로 났다 — 비어 있는 두 버퍼도 같다.
    SW_EXPECT_TRUE( test::AudioTestUtil::computeRms( arrRun[0], 0, 0, 12000 ) > 0.1f );
}

/**
 * @brief [AudioMixerTest] 피치 +12 반음이면 같은 클립이 절반 시간에 끝나고, 끝난 재생은 살아 있지 않다
 */
SW_TEST_CASE( AudioMixerTest, PitchChangesPlaybackLength )
{
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( AudioMixerTestInternal::initializeEngine( engine, sw::AudioMixerDesc::makeDefault() ) );
    engine.getClipStore().addClip( sw::hashed_string( "test/short" ), test::AudioTestUtil::makeConstantClip( 0.5f, 4800 ) );
    sw::AudioClipPlayParams params;
    params._pitchSemitones             = 12.0f;
    const sw::AudioPlayingID playingID = engine.playClip( sw::hashed_string( "test/short" ), sw::hashed_string( "sfx" ), params );
    SW_EXPECT_TRUE( engine.isPlaying( playingID ) );

    const sw::vector<float32> listSample = test::AudioTestUtil::render( engine, 4800 );
    // 2400 프레임(±1 블록 안 보간 끝)에서 소리가 끝난다.
    SW_EXPECT_TRUE( test::AudioTestUtil::computePeak( listSample, 0, 1000, 1300 ) > 0.3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, test::AudioTestUtil::computePeak( listSample, 0, 2410, 2000 ), 1e-6f );
    SW_EXPECT_FALSE( engine.isPlaying( playingID ) );
    SW_EXPECT_EQUAL( 0u, engine.getStats()._voiceCount );
}

/**
 * @brief [AudioMixerTest] 사용자 설정의 버스 볼륨(`voice` · `ambient` · `ui` 포함)이 같은 이름의 믹서 버스에 닿고, `play` 가 버스를 고른다
 * @details 설정 메뉴는 `IAudioService::setBusVolume( "ambient", v )` 를 부른다. 예전에는 세 기본 버스 밖의 이름은 값만 기억되고 소리에 닿지 않았다.
 */
SW_TEST_CASE( AudioMixerTest, UserSettingsBusVolumeReachesTheMixer )
{
    sw::NullAudioBackend system;
    SW_ASSERT_TRUE( system.initialize() );
    SW_EXPECT_FALSE( system.isOutputOpen() );
    sw::AudioEngine& engine = system.getEngine();
    engine.getClipStore().addClip( sw::hashed_string( "test/dc_half" ), test::AudioTestUtil::makeConstantClip( 0.5f, 48000 ) );
    // 기본 그래프의 리버브 리턴은 이 측정에서 끈다(직류의 리버브 꼬리가 평균에 섞인다).
    engine.setBusMuted( sw::hashed_string( "reverb" ), true );

    system.setBusVolume( sw::hashed_string( "ambient" ), 0.25f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, system.getBusVolume( sw::hashed_string( "ambient" ) ), 1e-6f );
    (void)engine.playClip( sw::hashed_string( "test/dc_half" ), sw::hashed_string( "ambient" ), sw::AudioClipPlayParams{} );
    SW_EXPECT_NEAR_EQUAL( 0.125f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-3f );

    // 음소거는 master 한 자리 — 풀면 버스 볼륨은 그대로다.
    system.setMute( true );
    SW_EXPECT_NEAR_EQUAL( 0.0f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-4f );
    system.setMute( false );
    SW_EXPECT_NEAR_EQUAL( 0.125f, AudioMixerTestInternal::renderSteadyLeft( engine ), 1e-3f );
    system.shutdown();
}

/**
 * @brief [AudioMixerTest] 레이아웃의 채널 수 — 스테레오 2, 5.1 은 6, 7.1 은 8이고 모두 최대 채널 수 안이다
 */
SW_TEST_CASE( AudioMixerTest, ChannelLayoutCountsChannels )
{
    SW_EXPECT_EQUAL( 2u, sw::AudioChannelLayoutUtil::getChannelCount( sw::AudioChannelLayout::Stereo ) );
    SW_EXPECT_EQUAL( 6u, sw::AudioChannelLayoutUtil::getChannelCount( sw::AudioChannelLayout::Surround51 ) );
    SW_EXPECT_EQUAL( 8u, sw::AudioChannelLayoutUtil::getChannelCount( sw::AudioChannelLayout::Surround71 ) );
    SW_EXPECT_TRUE( sw::AudioChannelLayoutUtil::getChannelCount( sw::AudioChannelLayout::Surround71 ) <= sw::audio::kMaxChannelCount );
}

/**
 * @brief [AudioMixerTest] 5.1 · 7.1 믹서 — 버스 페이더 · 부모 합산이 채널마다 독립으로 같은 식을 따른다
 * @details sfx(-6.0206 dB, ×0.5)의 채널 c 에 직류 0.1 × (c + 1)을 넣으면 master 출력의 채널 c 는 그 ×0.5 다. 채널을 섞거나 빠뜨리면 값이 어긋난다.
 */
SW_TEST_CASE( AudioMixerTest, SurroundLayoutMixesEveryChannelIndependently )
{
    sw::AudioMixerDesc desc;
    SW_ASSERT_TRUE( desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus>
        <AudioBusDesc _name="master" /><AudioBusDesc _name="sfx" _parent="master" _volumeDb="-6.0206" /></_listBus></AudioMixerDesc>)" ) );
    const sw::AudioChannelLayout arrLayout[] = { sw::AudioChannelLayout::Surround51, sw::AudioChannelLayout::Surround71 };
    for ( const sw::AudioChannelLayout layout : arrLayout )
    {
        const uint32   channelCount = sw::AudioChannelLayoutUtil::getChannelCount( layout );
        sw::AudioMixer mixer;
        SW_ASSERT_TRUE( mixer.initialize( desc, layout ) );
        const uint32 sfxIndex = static_cast<uint32>( mixer.findBusIndex( sw::hashed_string( "sfx" ) ) );

        mixer.beginBlock( sw::audio::kBlockFrameCount );
        const sw::AudioBlockView input = mixer.getBusInput( sfxIndex );
        SW_ASSERT_EQUAL( channelCount, input._channelCount );
        for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
        {
            for ( uint32 frameIndex = 0; frameIndex < input._frameCount; ++frameIndex )
            {
                input.getChannel( channelIndex )[frameIndex] = 0.1f * static_cast<float32>( channelIndex + 1 );
            }
        }
        sw::AudioBlockBuffer outputBuffer;
        outputBuffer.allocate( layout );
        const sw::AudioBlockView output = outputBuffer.makeView( sw::audio::kBlockFrameCount );
        mixer.process( output );

        for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
        {
            const float32 expected = 0.05f * static_cast<float32>( channelIndex + 1 );
            SW_EXPECT_NEAR_EQUAL( expected, output.getChannel( channelIndex )[0], 1e-5f );
            SW_EXPECT_NEAR_EQUAL( expected, output.getChannel( channelIndex )[sw::audio::kBlockFrameCount - 1], 1e-5f );
        }
        SW_EXPECT_NEAR_EQUAL( 0.05f * static_cast<float32>( channelCount ), mixer.getBusPeak( sfxIndex ), 1e-5f ); // 피크는 모든 채널의 최댓값
    }
}

/**
 * @brief [AudioMixerTest] 이펙트가 있는 그래프와 엔진이 5.1 · 7.1 로 선다 — 이펙트 종류 표가 모든 레이아웃을 선언하고, 선언 밖의 레이아웃은 종류가 거절한다
 */
SW_TEST_CASE( AudioMixerTest, SurroundLayoutAcceptsEffectsAndTheEngine )
{
    sw::AudioMixerDesc desc;
    SW_ASSERT_TRUE( desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus><AudioBusDesc _name="master"><_listEffect><AudioEffectDesc _type="LowPass"><_listParameter>
        <AudioEffectParameterDesc _name="frequencyHz" _value="200" /></_listParameter></AudioEffectDesc></_listEffect></AudioBusDesc></_listBus></AudioMixerDesc>)" ) );
    const sw::AudioChannelLayout arrLayout[] = { sw::AudioChannelLayout::Stereo, sw::AudioChannelLayout::Surround51, sw::AudioChannelLayout::Surround71 };
    for ( const sw::AudioChannelLayout layout : arrLayout )
    {
        sw::AudioMixer mixer;
        SW_EXPECT_TRUE( mixer.initialize( desc, layout ) );
        sw::AudioEngine engine;
        SW_EXPECT_TRUE( engine.initialize( sw::AudioMixerDesc::makeDefault(), layout ) );
        SW_EXPECT_EQUAL( sw::AudioChannelLayoutUtil::getChannelCount( layout ), sw::AudioChannelLayoutUtil::getChannelCount( engine.getChannelLayout() ) );
    }

    // 선언한 레이아웃 밖은 거절한다(표의 칸이 기준이다).
    sw::AudioEffectTypeInfo stereoOnly;
    stereoOnly._supportedLayoutMask = sw::AudioEffectTypeInfo::makeLayoutBit( sw::AudioChannelLayout::Stereo );
    SW_EXPECT_TRUE( stereoOnly.supportsLayout( sw::AudioChannelLayout::Stereo ) );
    SW_EXPECT_FALSE( stereoOnly.supportsLayout( sw::AudioChannelLayout::Surround51 ) );
}

/**
 * @brief [AudioMixerTest] 버스별 레이아웃 — 두 채널 버스는 5.1 출력에서도 두 채널이고, 부모의 FL FR 에만 더하며, 넓은 버스의 센드는 BS.775 로 접혀 들어온다
 * @details ui(Stereo)에 직류 0.2 · 0.4 를 넣으면 master 의 FL · FR 이 그 값이고 나머지 채널은 0 이다. sfx(5.1)의 FC 에만 0.5 를 넣고 두 채널 리턴 버스(fx, 센드 0 dB)로 보내면
 *          fx 의 왼쪽 · 오른쪽은 0.5 × 0.7071 이고, master 의 FC 에는 sfx 의 직접 경로 0.5 가 남는다. master 를 두 채널로 적은 그래프는 검사에서 거절한다.
 */
SW_TEST_CASE( AudioMixerTest, StereoBusInsideSurroundOutput )
{
    sw::AudioMixerDesc desc;
    SW_ASSERT_TRUE( desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus>
        <AudioBusDesc _name="master" />
        <AudioBusDesc _name="ui" _parent="master" _channels="Stereo" />
        <AudioBusDesc _name="sfx" _parent="master"><_listSend><AudioSendDesc _bus="fx" _levelDb="0" /></_listSend></AudioBusDesc>
        <AudioBusDesc _name="fx" _parent="master" _channels="Stereo" /></_listBus></AudioMixerDesc>)" ) );
    sw::AudioMixer mixer;
    SW_ASSERT_TRUE( mixer.initialize( desc, sw::AudioChannelLayout::Surround51 ) );
    const uint32 uiIndex  = static_cast<uint32>( mixer.findBusIndex( sw::hashed_string( "ui" ) ) );
    const uint32 sfxIndex = static_cast<uint32>( mixer.findBusIndex( sw::hashed_string( "sfx" ) ) );
    const uint32 fxIndex  = static_cast<uint32>( mixer.findBusIndex( sw::hashed_string( "fx" ) ) );
    SW_EXPECT_TRUE( mixer.getBusLayout( uiIndex ) == sw::AudioChannelLayout::Stereo );
    SW_EXPECT_TRUE( mixer.getBusLayout( sfxIndex ) == sw::AudioChannelLayout::Surround51 );

    mixer.beginBlock( sw::audio::kBlockFrameCount );
    const sw::AudioBlockView ui = mixer.getBusInput( uiIndex );
    SW_ASSERT_EQUAL( 2u, ui._channelCount );
    const sw::AudioBlockView sfx = mixer.getBusInput( sfxIndex );
    SW_ASSERT_EQUAL( 6u, sfx._channelCount );
    for ( uint32 frameIndex = 0; frameIndex < sw::audio::kBlockFrameCount; ++frameIndex )
    {
        ui.getChannel( 0 )[frameIndex]  = 0.2f;
        ui.getChannel( 1 )[frameIndex]  = 0.4f;
        sfx.getChannel( 2 )[frameIndex] = 0.5f; // FC
    }
    sw::AudioBlockBuffer outputBuffer;
    outputBuffer.allocate( sw::AudioChannelLayout::Surround51 );
    const sw::AudioBlockView output = outputBuffer.makeView( sw::audio::kBlockFrameCount );
    mixer.process( output );

    constexpr float32 kCenterFold = 0.5f * 0.70710678f;
    const uint32      lastFrame   = sw::audio::kBlockFrameCount - 1;
    SW_EXPECT_NEAR_EQUAL( 0.2f + kCenterFold, output.getChannel( 0 )[lastFrame], 1e-4f ); // FL = ui 왼쪽 + fx 왼쪽
    SW_EXPECT_NEAR_EQUAL( 0.4f + kCenterFold, output.getChannel( 1 )[lastFrame], 1e-4f ); // FR = ui 오른쪽 + fx 오른쪽
    SW_EXPECT_NEAR_EQUAL( 0.5f, output.getChannel( 2 )[lastFrame], 1e-5f );               // FC = sfx 직접
    for ( uint32 channelIndex = 3; channelIndex < 6; ++channelIndex )
    {
        SW_EXPECT_NEAR_EQUAL( 0.0f, output.getChannel( channelIndex )[lastFrame], 1e-6f );
    }
    SW_EXPECT_NEAR_EQUAL( kCenterFold, mixer.getBusPeak( fxIndex ), 1e-4f );

    sw::AudioMixerDesc stereoMaster;
    SW_EXPECT_FALSE( stereoMaster.loadFromXMLText( R"(<AudioMixerDesc><_listBus><AudioBusDesc _name="master" _channels="Stereo" /></_listBus></AudioMixerDesc>)" ) );
}
