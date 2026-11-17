// ConfigManagerTest — 설정 표의 열쇠(타입), 필수 · 선택 설정의 없음(폴백 없음), 제자리 다시 읽기와 알림, 틀린 파일의 키 이름 보고, 플랫폼 · 품질 단계 층(nogpu).
// 여기서 잘못되면 증상이 "창 크기 · VSync · 리소스 우선순위가 조용히 기본값이 된다" 라서, 실행해 보고도 원인을 짚기 어렵다.
// 상대 경로는 프로젝트 루트 아래 한 곳뿐이라(`ConfigManager::resolveConfigPath`) 옛 루트 지정(`setRootDirectory`) 케이스는 없다.
#include "pch.h"

#include "Core/Delegate/Delegate.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PlatformConfigOverride.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "sw/config/ConfigConstants.h"
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
        return document.dump().value_or( string() ); // 못 쓰면 빈 글 - 부르는 쪽의 읽기가 실패한다
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
    SW_EXPECT_TRUE( manager.reloadConfigFile( path ) == ReloadResult::Reloaded );
    SW_EXPECT_TRUE_MSG( manager.getConfig<EngineConfig>() == pBefore, "the reload replaced the config object (a kept pointer dangles)" );
    SW_EXPECT_EQUAL( 1024u, pBefore->_window._width );
    SW_EXPECT_EQUAL( 1u, notifyCount );
    SW_EXPECT_TRUE( notifiedType == EngineConfig::StaticType()->_fullyQualifiedName );

    // 모르는 경로는 아무것도 하지 않는다.
    SW_EXPECT_TRUE( manager.reloadConfigFile( test::makeScratchPath( "Other.json" ) ) == ReloadResult::NotTracked );
    // 깨진 JSON 은 이전 값을 지킨다.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "{ \"_window\": { \"_width\": " ) );
    SW_EXPECT_LOG( "JSON.ParseError" );
    SW_EXPECT_LOG( "Config.NotJSONObject" );
    SW_EXPECT_LOG( "Config.ReloadInvalid" );
    SW_EXPECT_TRUE( manager.reloadConfigFile( path ) == ReloadResult::Failed );
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
    const string           path = test::makeScratchPath( "InvalidEngineConfig.json" );
    const expected<string> text = document.dump();
    SW_REQUIRE_TRUE( text.has_value() );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, *text ) );

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
    const expected<string> text = document.dump();
    SW_REQUIRE_TRUE( text.has_value() );
    SW_REQUIRE_TRUE( manager.loadConfigFromJSON<EngineConfig>( *text, "test" ) );
    SW_REQUIRE_NOT_NULL( manager.getConfig<EngineConfig>() );
    SW_EXPECT_EQUAL( 800u, manager.getConfig<EngineConfig>()->_window._width );
    SW_EXPECT_TRUE( manager.getConfig<EngineConfig>()->_window._defaultRHI == RHIBackend::Vulkan );
}

/**
 * @brief [ConfigManagerTest] 플랫폼 덮어쓰기 합치기 — 둘 다 객체인 칸은 키마다 안으로, 값 · 배열은 덮어쓰기가 통째로 이기고, 객체가 아닌 덮어쓰기는 오류다
 */
SW_TEST_CASE( ConfigManagerTest, PlatformOverrideMergesObjectsAndReplacesValues )
{
    string merged;
    SW_REQUIRE_TRUE( PlatformConfigOverride::apply( R"({"_a":1,"_list":[1,2],"_window":{"_width":800,"_height":600}})",
                                                    R"({"_list":[3],"_window":{"_width":1280},"_new":true})", "test override", merged ) );
    JSONDocument document;
    SW_REQUIRE_TRUE( document.parse( merged ) );
    const JSONValue root = document.getRoot();
    SW_EXPECT_EQUAL( 1, root.get( "_a" ).read<int32>().value_or( 0 ) );
    SW_EXPECT_EQUAL( size_t( 1 ), root.get( "_list" ).size() ); // 배열은 원소 단위로 섞지 않는다
    SW_EXPECT_EQUAL( 1280, root.get( "_window" ).get( "_width" ).read<int32>().value_or( 0 ) );
    SW_EXPECT_EQUAL( 600, root.get( "_window" ).get( "_height" ).read<int32>().value_or( 0 ) ); // 객체 칸은 덮은 키만 바뀐다
    SW_EXPECT_TRUE( root.get( "_new" ).isValid() );                                             // 모르는 키는 옮긴다 — 엄격 읽기가 이름으로 알린다

    SW_EXPECT_LOG( "Config.OverrideNotObject" );
    SW_EXPECT_FALSE( PlatformConfigOverride::apply( R"({"_a":1})", "[1]", "test override", merged ) );
    SW_EXPECT_TRUE( PlatformConfigOverride::makeEngineConfigPath( "Android" ) == FileUtil::joinPath( FileUtil::joinPath( config::kDirRuntimePlatformConfig, "Android" ), "EngineConfig.json" ) );
}

