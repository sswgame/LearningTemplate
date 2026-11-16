#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/ServerConfig.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerRuntime.h"
#include "GameFramework/Runtime/Online/Server/Tests/OnlineConfigTestTypes.h"

// OnlineServerConfigTest — 온라인 서버 설정의 키트 설정 읽기(`_settings` 객체 · 모르는 키 거절 · 형식 오류 · 없는 칸은 초기값), 설정 파일 경로,
// 키트 팩토리 레지스트리(이름으로 찾기 · 내리기 · 만들기 함수 없는 팩토리 거절). 파일 · 네트워크 없음(nogpu).

namespace
{
    sw::OnlineServerKitEntry makeEntry( const utf8* pSettingsJSON )
    {
        sw::OnlineServerKitEntry entry;
        entry._name     = "testkit";
        entry._settings = pSettingsJSON;
        return entry;
    }

    sw::unique_ptr<sw::IOnlineServerKit> createNothing()
    {
        return nullptr;
    }
} // namespace

/**
 * @brief [OnlineServerConfigTest] 온라인 설정의 `_settings` 객체가 키트 설정 구조체로 읽힌다 — 글 · 정수 · 참거짓 · 글 목록, 적지 않은 칸은 초기값
 */
SW_TEST_CASE( OnlineServerConfigTest, SettingsObjectFillsTheKitConfig )
{
    sw::ConfigManager manager;
    SW_REQUIRE_TRUE( manager.loadConfigFromJSON<sw::OnlineServerConfig>( R"({ "_ttlSweepMinuteOfDay": 300, "_ttlBackfillRolloutHours": 24,
"_auditRetention": { "_expireAfterDays": 365, "_maxScanPerSweep": 20000, "_maxDeletePerSweep": 5000 },
"_ledgerRetention": { "_expireAfterDays": 0, "_maxScanPerSweep": 1, "_maxDeletePerSweep": 1 },
"_closedReportTTL": { "_expireAfterDays": 0, "_maxScanPerSweep": 1, "_maxDeletePerSweep": 1 },
"_mailSentTTL": { "_expireAfterDays": 0, "_maxScanPerSweep": 1, "_maxDeletePerSweep": 1 },
"_scheduleRunTTL": { "_expireAfterDays": 0, "_maxScanPerSweep": 1, "_maxDeletePerSweep": 1 },
"_listKit": [ { "_name": "testkit", "_settings": { "_port": 7777, "_bDebug": true, "_listMode": [ "duel", "squad" ] } } ] })",
                                                                         "inline" ) );
    const sw::OnlineServerConfig* pConfig = manager.getConfig<sw::OnlineServerConfig>();
    SW_REQUIRE_EQUAL( size_t( 1 ), pConfig->_listKit.size() );
    sw::OnlineConfigTestKitConfig kitConfig;
    {
        const auto result = pConfig->_listKit[0].readSettings( kitConfig );
        SW_REQUIRE_TRUE_MSG( result.has_value(), result.has_value() ? std::string_view{} : result.error().message() );
    }
    SW_EXPECT_EQUAL( 7777, kitConfig._port );
    SW_EXPECT_TRUE( kitConfig._bDebug );
    SW_REQUIRE_EQUAL( size_t( 2 ), kitConfig._listMode.size() );
    SW_EXPECT_EQUAL( sw::string( "squad" ), kitConfig._listMode[1] );
    SW_EXPECT_EQUAL( 42, kitConfig._untouched );
    SW_EXPECT_EQUAL( 365, pConfig->_auditRetention._expireAfterDays ); // 기반 표 TTL 은 구조체 칸으로 읽는다
    SW_EXPECT_EQUAL( 5000, pConfig->_auditRetention._maxDeletePerSweep );
}

/**
 * @brief [OnlineServerConfigTest] 설정을 적지 않은 키트는 빈 객체로 읽는다 — 초기값 그대로이고, 필수 칸이 있으면 빠졌다고 알린다
 */
SW_TEST_CASE( OnlineServerConfigTest, MissingSettingsReadAsEmptyObject )
{
    sw::OnlineConfigTestKitConfig kitConfig;
    SW_EXPECT_TRUE( makeEntry( "" ).readSettings( kitConfig ) );
    SW_EXPECT_EQUAL( 1, kitConfig._port );

    SW_EXPECT_LOG( "Config.KeyMissing" );
    sw::OnlineConfigTestRequiredConfig required;
    const sw::expected<void>           result = makeEntry( "" ).readSettings( required );
    SW_REQUIRE_FALSE( result.has_value() );
    SW_EXPECT_TRUE( result.error().detail() == "testkit" );
}

/**
 * @brief [OnlineServerConfigTest] 모르는 키(철자 틀림)와 형식이 틀린 값은 오류이고 오류 글에 키트 이름을 싣는다
 */
