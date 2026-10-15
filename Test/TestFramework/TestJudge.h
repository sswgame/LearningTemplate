/**
 * @file Test/TestFramework/TestJudge.h
 * @brief 케이스 판정기 — 본문의 단언 말고도 케이스를 지게 하는 규칙(호스트 스위트의 예상 밖 Error, 시간 예산)을 한 창구로 둔다
 * @details 판정기는 케이스 본문 앞(`beginCase`)과 정리 뒤 · 결과를 정하기 전(`endCase`)에 불린다. `endCase` 에서 `TestRegistry::addFailure` 를 부르면
 *          그 케이스가 진다. 다음 단계의 판정(GPU 메모리 누수, 프레임 예산)은 판정기 하나를 더 붙인다(`TestRegistry::registerJudge`) — 실행기를 고치지 않는다.
 */
#pragma once
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
     * @brief 호스트 스위트 케이스 하나 동안 남은 예상 밖 Error 로그를 세어, 하나라도 있으면 그 케이스를 지게 합니다.
     * @details GPU 테스트는 검증 레이어 · 드라이버 오류를 Error 로그로만 남기고 단언은 통과할 수 있다. 의도된 Error 는 `SW_TEST_DEFENSIVE_SCOPE`
     *          안에서 남기면 표식이 붙어 세지 않고, `SW_TEST_SUPPRESS_LOGS` 안의 로그는 아예 남지 않는다. 그 스위트에 선언된 알려진 Error
     *          (`SW_TEST_KNOWN_ERROR_LOG`)는 세지 않고 그 선언의 `_hitCount` 에 센다. 실패를 기록하며 남기는 `[FAILED]` 줄은 세지 않는다.
     */
    class HostErrorLogJudge final : public ITestCaseJudge
    {
    public:
        explicit HostErrorLogJudge( TestCatalog& catalog );
        ~HostErrorLogJudge() override;

        HostErrorLogJudge( const HostErrorLogJudge& )            = delete;
        HostErrorLogJudge& operator=( const HostErrorLogJudge& ) = delete;

        void beginCase( const TestCaseInfo& testInfo ) override;
        void endCase( const TestCaseInfo& testInfo, float64 elapsedMs, TestRegistry& registry ) override;

    private:
        struct State;

        TestCatalog*          _pCatalog;
        sw::unique_ptr<State> _pState;
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
