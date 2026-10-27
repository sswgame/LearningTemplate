#include "pch.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixer.h"
#include "Engine/Audio/AudioMixerDesc.h"

#include "EngineTest/AudioTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioDuckingTest — 사이드체인 덕킹(대사가 나올 때 음악 낮추기)
// ------------------------------------------------------------------------------

namespace
{
    struct AudioDuckingTestInternal
    {
        /** @brief music 은 voice 가 -40 dB 를 넘으면 6.02 dB(×0.5) 눌리고, 공격과 릴리스는 한 블록입니다. */
        static constexpr const utf8* kInstantDuckXML = R"(
<AudioMixerDesc>
	<_listBus>
		<AudioBusDesc _name="master" />
		<AudioBusDesc _name="music" _parent="master">
			<_listDuck><AudioDuckDesc _sourceBus="voice" _thresholdDb="-40" _depthDb="-6.0206" _attackSeconds="0" _releaseSeconds="0" /></_listDuck>
		</AudioBusDesc>
		<AudioBusDesc _name="voice" _parent="master" />
	</_listBus>
</AudioMixerDesc>)";

        /** @brief 같은 그래프에 공격 0.1 초 · 릴리스 0.3 초입니다. */
        static constexpr const utf8* kSmoothDuckXML = R"(
<AudioMixerDesc>
	<_listBus>
		<AudioBusDesc _name="master" />
		<AudioBusDesc _name="music" _parent="master">
			<_listDuck><AudioDuckDesc _sourceBus="voice" _thresholdDb="-40" _depthDb="-6.0206" _attackSeconds="0.1" _releaseSeconds="0.3" /></_listDuck>
		</AudioBusDesc>
		<AudioBusDesc _name="voice" _parent="master" />
	</_listBus>
</AudioMixerDesc>)";

        /** @brief 블록 @p blockCount 개를 돌립니다. voice 버스 입력은 매 블록 @p voiceLevel 직류입니다. */
        static void runBlocks( sw::AudioMixer& mixer, uint32 voiceIndex, float32 voiceLevel, uint32 blockCount )
        {
            sw::AudioBlockBuffer outputBuffer;
            outputBuffer.allocate( sw::AudioChannelLayout::Stereo );
            for ( uint32 blockIndex = 0; blockIndex < blockCount; ++blockIndex )
            {
                mixer.beginBlock( sw::audio::kBlockFrameCount );
                const sw::AudioBlockView voice = mixer.getBusInput( voiceIndex );
                for ( uint32 channelIndex = 0; channelIndex < voice._channelCount; ++channelIndex )
                {
                    for ( uint32 frameIndex = 0; frameIndex < voice._frameCount; ++frameIndex )
                    {
                        voice.getChannel( channelIndex )[frameIndex] = voiceLevel;
                    }
                }
                mixer.process( outputBuffer.makeView( sw::audio::kBlockFrameCount ) );
            }
        }

        static float32 renderSteadyLeft( sw::AudioEngine& engine )
        {
            (void)test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount * 2 );
            const sw::vector<float32> listSample = test::AudioTestUtil::render( engine, sw::audio::kBlockFrameCount );
            return test::AudioTestUtil::computeMean( listSample, 0, 0, sw::audio::kBlockFrameCount );
        }
    };
} // namespace

/**
 * @brief [AudioDuckingTest] voice 가 나오는 동안 music 이 ×0.5 로 눌리고, voice 가 끝나면 원래 크기로 돌아온다
 * @details music 0.5 직류 단독이면 0.5, voice 0.25 가 더해지면 music 은 0.25 로 눌려 합이 0.5 다. voice 가 끝난(1 초 클립) 뒤에는 music 이 다시 0.5 다.
 */
SW_TEST_CASE( AudioDuckingTest, SourceBusPressesTheDuckedBusAndReleases )
{
    sw::AudioMixerDesc desc;
    SW_ASSERT_TRUE( desc.loadFromXMLText( AudioDuckingTestInternal::kInstantDuckXML ) );
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( engine.initialize( desc, sw::AudioChannelLayout::Stereo ) );
    engine.getClipStore().addClip( sw::hashed_string( "test/dc_half" ), test::AudioTestUtil::makeConstantClip( 0.5f, 480000 ) );
    engine.getClipStore().addClip( sw::hashed_string( "test/dc_quarter" ), test::AudioTestUtil::makeConstantClip( 0.25f, 48000 ) );

    (void)engine.playClip( sw::hashed_string( "test/dc_half" ), sw::hashed_string( "music" ), sw::AudioClipPlayParams{} );
    SW_EXPECT_NEAR_EQUAL( 0.5f, AudioDuckingTestInternal::renderSteadyLeft( engine ), 1e-3f );

    (void)engine.playClip( sw::hashed_string( "test/dc_quarter" ), sw::hashed_string( "voice" ), sw::AudioClipPlayParams{} );
    SW_EXPECT_NEAR_EQUAL( 0.5f, AudioDuckingTestInternal::renderSteadyLeft( engine ), 2e-3f ); // 0.5 x 0.5 + 0.25

    (void)test::AudioTestUtil::render( engine, 60000 ); // voice 클립이 끝난다
    SW_EXPECT_NEAR_EQUAL( 0.5f, AudioDuckingTestInternal::renderSteadyLeft( engine ), 2e-3f );
}

