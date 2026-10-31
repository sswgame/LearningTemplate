#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Testing/TestFramework.h"

// 파일 도우미 — 경로 문자열(이름 · 폴더 · 확장자 · 절대 판정), 읽기 · 쓰기(대소문자 · 한글 경로 · 원자적 바꿔 끼우기 · 실패 · 범위),
// 폴더 만들기 · 막힌 폴더, 스탬프, 수집 · 순회, 읽기 전용, 대소문자 무시 풀기, 덧붙이기, 쓰기 게이트.

namespace
{
    /** @brief 모든 쓰기를 거절하는 쓰기 게이트입니다(`FileUtil::setWriteGate`). */
    bool refuseEveryWrite( sw::string_view /*path*/, sw::string& outReason )
    {
        outReason = "test gate refuses every write";
        return false;
    }
} // namespace

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
 * @brief [FileTest] hasExtension 은 한 단계 확장자와 두 단계 접미사를 모두 맞춘다
 */
SW_TEST_CASE( FileTest, HasExtensionMatchesCompoundSuffix )
{
    SW_EXPECT_TRUE( sw::FileUtil::hasExtension( "a/b/Hull.mesh", ".mesh" ) );
    SW_EXPECT_TRUE( sw::FileUtil::hasExtension( "a/b/Hull.MESH", "mesh" ) );
    SW_EXPECT_FALSE( sw::FileUtil::hasExtension( "a/b.dir/Hull", ".mesh" ) );
    // 두 단계 접미사 — 마지막 점 뒤만 보면 영영 맞지 않던 경우
    SW_EXPECT_TRUE( sw::FileUtil::hasExtension( "a/b/Hull.modifiers.json", ".modifiers.json" ) );
    SW_EXPECT_TRUE( sw::FileUtil::hasExtension( "a\\b\\Hull.Modifiers.JSON", "modifiers.json" ) );
    SW_EXPECT_TRUE( sw::FileUtil::hasExtension( "Hull.v2.modifiers.json", ".modifiers.json" ) );
    SW_EXPECT_FALSE( sw::FileUtil::hasExtension( "a/b/Hull.json", ".modifiers.json" ) );
    SW_EXPECT_FALSE( sw::FileUtil::hasExtension( "a/b/Hullmodifiers.json", ".modifiers.json" ) );
    SW_EXPECT_FALSE( sw::FileUtil::hasExtension( "a.modifiers/Hull.json", ".modifiers.json" ) );
    SW_EXPECT_FALSE( sw::FileUtil::hasExtension( "modifiers.json", ".modifiers.json" ) ); // 이름이 접미사 자체뿐이면 확장자가 아니다
    SW_EXPECT_TRUE( sw::FileUtil::hasAnyExtension( "x.prefab.xml", { ".png", ".prefab.xml" } ) );
}

/**
 * @brief [FileTest] 읽기/쓰기가 경로 대소문자 유지
 */
SW_TEST_CASE( FileTest, ReadWritePreservesPathCase )
{
    const sw::string dir = sw::test::makeScratchPath( "SwPathCaseTestDir" );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( dir ) );
    const sw::string pathStr = sw::FileUtil::joinPath( dir, "MixedCaseFile.bin" );
    const sw::string content = "case-sensitive-io";

    SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathStr, reinterpret_cast<const uint8*>( content.data() ), content.size() ) );
    SW_EXPECT_TRUE( sw::FileUtil::exists( pathStr ) );

    sw::vector<uint8> readBuffer;
    SW_EXPECT_TRUE( sw::FileUtil::readFile( pathStr, readBuffer ) );
    SW_EXPECT_EQUAL( content, sw::string( readBuffer.begin(), readBuffer.end() ) );
}

/**
 * @brief [FileTest] ensureParentDirectoryExists 는 없는 상위 폴더를 여러 단 만들고, 폴더가 없는 파일 이름에는 아무것도 하지 않는다
 * @details 에셋 셋(애니메이션 그래프 · 대화 그래프 · 시퀀스) · 전역 변수 프리셋 · 셰이더 디스크 캐시가 저장 전에 이것을 부른다.
 *          구분자가 섞여 있어도(`\\` · `/`) 만든다 — `ensureDirectoryExists` 와 같은 정규화를 거친다.
 */
