/**
 * @file Test/TestFramework/TestCatalog.h
 * @brief 케이스 등록 — 케이스와 스위트 선언(호스트 · 부하 · 환경 · 알려진 Error · 시간 예산)을 담는 표와, 그것을 채우는 매크로
 * @details 선언이 곧 분류다. CMake 에는 스위트 이름을 적지 않고, 실행 파일이 이 표를 들고 있다가 `--host_suites=` · `--load_suites=` 로 가른다.
 *          표는 정적 초기화로 채워지고 실행 중에는 읽기만 한다(알려진 Error 의 `_hitCount` 만 센다).
 */
#pragma once
#include "TestFramework/TestCase.h"

namespace test
{
    class TestCatalog
    {
    public:
        /** @brief 스위트 · 이름 · 함수로 케이스를 더합니다. */
        void addCase( const utf8* pSuiteName, const utf8* pTestName, sw::Delegate<void()> func );
        /** @brief 스위트에 종류 표식(@p kind)을 붙입니다 — 호스트 자원(`SW_TEST_REQUIRES_HOST`) · 부하 검증(`SW_TEST_LOAD_SUITE`). */
        void addSuiteKind( const utf8* pSuiteName, SuiteKind kind, const utf8* pReason );
        /** @brief 스위트가 바깥 환경을 요구하며 환경 변수 @p pEnvironmentName 이 그 주소를 준다고 선언합니다(`SW_TEST_REQUIRES_ENVIRONMENT`). */
        void addEnvironmentSuite( const utf8* pSuiteName, const utf8* pEnvironmentName, const utf8* pReason );
        /** @brief 호스트 스위트의 케이스가 남겨도 실패로 치지 않을 Error 로그(부분 문자열)를 더합니다(`SW_TEST_KNOWN_ERROR_LOG`). */
        void addKnownErrorLog( const utf8* pSuiteName, const utf8* pSubstring, const utf8* pReason );
        /** @brief 스위트의 케이스 하나가 넘지 말아야 할 시간을 선언합니다(`SW_TEST_TIME_BUDGET`). */
        void addTimeBudget( const utf8* pSuiteName, float64 maxCaseMilliseconds, const utf8* pReason );

        const sw::vector<TestCaseInfo>& getListCase() const { return _listCase; }
        /** @brief 종류 @p kind 가 붙은 스위트 → 까닭. */
        const sw::map<sw::string, sw::string>& getSuiteKindReasons( SuiteKind kind ) const { return _arrMapSuiteKindReason[static_cast<size_t>( kind )]; }
        /** @brief 스위트에 종류 @p kind 가 붙었는가. */
        bool                                         isSuiteOfKind( const sw::string& suiteName, SuiteKind kind ) const;
        const sw::map<sw::string, EnvironmentSuite>& getEnvironmentSuites() const { return _mapEnvironmentSuite; }
        /** @brief 이 스위트가 환경 스위트이고 그 변수가 비었는가입니다. */
        bool                             isEnvironmentMissing( const sw::string& suiteName ) const;
        sw::vector<KnownErrorLog>&       getListKnownErrorLog() { return _listKnownErrorLog; }
        const sw::vector<KnownErrorLog>& getListKnownErrorLog() const { return _listKnownErrorLog; }
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
        sw::vector<TestCaseInfo>              _listCase;
        sw::map<sw::string, sw::string>       _arrMapSuiteKindReason[static_cast<size_t>( SuiteKind::Count )];
        sw::map<sw::string, EnvironmentSuite> _mapEnvironmentSuite;
        sw::vector<KnownErrorLog>             _listKnownErrorLog;
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
    /** @brief 정적 초기화로 알려진 Error 로그 선언을 붙입니다(`SW_TEST_KNOWN_ERROR_LOG`). */
    class KnownErrorLogRegistrar
    {
    public:
        KnownErrorLogRegistrar( const utf8* pSuiteName, const utf8* pSubstring, const utf8* pReason );
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
} // namespace test

/** @brief 테스트 케이스를 등록하고 함수를 정의합니다. */
#define SW_TEST_CASE( SuiteName, TestName )                                                                                                                              \
    void                       test_##SuiteName##_##TestName();                                                                                                          \
    static test::TestRegistrar registrar_##SuiteName##_##TestName( #SuiteName, #TestName, SW_DELEGATE_FUNCTION( sw::Delegate<void()>, test_##SuiteName##_##TestName ) ); \
    void                       test_##SuiteName##_##TestName()

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
 * @brief 호스트 스위트가 남기는 Error 로그 가운데 **아직 고치지 못한 것** 하나를 견딥니다.
 * @param SuiteName 그 스위트(`SW_TEST_REQUIRES_HOST` 로 선언한 것).
 * @param substring 그 Error 줄에 든 고정 문구(영문 원문).
 * @param reason    왜 견디는지 · 어디서 고칠지(영문 — 실행 요약에 찍힌다).
 * @details 호스트 스위트의 케이스는 예상 밖 Error 로그 하나로 진다(검증 레이어 · 드라이버 오류가 단언 없이 지나가지 않게).
 *          의도된 Error 는 `SW_TEST_DEFENSIVE_SCOPE` 로 감싸고, 이 선언은 **원인이 테스트 밖(엔진)에 있고 아직 못 고친 것**에만 쓴다.
 *          실행 요약이 견딘 줄 수를 찍고, 한 번도 나오지 않았으면 "지워도 된다" 고 알린다. 고쳤으면 이 줄을 지운다.
 */
#define SW_TEST_KNOWN_ERROR_LOG( SuiteName, substring, reason ) \
    static test::KnownErrorLogRegistrar SW_CONCAT( knownErrorLog_##SuiteName##_, __LINE__ )( #SuiteName, substring, reason )

/**
 * @brief 스위트의 케이스 하나가 @p milliseconds 를 넘지 말아야 한다고 선언합니다(시간 예산 — `TimeBudget`).
 * @details `--enforce_budgets` 실행에서만 넘으면 진다. 그 밖의 실행은 요약에 넘은 케이스만 찍는다(부하가 겹친 기계의 숫자로 지지 않게).
 */
#define SW_TEST_TIME_BUDGET( SuiteName, milliseconds, reason ) static test::TimeBudgetRegistrar timeBudget_##SuiteName( #SuiteName, milliseconds, reason )
