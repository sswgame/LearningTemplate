#include "pch.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/File/FileUtil.h"

#include "TestFramework/TestFramework.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
    /** @brief 이미지 범위 테스트가 주소를 쓰는 함수입니다. */
    int32 imageRangeProbe()
    {
        return 7;
    }
} // namespace

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
    const sw::string dir     = test::makeTempDirectory( "SwPathCaseTestDir" );
    const sw::string pathStr = sw::FileUtil::joinPath( dir, "MixedCaseFile.bin" );
    const sw::string content = "case-sensitive-io";

    SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathStr, reinterpret_cast<const uint8*>( content.data() ), content.size() ) );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( pathStr ) );

    sw::vector<uint8> readBuffer;
    SW_EXPECT_TRUE( sw::FileUtil::readFile( pathStr, readBuffer ) );
    SW_EXPECT_EQUAL( content, sw::string( readBuffer.begin(), readBuffer.end() ) );
}

/**
 * @brief [FileTest] createParentDirectory 는 없는 상위 폴더를 여러 단 만들고, 폴더가 없는 파일 이름에는 아무것도 하지 않는다
 * @details 에셋 셋(애니메이션 그래프 · 대화 그래프 · 시퀀스) · 전역 변수 프리셋 · 셰이더 디스크 캐시가 저장 전에 이것을 부른다.
 *          구분자가 섞여 있어도(`\\` · `/`) 만든다 — `ensureDirectoryExists` 와 같은 정규화를 거친다.
 */
SW_TEST_CASE( FileTest, CreateParentDirectoryMakesNestedFolders )
{
    const sw::string root     = test::makeTempPath( "SwParentDirTest" );
    const sw::string filePath = root + "/a\\b/c/leaf.txt";
    sw::FileUtil::createParentDirectory( filePath );
    SW_EXPECT_TRUE( sw::FileUtil::directoryExists( root + "/a/b/c" ) );
    SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( filePath, "leaf" ) );

    sw::FileUtil::createParentDirectory( "LeafWithoutFolder.txt" ); // 폴더 부분이 없다 — 아무것도 만들지 않는다
    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( root ) );
}

/**
 * @brief [FileTest] 파일 도장(크기 · 쓰기 시각)은 파일을 열지 않고 바뀜을 알린다
 * @details 내용에서 뽑은 값(셰이더 소스 해시)을 캐시하고 "그 뒤로 바뀌었나" 만 볼 때 쓴다. 크기가 달라지면 도장이 달라야 하고,
 *          없는 파일 · 폴더는 도장이 없다(false). 같은 크기의 재쓰기는 쓰기 시각에 달렸는데 그 시각의 눈금은 시스템 시계(~1 ~ 16 ms)라
 *          여기서는 크기가 다른 재쓰기로 본다.
 */
SW_TEST_CASE( FileTest, FileStampReportsSizeAndNoticesChanges )
{
    const sw::string dir  = test::makeTempDirectory( "SwFileStampTestDir" );
    const sw::string path = sw::FileUtil::joinPath( dir, "Stamp.txt" );

    const sw::string shortText = "0123456789";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, reinterpret_cast<const uint8*>( shortText.data() ), shortText.size() ) );
    sw::FileStamp first{};
    SW_ASSERT_TRUE( sw::FileUtil::getFileStamp( path, first ) );
    SW_EXPECT_EQUAL( uint64( 10 ), first._size );
    SW_EXPECT_TRUE( first._writeTime != 0u );

    sw::FileStamp again{};
    SW_ASSERT_TRUE( sw::FileUtil::getFileStamp( path, again ) );
    SW_EXPECT_TRUE( first == again );

    const sw::string longText = "0123456789abcdefghij";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, reinterpret_cast<const uint8*>( longText.data() ), longText.size() ) );
    sw::FileStamp second{};
    SW_ASSERT_TRUE( sw::FileUtil::getFileStamp( path, second ) );
    SW_EXPECT_EQUAL( uint64( 20 ), second._size );
    SW_EXPECT_TRUE( first != second );

    sw::FileStamp none{};
    SW_EXPECT_FALSE( sw::FileUtil::getFileStamp( sw::FileUtil::joinPath( dir, "Missing.txt" ), none ) );
    SW_EXPECT_FALSE( sw::FileUtil::getFileStamp( dir, none ) );
    SW_EXPECT_FALSE( sw::FileUtil::getFileStamp( "", none ) );
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
    const sw::string rootDir = test::makeTempPath( "SwCollectCaseRoot" );
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
}