SW_TEST_CASE( FileTest, CreateParentDirectoryMakesNestedFolders )
{
    const sw::string root     = sw::test::makeScratchPath( "SwParentDirTest" );
    const sw::string filePath = root + "/a\\b/c/leaf.txt";
    SW_EXPECT_TRUE( sw::FileUtil::ensureParentDirectoryExists( filePath ) );
    SW_EXPECT_TRUE( sw::FileUtil::isDirectory( root + "/a/b/c" ) );
    SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( filePath, "leaf" ) );

    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( root ) );
}

/**
 * @brief [FileTest] 폴더를 만들지 못하면 false 이고 어느 폴더인지 알린다 — 길을 파일이 막고 있을 때
 * @details 실패를 돌려주지 않으면 뒤따르는 쓰기가 "임시 파일을 열 수 없다" 로만 실패해 막힌 폴더를 알 수 없다.
 */
SW_TEST_CASE( FileTest, DirectoryThatCannotBeCreatedSaysSo )
{
    const sw::string root    = sw::test::makeScratchPath( "SwBlockedDirTest" );
    const sw::string blocker = sw::FileUtil::joinPath( root, "blocker" );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( blocker, "a file, not a folder" ) );

    SW_EXPECT_LOG( "File.CreateDirectoryFailed" ); // 어느 폴더를 만들지 못했는지 남긴다
    SW_EXPECT_FALSE( sw::FileUtil::ensureDirectoryExists( sw::FileUtil::joinPath( blocker, "child" ) ) );
    SW_EXPECT_FALSE( sw::FileUtil::ensureParentDirectoryExists( sw::FileUtil::joinPath( blocker, "child/leaf.txt" ) ) );
}

/**
 * @brief [FileTest] 파일 스탬프(크기 · 쓰기 시각)은 파일을 열지 않고 바뀜을 알린다
 * @details 내용에서 뽑은 값(셰이더 소스 해시)을 캐시하고 "그 뒤로 바뀌었나" 만 볼 때 쓴다. 크기가 달라지면 스탬프가 달라야 하고,
 *          없는 파일 · 폴더는 스탬프가 없다(false). 같은 크기의 재쓰기는 쓰기 시각에 달렸는데 그 시각의 눈금은 시스템 시계(~1 ~ 16 ms)라
 *          여기서는 크기가 다른 재쓰기로 본다.
 */
SW_TEST_CASE( FileTest, FileStampReportsSizeAndNoticesChanges )
{
    const sw::string dir = sw::test::makeScratchPath( "SwFileStampTestDir" );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( dir ) );
    const sw::string path = sw::FileUtil::joinPath( dir, "Stamp.txt" );

    const sw::string shortText = "0123456789";
    SW_REQUIRE_TRUE( sw::FileUtil::writeFile( path, reinterpret_cast<const uint8*>( shortText.data() ), shortText.size() ) );
    sw::FileStamp first{};
    SW_REQUIRE_TRUE( sw::FileUtil::getFileStamp( path, first ) );
    SW_EXPECT_EQUAL( uint64( 10 ), first._sizeBytes );
    SW_EXPECT_TRUE( first._writeTime != 0u );

    sw::FileStamp again{};
    SW_REQUIRE_TRUE( sw::FileUtil::getFileStamp( path, again ) );
    SW_EXPECT_TRUE( first == again );

    const sw::string longText = "0123456789abcdefghij";
    SW_REQUIRE_TRUE( sw::FileUtil::writeFile( path, reinterpret_cast<const uint8*>( longText.data() ), longText.size() ) );
    sw::FileStamp second{};
    SW_REQUIRE_TRUE( sw::FileUtil::getFileStamp( path, second ) );
    SW_EXPECT_EQUAL( uint64( 20 ), second._sizeBytes );
    SW_EXPECT_TRUE( first != second );

    sw::FileStamp none{};
    SW_EXPECT_FALSE( sw::FileUtil::getFileStamp( sw::FileUtil::joinPath( dir, "Missing.txt" ), none ) );
    SW_EXPECT_FALSE( sw::FileUtil::getFileStamp( dir, none ) );
}

