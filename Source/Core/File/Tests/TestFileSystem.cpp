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
    const string root = test::makeScratchPath( "tree" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( FileUtil::joinPath( root, "a/b/c" ) ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( root, "a/one.txt" ), "1" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( root, "a/b/c/two.txt" ), "22" ) );
    SW_EXPECT_TRUE( FileUtil::ensureDirectoryExists( FileUtil::joinPath( root, "a/b/c" ) ) ); // 이미 있으면 성공

    uint32 fileCount = 0;
    SW_EXPECT_TRUE( FileUtil::forEachDirectoryEntry( root, true, TestFileSystemInternal::countEntry, &fileCount ) );
    SW_EXPECT_EQUAL( 2u, fileCount );

    uint32 visitCount = 0;
    SW_EXPECT_TRUE( FileUtil::forEachDirectoryEntry( root, true, TestFileSystemInternal::stopAtFirst, &visitCount ) ); // 콜백이 멈춘 것은 실패가 아니다
    SW_EXPECT_EQUAL( 1u, visitCount );

    SW_EXPECT_EQUAL( uint64{ 2 }, FileUtil::getFileSize( FileUtil::joinPath( root, "a/b/c/two.txt" ) ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, FileUtil::getFileSize( FileUtil::joinPath( root, "a" ) ) ); // 폴더는 크기가 없다

    // 읽기 전용 파일이 있어도 폴더째 지운다.
    SW_REQUIRE_TRUE( FileUtil::setWritable( FileUtil::joinPath( root, "a/one.txt" ), false ) );
    SW_EXPECT_TRUE( FileUtil::isReadOnlyFile( FileUtil::joinPath( root, "a/one.txt" ) ) );
    SW_EXPECT_TRUE( FileUtil::removeDirectory( FileUtil::joinPath( root, "a" ) ) );
    SW_EXPECT_FALSE( FileUtil::exists( FileUtil::joinPath( root, "a" ) ) );
}

SW_TEST_CASE( FileSystemTest, WriteTimeRoundTrips )
{
    const string file = test::makeScratchPath( "stamp.txt" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( file, "x" ) );
    const int64 ticks = FileUtil::getCurrentFileWriteTime() - 3600 * FileUtil::kFileTimeTicksPerSecond;
    SW_REQUIRE_TRUE( FileUtil::setFileWriteTime( file, ticks ) );
    int64 readTicks = 0;
    SW_REQUIRE_TRUE( FileUtil::getFileWriteTime( file, readTicks ) );
    SW_EXPECT_EQUAL( ticks / FileUtil::kFileTimeTicksPerSecond, readTicks / FileUtil::kFileTimeTicksPerSecond );
}

SW_TEST_CASE( FileSystemTest, RelativePathIsTextual )
{
    const string root = test::makeScratchPath( "rel" );
    string       relative;
    SW_REQUIRE_TRUE( FileUtil::makeRelativePath( root, FileUtil::joinPath( root, "x/y.txt" ), relative ) );
    SW_EXPECT_EQUAL( string( "x/y.txt" ), relative );
    SW_REQUIRE_TRUE( FileUtil::makeRelativePath( FileUtil::joinPath( root, "x/z" ), FileUtil::joinPath( root, "w" ), relative ) );
    SW_EXPECT_EQUAL( string( "../../w" ), relative );
    SW_REQUIRE_TRUE( FileUtil::makeRelativePath( root, root, relative ) );
    SW_EXPECT_EQUAL( string( "." ), relative );
    SW_EXPECT_FALSE( FileUtil::makeRelativePath( root, "x/y.txt", relative ) ); // 상대 경로는 받지 않는다
}
