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
 *          `SW_REQUIRE_*` 는 실패하면 케이스 함수에서 `return` 한다 — 케이스 본문에서만 쓴다.
 */
#pragma once
#include "Core/Container/formatString.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include <type_traits>

#if !defined( SW_SHIPPING )
    #define SW_TESTS_ENABLED 1
#else
    #define SW_TESTS_ENABLED 0
#endif

namespace sw::test
{
    /** @brief 케이스 본문 함수입니다. */
    using TestFunction = void ( * )();

    /** @struct TestCaseRegistration @brief 케이스 하나의 등록 줄입니다. 글자는 등록한 이미지(모듈)의 정적 저장소에 있습니다. */
    struct TestCaseRegistration
    {
        const utf8*  _pName; ///< `<스위트>.<케이스>`
        TestFunction _pfnRun;
        const utf8*  _pFile; ///< 케이스를 정의한 파일(스냅숏 폴더가 그 옆 `Snapshots/` 이다)
        int32        _line;
    };

    /** @brief `SW_TEST_CASE` 가 두는 정적 등록자입니다 — 만들 때 올리고 없앨 때(모듈을 내릴 때) 뺍니다. */
    struct SW_API TestCaseRegistrar
    {
        explicit TestCaseRegistrar( const TestCaseRegistration* pRegistration );
        ~TestCaseRegistrar();
        TestCaseRegistrar( const TestCaseRegistrar& )            = delete;
        TestCaseRegistrar& operator=( const TestCaseRegistrar& ) = delete;

        const TestCaseRegistration* _pRegistration;
        bool                        _bRegistered;
    };

    /** @brief 지금 도는 케이스에 실패 하나를 적습니다(`파일(줄): error: …` 로 바로 찍는다 — IDE 가 그 줄로 간다). */
    SW_API void reportFailure( const utf8* pFile, int32 line, string_view message );
    /** @brief 단언 하나를 셉니다(통과든 실패든). 단언이 없는 케이스는 실패다. */
    SW_API void countAssertion();
    /** @brief 이 케이스가 일부러 내는 로그 id 를 적습니다. 그 id 의 Warning · Error 는 실패로 세지 않고, 끝까지 한 번도 안 나오면 실패입니다. */
    SW_API void expectLog( const utf8* pLogID, const utf8* pFile, int32 line );
    /**
     * @brief 이 케이스만 쓰는 빈 폴더 아래의 경로입니다(`<작업 폴더>/Saved/Test/<프로세스>/<케이스>/<회차>/<name>`).
     * @details 폴더는 처음 부를 때 만든다. 테스트는 이 밖(Bin · 소스 트리 · 고정 이름 임시 경로)에 쓰지 않는다 — 나란히 도는 프로세스 · 반복 회차가 부딪치지 않게.
     */
    SW_API string makeScratchPath( string_view name );
    /**
     * @brief @p actual 을 케이스 파일 옆 `Snapshots/<케이스>.<name>.txt` 와 비교합니다(줄 끝은 LF 로 맞춘다).
     * @details 스냅숏이 없거나 다르면 실패이고, `--update-snapshots` 로 돌면 파일을 쓰고 통과한다(쓴 것을 보고에 적는다). 손으로 옮겨 적는 기대값을 대신한다.
     */
    SW_API bool checkSnapshot( string_view name, string_view actual, const utf8* pFile, int32 line );

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
            utf8 arrBuffer[constant::kMaxBuffer256];
            formatstring( arrBuffer, constant::kMaxBuffer256, "%#", value );
            return string( arrBuffer );
        }
        else
        {
            return string( "(value)" );
        }
    }

    /** @brief `SW_EXPECT_EQUAL` 의 본체입니다. */
    template <typename TExpected, typename TActual>
    bool checkEqual( const TExpected& expected, const TActual& actual, const utf8* pExpression, const utf8* pFile, int32 line )
    {
        countAssertion();
        if ( expected == actual )
            return true;
        const string expectedText = describeValue( expected );
        const string actualText   = describeValue( actual );
        utf8         arrBuffer[constant::kMaxBuffer1024];
        formatstring( arrBuffer, constant::kMaxBuffer1024, "%# — expected %#, actual %#", pExpression, expectedText.c_str(), actualText.c_str() );
        reportFailure( pFile, line, arrBuffer );
        return false;
    }

    /** @brief `SW_EXPECT_NEAR` 의 본체입니다. */
    SW_API bool checkNear( float64 expected, float64 actual, float64 tolerance, const utf8* pExpression, const utf8* pFile, int32 line );
    /** @brief `SW_EXPECT_TRUE` · `SW_EXPECT_FALSE` · `SW_REQUIRE_TRUE` 의 본체입니다. */
    SW_API bool checkCondition( bool bPassed, const utf8* pExpression, const utf8* pFile, int32 line );
} // namespace sw::test

#if SW_TESTS_ENABLED
    /** @brief 케이스 하나를 정의합니다 — `SW_TEST_CASE( Base64UtilTest, RoundTrips ) { … }`(기능 폴더의 `Tests/` 파일, 파일 범위). 이름은 `<스위트>.<케이스>` 입니다. */
    #define SW_TEST_CASE( suite, name )                                                                                                        \
        static void                                   sw_testBody_##suite##_##name();                                                          \
        static const ::sw::test::TestCaseRegistration sw_testCase_##suite##_##name{ #suite "." #name, &sw_testBody_##suite##_##name, __FILE__, \
                                                                                    __LINE__ };                                                \
        static const ::sw::test::TestCaseRegistrar    sw_testRegistrar_##suite##_##name{ &sw_testCase_##suite##_##name };                      \
        static void                                   sw_testBody_##suite##_##name()
#else
    #define SW_TEST_CASE( suite, name ) [[maybe_unused]] static void sw_testBody_##suite##_##name()
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

/** @brief 조건이 참이어야 하고, 아니면 이 케이스를 바로 끝냅니다(케이스 함수 본문에서만). */
#define SW_REQUIRE_TRUE( condition )                                                                                 \
    do                                                                                                               \
    {                                                                                                                \
        if ( ::sw::test::checkCondition( static_cast<bool>( condition ), #condition, __FILE__, __LINE__ ) == false ) \
            return;                                                                                                  \
    } while ( false )
/** @brief 두 값이 같아야 하고, 아니면 이 케이스를 바로 끝냅니다(케이스 함수 본문에서만). */
#define SW_REQUIRE_EQUAL( expected, actual )                                                                             \
    do                                                                                                                   \
    {                                                                                                                    \
        if ( ::sw::test::checkEqual( ( expected ), ( actual ), #expected " == " #actual, __FILE__, __LINE__ ) == false ) \
            return;                                                                                                      \
    } while ( false )
