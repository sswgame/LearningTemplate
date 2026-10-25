/**
 * @file Test/TestFramework/TestCatalog.h
 * @brief 케이스 등록 — 케이스와 스위트 선언(호스트 · 부하 · 환경 · 시간 예산)을 담는 표와, 그것을 채우는 매크로
 * @details 선언이 곧 분류다. CMake 에는 스위트 이름을 적지 않고, 실행 파일이 이 표를 들고 있다가 `--host_suites=` · `--load_suites=` 로 가른다.
 *          표는 정적 초기화로 채워지고 실행 중에는 읽기만 한다. 호스트 스위트가 아직 못 고친 Error · Warning 은 이 표가 아니라 허용 목록
 *          (`Resource/engine/automation/allowedlogs.json` 의 `suite` 항목)에 적는다.
 */
#pragma once
#include "TestFramework/TestCase.h"

namespace test
{
    class TestCatalog
    {
    public:
        /** @brief 실행 시작에 케이스를 더하는 함수 — `SW_TEST_CASE_GENERATOR` 가 등록합니다. 받은 목록에 `addCase` 로 더합니다. */
        using CaseGenerator = void ( * )( TestCatalog& catalog );

        /** @brief 매개변수 목록을 돌려주는 함수 — 실행 시작에 한 번 불립니다(`SW_TEST_CASE_PER_PARAMETER`). 값은 케이스가 복사해 쥔다(동적으로 만든 이름도 된다). */
        using ParameterListFunc = sw::vector<sw::string> ( * )();
        /** @brief 매개변수 하나로 도는 케이스 본문입니다. */
        using ParameterCaseFunc = void ( * )( const utf8* pParameter );

        /**
         * @brief 스위트 · 이름 · 함수로 케이스를 더합니다. 이름 글은 복사하므로 동적으로 만든 이름도 된다.
         * @param expectedSeconds 예상 비용(초, 0 = 모름). 샤드 배정이 `--test_costs` 에 없는 케이스에 쓴다.
         */
        void addCase( const utf8* pSuiteName, const utf8* pTestName, sw::Delegate<void()> func, float64 expectedSeconds = 0.0, const utf8* pParameter = "" );
        /** @brief 매개변수마다 케이스 하나인 묶음을 더합니다. 케이스 이름은 `<이름>.<매개변수 이름>`(`makeParameterLabel`)이고 `expandCaseGenerators` 가 만든다. */
        void addParameterizedCase( const utf8* pSuiteName, const utf8* pTestName, ParameterListFunc listFunc, ParameterCaseFunc caseFunc, float64 expectedSeconds );
        /** @brief 매개변수의 케이스 이름 조각입니다 — 스위치 `-dx12` 는 `dx12`, 빈 글은 `default`(배포본의 백엔드 스위치 없음), 그 밖은 그대로. */
        static sw::string makeParameterLabel( const utf8* pParameter );
        /** @brief 생성기를 더합니다. 부르는 때는 정적 초기화이고, 생성기는 `expandCaseGenerators` 가 실행 시작에 한 번 부릅니다. */
        void addCaseGenerator( CaseGenerator generator );
        /**
         * @brief 생성기를 불러 케이스를 더합니다. 두 번째부터는 아무것도 하지 않습니다(`runAllTests` 가 맨 먼저 부른다 — `--test_list` 도 같다).
         * @details 시나리오 폴더처럼 엔진이 선 뒤에야 알 수 있는 목록으로 케이스를 만들 자리다. 모든 샤드가 같은 폴더를 보므로 같은 표를 얻는다.
         */
        void expandCaseGenerators();
        /** @brief 스위트에 종류 표식(@p kind)을 붙입니다 — 호스트 자원(`SW_TEST_REQUIRES_HOST`) · 부하 검증(`SW_TEST_LOAD_SUITE`). */
        void addSuiteKind( const utf8* pSuiteName, SuiteKind kind, const utf8* pReason );
        /** @brief 스위트가 바깥 환경을 요구하며 환경 변수 @p pEnvironmentName 이 그 주소를 준다고 선언합니다(`SW_TEST_REQUIRES_ENVIRONMENT`). */
        void addEnvironmentSuite( const utf8* pSuiteName, const utf8* pEnvironmentName, const utf8* pReason );
        /** @brief 스위트의 케이스 하나가 넘지 말아야 할 시간을 선언합니다(`SW_TEST_TIME_BUDGET`). */
        void addTimeBudget( const utf8* pSuiteName, float64 maxCaseMilliseconds, const utf8* pReason );