/**
 * @brief [AudioDuckingTest] 공격 · 릴리스는 dB 축에서 부드럽게 따라간다 — 한 블록에 끝까지 가지 않고, 시간이 지나면 깊이에 닿고, 조용해지면 0 으로 돌아온다
 */
SW_TEST_CASE( AudioDuckingTest, AttackAndReleaseAreSmooth )
{
    sw::AudioMixerDesc desc;
    SW_ASSERT_TRUE( desc.loadFromXMLText( AudioDuckingTestInternal::kSmoothDuckXML ) );
    sw::AudioMixer mixer;
    SW_ASSERT_TRUE( mixer.initialize( desc, sw::AudioChannelLayout::Stereo ) );
    const uint32 musicIndex = static_cast<uint32>( mixer.findBusIndex( sw::hashed_string( "music" ) ) );
    const uint32 voiceIndex = static_cast<uint32>( mixer.findBusIndex( sw::hashed_string( "voice" ) ) );

    AudioDuckingTestInternal::runBlocks( mixer, voiceIndex, 0.5f, 5 );
    SW_EXPECT_TRUE( -6.0206f < mixer.getBusDuckDb( musicIndex ) && mixer.getBusDuckDb( musicIndex ) < -0.2f ); // 오르는 중

    AudioDuckingTestInternal::runBlocks( mixer, voiceIndex, 0.5f, 200 ); // 약 1 초
    SW_EXPECT_NEAR_EQUAL( -6.0206f, mixer.getBusDuckDb( musicIndex ), 0.05f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, mixer.computeBusGain( musicIndex ), 0.01f ); // dB 눌림이 페이더 게인에 곱해진다

    AudioDuckingTestInternal::runBlocks( mixer, voiceIndex, 0.0f, 5 );
    SW_EXPECT_TRUE( -6.0206f < mixer.getBusDuckDb( musicIndex ) && mixer.getBusDuckDb( musicIndex ) < -0.5f ); // 되돌아오는 중

    AudioDuckingTestInternal::runBlocks( mixer, voiceIndex, 0.0f, 400 ); // 약 2 초
    SW_EXPECT_NEAR_EQUAL( 0.0f, mixer.getBusDuckDb( musicIndex ), 0.05f );
}

/**
 * @brief [AudioDuckingTest] 임계값 아래의 소스는 누르지 않고, 소스 버스 자신은 눌리지 않는다
 */
SW_TEST_CASE( AudioDuckingTest, BelowThresholdDoesNotDuck )
{
    sw::AudioMixerDesc desc;
    SW_ASSERT_TRUE( desc.loadFromXMLText( AudioDuckingTestInternal::kInstantDuckXML ) );
    sw::AudioMixer mixer;
    SW_ASSERT_TRUE( mixer.initialize( desc, sw::AudioChannelLayout::Stereo ) );
    const uint32 musicIndex = static_cast<uint32>( mixer.findBusIndex( sw::hashed_string( "music" ) ) );
    const uint32 voiceIndex = static_cast<uint32>( mixer.findBusIndex( sw::hashed_string( "voice" ) ) );

    AudioDuckingTestInternal::runBlocks( mixer, voiceIndex, 0.001f, 20 ); // -60 dB
    SW_EXPECT_NEAR_EQUAL( 0.0f, mixer.getBusDuckDb( musicIndex ), 1e-4f );
    AudioDuckingTestInternal::runBlocks( mixer, voiceIndex, 0.1f, 20 ); // -20 dB
    SW_EXPECT_NEAR_EQUAL( -6.0206f, mixer.getBusDuckDb( musicIndex ), 0.01f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, mixer.getBusDuckDb( voiceIndex ), 1e-6f );
}

/**
 * @brief [AudioDuckingTest] 모르는 소스 버스 · 자기 자신 · 양수 깊이는 읽기 오류다
 */
SW_TEST_CASE( AudioDuckingTest, BadDuckDataIsALoadError )
{
    SW_TEST_DEFENSIVE_SCOPE( "bad duck data is a read error" ); // 아래 자료는 모두 일부러 깨져 있다
    test::ScopedLogCollector logs;
    sw::AudioMixerDesc       desc;
    SW_EXPECT_FALSE( desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus><AudioBusDesc _name="master" />
        <AudioBusDesc _name="music" _parent="master"><_listDuck><AudioDuckDesc _sourceBus="nope" /></_listDuck></AudioBusDesc></_listBus></AudioMixerDesc>)" ) );
    SW_EXPECT_FALSE( desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus><AudioBusDesc _name="master" />
        <AudioBusDesc _name="music" _parent="master"><_listDuck><AudioDuckDesc _sourceBus="music" /></_listDuck></AudioBusDesc></_listBus></AudioMixerDesc>)" ) );
    SW_EXPECT_FALSE( desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus><AudioBusDesc _name="master" />
        <AudioBusDesc _name="music" _parent="master"><_listDuck><AudioDuckDesc _sourceBus="master" _depthDb="3" /></_listDuck></AudioBusDesc></_listBus></AudioMixerDesc>)" ) );
    SW_EXPECT_TRUE( logs.joined().find( "ducks itself" ) != sw::string::npos );
}
