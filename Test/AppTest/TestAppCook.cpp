#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// AppCookTest — 빌드가 부르는 그대로 `App --cook-scenes` 를 돌려 본다
//
// 씬 쿠킹은 엔진 안에서만 돈다(리플렉션이 필요하다). 그래서 쿠커의 입력이 무엇인지 — 어떤 타입이 등록됐고 어떤 콘텐츠가 마운트됐는지 — 는
// 실제 실행 파일의 기동 순서와 링크가 정한다. 시험 실행 파일은 모든 타입을 스스로 링크 · 등록하므로 그 결함을 볼 수 없다. 창 · GPU 가
// 필요 없는 헤드리스 실행이라 CI 에서도 돈다.
// ------------------------------------------------------------------------------

namespace
{
    /** @brief 쿠킹 한 판의 결과입니다. */
    struct CookRunResult
    {
        int32          _exitCode{ -1 };
        vector<string> _listMissingComponentLine{}; ///< `MissingComponent` 가 든 줄 — 쿠킹이 모르는 타입을 만난 흔적
        vector<string> _listProblemLine{};          ///< `[Error]` · `[Warning]` 줄 — 쿠킹이 입력(엔진 데이터 · 프리팹 · 클립)을 읽지 못한 흔적
        bool           _bLaunched{ false };
    };

    /** @brief `App --cook-scenes --cooked-dir=<cookedDir>` 를 한 판 돌립니다. */
    CookRunResult runSceneCook( const string& cookedDir )
    {
        CookRunResult result{};
        string        arguments{ "--cook-scenes \"--cooked-dir=" };
        arguments += cookedDir;
        arguments += "\"";

        Process process;
        if ( test::AppTestUtil::launchApp( process, arguments ) == false )
            return result;
        result._bLaunched = true;

        string line;
        while ( process.readOutputLine( line ) )
        {
            if ( line.find( "MissingComponent" ) != string::npos )
                result._listMissingComponentLine.push_back( line );
            if ( line.find( "[Error]" ) != string::npos || line.find( "[Warning]" ) != string::npos )
                result._listProblemLine.push_back( line );
        }
        result._exitCode = process.waitForExit();
        return result;
    }
} // namespace

/**
 * @brief [AppCookTest] 씬 쿠킹이 GameFramework 컴포넌트를 제 타입으로 쿠킹한다 — spriteui 의 HPBar · DamageUI · Effect 가 MissingComponent 가 아니다
 * @details 두 구성이 서로 다른 이유로 GameFramework 타입이 없는 채 씬을 쿠킹할 수 있다. Dev 는 헤드리스 쿠킹이 모듈 DLL 을 올려야 하고(RHI 뒤에서
 *          ModuleHost 가 올리면 쿠킹에는 없다), Shipping 은 App 이 통째로 링크할 정적 라이브러리 목록을 GameFramework · 킷 · 게임이 등록된 뒤에
 *          읽어야 한다 — 아무도 참조하지 않는 GameFramework 의 등록기가 링크에서 빠진다. 그래도 쿠킹은 경고만 남기고 성공하므로 여기서 본다.
 */
SW_TEST_CASE( AppCookTest, SceneCookBuildsGameFrameworkComponents )
{
    const string        cookedDir = test::makeTempDirectory( "app_scene_cook" );
    const CookRunResult result    = runSceneCook( cookedDir );

    SW_ASSERT_TRUE_MSG( result._bLaunched, "App could not be launched - is it next to the test binary or in the working folder (Bin)?" );
    SW_EXPECT_EQUAL( 0, result._exitCode );
    SW_EXPECT_TRUE_MSG( result._listMissingComponentLine.empty(),
                        result._listMissingComponentLine.empty() ? "" : result._listMissingComponentLine.front().c_str() );
    SW_EXPECT_TRUE( FileUtil::fileExists( FileUtil::joinPath( cookedDir, "game/empty/maps/spriteui.scene.bin" ) ) );
}

/**
 * @brief [AppCookTest] 씬 쿠킹은 소스 트리를 읽어 오류 · 경고 없이 끝난다 — 배포 구성도 같다
 * @details 쿠킹은 소스 트리를 올린다(`ContentSource::SourceTree`). 배포 구성의 쿠킹이 실행처럼 팩만 읽고 느슨한 파일을 막으면, 팩이 없는 첫 빌드에서
 *          `engine/data/enginedefaultassets.xml` 을 찾지 못하고 옮긴 프리팹을 GUID 로 찾지 못해 쿠킹본 `.bin` 을 요구하며, 팩이 있는 빌드에서는 지난 빌드의
 *          팩을 입력으로 읽는다.
 */
SW_TEST_CASE( AppCookTest, SceneCookReadsTheSourceTreeCleanly )
{
    const string        cookedDir = test::makeTempDirectory( "app_scene_cook_clean" );
    const CookRunResult result    = runSceneCook( cookedDir );

    SW_ASSERT_TRUE_MSG( result._bLaunched, "App could not be launched - is it next to the test binary or in the working folder (Bin)?" );
    SW_EXPECT_EQUAL( 0, result._exitCode );
    SW_EXPECT_TRUE_MSG( result._listProblemLine.empty(), result._listProblemLine.empty() ? "" : result._listProblemLine.front().c_str() );
    SW_EXPECT_TRUE( FileUtil::fileExists( FileUtil::joinPath( cookedDir, "game/empty/prefabs/testprop.prefab.bin" ) ) );
}
