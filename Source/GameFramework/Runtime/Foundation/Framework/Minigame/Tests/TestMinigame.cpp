// MinigameTest — 미니게임 데이터 읽기 · 참조 검사(모르는 게임 기능 · 겹친 이름 · 빠진 맵, 모르는 속성 · 원소)(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"

#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameDefinition.h"

using namespace sw;

namespace
{
    bool activateTestFeature( GameFeatureScope& scope )
    {
        (void)scope;
        return true;
    }

    constexpr string_view kValidXML = R"(<Minigame name="arcade" map="testproject/minigame/arcade.scene.xml" entry="SceneSwitch" features="TestFeature"
        inputContexts="arcade,menu" ui="testproject/minigame/hud.ui.xml" catalogs="abilities"/>)";
} // namespace

/** @brief [MinigameTest] 루트 속성을 읽는다 — 목록은 쉼표로 가른다 */
SW_TEST_CASE( MinigameTest, ReadsDefinition )
{
    MinigameDefinition definition;
    SW_REQUIRE_TRUE( MinigameDefinition::loadFromXMLText( kValidXML, "arcade.minigame.xml", definition ) );
    SW_EXPECT_EQUAL( string( "arcade" ), string( definition._name.view() ) );
    SW_EXPECT_EQUAL( string( "testproject/minigame/arcade.scene.xml" ), definition._map.get() );
    SW_EXPECT_TRUE( definition._entry == MinigameEntry::SceneSwitch );
    SW_REQUIRE_TRUE( definition._listFeature.size() == 1 );
    SW_EXPECT_TRUE( definition._listFeature[0] == hashed_string( "TestFeature" ) );
    SW_REQUIRE_TRUE( definition._listInputContext.size() == 2 );
    SW_EXPECT_TRUE( definition._listInputContext[1] == hashed_string( "menu" ) );
    SW_REQUIRE_TRUE( definition._listUI.size() == 1 );
    SW_REQUIRE_TRUE( definition._listCatalog.size() == 1 );
}

/** @brief [MinigameTest] 필수 칸(map)이 빠진 파일은 읽지 않는다 */
SW_TEST_CASE( MinigameTest, MissingMapIsReadError )
{
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    MinigameDefinition definition;
    SW_EXPECT_FALSE( MinigameDefinition::loadFromXMLText( R"(<Minigame name="x" entry="SceneSwitch"/>)", "x.minigame.xml", definition ) );
}

/** @brief [MinigameTest] 모르는 속성(옛 이름 kits 포함) · 원소(지운 `<GameMode>` 포함)는 읽기 오류다(오타 · 옛 데이터가 조용히 빠지지 않는다) */
SW_TEST_CASE( MinigameTest, UnknownNamesAreReadErrors )
{
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    MinigameDefinition definition;
    SW_EXPECT_FALSE( MinigameDefinition::loadFromXMLText( R"(<Minigame name="x" map="a/b.scene.xml" entry="SceneSwitch" kits="Typo"/>)", "x.minigame.xml", definition ) );
    SW_EXPECT_FALSE( MinigameDefinition::loadFromXMLText( R"(<Minigame name="x" map="a/b.scene.xml" entry="SceneSwitch"><GameMode type="A"/></Minigame>)", "x.minigame.xml", definition ) );
}

/** @brief [MinigameTest] 검사: 등록된 기능 · 있는 맵 · 있는 UI 면 통과하고, 모르는 기능 · 겹친 기능 · 빠진 맵은 진다 */
SW_TEST_CASE( MinigameTest, ValidatesFeaturesAndMap )
{
    const test::ScopedScratchDomain domain;
    for ( const utf8* pFile : { "minigame/arcade.scene.xml", "minigame/hud.ui.xml" } )
    {
        const string path = domain.makeDiskPath( pFile );
        SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( path ) ) && FileUtil::writeTextFile( path, "<Scene/>" ) );
    }
    GameFeatureRegistry                  features;
    static const GameFeatureRegistration s_testFeature{ "TestFeature", &activateTestFeature };
    SW_REQUIRE_TRUE( features.registerEntry( &s_testFeature ) );

    MinigameDefinition definition;
    SW_REQUIRE_TRUE( MinigameDefinition::loadFromXMLText( kValidXML, "arcade.minigame.xml", definition ) );
    SW_EXPECT_TRUE( definition.validate( features, "arcade.minigame.xml" ) );

    SW_EXPECT_LOG( "MinigameDefinition.UnknownFeature" );
    SW_EXPECT_LOG( "MinigameDefinition.DuplicateName" );
    SW_EXPECT_LOG( "MinigameDefinition.MapMissing" );
    MinigameDefinition broken;
    SW_REQUIRE_TRUE( MinigameDefinition::loadFromXMLText( R"(<Minigame name="x" map="testproject/minigame/none.scene.xml" entry="SceneSwitch" features="TestFeature,testfeature,NoSuchFeature"/>)",
                                                          "x.minigame.xml", broken ) );
    SW_EXPECT_FALSE( broken.validate( features, "x.minigame.xml" ) );
    features.unregisterEntry( &s_testFeature );
}