        const sw::vector<TestCaseInfo>& getListCase() const { return _listCase; }
        /** @brief 종류 @p kind 가 붙은 스위트 → 까닭. */
        const sw::map<sw::string, sw::string>& getSuiteKindReasons( SuiteKind kind ) const { return _arrMapSuiteKindReason[static_cast<size_t>( kind )]; }
        /** @brief 스위트에 종류 @p kind 가 붙었는가. */
        bool                                         isSuiteOfKind( const sw::string& suiteName, SuiteKind kind ) const;
        const sw::map<sw::string, EnvironmentSuite>& getEnvironmentSuites() const { return _mapEnvironmentSuite; }
        /** @brief 이 스위트가 환경 스위트이고 그 변수가 비었는가입니다. */
        bool isEnvironmentMissing( const sw::string& suiteName ) const;
        /** @brief 스위트의 시간 예산(없으면 널)입니다. */
        const TimeBudget* findTimeBudget( const sw::string& suiteName ) const;
        /** @brief 스위트에 케이스가 하나라도 있는가. */
        bool hasSuite( const sw::string& suiteName ) const;

        /**
         * @brief 케이스가 하나도 없는 스위트를 가리키는 선언을 찍고 그 수를 돌려줍니다.
         * @details 대개 스위트 이름을 바꾸고 선언을 놓친 것이다. 호스트 선언이 그러면 이름이 바뀐 스위트가 **CI 로 들어간다** — 조용히 넘기지 않는다.
         */
        int32 countDeclarationMismatch() const;

    private:
        /** @brief `addParameterizedCase` 가 받아 둔 묶음. */
        struct ParameterizedCase
        {
            sw::string        _suiteName;
            sw::string        _testName;
            ParameterListFunc _listFunc;
            ParameterCaseFunc _caseFunc;
            float64           _expectedSeconds;
        };

        sw::vector<TestCaseInfo>              _listCase;
        sw::vector<CaseGenerator>             _listGenerator;
        sw::vector<ParameterizedCase>         _listParameterized;
        bool                                  _bGeneratorsExpanded{ false };
        sw::map<sw::string, sw::string>       _arrMapSuiteKindReason[static_cast<size_t>( SuiteKind::Count )];
        sw::map<sw::string, EnvironmentSuite> _mapEnvironmentSuite;
        sw::map<sw::string, TimeBudget>       _mapTimeBudget;
    };
} // namespace test

namespace test
{
    /** @brief 정적 초기화로 케이스를 프로세스 레지스트리에 붙입니다(`SW_TEST_CASE`). */
    class TestRegistrar
    {
    public:
        TestRegistrar( const utf8* pSuiteName, const utf8* pTestName, sw::Delegate<void()> func );
    };
} // namespace test

namespace test
{
    /** @brief 정적 초기화로 케이스 생성기를 프로세스 레지스트리에 붙입니다(`SW_TEST_CASE_GENERATOR`). */
    class CaseGeneratorRegistrar
    {
    public:
        explicit CaseGeneratorRegistrar( TestCatalog::CaseGenerator generator );
    };
} // namespace test

namespace test
{
    /** @brief 정적 초기화로 매개변수 묶음 케이스를 프로세스 레지스트리에 붙입니다(`SW_TEST_CASE_PER_PARAMETER`). */
    class ParameterizedCaseRegistrar
    {
    public:
        ParameterizedCaseRegistrar( const utf8* pSuiteName, const utf8* pTestName, TestCatalog::ParameterListFunc listFunc, TestCatalog::ParameterCaseFunc caseFunc,
                                    float64 expectedSeconds );
    };
} // namespace test

namespace test
{
    /** @brief 정적 초기화로 스위트 종류 표식을 붙입니다(`SW_TEST_REQUIRES_HOST` · `SW_TEST_LOAD_SUITE`). */
    class SuiteKindRegistrar
    {
    public:
        SuiteKindRegistrar( const utf8* pSuiteName, SuiteKind kind, const utf8* pReason );
    };
} // namespace test

