#include "pch.h"

#include "Core/File/FileUtil.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [TestFrameworkTest] 임시 파일 경로는 **프로세스마다 · 케이스마다** 다르다
 * @details 테스트들이 `%TEMP%/test_malformed.wav` 처럼 고정된 이름에 쓰고 있었다. 같은 테스트
 *          실행 파일이 네 프리셋에서 각각 돌고 CI 는 그것들을 나란히 돌리므로, 한쪽의
 *          `removeFile` 이 다른 쪽이 방금 쓴 파일을 지울 수 있다. 확장자는 그대로 남아야
 *          한다 — 로더가 그것으로 형식을 고른다.
 */

SW_TEST_CASE( TestFrameworkTest, TempPathIsUniquePerProcessAndPerCase )
{
    const sw::string path = test::makeTempPath( "sample.wav" );

    // 임시 폴더 아래에 있고, 확장자와 원래 이름이 그대로 남는다.
    SW_EXPECT_TRUE_MSG( sw::StringUtil::startsWith( path, sw::FileUtil::getTempDirectory() ),
                        "임시 폴더 아래가 아닙니다" );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::endsWith( path, "sample.wav" ), "원래 이름과 확장자가 사라졌습니다" );

    // 케이스 이름이 섞여 있어야 한 프로세스 안에서 두 케이스가 서로를 안 밟는다.
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( path, "TempPathIsUniquePerProcessAndPerCase" ),
                        "케이스 이름이 경로에 없습니다" );

    // 같은 이름을 다시 물으면 같은 경로여야 한다 — 한 케이스 안에서는 안정적이어야 쓴 것을 읽는다.
    SW_EXPECT_EQUAL( path, test::makeTempPath( "sample.wav" ) );

    // 다른 이름은 다른 경로다.
    SW_EXPECT_TRUE( path != test::makeTempPath( "other.wav" ) );
}

/**
 * @brief [TestFrameworkTest] 옆 케이스가 같은 파일 이름을 써도 경로가 겹치지 않는다
 * @details 실제로 `sw_test_scene_desc.bin` 을 두 케이스가 같이 쓰고 있었다. 순서대로 돌기는
 *          하지만 앞 케이스가 정리 전에 실패하면 뒤 케이스가 남은 파일을 읽는다.
 */
SW_TEST_CASE( TestFrameworkTest, TwoCasesAskingForTheSameFileNameGetDifferentPaths )
{
    const sw::string path = test::makeTempPath( "sample.wav" );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( path, "TwoCasesAskingForTheSameFileNameGetDifferentPaths" ),
                        "케이스 이름이 경로에 없습니다" );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( path, "TempPathIsUniquePerProcessAndPerCase" ) == false,
                        "옆 케이스의 경로와 같습니다" );
}
