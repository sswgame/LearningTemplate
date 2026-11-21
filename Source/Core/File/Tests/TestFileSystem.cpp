#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

// 파일 시스템 함수(네이티브 원시 연산 위의 재귀 만들기 · 지우기 · 순회 · 시각 · 읽기 전용 · 상대 경로).

using namespace sw;

namespace
{
    struct TestFileSystemInternal
    {
        static bool countEntry( void* pContext, const DirectoryEntry& entry )
        {
            uint32& count = *static_cast<uint32*>( pContext );
            if ( entry._bDirectory == false )
                ++count;
            return true;
        }

        static bool stopAtFirst( void* pContext, const DirectoryEntry& )
        {
            ++*static_cast<uint32*>( pContext );
            return false;
        }
    };
} // namespace

SW_TEST_CASE( FileSystemTest, CreatesWalksAndRemovesATree )
{
    const AbsolutePath root = test::makeScratchPath( "tree" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root.join( "a/b/c" ) ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( root.join( "a/one.txt" ), "1" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( root.join( "a/b/c/two.txt" ), "22" ) );
    SW_EXPECT_TRUE( FileUtil::ensureDirectoryExists( root.join( "a/b/c" ) ) ); // 이미 있으면 성공

    uint32 fileCount = 0;
    SW_EXPECT_TRUE( FileUtil::forEachDirectoryEntry( root, true, TestFileSystemInternal::countEntry, &fileCount ) );
    SW_EXPECT_EQUAL( 2u, fileCount );

    uint32 visitCount = 0;
    SW_EXPECT_TRUE( FileUtil::forEachDirectoryEntry( root, true, TestFileSystemInternal::stopAtFirst, &visitCount ) ); // 콜백이 멈춘 것은 실패가 아니다
    SW_EXPECT_EQUAL( 1u, visitCount );

    uint64 size = 0;
    SW_REQUIRE_TRUE( FileUtil::tryGetFileSize( root.join( "a/b/c/two.txt" ), size ) );
    SW_EXPECT_EQUAL( uint64{ 2 }, size );
    SW_EXPECT_FALSE( FileUtil::tryGetFileSize( root.join( "a" ), size ) );       // 폴더는 크기가 없다
    SW_EXPECT_FALSE( FileUtil::tryGetFileSize( root.join( "missing" ), size ) ); // 없는 파일은 0 이 아니라 실패다

    // 읽기 전용 파일이 있어도 폴더째 지운다.
    SW_REQUIRE_TRUE( FileUtil::setWritable( root.join( "a/one.txt" ), false ) );
    SW_EXPECT_TRUE( FileUtil::isReadOnlyFile( root.join( "a/one.txt" ) ) );
    SW_EXPECT_TRUE( FileUtil::removeDirectory( root.join( "a" ) ) );
    SW_EXPECT_FALSE( FileUtil::exists( root.join( "a" ) ) );
}

SW_TEST_CASE( FileSystemTest, WriteTimeRoundTrips )
{
    const AbsolutePath file = test::makeScratchPath( "stamp.txt" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( file, "x" ) );
    const int64 ticks = FileUtil::getCurrentFileWriteTime() - 3600 * FileUtil::kFileTimeTicksPerSecond;
    SW_REQUIRE_TRUE( FileUtil::setFileWriteTime( file, ticks ) );
    int64 readTicks = 0;
    SW_REQUIRE_TRUE( FileUtil::getFileWriteTime( file, readTicks ) );
    SW_EXPECT_EQUAL( ticks / FileUtil::kFileTimeTicksPerSecond, readTicks / FileUtil::kFileTimeTicksPerSecond );
}

SW_TEST_CASE( FileSystemTest, RelativePathIsTextual )
{
    const AbsolutePath root = test::makeScratchPath( "rel" );
    string             relative;
    SW_REQUIRE_TRUE( FileUtil::makeRelativePath( root.str(), FileUtil::joinPath( root.str(), "x/y.txt" ), relative ) );
    SW_EXPECT_EQUAL( string( "x/y.txt" ), relative );
    SW_REQUIRE_TRUE( FileUtil::makeRelativePath( FileUtil::joinPath( root.str(), "x/z" ), FileUtil::joinPath( root.str(), "w" ), relative ) );
    SW_EXPECT_EQUAL( string( "../../w" ), relative );
    SW_REQUIRE_TRUE( FileUtil::makeRelativePath( root.str(), root.str(), relative ) );
    SW_EXPECT_EQUAL( string( "." ), relative );
    SW_EXPECT_FALSE( FileUtil::makeRelativePath( root.str(), "x/y.txt", relative ) ); // 상대 경로는 받지 않는다
}

SW_TEST_CASE( FileSystemTest, OptionalReadTellsMissingFromFailed )
{
    const AbsolutePath file = test::makeScratchPath( "record.txt" );
    string             text;
    SW_EXPECT_TRUE( FileUtil::readOptionalTextFile( file, text ) == OptionalFileRead::Missing ); // 로그 없음 - 오류 로그가 나면 케이스가 진다
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( file, "abc" ) );
    SW_REQUIRE_TRUE( FileUtil::readOptionalTextFile( file, text ) == OptionalFileRead::Read );
    SW_EXPECT_EQUAL( string( "abc" ), text );
}

