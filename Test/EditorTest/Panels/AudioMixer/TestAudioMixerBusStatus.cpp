#include "pch.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/IAudioService.h"

#include "TestFramework/TestFramework.h"

// AudioMixerBusStatusTest — 오디오 믹서 창이 읽는 버스 상태 사본(ImGui 없음).

/**
 * @brief [AudioMixerBusStatusTest] 버스 상태 사본은 사용자 볼륨 · 음소거 · 솔로를 다음 블록에 보인다 — 오디오 믹서 창이 읽는 값이다
 */
SW_TEST_CASE( AudioMixerBusStatusTest, BusStatusFollowsTheGameThreadCommands )
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
