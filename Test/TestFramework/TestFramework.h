/**
 * @file TestFramework.h
 * @brief 단위 테스트 틀 — 케이스 등록(`SW_TEST_CASE`), 단언(`SW_EXPECT_*` · `SW_REQUIRE_*`), 실행(`TestRunner`)입니다.
 * @details 케이스는 정적 초기화에서 등록표에 오르고, 실행 파일의 `main`(`TestMain.cpp`)이 거른 케이스를 차례로 돈다. 이름은 `<스위트>.<케이스>` 이고
 *          `--filter=` 는 와일드카드(`*` · `?`, 쉼표로 여럿, 앞에 `-` 면 뺀다)로 고른다 — gtest · Catch2 와 같은 모양이다.
 *          `SW_EXPECT_*` 는 실패를 적고 이어 가며, `SW_REQUIRE_*` 는 실패하면 그 케이스를 바로 끝낸다(gtest 의 EXPECT · ASSERT).
 *          예외를 쓰지 않는 엔진이라(`/EHsc` 이지만 엔진 코드는 던지지 않는다) REQUIRE 는 케이스 함수에서 `return` 으로 빠진다 — 도우미 함수 안에서는 쓰지 않는다.
 */
#pragma once
#include "Core/Container/formatString.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include <type_traits>

namespace sw::test
{
    /** @brief 케이스 본문 함수입니다. */
    using TestFunction = void ( * )();

    /** @struct TestCase @brief 등록된 케이스 하나입니다. 등록 매크로가 정적 저장소에 둡니다. */
    struct TestCase
    {
        const utf8*  _pSuite;
        const utf8*  _pName;
        TestFunction _pfnRun;
        const utf8*  _pFile;
        int32        _line;
    };

    /**
     * @class TestRegistry
     * @brief 케이스 등록표입니다. 정적 초기화에서 채우므로 저장소는 함수 안 정적이다(초기화 순서).
     */
    class TestRegistry
    {
    public:
        /** @brief 케이스를 더합니다(등록 매크로가 부른다). */
        static void add( const TestCase* pCase );
        /** @brief 등록된 모든 케이스입니다(등록 순서 — 링크 순서라 실행기가 이름 순으로 정렬한다). */
        static const vector<const TestCase*>& getAll();
    };

    /** @brief `SW_TEST_CASE` 가 두는 정적 등록자입니다. */
    struct TestRegistrar
    {
        explicit TestRegistrar( const TestCase* pCase ) { TestRegistry::add( pCase ); }
    };

    /**
     * @class TestContext
     * @brief 지금 도는 케이스의 실패 기록입니다. 단언 매크로가 실패를 여기 적고, 실행기가 케이스마다 비우고 읽는다.
     */
    class TestContext
    {
    public:
        /** @brief 실패 하나를 적고 바로 표준 출력에 씁니다(`파일(줄): 메시지` — IDE 가 줄로 건너뛴다). */
        static void reportFailure( const utf8* pFile, int32 line, string_view message );
        /** @brief 이번 케이스의 실패 수입니다. */
        static uint32 getFailureCount();
        /** @brief 케이스를 시작할 때 실패 수를 비웁니다(실행기가 부른다). */
        static void beginCase();
    };

    /** @brief 단언 실패 글에 값을 넣습니다. 숫자 · 문자열이면 값을, 그 밖의 타입이면 `(값)` 을 씁니다(`formatstring` 이 받지 못하는 타입은 컴파일되지 않으므로). */
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

    /** @brief `SW_EXPECT_EQUAL` 의 본체입니다. 같으면 true 이고, 다르면 두 값을 적은 실패를 남깁니다. */
    template <typename TExpected, typename TActual>
    bool checkEqual( const TExpected& expected, const TActual& actual, const utf8* pExpression, const utf8* pFile, int32 line )
    {
        if ( expected == actual )
            return true;
        const string expectedText = describeValue( expected );
        const string actualText   = describeValue( actual );
        utf8         arrBuffer[constant::kMaxBuffer1024];
        formatstring( arrBuffer, constant::kMaxBuffer1024, "%# — expected %#, actual %#", pExpression, expectedText.c_str(), actualText.c_str() );
        TestContext::reportFailure( pFile, line, arrBuffer );
        return false;
    }

    /** @brief `SW_EXPECT_NEAR` 의 본체입니다. 차이가 @p tolerance 이하면 true 입니다. */
    bool checkNear( float64 expected, float64 actual, float64 tolerance, const utf8* pExpression, const utf8* pFile, int32 line );
    /** @brief `SW_EXPECT_TRUE` · `SW_EXPECT_FALSE` 의 본체입니다. */
    bool checkCondition( bool bPassed, const utf8* pExpression, const utf8* pFile, int32 line );

    /** @struct TestRunOptions @brief 실행기의 명령줄 값입니다. */
    struct TestRunOptions
    {
        string _filter{ "*" }; ///< 와일드카드(쉼표로 여럿, 앞에 `-` 면 뺀다)
        bool   _bListOnly{ false };
    };

    /**
     * @class TestRunner
     * @brief 거른 케이스를 이름 순으로 돌리고 결과를 씁니다.
     */
    class TestRunner
    {
    public:
        /** @brief 명령줄(`--filter=` · `--gtest_filter=` · `--list`)을 읽습니다. 모르는 인자는 false 와 함께 @p outError 에 적습니다. */
        [[nodiscard]] static bool parseArguments( int32 argumentCount, const char* const* ppArgument, TestRunOptions& outOptions, string& outError );
        /** @brief @p name(`스위트.케이스`)이 거르개에 맞는지입니다. */
        [[nodiscard]] static bool matchesFilter( string_view filter, string_view name );
        /** @brief 돌립니다. 실패한 케이스가 없으면 0, 있으면 1, 거르개에 맞는 케이스가 없으면 2 입니다(빈 실행을 통과로 보지 않는다). */
        static int32 run( const TestRunOptions& options );
    };
} // namespace sw::test

/** @brief 케이스 하나를 정의합니다 — `SW_TEST_CASE( Base64UtilTest, RoundTrips ) { … }`. 이름은 `<스위트>.<케이스>` 입니다. */
#define SW_TEST_CASE( suite, name )                                                                                                          \
    static void                            sw_testBody_##suite##_##name();                                                                   \
    static const ::sw::test::TestCase      sw_testCase_##suite##_##name{ #suite, #name, &sw_testBody_##suite##_##name, __FILE__, __LINE__ }; \
    static const ::sw::test::TestRegistrar sw_testRegistrar_##suite##_##name{ &sw_testCase_##suite##_##name };                               \
    static void                            sw_testBody_##suite##_##name()

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
