#include "pch.h"

#include "Core/File/BinaryBlob.h"
#include "Core/File/FileUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_File — 경로·읽기쓰기·바이너리블롭
// ------------------------------------------------------------------------------
/**
 * @brief [Core_File] FileUtil 경로 동작
 */

SW_TEST_CASE( Core_File, FileUtilPathOperations )
{
    sw::string fullPath = "Projects/Sample/TestFile.txt";
    sw::string fileName = sw::FileUtil::getFileNamePart( fullPath );
    SW_EXPECT_EQUAL( sw::string( "TestFile.txt" ), fileName );

    sw::string dirName = sw::FileUtil::getDirectoryPart( fullPath );
    SW_EXPECT_FALSE( dirName.empty() );
    SW_EXPECT_TRUE( dirName.find( "Sample" ) != sw::string::npos );

    sw::string noExt = sw::FileUtil::removeExtension( fullPath );
    SW_EXPECT_EQUAL( sw::string( "Projects/Sample/TestFile" ), noExt );

    sw::string newExt = sw::FileUtil::replaceExtension( fullPath, "bin" );
    SW_EXPECT_EQUAL( sw::string( "Projects/Sample/TestFile.bin" ), newExt );

    SW_EXPECT_EQUAL( sw::string( "Foo/Bar" ), sw::FileUtil::normalizeSeparators( "Foo\\Bar" ) );
    SW_EXPECT_EQUAL( sw::string( "foo/bar" ), sw::FileUtil::normalizePath( "Foo\\Bar" ) );
    SW_EXPECT_TRUE( sw::FileUtil::pathsEqualNormalized( "Foo/Bar", "foo/bar" ) );
}

/**
 * @brief [Core_File] 읽기/쓰기가 경로 대소문자 유지
 */
SW_TEST_CASE( Core_File, ReadWritePreservesPathCase )
{
    const sw::string dir = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "SwPathCaseTestDir" );
    sw::FileUtil::ensureDirectoryExists( dir );
    const sw::string pathStr = sw::FileUtil::joinPath( dir, "MixedCaseFile.bin" );
    const sw::string content = "case-sensitive-io";

    SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathStr, reinterpret_cast<const uint8*>( content.data() ), content.size() ) );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( pathStr ) );

    sw::vector<uint8> readBuffer;
    SW_EXPECT_TRUE( sw::FileUtil::readFile( pathStr, readBuffer ) );
    SW_EXPECT_EQUAL( content, sw::string( readBuffer.begin(), readBuffer.end() ) );

    sw::FileUtil::removeFile( pathStr );
}

/**
 * @brief [Core_File] 파일 쓰기와 읽기
 */
SW_TEST_CASE( Core_File, WriteAndReadFile )
{
    sw::string testPath    = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_output_temp.bin" );
    sw::string testContent = "Hello C++ Workspace!";

    bool writeOk = sw::FileUtil::writeFile( testPath, reinterpret_cast<const uint8*>( testContent.data() ), testContent.size() );
    SW_EXPECT_TRUE( writeOk );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( testPath ) );

    sw::vector<uint8> readBuffer;
    bool              readOk = sw::FileUtil::readFile( testPath, readBuffer );
    SW_EXPECT_TRUE( readOk );

    sw::string readContent( readBuffer.begin(), readBuffer.end() );
    SW_EXPECT_EQUAL( testContent, readContent );

    sw::FileUtil::removeFile( testPath );
}

/**
 * @brief [Core_File] BinaryBlob 리틀엔디언 u32/i32 및 문자열 blob append/read 검증
 */
SW_TEST_CASE( Core_File, BinaryBlobReadWritePrimitives )
{
    sw::vector<uint8> listBlob;

    // 1) Write primitives
    sw::BinaryBlob::appendU32( listBlob, 0x12345678u );
    sw::BinaryBlob::appendI32( listBlob, -98765 );
    sw::BinaryBlob::appendString( listBlob, "BinaryBlobPayload" );

    SW_EXPECT_FALSE( listBlob.empty() );

    // 2) Read primitives
    size_t     offset = 0;
    uint32     outU32 = 0;
    int32      outI32 = 0;
    sw::string outStr;

    SW_EXPECT_TRUE( sw::BinaryBlob::readU32( listBlob, offset, outU32 ) );
    SW_EXPECT_EQUAL( 0x12345678u, outU32 );
    SW_EXPECT_EQUAL( 4u, offset );

    SW_EXPECT_TRUE( sw::BinaryBlob::readI32( listBlob, offset, outI32 ) );
    SW_EXPECT_EQUAL( -98765, outI32 );
    SW_EXPECT_EQUAL( 8u, offset );

    SW_EXPECT_TRUE( sw::BinaryBlob::readString( listBlob, offset, outStr ) );
    SW_EXPECT_EQUAL( sw::string( "BinaryBlobPayload" ), outStr );
    SW_EXPECT_EQUAL( listBlob.size(), offset );

    // 3) Out of range read failure
    SW_EXPECT_FALSE( sw::BinaryBlob::readU32( listBlob, offset, outU32 ) );
    SW_EXPECT_FALSE( sw::BinaryBlob::readI32( listBlob, offset, outI32 ) );
    SW_EXPECT_FALSE( sw::BinaryBlob::readString( listBlob, offset, outStr ) );
}
