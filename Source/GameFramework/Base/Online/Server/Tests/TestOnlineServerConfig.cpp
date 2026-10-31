#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"

// OnlineServerConfigTest — 온라인 서버 설정의 키트 옵션 읽기(형식별 규칙 · 모르는 키 거절 · 형식 오류 · 없는 옵션은 기본값), 설정 파일 경로,
// 키트 팩토리 레지스트리(이름으로 찾기 · 내리기 · 만들기 함수 없는 팩토리 거절). 파일 · 네트워크 없음(nogpu).

namespace
{
    sw::OnlineServerKitEntry makeEntry( std::initializer_list<std::pair<const utf8*, const utf8*>> listOption )
    {
        sw::OnlineServerKitEntry entry;
        entry._name = "testkit";
        for ( const auto& [pKey, pValue] : listOption )
        {
            sw::OnlineServerOption& option = entry._listOption.emplace_back();
            option._key                    = pKey;
            option._value                  = pValue;
        }
        return entry;
    }

    sw::unique_ptr<sw::IOnlineServerKit> createNothing()
    {
        return nullptr;
    }
} // namespace

/**
 * @brief [OnlineServerConfigTest] 옵션 테이블로 정수 · 참거짓 · 글 · 글 목록을 읽는다 — 글은 첫 값, 목록은 같은 키의 값 전부(적은 순서), 없는 옵션은 기본값 그대로
 */
SW_TEST_CASE( OnlineServerConfigTest, ReadOptionsFillsEachFieldKind )
{
    const sw::OnlineServerKitEntry entry =
        makeEntry( {
            { "port",   "7777"},
            {"debug",   "true"},
            { "name",  "first"},
            { "name", "second"},
            { "mode",   "duel"},
            { "mode",  "squad"}
    } );

    int32                  port   = 0;
    bool                   bDebug = false;
    sw::string             name   = "default";
    sw::vector<sw::string> listMode;
    int32                  untouched = 42;
    sw::string             error;
    SW_REQUIRE_TRUE( entry.readOptions( {
                                            {  "port",      port},
                                            { "debug",    bDebug},
                                            {  "name",      name},
                                            {  "mode",  listMode},
                                            {"absent", untouched},
    },
                                        error ) );
    SW_EXPECT_TRUE( error.empty() );
    SW_EXPECT_EQUAL( 7777, port );
    SW_EXPECT_TRUE( bDebug );
    SW_EXPECT_EQUAL( sw::string( "first" ), name );
    SW_REQUIRE_EQUAL( size_t( 2 ), listMode.size() );
    SW_EXPECT_EQUAL( sw::string( "duel" ), listMode[0] );
    SW_EXPECT_EQUAL( sw::string( "squad" ), listMode[1] );
    SW_EXPECT_EQUAL( 42, untouched );
}

/**
 * @brief [OnlineServerConfigTest] 참거짓은 `0` · `false` 도 읽고 음수 정수도 읽는다. 옵션이 없는 키트는 빈 테이블로 통과한다
 */
SW_TEST_CASE( OnlineServerConfigTest, ReadOptionsAcceptsFalseNegativeAndEmptyTable )
{
    bool       bOn   = true;
    bool       bOff  = true;
    int32      delta = 0;
    sw::string error;
    SW_REQUIRE_TRUE( makeEntry( {
                                    {   "on",     "0"},
                                    {  "off", "false"},
                                    {"delta",   "-15"}
    } )
                         .readOptions( { { "on", bOn }, { "off", bOff }, { "delta", delta } }, error ) );
    SW_EXPECT_FALSE( bOn );
    SW_EXPECT_FALSE( bOff );
    SW_EXPECT_EQUAL( -15, delta );

    SW_EXPECT_TRUE( makeEntry( {} ).readOptions( {}, error ) );
}

/**
 * @brief [OnlineServerConfigTest] 테이블에 없는 키(철자 틀림)는 다른 필드를 하나도 읽기 전에 거절하고, 오류에 키와 키트 이름을 싣는다
 */
SW_TEST_CASE( OnlineServerConfigTest, ReadOptionsRejectsUnknownKeyBeforeReading )
{
    int32      port = 1;
    sw::string error;
    SW_EXPECT_FALSE( makeEntry( {
                                    {"port", "9000"},
                                    {"prot", "9001"}
    } )
                         .readOptions( { { "port", port } }, error ) );
    SW_EXPECT_EQUAL( 1, port );
    SW_EXPECT_TRUE( error.find( "'prot'" ) != sw::string::npos );
    SW_EXPECT_TRUE( error.find( "'testkit'" ) != sw::string::npos );
}

/**
 * @brief [OnlineServerConfigTest] 정수가 아닌 값 · 참거짓이 아닌 값은 false 와 오류이고, 필드는 바꾸지 않는다
 */
SW_TEST_CASE( OnlineServerConfigTest, ReadOptionsRejectsMalformedValues )
{
    int32      port = 5;
    sw::string error;
    SW_EXPECT_FALSE( makeEntry( {
                                    { "port", "77x" }
    } )
                         .readOptions( { { "port", port } }, error ) );
    SW_EXPECT_EQUAL( 5, port );
    SW_EXPECT_TRUE( error.find( "integer" ) != sw::string::npos );

    bool bFlag = true;
    error.clear();
    SW_EXPECT_FALSE( makeEntry( {
                                    { "flag", "maybe" }
    } )
                         .readOptions( { { "flag", bFlag } }, error ) );
    SW_EXPECT_TRUE( bFlag );
    SW_EXPECT_TRUE( error.find( "boolean" ) != sw::string::npos );

    int32 empty = 3;
    error.clear();
    SW_EXPECT_FALSE( makeEntry( {
                                    { "count", "" }
    } )
                         .readOptions( { { "count", empty } }, error ) );
    SW_EXPECT_EQUAL( 3, empty );
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
