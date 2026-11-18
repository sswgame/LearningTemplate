// PackagingViewModelTest — 패키징 창의 오류 줄 판정(감사 E-15): 낱말 "error" 가 든 줄이 아니라 도구가 오류로 찍은 줄만 오류다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Packaging/Model/PackagingViewModel.h"

/** @brief [PackagingViewModelTest] 컴파일러 · 링커 · ninja · 진입점의 실패 줄은 오류이고, "0 errors" · 파일 이름의 error 는 오류가 아니다 */
SW_TEST_CASE( PackagingViewModelTest, ErrorLineIsWhatTheToolMarked )
{
    using Model = sw::editor::PackagingViewModel;
    SW_EXPECT_TRUE( Model::isErrorLine( "C:/src/a.cpp(12): error C2065: 'x': undeclared identifier" ) );
    SW_EXPECT_TRUE( Model::isErrorLine( "src/a.cpp:12:3: error: use of undeclared identifier 'x'" ) );
    SW_EXPECT_TRUE( Model::isErrorLine( "LINK : fatal error LNK1104: cannot open file 'x.lib'" ) );
    SW_EXPECT_TRUE( Model::isErrorLine( "FAILED: Source/Engine/CMakeFiles/a.cpp.obj" ) );
    SW_EXPECT_TRUE( Model::isErrorLine( "[package] FAILED cook the cooker exited with 1" ) );

    SW_EXPECT_FALSE( Model::isErrorLine( "Build finished: 0 errors, 2 warnings" ) );
    SW_EXPECT_FALSE( Model::isErrorLine( "Compiling error_report.cpp" ) );
    SW_EXPECT_FALSE( Model::isErrorLine( "copied ErrorScreen.ui.xml" ) );
}