/**
 * @brief [FileTest] 파일 쓰기와 읽기
 */
SW_TEST_CASE( FileTest, WriteAndReadFile )
{
    sw::string testPath    = test::makeTempPath( "test_output_temp.bin" );
    sw::string testContent = "Hello C++ Workspace!";

    bool writeOk = sw::FileUtil::writeFile( testPath, reinterpret_cast<const uint8*>( testContent.data() ), testContent.size() );
    SW_EXPECT_TRUE( writeOk );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( testPath ) );

    sw::vector<uint8> readBuffer;
    bool              readOk = sw::FileUtil::readFile( testPath, readBuffer );
    SW_EXPECT_TRUE( readOk );

    sw::string readContent( readBuffer.begin(), readBuffer.end() );
    SW_EXPECT_EQUAL( testContent, readContent );
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

/**
 * @brief [FileTest] 디렉터리 순회가 재귀·비재귀 모두 같은 규칙으로 걷는지 검증
 * @details `collectFiles` · `collectFolders` 는 같은 순회를 **네 벌**(파일/폴더 × 재귀/비재귀)로
 *          따로 적고 있었고, 넷 다 `std::error_code` 없이 순회자를 만들었다 — 권한이 없는 폴더나
 *          순회 중 지워진 폴더에서 **예외를 던진다.** 두 함수는 `bool` 로 실패를 알리는 약속이라
 *          그 예외가 호출부를 뚫고 나간다. 같은 파일의 `makeRelativePath` · `makeAbsolutePath` 는
 *          처음부터 `error_code` 를 받고 있었다.
 *
 *          넷을 헬퍼 하나로 모았으므로, 여기서는 그 헬퍼가 네 경우를 모두 예전과 같이 걷는지 본다.
 */
