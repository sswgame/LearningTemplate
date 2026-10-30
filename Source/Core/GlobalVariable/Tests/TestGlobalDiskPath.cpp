#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Testing/TestFramework.h"

// 디스크 경로 전역 변수 — 받을 때 기동 기준 폴더(명령줄과 같은 기준)로 절대화하고, 드라이브 상대 경로는 거절한다.

using namespace sw;

SW_TEST_CASE( GlobalDiskPathTest, RelativeValueBecomesAbsolute )
{
    GlobalVariableManager manager;
    GlobalDiskPath        value;
    SW_REQUIRE_TRUE( manager.registerVariable( "gv_testDiskPath", GlobalVariableType::DiskPath, &value, string{}, "test" ) );

    // 기준은 기동 때 명령줄이 정했다(App --test 의 작업 폴더 - 테스트 중에 바뀌지 않는다).
    const std::optional<AbsolutePath> base = AbsolutePath::tryMake( FileUtil::getCurrentPath() );
    SW_REQUIRE_TRUE( base.has_value() );
    SW_REQUIRE_TRUE( manager.setValueFromString( "gv_testDiskPath", "out/dump.wav" ) );
    SW_EXPECT_EQUAL( AbsolutePath::tryResolve( "out/dump.wav", *base )->str(), string( value ) );

    SW_EXPECT_FALSE( manager.setValueFromString( "gv_testDiskPath", "C:dump.wav" ) ); // 드라이브 상대는 경로가 아니다
    SW_REQUIRE_TRUE( manager.setValueFromString( "gv_testDiskPath", "" ) );           // 빈 값은 "끔"
    SW_EXPECT_TRUE( value.empty() );
}
