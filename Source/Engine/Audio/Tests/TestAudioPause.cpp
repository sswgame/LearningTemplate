// AudioPauseTest — 일시정지는 소리와 시계(적응형 음악의 구간 위치 · 예약한 전환, 이벤트 인스턴스의 경과)를 함께 세우고 그 자리에서 잇는다. 멈춘 음악을 멈추면 보이스가 남지 않는다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Audio/AudioMixerDef.h"
#include "Engine/Audio/AudioMusic.h"
#include "Engine/Audio/Tests/OfflineAudioEngine.h"

namespace
{
    struct AudioPauseTestUtil
    {
        /** @brief 픽스처의 클립 한 레이어짜리 반복 구간 둘(A · B)이고, 구간은 다음 박에 넘어가는 음악입니다. */
        static sw::shared_ptr<const sw::AudioMusicDef> makeTwoSegmentMusic()
        {
            sw::AudioMusicLayerDef layer;
            layer._path                              = sw::hashed_string( sw::test::OfflineAudioEngine::kClipPath );
            sw::shared_ptr<sw::AudioMusicDef> pMusic = sw::make_shared<sw::AudioMusicDef>();
            pMusic->_bus                             = sw::hashed_string( sw::AudioBusNames::kMusic );
            pMusic->_defaultSync                     = sw::AudioMusicSync::NextBeat;
            for ( const utf8* pName : { "A", "B" } )
            {
                sw::AudioMusicSegmentDef segment;
                segment._name = sw::hashed_string( pName );
                segment._listLayer.push_back( layer );
                pMusic->_listSegment.push_back( segment );
            }
            return pMusic;
        }
    };
} // namespace

/**
 * @brief [AudioPauseTest] 멈춘 동안 구간 위치가 그대로이고, 이어 내면 멈춘 자리부터 흐른다 — 리듬 판정이 읽는 시계다
 * @details 변이: 음악 시계를 렌더 프레임으로 두면(`getMusicClock` 이 `_renderedFrameCount`) 멈춘 블록만큼 박이 앞서 첫 단언이 진다.
 */
SW_TEST_CASE( AudioPauseTest, SegmentClockStandsWhilePaused )
{
    sw::test::OfflineAudioEngine offline;
    sw::AudioEngine&             engine    = offline.getEngine();
    const sw::AudioPlayingID     playingID = engine.startMusic( AudioPauseTestUtil::makeTwoSegmentMusic(), 0.0f );
    SW_REQUIRE_TRUE( playingID != 0 );
    offline.renderBlocks( 4 );

    engine.setPaused( playingID, true );
    offline.renderBlocks( 1 );
    const float64 pausedBeat = engine.getMusicStatus()._beat;
    offline.renderBlocks( 32 );
    SW_EXPECT_TRUE( engine.getMusicStatus()._bPlaying );
    SW_EXPECT_NEAR( pausedBeat, engine.getMusicStatus()._beat, 1e-9 );

    engine.setPaused( playingID, false );
    offline.renderBlocks( 4 );
    const float64 resumedBeat = engine.getMusicStatus()._beat;
    SW_EXPECT_NEAR( pausedBeat * 2.0, resumedBeat, 1e-9 ); // 앞뒤로 네 블록씩 흘렀다
}

/** @brief [AudioPauseTest] 멈춘 음악에 예약한 구간은 멈춘 동안 넘어가지 않고, 이어 낸 뒤 제 박에 넘어간다 */
SW_TEST_CASE( AudioPauseTest, TransitionWaitsForResume )
{
    const uint32                 blocksPerBeat = sw::audio::kSampleRate / 2 / sw::audio::kBlockFrameCount + 1; // 120 BPM 의 한 박
    sw::test::OfflineAudioEngine offline;
    sw::AudioEngine&             engine    = offline.getEngine();
    const sw::AudioPlayingID     playingID = engine.startMusic( AudioPauseTestUtil::makeTwoSegmentMusic(), 0.0f );
    offline.renderBlocks( 4 );
    engine.setPaused( playingID, true );
    engine.setMusicSegment( sw::hashed_string( "B" ) );
    offline.renderBlocks( blocksPerBeat * 2 );
    SW_EXPECT_TRUE( engine.getMusicStatus()._segment == sw::hashed_string( "A" ) );

    engine.setPaused( playingID, false );
    offline.renderBlocks( blocksPerBeat );
    SW_EXPECT_TRUE( engine.getMusicStatus()._segment == sw::hashed_string( "B" ) );
}

/**
 * @brief [AudioPauseTest] 멈춘 음악을 페이드로 멈춰도 보이스가 남지 않고, 다음 음악은 멈추지 않은 채 흐른다
 * @details 변이: 멈춘 보이스에도 페이드를 걸면 페이드가 흐르지 않아 보이스가 남는다 — 보이스 수 단언이 진다.
 */
