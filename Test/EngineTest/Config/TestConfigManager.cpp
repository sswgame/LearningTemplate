#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ShippingHostDefaults.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) ConfigManagerTest — 설정 표의 열쇠, 경로 해석, 폴백 사슬
//
//    여기서 잘못되면 증상이 "창 크기·VSync·리소스 우선순위가 조용히 기본값이 된다" 라서,
//    실행해 보고도 원인을 짚기 어렵다.
// ------------------------------------------------------------------------------

namespace
{
    /**
     * @brief 바탕 층 파일(구성 때 베이크한 `EngineConfig.json` 사본)에서 창 크기만 바꾼 JSON 을 만듭니다.
     * @details `EngineConfig` 는 값이 파일에만 있어 일부 키만 적은 JSON 은 범위 검사에서 진다 — 테스트도 온전한 파일에서 시작한다.
     */
    string makeEngineConfigJSON( uint32 width, uint32 height )
    {
        JSONDocument document;
        const bool   bParsed = document.parse( shipping_host::kEngineConfigJSON );
        SW_LOG_ASSERT( bParsed, "the configure-time EngineConfig.json copy must parse" );
        (void)bParsed;
        const JSONValue window = document.getRoot().get( "_window" );
        window.set( "_width" ).setUint( width );
        window.set( "_height" ).setUint( height );
        return document.dump();
    }
} // namespace

/**
 * @brief [ConfigManagerTest] 설정 표의 열쇠가 **타입**인지 검증
 * @details 호출부가 이름(`hashed_string( "EngineConfig" )`)을 손으로 적어 넘기지 않으므로 철자가 어긋날 자리가 없고,
 *          다른 타입을 물으면 그 타입이 없다고 답한다.
 */
SW_TEST_CASE( ConfigManagerTest, ConfigTableIsKeyedByType )
{
    ConfigManager manager;

    SW_EXPECT_NULL( manager.getConfig<EngineConfig>() );
    SW_EXPECT_NULL( manager.getConfig<GameConfig>() );

    SW_EXPECT_TRUE( manager.loadConfigFromJSON<EngineConfig>( makeEngineConfigJSON( 1600, 900 ), "test" ) );

    EngineConfig* pEngineConfig = manager.getConfig<EngineConfig>();
    SW_EXPECT_NOT_NULL( pEngineConfig );
    SW_EXPECT_EQUAL( 1600u, pEngineConfig->_window._width );

    // 다른 설정 타입은 아직 없다 — 같은 표에 섞이지 않는다.
    SW_EXPECT_NULL( manager.getConfig<GameConfig>() );

    // 다시 읽으면 덮어쓴다.
    SW_EXPECT_TRUE( manager.loadConfigFromJSON<EngineConfig>( makeEngineConfigJSON( 800, 600 ), "test" ) );
    SW_EXPECT_EQUAL( 800u, manager.getConfig<EngineConfig>()->_window._width );
}

/**
 * @brief [ConfigManagerTest] 상대 경로를 루트 디렉터리 기준으로 찾는지 검증
 * @details 실행 파일은 `build/<preset>/Bin` 에서 돌고 `Config/` 는 프로젝트 루트에 있다. 작업 디렉터리 기준으로만
 *          찾으면 설정 셋이 **매 실행마다 전부 "없음"** 이 되어 생성된 기본값으로 조용히 떨어진다(창 크기·VSync·
 *          리소스 우선순위가 전부 무시된다). `setRootDirectory` 가 그것을 막는다.
 */
SW_TEST_CASE( ConfigManagerTest, RelativePathResolvesAgainstRootDirectory )
{
#if defined( SW_SHIPPING )
    SW_TEST_REQUIRES_FEATURE( DevelopmentBuild ); // Shipping 은 디스크의 Config/ 를 보지 않는다 — 생성된 JSON 만 쓴다
#else
    test::ScopedLogSuppressor suppressor;

    const string rootDir      = test::makeTempPath( "sw_config_root_test" );
    const string relativePath = "TestEngineConfig.json";
    const string absolutePath = FileUtil::joinPath( rootDir, relativePath );

    SW_EXPECT_TRUE( FileUtil::ensureDirectoryExists( rootDir ) );
    SW_EXPECT_TRUE( FileUtil::writeTextFile( absolutePath, makeEngineConfigJSON( 1920, 1080 ) ) );

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

    SW_EXPECT_TRUE( FileUtil::removeFile( absolutePath ) );
#endif
}