SW_TEST_CASE( FileSystemTest, RelativePathsDoNotReachTheOS )
{
    // R2 - 디스크 I/O 는 절대 경로만 받는다. 작업 폴더로 풀지 않고 오류로 알린다(입구가 받을 때 절대화했어야 한다).
    SW_EXPECT_LOG( "File.RelativePath" );
    SW_EXPECT_FALSE( FileUtil::exists( FileUtil::toDiskPath( "Config/Engine/EngineConfig.json" ) ) ); // 작업 폴더가 저장소 루트여도 없다
}

namespace
{
    /** @brief `FileUtil::exists` 를 @p T 로 부를 수 있으면 true — 문자열 판은 private 이라 바깥에서는 부를 수 없다(접근 검사도 치환 실패다). */
    template <typename T, typename = void>
    struct CanCallExists : std::false_type
    {
    };
    template <typename T>
    struct CanCallExists<T, std::void_t<decltype( FileUtil::exists( std::declval<T>() ) )>> : std::true_type
    {
    };
    static_assert( CanCallExists<const AbsolutePath&>::value, "disk I/O takes AbsolutePath" );
    static_assert( CanCallExists<string>::value == false && CanCallExists<string_view>::value == false && CanCallExists<const utf8*>::value == false,
                   "disk I/O does not take strings - lift them with FileUtil::toDiskPath (disk-path-args)" );
    static_assert( std::is_convertible_v<string, AbsolutePath> == false && std::is_convertible_v<string_view, AbsolutePath> == false,
                   "a string never becomes a disk path implicitly" );
} // namespace

/**
 * @brief 문자열 → 디스크 경로의 명시적 입구(`FileUtil::toDiskPath`): 엄격한 절대 경로는 구분자를 `/` 로 맞추고, 빈 글은 오류 없이 빈 경로이며 그 경로의 I/O 는 실패한다.
 * @details 변이: 구분자 정규화를 빼면 Windows 의 `\` 경로가 그대로 남아 진다. 빈 글에 오류를 남기면 이 케이스가 로그로 진다(빈 인자는 예전에도 조용한 실패였다).
 */
SW_TEST_CASE( FileSystemTest, ToDiskPathNormalizesAndEmptyFails )
{
    const AbsolutePath file     = test::makeScratchPath( "disk-path.txt" );
    string             text     = file.str(); // 글 입구를 본다 — `const` 글이면 바꾸기 도구(disk-path-join)가 형식으로 올린다
    const AbsolutePath diskPath = FileUtil::toDiskPath( text );
    SW_REQUIRE_FALSE( diskPath.empty() );
    SW_EXPECT_EQUAL( FileUtil::normalizeSeparators( file.str() ), diskPath.str() );
#if defined( SW_PLATFORM_WINDOWS )
    SW_EXPECT_EQUAL( string( "C:/a/b" ), FileUtil::toDiskPath( "C:\\a\\b" ).str() );
#endif
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( diskPath, "abc" ) );
    SW_EXPECT_TRUE( FileUtil::isRegularFile( diskPath ) );

    const AbsolutePath emptyPath = FileUtil::toDiskPath( "" ); // 로그 없음 - 오류 로그가 나면 케이스가 진다
    SW_EXPECT_TRUE( emptyPath.empty() );
    SW_EXPECT_FALSE( FileUtil::exists( emptyPath ) );
    string emptyText;
    SW_EXPECT_TRUE( FileUtil::readOptionalTextFile( emptyPath, emptyText ) != OptionalFileRead::Read );
}
