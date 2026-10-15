/**
 * @file Test/TestFramework/TestRegistry.h
 * @brief 프로세스 테스트 레지스트리 — 등록 표 · 실행 옵션 · 지금 케이스의 상태를 들고, 고르고(`TestSelection.cpp`) 돌린다(`TestRunner.cpp`)
 * @details 실패 기록 · 건너뜀 · 추적은 단언과 케이스가 이 레지스트리를 거쳐 남긴다. 출력은 리스너(`TestReport.h`), 단언 밖의 판정은
 *          판정기(`TestJudge.h`)가 맡는다. 틀 자체를 테스트하는 케이스는 지역 레지스트리를 만들어 쓴다(결과 줄 머리가 `(nested) `).
 */
#pragma once
#include "TestFramework/TestCatalog.h"
#include "TestFramework/TestContext.h"
#include "TestFramework/TestEnvironment.h"
#include "TestFramework/TestJudge.h"
#include "TestFramework/TestReport.h"
#include "TestFramework/TestRunOptions.h"

namespace test
{
    class TestRegistry
    {
    public:
        TestRegistry();
        ~TestRegistry();

        TestRegistry( const TestRegistry& )            = delete;
        TestRegistry& operator=( const TestRegistry& ) = delete;

        /** @brief 프로세스 전역 레지스트리를 반환합니다. */
        static TestRegistry& getInstance();

        /** @brief 테스트 전용 인자를 파싱하고 나머지 인자(엔진 명령줄)를 반환합니다. */
        sw::vector<utf8*> configureFromArgs( int32 argc, utf8* argv[] );

        /** @brief 고른 케이스를 모두 실행하고 종료 코드(0 = 통과)를 반환합니다. */
        int32 runAllTests();
        /** @brief 이번 실행이 고르는 케이스 이름과 선언된 호스트 스위트를 나열합니다. */
        void listTests() const;

        /** @brief 이번 실행이 이 케이스를 고르는지 — 이름 필터 · 환경 · 스위트 종류 모드를 함께 봅니다. */
        bool isSelected( const TestCaseInfo& testInfo ) const;
        /**
         * @brief 이 실행이 맡은 케이스 — 고른 케이스(`isSelected`) 중 이 샤드의 것을 등록 순서로.
         * @details `--test_shard=INDEX/COUNT`(또는 gtest 의 `GTEST_SHARD_INDEX` · `GTEST_TOTAL_SHARDS`)이면 **비용으로 고르게** 나눈다 —
         *          비용은 스위트의 시간 예산(`SW_TEST_TIME_BUDGET`, 없으면 기본 비용)이고, 비싼 케이스부터 지금 가장 가벼운 샤드에 놓는다(LPT).
         *          비용이 같으면 스위트 안에서 번갈아 놓이므로 느린 스위트 하나가 한 샤드에 몰리지 않는다. 모든 샤드가 같은 표로 같은 답을 낸다.
         */
        sw::vector<const TestCaseInfo*> selectCasesForThisShard() const;
        /**
         * @brief 이번 실행의 케이스 순서 — 고른 케이스를 등록 순서로, `--test_shuffle` 이면 섞어서.
         * @details 섞을 때는 gtest 처럼 **스위트 순서를 섞고 스위트 안의 케이스를 섞는다**(스위트는 붙어 있다). 회차마다 씨앗에 회차를
         *          더한다 — 같은 `--test_shuffle=<씨앗>` 이면 같은 순서가 다시 나온다.
         */
        sw::vector<const TestCaseInfo*> makeRunOrder( uint32 iteration ) const;

        /** @brief 현재 테스트의 실패를 기록합니다(이 스레드의 추적 `ScopedTrace` 를 함께 남긴다). */
        void addFailure( const sw::string& condition, const sw::string& file, int32 line, const sw::string& message = "" );
        /** @brief 현재 테스트를 실패 없이 건너뜁니다(환경 탓 — "아무것도 검증하지 않은 스위트" 판정에 든다). */
        void skipCurrentTest( const sw::string& reason, const sw::string& file, int32 line );
        /** @brief @p feature 가 이 빌드에 없으면 현재 테스트를 건너뛰고 true 를 돌려줍니다(`SW_TEST_REQUIRES_FEATURE`). */
        bool skipIfFeatureMissing( BuildFeature feature, const sw::string& file, int32 line );
        /** @brief 이 실행의 씨앗 — 읽은 케이스가 지면 재현 명령에 `--test_seed=` 가 붙는다. */
        uint32 getRandomSeed();

        /** @brief 보고 리스너를 더합니다(기본 콘솔 보고기 뒤에 불린다). 수명은 부른 쪽이 레지스트리보다 길게 쥔다. */
        void registerListener( ITestRunListener* pListener );
        void unregisterListener( ITestRunListener* pListener );
        /** @brief 케이스 판정기를 더합니다(기본 판정기 뒤에 불린다). 수명은 부른 쪽이 쥔다. */
        void registerJudge( ITestCaseJudge* pJudge );
        void unregisterJudge( ITestCaseJudge* pJudge );