/**
 * @brief [ConfigManagerTest] 필수 설정 파일이 없으면 실패하고, 베이크한 JSON · C++ 기본값으로 대신하지 않는다
 * @details 설정이 없는데 서는 것은 값이 어디서 왔는지 아무도 모르는 상태다. 선택 설정(`loadOptionalConfig`)만 없음(`Missing`)을 돌려준다.
 */
SW_TEST_CASE( ConfigManagerTest, MissingRequiredFileFails )
{
    test::ScopedLogSuppressor suppressor;

    const string missingPath = test::makeTempPath( "sw_config_that_does_not_exist.json" );
    SW_ASSERT_TRUE( FileUtil::removeFile( missingPath ) );

    // 1) 필수 설정은 파일이 없으면 nullptr 이다. 값이 파일에만 있는 EngineConfig 도, 헤더에 기본값이 있는 GameConfig 도 같다.
    {
        ConfigManager manager;
        SW_EXPECT_NULL( manager.loadHostConfig<EngineConfig>( ConfigSource::disk( missingPath ) ) );
        SW_EXPECT_NULL( manager.getConfig<EngineConfig>() );
    }
    {
        ConfigManager manager;
        SW_EXPECT_NULL( manager.loadHostConfig<GameConfig>( ConfigSource::disk( missingPath ) ) );
        SW_EXPECT_NULL( manager.getConfig<GameConfig>() );
    }

    // 2) 선택 설정은 없음을 돌려주고 아무것도 등록하지 않는다.
    {
        ConfigManager manager;
        SW_EXPECT_TRUE( manager.loadOptionalConfig<GameConfig>( ConfigSource::disk( missingPath ) ) == ConfigReadResult::Missing );
        SW_EXPECT_NULL( manager.getConfig<GameConfig>() );
    }

    // 3) 베이크한 JSON 은 출처를 명시한 때만 읽는다(Shipping 의 길).
    {
        ConfigManager manager;
        const string  baked         = makeEngineConfigJSON( 640, 480 );
        EngineConfig* pEngineConfig = manager.loadHostConfig<EngineConfig>( ConfigSource::baked( baked.c_str() ) );
        SW_EXPECT_NOT_NULL( pEngineConfig );
        SW_EXPECT_EQUAL( 640u, pEngineConfig->_window._width );
    }

    // 4) 깨진 JSON 은 로드 실패다 — 절반만 채워 넣지 않는다.
    {
        ConfigManager manager;
        SW_EXPECT_FALSE( manager.loadConfigFromJSON<EngineConfig>( string( "{ this is not json" ), "test" ) );
        SW_EXPECT_NULL( manager.getConfig<EngineConfig>() );
    }
}

/**
 * @brief [ConfigManagerTest] 파일에서 읽은 설정은 경로로 다시 읽히고, 객체는 그대로(포인터 유효) 값만 바뀌며 알림이 간다
 * @details 에디터가 `Config/` 를 감시해 바뀐 파일을 `reloadConfigFile` 로 넘긴다. 새 객체로 바꾸면 기동 때 받아 둔 포인터
 *          (`Engine::_pEngineConfig`)가 댕글링이 된다. 깨진 JSON 은 이전 값을 지키고, 모르는 경로는 아무것도 하지 않는다.
 */
SW_TEST_CASE( ConfigManagerTest, ReloadConfigFileUpdatesInPlaceAndNotifies )
{
    test::ScopedLogSuppressor suppressor;

    const string rootDir = test::makeTempPath( "sw_config_reload_test" );
    const string path    = FileUtil::joinPath( rootDir, "ReloadEngineConfig.json" );
    SW_EXPECT_TRUE( FileUtil::ensureDirectoryExists( rootDir ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, makeEngineConfigJSON( 800, 600 ) ) );

    ConfigManager manager;
    SW_ASSERT_TRUE( manager.loadConfig<EngineConfig>( path ) );
    const EngineConfig* pBefore = manager.getConfig<EngineConfig>();
    SW_ASSERT_NOT_NULL( pBefore );
    SW_EXPECT_EQUAL( 1u, manager.getReloadableCount() );

    uint32             notifyCount{ 0 };
    hashed_string      notifiedType{};
    const Subscription reloadSubscription = manager.onConfigReloaded().add( SW_DELEGATE_LAMBDA( Delegate<void( const hashed_string& )>, [&notifyCount, &notifiedType]( const hashed_string& typeName )
    {
        ++notifyCount;
        notifiedType = typeName;
    } ) );

    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, makeEngineConfigJSON( 1024, 768 ) ) );
    SW_EXPECT_TRUE( manager.reloadConfigFile( path ) == ReloadOutcome::Reloaded );
    SW_EXPECT_TRUE_MSG( manager.getConfig<EngineConfig>() == pBefore, "다시 읽으며 설정 객체를 바꿨습니다(받아 둔 포인터가 댕글링이 된다)" );
    SW_EXPECT_EQUAL( 1024u, pBefore->_window._width );
    SW_EXPECT_EQUAL( 1u, notifyCount );
    SW_EXPECT_TRUE( notifiedType == EngineConfig::StaticType()->_fullyQualifiedName );

    // 모르는 경로는 아무것도 하지 않는다.
    SW_EXPECT_TRUE( manager.reloadConfigFile( FileUtil::joinPath( rootDir, "Other.json" ) ) == ReloadOutcome::NotTracked );
    // 깨진 JSON 은 이전 값을 지킨다.
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, "{ \"_window\": { \"_width\": " ) );
    SW_EXPECT_TRUE( manager.reloadConfigFile( path ) == ReloadOutcome::Failed );
    SW_EXPECT_EQUAL( 1024u, pBefore->_window._width );
    SW_EXPECT_EQUAL( 1u, notifyCount );
}