SW_TEST_CASE( AudioPauseTest, StoppingPausedMusicFreesItsVoices )
{
    sw::test::OfflineAudioEngine offline;
    sw::AudioEngine&             engine    = offline.getEngine();
    const sw::AudioPlayingID     playingID = engine.startMusic( AudioPauseTestUtil::makeTwoSegmentMusic(), 0.0f );
    offline.renderBlocks( 1 );
    engine.setPaused( playingID, true );
    engine.stopMusic( 1.0f );
    offline.renderBlocks( 1 );
    SW_EXPECT_EQUAL( 0u, engine.getStats()._voiceCount );
    SW_EXPECT_FALSE( engine.getMusicStatus()._bPlaying );

    SW_REQUIRE_TRUE( engine.startMusic( AudioPauseTestUtil::makeTwoSegmentMusic(), 0.0f ) != 0 );
    offline.renderBlocks( 2 );
    SW_EXPECT_TRUE( engine.getMusicStatus()._beat > 0.0 );
}

/**
 * @brief [AudioPauseTest] 멈춘 이벤트 인스턴스의 경과 시간도 멈추고, 이어 내면 그 자리부터 센다(음악 시계와 같은 규칙)
 * @details 변이: 경과를 렌더 프레임에서 시작 프레임을 빼 구하면 멈춘 블록만큼 앞서 둘째 단언이 진다.
 */
SW_TEST_CASE( AudioPauseTest, EventElapsedStandsWhilePaused )
{
    sw::test::OfflineAudioEngine        offline;
    sw::AudioEngine&                    engine    = offline.getEngine();
    const sw::AudioPlayingID            playingID = engine.postEvent( sw::hashed_string( sw::test::OfflineAudioEngine::kLoopEvent ) );
    sw::vector<sw::AudioInstanceStatus> listStatus;
    offline.renderBlocks( 4 );
    engine.setPaused( playingID, true );
    offline.renderBlocks( 32 );
    engine.getInstanceStatuses( listStatus );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listStatus.size() );
    SW_EXPECT_NEAR( sw::audio::kBlockSeconds * 4.0f, listStatus[0]._elapsedSeconds, 1e-6f );

    engine.setPaused( playingID, false );
    offline.renderBlocks( 4 );
    engine.getInstanceStatuses( listStatus );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listStatus.size() );
    SW_EXPECT_NEAR( sw::audio::kBlockSeconds * 8.0f, listStatus[0]._elapsedSeconds, 1e-6f );
}

/**
 * @brief [AudioPauseTest] 게임 일시정지는 게임 버스의 소리(이벤트 경과)를 세우고 음악 버스는 계속 낸다 — 풀면 멈춘 자리부터 이어진다
 * @details 변이: 블록이 버스를 보지 않고 모두 세우면 음악 박이, 아무것도 세우지 않으면 이벤트 경과가 진다.
 */
SW_TEST_CASE( AudioPauseTest, GamePauseHoldsGameBusesButNotMusic )
{
    sw::test::OfflineAudioEngine        offline;
    sw::AudioEngine&                    engine = offline.getEngine();
    sw::vector<sw::AudioInstanceStatus> listStatus;
    (void)engine.postEvent( sw::hashed_string( sw::test::OfflineAudioEngine::kLoopEvent ) ); // sfx 버스
    SW_REQUIRE_TRUE( engine.startMusic( AudioPauseTestUtil::makeTwoSegmentMusic(), 0.0f ) != 0 );
    offline.renderBlocks( 4 );
    const float64 beatBefore = engine.getMusicStatus()._beat;

    engine.setGamePaused( true );
    offline.renderBlocks( 8 );
    engine.getInstanceStatuses( listStatus );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listStatus.size() );
    SW_EXPECT_NEAR( sw::audio::kBlockSeconds * 4.0f, listStatus[0]._elapsedSeconds, 1e-6f );
    SW_EXPECT_NEAR( beatBefore * 3.0, engine.getMusicStatus()._beat, 1e-9 );

    engine.setGamePaused( false );
    offline.renderBlocks( 4 );
    engine.getInstanceStatuses( listStatus );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listStatus.size() );
    SW_EXPECT_NEAR( sw::audio::kBlockSeconds * 8.0f, listStatus[0]._elapsedSeconds, 1e-6f );
}

/** @brief [AudioPauseTest] 음악 버스가 멈추는 그래프에서는 음악 시계도 게임과 함께 서고 이어진다. 변이: 음악 플레이어가 게임 일시정지를 멈춤 청으로 적으면 이어진 뒤 박이 흐르지 않아 진다. */
SW_TEST_CASE( AudioPauseTest, MusicFollowsItsBusInGamePause )
{
    sw::test::OfflineAudioEngine offline;
    sw::AudioEngine&             engine = offline.getEngine();
    sw::AudioMixerDef            mixer  = sw::AudioMixerDef::makeDefault();
    for ( sw::AudioBusDef& bus : mixer._listBus )
    {
        bus._bPlaysInGamePause = false;
    }
    SW_REQUIRE_TRUE( engine.loadMixer( mixer ) );
    SW_REQUIRE_TRUE( engine.startMusic( AudioPauseTestUtil::makeTwoSegmentMusic(), 0.0f ) != 0 );
    offline.renderBlocks( 4 );
    const float64 beatBefore = engine.getMusicStatus()._beat;
    engine.setGamePaused( true );
    offline.renderBlocks( 8 );
    SW_EXPECT_NEAR( beatBefore, engine.getMusicStatus()._beat, 1e-9 );
    engine.setGamePaused( false );
    offline.renderBlocks( 4 );
    SW_EXPECT_NEAR( beatBefore * 2.0, engine.getMusicStatus()._beat, 1e-9 );
}
