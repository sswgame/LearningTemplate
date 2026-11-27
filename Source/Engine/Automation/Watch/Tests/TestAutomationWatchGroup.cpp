// AutomationWatchGroupTest — 감시 값 묶음은 `<묶음>.` 으로 시작하는 씬 감시 값만 모은다(디버그 HUD 범주가 보이는 것, nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Content/Pack/PackTypes.h"

using namespace sw;

namespace
{
    bool readOne( const GameObjectManager* /*pManager*/, AutomationWatchValue& outValue )
    {
        outValue.setNumber( 1.0 );
        return true;
    }

    template <typename T, typename = void>
    struct CanSetNumber : std::false_type
    {
    };
    template <typename T>
    struct CanSetNumber<T, std::void_t<decltype( std::declval<AutomationWatchValue&>().setNumber( std::declval<T>() ) )>> : std::true_type
    {
    };
    static_assert( CanSetNumber<float64>::value && CanSetNumber<int32>::value, "numbers go through setNumber" );
    static_assert( CanSetNumber<PackLayoutMode>::value == false, "an enum must not reach setNumber - its number shifts when an enumerator is added" );

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

/** @brief [AutomationWatchGroupTest] 열거 값은 열거자 이름 글이고, 이름이 없는 값은 그 사실을 적은 글이다(번호를 내지 않는다) */
SW_TEST_CASE( AutomationWatchGroupTest, EnumValueIsEnumeratorName )
{
    AutomationWatchValue value;
    value.setEnum( PackCompressionKind::Zlib );
    SW_EXPECT_TRUE( value._bText );
    SW_EXPECT_EQUAL( string( "Zlib" ), value._text );
    value.setEnum( static_cast<PackCompressionKind>( 7 ) );
    SW_EXPECT_TRUE( value._bText );
    SW_EXPECT_TRUE( value._text.find( "no ENUM() name" ) != string::npos );
}