        TestCatalog&          getCatalog() { return _catalog; }
        const TestCatalog&    getCatalog() const { return _catalog; }
        TestRunOptions&       getOptions() { return _options; }
        const TestRunOptions& getOptions() const { return _options; }
        /** @brief 명령줄에 `--help` · `-h` · `-?` · `/?` · `--gtest_help` 가 있었는가 — 있으면 하네스는 엔진을 세우지 않고 도움말만 찍고 끝난다. */
        bool isHelpRequested() const { return _options._bHelpRequested; }

        /** @brief 현재 테스트의 상세 실행 컨텍스트를 반환합니다. */
        TestContext*       getCurrentContext() { return &_currentContext; }
        const TestContext* getCurrentContext() const { return &_currentContext; }

        /** @brief 실패를 현재 케이스 대신 이 목록에 모읍니다(널이면 되돌림). `ScopedFailureCapture` 만 부릅니다. */
        void                     setFailureCapture( sw::vector<TestFailure>* pCapture ) { _pFailureCapture = pCapture; }
        sw::vector<TestFailure>* getFailureCapture() const { return _pFailureCapture; }

    private:
        /**
         * @param pLinePrefix 결과 줄(`[ RUN ]` · `[FAILED]` · 실패 요약)의 머리. 프로세스 레지스트리(`getInstance`)는 빈 문자열, 테스트 안에서 만든
         *                    레지스트리(기본 생성자)는 `(nested) ` 다. CI 실패 보고(`Scripts/dev/CiFailureReport.py`)와 에디터 테스트 출력 해석은 줄
         *                    머리로 실패를 고른다 — 틀 테스트가 일부러 지게 만든 안쪽 케이스가 바깥 실행의 실패로 보이지 않게 한다.
         */
        explicit TestRegistry( const utf8* pLinePrefix );

        /** @brief 케이스 하나를 돌리고(판정 · 정리 · 임시 폴더 지우기까지) 기록을 돌려줍니다. */
        CaseRecord runCase( const TestCaseInfo& testInfo, uint32 iteration );
        /** @brief 고른 스위트 가운데 케이스가 전부 환경 탓으로 건너뛴 것(샤드에 갈린 스위트는 빼고)을 셉니다. */
        sw::vector<sw::string> collectEmptySuites( const sw::vector<CaseRecord>& listRecord ) const;

        TestCatalog                   _catalog;
        TestRunOptions                _options;
        TestContext                   _currentContext;
        TestEnvironment               _environment;
        ConsoleTestReporter           _consoleReporter;
        HostLogJudge                  _hostLogJudge;
        TimeBudgetJudge               _timeBudgetJudge;
        sw::vector<ITestRunListener*> _listListener; ///< 콘솔 보고기가 맨 앞
        sw::vector<ITestCaseJudge*>   _listJudge;    ///< 기본 판정기 둘이 맨 앞
        sw::vector<TestFailure>*      _pFailureCapture;
        const utf8*                   _pLinePrefix;
    };
} // namespace test

namespace test
{
    /** @brief 이 실행의 씨앗입니다(`--test_seed=`, 없으면 실행마다 고른 값). 무작위 입력을 만드는 케이스가 쓴다 — 그 케이스가 지면 재현 명령에 씨앗이 붙는다. */
    uint32 getRandomSeed();
    /** @brief 이 스레드가 지금 실패를 기록하는 중인가 — 그때 남는 `[FAILED]` Error 줄은 판정기가 세지 않는다. */
    bool isReportingFailureOnThisThread();
} // namespace test

/** @brief 현재 테스트를 사유와 함께 건너뜁니다 — 환경 탓(파일 · 장치 · 서버가 없음)일 때. 스위트가 전부 이것으로 건너뛰면 "아무것도 검증하지 않은 스위트" 로 진다. */
#define SW_TEST_SKIP( reason )                                                               \
    do                                                                                       \
    {                                                                                        \
        test::TestRegistry::getInstance().skipCurrentTest( ( reason ), __FILE__, __LINE__ ); \
        return;                                                                              \
    } while ( 0 )

/**
 * @brief 이 빌드에 구성 기능(@p feature — `test::BuildFeature` 의 이름)이 없으면 현재 테스트를 건너뜁니다 — "이 구성에 없다" 를 데이터로 선언한다.
 * @details 실행 요약이 기능별로 세어 찍고, 스위트가 전부 이것으로 건너뛰어도 실패가 아니다(구성 차이는 의도다). 기능이 있는 구성의 본문이 쓰는
 *          심볼이 없는 구성에서는 본문을 `#if` 로 감싼다 — 등록은 그대로 두어 `--test_list` 와 요약에 남게 한다.
 */
#define SW_TEST_REQUIRES_FEATURE( feature )                                                                              \
    do                                                                                                                   \
    {                                                                                                                    \
        if ( test::TestRegistry::getInstance().skipIfFeatureMissing( test::BuildFeature::feature, __FILE__, __LINE__ ) ) \
            return;                                                                                                      \
    } while ( 0 )

/** @brief 현재 테스트 종료 후 역순으로 실행할 정리 함수를 등록합니다. */
#define SW_TEST_DEFER_CLEANUP( cleanup ) test::TestRegistry::getInstance().getCurrentContext()->deferCleanup( cleanup )