/**
 * @brief [FileTest] 디렉터리 수집이 경로 대소문자 유지
 * @details collectFiles/collectFolders 는 **실제 파일시스템을 훑어** 경로를 만든다. 그래서 돌려준 경로는
 *          그대로 열 수 있어야 한다. 결과를 normalizePath 로 통째 소문자화하면, 대소문자를 가리는
 *          파일시스템(리눅스 CI)에서는 상위 디렉터리 이름(`/home/runner/work/LearningTemplate/.../Resource`)
 *          까지 소문자가 되어 열거한 파일이 곧바로 "File not found" 가 된다. 윈도우에서는 파일이
 *          그냥 열려서 드러나지 않으므로, 여기서는 **문자열이 만든 그대로인지**를 본다.
 */
SW_TEST_CASE( FileTest, CollectPreservesPathCase )
{
    const sw::string rootDir = sw::test::makeScratchPath( "SwCollectCaseRoot" );
    const sw::string subDir  = sw::FileUtil::joinPath( rootDir, "MixedCaseSub" );
    SW_EXPECT_TRUE( sw::FileUtil::ensureDirectoryExists( subDir ) );

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
    sw::string testPath    = sw::test::makeScratchPath( "test_output_temp.bin" );
    sw::string testContent = "Hello C++ Workspace!";

    bool writeOk = sw::FileUtil::writeFile( testPath, reinterpret_cast<const uint8*>( testContent.data() ), testContent.size() );
    SW_EXPECT_TRUE( writeOk );
    SW_EXPECT_TRUE( sw::FileUtil::exists( testPath ) );

    sw::vector<uint8> readBuffer;
    bool              readOk = sw::FileUtil::readFile( testPath, readBuffer );
    SW_EXPECT_TRUE( readOk );

    sw::string readContent( readBuffer.begin(), readBuffer.end() );
    SW_EXPECT_EQUAL( testContent, readContent );
}

/**
 * @brief [FileTest] 실행 파일 경로는 **실제로 있는 파일**을 가리킨다
 * @details 윈도우의 `GetModuleFileNameW` 는 버퍼가 모자라면 **잘라서** 돌려주고 그 사실을
 *          반환값으로만 알린다. 260자 고정 버퍼에 담고 반환값을 보지 않으면 그보다
 *          깊은 경로에 설치될 때 exe 위치가 조용히 틀려지고, 그 자리를 기준으로 찾는 모듈 DLL ·
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

    SW_REQUIRE_TRUE( executablePath.empty() == false );
    SW_EXPECT_TRUE_MSG( sw::FileUtil::exists( executablePath ), "the executable path names a missing file - it was cut or broken" );
    SW_EXPECT_TRUE_MSG( sw::FileUtil::isAbsolutePath( executablePath ), "the executable path is not absolute" );

    // 디렉터리 부분도 실제로 있어야 한다 — 엔진이 리소스·모듈을 찾는 기준점이다.
    const sw::string executableDirectory = sw::FileUtil::getDirectoryPart( executablePath );
    SW_EXPECT_TRUE_MSG( sw::FileUtil::isDirectory( executableDirectory ), "the executable directory does not exist" );

    // 두 번 물어도 같은 답이어야 한다(버퍼를 키우는 루프가 상태를 남기지 않는다).
    SW_EXPECT_TRUE( sw::FileUtil::getExecutablePath() == executablePath );
}

/**
 * @brief [FileTest] 디렉터리 순회가 재귀·비재귀 모두 같은 규칙으로 걷는지 검증
 * @details `collectFiles` · `collectFolders` 는 네 경우(파일/폴더 × 재귀/비재귀)를 헬퍼 하나로 걷는다.
 *          순회자를 `std::error_code` 없이 만들면 권한이 없는 폴더나 순회 중 지워진 폴더에서 **예외를 던지고**,
 *          `bool` 로 실패를 알리는 두 함수의 호출부를 뚫고 나간다. 여기서는 네 경우가 모두 같은 규칙으로 걷는지 본다.
 */
