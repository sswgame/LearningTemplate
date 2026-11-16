#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Testing/TestFramework.h"

// Windows 전용 파일 케이스 — 플랫폼 폴더(`Windows/`)라 Windows 에서만 짓는다(파일 안 `#if` 를 두지 않는다).

/**
 * @brief [FileTest] 이 프로세스의 ANSI 코드 페이지는 UTF-8 이다(실행 파일 매니페스트 activeCodePage).
 * @details 엔진의 좁은 문자 문자열은 UTF-8 이다. 매니페스트가 빠지면 fopen · std::filesystem · CreateFileA · argv 가 다시 CP949 로
 *          해석되고, 한글 경로가 조용히 깨진다. 매니페스트를 붙이는 CMake 함수(sw_add_executable)가 빠진 실행 파일을 여기서 잡는다.
 */
SW_TEST_CASE( FileTest, ProcessUsesUTF8AnsiCodePage )
{
    SW_EXPECT_EQUAL( static_cast<uint32>( CP_UTF8 ), static_cast<uint32>( GetACP() ) );
}

/**
 * @brief [FileTest] 260 자(MAX_PATH)를 넘는 경로도 폴더를 만들고 쓰고 읽는다 — 파일 API 길목(`PlatformFileUtil::toWindowsFilePath`)이 확장 경로 접두를 붙인다.
 * @details 접두가 없던 때는 깊은 작업 트리의 테스트 임시 경로(266 자)에서 열기가 실패했다. 짧은 경로는 접두 없이 그대로다(접두를 받지 않는 API 와 로그가 같은 글을 본다).
 */
SW_TEST_CASE( FileTest, LongPathBeyondMaxPathIsWrittenAndRead )
{
    sw::string dir = sw::test::makeScratchPath( "SwLongPathRoot" );
    while ( dir.size() < 300 )
        dir += "/segment_padding_for_long_path";
    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( dir ) );
    const sw::string filePath = dir + "/long.txt";
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( filePath, "long path" ) );
    sw::string text;
    SW_REQUIRE_TRUE( sw::FileUtil::readTextFile( filePath, text ) );
    SW_EXPECT_EQUAL( sw::string( "long path" ), text );

    SW_EXPECT_TRUE( sw::PlatformFileUtil::toWindowsFilePath( filePath ).compare( 0, 4, L"\\\\?\\" ) == 0 );
    SW_EXPECT_TRUE( sw::PlatformFileUtil::toWindowsFilePath( "C:/short/x.txt" ) == sw::wstring( L"C:\\short\\x.txt" ) );

    // 접두는 들어가는 쪽 표기다 — 나오는 경로에는 없어 짧은 폴더 아래의 긴 파일도 그 폴더 안으로 판정된다.
    const sw::string shortRoot = sw::test::makeScratchPath( "SwLongPathRoot" );
    sw::string       relativePath;
    SW_REQUIRE_TRUE( sw::FileUtil::makeRelativePath( shortRoot, filePath, relativePath ) );
    SW_EXPECT_EQUAL( filePath.substr( shortRoot.size() + 1 ), sw::FileUtil::normalizeSeparators( relativePath ) );
}