/**
 * @brief [ConfigManagerTest] 디스크 설정에 덮어쓰기 파일이 바탕 위에 선다 — 없으면 바탕만, 덮어쓰기의 모르는 키는 오류, 덮어쓰기 파일이 바뀌어도 제자리에서 다시 읽는다
 * @details 언리얼 플랫폼 ini 층(`Config/<Platform>/<Platform>Engine.ini`)과 같은 자리(A-23). 엔진 기동은 `PlatformConfigOverride::makeEngineConfigPath()` 를 넘긴다.
 */
SW_TEST_CASE( ConfigManagerTest, PlatformOverrideFileLayersOnTheBase )
{
    const string basePath     = test::makeScratchPath( "LayeredEngineConfig.json" );
    const string overridePath = test::makeScratchPath( "LayeredEngineConfig.Platform.json" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( basePath, makeEngineConfigJSON( 800, 600 ) ) );

    // 1) 덮어쓰기 파일이 없으면 바탕만 읽는다(선택 층).
    {
        ConfigManager manager;
        SW_REQUIRE_TRUE( manager.loadConfigFile<EngineConfig>( basePath, overridePath ) == ConfigReadResult::Loaded );
        SW_EXPECT_EQUAL( 800u, manager.getConfig<EngineConfig>()->_window._width );
    }

    // 2) 덮어쓰기는 적은 키만 바꾸고, 덮어쓰기 파일이 바뀌면 다시 읽는다.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( overridePath, R"({ "_window": { "_width": 1280 } })" ) );
    {
        ConfigManager manager;
        SW_REQUIRE_TRUE( manager.loadHostConfig<EngineConfig>( ConfigSource::disk( basePath, overridePath ) ) != nullptr );
        const EngineConfig* pConfig = manager.getConfig<EngineConfig>();
        SW_EXPECT_EQUAL( 1280u, pConfig->_window._width );
        SW_EXPECT_EQUAL( 600u, pConfig->_window._height );

        SW_REQUIRE_TRUE( FileUtil::writeTextFile( overridePath, R"({ "_window": { "_width": 1920 } })" ) );
        SW_EXPECT_TRUE( manager.reloadConfigFile( overridePath ) == ReloadResult::Reloaded );
        SW_EXPECT_TRUE( manager.getConfig<EngineConfig>() == pConfig );
        SW_EXPECT_EQUAL( 1920u, pConfig->_window._width );
    }

    // 3) 구운 바탕에는 구운 덮어쓰기 글이 같은 규칙으로 선다(정적 구성의 길).
    {
        ConfigManager manager;
        const string  baked  = makeEngineConfigJSON( 640, 480 );
        EngineConfig* pBaked = manager.loadHostConfig<EngineConfig>( ConfigSource::baked( baked.c_str(), R"({ "_window": { "_height": 360 } })" ) );
        SW_REQUIRE_NOT_NULL( pBaked );
        SW_EXPECT_EQUAL( 640u, pBaked->_window._width );
        SW_EXPECT_EQUAL( 360u, pBaked->_window._height );
    }

    // 4) 덮어쓰기의 모르는 키는 바탕과 같은 엄격 읽기에서 진다 — 기본값으로 떨어지지 않는다.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( overridePath, R"({ "_windw": { "_width": 1280 } })" ) );
    SW_EXPECT_LOG( "Config.KeyUnknown" );
    {
        ConfigManager manager;
        SW_EXPECT_NULL( manager.loadHostConfig<EngineConfig>( ConfigSource::disk( basePath, overridePath ) ) );
    }

    // 5) 구조체 칸 안의 모르는 키도 모르는 키 하나로 끝난다 — 읽지 못한 `_render` 를 읽기 전 값(새 객체의 0, 범위 밖)으로 되돌리는 길이 범위 검사로 덧나지 않는다.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( overridePath, R"({ "_render": { "_shadowCullSlots": 4 } })" ) );
    {
        ConfigManager manager;
        SW_EXPECT_NULL( manager.loadHostConfig<EngineConfig>( ConfigSource::disk( basePath, overridePath ) ) );
    }
}