SW_TEST_CASE( FileTest, DirectoryWalkCollectsFilesAndFolders )
{
    const sw::string rootDir = sw::test::makeScratchPath( "sw_walk_root" );
    const sw::string subDir  = sw::FileUtil::joinPath( rootDir, "nested" );
    SW_EXPECT_TRUE( sw::FileUtil::ensureDirectoryExists( subDir ) );

    const sw::string topFile    = sw::FileUtil::joinPath( rootDir, "top.txt" );
    const sw::string nestedFile = sw::FileUtil::joinPath( subDir, "deep.txt" );
    const sw::string otherFile  = sw::FileUtil::joinPath( rootDir, "skip.bin" );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( topFile, "a" ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( nestedFile, "b" ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( otherFile, "c" ) );

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
 * @brief [FileTest] 쓰기는 원자적이다: 다 쓴 뒤 바꿔 끼우고, 같은 폴더에 임시 파일을 남기지 않는다.
 * @details 원본을 "wb" 로 열어 그 자리에 쓰면 도중에 실패할 때 원본이 빈 파일로 남는다.
 */
SW_TEST_CASE( FileTest, WriteReplacesAtomicallyAndLeavesNoTemporaryFile )
{
    const sw::string dir = sw::test::makeScratchPath( "SwAtomicWriteTest" );
    SW_EXPECT_TRUE( sw::FileUtil::ensureDirectoryExists( dir ) );
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
    uint64 emptySize = 1;
    SW_EXPECT_TRUE( sw::FileUtil::tryGetFileSize( filePath, emptySize ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, emptySize );

    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( dir ) );
}

/**
 * @brief [FileTest] 쓰기가 실패하면 false 이고 원본은 그대로다.
 * @details 대상 자리에 폴더가 있으면 바꿔 끼울 수 없다 — 그 실패가 true 로 돌아오면 안 된다.
 */
SW_TEST_CASE( FileTest, FailedWriteReportsFalseAndKeepsOriginal )
{
    SW_EXPECT_LOG( "File.WriteFailed" ); // 실패한 쓰기는 원본을 두고 오류를 남긴다(바꿔 끼우기 · 임시 파일 열기)
    const sw::string dir         = sw::test::makeScratchPath( "SwFailedWriteTest" );
    const sw::string blockedPath = sw::FileUtil::joinPath( dir, "blocked" );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( sw::FileUtil::joinPath( blockedPath, "child" ) ) );

    SW_EXPECT_FALSE( sw::FileUtil::writeTextFile( blockedPath, "must-not-land" ) );
    SW_EXPECT_TRUE( sw::FileUtil::isDirectory( sw::FileUtil::joinPath( blockedPath, "child" ) ) );

    // 폴더 안에 임시 파일을 남기지 않는다.
    sw::vector<sw::string> listFile;
    SW_EXPECT_TRUE( sw::FileUtil::collectFiles( dir, "", listFile, false ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( listFile.size() ) );

    // 없는 폴더에 쓰기도 false 다.
    SW_EXPECT_FALSE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( dir, "missing/leaf.txt" ), "x" ) );

    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( dir ) );
}

/**
 * @brief [FileTest] 한글이 들어간 경로도 쓰기 · 읽기 · 존재 확인 · 폴더 훑기가 같은 파일을 본다.
 * @details Windows 에서 좁은 문자 경로는 실행 파일 매니페스트(activeCodePage=UTF-8)가 없으면 ANSI 코드 페이지(CP949)로 해석된다. 경로 API 마다
 *          해석이 다르면 사용자 폴더 이름이 한글일 때 `exists` 는 있다고 하는데 읽기는 "File not found" 가 된다.
 */
SW_TEST_CASE( FileTest, NonAsciiPathRoundTrips )
{
    const sw::string dir = sw::test::makeScratchPath( "SwUnicodePath_한글폴더" );
    SW_EXPECT_TRUE( sw::FileUtil::ensureDirectoryExists( dir ) );
    SW_REQUIRE_TRUE( sw::FileUtil::isDirectory( dir ) );

    const sw::string filePath = sw::FileUtil::joinPath( dir, "세이브_1.txt" );
    SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( filePath, "저장됨" ) );
    SW_EXPECT_TRUE( sw::FileUtil::exists( filePath ) );

    sw::string text;
    SW_EXPECT_TRUE( sw::FileUtil::readTextFile( filePath, text ) );
    SW_EXPECT_EQUAL( sw::string( "저장됨" ), text );

    sw::vector<sw::string> listFile;
    SW_EXPECT_TRUE( sw::FileUtil::collectFiles( dir, "txt", listFile, false ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listFile.size() ) );
    SW_EXPECT_TRUE( sw::FileUtil::getFileNamePart( listFile[0] ) == "세이브_1.txt" );

    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( dir ) );
}

