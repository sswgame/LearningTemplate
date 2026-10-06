#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/CookContract.gen.h"

// 실제 App.exe 를 시나리오마다 · 백엔드마다 띄운다 — GPU · 창 · 셰이더가 필요하다. CI 러너엔 없다.
SW_TEST_REQUIRES_HOST( AppScenarioTest, "launches App.exe per automation scenario and backend - needs a GPU and a window" );

// ------------------------------------------------------------------------------
// AppScenarioTest — 자동화 시나리오(`Resource/<영역>/automation/*.scenario.xml`)를 백엔드마다 돌려 종료 코드를 본다
//
// 시나리오 파일을 놓기만 하면 돈다(파일 목록을 CMake 에 적지 않는다). 게임은 프리셋마다 하나(`SW_ACTIVE_GAME`)이므로 그 프리셋의
// `AppTest_HostOnly` 가 **엔진 시나리오 + 그 게임 팩의 시나리오**를 돈다. 종료 코드 13(건너뜀 — 전경 창을 못 얻음 등) · 77(이 기계에 없는 백엔드)은
// 건너뛴다. 언리얼 Gauntlet 이 프로세스를 띄워 결과 코드 · 로그를 모으는 것과 같은 자리다.
// ------------------------------------------------------------------------------

namespace
{
    struct AppScenarioTestInternal
    {
        /** @brief 엔진(`engine/automation`)과 활성 게임 팩(`<팩>/automation`)의 시나리오 — 리소스 경로로, 이름순입니다. */
        static sw::vector<sw::string> collectScenarioPaths()
        {
            sw::vector<sw::string> listPath;
            if ( sw::ResourceUtil::initialize() == false )
                return listPath;
            const sw::string&      resourceRoot = sw::ResourceUtil::getRootFolderPath();
            sw::vector<sw::string> listDomain{ "engine" };
            const sw::string       packRoot = test::AppTestUtil::readActivePackRoot();
            if ( packRoot.empty() == false )
                listDomain.push_back( packRoot );
            for ( const sw::string& domain : listDomain )
            {
                sw::vector<sw::string> listFile;
                const sw::string       folder = sw::FileUtil::joinPath( resourceRoot, domain + "/automation" );
                if ( sw::FileUtil::isDirectory( folder ) == false || sw::FileUtil::collectFiles( folder, ".xml", listFile, false ) == false )
                    continue;
                std::sort( listFile.begin(), listFile.end() );
                for ( const sw::string& filePath : listFile )
                {
                    if ( sw::StringUtil::endsWith( filePath, ".scenario.xml", true ) )
                        listPath.push_back( sw::ResourceUtil::toResourceId( filePath ) );
                }
            }
            return listPath;
        }
    };
} // namespace

/**
 * @brief [AppScenarioTest] 엔진 · 활성 게임의 시나리오가 모든 백엔드에서 통과한다(종료 코드 0 — 13 건너뜀 · 77 이 기계에 없는 백엔드)
 * @details 사례가 하나라 실패 메시지에 시나리오 · 백엔드가 같이 나온다. 로그는 `Bin/Saved/Automation/<시나리오>_<백엔드>.log`.
 */
SW_TEST_CASE( AppScenarioTest, EveryScenarioPassesOnEveryBackend )
{
    using Internal                            = AppScenarioTestInternal;
    const sw::vector<sw::string> listScenario = Internal::collectScenarioPaths();
    if ( listScenario.empty() )
        SW_TEST_SKIP( "no automation scenario for the engine or this game" );
#if defined( SW_SHIPPING )
    // 배포본은 백엔드를 하나만 링크한다 — 스위치 없이 "이 빌드가 가진 것" 으로 돌린다(AppSmokeTest 와 같다).
    constexpr const utf8* kArrBackendSwitch[] = { "" };
#else
    #define SW_APP_SCENARIO_BACKEND_SWITCH( Backend, ShaderFolder, ShaderTarget, Argument, FirstAlias, ... ) "-" FirstAlias,
    constexpr const utf8* kArrBackendSwitch[] = { SW_RHI_BACKEND_TABLE( SW_APP_SCENARIO_BACKEND_SWITCH ) };
    #undef SW_APP_SCENARIO_BACKEND_SWITCH
#endif
    uint32 ranCount = 0;
    for ( const sw::string& scenarioPath : listScenario )
    {
        for ( const utf8* pSwitch : kArrBackendSwitch )
        {
            sw::string       scenarioLines;
            const int32      exitCode = test::AppTestUtil::runScenario( scenarioPath, pSwitch, scenarioLines );
            const sw::string label    = scenarioPath + " " + pSwitch + " -> exit " + sw::to_string( exitCode ) + "\n" + scenarioLines;
            SW_EXPECT_TRUE_MSG( exitCode != test::AppTestUtil::kNotLaunchedExitCode, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)에 App 이 있습니까?" );
            if ( test::AppTestUtil::isSkippedExitCode( exitCode ) )
            {
                SW_LOG_INFO( "[AppScenarioTest] skipped %#", label.c_str() );
                continue;
            }
            SW_EXPECT_TRUE_MSG( exitCode == 0, label.c_str() );
            ++ranCount;
        }
    }
    if ( ranCount == 0 )
        SW_TEST_SKIP( "no scenario could run on this machine" );
}