/**
 * @brief [ConfigManagerTest] 품질 단계는 바탕 → 플랫폼 → 단계 순으로 선다 — 단계가 이기고, 단계가 적지 않은 키는 플랫폼 값이 남고, 단계 없음은 바탕(+ 플랫폼) 그대로다
 * @details 언리얼 스케일러빌리티 그룹 · 유니티 SRP 품질 에셋 자리(RN-16). 단계 표가 바뀌어도 제자리에서 다시 읽는다.
 *          프로젝트 설정 창이 받는 경로(`forEachFileSource`)는 바탕 파일 하나다 — 창은 그 파일 글을 열고 쓰므로 합친 값(플랫폼 · 단계)이 바탕 파일로 새지 않는다.
 */
SW_TEST_CASE( ConfigManagerTest, RenderQualityTierLayersAfterPlatform )
{
    const string basePath     = test::makeScratchPath( "QualityEngineConfig.json" );
    const string overridePath = test::makeScratchPath( "QualityEngineConfig.Platform.json" );
    const string qualityPath  = test::makeScratchPath( "QualityRenderQuality.json" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( basePath, makeEngineConfigJSON( 800, 600 ) ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( overridePath, R"({ "_window": { "_width": 1280 }, "_render": { "_shadowLocalCullSlotLimit": 10, "_maxExtraViewCount": 5 } })" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( qualityPath, R"({ "low": { "_shadowLocalCullSlotLimit": 4, "_textureStreaming": { "_poolMegabytes": 256 } }, "high": {} })" ) );

    ConfigManager baseOnly;
    SW_REQUIRE_TRUE( baseOnly.loadConfigFile<EngineConfig>( basePath ) == ConfigReadResult::Loaded );
    const RenderConfig& baseRender = baseOnly.getConfig<EngineConfig>()->_render;

    // 1) 단계가 플랫폼 위에 선다 — 같은 키는 단계가, 단계가 적지 않은 키는 플랫폼이, 둘 다 적지 않은 키는 바탕이 남는다.
    {
        ConfigManager       manager;
        const EngineConfig* pConfig = manager.loadHostConfig<EngineConfig>( ConfigSource::disk( basePath, overridePath ).withRenderQuality( qualityPath, "low" ) );
        SW_REQUIRE_NOT_NULL( pConfig );
        SW_EXPECT_EQUAL( 4u, pConfig->_render._shadowLocalCullSlotLimit );
        SW_EXPECT_EQUAL( 5u, pConfig->_render._maxExtraViewCount );
        SW_EXPECT_EQUAL( 256u, pConfig->_render._textureStreaming._poolMegabytes );
        SW_EXPECT_EQUAL( baseRender._textureStreaming._uploadsPerFrame, pConfig->_render._textureStreaming._uploadsPerFrame ); // 구조체 칸도 적은 키만 바뀐다
        SW_EXPECT_EQUAL( 1280u, pConfig->_window._width );

        // 프로젝트 설정 창은 바탕 파일만 받는다(합친 값을 쓸 자리가 없다).
        uint32 sourceCount = 0;
        manager.forEachFileSource( [&]( const hashed_string&, string_view resolvedPath )
        {
            ++sourceCount;
            SW_EXPECT_TRUE( FileUtil::isSameDiskPath( resolvedPath, basePath ) );
        } );
        SW_EXPECT_EQUAL( 1u, sourceCount );

        // 2) 단계 표가 바뀌면 제자리에서 다시 읽는다.
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( qualityPath, R"({ "low": { "_shadowLocalCullSlotLimit": 2 }, "high": {} })" ) );
        SW_EXPECT_TRUE( manager.reloadConfigFile( qualityPath ) == ReloadResult::Reloaded );
        SW_EXPECT_TRUE( manager.getConfig<EngineConfig>() == pConfig );
        SW_EXPECT_EQUAL( 2u, pConfig->_render._shadowLocalCullSlotLimit );
    }

    // 3) 단계 없음(빈 이름)과 빈 단계(high)는 아래 층 그대로다.
    for ( const utf8* pTier : { "", "high" } )
    {
        ConfigManager       manager;
        const EngineConfig* pConfig = manager.loadHostConfig<EngineConfig>( ConfigSource::disk( basePath, overridePath ).withRenderQuality( qualityPath, pTier ) );
        SW_REQUIRE_NOT_NULL( pConfig );
        SW_EXPECT_EQUAL( 10u, pConfig->_render._shadowLocalCullSlotLimit );
    }
    {
        ConfigManager       manager;
        const EngineConfig* pConfig = manager.loadHostConfig<EngineConfig>( ConfigSource::disk( basePath ).withRenderQuality( qualityPath, "" ) );
        SW_REQUIRE_NOT_NULL( pConfig );
        SW_EXPECT_EQUAL( baseRender._shadowLocalCullSlotLimit, pConfig->_render._shadowLocalCullSlotLimit );
        SW_EXPECT_EQUAL( baseRender._maxExtraViewCount, pConfig->_render._maxExtraViewCount );
    }

    // 4) 구운 출처도 같은 순서로 선다(정적 구성의 길).
    {
        ConfigManager       manager;
        const string        baked   = makeEngineConfigJSON( 640, 480 );
        const EngineConfig* pConfig = manager.loadHostConfig<EngineConfig>(
            ConfigSource::baked( baked.c_str(), R"({ "_render": { "_shadowLocalCullSlotLimit": 10, "_maxExtraViewCount": 5 } })" )
                .withRenderQuality( R"({ "low": { "_shadowLocalCullSlotLimit": 4 } })", "low" ) );
        SW_REQUIRE_NOT_NULL( pConfig );
        SW_EXPECT_EQUAL( 4u, pConfig->_render._shadowLocalCullSlotLimit );
        SW_EXPECT_EQUAL( 5u, pConfig->_render._maxExtraViewCount );
    }
}

/**
 * @brief [ConfigManagerTest] 없는 단계 이름 · 단계의 모르는 키 · `_render` 밖 칸 · 단계를 골랐는데 표가 없는 것은 모두 로드 오류다(바탕으로 떨어지지 않는다)
 */
SW_TEST_CASE( ConfigManagerTest, RenderQualityRejectsUnknownTierAndKey )
{
    const string basePath    = test::makeScratchPath( "QualityRejectEngineConfig.json" );
    const string qualityPath = test::makeScratchPath( "QualityRejectRenderQuality.json" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( basePath, makeEngineConfigJSON( 800, 600 ) ) );

    // 1) 표가 없다.
    SW_EXPECT_LOG( "Config.RenderQualityMissing" );
    {
        ConfigManager manager;
        SW_EXPECT_NULL( manager.loadHostConfig<EngineConfig>( ConfigSource::disk( basePath ).withRenderQuality( qualityPath, "low" ) ) );
    }

    // 2) 없는 단계 이름.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( qualityPath, R"({ "low": {}, "high": {} })" ) );
    SW_EXPECT_LOG( "Config.RenderQualityUnknownTier" );
    {
        ConfigManager manager;
        SW_EXPECT_NULL( manager.loadHostConfig<EngineConfig>( ConfigSource::disk( basePath ).withRenderQuality( qualityPath, "nope" ) ) );
    }

    // 3) 단계의 모르는 키, 그리고 `_render` 밖 칸(단계는 `_render` 안쪽이라 `_window` 는 RenderConfig 의 모르는 키다).
    SW_EXPECT_LOG( "Config.KeyUnknown" );
    for ( const utf8* pTiers : { R"({ "low": { "_shadowCullSlots": 4 } })", R"({ "low": { "_window": { "_width": 1 } } })" } )
    {
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( qualityPath, pTiers ) );
        ConfigManager manager;
        SW_EXPECT_NULL( manager.loadHostConfig<EngineConfig>( ConfigSource::disk( basePath ).withRenderQuality( qualityPath, "low" ) ) );
    }

    // 4) 단계가 객체가 아니다.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( qualityPath, R"({ "low": 1 })" ) );
    SW_EXPECT_LOG( "Config.RenderQualityTierNotObject" );
    {
        ConfigManager manager;
        SW_EXPECT_NULL( manager.loadHostConfig<EngineConfig>( ConfigSource::disk( basePath ).withRenderQuality( qualityPath, "low" ) ) );
    }
}