/**
 * @brief [FileTest] 범위 읽기의 오프셋 · 길이는 64 비트다(uint32 면 4 GB 이상 파일이 조용히 잘린다). 파일 끝을 넘는 오프셋은 실패다.
 */
SW_TEST_CASE( FileTest, ReadRangeRespectsOffsetAndRejectsPastEnd )
{
    SW_EXPECT_LOG( "File.ReadPastEnd" ); // 끝을 넘는 오프셋은 거절하며 남긴다
    const sw::string dir = sw::test::makeScratchPath( "SwReadRangeTest" );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( dir ) );
    const sw::string filePath = sw::FileUtil::joinPath( dir, "range.bin" );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( filePath, "0123456789" ) );

    sw::vector<uint8> bytes;
    SW_EXPECT_TRUE( sw::FileUtil::readFile( filePath, bytes, 3ull, 4ull ) );
    SW_EXPECT_EQUAL( sw::string( "3456" ), sw::string( bytes.begin(), bytes.end() ) );

    SW_EXPECT_TRUE( sw::FileUtil::readFile( filePath, bytes, 8ull ) );
    SW_EXPECT_EQUAL( sw::string( "89" ), sw::string( bytes.begin(), bytes.end() ) );

    SW_EXPECT_FALSE( sw::FileUtil::readFile( filePath, bytes, 11ull ) );

    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( dir ) );
}

/**
 * @brief [FileTest] 읽기 전용 파일을 읽기 전용이라고 답한다 — 없는 파일 · 폴더는 false
 * @details 에디터는 git LFS 잠금(잠그기 전까지 읽기 전용)을 이 답으로 보여 준다. 쓰기를 다시 허락하면 false 로 돌아온다.
 */
SW_TEST_CASE( FileTest, ReadOnlyFileIsReported )
{
    const sw::string root     = sw::test::makeScratchPath( "SwReadOnlyTest" );
    const sw::string filePath = root + "/locked.txt";
    SW_REQUIRE_TRUE( sw::FileUtil::ensureParentDirectoryExists( filePath ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( filePath, "locked" ) );
    SW_EXPECT_FALSE( sw::FileUtil::isReadOnlyFile( filePath ) );

    SW_REQUIRE_TRUE( sw::FileUtil::setWritable( filePath, false ) );
    SW_EXPECT_TRUE( sw::FileUtil::isReadOnlyFile( filePath ) );

    SW_REQUIRE_TRUE( sw::FileUtil::setWritable( filePath, true ) );
    SW_EXPECT_FALSE( sw::FileUtil::isReadOnlyFile( filePath ) );

    SW_EXPECT_FALSE( sw::FileUtil::isReadOnlyFile( root + "/missing.txt" ) );
    SW_EXPECT_FALSE( sw::FileUtil::isReadOnlyFile( root ) );
    SW_EXPECT_TRUE( sw::FileUtil::removeDirectory( root ) );
}

/**
 * @brief [FileTest] 순회 콜백은 루트로 시작하는 `/` 경로와 디렉터리 여부를 주고, false 를 돌려주면 멈춘다
 */
SW_TEST_CASE( FileTest, DirectoryVisitorReportsEntriesAndStops )
{
    const sw::string root = sw::test::makeScratchPath( "SwVisitorTest" );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( root + "/a.txt", "a" ) );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( root + "/sub" ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( root + "/sub/b.txt", "b" ) );

    uint32 fileCount{ 0 };
    uint32 directoryCount{ 0 };
    bool   bAllUnderRoot = true;
    SW_EXPECT_TRUE( sw::FileUtil::forEachDirectoryEntry( root, true, [&]( const sw::DirectoryEntry& entry )
    {
        bAllUnderRoot = bAllUnderRoot && sw::FileUtil::startsWithPathComponent( entry._path, root ) && entry._path.find( '\\' ) == sw::string_view::npos;
        if ( entry._bDirectory )
            ++directoryCount;
        else
            ++fileCount;
        return true;
    } ) );
    SW_EXPECT_EQUAL( 2u, fileCount );
    SW_EXPECT_EQUAL( 1u, directoryCount );
    SW_EXPECT_TRUE( bAllUnderRoot );

    uint32 visitedBeforeStop{ 0 };
    SW_EXPECT_TRUE( sw::FileUtil::forEachDirectoryEntry( root, true, [&visitedBeforeStop]( const sw::DirectoryEntry& )
    {
        ++visitedBeforeStop;
        return false;
    } ) );
    SW_EXPECT_EQUAL( 1u, visitedBeforeStop );
    SW_EXPECT_FALSE( sw::FileUtil::forEachDirectoryEntry( root + "/missing", true, []( const sw::DirectoryEntry& )
    { return true; } ) );
}

