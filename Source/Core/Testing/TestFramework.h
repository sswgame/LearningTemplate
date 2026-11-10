/**
 * @file TestFramework.h
 * @brief 엔진 안 단위 테스트 — 케이스 등록(`SW_TEST_CASE`)과 단언(`SW_EXPECT_*` · `SW_REQUIRE_*` · `SW_EXPECT_SNAPSHOT` · `SW_EXPECT_LOG`)입니다.
 * @details 케이스는 기능 폴더 옆 `Tests/` 에 두고 그 모듈과 함께 짓는다(배포 구성은 `Tests/` 폴더째 짓지 않는다). 실행은 `App --test` 하나가 헤드리스로 선 엔진
 *          위에서 한다 — 엔진 서비스(`engine::getSubsystem`)를 그대로 쓴다(언리얼 Automation Spec). 실행기는 `Core/Testing/TestRunner.h`.
 *          옛 틀의 문제를 형식으로 막는다:
 *          - 단언이 하나도 없는 케이스는 실패다(0 건 통과 금지).
 *          - 케이스 동안 남은 Warning · Error 로그는 실패다. 일부러 내는 로그는 그 로그의 id 로 `SW_EXPECT_LOG` 에 적는다(부분 문자열로 가리지 않는다).
 *          - 실행 순서는 매번 섞인다(씨앗을 찍는다 — `--test-seed` 로 같은 순서를 다시 돈다). 앞 케이스에 기대는 케이스는 드러난다.
 *          - 쓰기는 `makeScratchPath` 의 케이스별 폴더에만 한다(프로세스 · 케이스 · 회차마다 다르다).
 *          - 손으로 옮겨 적는 기대값 대신 스냅숏(`SW_EXPECT_SNAPSHOT`, `--update-snapshots` 로 갱신)을 쓴다.
 *          - 멈춘 케이스는 시한(`TestCaseOptions::_timeoutSeconds`, 기본 `kDefaultCaseTimeoutSeconds`)이 끝내고 이름을 남긴다. 크래시 문맥에도 지금 케이스가 있다.
 *          선언 넷 — 모두 끝에 케이스 옵션(지정 초기화)을 붙일 수 있다(`._tags = "slow"` · `._disabledReason = "…"` · `._timeoutSeconds = 60` · `._bIsolated = true`):
 *          - `SW_TEST_CASE( Suite, Name )` — 함수 하나.
 *          - `SW_TEST_CASE_F( Fixture, Name )` — 케이스마다 `Fixture` 를 새로 만들고(생성자 = 준비, 소멸자 = 정리) 본문은 그 멤버 함수다(GoogleTest TEST_F).
 *          - `SW_TEST_CASE_EACH( Suite, Name, 배열 )` — 배열 원소마다 케이스 `Name/<번호>` 하나, 본문은 `param` 을 받는다(GoogleTest TEST_P · Catch2 GENERATE).
 *            원소 형식에 `const utf8* _pTestName` 칸이 있으면 이름은 `Name/<그 글>` 이다 — 배열 순서를 바꿔도 케이스 이름 · 거르개 · 보고가 같은 입력을 가리킨다(GoogleTest 의 이름 생성기 자리).
 *          - `SW_TEST_CHILD_ROLE( Suite, Name )` — 자식 역할. 평소에는 돌지 않고 부모 케이스가 `runChildRole` 로 자식 프로세스에서 띄운다(크래시 · 교착 · 표준 출력).
 *          `SW_REQUIRE_*` 는 실패하면 케이스 함수에서 `return` 한다 — 케이스 본문에서만 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/formatString.h"

#include <iterator>
#include <type_traits>
#include <utility>

namespace sw::test
{
    /** @brief 케이스 본문 함수입니다. */
    using TestFunction = void ( * )();
    /** @brief 매개변수 케이스(`SW_TEST_CASE_EACH`)의 본문 함수입니다 — 배열의 몇 번째 원소인지 받는다. */
    using TestIndexedFunction = void ( * )( size_t index );

    /** @brief 케이스가 옵션으로 시한을 주지 않았을 때의 시한(초)입니다. 넘기면 실행 전체를 끝낸다(멈춘 케이스는 되살릴 수 없다). */
    inline constexpr float32 kDefaultCaseTimeoutSeconds = 30.0f;
    /** @brief 케이스가 시한을 넘겨 실행을 끝낼 때의 종료 코드입니다(시나리오의 시간 초과와 같은 12). */
    inline constexpr int32 kTestTimeoutExitCode = 12;

    /**
     * @struct TestCaseOptions
     * @brief 케이스 선언 끝에 붙이는 옵션입니다(지정 초기화 — `SW_TEST_CASE( A, B, ._tags = "slow" )`).
     */
    struct TestCaseOptions
    {
        const utf8* _tags{ "" };                ///< 공백으로 가른 태그 — 거르개의 `[태그]` 가 고른다(`--test-filter=[slow]`, 빼기는 `-[slow]`)
        const utf8* _disabledReason{ nullptr }; ///< 주면 돌리지 않는다. 까닭은 보고에 남는다(`--test-run-disabled` 면 돈다) — 지우지 않고 끄는 자리
        float32     _timeoutSeconds{ 0.0f };    ///< 0 이면 `kDefaultCaseTimeoutSeconds`
        bool        _bIsolated{ false };        ///< 자식 프로세스에서 돈다 — 크래시 · 교착 · 강제 종료가 이 케이스의 실패로 남고 나머지 케이스와 보고는 이어진다(엔진 기동 비용이 붙어 필요한 케이스만) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /** @struct TestCaseRegistration @brief 케이스 하나의 등록 줄입니다. 글자는 등록한 이미지(모듈)의 정적 저장소(매개변수 케이스는 그 등록자)에 있습니다. */
    struct TestCaseRegistration
    {
        const utf8*         _pName; ///< `<스위트>.<케이스>`(매개변수 케이스는 `<스위트>.<케이스>/<번호>`)
        TestFunction        _pfnRun;
        const utf8*         _pFile; ///< 케이스를 정의한 파일(스냅숏 폴더가 그 옆 `Snapshots/` 이다)
        int32               _line;
        TestCaseOptions     _options{};
        TestIndexedFunction _pfnRunIndexed{ nullptr }; ///< 매개변수 케이스면 `_pfnRun` 대신 이것을 `_index` 로 부른다
        size_t              _index{ 0 };
        bool                _bChildRole{ false }; ///< 자식 역할(`SW_TEST_CHILD_ROLE`) — 평소 실행 · 목록 · 케이스 수에서 빠지고 `--test-child-role` 로만 돈다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /** @brief `SW_TEST_CASE` 가 두는 정적 등록자입니다 — 만들 때 올리고 없앨 때(모듈을 내릴 때) 뺍니다. */
    struct SW_API TestCaseRegistrar
    {

        const TestCaseRegistration* _pRegistration;
        uint8                       _bRegistered : 1;
        explicit TestCaseRegistrar( const TestCaseRegistration* pRegistration );
        ~TestCaseRegistrar();
        TestCaseRegistrar( const TestCaseRegistrar& )            = delete;
        TestCaseRegistrar& operator=( const TestCaseRegistrar& ) = delete;
    };

    /** @brief 매개변수 케이스 원소의 안정 이름입니다(`_pTestName` 칸). 없으면 nullptr 이고 이름은 번호다. */
    using TestParamNameFunction = const utf8* (*)( size_t index );

    /** @brief 원소 형식 @p TParam 에 `_pTestName` 칸이 있는지입니다. */
    template <typename TParam, typename = void>
    struct HasTestName : std::false_type
    {
    };
    template <typename TParam>
    struct HasTestName<TParam, std::void_t<decltype( std::declval<const TParam&>()._pTestName )>> : std::true_type
    {
    };

    /** @brief 원소 @p param 의 안정 이름입니다(`_pTestName`). 칸이 없는 형식은 nullptr 입니다. */
    template <typename TParam>
    const utf8* findTestParamName( const TParam& param )
    {
        if constexpr ( HasTestName<TParam>::value )
            return param._pTestName;
        else
            return nullptr;
    }

    /**
     * @brief `SW_TEST_CASE_EACH` 가 두는 정적 등록자입니다 — 원소마다 이름(`<기본 이름>/<안정 이름 또는 번호>`)과 등록 줄을 만들어 올린다.
     * @details 안정 이름이 비었거나 겹치면 등록표가 거절하고 알린다(같은 이름의 케이스 둘은 거르개 · 보고가 가르지 못한다).
     */
    struct SW_API TestCaseEachRegistrar
    {
        TestCaseEachRegistrar( const utf8* pBaseName, size_t count, TestIndexedFunction pfnRun, TestParamNameFunction pfnName, const utf8* pFile, int32 line,
                               const TestCaseOptions& options );
        ~TestCaseEachRegistrar();
        TestCaseEachRegistrar( const TestCaseEachRegistrar& )            = delete;
        TestCaseEachRegistrar& operator=( const TestCaseEachRegistrar& ) = delete;

    private:
        vector<string>                        _listName;
        vector<TestCaseRegistration>          _listRegistration; ///< 크기를 처음에 정한다 — 등록표가 원소의 주소를 쥔다
        vector<unique_ptr<TestCaseRegistrar>> _listRegistrar;
    };

    /** @brief 지금 도는 케이스에 실패 하나를 적습니다(`파일(줄): error: …` 로 바로 찍는다 — IDE 가 그 줄로 간다). */
    SW_API void reportFailure( const utf8* pFile, int32 line, string_view message );
    /** @brief 단언 하나를 셉니다(통과든 실패든). 단언이 없는 케이스는 실패다. */
    SW_API void countAssertion();
    /** @brief 이 케이스가 일부러 내는 로그 id 를 적습니다. 그 id 의 Warning · Error 는 실패로 세지 않고, 끝까지 한 번도 안 나오면 실패입니다. */
    SW_API void expectLog( const utf8* pLogID, const utf8* pFile, int32 line );
    /**
     * @brief 지금 도는 케이스가 @p pLogID 를 `SW_EXPECT_LOG` 로 선언했으면 true 입니다. 실행 밖이거나 id 가 없으면 false 입니다.
     * @details 시나리오 안에서 단위 테스트가 돌 때(에디터 `editor.unitTests`) 자동화 로그 게이트가 같은 줄을 두 번 세지 않게 묻는다. 예전에는 로거가 이 질의를 들고 있어
     *          모든 실행(배포본 포함)의 로거에 테스트 개념이 들어 있었다 — 묻는 쪽(테스트 틀 · 자동화)끼리 잇는다.
     */
    [[nodiscard]] SW_API bool isExpectedLog( const utf8* pLogID );
    /**
     * @brief 실행 끝 줄 `[ DECLARED ] <N> error log line(s) …` 의 머리입니다. N 은 실행 동안 케이스가 `SW_EXPECT_LOG` 로 선언해 맞은 Error 줄 수입니다.
     * @details 출력만 보는 쪽(에디터 Test Runner)이 Error 줄 수에서 이것을 빼 "선언 밖 오류" 를 낸다 — 거절 경로 테스트가 일부러 낸 수천 줄이 오류로 보였다.
     *          줄 모양을 바꾸지 않고(로그는 그대로 `[Error]`) 수만 알린다. 표시는 읽는 쪽이 정한다.
     */
    inline constexpr string_view kDeclaredErrorLinesPrefix = "[ DECLARED ] ";
    /**
     * @brief 이 케이스만 쓰는 빈 폴더 아래의 절대 경로입니다(`<작업 폴더>/Saved/Test/<프로세스>/<케이스>/<회차>/<name>`).
     * @details 폴더는 처음 부를 때 만든다. 테스트는 이 밖(Bin · 소스 트리 · 고정 이름 임시 경로)에 쓰지 않는다 — 나란히 도는 프로세스 · 반복 회차가 부딪치지 않게.
     */
    SW_API string makeScratchPath( string_view name );
    /**
     * @brief @p actual 을 케이스 파일 옆 `Snapshots/<케이스>.<name>.txt` 와 비교합니다(줄 끝은 LF 로 맞춘다).
     * @details 스냅숏이 없거나 다르면 실패이고, `--update-snapshots` 로 돌면 파일을 쓰고 통과한다(쓴 것을 보고에 적는다). 손으로 옮겨 적는 기대값을 대신한다.
     */
    SW_API bool checkSnapshot( string_view name, string_view actual, const utf8* pFile, int32 line );

    /** @brief 견주고 찍을 값입니다. 무효 인덱스 표지(`constant::InvalidIndex`)는 그 정수다 — 부호가 다른 정수 규칙(`areValuesEqual`)이 표지에도 선다. */
    template <typename T>
    constexpr const T& toComparable( const T& value )
    {
        return value;
    }
    template <typename T>
    constexpr T toComparable( const constant::InvalidIndex<T>& marker )
    {
        return marker;
    }

    /** @brief 단언 실패 글에 값을 넣습니다. 숫자 · 문자열이면 값을, 그 밖의 타입이면 `(value)` 를 씁니다. */
    template <typename T>
    string describeValue( const T& value )
    {
        using TValue = std::decay_t<T>;
        if constexpr ( std::is_same_v<TValue, bool> )
        {
            return value ? string( "true" ) : string( "false" );
        }
        else if constexpr ( std::is_enum_v<TValue> )
        {
            return describeValue( static_cast<std::underlying_type_t<TValue>>( value ) );
        }
        else if constexpr ( std::is_arithmetic_v<TValue> || std::is_convertible_v<const TValue&, string_view> )
        {
            utf8 arrBuffer[constant::buffer::kMax256];
            formatstring( arrBuffer, constant::buffer::kMax256, "%#", value );
            return string( arrBuffer );
        }
        else
        {
            return string( "(value)" );
        }
    }

    /**
     * @brief 두 값이 같은지 봅니다. 부호가 다른 정수끼리는 값으로 견준다(C++20 `std::cmp_equal` 과 같다) — `SW_TRUE`(int) 와 부호 없는 비트필드처럼 섞인 비교가
     *        `==` 의 부호 변환(-1 == 0xFFFFFFFF)으로 잘못 통과하지 않고 경고도 내지 않는다.
     */
    template <typename TExpected, typename TActual>
    constexpr bool areValuesEqual( const TExpected& expected, const TActual& actual )
    {
        using TLhs = std::decay_t<TExpected>;
        using TRhs = std::decay_t<TActual>;
        if constexpr ( std::is_integral_v<TLhs> && std::is_integral_v<TRhs> && std::is_same_v<TLhs, bool> == false && std::is_same_v<TRhs, bool> == false &&
                       std::is_signed_v<TLhs> != std::is_signed_v<TRhs> )
        {
            if constexpr ( std::is_signed_v<TLhs> )
                return expected >= 0 && static_cast<std::make_unsigned_t<TLhs>>( expected ) == actual;
            else
                return actual >= 0 && expected == static_cast<std::make_unsigned_t<TRhs>>( actual );
        }
        else
        {
            return expected == actual;
        }
    }

    /** @brief `SW_EXPECT_EQUAL` 의 본체입니다. */
    template <typename TExpected, typename TActual>
    bool checkEqual( const TExpected& expected, const TActual& actual, const utf8* pExpression, const utf8* pFile, int32 line )
    {
        countAssertion();
        if ( areValuesEqual( toComparable( expected ), toComparable( actual ) ) )
            return true;
        const string expectedText = describeValue( toComparable( expected ) );
        const string actualText   = describeValue( toComparable( actual ) );
        utf8         arrBuffer[constant::buffer::kMax1024];
        formatstring( arrBuffer, constant::buffer::kMax1024, "%# — expected %#, actual %#", pExpression, expectedText.c_str(), actualText.c_str() );
        reportFailure( pFile, line, arrBuffer );
        return false;
    }

    /** @brief `SW_EXPECT_NOT_EQUAL` 의 본체입니다. */
    template <typename TUnexpected, typename TActual>
    bool checkNotEqual( const TUnexpected& unexpected, const TActual& actual, const utf8* pExpression, const utf8* pFile, int32 line )
    {
        countAssertion();
        if ( ( unexpected == actual ) == false )
            return true;
        const string valueText = describeValue( toComparable( actual ) );
        utf8         arrBuffer[constant::buffer::kMax1024];
        formatstring( arrBuffer, constant::buffer::kMax1024, "%# — both are %#", pExpression, valueText.c_str() );
        reportFailure( pFile, line, arrBuffer );
        return false;
    }

    /** @brief `*_MSG` 단언의 본체입니다 — 실패 글에 식과 함께 @p message 를 붙인다. */
    SW_API bool checkConditionWithMessage( bool bPassed, const utf8* pExpression, string_view message, const utf8* pFile, int32 line );

    /**
     * @brief 자식 역할 하나를 띄운 결과입니다(`runChildRole`).
     */
    struct SW_API ChildRunResult
    {
        int32  _exitCode{ -1 };     ///< 자식의 종료 코드(띄우지 못했으면 -1). 자식 역할이 통과하면 0 이다
        bool   _bLaunched{ false }; ///< 프로세스를 만들었는가 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool   _bTimedOut{ false }; ///< 시한을 넘겨 죽였는가 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        string _output;             ///< 표준 출력과 표준 에러(합쳐서, 줄마다 `\n`)

        /** @brief 실패 글에 붙일 마지막 몇 줄입니다 — 자식이 어디서 멈췄는지가 대개 거기 있다. */
        [[nodiscard]] string getOutputTail( uint32 lineCount = 12 ) const;
    };

    /**
     * @brief 이 실행 파일을 자식 프로세스로 띄워 자식 역할(`SW_TEST_CHILD_ROLE`) @p roleName(`스위트.케이스`) 하나만 돌립니다(`-test -test-child-role=… -unattended`).
     * @details 프로세스를 죽이거나(크래시 보고) 멈출 수 있는 일(교착) · 표준 출력 · 표준 에러 · 프로세스 전역 상태(전역 싱크를 뗀다)를 보는 케이스가 쓴다.
     *          @p argument 는 자식이 `getChildRoleArgument` 로 받는다(환경 변수 대신 — 부모 환경을 건드리지 않고, 명령줄로 자식 하나를 손으로 다시 돌릴 수 있다).
     *          시한을 넘기면 감시 스레드가 자식을 죽이고 `_bTimedOut` 이 선다(실패로 적지는 않는다 — 죽은 자식의 종료 코드는 0 이 아니므로 종료 코드를 보면 걸린다).
     *          자식의 로그 파일은 이 케이스의 쓰기 폴더에 둔다(`Saved/Logs` 에 쌓지 않는다). 자식 App 기동만 Debug 에서 몇 초다 — 부르는 케이스의 `._timeoutSeconds` 를
     *          자식 시한보다 넉넉히 준다. 등록되지 않은 역할 이름 · 띄우지 못한 자식은 실패로 적는다.
     */
    SW_API ChildRunResult runChildRole( string_view roleName, string_view argument, float32 timeoutSeconds );
    /**
     * @brief 자식 역할로 띄워진 이 프로세스가 받은 `--test-child-arg` 의 값입니다(없으면 빈 글).
     * @details 자식 역할 케이스 밖에서 부르면 실패다(평소 실행에서 빈 값으로 이어 가지 않는다).
     */
    SW_API string getChildRoleArgument();
    /** @brief 지금 케이스가 격리 케이스의 자식 프로세스(`._bIsolated` — `--test-isolated-case`)에서 도는지입니다. 격리가 실제로 일어났는지 보는 케이스가 쓴다. */
    SW_API bool isRunningIsolated();

    /** @brief `SW_EXPECT_NEAR` 의 본체입니다. */
    SW_API bool checkNear( float64 expected, float64 actual, float64 tolerance, const utf8* pExpression, const utf8* pFile, int32 line );
    /** @brief `SW_EXPECT_TRUE` · `SW_EXPECT_FALSE` · `SW_REQUIRE_TRUE` 의 본체입니다. */
    SW_API bool checkCondition( bool bPassed, const utf8* pExpression, const utf8* pFile, int32 line );
} // namespace sw::test

#if !defined( SW_SHIPPING )
    /**
     * @brief 케이스 하나를 정의합니다 — `SW_TEST_CASE( Base64UtilTest, RoundTrips ) { … }`(기능 폴더의 `Tests/` 파일, 파일 범위). 이름은 `<스위트>.<케이스>` 입니다.
     * @details 끝에 옵션을 붙일 수 있다: `SW_TEST_CASE( A, B, ._tags = "slow", ._timeoutSeconds = 60 )`.
     */
    #define SW_TEST_CASE( suite, name, ... )                                                                                                   \
        static void                                   sw_testBody_##suite##_##name();                                                          \
        static const ::sw::test::TestCaseRegistration sw_testCase_##suite##_##name{ #suite "." #name, &sw_testBody_##suite##_##name, __FILE__, \
                                                                                    __LINE__, ::sw::test::TestCaseOptions{ __VA_ARGS__ } };    \
        static const ::sw::test::TestCaseRegistrar    sw_testRegistrar_##suite##_##name{ &sw_testCase_##suite##_##name };                      \
        static void                                   sw_testBody_##suite##_##name()

    /**
     * @brief 자식 역할 — `SW_TEST_CHILD_ROLE( LogTest, CrashChild ) { … test::getChildRoleArgument() … }`. 부모 케이스가 `test::runChildRole` 로 띄운다.
     * @details 평소 실행 · 목록 · 케이스 수에서 빠지고 `App --test --test-child-role=<스위트>.<케이스> [--test-child-arg=<글>]` 로만 그 하나가 돈다.
     *          옛 틀은 자식 역할이 보통 케이스라 평소 실행에서 스스로 건너뛰었다 — 지금 틀은 단언 없는 케이스가 실패라 그 모양을 쓸 수 없고, 등록 줄에 표시해 아예 고르지 않는다.
     *          본문은 보통 케이스와 같은 규칙(단언 · 기대 로그)으로 판정되고, 통과하면 자식의 종료 코드가 0 이다.
     */
    #define SW_TEST_CHILD_ROLE( suite, name, ... )                                                                                                            \
        static void                                   sw_testBody_##suite##_##name();                                                                         \
        static const ::sw::test::TestCaseRegistration sw_testCase_##suite##_##name{ #suite "." #name, &sw_testBody_##suite##_##name, __FILE__,                \
                                                                                    __LINE__, ::sw::test::TestCaseOptions{ __VA_ARGS__ }, nullptr, 0, true }; \
        static const ::sw::test::TestCaseRegistrar    sw_testRegistrar_##suite##_##name{ &sw_testCase_##suite##_##name };                                     \
        static void                                   sw_testBody_##suite##_##name()

    /**
     * @brief 픽스처 케이스 — `SW_TEST_CASE_F( MyFixture, Name ) { … }`. 케이스마다 `MyFixture` 를 새로 만들고(생성자가 준비, 소멸자가 정리 — 실패 · `SW_REQUIRE` 로
     *        끝나도 정리된다) 본문은 그 멤버 함수라 픽스처의 멤버를 바로 쓴다. 이름은 `<픽스처>.<케이스>` 입니다.
     */
    #define SW_TEST_CASE_F( fixture, name, ... )                                                                                                     \
        struct sw_testFixture_##fixture##_##name final : fixture                                                                                     \
        {                                                                                                                                            \
            void runBody();                                                                                                                          \
        };                                                                                                                                           \
        static void sw_testBody_##fixture##_##name()                                                                                                 \
        {                                                                                                                                            \
            sw_testFixture_##fixture##_##name instance;                                                                                              \
            instance.runBody();                                                                                                                      \
        }                                                                                                                                            \
        static const ::sw::test::TestCaseRegistration sw_testCase_##fixture##_##name{ #fixture "." #name, &sw_testBody_##fixture##_##name, __FILE__, \
                                                                                      __LINE__, ::sw::test::TestCaseOptions{ __VA_ARGS__ } };        \
        static const ::sw::test::TestCaseRegistrar    sw_testRegistrar_##fixture##_##name{ &sw_testCase_##fixture##_##name };                        \
        void                                          sw_testFixture_##fixture##_##name::runBody()

    /**
     * @brief 매개변수 케이스 — `static constexpr int32 kArrSize[] = { 1, 2, 4 }; SW_TEST_CASE_EACH( Pool, Grows, kArrSize ) { … param … }`.
     * @details 원소마다 케이스 `<스위트>.<케이스>/<번호>` 가 따로 돌고(거르개 · 보고 · 실패가 원소 단위) 본문은 그 원소를 `param` 으로 받는다. 배열은 이 줄보다 앞에 둔다.
     */
    #define SW_TEST_CASE_EACH( suite, name, values, ... )                                                                                         \
        static void sw_testBody_##suite##_##name( const std::remove_cv_t<std::remove_reference_t<decltype( ( values )[0] )>>& param );            \
        static void sw_testEach_##suite##_##name( size_t index )                                                                                  \
        {                                                                                                                                         \
            sw_testBody_##suite##_##name( ( values )[index] );                                                                                    \
        }                                                                                                                                         \
        static const utf8* sw_testEachName_##suite##_##name( size_t index )                                                                       \
        {                                                                                                                                         \
            return ::sw::test::findTestParamName( ( values )[index] );                                                                            \
        }                                                                                                                                         \
        static const ::sw::test::TestCaseEachRegistrar sw_testRegistrar_##suite##_##name{ #suite "." #name, std::size( values ),                  \
                                                                                          &sw_testEach_##suite##_##name,                          \
                                                                                          &sw_testEachName_##suite##_##name, __FILE__,            \
                                                                                          __LINE__, ::sw::test::TestCaseOptions{ __VA_ARGS__ } }; \
        static void                                    sw_testBody_##suite##_##name( const std::remove_cv_t<std::remove_reference_t<decltype( ( values )[0] )>>& param )
