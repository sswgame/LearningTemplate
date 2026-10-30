#include "pch.h"

#include "Core/File/AbsolutePath.h"
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