/**
 * @brief [FileTest] isRegularFile 은 파일에만 true 이고, exists 는 파일과 폴더 모두에 true 다
 * @details 후보 경로를 차례로 보는 곳(설정 · 리소스 낱개 · 폰트)은 같은 이름의 폴더를 건너뛰어야 다음 후보로 간다.
 */
SW_TEST_CASE( FileTest, RegularFileExcludesDirectories )
{
    const sw::string root = sw::test::makeScratchPath( "SwRegularFileTest" );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( root ) );
    const sw::string filePath = root + "/plain.txt";
    const sw::string dirPath  = root + "/folder.txt";
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( filePath, "plain" ) );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( dirPath ) );

    SW_EXPECT_TRUE( sw::FileUtil::isRegularFile( filePath ) );
    SW_EXPECT_FALSE( sw::FileUtil::isRegularFile( dirPath ) );
    SW_EXPECT_FALSE( sw::FileUtil::isRegularFile( root + "/missing.txt" ) );

    SW_EXPECT_TRUE( sw::FileUtil::exists( filePath ) );
    SW_EXPECT_TRUE( sw::FileUtil::exists( dirPath ) );
}

/**
 * @brief [FileTest] findPathIgnoringCase 는 소문자 키를 실제 디스크 표기로 풀고, 없으면 빈 문자열이다
 * @details 대소문자를 구분하는 파일 시스템(Linux)에서 소문자 id 로 기록한 입력을 다시 열려면 이 풀이가 필요하다.
 */
SW_TEST_CASE( FileTest, FindPathIgnoringCaseReturnsTheDiskSpelling )
{
    const sw::string root = sw::test::makeScratchPath( "SwIgnoreCaseRoot" );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( root + "/MixedDir/Sub" ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( root + "/MixedDir/Sub/File.TXT", "x" ) );

    const sw::string found = sw::FileUtil::findPathIgnoringCase( root, "mixeddir/sub/file.txt" );
    SW_EXPECT_EQUAL( sw::FileUtil::normalizeSeparators( root ) + "/MixedDir/Sub/File.TXT", found );
    SW_EXPECT_TRUE( sw::FileUtil::exists( found ) );
    SW_EXPECT_TRUE( sw::FileUtil::findPathIgnoringCase( root, "mixeddir/sub/missing.txt" ).empty() );
    SW_EXPECT_TRUE( sw::FileUtil::findPathIgnoringCase( root, "nodir/sub/file.txt" ).empty() );
}

/**
 * @brief [FileTest] makePathIgnoringCase 는 있는 성분은 디스크 표기로, 없는 성분부터는 받은 표기 그대로 잇는다
 * @details 쓰기용이다 — 소문자 id 를 그대로 이으면 대소문자를 구분하는 파일 시스템(Linux)에서 이미 있는 폴더 옆에 대소문자만 다른 새 폴더가 생긴다.
 */
