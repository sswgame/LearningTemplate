// X11InputMethodTest — XIM 조합 글(multi_byte)은 로캘 인코딩이라 로캘 변환으로 UTF-8 로 바꾼다(nogpu, Linux 클라이언트 빌드만).
#include "pch.h"

#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Window/Linux/X11InputMethod.h"

#include <clocale>

using namespace sw;

namespace
{
    /** @brief 케이스 동안 `LC_CTYPE` 를 바꾸고 끝나면 되돌립니다. */
    class CharacterLocaleScope
    {
    public:
        explicit CharacterLocaleScope( const char* pLocale )
            : _previous{ std::setlocale( LC_CTYPE, nullptr ) }
            , _bApplied{ SW_FALSE }
        {
            _bApplied = std::setlocale( LC_CTYPE, pLocale ) != nullptr ? SW_TRUE : SW_FALSE;
        }
        ~CharacterLocaleScope() { (void)std::setlocale( LC_CTYPE, _previous.c_str() ); }
        CharacterLocaleScope( const CharacterLocaleScope& )            = delete;
        CharacterLocaleScope& operator=( const CharacterLocaleScope& ) = delete;

        bool isApplied() const { return _bApplied == SW_TRUE; }

    private:
        string _previous;
        uint8  _bApplied : 1;
    };
} // namespace

/** @brief [X11InputMethodTest] UTF-8 로캘의 한글 조합 글은 그대로 UTF-8 이고, 로캘에서 틀린 바이트는 오류로 멈춘다(그 앞까지만 붙는다) */
SW_TEST_CASE( X11InputMethodTest, PreeditTextIsDecodedWithTheLocale )
{
    const CharacterLocaleScope locale{ "C.UTF-8" };
    SW_REQUIRE_TRUE_MSG( locale.isApplied(), "the C.UTF-8 locale is part of glibc 2.35+ - the Linux test machine must have it" );

    string text;
    SW_EXPECT_TRUE( X11InputMethod::appendLocaleText( "\xEA\xB0\x80\xEB\x82\x98", text ) ); // 가나
    SW_EXPECT_EQUAL( string( "\xEA\xB0\x80\xEB\x82\x98" ), text );

    SW_EXPECT_LOG( "X11InputMethod.BadLocaleText" );
    string broken;
    SW_EXPECT_FALSE( X11InputMethod::appendLocaleText( "a\xFF"
                                                       "b",
                                                       broken ) );
    SW_EXPECT_EQUAL( string( "a" ), broken );
}
