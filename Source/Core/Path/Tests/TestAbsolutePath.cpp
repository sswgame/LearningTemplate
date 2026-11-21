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

namespace
{
    /** @brief 기대 경로 글을 잇습니다(`string` 의 `+` 는 지웠다). */
    string concat( const string& head, string_view tail )
    {
        string text = head;
        text.append( tail.data(), tail.size() );
        return text;
    }
} // namespace

SW_TEST_CASE( AbsolutePathTest, JoinNormalizesAndRejectsEscapingAboveTheBase )
{
#if defined( SW_PLATFORM_POSIX )
    const optional<AbsolutePath> base     = AbsolutePath::tryMake( "/work/out/" );
    const string                 root     = "/";
    const string                 baseText = "/work";
#elif defined( SW_PLATFORM_WINDOWS )
    const optional<AbsolutePath> base     = AbsolutePath::tryMake( "C:\\work\\out\\" );
    const string                 root     = "C:/";
    const string                 baseText = "C:/work";
#else
    #error "Unsupported platform"
#endif
    SW_REQUIRE_TRUE( base.has_value() );
    // 구분자 · 끝 구분자 · 앞 구분자 · 빈 구간 · `.` 는 맞추고, 이은 부분 안의 `..` 는 푼다
    SW_EXPECT_EQUAL( concat( baseText, "/out/a/b.txt" ), base->join( "a\\b.txt" ).str() );
    SW_EXPECT_EQUAL( concat( baseText, "/out/a/c" ), base->join( "/a//./b/../c/" ).str() );
    SW_EXPECT_EQUAL( concat( baseText, "/out" ), base->join( "" ).str() );
    SW_EXPECT_EQUAL( concat( baseText, "/out/x/y" ), base->join( "x" ).join( "y" ).str() );
    // 이 경로 위로 올라가면 빈 경로다 — 그 값을 받은 I/O 는 실패한다
    SW_EXPECT_TRUE( base->join( ".." ).empty() );
    SW_EXPECT_TRUE( base->join( "a/../../b" ).empty() );
    SW_EXPECT_TRUE( base->join( "a/../.." ).empty() );
    SW_EXPECT_TRUE( base->join( string_view( "a\0b", 3 ) ).empty() );
    SW_EXPECT_TRUE( base->join( "" ).join( ".." ).empty() );
    // 뿌리에 잇기 · 뿌리에서 `..` 는 뿌리 밑으로 내려가지 않는다
    const optional<AbsolutePath> rootPath = AbsolutePath::tryMake( root );
    SW_REQUIRE_TRUE( rootPath.has_value() );
    SW_EXPECT_EQUAL( concat( root, "a" ), rootPath->join( "a" ).str() );
    SW_EXPECT_EQUAL( root, rootPath->join( "a/.." ).str() );
    SW_EXPECT_TRUE( rootPath->join( ".." ).empty() );
}

SW_TEST_CASE( AbsolutePathTest, ParentFollowsDirectoryPartRules )
{
#if defined( SW_PLATFORM_POSIX )
    const string root = "/";
#elif defined( SW_PLATFORM_WINDOWS )
    const string root = "C:/";
#else
    #error "Unsupported platform"
#endif
    const optional<AbsolutePath> file = AbsolutePath::tryMake( concat( root, "baseText/out/a.txt" ) );
    SW_REQUIRE_TRUE( file.has_value() );
    SW_EXPECT_EQUAL( concat( root, "baseText/out" ), file->getParent().str() );
    SW_EXPECT_EQUAL( concat( root, "baseText" ), file->getParent().getParent().str() );
    SW_EXPECT_EQUAL( root, file->getParent().getParent().getParent().str() );        // 부모가 뿌리면 뿌리
    SW_EXPECT_TRUE( file->getParent().getParent().getParent().getParent().empty() ); // 뿌리의 부모는 없다
    const optional<AbsolutePath> folder = AbsolutePath::tryMake( concat( root, "baseText/out/" ) );
    SW_REQUIRE_TRUE( folder.has_value() );
    SW_EXPECT_EQUAL( concat( root, "baseText" ), folder->getParent().str() ); // 끝 구분자는 무시한다
}

SW_TEST_CASE( AbsolutePathTest, AppendSuffixNamesASidecarAndRejectsComponentChanges )
{
#if defined( SW_PLATFORM_POSIX )
    const string root = "/";
#elif defined( SW_PLATFORM_WINDOWS )
    const string root = "C:/";
#else
    #error "Unsupported platform"
#endif
    const optional<AbsolutePath> file = AbsolutePath::tryMake( concat( root, "baseText/hit.ogg" ) );
    SW_REQUIRE_TRUE( file.has_value() );
    SW_EXPECT_EQUAL( concat( root, "baseText/hit.ogg.meta" ), file->appendSuffix( ".meta" ).str() );
    SW_EXPECT_TRUE( file->appendSuffix( "" ) == *file );
    SW_EXPECT_TRUE( file->appendSuffix( "/x" ).empty() ); // 성분을 바꾸는 접미사는 join 의 일이다
    SW_EXPECT_TRUE( file->appendSuffix( "\\x" ).empty() );
    const optional<AbsolutePath> rootPath = AbsolutePath::tryMake( root );
    SW_REQUIRE_TRUE( rootPath.has_value() );
    SW_EXPECT_TRUE( rootPath->appendSuffix( ".meta" ).empty() ); // 뿌리는 이름이 없다
}
