#include "pch.h"

#include "Core/File/FileUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_File — 경로·읽기쓰기·바이너리블롭
// ------------------------------------------------------------------------------
/**
 * @brief [FileTest] FileUtil 경로 동작
 */

SW_TEST_CASE( FileTest, FileUtilPathOperations )
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
 * @brief [FileTest] 읽기/쓰기가 경로 대소문자 유지
 */
SW_TEST_CASE( FileTest, ReadWritePreservesPathCase )
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
 * @brief [FileTest] 디렉터리 수집이 경로 대소문자 유지
 * @details collectFiles/collectFolders 는 **실제 파일시스템을 훑어** 경로를 만든다. 그래서 돌려준 경로는
 *          그대로 열 수 있어야 한다. 예전엔 결과를 normalizePath 로 통째 소문자화해서, 대소문자를 가리는
 *          파일시스템(리눅스 CI)에서는 상위 디렉터리 이름(`/home/runner/work/LearningTemplate/.../Resource`)
 *          까지 소문자가 되어 열거한 파일을 곧바로 "File not found" 로 되돌려줬다. 윈도우에서는 파일이
 *          그냥 열려서 드러나지 않았으므로, 여기서는 **문자열이 만든 그대로인지**를 본다.
 */
SW_TEST_CASE( FileTest, CollectPreservesPathCase )
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
 * @brief [FileTest] 파일 쓰기와 읽기
 */
SW_TEST_CASE( FileTest, WriteAndReadFile )
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
 * @brief [FileTest] 실행 파일 경로는 **실제로 있는 파일**을 가리킨다
 * @details 윈도우의 `GetModuleFileNameW` 는 버퍼가 모자라면 **잘라서** 돌려주고 그 사실을
 *          반환값으로만 알린다. 예전에는 260자 고정 버퍼에 담고 반환값을 보지 않았다 — 그보다
 *          깊은 경로에 설치되면 exe 위치가 조용히 틀려지고, 그 자리를 기준으로 찾는 모듈 DLL ·
 *          RHI 백엔드 · 셰이더 · 리소스가 **전부** "없다" 가 된다. 원인은 어디에도 남지 않는다.
 *
 *          260자를 넘는 설치 경로를 여기서 만들어 낼 수는 없으므로, 이 검사는 **정상 경로가
 *          여전히 온전한지**를 본다(잘림 처리를 잘못 넣으면 여기서 빈 문자열이나 깨진 경로가
 *          나온다). 실제로 이 저장소의 테스트 상당수가 이 함수로 Bin 폴더를 찾으므로, 망가지면
 *          그쪽이 먼저 무너진다.
 */
SW_TEST_CASE( FileTest, ExecutablePathPointsAtARealFile )
{
    const sw::string executablePath = sw::FileUtil::getExecutablePath();

    SW_ASSERT_TRUE( executablePath.empty() == false );
    SW_EXPECT_TRUE_MSG( sw::FileUtil::fileExists( executablePath ),
                        "실행 파일 경로가 없는 파일을 가리킵니다 — 잘렸거나 깨졌습니다" );
    SW_EXPECT_TRUE_MSG( sw::FileUtil::isAbsolutePath( executablePath ), "실행 파일 경로가 절대 경로가 아닙니다" );

    // 디렉터리 부분도 실제로 있어야 한다 — 엔진이 리소스·모듈을 찾는 기준점이다.
    const sw::string executableDirectory = sw::FileUtil::getDirectoryPart( executablePath );
    SW_EXPECT_TRUE_MSG( sw::FileUtil::directoryExists( executableDirectory ),
                        "실행 파일의 디렉터리가 없습니다" );

    // 두 번 물어도 같은 답이어야 한다(버퍼를 키우는 루프가 상태를 남기지 않는다).
    SW_EXPECT_TRUE( sw::FileUtil::getExecutablePath() == executablePath );
}