/**
 * @brief [ConfigManagerTest] 커밋된 단계 표(구성 때 구운 사본)의 단계마다 커밋된 엔진 설정 위에 읽히고, 기본 단계 high(`gv_renderQuality`)는 바탕 값 그대로다
 * @details high 가 바탕을 바꾸면 기본 실행의 렌더 값이 바뀌어 골든이 흔들린다 — 바탕이 곧 high 다.
 */
SW_TEST_CASE( ConfigManagerTest, CommittedRenderQualityTiersLoad )
{
    JSONDocument tiers;
    SW_REQUIRE_TRUE( tiers.parse( shipping_host::kRenderQualityJSON, "RenderQuality.json (configure-time copy)" ) );
    const vector<string> listTier = tiers.getRoot().getMemberNames();
    SW_EXPECT_TRUE( listTier.empty() == false );

    ConfigManager baseOnly;
    SW_REQUIRE_NOT_NULL( baseOnly.loadHostConfig<EngineConfig>( ConfigSource::baked( shipping_host::kEngineConfigJSON ) ) );
    const expected<string> baseText = JSONSerializer::serialize( baseOnly.getConfig<EngineConfig>(), *EngineConfig::StaticType() );
    SW_REQUIRE_TRUE( baseText.has_value() );

    bool bHasHigh = false;
    for ( const string& tier : listTier )
    {
        ConfigManager       manager;
        const EngineConfig* pConfig = manager.loadHostConfig<EngineConfig>( ConfigSource::baked( shipping_host::kEngineConfigJSON ).withRenderQuality( shipping_host::kRenderQualityJSON, tier ) );
        SW_REQUIRE_TRUE_MSG( pConfig != nullptr, tier.c_str() );
        if ( tier != "high" )
            continue;
        bHasHigh                        = true;
        const expected<string> highText = JSONSerializer::serialize( pConfig, *EngineConfig::StaticType() );
        SW_REQUIRE_TRUE( highText.has_value() );
        SW_EXPECT_TRUE( *highText == *baseText );
    }
    SW_EXPECT_TRUE( bHasHigh );
}