namespace test
{
    /** @brief 정적 초기화로 환경 스위트 선언을 붙입니다(`SW_TEST_REQUIRES_ENVIRONMENT`). */
    class EnvironmentSuiteRegistrar
    {
    public:
        EnvironmentSuiteRegistrar( const utf8* pSuiteName, const utf8* pEnvironmentName, const utf8* pReason );
    };
} // namespace test

namespace test
{
    /** @brief 정적 초기화로 시간 예산 선언을 붙입니다(`SW_TEST_TIME_BUDGET`). */
    class TimeBudgetRegistrar
    {
    public:
        TimeBudgetRegistrar( const utf8* pSuiteName, float64 maxCaseMilliseconds, const utf8* pReason );
    };

    /** @brief 환경 변수 값입니다(없으면 빈 글). 환경 스위트의 케이스가 서버 주소를 읽는다. */
    sw::string getEnvironmentValue( const utf8* pEnvironmentName );

    /**
     * @brief 지금 도는 매개변수 케이스(`SW_TEST_CASE_PER_PARAMETER`)의 매개변수입니다 — 매개변수 케이스가 아니면 빈 글.
     * @details 본문을 고치지 않고도 도우미가 매개변수를 읽게 한다: `RHIBackendSweep` 은 케이스의 백엔드(`-dx12` …)로 도는 범위를 좁힌다.
     */
    const utf8* getCurrentCaseParameter();
} // namespace test

/** @brief 테스트 케이스를 등록하고 함수를 정의합니다. */
#define SW_TEST_CASE( SuiteName, TestName )                                                                                                                              \
    void                       test_##SuiteName##_##TestName();                                                                                                          \
    static test::TestRegistrar registrar_##SuiteName##_##TestName( #SuiteName, #TestName, SW_DELEGATE_FUNCTION( sw::Delegate<void()>, test_##SuiteName##_##TestName ) ); \
    void                       test_##SuiteName##_##TestName()

/**
 * @brief 실행 시작에 케이스를 만드는 함수 @p generatorFunc( `void ( test::TestCatalog& )` )를 등록합니다 — 폴더를 훑어 만든 목록이 케이스가 되게 한다.
 * @param SuiteName 만드는 케이스의 스위트(등록자 이름을 붙이는 데만 쓴다). **이 파일에 사는 스위트**여야 하고 `SW_TEST_CASE` 가 하나라도 있어야 한다(`CheckTestSuites`).
 * @details 케이스 이름은 `catalog.addCase( 스위트, 이름, 함수 )` 로 더하고, 이름이 `--test_filter` · `--test_list` · 샤드 비용표에 그대로 쓰인다. 생성기는 엔진이 선 뒤
 *          (`runAllTests` 시작)에 불리므로 리소스 폴더를 읽어도 된다. 함수는 일반 함수나 캡처 없는 람다다(함수 포인터). 목록이 비면 케이스도 없으니, 비는 것이 오류이면 따로 단언하는 케이스를 둔다.
 */
#define SW_TEST_CASE_GENERATOR( SuiteName, generatorFunc ) static test::CaseGeneratorRegistrar generatorRegistrar_##SuiteName( generatorFunc )

/**
 * @brief 매개변수(보통 백엔드 스위치) 하나마다 케이스 하나로 도는 케이스를 정의합니다 — 케이스 이름은 `Suite.<TestName>.<매개변수 이름>`(`dx12` · `vk` …).
 * @param parameterListFunc `sw::vector<sw::string> ()` — 매개변수 목록(백엔드 스위치 · 게임 이름 …). 실행 시작에 한 번 불린다(엔진이 선 뒤).
 * @param expectedSeconds   케이스 하나의 예상 비용(초, 0 = 모름) — 샤드 배정이 `--test_costs` 에 없는 케이스에 쓴다. 모아 도는 한 케이스의 옛 비용을 매개변수 수로 나눈다.
 * @details 본문은 `void ( const utf8* pParameter )` 다. 한 판이 못 돌면(`SW_TEST_SKIP`) 그 매개변수의 케이스만 건너뛴다. 하나가 져도 다른 매개변수의 케이스는 돈다.
 *          `--test_filter=Suite.<TestName>.*`(정책의 `raceCases` 도 같은 모양)으로 묶음을, `Suite.<TestName>.gl` 로 하나를 고른다. 이름은 `CheckTestSuites` 가 `Suite.TestName` 으로 읽는다.
 *          인자에 중괄호를 쓰지 않는다(린트가 본문 시작을 중괄호로 찾는다).
 */