/**
 * @brief [ConfigManagerTest] 틀린 설정 파일은 기본값으로 떨어지지 않고 nullptr — 모르는 키 · 대소문자만 다른 키 · 범위 밖 값을 이름으로 알린다
 * @details 베이크한 사본으로 조용히 떨어지면 고친 값이 무시된 것을 아무도 모른다. 파일이 없을 때도 같다(`MissingRequiredFileFails`).
 */
SW_TEST_CASE( ConfigManagerTest, InvalidFileStopsAndNamesTheKey )
{
#if defined( SW_SHIPPING )
    SW_TEST_REQUIRES_FEATURE( DevelopmentBuild ); // Shipping 은 디스크의 Config/ 를 보지 않는다
#else
    const string rootDir = test::makeTempPath( "sw_config_invalid_test" );
    const string path    = FileUtil::joinPath( rootDir, "InvalidEngineConfig.json" );
    SW_EXPECT_TRUE( FileUtil::ensureDirectoryExists( rootDir ) );

    struct Case
    {
        const utf8* _pJSON;
        const utf8* _pExpectedID;
    };
    const Case arrCase[] = {
        {      "{ \"_maxFrameDeltaTme\": 0.2 }",             "Config.KeyUnknown"}, // 모르는 키
        {     "{ \"_MaxFrameDeltaTime\": 0.2 }",             "Config.KeyUnknown"}, // 대소문자만 다르다
        {          "{ \"_fixedDeltaTime\": 0 }", "Serialization.ValueOutOfRange"}, // Min 밖
        {"{ \"_window\": { \"_widht\": 800 } }",   "Serialization.RestoreFailed"}, // 구조체 칸 안의 모르는 키는 바깥 칸(_window) 이름으로 읽기 실패를 알린다
    };
    for ( const Case& testCase : arrCase )
    {
        SW_ASSERT_TRUE( FileUtil::writeTextFile( path, testCase._pJSON ) );
        test::ScopedDefensiveTestLog defensive( "틀린 설정 파일을 일부러 읽는다" );
        test::ScopedLogCollector     logs;
        ConfigManager                manager;
        SW_EXPECT_TRUE_MSG( manager.loadHostConfig<EngineConfig>( ConfigSource::disk( path ) ) == nullptr, testCase._pJSON );
        SW_EXPECT_NULL( manager.getConfig<EngineConfig>() );
        SW_EXPECT_TRUE_MSG( logs.countWithID( testCase._pExpectedID ) >= 1u, string( testCase._pJSON ) + logs.joined() );
    }
    SW_EXPECT_TRUE( FileUtil::removeFile( path ) );
#endif
}

/**
 * @brief [ConfigManagerTest] 맞는 파일은 그대로 읽힌다 — 엄격하게 바꿔도 정상 파일 · 구조체 칸 · enum 칸이 실패하지 않는다(엄격 검사의 짝)
 */
SW_TEST_CASE( ConfigManagerTest, ValidFileWithNestedStructLoads )
{
    ConfigManager manager;
    JSONDocument  document;
    SW_ASSERT_TRUE( document.parse( makeEngineConfigJSON( 800, 600 ) ) );
    document.getRoot().get( "_window" ).set( "_defaultRHI" ).setString( "Vulkan" );
    document.getRoot().set( "_fixedDeltaTime" ).setFloat( 0.02 );
    SW_EXPECT_TRUE( manager.loadConfigFromJSON<EngineConfig>( document.dump(), "test" ) );
    SW_ASSERT_NOT_NULL( manager.getConfig<EngineConfig>() );
    SW_EXPECT_EQUAL( 800u, manager.getConfig<EngineConfig>()->_window._width );
}
