#include "pch.h"

#include "Editor/Panels/AudioMixer/AudioDataHotReload.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/IAudioService.h"

#include "TestFramework/TestFramework.h"

// AudioDataHotReload — 오디오 데이터 파일이 바뀌면 엔진이 다시 읽는 길(ImGui 없음).

/**
 * @brief [AudioDataHotReloadTest] 믹서 그래프 · 이벤트 라이브러리 파일만 오디오 데이터로 본다(대소문자 무시)
 */
SW_TEST_CASE( AudioDataHotReloadTest, RecognizesAudioDataFiles )
{
    SW_EXPECT_TRUE( sw::editor::AudioDataHotReload::isAudioDataPath( "engine/audio/default.audiomixer.xml" ) );
    SW_EXPECT_TRUE( sw::editor::AudioDataHotReload::isAudioDataPath( "game/shooter3d/audio/shooter3d.audioevents.xml" ) );
    SW_EXPECT_TRUE( sw::editor::AudioDataHotReload::isAudioDataPath( "game/x/audio/X.AudioEvents.XML" ) );
    SW_EXPECT_FALSE( sw::editor::AudioDataHotReload::isAudioDataPath( "game/x/audio/theme.music.xml" ) );
    SW_EXPECT_FALSE( sw::editor::AudioDataHotReload::isAudioDataPath( "textures/stone.dds" ) );
}

/**
 * @brief [AudioDataHotReloadTest] 게임이 로드하지 않은 라이브러리와 읽지 못하는 믹서 파일은 다시 읽지 않고, 지금 믹서 그래프가 그대로 남는다
 */
SW_TEST_CASE( AudioDataHotReloadTest, KeepsTheCurrentDataWhenTheFileCannotBeUsed )
{
    SW_TEST_DEFENSIVE_SCOPE( "Reloading audio data that is not loaded or not readable" );
    sw::unique_ptr<sw::IAudioService> pAudio = sw::IAudioService::createNull();
    SW_ASSERT_TRUE( pAudio->initialize() );
    pAudio->update( 0.05f ); // 장치가 없으면 update 가 오프라인으로 렌더해 버스 상태를 게시한다
    sw::vector<sw::AudioBusStatus> listBefore;
    pAudio->getEngine().getBusStatuses( listBefore );
    SW_ASSERT_TRUE( listBefore.empty() == false );

    SW_EXPECT_FALSE( sw::editor::AudioDataHotReload::reloadChangedFile( *pAudio, "game/none/audio/none.audioevents.xml" ) ); // 게임이 로드하지 않았다
    SW_EXPECT_FALSE( sw::editor::AudioDataHotReload::reloadChangedFile( *pAudio, "textures/stone.dds" ) );
    SW_EXPECT_FALSE( sw::editor::AudioDataHotReload::reloadChangedFile( *pAudio, "tests/missing.audiomixer.xml" ) ); // 없는 믹서 파일

    pAudio->update( 0.05f );
    sw::vector<sw::AudioBusStatus> listAfter;
    pAudio->getEngine().getBusStatuses( listAfter );
    SW_EXPECT_EQUAL( listBefore.size(), listAfter.size() );
    SW_EXPECT_TRUE( pAudio->getEngine().hasEventLibrary( sw::hashed_string( "game/none/audio/none.audioevents.xml" ) ) == false );
    pAudio->shutdown();
}

/**
 * @brief [AudioDataHotReloadTest] 버스 상태 사본은 사용자 볼륨 · 음소거 · 솔로를 다음 블록에 보인다 — 오디오 믹서 창이 읽는 값이다
 */
SW_TEST_CASE( AudioDataHotReloadTest, BusStatusFollowsTheGameThreadCommands )
{
    sw::unique_ptr<sw::IAudioService> pAudio = sw::IAudioService::createNull();
    SW_ASSERT_TRUE( pAudio->initialize() );
    sw::AudioEngine& engine = pAudio->getEngine();
    pAudio->update( 0.05f );
    sw::vector<sw::AudioBusStatus> listStatus;
    engine.getBusStatuses( listStatus );
    SW_ASSERT_TRUE( listStatus.empty() == false );
    SW_EXPECT_TRUE( listStatus[0]._name == sw::hashed_string( "master" ) );
    SW_EXPECT_EQUAL( -1, listStatus[0]._parentIndex );

    pAudio->setBusVolume( sw::hashed_string( "sfx" ), 0.25f );
    engine.setBusMuted( sw::hashed_string( "music" ), true );
    engine.setBusSolo( sw::hashed_string( "ui" ), true );
    pAudio->update( 0.05f );
    engine.getBusStatuses( listStatus );
    bool bSawSfx   = false;
    bool bSawMusic = false;
    bool bSawUI    = false;
    for ( const sw::AudioBusStatus& status : listStatus )
    {
        if ( status._name == sw::hashed_string( "sfx" ) )
        {
            bSawSfx = true;
            SW_EXPECT_NEAR_EQUAL( 0.25f, status._userVolume, 0.0001f );
        }
        else if ( status._name == sw::hashed_string( "music" ) )
        {
            bSawMusic = true;
            SW_EXPECT_TRUE( status._bMuted );
        }
        else if ( status._name == sw::hashed_string( "ui" ) )
        {
            bSawUI = true;
            SW_EXPECT_TRUE( status._bSolo );
        }
    }
    SW_EXPECT_TRUE( bSawSfx && bSawMusic && bSawUI );
    pAudio->shutdown();
}
