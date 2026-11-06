// LiveEventRulesTest — 라이브 이벤트 열림 판정: 한 번짜리는 반열림 [시작, 끝), 매일 반복은 UTC 회차 창 안에서만(정의 구간 밖은 닫힘), 대상(지역 · 최소 빌드 ·
// 출시 비율 0 과 전부), 정의 검사(id 형식 · 끝이 시작 뒤 · 반복 길이). 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveEventRules.h"

using namespace sw;

namespace
{
    constexpr int64 kMinuteMs = 60 * 1000;
    constexpr int64 kDayMs    = 24 * 60 * kMinuteMs;

    LiveEventDef makeOnce( int64 startMs, int64 endMs )
    {
        LiveEventDef definition;
        definition._eventID = "spring.sale";
        definition._kind    = "shop_sale";
        definition._startMs = startMs;
        definition._endMs   = endMs;
        return definition;
    }
} // namespace

/** @brief [LiveEventRulesTest] 한 번짜리 이벤트는 시작 시각에 열리고 끝 시각에 닫힌다(반열림), 창의 끝은 정의의 끝이다 */
SW_TEST_CASE( LiveEventRulesTest, OnceEventIsOpenInsideItsHalfOpenRange )
{
    const LiveEventDef definition = makeOnce( 1000, 5000 );
    SW_REQUIRE_TRUE( LiveEventRules::isValid( definition ) );
    int64 windowEndMs = 0;
    SW_EXPECT_FALSE( LiveEventRules::isWindowOpen( definition, 999, windowEndMs ) );
    SW_EXPECT_TRUE( LiveEventRules::isWindowOpen( definition, 1000, windowEndMs ) );
    SW_EXPECT_EQUAL( int64( 5000 ), windowEndMs );
    SW_EXPECT_TRUE( LiveEventRules::isWindowOpen( definition, 4999, windowEndMs ) );
    SW_EXPECT_FALSE( LiveEventRules::isWindowOpen( definition, 5000, windowEndMs ) );
}

/** @brief [LiveEventRulesTest] 매일 반복은 UTC 회차(시작 분 · 길이) 안에서만 열리고, 창의 시작 · 끝을 돌려주며, 정의 구간 밖의 회차는 열리지 않는다 */
SW_TEST_CASE( LiveEventRulesTest, DailyEventOpensOnlyInsideEachOccurrence )
{
    LiveEventDef definition       = makeOnce( 0, 10 * kDayMs );
    definition._recurrence        = LiveEventRecurrence::Daily;
    definition._activeMinuteOfDay = 60; // 01:00 UTC
    definition._activeDurationMs  = 30 * kMinuteMs;
    SW_REQUIRE_TRUE( LiveEventRules::isValid( definition ) );

    const int64 dayTwo      = 2 * kDayMs;
    int64       windowStart = 0;
    int64       windowEnd   = 0;
    SW_EXPECT_FALSE( LiveEventRules::findOpenWindow( definition, dayTwo + 59 * kMinuteMs, windowStart, windowEnd ) );
    SW_REQUIRE_TRUE( LiveEventRules::findOpenWindow( definition, dayTwo + 70 * kMinuteMs, windowStart, windowEnd ) );
    SW_EXPECT_EQUAL( dayTwo + 60 * kMinuteMs, windowStart );
    SW_EXPECT_EQUAL( dayTwo + 90 * kMinuteMs, windowEnd );
    SW_EXPECT_FALSE( LiveEventRules::findOpenWindow( definition, dayTwo + 90 * kMinuteMs, windowStart, windowEnd ) );
    SW_EXPECT_FALSE( LiveEventRules::findOpenWindow( definition, 11 * kDayMs + 70 * kMinuteMs, windowStart, windowEnd ) ); // 정의가 끝났다
}

/** @brief [LiveEventRulesTest] 대상: 지역 목록이 있으면 그 지역만, 최소 빌드 미만은 안 보이고, 출시 비율 0 은 아무에게도 · 전부는 모두에게 보인다 */
SW_TEST_CASE( LiveEventRulesTest, AudienceMatchesRegionBuildAndRollout )
{
    LiveEventDef definition     = makeOnce( 0, kDayMs );
    definition._listRegion      = { "eu" };
    definition._minBuildVersion = 5;
    SW_EXPECT_TRUE( LiveEventRules::isAudienceMatch( definition, 42, "eu", 5 ) );
    SW_EXPECT_FALSE( LiveEventRules::isAudienceMatch( definition, 42, "us", 5 ) );
    SW_EXPECT_FALSE( LiveEventRules::isAudienceMatch( definition, 42, "eu", 4 ) );

    definition._listRegion.clear();
    definition._rolloutBasisPoints = 0;
    for ( AccountID accountID = 1; accountID <= 64; ++accountID )
        SW_EXPECT_FALSE( LiveEventRules::isAudienceMatch( definition, accountID, "us", 9 ) );
    definition._rolloutBasisPoints = 10000;
    for ( AccountID accountID = 1; accountID <= 64; ++accountID )
        SW_EXPECT_TRUE( LiveEventRules::isAudienceMatch( definition, accountID, "us", 9 ) );
}

/** @brief [LiveEventRulesTest] 정의 검사: id 형식 · 끝이 시작 뒤 · 반복이면 회차 길이 · 출시 비율 범위를 본다 */
SW_TEST_CASE( LiveEventRulesTest, InvalidDefinitionsAreRejected )
{
    SW_EXPECT_TRUE( LiveEventRules::isValid( makeOnce( 0, 1 ) ) );
    SW_EXPECT_FALSE( LiveEventRules::isValid( makeOnce( 5, 5 ) ) );
    LiveEventDef badID = makeOnce( 0, 1 );
    badID._eventID     = "Spring Sale";
    SW_EXPECT_FALSE( LiveEventRules::isValid( badID ) );
    LiveEventDef noDuration = makeOnce( 0, kDayMs );
    noDuration._recurrence  = LiveEventRecurrence::Daily;
    SW_EXPECT_FALSE( LiveEventRules::isValid( noDuration ) );
    LiveEventDef overRollout        = makeOnce( 0, 1 );
    overRollout._rolloutBasisPoints = 10001;
    SW_EXPECT_FALSE( LiveEventRules::isValid( overRollout ) );
}