#define SW_TEST_CASE_PER_PARAMETER( SuiteName, TestName, parameterListFunc, expectedSeconds )                                                                               \
    void                                    test_##SuiteName##_##TestName( const utf8* pParameter );                                                                        \
    static test::ParameterizedCaseRegistrar registrar_##SuiteName##_##TestName( #SuiteName, #TestName, parameterListFunc, test_##SuiteName##_##TestName, expectedSeconds ); \
    void                                    test_##SuiteName##_##TestName( [[maybe_unused]] const utf8* pParameter )

/**
 * @brief 이 스위트는 CI 러너가 돌릴 수 없다고 선언합니다 — GPU · 디스플레이 · DXC 처럼 호스트에만 있는 것이 필요할 때.
 * @param SuiteName 그 스위트. **이 파일에 사는 스위트**여야 하고, 이 파일에는 다른 스위트를 두지 않는다(`CheckTestSuites`).
 * @param reason    왜 CI 가 못 돌리는지(영문 — `--test_list` 와 실행 요약에 찍힌다).
 * @details 선언이 곧 분류다. `Test/TestPolicy.json` 에서 `hostSplit` 인 실행 파일은 `<타깃>_NoGPU`(`--host_suites=exclude`,
 *          라벨 `nogpu`)와 `<타깃>_HostOnly`(`--host_suites=only`, 라벨 `hostgpu`)로 등록되므로 새 호스트 스위트는
 *          **이 한 줄**로 CI 에서 빠지고 호스트 실행에 들어간다. CMake 에는 스위트 이름을 적지 않는다.
 */
#define SW_TEST_REQUIRES_HOST( SuiteName, reason ) static test::SuiteKindRegistrar hostSuite_##SuiteName( #SuiteName, test::SuiteKind::Host, reason )

/**
 * @brief 이 스위트는 부하 · 시간 예산 검증이라고 선언합니다 — 기본 실행에서 빠지고 `--load_suites=only` 실행(`<타깃>_Load`, 라벨 `load`)에서만 돈다.
 * @details 그 실행은 `--enforce_budgets` 를 받아 `SW_TEST_TIME_BUDGET` 을 넘은 케이스를 실패로 친다. 실행 파일이 `Test/TestPolicy.json` 에서
 *          `loadSuites` 여야 ctest 항목이 생긴다. 이 파일에는 다른 스위트를 두지 않는다(`CheckTestSuites`).
 */
#define SW_TEST_LOAD_SUITE( SuiteName, reason ) static test::SuiteKindRegistrar loadSuite_##SuiteName( #SuiteName, test::SuiteKind::Load, reason )

/**
 * @brief 스위트가 바깥 환경(DB · 캐시 서버)을 요구하며 환경 변수 @p envName 이 그 주소를 준다고 선언합니다.
 * @details 변수가 비면 그 스위트는 고르지 않고 `[ SKIP SUITE ] <스위트> - <envName> is not set (<reason>)` 한 줄만 찍는다 — 실패가 아니고,
 *          "모두 건너뜀" 판정에도 들지 않는다. 변수가 있으면 보통 스위트처럼 돈다(CI 는 서버를 띄운 잡에서만 변수를 준다). 호스트 스위트가 아니다 —
 *          `--host_suites` 와 섞이지 않는다. 케이스는 `test::getEnvironmentValue( envName )` 로 주소를 읽는다. 이 파일에는 다른 스위트를 두지 않는다(`CheckTestSuites`).
 */
#define SW_TEST_REQUIRES_ENVIRONMENT( SuiteName, envName, reason ) static test::EnvironmentSuiteRegistrar environmentSuite_##SuiteName( #SuiteName, envName, reason )

/**
 * @brief 스위트의 케이스 하나가 @p milliseconds 를 넘지 말아야 한다고 선언합니다(시간 예산 — `TimeBudget`).
 * @details `--enforce_budgets` 실행에서만 넘으면 진다. 그 밖의 실행은 요약에 넘은 케이스만 찍는다(부하가 겹친 기계의 숫자로 지지 않게).
 */
#define SW_TEST_TIME_BUDGET( SuiteName, milliseconds, reason ) static test::TimeBudgetRegistrar timeBudget_##SuiteName( #SuiteName, milliseconds, reason )
