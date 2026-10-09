#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Renderer/Capture/ScreenshotPathUtil.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [ScreenshotPathUtilTest] 기본 경로는 `Saved/Screenshots/<yyyyMMdd-HHmmss>.png` 다 — 자리마다 0 을 채운다
 */
SW_TEST_CASE( ScreenshotPathUtilTest, DefaultPathIsTimestampPngUnderSaved )
{
    sw::ScreenshotLocalTime localTime{};
    localTime._year   = 2026;
    localTime._month  = 3;
    localTime._day    = 7;
    localTime._hour   = 9;
    localTime._minute = 5;
    localTime._second = 1;
    SW_EXPECT_EQUAL( sw::ScreenshotPathUtil::makeDefaultPath( localTime ), sw::FileUtil::normalizeSeparators( "Saved/Screenshots/20260307-090501.png" ) );
}

/**
 * @brief [ScreenshotPathUtilTest] 지금 시각의 경로도 같은 폴더 · 같은 길이 · `.png` 로 끝난다
 */
SW_TEST_CASE( ScreenshotPathUtilTest, NowPathHasTheSameShape )
{
    const sw::string path     = sw::ScreenshotPathUtil::makeDefaultPathNow();
    const sw::string expected = sw::FileUtil::normalizeSeparators( "Saved/Screenshots/20260307-090501.png" );
    SW_EXPECT_EQUAL( path.size(), expected.size() );
    SW_EXPECT_TRUE( sw::StringUtil::endsWith( path, ".png", false ) );
    SW_EXPECT_TRUE( sw::StringUtil::startsWith( path, sw::FileUtil::normalizeSeparators( "Saved/Screenshots/" ), false ) );
}
