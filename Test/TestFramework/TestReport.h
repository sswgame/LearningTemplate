/**
 * @file Test/TestFramework/TestReport.h
 * @brief 출력 · 보고 — 실행 사건을 받는 창구(`ITestRunListener`)와, 결과 줄 · 요약 · 재현 명령을 찍는 콘솔 보고기
 * @details 결과 줄 머리(`[ RUN      ]` · `[       OK ]` · `[  FAILED  ]` · `[  SKIPPED ]` · `  [FAILED]`)는 계약이다 — 에디터 테스트 출력 해석
 *          (`EditorTestOutputParser`), CI 실패 보고(`Scripts/dev/CiFailureReport.py`), `Scripts/dev/RunTests.py` 가 그 머리로 읽는다.
 *          다른 형식(JUnit XML, 대시보드)이 필요하면 리스너를 하나 더 붙인다(`TestRegistry::registerListener`).
 */
#pragma once
#include "TestFramework/TestCase.h"

namespace test
{
    struct TestRunOptions;

    class TestCatalog;
} // namespace test

namespace test
{
    /** @brief 케이스 한 번(한 회차)의 기록. */
    struct CaseRecord
    {
        const TestCaseInfo*     _pTestInfo{ nullptr };
        sw::vector<sw::string>  _listTrace; ///< 진 단언들이 남긴 추적(`test::ScopedTrace`) — 중복 없이
        float64                 _elapsedMs{ 0.0 };
        uint32                  _iteration{ 0 }; ///< 0 부터
        CaseResult              _result{ CaseResult::Passed };
        BuildFeature            _missingFeature{ BuildFeature::Count }; ///< 구성 기능이 없어 건너뛰었으면 그 기능(아니면 `Count`)
        sw::vector<SkippedPart> _listSkippedPart;                       ///< 케이스는 돌았지만 구성 기능이 없어 건너뛴 일부(`test::skipPartIfFeatureMissing`)
        bool                    _bSeedUsed{ false };                    ///< 케이스가 `test::getRandomSeed()` 를 읽었다
    };
} // namespace test

namespace test
{
    /** @brief 한 실행이 끝난 뒤의 판정 재료 — 보고기가 읽고, 종료 코드는 `isPassed` 가 정한다. */
    struct TestRunSummary
    {
        bool isPassed() const;

        sw::vector<CaseRecord> _listRecord;     ///< 돈 순서대로 — 회차마다 한 줄
        sw::vector<sw::string> _listEmptySuite; ///< 고른 스위트인데 케이스가 전부 환경 탓으로 건너뛴 것
        sw::vector<sw::string> _listOverBudget; ///< 시간 예산을 넘은 케이스(`Suite.Case (x ms > y ms)`)
        float64                _totalMs{ 0.0 };
        uint32                 _runnableCount{ 0 };
        int32                  _passedCount{ 0 };
        int32                  _failedCount{ 0 };
        int32                  _skippedCount{ 0 };
        int32                  _declarationMismatchCount{ 0 };
        bool                   _bEmptySuiteIsFailure{ false };
        bool                   _bKindOnlyRanNothing{ false }; ///< `--host_suites=only` · `--load_suites=only` 인데 고른 것이 없다
        bool                   _bFilterMatchedNothing{ false };
        bool                   _bInvalidArgument{ false };
    };
} // namespace test

namespace test
{
    /**
     * @brief 실행 사건을 받는 창구 — 보고 형식 하나가 리스너 하나다(gtest `TestEventListener` 와 같은 자리).
     * @details 판정은 하지 않는다. 케이스를 지게 하는 것은 판정기(`ITestCaseJudge`)의 일이다. 기본 콘솔 보고기는 늘 붙어 있다.
     */
    class ITestRunListener
    {
    public:
        virtual ~ITestRunListener() = default;

        virtual void onRunStarted( const TestCatalog& catalog, const TestRunOptions& options, uint32 runnableCount )           = 0;
        virtual void onIterationStarted( uint32 iteration, uint32 repeatCount )                                                = 0;
        virtual void onCaseStarted( const TestCaseInfo& testInfo )                                                             = 0;
        virtual void onCaseFinished( const CaseRecord& record )                                                                = 0;
        virtual void onRunFinished( const TestCatalog& catalog, const TestRunOptions& options, const TestRunSummary& summary ) = 0;
    };
} // namespace test

namespace test
{
    /**
     * @brief 표준 출력에 결과 줄 · 요약을 찍는 기본 보고기.
     * @details 요약 끝에는 진 실행을 다시 만드는 명령(`Reproduce:`)을 찍는다 — 실행 파일과 작업 폴더, 진 케이스만 고른 필터, 씨앗 · 샤드 ·
     *          스위트 종류 · 엔진 인자, ctest 항목 이름(`SW_TEST_CTEST_NAME`)과 구성. 되풀이(`--test_repeat`)면 진 케이스마다 재현율을 찍는다.
     */
    class ConsoleTestReporter final : public ITestRunListener
    {
    public:
        /** @param pLinePrefix 결과 줄 머리 앞에 붙일 글 — 틀 테스트 안에서 만든 레지스트리는 `(nested) `(바깥 실행의 실패로 읽히지 않게). */
        explicit ConsoleTestReporter( const utf8* pLinePrefix );

        void onRunStarted( const TestCatalog& catalog, const TestRunOptions& options, uint32 runnableCount ) override;
        void onIterationStarted( uint32 iteration, uint32 repeatCount ) override;
        void onCaseStarted( const TestCaseInfo& testInfo ) override;
        void onCaseFinished( const CaseRecord& record ) override;
        void onRunFinished( const TestCatalog& catalog, const TestRunOptions& options, const TestRunSummary& summary ) override;

    private:
        void printSlowestCases( const TestRunSummary& summary ) const;
        void printFeatureSkips( const TestRunSummary& summary ) const;
        void printFailureRates( const TestRunSummary& summary, uint32 repeatCount ) const;
        void printReproduction( const TestRunOptions& options, const TestRunSummary& summary ) const;

        const utf8* _pLinePrefix;
    };
} // namespace test
