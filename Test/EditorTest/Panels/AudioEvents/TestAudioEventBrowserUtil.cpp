#include "pch.h"

#include "Editor/Panels/AudioEvents/AudioEventBrowserUtil.h"

#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioSpatial.h"

#include "TestFramework/TestFramework.h"

// AudioEventBrowserUtilTest — 오디오 이벤트 창이 엔진 상태에서 만드는 표 행(ImGui 없음).

namespace
{
    sw::AudioInstanceStatus makeStatus( const char* pEvent, uint32 playingID, float32 remainingSeconds )
    {
        sw::AudioInstanceStatus status;
        status._event            = sw::hashed_string( pEvent );
        status._bus              = sw::hashed_string( "sfx" );
        status._playingID        = playingID;
        status._remainingSeconds = remainingSeconds;
        status._voiceCount       = 1;
        return status;
    }
} // namespace

/**
 * @brief [AudioEventBrowserUtilTest] 서비스 상태가 표 행이 된다 — 이름 거르기, 인스턴스 정렬과 거르기, 보이스 상한, 이벤트 속성 값
 */
SW_TEST_CASE( AudioEventBrowserUtilTest, EngineStateBecomesTableRows )
{
    using sw::editor::AudioEventBrowserUtil;

    // 이름: 대소문자 무시 필터, 사전순
    sw::vector<sw::hashed_string> listName{ sw::hashed_string( "Footstep" ), sw::hashed_string( "Explosion" ), sw::hashed_string( "footfall" ) };
    sw::vector<sw::hashed_string> listVisible;
    AudioEventBrowserUtil::filterEventNames( listName, "FOOT", listVisible );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listVisible.size() ) );
    SW_EXPECT_TRUE( listVisible[0] == sw::hashed_string( "Footstep" ) || listVisible[0] == sw::hashed_string( "footfall" ) );
    AudioEventBrowserUtil::filterEventNames( listName, "", listVisible );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listVisible.size() ) );

    // 인스턴스: 남은 시간이 짧은 것이 먼저, 반복(-1)은 맨 아래, 이벤트로 거른다
    sw::vector<sw::AudioInstanceStatus> listStatus{ makeStatus( "Loop", 1, -1.0f ), makeStatus( "Footstep", 2, 0.75f ), makeStatus( "Footstep", 3, 0.25f ) };
    sw::vector<sw::AudioInstanceStatus> listRow;
    AudioEventBrowserUtil::buildInstanceRows( listStatus, sw::hashed_string{}, listRow );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listRow.size() ) );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listRow[0]._playingID ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listRow[1]._playingID ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listRow[2]._playingID ) );
    AudioEventBrowserUtil::buildInstanceRows( listStatus, sw::hashed_string( "Loop" ), listRow );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listRow.size() ) );
    SW_EXPECT_TRUE( listRow[0]._event == sw::hashed_string( "Loop" ) );

    // 보이스: 실제 상한에 닿으면 표시한다. 상한을 모르면(0) 사용률 0
    sw::AudioEngineStats stats;
    stats._realVoiceCount    = 4;
    stats._virtualVoiceCount = 3;
    stats._voiceCount        = 7;
    stats._maxRealVoiceCount = 4;
    stats._maxVoiceCount     = 14;
    const sw::editor::AudioVoiceProfile profile = AudioEventBrowserUtil::buildVoiceProfile( stats );
    SW_EXPECT_TRUE( profile._bRealLimitReached );
    SW_EXPECT_FALSE( profile._bVoiceLimitReached );
    SW_EXPECT_NEAR_EQUAL( 1.0f, profile._realUsage, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, profile._voiceUsage, 0.0001f );
    SW_EXPECT_EQUAL( 3u, profile._virtualVoiceCount );
    const sw::editor::AudioVoiceProfile emptyProfile = AudioEventBrowserUtil::buildVoiceProfile( sw::AudioEngineStats{} );
    SW_EXPECT_FALSE( emptyProfile._bRealLimitReached );
    SW_EXPECT_NEAR_EQUAL( 0.0f, emptyProfile._realUsage, 0.0001f );

    // 이벤트 속성: 볼륨 범위, 2D 반경, 감쇠 반경
    sw::AudioEventDesc eventDesc;
    eventDesc._bus         = sw::hashed_string( "sfx" );
    eventDesc._volumeDbMin = -3.0f;
    eventDesc._volumeDbMax = 0.0f;
    sw::AudioAttenuationDesc attenuation;
    sw::vector<sw::editor::AudioEventFieldRow> listField;
    AudioEventBrowserUtil::buildEventFieldRows( eventDesc, attenuation, listField );
    SW_ASSERT_TRUE( listField.size() >= 4 );
    SW_EXPECT_TRUE( listField[0]._value == "sfx" );
    SW_EXPECT_TRUE( listField[1]._value == "-3.00 .. 0.00 dB" );
    SW_EXPECT_TRUE( listField[3]._value == "2D" );
    eventDesc._attenuation   = sw::hashed_string( "Small" );
    attenuation._minDistance = 2.0f;
    attenuation._maxDistance = 20.0f;
    AudioEventBrowserUtil::buildEventFieldRows( eventDesc, attenuation, listField );
    SW_EXPECT_TRUE( listField[3]._value == "2.00 .. 20.00 m (Small)" );

    SW_EXPECT_TRUE( AudioEventBrowserUtil::formatRemainingSeconds( 1.25f ) == "1.25 s" );
}