SW_TEST_CASE( FileTest, MakePathIgnoringCaseKeepsExistingSpellingAndAppendsTheRest )
{
    const sw::string root = sw::test::makeScratchPath( "SwMakePathCaseRoot" );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( root + "/MixedDir/Sub" ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( root + "/MixedDir/Sub/File.TXT", "x" ) );
    const sw::string base = sw::FileUtil::normalizeSeparators( root );

    // 있는 파일: 디스크 표기로 풀려 같은 파일을 가리킨다(둘째 쓰기가 옆 파일을 만들지 않는다).
    SW_EXPECT_EQUAL( base + "/MixedDir/Sub/File.TXT", sw::FileUtil::makePathIgnoringCase( root, "mixeddir/sub/file.txt" ) );
    // 새 파일: 있는 폴더는 디스크 표기이고 파일 이름은 받은 표기다. 구분자는 `\` 도 받는다.
    SW_EXPECT_EQUAL( base + "/MixedDir/Sub/New.Txt", sw::FileUtil::makePathIgnoringCase( root, "MIXEDDIR\\sub/New.Txt" ) );
    // 새 폴더: 없는 성분부터는 받은 표기를 그대로 잇고 그 아래는 훑지 않는다.
    SW_EXPECT_EQUAL( base + "/MixedDir/Fresh/Deep/x.txt", sw::FileUtil::makePathIgnoringCase( root, "mixeddir/Fresh/Deep/x.txt" ) );
    SW_EXPECT_EQUAL( base + "/NoDir/a.txt", sw::FileUtil::makePathIgnoringCase( root, "NoDir/a.txt" ) );
    SW_EXPECT_EQUAL( base, sw::FileUtil::makePathIgnoringCase( root, "" ) );
}

/**
 * @brief [FileTest] 덧붙이기는 파일 끝에 쓰고 없는 파일은 만든다
 */
SW_TEST_CASE( FileTest, AppendTextFileAddsToTheEnd )
{
    const sw::string root = sw::test::makeScratchPath( "SwAppendRoot" );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( root ) );
    const sw::string path = root + "/Spool.txt";

    SW_REQUIRE_TRUE( sw::FileUtil::appendTextFile( path, "a\n" ) ); // 없는 파일은 만든다
    SW_REQUIRE_TRUE( sw::FileUtil::appendTextFile( path, "b\n" ) );
    SW_EXPECT_TRUE( sw::FileUtil::appendTextFile( path, "" ) ); // 빈 글은 아무것도 더하지 않고 성공이다

    sw::string text;
    SW_REQUIRE_TRUE( sw::FileUtil::readTextFile( path, text ) );
    SW_EXPECT_EQUAL( sw::string( "a\nb\n" ), text );
}

/**
 * @brief [FileTest] 쓰기 게이트가 거절하면 덧붙이기 · 휴지통 · 쓰기가 모두 false 이고 원본은 그대로다
 * @details 파일을 바꾸는 입구가 모두 `isWriteAllowed` 를 지나는지 본다. 휴지통은 거절하는 쪽만 본다(통과하면 실제로 휴지통에 간다).
 *          게이트는 프로세스 전역이라 단언이 중간에 끝내지 않도록 결과를 모은 뒤 걷고 판정한다.
 */
SW_TEST_CASE( FileTest, WriteGateRefusesEveryMutatingEntry )
{
    const sw::string root = sw::test::makeScratchPath( "SwWriteGateRoot" );
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( root ) );
    const sw::string path = root + "/Guarded.txt";
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( path, "keep" ) );

    bool bWritten  = true;
    bool bAppended = true;
    bool bTrashed  = true;
    bool bRemoved  = true;
    {
        SW_EXPECT_LOG( "File.WriteRefused" ); // 게이트가 거절한 쓰기는 오류로 남는다
        sw::FileUtil::setWriteGate( &refuseEveryWrite );
        bWritten  = sw::FileUtil::writeTextFile( path, "changed" );
        bAppended = sw::FileUtil::appendTextFile( path, "more" );
        bTrashed  = sw::FileUtil::moveToRecycleBin( sw::vector<sw::string>{ path } );
        bRemoved  = sw::FileUtil::removeFile( path );
        sw::FileUtil::setWriteGate( nullptr );
    }

    SW_EXPECT_FALSE( bWritten );
    SW_EXPECT_FALSE( bAppended );
    SW_EXPECT_FALSE( bTrashed );
    SW_EXPECT_FALSE( bRemoved );
    sw::string text;
    SW_REQUIRE_TRUE( sw::FileUtil::readTextFile( path, text ) );
    SW_EXPECT_EQUAL( sw::string( "keep" ), text );
}