SW_TEST_CASE( FileTest, DirectoryWalkCollectsFilesAndFolders )
{
    const sw::string rootDir = test::makeTempPath( "sw_walk_root" );
    const sw::string subDir  = sw::FileUtil::joinPath( rootDir, "nested" );
    sw::FileUtil::ensureDirectoryExists( subDir );

    const sw::string topFile    = sw::FileUtil::joinPath( rootDir, "top.txt" );
    const sw::string nestedFile = sw::FileUtil::joinPath( subDir, "deep.txt" );
    const sw::string otherFile  = sw::FileUtil::joinPath( rootDir, "skip.bin" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( topFile, "a" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( nestedFile, "b" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( otherFile, "c" ) );

    BLOCK( "비재귀 파일 — 바로 아래만, 확장자 필터가 걸린다" )
    {
        sw::vector<sw::string> listFile;
        SW_EXPECT_TRUE( sw::FileUtil::collectFiles( rootDir, ".txt", listFile, false ) );
        SW_EXPECT_EQUAL( size_t( 1 ), listFile.size() );
    }

    BLOCK( "재귀 파일 — 하위까지 본다" )
    {
        sw::vector<sw::string> listFile;
        SW_EXPECT_TRUE( sw::FileUtil::collectFiles( rootDir, ".txt", listFile, true ) );
        SW_EXPECT_EQUAL( size_t( 2 ), listFile.size() );
    }

    BLOCK( "필터가 비면 전부" )
    {
        sw::vector<sw::string> listFile;
        SW_EXPECT_TRUE( sw::FileUtil::collectFiles( rootDir, "", listFile, true ) );
        SW_EXPECT_EQUAL( size_t( 3 ), listFile.size() );
    }

    BLOCK( "폴더 — 비재귀와 재귀" )
    {
        sw::vector<sw::string> listFolder;
        SW_EXPECT_TRUE( sw::FileUtil::collectFolders( rootDir, listFolder, false ) );
        SW_EXPECT_EQUAL( size_t( 1 ), listFolder.size() );

        listFolder.clear();
        SW_EXPECT_TRUE( sw::FileUtil::collectFolders( rootDir, listFolder, true ) );
        SW_EXPECT_EQUAL( size_t( 1 ), listFolder.size() );
    }

    BLOCK( "없는 디렉터리는 false — 던지지 않는다" )
    {
        sw::vector<sw::string> listFile;
        SW_EXPECT_FALSE( sw::FileUtil::collectFiles( sw::FileUtil::joinPath( rootDir, "nope" ), "", listFile, true ) );
        SW_EXPECT_TRUE( listFile.empty() );
    }
}

/**
 * @brief [FileTest] findLoadedImageRange 는 주소를 담은 실행 이미지의 범위를 준다
 * @details 핫 리로드가 "이 코드가 내리려는 모듈의 것인가" 를 가리는 기준이다. 범위가 주소를 담고, 잘못된 주소는 거절한다.
 */
SW_TEST_CASE( FileTest, LoadedImageRangeContainsTheAddress )
{
    const void* pProbe = reinterpret_cast<const void*>( &imageRangeProbe );
    const void* pBegin{ nullptr };
    const void* pEnd{ nullptr };
    SW_ASSERT_TRUE( sw::FileUtil::findLoadedImageRange( pProbe, pBegin, pEnd ) );

    const uintptr_t probe       = reinterpret_cast<uintptr_t>( pProbe );
    const bool      bContainsIt = reinterpret_cast<uintptr_t>( pBegin ) <= probe && probe < reinterpret_cast<uintptr_t>( pEnd );
    SW_EXPECT_TRUE( bContainsIt );
    SW_EXPECT_EQUAL( 7, imageRangeProbe() );

    SW_EXPECT_FALSE( sw::FileUtil::findLoadedImageRange( nullptr, pBegin, pEnd ) );
}

/**
 * @brief [FileTest] 쓰기는 원자적이다: 다 쓴 뒤 바꿔 끼우고, 같은 폴더에 임시 파일을 남기지 않는다.
 * @details 예전 `writeTextFile` 은 원본을 "wb" 로 열어 그 자리에 쓰고 결과를 보지 않았다. 도중에 실패하면 원본이 빈 파일로 남았다.
 */
SW_TEST_CASE( FileTest, WriteReplacesAtomicallyAndLeavesNoTemporaryFile )
{
    const sw::string dir = test::makeTempPath( "SwAtomicWriteTest" );
    sw::FileUtil::removeDirectory( dir );
    sw::FileUtil::ensureDirectoryExists( dir );
    const sw::string filePath = sw::FileUtil::joinPath( dir, "scene.xml" );

    SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( filePath, "first" ) );
    SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( filePath, "second-longer" ) );
    SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( filePath, "3" ) );

    sw::string text;
    SW_EXPECT_TRUE( sw::FileUtil::readTextFile( filePath, text ) );
    SW_EXPECT_EQUAL( sw::string( "3" ), text );

    sw::vector<sw::string> listFile;
    SW_EXPECT_TRUE( sw::FileUtil::collectFiles( dir, "", listFile, false ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listFile.size() ) );

    // 빈 글자도 빈 파일로 쓴다.
    SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( filePath, "" ) );
    SW_EXPECT_TRUE( sw::FileUtil::getFileSize( filePath ) == 0 );

    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( dir ) );
}

/**
 * @brief [FileTest] 쓰기가 실패하면 false 이고 원본은 그대로다.
 * @details 대상 자리에 폴더가 있으면 바꿔 끼울 수 없다. 예전 `writeTextFile` 은 어떤 실패에도 true 를 돌려줬다.
 */
SW_TEST_CASE( FileTest, FailedWriteReportsFalseAndKeepsOriginal )
{
    const sw::string dir = test::makeTempPath( "SwFailedWriteTest" );
    sw::FileUtil::removeDirectory( dir );
    const sw::string blockedPath = sw::FileUtil::joinPath( dir, "blocked" );
    sw::FileUtil::ensureDirectoryExists( sw::FileUtil::joinPath( blockedPath, "child" ) );

    SW_EXPECT_FALSE( sw::FileUtil::writeTextFile( blockedPath, "must-not-land" ) );
    SW_EXPECT_TRUE( sw::FileUtil::directoryExists( sw::FileUtil::joinPath( blockedPath, "child" ) ) );

    // 폴더 안에 임시 파일을 남기지 않았다.
    sw::vector<sw::string> listFile;
    sw::FileUtil::collectFiles( dir, "", listFile, false );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( listFile.size() ) );

    // 없는 폴더에 쓰기도 false 다.
    SW_EXPECT_FALSE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( dir, "missing/leaf.txt" ), "x" ) );

    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( dir ) );
}

