// ProcessEnvironment — 환경 변수의 세 상태(없음 · 빈 값 · 값)를 가르고, 덮어쓰기 · 지우기 · UTF-8 값 왕복, 잘못된 이름 거절.
#include "pch.h"

#include "Core/Process/ProcessEnvironment.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 케이스가 건드린 환경 변수의 옛 상태(없음 · 값)를 저장했다가 끝날 때 되돌립니다 — 프로세스 환경은 케이스 사이에 새지 않게 한다. */
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

    constexpr const utf8* kProcessEnvironmentTestVariable = "SW_TEST_PROCESS_ENVIRONMENT_VARIABLE";
} // namespace

/**
 * @brief `tryRead` 는 빈 값을 정해진 값으로, `tryReadNonEmpty` 는 없음과 같게 읽는다.
 */
SW_TEST_CASE( ProcessEnvironmentTest, DistinguishesUnsetEmptyAndSet )
{
    ScopedEnvironmentVariable restore{ kProcessEnvironmentTestVariable };
    string                    value{ "stale" };

    SW_REQUIRE_TRUE( ProcessEnvironment::tryUnset( kProcessEnvironmentTestVariable ) );
    SW_EXPECT_FALSE( ProcessEnvironment::tryRead( kProcessEnvironmentTestVariable, value ) );
    SW_EXPECT_TRUE( value.empty() ); // 없으면 출력도 비운다
    SW_EXPECT_FALSE( ProcessEnvironment::tryReadNonEmpty( kProcessEnvironmentTestVariable, value ) );

    SW_REQUIRE_TRUE( ProcessEnvironment::trySet( kProcessEnvironmentTestVariable, "" ) );
    value = "stale";
    SW_EXPECT_TRUE( ProcessEnvironment::tryRead( kProcessEnvironmentTestVariable, value ) ); // 빈 값은 정해진 값이다
    SW_EXPECT_TRUE( value.empty() );
    SW_EXPECT_FALSE( ProcessEnvironment::tryReadNonEmpty( kProcessEnvironmentTestVariable, value ) ); // 빈 값은 값이 아니다

    SW_REQUIRE_TRUE( ProcessEnvironment::trySet( kProcessEnvironmentTestVariable, "value 1" ) );
    SW_EXPECT_TRUE( ProcessEnvironment::tryRead( kProcessEnvironmentTestVariable, value ) );
    SW_EXPECT_EQUAL( string( "value 1" ), value );
    SW_EXPECT_TRUE( ProcessEnvironment::tryReadNonEmpty( kProcessEnvironmentTestVariable, value ) );
}

/**
 * @brief 다시 정하면 새 값이 읽히고 지우면 없어지며, 한글 값도 UTF-8 로 돌아온다(Windows 에서 CRT `getenv`(ANSI)로 읽으면 깨진다).
 */
SW_TEST_CASE( ProcessEnvironmentTest, SetOverwritesUnsetRemovesAndKeepsUtf8 )
{
    ScopedEnvironmentVariable restore{ kProcessEnvironmentTestVariable };
    string                    value;

    SW_REQUIRE_TRUE( ProcessEnvironment::trySet( kProcessEnvironmentTestVariable, "first" ) );
    SW_REQUIRE_TRUE( ProcessEnvironment::trySet( kProcessEnvironmentTestVariable, "second" ) );
    SW_REQUIRE_TRUE( ProcessEnvironment::tryRead( kProcessEnvironmentTestVariable, value ) );
    SW_EXPECT_EQUAL( string( "second" ), value );

    SW_REQUIRE_TRUE( ProcessEnvironment::trySet( kProcessEnvironmentTestVariable, "" ) ); // 값이 있던 변수를 빈 값으로 — 옛 값이 남지 않는다
    SW_REQUIRE_TRUE( ProcessEnvironment::tryRead( kProcessEnvironmentTestVariable, value ) );
    SW_EXPECT_TRUE( value.empty() );

    SW_REQUIRE_TRUE( ProcessEnvironment::tryUnset( kProcessEnvironmentTestVariable ) );
    SW_EXPECT_FALSE( ProcessEnvironment::tryRead( kProcessEnvironmentTestVariable, value ) );
    SW_EXPECT_TRUE( ProcessEnvironment::tryUnset( kProcessEnvironmentTestVariable ) ); // 없던 변수를 지워도 성공이다

    const utf8* const pKoreanValue = "경로/폴더";
    SW_REQUIRE_TRUE( ProcessEnvironment::trySet( kProcessEnvironmentTestVariable, pKoreanValue ) );
    SW_REQUIRE_TRUE( ProcessEnvironment::tryRead( kProcessEnvironmentTestVariable, value ) );
    SW_EXPECT_EQUAL( string( pKoreanValue ), value );
}

/**
 * @brief 이름이 비었거나 `=` 를 품으면 거절한다 — 읽기는 false, 정하기 · 지우기도 false.
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