/**
 * @brief [ConfigManagerTest] 단계를 바꾸면 엔진 설정을 제자리에서 다시 읽고 알린다 — 없는 단계는 이전 값 · 이전 단계를 두고, 파일 다시 읽기는 새 단계로 합친다
 * @details 사용자 옵션 `graphics.renderQuality` → `gv_renderQuality` → `applyRenderQuality` 의 끝(언리얼 ScalabilityQuality 즉시 적용).
 */
SW_TEST_CASE( ConfigManagerTest, RenderQualityAppliesInPlace )
{
    const string basePath    = test::makeScratchPath( "QualityApplyEngineConfig.json" );
    const string qualityPath = test::makeScratchPath( "QualityApplyRenderQuality.json" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( basePath, makeEngineConfigJSON( 800, 600 ) ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( qualityPath, R"({ "low": { "_shadowLocalCullSlotLimit": 4 }, "high": {} })" ) );

    ConfigManager manager;
    SW_EXPECT_LOG( "ConfigManager.RenderQualityBeforeLoad" );
    SW_EXPECT_FALSE( manager.applyRenderQuality( "low" ) ); // 읽은 출처가 없으면 다시 읽을 수 없다

    const EngineConfig* pConfig = manager.loadEngineConfig( ConfigSource::disk( basePath ).withRenderQuality( qualityPath, "high" ) );
    SW_REQUIRE_NOT_NULL( pConfig );
    const uint32       baseLimit          = pConfig->_render._shadowLocalCullSlotLimit;
    uint32             notifyCount        = 0;
    const Subscription reloadSubscription = manager.onConfigReloaded().add( SW_DELEGATE_LAMBDA( Delegate<void( const hashed_string& )>, [&notifyCount]( const hashed_string& )
    {
        ++notifyCount;
    } ) );

    // 1) 단계를 바꾸면 같은 객체에 새 값이 들고 한 번 알린다. 같은 단계는 다시 읽지 않는다.
    SW_EXPECT_TRUE( manager.applyRenderQuality( "low" ) );
    SW_EXPECT_TRUE( manager.getConfig<EngineConfig>() == pConfig );
    SW_EXPECT_EQUAL( 4u, pConfig->_render._shadowLocalCullSlotLimit );
    SW_EXPECT_TRUE( manager.applyRenderQuality( "low" ) );
    SW_EXPECT_EQUAL( 1u, notifyCount );

    // 2) 없는 단계는 오류이고 값과 단계는 그대로다.
    SW_EXPECT_LOG( "Config.RenderQualityUnknownTier" );
    SW_EXPECT_FALSE( manager.applyRenderQuality( "nope" ) );
    SW_EXPECT_EQUAL( 4u, pConfig->_render._shadowLocalCullSlotLimit );
    SW_EXPECT_EQUAL( 1u, notifyCount );

    // 3) 바탕 파일을 다시 읽어도 고른 단계(low)가 위에 선다.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( basePath, makeEngineConfigJSON( 1024, 600 ) ) );
    SW_EXPECT_TRUE( manager.reloadConfigFile( basePath ) == ReloadResult::Reloaded );
    SW_EXPECT_EQUAL( 1024u, pConfig->_window._width );
    SW_EXPECT_EQUAL( 4u, pConfig->_render._shadowLocalCullSlotLimit );

    // 4) 기본 단계로 돌아가면 바탕 값이다.
    SW_EXPECT_TRUE( manager.applyRenderQuality( "high" ) );
    SW_EXPECT_EQUAL( baseLimit, pConfig->_render._shadowLocalCullSlotLimit );
}

/**
 * @brief [ConfigManagerTest] 이미 읽은 엔진 설정을 덮는 길(품질 단계 바꾸기 · 파일 다시 읽기)은 울타리(렌더 구동기의 "받은 패킷을 모두 끝낸다")를 지난 뒤에만 새 값이 보인다
 * @details 울타리 안에서 본 값은 옛 값이다 — 렌더 스레드가 앞 패킷을 끝내기 전에는 새 값이 쓰이지 않는다. 첫 읽기와 읽지 못한 다시 읽기는 덮지 않으므로 울타리를 부르지 않는다.
 */
SW_TEST_CASE( ConfigManagerTest, InPlaceWriteWaitsForWriteFence )
{
    const string basePath    = test::makeScratchPath( "FenceEngineConfig.json" );
    const string qualityPath = test::makeScratchPath( "FenceRenderQuality.json" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( basePath, makeEngineConfigJSON( 800, 600 ) ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( qualityPath, R"({ "low": { "_shadowLocalCullSlotLimit": 4 }, "high": {} })" ) );

    ConfigManager manager;
    uint32        fenceCount   = 0;
    uint32        limitAtFence = 0;
    uint32        widthAtFence = 0;
    manager.setWriteFence( SW_DELEGATE_LAMBDA( ConfigWriteFenceDelegate, [&]()
    {
        ++fenceCount;
        const EngineConfig* pSeen = manager.getConfig<EngineConfig>();
        limitAtFence              = pSeen != nullptr ? pSeen->_render._shadowLocalCullSlotLimit : 0u;
        widthAtFence              = pSeen != nullptr ? pSeen->_window._width : 0u;
    } ) );

    // 1) 첫 읽기는 덮지 않는다.
    const EngineConfig* pConfig = manager.loadEngineConfig( ConfigSource::disk( basePath ).withRenderQuality( qualityPath, "high" ) );
    SW_REQUIRE_NOT_NULL( pConfig );
    SW_EXPECT_EQUAL( 0u, fenceCount );
    const uint32 baseLimit = pConfig->_render._shadowLocalCullSlotLimit;

    // 2) 단계 바꾸기 — 울타리 안에서는 옛 값, 지난 뒤에는 새 값.
    SW_EXPECT_TRUE( manager.applyRenderQuality( "low" ) );
    SW_EXPECT_EQUAL( 1u, fenceCount );
    SW_EXPECT_EQUAL( baseLimit, limitAtFence );
    SW_EXPECT_EQUAL( 4u, pConfig->_render._shadowLocalCullSlotLimit );

    // 3) 읽지 못하면 덮지 않으므로 기다리지도 않는다.
    SW_EXPECT_LOG( "Config.RenderQualityUnknownTier" );
    SW_EXPECT_FALSE( manager.applyRenderQuality( "nope" ) );
    SW_EXPECT_EQUAL( 1u, fenceCount );

    // 4) 파일 다시 읽기도 같은 울타리를 지난다.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( basePath, makeEngineConfigJSON( 1024, 600 ) ) );
    SW_EXPECT_TRUE( manager.reloadConfigFile( basePath ) == ReloadResult::Reloaded );
    SW_EXPECT_EQUAL( 2u, fenceCount );
    SW_EXPECT_EQUAL( 800u, widthAtFence );
    SW_EXPECT_EQUAL( 1024u, pConfig->_window._width );
    manager.setWriteFence( {} );
}
