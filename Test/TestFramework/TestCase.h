/**
 * @file Test/TestFramework/TestCase.h
 * @brief 테스트 프레임워크의 기본 자료 — 케이스 메타, 결과, 스위트 종류, 구성 기능
 * @details 등록(`TestCatalog`) · 선택(`TestRegistry::isSelected`) · 실행(`TestRegistry::runAllTests`) · 보고(`TestReport`)가 함께 쓰는 타입만 둔다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace test
{
    /** @brief 등록된 테스트 케이스 메타데이터. */
    struct TestCaseInfo
    {
        sw::string           _groupName;
        sw::string           _testName;
        sw::Delegate<void()> _func;

        /** @brief `Suite.Test` 전체 이름을 반환합니다. */
        sw::string fullName() const { return _groupName + "." + _testName; }
    };
} // namespace test

namespace test
{
    /** @brief 케이스 하나를 돌린 결과. */
    enum class CaseResult : uint8
    {
        Passed,
        Failed,
        Skipped,
    };

    /**
     * @brief 스위트에 붙는 종류 표식 — 종류마다 이번 실행에서 넣을지 뺄지를 `SuiteKindMode` 로 정한다.
     * @details `Host` 는 CI 러너가 못 돌리는 것(GPU · 창 · DXC, `SW_TEST_REQUIRES_HOST`), `Load` 는 부하 · 시간 예산 검증(`SW_TEST_LOAD_SUITE`)이다.
     *          `Load` 는 기본으로 빠진다 — 몇 분씩 걸리고 혼자 돌아야 숫자가 뜻이 있다(ctest 라벨 `load`, `<타깃>_Load`).
     */
    enum class SuiteKind : uint8
    {
        Host,
        Load,
        Count,
    };

    /** @brief 표식이 붙은 스위트를 이번 실행에서 어떻게 다루는지(`--host_suites=` · `--load_suites=`). */
    enum class SuiteKindMode : uint8
    {
        All,     /**< 표식과 상관없이 돈다. */
        Exclude, /**< 표식이 붙은 스위트를 뺀다. */
        Only,    /**< 표식이 붙은 스위트만 돈다. */
    };
} // namespace test

namespace test
{
    /**
     * @brief 구성(Debug · Dev · Shipping)마다 있거나 없는 기능 — 케이스가 "이 구성에 없다" 로 건너뛰는 근거를 데이터로 선언한다.
     * @details `SW_TEST_REQUIRES_FEATURE( 기능 )` 로 건너뛴 케이스는 실행 요약에 기능별로 세어지고, 스위트가 전부 이것으로 건너뛰어도
     *          "아무것도 검증하지 않은 스위트" 로 지지 않는다(구성 차이는 의도다). 환경 탓 건너뜀(`SW_TEST_SKIP`)만 그 판정에 든다.
     *          새 기능은 이 열거형과 `TestCatalog.cpp` 의 표 한 줄을 함께 더한다.
     */
    enum class BuildFeature : uint8
    {
        DevelopmentBuild, /**< Shipping 이 아닌 구성(개발 명령 · 핫 리로드 · 에디터 · 메타데이터). */
        DebugAsserts,     /**< `SW_ASSERT` 가 살아 있는 구성(Debug). */
        DebugLog,         /**< `SW_LOG_*` 가 컴파일되는 구성(Debug). */
        MemoryTagScopes,  /**< 메모리 태그 범위(`sw::kMemoryTagScopesEnabled`). */
        SourceResources,  /**< `Resource/` 의 원본 파일(XML · 모든 게임 팩의 데이터 · 셰이더 리플렉션)과 게임 모듈의 컴포넌트를 읽는 구성(Shipping 은 구운 팩의 활성 게임만 본다). */
        Count,
    };

    /** @brief 이 빌드에 @p feature 가 있는가. */
    bool isBuildFeatureAvailable( BuildFeature feature );
    /** @brief 기능의 이름(영문 — 요약 · 건너뜀 사유에 찍힌다). */
    const utf8* getBuildFeatureName( BuildFeature feature );

    /**
     * @brief 케이스가 끝까지 돌면서 일부(시나리오 하나 · 백엔드 하나)만 "이 구성에 기능이 없다" 로 건너뛴 기록(`test::skipPartIfFeatureMissing`).
     * @details 케이스 전체를 건너뛰는 `SW_TEST_REQUIRES_FEATURE` 와 달리 케이스는 통과로 남는다. 조용히 지나가지 않도록 보고기가 항목마다 한 줄과 요약의 기능별 개수를 찍는다.
     */
    struct SkippedPart
    {
        BuildFeature _feature;
        sw::string   _subject; ///< 건너뛴 대상(시나리오 경로 등)과 이유
    };
} // namespace test

namespace test
{
    /**
     * @brief 호스트 스위트에서 아직 고치지 못한 Error · Warning 로그 하나(`SW_TEST_KNOWN_LOG`).
     * @details 그 스위트의 케이스가 남긴 Error · Warning 중 `_id` 을 담은 줄은 예상 밖 줄로 세지 않고 `_hitCount` 에 센다.
     *          실행 요약이 견딘 줄 수를 찍으므로 조용히 묻히지 않는다.
     */
    struct KnownLog
    {
        sw::string _suiteName;
        sw::string _id;
        sw::string _reason;
        uint32     _hitCount{ 0 };
    };
} // namespace test

namespace test
{
    /** @brief 바깥 환경(DB 서버 등)을 요구하는 스위트 선언(`SW_TEST_REQUIRES_ENVIRONMENT`). */
    struct EnvironmentSuite
    {
        sw::string _environmentName;
        sw::string _reason;
    };
} // namespace test

namespace test
{
    /**
     * @brief 스위트의 케이스 하나가 넘지 말아야 할 시간(`SW_TEST_TIME_BUDGET`).
     * @details 시간 초과(ctest `TIMEOUT`)는 멈춤을 잡는 그물이고, 이것은 느려짐을 잡는 판정이다. 부하가 겹친 기계에서는 숫자가 흔들리므로
     *          `--enforce_budgets` 를 준 실행(부하 검증 항목 `<타깃>_Load`)만 넘으면 진다. 그 밖에는 요약에 넘은 케이스를 찍기만 한다.
     */
    struct TimeBudget
    {
        float64    _maxCaseMilliseconds{ 0.0 };
        sw::string _reason;
    };
} // namespace test