#else
    #define SW_TEST_CASE( suite, name, ... )       [[maybe_unused]] static void sw_testBody_##suite##_##name()
    #define SW_TEST_CHILD_ROLE( suite, name, ... ) [[maybe_unused]] static void sw_testBody_##suite##_##name()
    #define SW_TEST_CASE_F( fixture, name, ... )                 \
        struct sw_testFixture_##fixture##_##name final : fixture \
        {                                                        \
            void runBody();                                      \
        };                                                       \
        void sw_testFixture_##fixture##_##name::runBody()
    #define SW_TEST_CASE_EACH( suite, name, values, ... ) \
        [[maybe_unused]] static void sw_testBody_##suite##_##name( const std::remove_cv_t<std::remove_reference_t<decltype( ( values )[0] )>>& param )
#endif

/** @brief 조건이 참이어야 합니다(실패해도 이어 간다). */
#define SW_EXPECT_TRUE( condition ) (void)::sw::test::checkCondition( static_cast<bool>( condition ), #condition, __FILE__, __LINE__ )
/** @brief 조건이 거짓이어야 합니다(실패해도 이어 간다). */
#define SW_EXPECT_FALSE( condition ) (void)::sw::test::checkCondition( !static_cast<bool>( condition ), "!(" #condition ")", __FILE__, __LINE__ )
/** @brief 두 값이 같아야 합니다(`==`, 실패 글에 두 값을 쓴다). */
#define SW_EXPECT_EQUAL( expected, actual ) (void)::sw::test::checkEqual( ( expected ), ( actual ), #expected " == " #actual, __FILE__, __LINE__ )
/** @brief 두 실수의 차이가 허용 오차 이하여야 합니다. */
#define SW_EXPECT_NEAR( expected, actual, tolerance )                                                                                 \
    (void)::sw::test::checkNear( static_cast<float64>( expected ), static_cast<float64>( actual ), static_cast<float64>( tolerance ), \
                                 #expected " ~= " #actual, __FILE__, __LINE__ )
/** @brief 글이 케이스 옆 스냅숏 `Snapshots/<케이스>.<name>.txt` 와 같아야 합니다(`--update-snapshots` 로 갱신). */
#define SW_EXPECT_SNAPSHOT( name, actual ) (void)::sw::test::checkSnapshot( ( name ), ( actual ), __FILE__, __LINE__ )
/** @brief 이 케이스가 일부러 내는 로그(`SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID` 의 id)를 적습니다. 케이스 끝까지 한 번은 나와야 합니다. */
#define SW_EXPECT_LOG( logID ) ::sw::test::expectLog( ( logID ), __FILE__, __LINE__ )

/** @brief 두 값이 달라야 합니다(`==` 가 거짓, 실패 글에 그 값을 쓴다). */
#define SW_EXPECT_NOT_EQUAL( unexpected, actual ) (void)::sw::test::checkNotEqual( ( unexpected ), ( actual ), #unexpected " != " #actual, __FILE__, __LINE__ )
/** @brief 포인터가 nullptr 이어야 합니다. */
#define SW_EXPECT_NULL( pointer ) (void)::sw::test::checkCondition( ( pointer ) == nullptr, #pointer " == nullptr", __FILE__, __LINE__ )
/** @brief 포인터가 nullptr 이 아니어야 합니다. */
#define SW_EXPECT_NOT_NULL( pointer ) (void)::sw::test::checkCondition( ( pointer ) != nullptr, #pointer " != nullptr", __FILE__, __LINE__ )
/** @brief 조건이 참이어야 하고, 실패하면 @p message 를 함께 적습니다. */
#define SW_EXPECT_TRUE_MSG( condition, message ) (void)::sw::test::checkConditionWithMessage( static_cast<bool>( condition ), #condition, ( message ), __FILE__, __LINE__ )
/** @brief 조건이 거짓이어야 하고, 실패하면 @p message 를 함께 적습니다. */
#define SW_EXPECT_FALSE_MSG( condition, message ) \
    (void)::sw::test::checkConditionWithMessage( !static_cast<bool>( condition ), "!(" #condition ")", ( message ), __FILE__, __LINE__ )

/** @brief 조건이 참이어야 하고, 아니면 이 케이스를 바로 끝냅니다(케이스 함수 본문에서만). */
#define SW_REQUIRE_TRUE( condition )                                                                                 \
    do                                                                                                               \
    {                                                                                                                \
        if ( ::sw::test::checkCondition( static_cast<bool>( condition ), #condition, __FILE__, __LINE__ ) == false ) \
            return;                                                                                                  \
    } while ( false )
/** @brief 조건이 거짓이어야 하고, 아니면 이 케이스를 바로 끝냅니다(케이스 함수 본문에서만). */
#define SW_REQUIRE_FALSE( condition ) SW_REQUIRE_TRUE( !( condition ) )
/** @brief 포인터가 nullptr 이 아니어야 하고, 아니면 이 케이스를 바로 끝냅니다(케이스 함수 본문에서만). */
#define SW_REQUIRE_NOT_NULL( pointer ) SW_REQUIRE_TRUE( ( pointer ) != nullptr )
/** @brief 조건이 참이어야 하고, 아니면 @p message 를 적고 이 케이스를 바로 끝냅니다(케이스 함수 본문에서만). */
#define SW_REQUIRE_TRUE_MSG( condition, message )                                                                                            \
    do                                                                                                                                       \
    {                                                                                                                                        \
        if ( ::sw::test::checkConditionWithMessage( static_cast<bool>( condition ), #condition, ( message ), __FILE__, __LINE__ ) == false ) \
            return;                                                                                                                          \
    } while ( false )
/** @brief 두 값이 같아야 하고, 아니면 이 케이스를 바로 끝냅니다(케이스 함수 본문에서만). */
#define SW_REQUIRE_EQUAL( expected, actual )                                                                             \
    do                                                                                                                   \
    {                                                                                                                    \
        if ( ::sw::test::checkEqual( ( expected ), ( actual ), #expected " == " #actual, __FILE__, __LINE__ ) == false ) \
            return;                                                                                                      \
    } while ( false )
