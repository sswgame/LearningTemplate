// AutomationStageFingerprintTest — 자동화 단계 지문의 비교: 새로 남은 상태와 늘어난 수를 이름으로 대고, 처음 센 수 · 줄어든 수 · 같은 상태는 새지 않은 것이다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "DevTools/Client/Automation/Runner/AutomationStageFingerprint.h"

using namespace sw;

/**
 * @brief [AutomationStageFingerprintTest] 앞 단계가 남긴 상태 · 늘어난 수만 이름으로 낸다. 결함이면 새는 상태가 다음 단계의 골든이 우연히 갈릴 때만 보이거나, 정상인 단계가 진다
 */
SW_TEST_CASE( AutomationStageFingerprintTest, NamesNewStatesAndGrownCounts )
{
    AutomationStageFingerprint baseline;
    baseline._listEntry.push_back( AutomationStageFingerprintEntry{ "Localization", "missing localization keys", nullopt } );
    baseline._listEntry.push_back( AutomationStageFingerprintEntry{ "EventDispatcher subscribers", {}, uint64{ 12 } } );
    baseline._listEntry.push_back( AutomationStageFingerprintEntry{ "SceneManager loaded scenes", {}, uint64{ 1 } } );
    SW_EXPECT_TRUE( AutomationStageFingerprint::describeLeaks( baseline, baseline ).empty() );

    AutomationStageFingerprint current;
    current._listEntry.push_back( AutomationStageFingerprintEntry{ "UISubtitles", "subtitles", nullopt } );
    current._listEntry.push_back( AutomationStageFingerprintEntry{ "Localization", "missing localization keys", nullopt } );
    current._listEntry.push_back( AutomationStageFingerprintEntry{ "EventDispatcher subscribers", {}, uint64{ 13 } } );
    current._listEntry.push_back( AutomationStageFingerprintEntry{ "SceneManager loaded scenes", {}, uint64{ 0 } } );
    current._listEntry.push_back( AutomationStageFingerprintEntry{ "SceneManager preview scenes", {}, uint64{ 4 } } ); // 처음 센 값이다
    const vector<string> listLeak = AutomationStageFingerprint::describeLeaks( baseline, current );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listLeak.size() );
    SW_EXPECT_EQUAL( string( "UISubtitles: subtitles" ), listLeak[0] );
    SW_EXPECT_EQUAL( string( "EventDispatcher subscribers: 12 -> 13" ), listLeak[1] );
}
