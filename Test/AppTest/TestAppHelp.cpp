#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Container/string.h"
#include "Core/Process/Process.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// AppHelpTest — `App -help` 가 앱을 띄우지 않고 받는 인자 목록을 찍고 끝나는지 실제 실행 파일로 본다
//
// 명령줄 표(ArgumentList.xxx)의 줄과 헤드리스 작업 순서는 실행 파일의 기동 순서가 정하므로 프로세스를 띄워서 본다. 창 · GPU 가 없어도 돈다.
// ------------------------------------------------------------------------------

/**
 * @brief [AppHelpTest] `-help` 는 인자 목록을 표준 출력에 찍고 종료 코드 0 으로 끝난다 — 창을 세우지 않는다
 * @details 예전에는 모르는 인자처럼 넘어가 앱이 그대로 떴다(사람이 창을 닫아야 끝났다). 목록은 표에서 만들므로 표의 다른 줄(`-cook-shaders`)과
 *          값을 받는 줄의 모양(`-W=<int>`)이 함께 보여야 한다. 변이: `runHelpTask` 를 작업 순서에서 빼면 앱이 떠 시한을 넘기거나 목록이 없어 진다.
 */
SW_TEST_CASE( AppHelpTest, HelpPrintsTheArgumentListAndExits )
{
    string     output;
    const auto run = test::AppTestUtil::runApp( "-help", test::AppTestUtil::kAppRunTimeoutSeconds, [&output]( Process&, const string& line )
    {
        output += line;
        output += "\n";
    } );

    SW_ASSERT_TRUE_MSG( run._bLaunched, "App could not be launched - is it next to the test binary or in the working folder (Bin)?" );
    SW_ASSERT_TRUE_MSG( run._bTimedOut == false, "App -help did not exit - it started the app instead of printing the help" );
    SW_EXPECT_EQUAL( 0, run._exitCode );
    SW_EXPECT_TRUE_MSG( output.find( "Usage:" ) != string::npos, output.c_str() );
    SW_EXPECT_TRUE_MSG( output.find( "-cook-shaders" ) != string::npos, output.c_str() );
    SW_EXPECT_TRUE_MSG( output.find( "-W=<int>" ) != string::npos, output.c_str() );
    SW_EXPECT_TRUE_MSG( output.find( "docs/Config/CommandLine.md" ) != string::npos, output.c_str() );
}