/**
 * @brief [FileTest] 한글이 들어간 경로도 쓰기 · 읽기 · 존재 확인 · 폴더 훑기가 같은 파일을 본다.
 * @details Windows 에서 좁은 문자 경로는 ANSI 코드 페이지(CP949)로 해석됐다. 읽기는 UTF-16 으로 바꿔 열어서, 사용자 폴더 이름이 한글이면
 *          `fileExists` 는 있다고 하는데 읽기는 "File not found" 였다. 실행 파일 매니페스트(activeCodePage=UTF-8)와 UTF-16 열기로 고쳤다.
 */
SW_TEST_CASE( FileTest, NonAsciiPathRoundTrips )
{
    const sw::string dir = test::makeTempPath( "SwUnicodePath_한글폴더" );
    sw::FileUtil::removeDirectory( dir );
    sw::FileUtil::ensureDirectoryExists( dir );
    SW_ASSERT_TRUE( sw::FileUtil::directoryExists( dir ) );

    const sw::string filePath = sw::FileUtil::joinPath( dir, "세이브_1.txt" );
    SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( filePath, "저장됨" ) );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( filePath ) );

    sw::string text;
    SW_EXPECT_TRUE( sw::FileUtil::readTextFile( filePath, text ) );
    SW_EXPECT_EQUAL( sw::string( "저장됨" ), text );

    sw::vector<sw::string> listFile;
    SW_EXPECT_TRUE( sw::FileUtil::collectFiles( dir, "txt", listFile, false ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listFile.size() ) );
    SW_EXPECT_TRUE( sw::FileUtil::getFileNamePart( listFile[0] ) == "세이브_1.txt" );

    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( dir ) );
}

/**
 * @brief [FileTest] 범위 읽기의 오프셋 · 길이는 64 비트다(예전엔 uint32 라 4 GB 이상 파일이 조용히 잘렸다). 파일 끝을 넘는 오프셋은 실패다.
 */
SW_TEST_CASE( FileTest, ReadRangeRespectsOffsetAndRejectsPastEnd )
{
    const sw::string dir      = test::makeTempDirectory( "SwReadRangeTest" );
    const sw::string filePath = sw::FileUtil::joinPath( dir, "range.bin" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( filePath, "0123456789" ) );

    sw::vector<uint8> bytes;
    SW_EXPECT_TRUE( sw::FileUtil::readFile( filePath, bytes, 3ull, 4ull ) );
    SW_EXPECT_EQUAL( sw::string( "3456" ), sw::string( bytes.begin(), bytes.end() ) );

    SW_EXPECT_TRUE( sw::FileUtil::readFile( filePath, bytes, 8ull ) );
    SW_EXPECT_EQUAL( sw::string( "89" ), sw::string( bytes.begin(), bytes.end() ) );

    SW_EXPECT_FALSE( sw::FileUtil::readFile( filePath, bytes, 11ull ) );

    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( dir ) );
}

#if defined( SW_PLATFORM_WINDOWS )
/**
 * @brief [FileTest] 이 프로세스의 ANSI 코드 페이지는 UTF-8 이다(실행 파일 매니페스트 activeCodePage).
 * @details 엔진의 좁은 문자 문자열은 UTF-8 이다. 매니페스트가 빠지면 fopen · std::filesystem · CreateFileA · argv 가 다시 CP949 로
 *          해석되고, 한글 경로가 조용히 깨진다. 매니페스트를 붙이는 CMake 함수(sw_embedProcessManifest)가 빠진 실행 파일을 여기서 잡는다.
 */
SW_TEST_CASE( FileTest, ProcessUsesUtf8AnsiCodePage )
{
    SW_EXPECT_EQUAL( static_cast<uint32>( CP_UTF8 ), static_cast<uint32>( GetACP() ) );
}
#endif

