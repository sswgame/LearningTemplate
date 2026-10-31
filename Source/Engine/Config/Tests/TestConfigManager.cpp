// ConfigManagerTest — 설정 표의 열쇠(타입), 필수 · 선택 설정의 없음(폴백 없음), 제자리 다시 읽기와 알림, 틀린 파일의 키 이름 보고(nogpu).
// 여기서 잘못되면 증상이 "창 크기 · VSync · 리소스 우선순위가 조용히 기본값이 된다" 라서, 실행해 보고도 원인을 짚기 어렵다.
// 상대 경로는 프로젝트 루트 아래 한 곳뿐이라(`ConfigManager::resolveConfigPath`) 옛 루트 지정(`setRootDirectory`) 케이스는 없다.
#include "pch.h"

#include "Core/Delegate/Delegate.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "sw/config/ShippingHostDefaults.h"

using namespace sw;

namespace
{
    /**
     * @brief 바탕 층 파일(구성 때 베이크한 `EngineConfig.json` 사본)을 문서로 엽니다.
     * @details `EngineConfig` 는 값이 파일에만 있어 일부 키만 적은 JSON 은 범위 검사에서 진다 — 테스트도 온전한 파일에서 시작한다.
     */
    [[nodiscard]] bool openEngineConfigDocument( JSONDocument& outDocument )
    {
        return outDocument.parse( shipping_host::kEngineConfigJSON, "EngineConfig.json (configure-time copy)" );
    }

    /** @brief 바탕 층 사본에서 창 크기만 바꾼 JSON 입니다. 사본이 읽히지 않으면 빈 글입니다(부르는 쪽의 읽기가 실패한다). */
    string makeEngineConfigJSON( uint32 width, uint32 height )
    {
        JSONDocument document;
        if ( openEngineConfigDocument( document ) == false )
            return string();
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

    SW_REQUIRE_TRUE( manager.loadConfigFromJSON<EngineConfig>( makeEngineConfigJSON( 1600, 900 ), "test" ) );

    EngineConfig* pEngineConfig = manager.getConfig<EngineConfig>();
    SW_REQUIRE_NOT_NULL( pEngineConfig );
    SW_EXPECT_EQUAL( 1600u, pEngineConfig->_window._width );

    // 다른 설정 타입은 아직 없다 — 같은 표에 섞이지 않는다.
    SW_EXPECT_NULL( manager.getConfig<GameConfig>() );

    // 다시 읽으면 덮어쓴다(같은 객체에).
    SW_REQUIRE_TRUE( manager.loadConfigFromJSON<EngineConfig>( makeEngineConfigJSON( 800, 600 ), "test" ) );
    SW_EXPECT_TRUE( manager.getConfig<EngineConfig>() == pEngineConfig );
    SW_EXPECT_EQUAL( 800u, pEngineConfig->_window._width );
}

/**
 * @brief [ConfigManagerTest] 필수 설정 파일이 없으면 실패하고, 베이크한 JSON · C++ 기본값으로 대신하지 않는다
 * @details 설정이 없는데 서는 것은 값이 어디서 왔는지 아무도 모르는 상태다. 선택 설정(`loadOptionalConfig`)만 없음(`Missing`)을 돌려준다.
 */
SW_TEST_CASE( ConfigManagerTest, MissingRequiredFileFails )
{
    const string missingPath = test::makeScratchPath( "config_that_does_not_exist.json" ); // 케이스 폴더는 비어 있다 — 만들지 않는다

    // 1) 필수 설정은 파일이 없으면 nullptr 이다. 값이 파일에만 있는 EngineConfig 도, 헤더에 기본값이 있는 GameConfig 도 같다.
    SW_EXPECT_LOG( "Config.RequiredMissing" );
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

    // 2) 선택 설정은 없음을 돌려주고 아무것도 등록하지 않는다(로그도 없다).
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
        SW_REQUIRE_NOT_NULL( pEngineConfig );
        SW_EXPECT_EQUAL( 640u, pEngineConfig->_window._width );
    }

