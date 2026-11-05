#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Capture/BugReport.h"

using namespace sw;

// 다른 프로세스의 보관 개수 정리가 목록을 읽는 사이에 폴더를 지울 수 있다. 사라진 리포트는 오류 로그 없이 "없음" 이다(로그가 나면 이 테스트가 진다).
SW_TEST_CASE( BugReportTest, ReportThatVanishedIsNotAnError )
{
    BugReport report;
    SW_EXPECT_FALSE( BugReport::readInfo( test::makeScratchPath( "SwNoSuchBugReport" ), report ) );
}
