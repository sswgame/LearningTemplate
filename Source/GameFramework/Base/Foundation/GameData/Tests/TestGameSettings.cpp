#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Foundation/GameData/GamePackDataUtil.h"
#include "GameFramework/Base/Foundation/GameData/GameSettings.h"

// GameSettingsTest — 공용 팩에서 읽는 필수 데이터(<RequiredData kind pack>)와 그 자리 규칙(nogpu).

/**
 * @brief [GameSettingsTest] <RequiredData> 는 종류마다 팩을 적고, 자리는 그 팩 안의 등록 줄 규칙이다(공용 팩의 아이템 · 외형)
 */
SW_TEST_CASE( GameSettingsTest, RequiredDataNamesThePackOfAKind )
{
    sw::GameSettings settings;
    SW_REQUIRE_TRUE( settings.loadFromXMLText( R"(<GameSettings>
  <startMap>game/test/maps/start.scene.xml</startMap>
  <RequiredData kind="items" pack="common"/>
  <RequiredData kind="appearance" pack="common"/>
</GameSettings>)",
                                               "GameSettingsTest" ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), settings._mapRequiredDataPack.size() );
    SW_EXPECT_EQUAL( sw::string( "common" ), settings._mapRequiredDataPack.at( "items" ) );

    sw::string path;
    sw::string error;
    SW_REQUIRE_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "common", "items", path, error ) );
    SW_EXPECT_EQUAL( sw::string( "common/data/items.xml" ), path );
    SW_REQUIRE_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "common", "appearance", path, error ) );
    SW_EXPECT_EQUAL( sw::string( "common/data/appearance" ), path );
}
