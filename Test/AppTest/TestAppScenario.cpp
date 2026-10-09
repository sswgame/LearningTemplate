#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/CookContract.gen.h"

// 실제 App.exe 를 시나리오마다 · 백엔드마다 띄운다 — GPU · 창 · 셰이더가 필요하다. CI 러너엔 없다.
SW_TEST_REQUIRES_HOST( AppScenarioTest, "launches App.exe per automation scenario and backend - needs a GPU and a window" );

// ------------------------------------------------------------------------------
// AppScenarioTest — 자동화 시나리오(`Resource/<영역>/automation/*.scenario.xml`)를 백엔드마다 돌려 종료 코드를 본다
//
// 시나리오 파일을 놓기만 하면 돈다(파일 목록을 CMake 에 적지 않는다). 게임은 프리셋마다 하나(`SW_ACTIVE_GAME`)이므로 그 프리셋의
// `AppTest_HostOnly` 가 **엔진 시나리오 + 그 게임 팩의 시나리오**를 돈다. 에디터 시나리오(`engine/automation/editor/`)는 `-EnableEditor` 로 돈다.
// 종료 코드 13(건너뜀 — 전경 창을 못 얻음 등) · 77(이 기계에 없는 백엔드)은 건너뛴다. 언리얼 Gauntlet 이 프로세스를 띄워 결과 코드 · 로그를 모으는 것과 같은 자리다.
// ------------------------------------------------------------------------------