SW_TEST_CASE( OnlineServerConfigTest, SettingsRejectUnknownKeyAndMalformedValue )
{
    SW_EXPECT_LOG( "Config.KeyUnknown" );
    sw::OnlineConfigTestKitConfig kitConfig;
    const sw::expected<void>      unknownKey = makeEntry( R"({ "_prot": 9001 })" ).readSettings( kitConfig );
    SW_REQUIRE_FALSE( unknownKey.has_value() );
    SW_EXPECT_TRUE( unknownKey.error().detail() == "testkit" );
    SW_EXPECT_FALSE( makeEntry( R"({ "_port": "77x" })" ).readSettings( kitConfig ) );
}

/**
 * @brief [OnlineServerConfigTest] 파일 경로는 빈 글이면 "정하지 않음"이고, 상대 경로는 프로젝트 루트 아래로 푼다
 */
SW_TEST_CASE( OnlineServerConfigTest, ResolveFileHandlesEmptyAndRelativePaths )
{
    const sw::OnlineServerKitEntry                     entry = makeEntry( "" );
    const sw::expected<sw::optional<sw::AbsolutePath>> empty = entry.resolveFile( "", "_file" );
    SW_REQUIRE_TRUE( empty.has_value() );
    SW_EXPECT_FALSE( empty->has_value() );
    const sw::expected<sw::optional<sw::AbsolutePath>> relative = entry.resolveFile( "Config/Server/a.txt", "_file" );
    SW_REQUIRE_TRUE( relative.has_value() );
    SW_REQUIRE_TRUE( relative->has_value() );
    SW_EXPECT_TRUE( ( *relative )->str().find( "a.txt" ) != sw::string::npos );
}

/**
 * @brief [OnlineServerConfigTest] 온라인 설정 경로는 서버 설정 경로의 `.json` 을 `.online.json` 으로 바꾼 것이다(확장자가 없으면 붙인다)
 */
SW_TEST_CASE( OnlineServerConfigTest, ConfigPathReplacesJSONExtension )
{
    SW_EXPECT_EQUAL( sw::string( "Config/Server/Empty.online.json" ), sw::OnlineServerConfig::makeConfigPath( "Config/Server/Empty.json" ) );
    SW_EXPECT_EQUAL( sw::string( "Config/Server/Empty.online.json" ), sw::OnlineServerConfig::makeConfigPath( "Config/Server/Empty" ) );
    SW_EXPECT_EQUAL( sw::string( "a.b.online.json" ), sw::OnlineServerConfig::makeConfigPath( "a.b.json" ) );
}

/**
 * @brief [OnlineServerConfigTest] 키트 레지스트리: 올린 팩토리를 이름으로 찾고 목록에 담으며, 내리면 사라진다. 만들기 함수가 없는 팩토리는 올리지 않는다
 */
SW_TEST_CASE( OnlineServerConfigTest, KitRegistryFindsAndForgetsFactories )
{
    sw::OnlineServerKitFactory factory;
    factory._pName   = "__test_online_kit";
    factory._pCreate = &createNothing;
    sw::OnlineServerKitFactory noCreate;
    noCreate._pName = "__test_online_kit_nocreate";

    SW_EXPECT_NULL( sw::OnlineServerKitRegistry::findFactory( "__test_online_kit" ) );
    sw::OnlineServerKitRegistry::registerFactory( &factory );
    sw::OnlineServerKitRegistry::registerFactory( &noCreate );
    sw::OnlineServerKitRegistry::registerFactory( nullptr );
    SW_EXPECT_TRUE( sw::OnlineServerKitRegistry::findFactory( "__test_online_kit" ) == &factory );
    SW_EXPECT_NULL( sw::OnlineServerKitRegistry::findFactory( "__test_online_kit_nocreate" ) );
    SW_EXPECT_NULL( sw::OnlineServerKitRegistry::findFactory( "__test_online" ) ); // 앞부분만 같은 이름은 아니다

    sw::vector<sw::OnlineServerKitFactory*> listFactory;
    sw::OnlineServerKitRegistry::collectFactories( listFactory );
    int32 foundCount = 0;
    for ( const sw::OnlineServerKitFactory* pFactory : listFactory )
    {
        foundCount += pFactory == &factory ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 1, foundCount );

    sw::OnlineServerKitRegistry::unregisterFactory( &factory );
    sw::OnlineServerKitRegistry::unregisterFactory( &noCreate ); // 올리지 않은 것을 내려도 된다
    SW_EXPECT_NULL( sw::OnlineServerKitRegistry::findFactory( "__test_online_kit" ) );
}