    // 4) 깨진 JSON 은 로드 실패다 — 절반만 채워 넣지 않는다.
    SW_EXPECT_LOG( "JSON.ParseError" );
    SW_EXPECT_LOG( "Config.NotJSONObject" );
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
    const string path = test::makeScratchPath( "ReloadEngineConfig.json" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, makeEngineConfigJSON( 800, 600 ) ) );

    ConfigManager manager;
    SW_REQUIRE_TRUE( manager.loadConfig<EngineConfig>( path ) );
    const EngineConfig* pBefore = manager.getConfig<EngineConfig>();
    SW_REQUIRE_NOT_NULL( pBefore );
    SW_EXPECT_EQUAL( 1u, manager.getReloadableCount() );

    uint32             notifyCount{ 0 };
    hashed_string      notifiedType{};
    const Subscription reloadSubscription = manager.onConfigReloaded().add( SW_DELEGATE_LAMBDA( Delegate<void( const hashed_string& )>, [&notifyCount, &notifiedType]( const hashed_string& typeName )
    {
        ++notifyCount;
        notifiedType = typeName;
    } ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, makeEngineConfigJSON( 1024, 768 ) ) );
    SW_EXPECT_TRUE( manager.reloadConfigFile( path ) == ReloadOutcome::Reloaded );
    SW_EXPECT_TRUE_MSG( manager.getConfig<EngineConfig>() == pBefore, "the reload replaced the config object (a kept pointer dangles)" );
    SW_EXPECT_EQUAL( 1024u, pBefore->_window._width );
    SW_EXPECT_EQUAL( 1u, notifyCount );
    SW_EXPECT_TRUE( notifiedType == EngineConfig::StaticType()->_fullyQualifiedName );

    // 모르는 경로는 아무것도 하지 않는다.
    SW_EXPECT_TRUE( manager.reloadConfigFile( test::makeScratchPath( "Other.json" ) ) == ReloadOutcome::NotTracked );
    // 깨진 JSON 은 이전 값을 지킨다.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "{ \"_window\": { \"_width\": " ) );
    SW_EXPECT_LOG( "JSON.ParseError" );
    SW_EXPECT_LOG( "Config.NotJSONObject" );
    SW_EXPECT_LOG( "Config.ReloadInvalid" );
    SW_EXPECT_TRUE( manager.reloadConfigFile( path ) == ReloadOutcome::Failed );
    SW_EXPECT_EQUAL( 1024u, pBefore->_window._width );
    SW_EXPECT_EQUAL( 1u, notifyCount );
}

namespace
{
    /** @brief 틀린 설정 한 줄 — 온전한 바탕 층 사본의 맨 위에 넣을 키와 값, 그리고 나와야 할 로그 id 입니다. */
    struct InvalidEngineConfigKey
    {
        const utf8* _pKey;
        float64     _value;
        const utf8* _pExpectedID;
    };

    const InvalidEngineConfigKey kArrInvalidEngineConfigKey[] = {
        { "_maxFrameDeltaTme", 0.2,             "Config.KeyUnknown"}, // 모르는 키
        {"_MaxFrameDeltaTime", 0.2,             "Config.KeyUnknown"}, // 대소문자만 다르다
        {   "_fixedDeltaTime", 0.0, "Serialization.ValueOutOfRange"}, // Min 밖
    };
} // namespace

/**
 * @brief [ConfigManagerTest] 틀린 설정 파일은 기본값으로 떨어지지 않고 nullptr — 모르는 키 · 대소문자만 다른 키 · 범위 밖 값을 이름으로 알린다
 * @details 베이크한 사본으로 조용히 떨어지면 고친 값이 무시된 것을 아무도 모른다. 파일이 없을 때도 같다(`MissingRequiredFileFails`).
 *          틀린 줄 하나만 보려고 온전한 바탕 층 사본에 그 줄을 더한다(일부 키만 적으면 빠진 키의 범위 오류가 섞인다).
 */
SW_TEST_CASE_EACH( ConfigManagerTest, InvalidFileStopsAndNamesTheKey, kArrInvalidEngineConfigKey )
{
    JSONDocument document;
    SW_REQUIRE_TRUE( openEngineConfigDocument( document ) );
    document.getRoot().set( param._pKey ).setFloat( param._value );
    const string path = test::makeScratchPath( "InvalidEngineConfig.json" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, document.dump() ) );

    SW_EXPECT_LOG( param._pExpectedID );
    ConfigManager manager;
    SW_EXPECT_TRUE_MSG( manager.loadHostConfig<EngineConfig>( ConfigSource::disk( path ) ) == nullptr, param._pKey );
    SW_EXPECT_NULL( manager.getConfig<EngineConfig>() );
}

/**
 * @brief [ConfigManagerTest] 맞는 파일은 그대로 읽힌다 — 엄격하게 바꿔도 정상 파일 · 구조체 칸 · enum 칸이 실패하지 않는다(엄격 검사의 짝)
 */
SW_TEST_CASE( ConfigManagerTest, ValidFileWithNestedStructLoads )
{
    ConfigManager manager;
    JSONDocument  document;
    SW_REQUIRE_TRUE( document.parse( makeEngineConfigJSON( 800, 600 ) ) );
    document.getRoot().get( "_window" ).set( "_defaultRHI" ).setString( "Vulkan" );
    document.getRoot().set( "_fixedDeltaTime" ).setFloat( 0.02 );
    SW_REQUIRE_TRUE( manager.loadConfigFromJSON<EngineConfig>( document.dump(), "test" ) );
    SW_REQUIRE_NOT_NULL( manager.getConfig<EngineConfig>() );
    SW_EXPECT_EQUAL( 800u, manager.getConfig<EngineConfig>()->_window._width );
    SW_EXPECT_TRUE( manager.getConfig<EngineConfig>()->_window._defaultRHI == RHIBackend::Vulkan );
}