/**
 * @brief [FileTest] 다른 스레드가 읽는 동안 같은 파일을 계속 덮어써도, 읽는 쪽은 반쯤 쓴 파일을 한 번도 보지 않는다.
 * @details 예전 쓰기는 원본을 "wb" 로 열어(길이 0) 그 자리에 썼다. 그 사이에 읽으면 빈 파일이나 앞부분만 있는 파일을 "정상으로" 읽었다 —
 *          에디터가 저장하는 순간 핫 리로드 · 파일 감시가 읽으면 그렇게 됐다. 지금은 다 쓴 뒤 바꿔 끼우므로 읽는 쪽은 옛 파일 전체나 새 파일
 *          전체만 본다.
 *
 *          읽기가 쓰기와 **겹쳐야** 시험이 된다. 예전에는 읽는 스레드를 띄우자마자 60 번을 썼는데, 붐비는 CI 러너(ctest 병렬)에서 그
 *          스레드가 쓰기가 다 끝날 때(65 ms)까지 한 번도 돌지 못해 "읽은 적 없음" 으로 졌다(2026-10-01, Windows Debug — 읽기 오류 줄도
 *          없었다). 이제 읽는 쪽이 돌기 시작한 뒤에 쓰고, 쓰는 동안 겹친 읽기를 세어 모자라면 더 쓴다.
 */
SW_TEST_CASE( FileTest, ReadersNeverObserveHalfWrittenFile )
{
    const sw::string dir      = test::makeTempDirectory( "SwTornWriteTest" );
    const sw::string filePath = sw::FileUtil::joinPath( dir, "shared.bin" );

    constexpr size_t  kFileBytes = 256 * 1024;
    sw::vector<uint8> bytesA( kFileBytes, static_cast<uint8>( 'A' ) );
    sw::vector<uint8> bytesB( kFileBytes, static_cast<uint8>( 'B' ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( filePath, bytesA.data(), bytesA.size() ) );

    std::atomic<bool>   bWriterDone{ false };
    std::atomic<uint32> tornReadCount{ 0 };
    std::atomic<uint32> goodReadCount{ 0 };
    std::atomic<uint32> attemptCount{ 0 };
    std::thread         reader( [&]()
    {
        sw::vector<uint8> readBytes;
        while ( bWriterDone.load() == false )
        {
            const bool bRead = sw::FileUtil::readFile( filePath, readBytes );
            attemptCount.fetch_add( 1 );
            if ( bRead == false )
                continue;
            const bool bWholeFile = readBytes.size() == kFileBytes && ( readBytes.front() == 'A' || readBytes.front() == 'B' ) &&
                                    readBytes.back() == readBytes.front();
            if ( bWholeFile )
                goodReadCount.fetch_add( 1 );
            else
                tornReadCount.fetch_add( 1 );
        }
    } );

    // 읽는 쪽이 돌기 시작한 뒤에 쓴다(위 설명).
    const auto waitStart = std::chrono::steady_clock::now();
    while ( attemptCount.load() == 0 && std::chrono::steady_clock::now() - waitStart < std::chrono::seconds( 10 ) )
        std::this_thread::yield();
    const uint32 readBeforeWrite = goodReadCount.load() + tornReadCount.load();

    // 쓰는 동안 겹친 읽기가 몇 번은 있어야 한다 — 60 번을 쓰고도 모자라면 더 쓴다(상한이 있다).
    constexpr uint32 kMinOverlappedRead = 5;
    for ( uint32 writeIndex = 0; writeIndex < 2000; ++writeIndex )
    {
        if ( writeIndex >= 60 && goodReadCount.load() + tornReadCount.load() - readBeforeWrite >= kMinOverlappedRead )
            break;
        const sw::vector<uint8>& bytes = ( writeIndex % 2 == 0 ) ? bytesB : bytesA;
        SW_EXPECT_TRUE( sw::FileUtil::writeFile( filePath, bytes.data(), bytes.size() ) );
    }
    const uint32 overlappedRead = goodReadCount.load() + tornReadCount.load() - readBeforeWrite;
    bWriterDone.store( true );
    reader.join();

    SW_EXPECT_EQUAL( 0u, tornReadCount.load() );
    SW_EXPECT_TRUE_MSG( overlappedRead >= kMinOverlappedRead, "읽기가 쓰기와 거의 겹치지 않았다 — 아무것도 시험하지 않은 것이다" );
    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( dir ) );
}
