#include "pch.h"

#include "Core/Process/ProcessEnvironment.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 시험이 건드린 환경 변수의 옛 상태(없음 · 값)를 저장했다가 끝날 때 되돌립니다 — 프로세스 환경은 시험 사이에 새지 않게 한다. */
    class ScopedEnvironmentVariable
    {
    public:
        explicit ScopedEnvironmentVariable( string name )
            : _name{ std::move( name ) }
            , _bHadValue{ ProcessEnvironment::tryRead( _name, _previous ) }
        {
        }

        ~ScopedEnvironmentVariable()
        {
            if ( _bHadValue )
                (void)ProcessEnvironment::trySet( _name, _previous ); // 소멸자에서는 알릴 길이 없다 - 이름은 읽을 때 검증된 유효한 이름이다
            else
                (void)ProcessEnvironment::tryUnset( _name ); // 위와 같다
        }

        ScopedEnvironmentVariable( const ScopedEnvironmentVariable& )            = delete;
        ScopedEnvironmentVariable& operator=( const ScopedEnvironmentVariable& ) = delete;

    private:
        string _name;
        string _previous;
        bool   _bHadValue;
    };

    constexpr const utf8* kVariable = "SW_TEST_PROCESS_ENVIRONMENT_VARIABLE";
} // namespace

/**
 * @brief [ProcessEnvironmentTest] 없음 · 빈 값 · 값 있음을 가른다 — `tryRead` 는 빈 값을 정해진 값으로, `tryReadNonEmpty` 는 없음과 같게 읽는다
 * @details 변이: 빈 값을 없음으로 읽으면 중간 단언이 지고, 빈 값을 값으로 읽는 비어 있지 않은 판이면 마지막 단언이 진다.
 */
SW_TEST_CASE( ProcessEnvironmentTest, DistinguishesUnsetEmptyAndSet )
{
    ScopedEnvironmentVariable restore{ kVariable };
    string                    value{ "stale" };

    SW_ASSERT_TRUE( ProcessEnvironment::tryUnset( kVariable ) );
    SW_EXPECT_FALSE( ProcessEnvironment::tryRead( kVariable, value ) );
    SW_EXPECT_TRUE( value.empty() ); // 없으면 출력도 비운다
    SW_EXPECT_FALSE( ProcessEnvironment::tryReadNonEmpty( kVariable, value ) );

    SW_ASSERT_TRUE( ProcessEnvironment::trySet( kVariable, "" ) );
    value = "stale";
    SW_EXPECT_TRUE( ProcessEnvironment::tryRead( kVariable, value ) ); // 빈 값은 정해진 값이다
    SW_EXPECT_TRUE( value.empty() );
    SW_EXPECT_FALSE( ProcessEnvironment::tryReadNonEmpty( kVariable, value ) ); // 빈 값은 값이 아니다

    SW_ASSERT_TRUE( ProcessEnvironment::trySet( kVariable, "value 1" ) );
    SW_EXPECT_TRUE( ProcessEnvironment::tryRead( kVariable, value ) );
    SW_EXPECT_STREQ( "value 1", value.c_str() );
    SW_EXPECT_TRUE( ProcessEnvironment::tryReadNonEmpty( kVariable, value ) );
}

/**
 * @brief [ProcessEnvironmentTest] 다시 정하면 새 값이 읽히고 지우면 없어지며, 한글 값도 UTF-8 로 돌아온다
 * @details 변이: Windows 에서 CRT `getenv`(ANSI)로 읽으면 한글 값이 깨져 마지막 단언이 진다.
 */
SW_TEST_CASE( ProcessEnvironmentTest, SetOverwritesUnsetRemovesAndKeepsUtf8 )
{
    ScopedEnvironmentVariable restore{ kVariable };
    string                    value;

    SW_ASSERT_TRUE( ProcessEnvironment::trySet( kVariable, "first" ) );
    SW_ASSERT_TRUE( ProcessEnvironment::trySet( kVariable, "second" ) );
    SW_ASSERT_TRUE( ProcessEnvironment::tryRead( kVariable, value ) );
    SW_EXPECT_STREQ( "second", value.c_str() );

    SW_ASSERT_TRUE( ProcessEnvironment::trySet( kVariable, "" ) ); // 값이 있던 변수를 빈 값으로 — 옛 값이 남지 않는다
    SW_ASSERT_TRUE( ProcessEnvironment::tryRead( kVariable, value ) );
    SW_EXPECT_TRUE( value.empty() );

    SW_ASSERT_TRUE( ProcessEnvironment::tryUnset( kVariable ) );
    SW_EXPECT_FALSE( ProcessEnvironment::tryRead( kVariable, value ) );
    SW_EXPECT_TRUE( ProcessEnvironment::tryUnset( kVariable ) ); // 없던 변수를 지워도 성공이다

    const utf8* const pKoreanValue = "경로/폴더";
    SW_ASSERT_TRUE( ProcessEnvironment::trySet( kVariable, pKoreanValue ) );
    SW_ASSERT_TRUE( ProcessEnvironment::tryRead( kVariable, value ) );
    SW_EXPECT_STREQ( pKoreanValue, value.c_str() );
}

/**
 * @brief [ProcessEnvironmentTest] 이름이 비었거나 `=` 를 품으면 거절한다 — 읽기는 false, 정하기 · 지우기도 false
 */
SW_TEST_CASE( ProcessEnvironmentTest, RejectsInvalidNames )
{
    string value{ "stale" };
    SW_EXPECT_FALSE( ProcessEnvironment::tryRead( "", value ) );
    SW_EXPECT_TRUE( value.empty() );
    SW_EXPECT_FALSE( ProcessEnvironment::tryRead( "A=B", value ) );
    SW_EXPECT_FALSE( ProcessEnvironment::trySet( "", "x" ) );
    SW_EXPECT_FALSE( ProcessEnvironment::trySet( "A=B", "x" ) );
    SW_EXPECT_FALSE( ProcessEnvironment::tryUnset( "" ) );
}
