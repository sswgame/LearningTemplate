// GameSettings — 팩 gamesettings.xml 에서 게임 쪽 칸을 읽고, 엔진이 읽는 `<Engine>` 요소는 모르는 원소로 다루지 않고 건너뛴다(요소마다 읽는 쪽이 하나, nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Config/PackEngineSettings.h"

#include "GameFramework/Base/Foundation/GameData/GameSettings.h"

/**
 * @brief [GameSettingsTest] 같은 파일의 `<Engine>` 요소는 `PackEngineSettings` 의 것이라 `GameSettings` 는 건너뛰고 자기 칸만 읽는다
 */
SW_TEST_CASE( GameSettingsTest, SkipsTheEngineElement )
{
    const sw::string xml = "<GameSettings>"
                           "<Engine _windowTitle=\"Probe Game\" _bUIPauseMenu=\"true\" />"
                           "<startMap>game/probe/maps/start.scene.xml</startMap>"
                           "</GameSettings>";
    sw::GameSettings gameSettings;
    SW_REQUIRE_TRUE( gameSettings.loadFromXMLText( xml, "probe" ) );
    SW_EXPECT_EQUAL( sw::string( "game/probe/maps/start.scene.xml" ), gameSettings._startMap.get() );

    sw::PackEngineSettings engineSettings;
    SW_REQUIRE_TRUE( engineSettings.loadFromXMLText( xml, "probe" ) );
    SW_EXPECT_EQUAL( sw::string( "Probe Game" ), engineSettings._windowTitle );
}
