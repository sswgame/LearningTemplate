#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Client/Automation/Runner/AutomationEvaluator.h"
#include "DevTools/Client/Automation/Runner/AutomationLogPolicy.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"

using namespace sw;

namespace
{
    struct AutomationLogPolicyTestUtil
    {
        /** @brief 에디터 콘솔이 묻는 것과 같은 질의 — 경고 줄 하나가 시나리오 선언(`Declared`)인가. */
        static bool isDeclared( const AutomationLogPolicy& policy, string_view id )
        {
            return policy.classify( LogLevel::Warning, id, "some warning" ) == AutomationLogResult::Declared;
        }

        /** @brief 시나리오 글을 읽어 판정에 올립니다. */
        static void prepare( AutomationLogPolicy& policy, string_view xmlText )
        {
            AutomationScenario scenario;
            SW_REQUIRE_TRUE( scenario.parse( xmlText, "test.scenario.xml" ).has_value() );
            policy.prepare( scenario );
        }
    };
} // namespace

// `<ExpectLog id>` 는 선언이고 `count="0"` 은 "없어야 한다" 는 단언이라 선언이 아니다. `<AllowLog>` 는 허용이지 기대가 아니다(콘솔은 경고색 그대로).
SW_TEST_CASE( AutomationLogPolicyTest, ExpectLogDeclaresButZeroCountAndAllowDoNot )
{
    using Util = AutomationLogPolicyTestUtil;
    AutomationLogPolicy policy;
    Util::prepare( policy, "<Scenario name=\"test.log\"><AllowLog id=\"Test.Allowed\" reason=\"test\"/><ExpectLog id=\"Test.Declared\" count=\"1\"/>"
                           "<ExpectLog id=\"Test.Absent\" count=\"0\"/><Pass/></Scenario>" );
    SW_EXPECT_TRUE( Util::isDeclared( policy, "Test.Declared" ) );
    SW_EXPECT_FALSE( Util::isDeclared( policy, "Test.Absent" ) );
    SW_EXPECT_FALSE( Util::isDeclared( policy, "Test.Allowed" ) );
    SW_EXPECT_TRUE( policy.classify( LogLevel::Warning, "Test.Allowed", "some warning" ) == AutomationLogResult::Allowed );
    SW_EXPECT_TRUE( policy.classify( LogLevel::Info, "Test.Declared", "some info" ) == AutomationLogResult::NotChecked ); // 게이트가 보지 않는 수준은 흐리게 할 것도 없다
    SW_EXPECT_FALSE( Util::isDeclared( policy, "" ) );                                                                    // id 없는 줄은 선언할 수 없다
}

// 단계가 바뀌면(다음 시나리오의 `prepare`) 앞 단계의 선언은 거짓이다. 단계의 판정이 사라져도 선언은 지워진다 — 세션이 든 한 벌을 판정이 빌린다.
SW_TEST_CASE( AutomationLogPolicyTest, NextStageAndEndedJudgeDropDeclarations )
{
    using Util                                 = AutomationLogPolicyTestUtil;
    const shared_ptr<AutomationLogPolicy> pLog = make_shared<AutomationLogPolicy>();
    Util::prepare( *pLog, "<Scenario name=\"test.first\"><ExpectLog id=\"Test.First\" count=\"1\"/><Pass/></Scenario>" );
    SW_EXPECT_TRUE( Util::isDeclared( *pLog, "Test.First" ) );

    Util::prepare( *pLog, "<Scenario name=\"test.second\"><ExpectLog id=\"Test.Second\" atLeast=\"1\"/><Pass/></Scenario>" );
    SW_EXPECT_FALSE( Util::isDeclared( *pLog, "Test.First" ) );
    SW_EXPECT_TRUE( Util::isDeclared( *pLog, "Test.Second" ) );

    {
        AutomationEvaluator evaluator{ pLog };
        SW_EXPECT_TRUE( Util::isDeclared( *pLog, "Test.Second" ) );
    }
    SW_EXPECT_FALSE( Util::isDeclared( *pLog, "Test.Second" ) );
}
