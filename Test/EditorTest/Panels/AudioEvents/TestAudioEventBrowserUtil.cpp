#include "pch.h"

#include "Editor/Panels/AudioEvents/Model/AudioEventBrowserUtil.h"

#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioSpatial.h"

#include "TestFramework/TestFramework.h"

// AudioEventBrowserUtilTest — 오디오 이벤트 창이 엔진 상태에서 만드는 표 행(ImGui 없음).

namespace
{
    sw::AudioInstanceStatus makeStatus( const utf8* pEvent, uint32 playingID, float32 remainingSeconds )
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
    AudioEventBrowserUtil::makeInstanceRows( listStatus, sw::hashed_string{}, listRow );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listRow.size() ) );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listRow[0]._playingID ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listRow[1]._playingID ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listRow[2]._playingID ) );
    AudioEventBrowserUtil::makeInstanceRows( listStatus, sw::hashed_string( "Loop" ), listRow );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listRow.size() ) );
    SW_EXPECT_TRUE( listRow[0]._event == sw::hashed_string( "Loop" ) );

    // 보이스: 실제 상한에 닿으면 표시한다. 상한을 모르면(0) 사용률 0
    sw::AudioEngineStats stats;
    stats._realVoiceCount                       = 4;
    stats._virtualVoiceCount                    = 3;
    stats._voiceCount                           = 7;
    stats._maxRealVoiceCount                    = 4;
    stats._maxVoiceCount                        = 14;
    const sw::editor::AudioVoiceProfile profile = AudioEventBrowserUtil::makeVoiceProfile( stats );
    SW_EXPECT_TRUE( profile._bRealLimitReached );
    SW_EXPECT_FALSE( profile._bVoiceLimitReached );
    SW_EXPECT_NEAR_EQUAL( 1.0f, profile._realUsage, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, profile._voiceUsage, 0.0001f );
    SW_EXPECT_EQUAL( 3u, profile._virtualVoiceCount );
    const sw::editor::AudioVoiceProfile emptyProfile = AudioEventBrowserUtil::makeVoiceProfile( sw::AudioEngineStats{} );
    SW_EXPECT_FALSE( emptyProfile._bRealLimitReached );
    SW_EXPECT_NEAR_EQUAL( 0.0f, emptyProfile._realUsage, 0.0001f );

    // 이벤트 속성: 볼륨 범위, 2D 반경, 감쇠 반경
    sw::AudioEventDesc eventDesc;
    eventDesc._bus         = sw::hashed_string( "sfx" );
    eventDesc._volumeDbMin = -3.0f;
    eventDesc._volumeDbMax = 0.0f;
    sw::AudioAttenuationDesc                   attenuation;
    sw::vector<sw::editor::AudioEventFieldRow> listField;
    AudioEventBrowserUtil::makeEventFieldRows( eventDesc, attenuation, listField );
    SW_ASSERT_TRUE( listField.size() >= 4 );
    SW_EXPECT_TRUE( listField[0]._value == "sfx" );
    SW_EXPECT_TRUE( listField[1]._value == "-3.00 .. 0.00 dB" );
    SW_EXPECT_TRUE( listField[3]._value == "2D" );
    eventDesc._attenuation   = sw::hashed_string( "Small" );
    attenuation._minDistance = 2.0f;
    attenuation._maxDistance = 20.0f;
    AudioEventBrowserUtil::makeEventFieldRows( eventDesc, attenuation, listField );
    SW_EXPECT_TRUE( listField[3]._value == "2.00 .. 20.00 m (Small)" );

    SW_EXPECT_TRUE( AudioEventBrowserUtil::formatRemainingSeconds( 1.25f ) == "1.25 s" );
}

/**
 * @brief [AudioEventBrowserUtilTest] 창에서 이벤트를 복제하고 지운 사본은 라이브러리 검사를 통과한다 — 복제는 겹치지 않는 이름과 원본의 클립을 갖는다
 * @details 클립 없는 빈 이벤트를 더하면 검사가 저장을 막는다. 이름이 겹쳐도 막는다. 그래서 더하기는 복제로 하고 이름은 `_Copy`, `_Copy2` 로 피한다.
 */
SW_TEST_CASE( AudioEventBrowserUtilTest, DuplicatedAndRemovedEventsKeepTheLibraryValid )
{
    using sw::editor::AudioEventBrowserUtil;

    sw::AudioEventLibrary library;
    sw::AudioEventDesc    step;
    step._name = sw::hashed_string( "Footstep" );
    step._bus  = sw::hashed_string( "sfx" );
    sw::AudioClipEntry clip;
    clip._path = sw::hashed_string( "game/x/sounds/step_0.ogg" );
    step._listClip.push_back( clip );
    library._listEvent.push_back( step );
    SW_ASSERT_TRUE( library.validate( "<test>" ) );

    const sw::hashed_string first  = AudioEventBrowserUtil::duplicateEvent( library, sw::hashed_string( "Footstep" ) );
    const sw::hashed_string second = AudioEventBrowserUtil::duplicateEvent( library, sw::hashed_string( "Footstep" ) );
    SW_EXPECT_TRUE( first == sw::hashed_string( "Footstep_Copy" ) );
    SW_EXPECT_TRUE( second == sw::hashed_string( "Footstep_Copy2" ) );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( library._listEvent.size() ) );
    const sw::AudioEventDesc* pCopy = library.findEvent( first );
    SW_ASSERT_NOT_NULL( pCopy );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( pCopy->_listClip.size() ) );
    SW_EXPECT_TRUE( pCopy->_bus == sw::hashed_string( "sfx" ) );
    SW_EXPECT_TRUE( library.validate( "<test>" ) );

    SW_EXPECT_TRUE( AudioEventBrowserUtil::duplicateEvent( library, sw::hashed_string( "Missing" ) ).empty() );
    SW_EXPECT_TRUE( AudioEventBrowserUtil::removeEvent( library, first ) );
    SW_EXPECT_FALSE( AudioEventBrowserUtil::removeEvent( library, first ) );
    SW_EXPECT_NULL( library.findEvent( first ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( library._listEvent.size() ) );
    SW_EXPECT_TRUE( library.validate( "<test>" ) );
}