namespace
{
    struct AppScenarioTestInternal
    {
        /** @brief @p listFolder(리소스 경로) 바로 아래의 시나리오 — 리소스 경로로, 폴더마다 이름순입니다(하위 폴더는 내려가지 않는다). */
        static sw::vector<sw::string> collectScenarioPaths( const sw::vector<sw::string>& listFolder )
        {
            sw::vector<sw::string> listPath;
            if ( sw::ResourceUtil::initialize() == false )
                return listPath;
            const sw::string& resourceRoot = sw::ResourceUtil::getRootFolderPath();
            for ( const sw::string& folderId : listFolder )
            {
                sw::vector<sw::string> listFile;
                const sw::string       folder = sw::FileUtil::joinPath( resourceRoot, folderId );
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

        /** @brief 엔진(`engine/automation`)과 활성 게임 팩(`<팩>/automation`)의 시나리오입니다. */
        static sw::vector<sw::string> collectGameScenarioPaths()
        {
            sw::vector<sw::string> listFolder{ "engine/automation" };
            const sw::string       packRoot = test::AppTestUtil::readActivePackRoot();
            if ( packRoot.empty() == false )
                listFolder.push_back( packRoot + "/automation" );
            return collectScenarioPaths( listFolder );
        }

        /** @brief 시나리오를 백엔드마다 돌려 종료 코드 0 을 단언합니다. 돌린(건너뛰지 않은) 수를 돌려줍니다. */
        static uint32 runEveryBackend( const sw::vector<sw::string>& listScenario, sw::string_view extraArguments )
        {
#if defined( SW_SHIPPING )
            // 배포본은 백엔드를 하나만 링크한다 — 스위치 없이 "이 빌드가 가진 것" 으로 돌린다(AppSmokeTest 와 같다).
            constexpr const utf8* kArrBackendSwitch[] = { "" };
#else
    #define SW_APP_SCENARIO_BACKEND_SWITCH( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) "-" CommandLineName,
            constexpr const utf8* kArrBackendSwitch[] = { SW_RHI_BACKEND_TABLE( SW_APP_SCENARIO_BACKEND_SWITCH ) };
    #undef SW_APP_SCENARIO_BACKEND_SWITCH
#endif
            uint32 ranCount = 0;
            for ( const sw::string& scenarioPath : listScenario )
            {
                for ( const utf8* pSwitch : kArrBackendSwitch )
                {
                    sw::string       scenarioLines;
                    const int32      exitCode = test::AppTestUtil::runScenario( scenarioPath, pSwitch, scenarioLines, extraArguments );
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
            return ranCount;
        }
    };

#if !defined( SW_SHIPPING )
    /**
     * @struct EditorStateSnapshot
     * @brief 에디터 상태 폴더(`<저장소>/Saved/Editor`)의 파일 바이트를 들고 있다가 되돌립니다 — 에디터 시나리오가 테마 · 레이아웃 · 최근 씬을
     *        다시 써도 사용자의 에디터 설정이 바뀌지 않게. 시나리오 동안 새로 생긴 파일은 지운다.
     */
    struct EditorStateSnapshot
    {
        struct SavedFile
        {
            sw::string        _path;
            sw::vector<uint8> _bytes;
        };

        sw::string            _folder;
        sw::vector<SavedFile> _listFile;

        void capture()
        {
            _listFile.clear();
            _folder = sw::FileUtil::joinPath( sw::FileUtil::getDirectoryPart( sw::ResourceUtil::getRootFolderPath() ), sw::config::kDirSavedEditor );
            sw::vector<sw::string> listPath;
            if ( sw::FileUtil::isDirectory( _folder ) == false || sw::FileUtil::collectFiles( _folder, "", listPath, true ) == false )
                return;
            for ( const sw::string& path : listPath )
            {
                SavedFile file;
                file._path = path;
                if ( sw::FileUtil::readFile( path, file._bytes ) )
                    _listFile.push_back( std::move( file ) );
            }
        }

        void restore() const
        {
            sw::vector<sw::string> listPath;
            if ( sw::FileUtil::isDirectory( _folder ) && sw::FileUtil::collectFiles( _folder, "", listPath, true ) )
            {
                for ( const sw::string& path : listPath )
                {
                    bool bKnown = false;
                    for ( const SavedFile& file : _listFile )
                    {
                        bKnown = bKnown || sw::FileUtil::pathsEqualNormalized( file._path, path );
                    }
                    if ( bKnown == false )
                        (void)sw::FileUtil::tryRemoveFile( path ); // 정리일 뿐이라 남아도 다음 실행이 다시 지운다
                }
            }
            for ( const SavedFile& file : _listFile )
            {
                const bool bWritten = sw::FileUtil::writeFile( file._path, file._bytes.data(), file._bytes.size() );
                SW_EXPECT_TRUE_MSG( bWritten, ( "에디터 상태 파일을 되돌리지 못했습니다: " + file._path ).c_str() );
            }
        }
    };
#endif
} // namespace

/**
 * @brief [AppScenarioTest] 엔진 · 활성 게임의 시나리오가 모든 백엔드에서 통과한다(종료 코드 0 — 13 건너뜀 · 77 이 기계에 없는 백엔드)
 * @details 사례가 하나라 실패 메시지에 시나리오 · 백엔드가 같이 나온다. 로그는 `Bin/Saved/Automation/<시나리오>_<백엔드>.log`.
 */
SW_TEST_CASE( AppScenarioTest, EveryScenarioPassesOnEveryBackend )
{
    const sw::vector<sw::string> listScenario = AppScenarioTestInternal::collectGameScenarioPaths();
    if ( listScenario.empty() )
        SW_TEST_SKIP( "no automation scenario for the engine or this game" );
    if ( AppScenarioTestInternal::runEveryBackend( listScenario, {} ) == 0 )
        SW_TEST_SKIP( "no scenario could run on this machine" );
}

/**
 * @brief [AppScenarioTest] 에디터 작업 흐름 시나리오(`engine/automation/editor` 의 `.scenario.xml`)가 에디터를 켠 실행(`-EnableEditor`)에서 모든 백엔드로 통과한다
 * @details 에디터 동작을 바꿨으면 이 시나리오로 확인한다 — 오브젝트 만들기 · 컴포넌트 · 인스펙터 · 되돌리기 · 저장 · 열기 · 플레이, 창 크기 · 배율 · 테마 · 스크린샷,
 *          Hierarchy 필터. 사용자의 에디터 상태(`Saved/Editor`)는 앞뒤로 바이트째 되돌린다. 배포본에는 에디터가 없다.
 */
SW_TEST_CASE( AppScenarioTest, EditorScenariosPassOnEveryBackend )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the shipping build has no editor" );
#else
    const sw::vector<sw::string> listScenario = AppScenarioTestInternal::collectScenarioPaths( { "engine/automation/editor" } );
    SW_ASSERT_TRUE_MSG( listScenario.empty() == false, "engine/automation/editor 에 에디터 시나리오가 없습니다" );
    EditorStateSnapshot snapshot;
    snapshot.capture();
    const uint32 ranCount = AppScenarioTestInternal::runEveryBackend( listScenario, "-EnableEditor" );
    snapshot.restore();
    if ( ranCount == 0 )
        SW_TEST_SKIP( "no editor scenario could run on this machine" );
#endif
}