/**
 * @brief [OnlineServerConfigTest] 같은 이름의 다른 팩토리는 거절 로그와 함께 올리지 않는다 — 먼저 올린 것이 남는다
 */
SW_TEST_CASE( OnlineServerConfigTest, KitRegistryRejectsDuplicateName )
{
    SW_EXPECT_LOG( "Registration.DuplicateName" );
    sw::OnlineServerKitFactory first;
    first._pName   = "__test_online_kit_dup";
    first._pCreate = &createNothing;
    sw::OnlineServerKitFactory second;
    second._pName   = "__test_online_kit_dup";
    second._pCreate = &createNothing;

    sw::OnlineServerKitRegistry::registerFactory( &first );
    sw::OnlineServerKitRegistry::registerFactory( &first ); // 같은 항목을 다시 올리는 것은 거절이 아니다
    sw::OnlineServerKitRegistry::registerFactory( &second );
    SW_EXPECT_TRUE( sw::OnlineServerKitRegistry::findFactory( "__test_online_kit_dup" ) == &first );
    sw::OnlineServerKitRegistry::unregisterFactory( &second );
    SW_EXPECT_TRUE( sw::OnlineServerKitRegistry::findFactory( "__test_online_kit_dup" ) == &first ); // 올라 있지 않은 것을 내려도 남은 것은 그대로
    sw::OnlineServerKitRegistry::unregisterFactory( &first );
    SW_EXPECT_NULL( sw::OnlineServerKitRegistry::findFactory( "__test_online_kit_dup" ) );
}

/**
 * @brief [OnlineServerConfigTest] 저장소의 온라인 서버 설정 쌍(`Config/Server/Online`)이 모두 엄격하게 읽히고 저장소 · 캐시 항목이 이름으로 풀린다
 * @details 온라인 설정은 키트 설정까지 기동에서 읽으므로, 파일 모양이 틀리면 전용 서버가 뜨지 않는다. 활성 게임만이 아니라 모든 파일을 본다.
 */
SW_TEST_CASE( OnlineServerConfigTest, RepositoryOnlineConfigsResolve )
{
    const sw::string       directory = sw::FileUtil::joinPath( sw::ProcessRoots::get()._project.str(), "Config/Server/Online" );
    sw::vector<sw::string> listFile;
    SW_REQUIRE_TRUE( sw::FileUtil::collectFiles( directory, ".json", listFile, false ) );
    int32 pairCount = 0;
    for ( const sw::string& filePath : listFile )
    {
        if ( filePath.find( ".online.json" ) != sw::string::npos )
            continue;
        sw::string serverText;
        sw::string onlineText;
        SW_REQUIRE_TRUE( sw::FileUtil::readTextFile( filePath, serverText ) );
        SW_REQUIRE_TRUE_MSG( sw::FileUtil::readTextFile( sw::OnlineServerConfig::makeConfigPath( filePath ), onlineText ), filePath.c_str() );
        sw::ConfigManager manager;
        SW_REQUIRE_TRUE_MSG( manager.loadConfigFromJSON<sw::ServerConfig>( serverText, filePath.c_str() ), filePath.c_str() );
        SW_REQUIRE_TRUE_MSG( manager.loadConfigFromJSON<sw::OnlineServerConfig>( onlineText, filePath.c_str() ), filePath.c_str() );
        // 비밀값은 환경 변수로만 온다 — 값이 아니라 설정의 짝이 맞는지 보는 테스트라 빈칸을 가짜 값으로 채운다.
        const sw::ServerConfig& serverConfig = *manager.getConfig<sw::ServerConfig>();
        sw::vector<sw::string>  listSecretName;
        const auto              addSecret = [&listSecretName]( const sw::string& name )
        {
            if ( name.empty() == false && sw::ProcessEnvironment::isSet( name ) == false && sw::ProcessEnvironment::trySet( name, "test-secret" ) )
                listSecretName.push_back( name );
        };
        for ( const sw::ServerStoreEntry& store : serverConfig._listStore )
        {
            addSecret( store._secretEnvironment );
        }
        for ( const sw::ServerCacheEntry& cache : serverConfig._listCache )
        {
            addSecret( cache._secretEnvironment );
        }
        sw::OnlineServerRuntimeConfig settings;
        {
            const auto result = sw::OnlineServerRuntime::resolveStores( *manager.getConfig<sw::ServerConfig>(), *manager.getConfig<sw::OnlineServerConfig>(), settings );
            SW_EXPECT_TRUE_MSG( result.has_value(), result.has_value() ? std::string_view{} : result.error().message() );
        }
        for ( const sw::string& name : listSecretName )
        {
            (void)sw::ProcessEnvironment::tryUnset( name );
        }
        ++pairCount;
    }
    SW_EXPECT_TRUE( pairCount > 0 );
}
