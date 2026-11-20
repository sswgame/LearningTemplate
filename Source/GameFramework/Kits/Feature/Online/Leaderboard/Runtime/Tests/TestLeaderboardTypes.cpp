// LeaderboardTypesTest — 순위표 · 통계 id 규칙: 소문자 · 숫자 · 밑줄만, 빈 id 와 상한 + 1 은 거절한다. 서버는 받은 id 를 이 함수로 거른다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardTypes.h"

using namespace sw;

/** @brief [LeaderboardTypesTest] 소문자 · 숫자 · 밑줄 id 는 받고, 빈 id · 대문자 · 다른 글자 · 상한 + 1 은 거절한다 */
SW_TEST_CASE( LeaderboardTypesTest, IDRuleAcceptsLowercaseDigitsAndUnderscore )
{
    SW_EXPECT_TRUE( LeaderboardNameRule::isValidID( "weekly_score_2" ) );
    SW_EXPECT_FALSE( LeaderboardNameRule::isValidID( "" ) );
    SW_EXPECT_FALSE( LeaderboardNameRule::isValidID( "Weekly" ) );
    SW_EXPECT_FALSE( LeaderboardNameRule::isValidID( "weekly-score" ) );
    SW_EXPECT_FALSE( LeaderboardNameRule::isValidID( "weekly score" ) );
    const string atLimit( static_cast<size_t>( LeaderboardLimit::kMaxIDSize ), 'a' );
    SW_EXPECT_TRUE( LeaderboardNameRule::isValidID( atLimit ) );
    StringBuilder<constant::buffer::kMax128> overLimit;
    overLimit.appendFormat( "%#a", atLimit );
    SW_EXPECT_FALSE( LeaderboardNameRule::isValidID( overLimit.view() ) );
}
