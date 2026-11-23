// AutomationWatchGroupTest — 감시 값 묶음은 `<묶음>.` 으로 시작하는 씬 감시 값만 모은다(디버그 HUD 범주가 보이는 것, nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

using namespace sw;

namespace
{
    bool readOne( const GameObjectManager* /*pManager*/, float64& outValue )
    {
        outValue = 1.0;
        return true;
    }

    bool readQuery( const AutomationWatchQuery& /*query*/, AutomationWatchValue& outValue )
    {
        outValue.setNumber( 2.0 );
        return true;
    }
} // namespace

/** @brief [AutomationWatchGroupTest] 앞만 같은 이름 · 대상이 필요한 값 · 바깥 일을 따르는 값은 빠진다 */
SW_TEST_CASE( AutomationWatchGroupTest, OnlySceneWatchesOfTheGroup )
{
    const AutomationWatchRegistration arrWatch[] = {
        {     "WatchGroupTest.A", "", &readOne,    nullptr, false, false, false, nullptr},
        {     "WatchGroupTest.B", "",  nullptr, &readQuery, false, false, false, nullptr},
        {     "WatchGroupTest.C", "", &readOne,    nullptr, false, false,  true, nullptr},
        {"WatchGroupTestOther.D", "", &readOne,    nullptr, false, false, false, nullptr},
    };
    for ( const AutomationWatchRegistration& watch : arrWatch )
    {
        SW_EXPECT_TRUE( AutomationWatchRegistry::registerWatch( &watch ) );
    }
    vector<const AutomationWatchRegistration*> listWatch;
    AutomationWatchRegistry::collectSceneWatches( "WatchGroupTest", listWatch );
    for ( const AutomationWatchRegistration& watch : arrWatch )
    {
        AutomationWatchRegistry::unregisterWatch( &watch );
    }
    SW_REQUIRE_EQUAL( size_t( 1 ), listWatch.size() );
    SW_EXPECT_TRUE( listWatch[0] == &arrWatch[0] );
}
