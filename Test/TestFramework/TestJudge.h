/**
 * @file Test/TestFramework/TestJudge.h
 * @brief 케이스 판정기 — 본문의 단언 말고도 케이스를 지게 하는 규칙(모든 케이스의 예상 밖 Error · Warning, 시간 예산)을 한 창구로 둔다
 * @details 판정기는 케이스 본문 앞(`beginCase`)과 정리 뒤 · 결과를 정하기 전(`endCase`)에 불린다. `endCase` 에서 `TestRegistry::addFailure` 를 부르면
 *          그 케이스가 진다. 다음 단계의 판정(GPU 메모리 누수, 프레임 예산)은 판정기 하나를 더 붙인다(`TestRegistry::registerJudge`) — 실행기를 고치지 않는다.
 */
#pragma once
#include "Engine/Automation/AutomationLogPolicy.h"

#include "TestFramework/TestCase.h"

namespace test
{
    struct TestRunOptions;

    class TestCatalog;
    class TestRegistry;
} // namespace test

namespace test
{
    /** @brief 케이스 판정기 하나. 한 실행에서 케이스마다 `beginCase` → (본문 · 정리) → `endCase` 순서로 한 번씩 불린다. */
    class ITestCaseJudge
    {
    public:
        virtual ~ITestCaseJudge() = default;

        virtual void beginCase( const TestCaseInfo& testInfo ) = 0;
        /** @param elapsedMs 본문과 정리에 든 시간. 여기서 `registry.addFailure` 하면 그 케이스가 진다. */
        virtual void endCase( const TestCaseInfo& testInfo, float64 elapsedMs, TestRegistry& registry ) = 0;
    };
} // namespace test

namespace test
{
    /**
     * @brief 케이스 하나 동안 남은 처리하지 않은 Error · Warning 로그를 모아, 하나라도 있으면 그 케이스를 지게 합니다 - 스위트 종류와 상관없이 모든 케이스를 판정한다.
     * @details 테스트는 오류 로그를 남기고도 단언은 통과할 수 있다(GPU 테스트의 검증 레이어 · 드라이버 오류, 깨진 자료를 읽는 CPU 테스트의 거절 로그). 의도된 줄은 `SW_TEST_DEFENSIVE_SCOPE`
     *          안에서 남기면 표식이 붙어 세지 않고, `SW_TEST_SUPPRESS_LOGS` 안의 로그는 아예 남지 않는다. 실패를 기록하며 남기는 `[FAILED]` 줄은 세지 않는다.
     *          나머지 줄은 시나리오 실행기와 같은 허용 목록(`AutomationLogPolicy` — `Resource/engine/automation/allowedlogs.json`)의 `suite` 항목으로만 가릴 수 있고,
     *          로그 id(`SW_LOG_*_ID`)가 없는 줄은 가릴 수 없다. 목록은 케이스가 끝난 뒤 처음 필요할 때 한 번 읽는다 — 읽지 못하면 그 이유가 실패 메시지에 든다.
     */
    class CaseLogJudge final : public ITestCaseJudge
    {
    public:
        CaseLogJudge();
        ~CaseLogJudge() override;

        CaseLogJudge( const CaseLogJudge& )            = delete;
        CaseLogJudge& operator=( const CaseLogJudge& ) = delete;

        void beginCase( const TestCaseInfo& testInfo ) override;
        void endCase( const TestCaseInfo& testInfo, float64 elapsedMs, TestRegistry& registry ) override;

        /** @brief 허용 목록과 범위를 들고 있는 정책입니다. 틀 테스트는 여기에 `parseAllowList` 로 목록을 직접 넣어 파일을 읽지 않게 합니다. */
        sw::AutomationLogPolicy& getPolicy() { return _policy; }
        /** @brief 이번 실행에서 허용 항목이 맞은 줄 수를 `스위트 id (n)` 줄로 찍습니다. 맞은 것이 없으면 아무것도 찍지 않습니다. */
        void printAllowedSummary() const;

        /**
         * @brief 지금 도는 케이스에 Error · Warning 줄 하나를 **값으로** 넣습니다 — 로그 리스너가 모으는 것과 같은 길을 지난다(같은 거름 · 같은 판정).
         * @details 판정기를 시험하는 테스트는 실제 로그를 내지 않고 이 함수로 판정 입력을 준다. 실제 로그를 내면 그 줄이 바깥 실행의 출력에 처리하지 않은 Error 로 남는다.
         *          도는 케이스가 없으면 아무것도 하지 않는다.
         */
        void recordLine( sw::LogLevel level, sw::string_view caller, sw::string_view id, sw::string_view message );

        /**
         * @brief 이 글 조각이 든 줄을 판정기가 모으지 않게 합니다(`ScopedJudgeIgnoredLog` 가 짝(`removeIgnoredNeedle`)으로 부른다).
         * @details 줄은 로그 싱크에 그대로 남아 다른 청취자(시나리오 실행기의 로그 게이트)는 표식 없이 본다 — `SW_TEST_DEFENSIVE_SCOPE` 의 표식은 실행기도 건너뛰게 하므로
         *          실행기의 게이트를 시험하는 테스트는 쓸 수 없다. 같은 글 조각을 겹쳐 더하면 더한 수만큼 빼야 풀린다.
         */
        void addIgnoredNeedle( sw::string_view needle );
        void removeIgnoredNeedle( sw::string_view needle );

    private:
        struct State;

        /** @brief 허용 목록을 아직 읽지 않았으면 한 번 읽습니다. 읽지 못하면 이유를 `_loadError` 에 남기고 목록 없이 판정합니다(어떤 줄도 가려지지 않는다). */
        void ensureAllowListLoaded();

        sw::unique_ptr<State>   _pState;
        sw::AutomationLogPolicy _policy;
        // (케이스가 도는지는 State::_bActive 가 든다 — 리스너 구독이 비어 있어도(로그를 막는 구간) 값 입력은 판정한다)
        sw::string _loadError;
        bool       _bLoadAttempted;
    };
} // namespace test

namespace test
{
    /**
     * @brief `SW_TEST_TIME_BUDGET` 을 넘은 케이스를 기록하고, `--enforce_budgets` 면 그 케이스를 지게 합니다.
     * @details 넘은 케이스는 실행 요약의 `Over time budget` 에 늘 찍힌다. 강제하지 않는 실행에서 지지 않는 것은 부하가 겹친 기계의 숫자가 흔들리기
     *          때문이다(시간 초과는 멈춤을 잡는 그물이지 느려짐 판정이 아니다 — `Test/TestPolicy.json`).
     */
    class TimeBudgetJudge final : public ITestCaseJudge
    {
    public:
        TimeBudgetJudge( const TestCatalog& catalog, const TestRunOptions& options );

        void beginCase( const TestCaseInfo& ) override {}
        void endCase( const TestCaseInfo& testInfo, float64 elapsedMs, TestRegistry& registry ) override;

        /** @brief 이번 실행에서 넘은 케이스 — `Suite.Case (x ms > y ms)`. */
        const sw::vector<sw::string>& getListOverBudget() const { return _listOverBudget; }
        void                          clear() { _listOverBudget.clear(); }

    private:
        const TestCatalog*     _pCatalog;
        const TestRunOptions*  _pOptions;
        sw::vector<sw::string> _listOverBudget;
    };
} // namespace test
