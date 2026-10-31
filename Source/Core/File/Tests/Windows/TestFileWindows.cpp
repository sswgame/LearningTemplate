#include "pch.h"

#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Testing/TestFramework.h"

// Windows 전용 파일 케이스 — 플랫폼 폴더(`Windows/`)라 Windows 에서만 짓는다(파일 안 `#if` 를 두지 않는다).

/**
 * @brief [FileTest] 이 프로세스의 ANSI 코드 페이지는 UTF-8 이다(실행 파일 매니페스트 activeCodePage).
 * @details 엔진의 좁은 문자 문자열은 UTF-8 이다. 매니페스트가 빠지면 fopen · std::filesystem · CreateFileA · argv 가 다시 CP949 로
 *          해석되고, 한글 경로가 조용히 깨진다. 매니페스트를 붙이는 CMake 함수(sw_embedProcessManifest)가 빠진 실행 파일을 여기서 잡는다.
 */
SW_TEST_CASE( FileTest, ProcessUsesUtf8AnsiCodePage )
{
    SW_EXPECT_EQUAL( static_cast<uint32>( CP_UTF8 ), static_cast<uint32>( GetACP() ) );
}
