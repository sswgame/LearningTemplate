#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) ConfigManagerTest — 설정 표의 열쇠, 경로 해석, 폴백 사슬
//
//    이 클래스에는 단위 테스트가 하나도 없었다. 그런데 여기서 잘못되면 증상이 "창 크기·VSync·
//    리소스 우선순위가 조용히 기본값이 된다" 라서, 실행해 보고도 원인을 짚기 어렵다.
// ------------------------------------------------------------------------------

namespace
{
    /** @brief 창 크기만 바꾼 EngineConfig JSON 을 만듭니다. */
    string makeEngineConfigJson( uint32 width, uint32 height )
    {
        utf8 arrBuffer[constant::kMaxBuffer256]{};
        formatstring( arrBuffer, constant::kMaxBuffer256, "{ \"_window\": { \"_width\": %#, \"_height\": %# } }", width, height );
        return string( arrBuffer );
    }
} // namespace

/**
 * @brief [ConfigManagerTest] 설정 표의 열쇠가 **타입**인지 검증
 * @details 예전에는 호출부가 `hashed_string( "EngineConfig" )` 를 손으로 적어 넘겼고, 표의 열쇠는
 *          그 이름의 해시였다. 이름을 넘기지 않으므로 철자가 어긋날 자리가 없고, 다른 타입을
 *          물으면 그 타입이 없다고 답한다.
 */
SW_TEST_CASE( ConfigManagerTest, ConfigTableIsKeyedByType )
{
    ConfigManager manager;

    SW_EXPECT_NULL( manager.getConfig<EngineConfig>() );
    SW_EXPECT_NULL( manager.getConfig<GameConfig>() );

    SW_EXPECT_TRUE( manager.loadConfigFromJson<EngineConfig>( makeEngineConfigJson( 1600, 900 ), "test" ) );

    EngineConfig* pEngineConfig = manager.getConfig<EngineConfig>();
    SW_EXPECT_NOT_NULL( pEngineConfig );
    SW_EXPECT_EQUAL( 1600u, pEngineConfig->_window._width );

    // 다른 설정 타입은 아직 없다 — 같은 표에 섞이지 않는다.
    SW_EXPECT_NULL( manager.getConfig<GameConfig>() );

    // 다시 읽으면 덮어쓴다.
    SW_EXPECT_TRUE( manager.loadConfigFromJson<EngineConfig>( makeEngineConfigJson( 800, 600 ), "test" ) );
    SW_EXPECT_EQUAL( 800u, manager.getConfig<EngineConfig>()->_window._width );
}

/**
 * @brief [ConfigManagerTest] 상대 경로를 루트 디렉터리 기준으로 찾는지 검증
 * @details 실행 파일은 `build/<preset>/Bin` 에서 돌고 `Config/` 는 프로젝트 루트에 있다. 작업
 *          디렉터리 기준으로만 찾던 시절에는 설정 셋이 **매 실행마다 전부 "없음"** 이 되어
 *          베이크된 기본값으로 조용히 떨어졌다(창 크기·VSync·리소스 우선순위가 전부 무시됐다).
 *          `setRootDirectory` 가 그것을 막는데 그때까지 확인하는 테스트가 없었다.
 */
SW_TEST_CASE( ConfigManagerTest, RelativePathResolvesAgainstRootDirectory )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "Shipping 은 디스크의 Config/ 를 보지 않는다 — 베이크된 JSON 만 쓴다" );
#else
    test::ScopedLogSuppressor suppressor;

    const string rootDir      = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_config_root_test" );
    const string relativePath = "TestEngineConfig.json";
    const string absolutePath = FileUtil::joinPath( rootDir, relativePath );

    FileUtil::ensureDirectoryExists( rootDir );
    SW_EXPECT_TRUE( FileUtil::writeTextFile( absolutePath, makeEngineConfigJson( 1920, 1080 ) ) );

    // 1) 루트를 알려 주지 않으면 못 찾는다 (작업 디렉터리에도 실행 파일 옆에도 없다).
    {
        ConfigManager manager;
        SW_EXPECT_FALSE( manager.loadConfig<EngineConfig>( relativePath ) );
        SW_EXPECT_NULL( manager.getConfig<EngineConfig>() );
    }

    // 2) 루트를 알려 주면 찾는다.
    {
        ConfigManager manager;
        manager.setRootDirectory( rootDir );
        SW_EXPECT_TRUE( manager.loadConfig<EngineConfig>( relativePath ) );
        SW_EXPECT_NOT_NULL( manager.getConfig<EngineConfig>() );
        SW_EXPECT_EQUAL( 1920u, manager.getConfig<EngineConfig>()->_window._width );
    }

    // 3) 절대 경로는 루트와 무관하게 찾는다.
    {
        ConfigManager manager;
        SW_EXPECT_TRUE( manager.loadConfig<EngineConfig>( absolutePath ) );
        SW_EXPECT_EQUAL( 1920u, manager.getConfig<EngineConfig>()->_window._width );
    }

    FileUtil::removeFile( absolutePath );
#endif
}

/**
 * @brief [ConfigManagerTest] 파일이 없을 때 베이크된 JSON → C++ 기본값 순으로 떨어지는지 검증
 * @details 설정이 없다고 기동을 멈추지 않는다. 다만 **무엇으로 떨어졌는지** 는 값으로 드러나야 한다.
 */
SW_TEST_CASE( ConfigManagerTest, MissingFileFallsBackToBakedThenCppDefaults )
{
    test::ScopedLogSuppressor suppressor;

    const string missingPath = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_config_that_does_not_exist.json" );
    FileUtil::removeFile( missingPath );

    // 1) 베이크된 JSON 이 있으면 그것으로 떨어진다.
    {
        ConfigManager manager;
        const string  baked         = makeEngineConfigJson( 640, 480 );
        EngineConfig* pEngineConfig = manager.ensureConfig<EngineConfig>( missingPath, baked.c_str() );
        SW_EXPECT_NOT_NULL( pEngineConfig );
        SW_EXPECT_EQUAL( 640u, pEngineConfig->_window._width );
    }

    // 2) 베이크된 JSON 도 없으면 C++ 기본값이다 — nullptr 이 아니다.
    {
        ConfigManager manager;
        EngineConfig* pEngineConfig = manager.ensureConfig<EngineConfig>( missingPath, nullptr );
        SW_EXPECT_NOT_NULL( pEngineConfig );
        SW_EXPECT_EQUAL( EngineConfig{}._window._width, pEngineConfig->_window._width );
        SW_EXPECT_EQUAL( pEngineConfig, manager.getConfig<EngineConfig>() );
    }

    // 3) 깨진 JSON 은 로드 실패다 — 절반만 채워 넣지 않는다.
    {
        ConfigManager manager;
        SW_EXPECT_FALSE( manager.loadConfigFromJson<EngineConfig>( string( "{ this is not json" ), "test" ) );
        SW_EXPECT_NULL( manager.getConfig<EngineConfig>() );
    }
}
