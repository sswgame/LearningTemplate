#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/World/Encounter/StepEncounter.h"

// StepEncounterTest — 걸음 조우 한 벌(`StepEncounterCounter`): 주기 방식의 판정(0 이하 끔 · 1 이상 매 걸음 · 1/rate 내림의 배수), 조우 뒤 유예(난수를 쓰지 않음),
// 확률 방식의 결정성(같은 시드 · 걸음이면 같은 걸음), 세이브 값 되돌리기의 범위 검사. 오버월드와 클래식 JRPG 가 이 셈을 함께 쓴다. nogpu.

using namespace sw;

/** @brief [StepEncounterTest] 주기 판정: 0 이하는 끄고, 1 이상은 매 걸음이며, 그 사이는 1/rate(내림) 걸음마다다 */
SW_TEST_CASE( StepEncounterTest, PeriodicRuleFollowsRate )
{
    SW_EXPECT_FALSE( StepEncounterCounter::isPeriodicEncounter( 0.0f, 4u ) );
    SW_EXPECT_FALSE( StepEncounterCounter::isPeriodicEncounter( -1.0f, 4u ) );
    SW_EXPECT_TRUE( StepEncounterCounter::isPeriodicEncounter( 1.0f, 3u ) );
    SW_EXPECT_TRUE( StepEncounterCounter::isPeriodicEncounter( 2.0f, 7u ) );
    // 0.3 이면 1/0.3 = 3.33 → 3 걸음마다
    SW_EXPECT_FALSE( StepEncounterCounter::isPeriodicEncounter( 0.3f, 1u ) );
    SW_EXPECT_FALSE( StepEncounterCounter::isPeriodicEncounter( 0.3f, 2u ) );
    SW_EXPECT_TRUE( StepEncounterCounter::isPeriodicEncounter( 0.3f, 3u ) );
    SW_EXPECT_TRUE( StepEncounterCounter::isPeriodicEncounter( 0.3f, 6u ) );
    // 0.6 이면 1/0.6 = 1.67 → 주기 1(매 걸음) — 높은 확률이 낮은 빈도가 되지 않는다
    SW_EXPECT_TRUE( StepEncounterCounter::isPeriodicEncounter( 0.6f, 5u ) );
}

/** @brief [StepEncounterTest] 주기 방식의 셈: 걸음을 세어 주기 걸음에서 나고, 조우 뒤 유예 걸음 동안은 나지 않는다 */
SW_TEST_CASE( StepEncounterTest, PeriodWithGraceAfterEncounter )
{
    StepEncounterRule rule;
    rule._mode       = StepEncounterMode::Period;
    rule._rate       = 0.5f; // 2 걸음마다
    rule._graceSteps = 2;

    StepEncounterCounter counter;
    SW_EXPECT_EQUAL( StepEncounterCounter::kNoEncounterYet, counter.getStepsSinceEncounter() ); // 처음은 유예가 없다
    counter.advance();
    SW_EXPECT_FALSE( counter.roll( rule, nullptr ) ); // 1 걸음
    counter.advance();
    SW_REQUIRE_TRUE( counter.roll( rule, nullptr ) ); // 2 걸음
    counter.markEncounter();
    SW_EXPECT_EQUAL( 0, counter.getStepsSinceEncounter() );

    counter.advance(); // 3 걸음 — 주기 아님
    SW_EXPECT_FALSE( counter.roll( rule, nullptr ) );
    counter.advance(); // 4 걸음 — 주기이지만 조우 뒤 2 걸음이라 유예다
    SW_EXPECT_FALSE( counter.roll( rule, nullptr ) );
    counter.advance(); // 5 걸음
    counter.advance(); // 6 걸음 — 유예가 끝났다
    SW_EXPECT_TRUE( counter.roll( rule, nullptr ) );
    SW_EXPECT_EQUAL( 6, counter.getTotalSteps() );

    counter.reset();
    SW_EXPECT_EQUAL( 0, counter.getTotalSteps() );
    SW_EXPECT_EQUAL( StepEncounterCounter::kNoEncounterYet, counter.getStepsSinceEncounter() );
}

/** @brief [StepEncounterTest] 확률 방식: 같은 시드와 걸음이면 같은 걸음에서 나고, 유예 중에는 난수를 쓰지 않는다(뒤 수열이 밀리지 않는다) */
SW_TEST_CASE( StepEncounterTest, ChanceIsDeterministicAndGraceSpendsNoRandom )
{
    StepEncounterRule rule;
    rule._mode       = StepEncounterMode::Chance;
    rule._rate       = 0.25f;
    rule._graceSteps = 3;

    const auto runSteps = []( const StepEncounterRule& stepRule, uint32 seed, vector<int32>& outEncounterSteps )
    {
        GameRandom           random( seed );
        StepEncounterCounter counter;
        for ( int32 step = 1; step <= 200; ++step )
        {
            counter.advance();
            if ( counter.roll( stepRule, &random ) )
            {
                counter.markEncounter();
                outEncounterSteps.push_back( step );
            }
        }
    };
    vector<int32> listFirst;
    vector<int32> listSecond;
    runSteps( rule, 7u, listFirst );
    runSteps( rule, 7u, listSecond );
    SW_REQUIRE_TRUE( listFirst.empty() == false );
    SW_EXPECT_TRUE( listFirst == listSecond );
    for ( size_t index = 1; index < listFirst.size(); ++index )
    {
        SW_EXPECT_TRUE( listFirst[index] - listFirst[index - 1] > rule._graceSteps ); // 유예 걸음에는 나지 않는다
    }

    // 유예 중에는 난수를 쓰지 않는다: 유예 걸음만 굴린 뒤 다음 난수가 손대지 않은 수열의 첫 값과 같다.
    GameRandom           random( 11u );
    GameRandom           untouched( 11u );
    StepEncounterCounter counter;
    counter.markEncounter();
    for ( int32 step = 0; step < rule._graceSteps; ++step )
    {
        counter.advance();
        SW_EXPECT_FALSE( counter.roll( rule, &random ) );
    }
    SW_EXPECT_EQUAL( untouched.nextUint(), random.nextUint() );
}

/** @brief [StepEncounterTest] 세이브 값 되돌리기: 범위 안이면 받고, 음수 · 상한 초과는 바꾸지 않고 거절한다 */
SW_TEST_CASE( StepEncounterTest, RestoreChecksRange )
{
    StepEncounterCounter counter;
    SW_REQUIRE_TRUE( counter.restore( 4, 90 ) );
    SW_EXPECT_EQUAL( 4, counter.getStepsSinceEncounter() );
    SW_EXPECT_EQUAL( 90, counter.getTotalSteps() );
    SW_EXPECT_FALSE( counter.restore( -1, 90 ) );
    SW_EXPECT_FALSE( counter.restore( StepEncounterCounter::kNoEncounterYet + 1, 90 ) );
    SW_EXPECT_FALSE( counter.restore( 4, -1 ) );
    SW_EXPECT_EQUAL( 4, counter.getStepsSinceEncounter() ); // 거절은 셈을 바꾸지 않는다
    SW_EXPECT_EQUAL( 90, counter.getTotalSteps() );
    SW_REQUIRE_TRUE( counter.restore( StepEncounterCounter::kNoEncounterYet, 0 ) ); // "아직 조우 없음" 도 세이브 값이다
}
