#include "pch.h"

#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

// 엄격한 절대 경로 판정 — 드라이브 상대 · 드라이브 없는 루트 · 상대 경로는 거절한다.

using namespace sw;

SW_TEST_CASE( AbsolutePathTest, RejectsRelativeAndHalfAbsolute )
{
    SW_EXPECT_FALSE( AbsolutePath::isStrictAbsolute( "" ) );
    SW_EXPECT_FALSE( AbsolutePath::isStrictAbsolute( "Saved/x" ) );
    SW_EXPECT_FALSE( AbsolutePath::isStrictAbsolute( "./x" ) );
    SW_EXPECT_FALSE( AbsolutePath::isStrictAbsolute( "C:foo" ) );
    SW_EXPECT_FALSE( AbsolutePath::tryMake( "x/y" ).has_value() );
}

#if defined( SW_PLATFORM_WINDOWS )
SW_TEST_CASE( AbsolutePathTest, WindowsShapes )
{
    SW_EXPECT_TRUE( AbsolutePath::isStrictAbsolute( "C:/a" ) );
    SW_EXPECT_TRUE( AbsolutePath::isStrictAbsolute( "d:\\a" ) );
    SW_EXPECT_TRUE( AbsolutePath::isStrictAbsolute( "//server/share" ) );
    SW_EXPECT_FALSE( AbsolutePath::isStrictAbsolute( "\\foo" ) );
    SW_EXPECT_FALSE( AbsolutePath::isStrictAbsolute( "///x" ) );

    const optional<AbsolutePath> path = AbsolutePath::tryMake( "C:\\Work\\Out" );
    SW_REQUIRE_TRUE( path.has_value() );
    SW_EXPECT_EQUAL( string( "C:/Work/Out" ), path->str() ); // 구분자만 맞추고 대소문자는 그대로
}
#elif defined( SW_PLATFORM_POSIX )
SW_TEST_CASE( AbsolutePathTest, PosixShapes )
{
    SW_EXPECT_TRUE( AbsolutePath::isStrictAbsolute( "/a" ) );
    SW_EXPECT_FALSE( AbsolutePath::isStrictAbsolute( "C:/a" ) );
}
#else
    #error "Unsupported platform"
#endif

SW_TEST_CASE( AbsolutePathTest, ResolveJoinsRelativeToTheGivenBase )
{
#if defined( SW_PLATFORM_POSIX )
    const optional<AbsolutePath> base     = AbsolutePath::tryMake( "/work" );
    const string                 expected = "/work/out/a.log";
#elif defined( SW_PLATFORM_WINDOWS )
    const optional<AbsolutePath> base     = AbsolutePath::tryMake( "C:/work" );
    const string                 expected = "C:/work/out/a.log";
#else
    #error "Unsupported platform"
#endif
    SW_REQUIRE_TRUE( base.has_value() );
    const optional<AbsolutePath> relative = AbsolutePath::tryResolve( "out/a.log", *base );
    SW_REQUIRE_TRUE( relative.has_value() );
    SW_EXPECT_EQUAL( expected, relative->str() );
    SW_EXPECT_TRUE( AbsolutePath::tryResolve( base->str(), *base ) == base ); // 절대면 그대로
    SW_EXPECT_FALSE( AbsolutePath::tryResolve( "", *base ).has_value() );
    SW_EXPECT_FALSE( AbsolutePath::tryResolve( "C:foo", *base ).has_value() ); // 드라이브 상대
}

SW_TEST_CASE( AbsolutePathTest, ResolveCollapsesDotSegments )
{
#if defined( SW_PLATFORM_POSIX )
    const optional<AbsolutePath> base        = AbsolutePath::tryMake( "/work/bin" );
    const string                 expected    = "/work/cooked/engine";
    const string                 absoluteDot = "/work/bin/../cooked/./engine";
#elif defined( SW_PLATFORM_WINDOWS )
    const optional<AbsolutePath> base        = AbsolutePath::tryMake( "C:/work/bin" );
    const string                 expected    = "C:/work/cooked/engine";
    const string                 absoluteDot = "C:\\work\\bin\\..\\cooked\\.\\engine";
#else
    #error "Unsupported platform"
#endif
    SW_REQUIRE_TRUE( base.has_value() );
    // `--cooked-dir=../Cooked` 가 `..` 를 단 채 남으면 포함 판정이 스테이징 파일을 모두 밖으로 봐 팩에서 뺐다.
    const optional<AbsolutePath> relative = AbsolutePath::tryResolve( "../cooked/./engine/", *base );
    SW_REQUIRE_TRUE( relative.has_value() );
    SW_EXPECT_EQUAL( expected, relative->str() );
    const optional<AbsolutePath> absolute = AbsolutePath::tryResolve( absoluteDot, *base );
    SW_REQUIRE_TRUE( absolute.has_value() );
    SW_EXPECT_EQUAL( expected, absolute->str() );
    SW_EXPECT_FALSE( AbsolutePath::tryResolve( "../../../x", *base ).has_value() ); // 뿌리 위
}
