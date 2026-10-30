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

#if !defined( SW_PLATFORM_LINUX )
SW_TEST_CASE( AbsolutePathTest, WindowsShapes )
{
    SW_EXPECT_TRUE( AbsolutePath::isStrictAbsolute( "C:/a" ) );
    SW_EXPECT_TRUE( AbsolutePath::isStrictAbsolute( "d:\\a" ) );
    SW_EXPECT_TRUE( AbsolutePath::isStrictAbsolute( "//server/share" ) );
    SW_EXPECT_FALSE( AbsolutePath::isStrictAbsolute( "\\foo" ) );
    SW_EXPECT_FALSE( AbsolutePath::isStrictAbsolute( "///x" ) );

    const std::optional<AbsolutePath> path = AbsolutePath::tryMake( "C:\\Work\\Out" );
    SW_REQUIRE_TRUE( path.has_value() );
    SW_EXPECT_EQUAL( string( "C:/Work/Out" ), path->str() ); // 구분자만 맞추고 대소문자는 그대로
}
#else
SW_TEST_CASE( AbsolutePathTest, PosixShapes )
{
    SW_EXPECT_TRUE( AbsolutePath::isStrictAbsolute( "/a" ) );
    SW_EXPECT_FALSE( AbsolutePath::isStrictAbsolute( "C:/a" ) );
}
#endif

SW_TEST_CASE( AbsolutePathTest, ResolveJoinsRelativeToTheGivenBase )
{
#if defined( SW_PLATFORM_LINUX )
    const std::optional<AbsolutePath> base     = AbsolutePath::tryMake( "/work" );
    const string                      expected = "/work/out/a.log";
#else
    const std::optional<AbsolutePath> base     = AbsolutePath::tryMake( "C:/work" );
    const string                      expected = "C:/work/out/a.log";
#endif
    SW_REQUIRE_TRUE( base.has_value() );
    const std::optional<AbsolutePath> relative = AbsolutePath::tryResolve( "out/a.log", *base );
    SW_REQUIRE_TRUE( relative.has_value() );
    SW_EXPECT_EQUAL( expected, relative->str() );
    SW_EXPECT_TRUE( AbsolutePath::tryResolve( base->str(), *base ) == base ); // 절대면 그대로
    SW_EXPECT_FALSE( AbsolutePath::tryResolve( "", *base ).has_value() );
    SW_EXPECT_FALSE( AbsolutePath::tryResolve( "C:foo", *base ).has_value() ); // 드라이브 상대
}
