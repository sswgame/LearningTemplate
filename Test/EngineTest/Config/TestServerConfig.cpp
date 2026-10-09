#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/ServerConfig.h"
#include "Engine/Config/ServerSecret.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [ServerConfigTest] 저장소의 서버 설정이 모두 읽히고, 저장소 · 캐시 항목 배열이 값 구조체 본문으로 읽힌다
 * @details 서버 설정은 Shipping 도 디스크에서 읽는다 — 틀린 키 · 모양은 배포 서버의 기동 실패다. 활성 게임만이 아니라 모든 게임의 파일을 본다.
 */
SW_TEST_CASE( ServerConfigTest, RepositoryServerConfigsLoad )
{
    const sw::string       serverDirectory = sw::FileUtil::joinPath( sw::ResourceUtil::getProjectFolderPath(), "Config/Server" );
    sw::vector<sw::string> listFile;
    SW_ASSERT_TRUE( sw::FileUtil::collectFiles( serverDirectory, ".json", listFile, false ) );
    SW_ASSERT_TRUE_MSG( listFile.empty() == false, serverDirectory.c_str() );
    for ( const sw::string& filePath : listFile )
    {
        sw::string text;
        SW_ASSERT_TRUE( sw::FileUtil::readTextFile( filePath, text ) );
        sw::ConfigManager manager;
        SW_EXPECT_TRUE_MSG( manager.loadConfigFromJson<sw::ServerConfig>( text, filePath.c_str() ), filePath.c_str() );
        const sw::ServerConfig* pConfig = manager.getConfig<sw::ServerConfig>();
        SW_ASSERT_NOT_NULL( pConfig );
        SW_EXPECT_TRUE_MSG( pConfig->_tickRateHz > 0 && pConfig->_gamePort > 0, filePath.c_str() );
    }

    // 항목 배열은 값 구조체 본문의 배열이다(래핑 없음).
    const sw::string  json = R"({ "_listStore": [ { "_name": "accounts", "_driver": "sqlite", "_connection": "Saved/accounts.db" } ],
                                  "_listCache": [ { "_name": "presence", "_driver": "resp", "_endpoint": "127.0.0.1:6379", "_secretEnvironment": "SW_CACHE_AUTH" } ] })";
    sw::ConfigManager manager;
    SW_ASSERT_TRUE( manager.loadConfigFromJson<sw::ServerConfig>( json, "inline" ) );
    const sw::ServerConfig* pConfig = manager.getConfig<sw::ServerConfig>();
    SW_ASSERT_NOT_NULL( pConfig );
    SW_ASSERT_EQUAL( size_t{ 1 }, pConfig->_listStore.size() );
    SW_EXPECT_STREQ( "sqlite", pConfig->_listStore[0]._driver.c_str() );
    SW_ASSERT_EQUAL( size_t{ 1 }, pConfig->_listCache.size() );
    SW_EXPECT_STREQ( "SW_CACHE_AUTH", pConfig->_listCache[0]._secretEnvironment.c_str() );
}

/**
 * @brief [ServerConfigTest] 비밀은 환경 변수에서만 — 이름이 비면 비밀 없음, 이름이 있는데 변수가 없으면 실패(값은 로그에 없다)
 */
SW_TEST_CASE( ServerConfigTest, SecretComesFromTheEnvironmentOnly )
{
    sw::string secret;
    SW_EXPECT_TRUE( sw::ServerSecret::read( "", secret ) );
    SW_EXPECT_TRUE( secret.empty() );
    {
        SW_TEST_DEFENSIVE_SCOPE( "the secret variable is deliberately missing" );
        SW_EXPECT_FALSE( sw::ServerSecret::read( "SW_TEST_SECRET_THAT_IS_NOT_SET_7F3A", secret ) );
    }
}
