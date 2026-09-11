#include "pch.h"

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

    SW_EXPECT_TRUE( sw::FileUtil::isAbsolutePath( "C:/Projects/Sample/TestFile.txt" ) );
    SW_EXPECT_TRUE( sw::FileUtil::isAbsolutePath( "c:\\projects\\sample\\testfile.txt" ) );
    SW_EXPECT_TRUE( sw::FileUtil::isAbsolutePath( "/home/runner/work/scene.xml" ) );
    SW_EXPECT_TRUE( sw::FileUtil::isAbsolutePath( "\\\\server\\share\\file.txt" ) );
    SW_EXPECT_FALSE( sw::FileUtil::isAbsolutePath( "Projects/Sample/TestFile.txt" ) );
    SW_EXPECT_FALSE( sw::FileUtil::isAbsolutePath( "TestFile.txt" ) );
    SW_EXPECT_FALSE( sw::FileUtil::isAbsolutePath( "" ) );
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
 * @brief [Core_File] 디렉터리 수집이 경로 대소문자 유지
 * @details collectFiles/collectFolders 는 **실제 파일시스템을 훑어** 경로를 만든다. 그래서 돌려준 경로는
 *          그대로 열 수 있어야 한다. 예전엔 결과를 normalizePath 로 통째 소문자화해서, 대소문자를 가리는
 *          파일시스템(리눅스 CI)에서는 상위 디렉터리 이름(`/home/runner/work/LearningTemplate/.../Resource`)
 *          까지 소문자가 되어 열거한 파일을 곧바로 "File not found" 로 되돌려줬다. 윈도우에서는 파일이
 *          그냥 열려서 드러나지 않았으므로, 여기서는 **문자열이 만든 그대로인지**를 본다.
 */
SW_TEST_CASE( Core_File, CollectPreservesPathCase )
{
    const sw::string rootDir = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "SwCollectCaseRoot" );
    const sw::string subDir  = sw::FileUtil::joinPath( rootDir, "MixedCaseSub" );
    sw::FileUtil::removeDirectory( rootDir ); // 앞 실행이 죽어 남긴 찌꺼기가 개수 단언을 흔들지 않게 한다
    sw::FileUtil::ensureDirectoryExists( subDir );

    const sw::string filePath = sw::FileUtil::joinPath( subDir, "MixedCaseAsset.Bin" );
    const sw::string content  = "collect-case";
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( filePath, reinterpret_cast<const uint8*>( content.data() ), content.size() ) );

    sw::vector<sw::string> listFolder;
    SW_EXPECT_TRUE( sw::FileUtil::collectFolders( rootDir, listFolder, false ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listFolder.size() ) );
    if ( listFolder.empty() == false )
        SW_EXPECT_EQUAL( subDir, listFolder[0] );

    sw::vector<sw::string> listFile;
    SW_EXPECT_TRUE( sw::FileUtil::collectFiles( rootDir, ".bin", listFile, true ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listFile.size() ) );
    if ( listFile.empty() == false )
    {
        // 수집한 경로를 **가공 없이** 다시 열 수 있어야 한다 — 이게 이 함수의 계약이다.
        SW_EXPECT_EQUAL( filePath, listFile[0] );
        sw::vector<uint8> readBuffer;
        SW_EXPECT_TRUE( sw::FileUtil::readFile( listFile[0], readBuffer ) );
        SW_EXPECT_EQUAL( content, sw::string( readBuffer.begin(), readBuffer.end() ) );
    }

    sw::FileUtil::removeFile( filePath );
    sw::FileUtil::removeDirectory( rootDir );
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
