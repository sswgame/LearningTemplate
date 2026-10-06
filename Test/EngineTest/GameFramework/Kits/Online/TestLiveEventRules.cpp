// 이벤트 열림 판정 — 한 번 기간, 매주 토요일 0 시부터 48 시간(주말 이벤트), 매일 20 시 1 시간, 기간 밖이면 반복도 닫힘(반열림 끝),
// 빌드 · 지역 · 출시 비율(같은 계정은 늘 같은 쪽, 1 만 계정 중 비율만큼, 이벤트마다 다른 계정 집합), 규칙 밖 정의는 거절.
#include "pch.h"

#include "GameFramework/Kits/Online/Server/LiveOps/LiveEventRules.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct LiveEventRulesTestInternal
    {
        static constexpr int64 kMonday20261005 = 1791158400000ll; ///< 2026-10-05 00:00 UTC(월요일)
        static constexpr int64 kHour           = 3600000ll;
        static constexpr int64 kDay            = 24 * kHour;
    };
} // namespace

SW_TEST_CASE( LiveEventRulesTest, OnceWeeklyWeekendAndDailyEvening )
{
    using Internal = LiveEventRulesTestInternal;
    LiveEventDefinition once;
    once._eventId     = "halloween";
    once._startMs     = Internal::kMonday20261005;
    once._endMs       = Internal::kMonday20261005 + 7 * Internal::kDay;
    int64 windowEndMs = 0;
    SW_EXPECT_FALSE( LiveEventRules::isWindowOpen( once, Internal::kMonday20261005 - 1, windowEndMs ) );
    SW_EXPECT_TRUE( LiveEventRules::isWindowOpen( once, Internal::kMonday20261005, windowEndMs ) );
    SW_EXPECT_EQUAL( windowEndMs, Internal::kMonday20261005 + 7 * Internal::kDay );
    SW_EXPECT_FALSE( LiveEventRules::isWindowOpen( once, Internal::kMonday20261005 + 7 * Internal::kDay, windowEndMs ) );

    LiveEventDefinition weekend;
    weekend._eventId          = "weekend_xp";
    weekend._startMs          = Internal::kMonday20261005;
    weekend._endMs            = Internal::kMonday20261005 + 28 * Internal::kDay;
    weekend._recurrence       = LiveEventRecurrence::Weekly;
    weekend._activeDayOfWeek  = 5; // 토요일
    weekend._activeDurationMs = 48 * Internal::kHour;
    SW_EXPECT_FALSE( LiveEventRules::isWindowOpen( weekend, Internal::kMonday20261005 + 4 * Internal::kDay, windowEndMs ) );                  // 금요일
    SW_EXPECT_TRUE( LiveEventRules::isWindowOpen( weekend, Internal::kMonday20261005 + 5 * Internal::kDay + Internal::kHour, windowEndMs ) ); // 토요일 1 시
    SW_EXPECT_EQUAL( windowEndMs, Internal::kMonday20261005 + 7 * Internal::kDay );
    SW_EXPECT_FALSE( LiveEventRules::isWindowOpen( weekend, Internal::kMonday20261005 + 7 * Internal::kDay, windowEndMs ) ); // 다음 월요일 0 시 — 끝(반열림)
    SW_EXPECT_TRUE( LiveEventRules::isWindowOpen( weekend, Internal::kMonday20261005 + 13 * Internal::kDay, windowEndMs ) ); // 둘째 주 일요일

    LiveEventDefinition evening;
    evening._eventId           = "evening_drop";
    evening._startMs           = Internal::kMonday20261005;
    evening._endMs             = Internal::kMonday20261005 + 3 * Internal::kDay;
    evening._recurrence        = LiveEventRecurrence::Daily;
    evening._activeMinuteOfDay = 20 * 60;
    evening._activeDurationMs  = Internal::kHour;
    SW_EXPECT_TRUE( LiveEventRules::isWindowOpen( evening, Internal::kMonday20261005 + Internal::kDay + 20 * Internal::kHour + Internal::kHour / 2, windowEndMs ) );
    SW_EXPECT_EQUAL( windowEndMs, Internal::kMonday20261005 + Internal::kDay + 21 * Internal::kHour );
    SW_EXPECT_FALSE( LiveEventRules::isWindowOpen( evening, Internal::kMonday20261005 + Internal::kDay + 21 * Internal::kHour, windowEndMs ) );
    SW_EXPECT_FALSE( LiveEventRules::isWindowOpen( evening, Internal::kMonday20261005 + 3 * Internal::kDay + 20 * Internal::kHour, windowEndMs ) ); // 기간 밖

    SW_EXPECT_TRUE( LiveEventRules::isValid( once ) );
    SW_EXPECT_TRUE( LiveEventRules::isValid( weekend ) );
    LiveEventDefinition broken = evening;
    broken._activeDurationMs   = 0; // 반복인데 회차 길이가 없다
    SW_EXPECT_FALSE( LiveEventRules::isValid( broken ) );
    broken          = once;
    broken._eventId = "Halloween"; // 키 규칙 밖
    SW_EXPECT_FALSE( LiveEventRules::isValid( broken ) );
    broken        = once;
    broken._endMs = broken._startMs;
    SW_EXPECT_FALSE( LiveEventRules::isValid( broken ) );
    broken                  = weekend;
    broken._activeDayOfWeek = 7;
    SW_EXPECT_FALSE( LiveEventRules::isValid( broken ) );
}

SW_TEST_CASE( LiveEventRulesTest, AudienceFilters )
{
    LiveEventDefinition definition;
    definition._eventId            = "beta_mode";
    definition._minBuildVersion    = 120;
    definition._listRegion         = { "kr", "jp" };
    definition._rolloutBasisPoints = 2500;
    SW_EXPECT_FALSE( LiveEventRules::isAudienceMatch( definition, 1, "kr", 119 ) );
    SW_EXPECT_FALSE( LiveEventRules::isAudienceMatch( definition, 1, "eu", 120 ) );
    LiveEventDefinition other = definition;
    other._eventId            = "beta_shop";
    int32 enabledCount        = 0;
    int32 differentCount      = 0;
    for ( AccountId accountId = 1; accountId <= 10000; ++accountId )
    {
        const bool bEnabled = LiveEventRules::isAudienceMatch( definition, accountId, "kr", 200 );
        enabledCount += bEnabled ? 1 : 0;
        differentCount += bEnabled != LiveEventRules::isAudienceMatch( other, accountId, "kr", 200 ) ? 1 : 0;
    }
    SW_EXPECT_EQUAL( enabledCount, 2498 );   // 결정적 해시 — 비율 2500 근처, 원격 설정 플래그와 같은 함수
    SW_EXPECT_EQUAL( differentCount, 3734 ); // 같은 비율이어도 이벤트마다 다른 계정 집합(해시 이름이 이벤트 id)
    SW_EXPECT_EQUAL( LiveEventRules::isAudienceMatch( definition, 77, "kr", 200 ), LiveEventRules::isAudienceMatch( definition, 77, "jp", 200 ) );
    definition._rolloutBasisPoints = 10000;
    definition._listRegion.clear();
    SW_EXPECT_TRUE( LiveEventRules::isAudienceMatch( definition, 1, "eu", 120 ) );
}
