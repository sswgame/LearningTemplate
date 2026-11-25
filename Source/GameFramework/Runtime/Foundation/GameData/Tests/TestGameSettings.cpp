#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Content/Pack/PackConfig.h"
#include "Engine/Reflection/TypeInfo.h"

#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

// GameSettingsTest — 공용 팩에서 읽는 필수 데이터(<RequiredData kind pack>)와 그 자리 규칙, 엔진이 읽는 `<Engine>` 요소는 건너뛰기(nogpu).

/**
 * @brief [GameSettingsTest] <RequiredData> 는 종류마다 팩을 적고, 자리는 그 팩 안의 등록 줄 규칙이다(공용 팩의 아이템 · 외형)
 */
SW_TEST_CASE( GameSettingsTest, RequiredDataNamesThePackOfAKind )
{
    sw::GameSettings settings;
    SW_REQUIRE_TRUE( settings.loadFromXMLText( R"(<GameSettings>
  <startMap>game/test/maps/start.scene.xml</startMap>
  <defaultLanguage>ko_kr</defaultLanguage>
  <RequiredData kind="items" pack="common"/>
  <RequiredData kind="appearance" pack="common"/>
</GameSettings>)",
                                               "GameSettingsTest" ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), settings._mapRequiredDataPack.size() );
    SW_EXPECT_EQUAL( sw::string( "common" ), settings._mapRequiredDataPack.at( "items" ) );

    sw::string path;
    SW_REQUIRE_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "common", "items", path ) );
    SW_EXPECT_EQUAL( sw::string( "common/data/items.xml" ), path );
    SW_REQUIRE_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "common", "appearance", path ) );
    SW_EXPECT_EQUAL( sw::string( "common/data/appearance" ), path );
}

/**
 * @brief [GameSettingsTest] 같은 파일의 `<Engine>` 요소는 `PackEngineSettings` 의 것이라 `GameSettings` 는 건너뛰고 자기 칸만 읽는다
 */
SW_TEST_CASE( GameSettingsTest, SkipsTheEngineElement )
{
    const sw::string xml = "<GameSettings>"
                           "<Engine _windowTitle=\"Probe Game\" _bUIPauseMenu=\"true\" />"
                           "<startMap>game/probe/maps/start.scene.xml</startMap>"
                           "<defaultLanguage>ko_kr</defaultLanguage>"
                           "</GameSettings>";
    sw::GameSettings gameSettings;
    SW_REQUIRE_TRUE( gameSettings.loadFromXMLText( xml, "probe" ) );
    SW_EXPECT_EQUAL( sw::string( "game/probe/maps/start.scene.xml" ), gameSettings._startMap.get() );

    sw::PackEngineSettings engineSettings;
    SW_REQUIRE_TRUE( engineSettings.loadFromXMLText( xml, "probe" ) );
    SW_EXPECT_EQUAL( sw::string( "Probe Game" ), engineSettings._windowTitle );
}

/**
 * @brief [GameSettingsTest] 온라인 식별자(지역 · 상품 카탈로그 · 재화 둘 · 순위표 · 상위 줄 수)는 데이터다 — 적지 않으면 비어 있고(코드 기본값이 없다) 쓰는 쪽이 오류로 거절한다
 */
SW_TEST_CASE( GameSettingsTest, OnlineIdentifiersComeFromData )
{
    sw::GameSettings written;
    // 시작 맵 · 기본 언어는 필수 칸이라 두 글 모두 적는다 — 이 테스트는 온라인 칸만 본다.
    SW_REQUIRE_TRUE( written.loadFromXMLText( "<GameSettings>"
                                              "<startMap>game/probe/maps/start.scene.xml</startMap>"
                                              "<defaultLanguage>ko_kr</defaultLanguage>"
                                              "<onlineRegion>kr</onlineRegion>"
                                              "<onlineOfferCatalog>game/probe/data/probe.offers.xml</onlineOfferCatalog>"
                                              "<onlineSoftCurrency>cur.coin</onlineSoftCurrency>"
                                              "<onlineHardCurrency>cur.star</onlineHardCurrency>"
                                              "<onlineLeaderboard>probe_board</onlineLeaderboard>"
                                              "<onlineLeaderboardTopCount>7</onlineLeaderboardTopCount>"
                                              "</GameSettings>",
                                              "probe" ) );
    SW_EXPECT_EQUAL( sw::string( "kr" ), written._onlineRegion );
    SW_EXPECT_EQUAL( sw::string( "game/probe/data/probe.offers.xml" ), written._onlineOfferCatalog.get() );
    SW_EXPECT_EQUAL( sw::string( "cur.coin" ), written._onlineSoftCurrency );
    SW_EXPECT_EQUAL( sw::string( "cur.star" ), written._onlineHardCurrency );
    SW_EXPECT_EQUAL( sw::string( "probe_board" ), written._onlineLeaderboard );
    SW_EXPECT_EQUAL( 7, written._onlineLeaderboardTopCount );

    sw::GameSettings bare;
    SW_REQUIRE_TRUE( bare.loadFromXMLText( "<GameSettings>"
                                           "<startMap>game/probe/maps/start.scene.xml</startMap>"
                                           "<defaultLanguage>ko_kr</defaultLanguage>"
                                           "</GameSettings>",
                                           "probe" ) );
    SW_EXPECT_TRUE( bare._onlineRegion.empty() && bare._onlineSoftCurrency.empty() && bare._onlineHardCurrency.empty() && bare._onlineLeaderboard.empty() );
    SW_EXPECT_FALSE( bare._onlineOfferCatalog.isSet() );
    SW_EXPECT_EQUAL( 0, bare._onlineLeaderboardTopCount );
}

/**
 * @brief [GameSettingsTest] 팩 쿠킹 규칙의 필수 데이터 자리(`PackConfig::_cookRules._listRequiredData`)는 종류 등록 줄의 규칙(`tryMakeKindPath`)과 같다
 * @details 팩 쿠킹은 게임 모듈을 올리기 전에 돌아 등록 줄을 보지 못해 같은 자리를 데이터로 적는다. 둘이 어긋나면 배포 팩에 필수 데이터가 빠지거나 엉뚱한 것이 든다.
 */
SW_TEST_CASE( GameSettingsTest, CookRequiredDataPathsMatchTheKindRegistry )
{
    const sw::expected<sw::PackConfig> loadedConfig = sw::ConfigManager::readConfig( sw::PackConfig::kConfigFile );
    SW_REQUIRE_TRUE( loadedConfig.has_value() );
    const sw::PackConfig& config = *loadedConfig;
    SW_REQUIRE_TRUE( config._cookRules._listRequiredData.empty() == false );
    for ( const sw::PackRequiredDataPath& entry : config._cookRules._listRequiredData )
    {
        sw::string kindPath;
        SW_REQUIRE_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "common", entry._kind, kindPath ) );
        SW_EXPECT_EQUAL( sw::FileUtil::joinPath( "common", entry._path ), kindPath );
    }
}
