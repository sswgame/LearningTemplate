// ScavengerGameModeTest — 협동 수집 공포 디렉터: 명령은 판이 열린 동안만 받고, 봇 승무원 혼자 터미널에서 자물쇠 따개를 사고 잠긴 문을 열어 고철을 싣고 팔아 할당량을 채우며, 상태는 왕복한다(nogpu).
#include "pch.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다

#include "Core/File/FileUtil.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Component/ScavengerGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Presentation/Tests/RecordingSoundPlayer.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

using namespace sw;

namespace
{
    // 방으로 들어가는 문은 모두 잠겨 있고(lockedChance 1) 고철 셋은 값이 10 이다.
    constexpr const utf8* kScavengerGameModeXML = R"(<ScavengerCatalog terminalShop="terminal" currency="Credits" crewHealth="100">
  <Quota start="30" days="1" increase="50" steepness="16" randomness="0" overtime="5" credits="60"/>
  <BuyRate daysLeft="0" rate="1"/>
  <Day secondsPerDay="4800" arrival="6" dusk="18" depart="24"/>
  <Carry slots="2" speedPerWeight="0.01" minSpeed="0.4" bodyWeight="60"/>
  <Facility rooms="4" roomsMax="4" lockedChance="1" loopChance="0" fireExits="0"/>
  <Scrap id="bolt" min="10" max="10" weight="5"/>
  <Moon id="rust" name="Rust" cost="0" scrap="3" scrapMax="3"/>
  <Moon id="company" name="Company" company="true"/>
</ScavengerCatalog>)";

    constexpr const utf8* kScavengerGameModeThreats = R"(<SpawnTable budgetPerMinute="0" maxBudget="1" startBudget="0" refund="false"><Entry id="lurker" cost="1" weight="1" tags="Indoor"/></SpawnTable>)";
    constexpr const utf8* kScavengerGameModeItems   = R"(<ItemCatalog><Item id="lockpick" category="Tool"/><Item id="boots" category="Tool"/></ItemCatalog>)";
    constexpr const utf8* kScavengerGameModeShops   = R"(<ShopCatalog><Shop id="terminal" currency="Credits" buybackLimit="0"><Stock item="lockpick" price="15" count="1"/><Stock item="boots" price="100"/></Shop></ShopCatalog>)";
    constexpr const utf8* kScavengerGameModeWeather = R"(<WeatherCatalog transition="1"><Weather id="clear" minDuration="86400" maxDuration="86400"><Values scrapValue="1" threat="1"/></Weather>
  <Weather id="eclipse" seasons="rust:100000000" minDuration="86400" maxDuration="86400"><Values scrapValue="2" threat="3"/></Weather></WeatherCatalog>)";
    constexpr float32     kScavengerGameModeStep    = 1.0f / 30.0f;

    /** @brief 카탈로그를 건 게임 범위와 위협 표를 둔 임시 도메인입니다. 테스트 본문이 `ScopedGameScope` 로 겁니다. */
    struct ScavengerGameModeBench
    {
        test::ScopedScratchDomain _domain{};
        ScavengerCatalog          _catalog{};
        ItemCatalog               _items{};
        RecordingSoundPlayer      _soundPlayer{};
        SubsystemScope            _scope{};

        [[nodiscard]] bool setUp()
        {
            (void)_domain.makeDiskFolder( "scavenger/data" ); // 폴더만 만든다
            return FileUtil::writeTextFile( _domain.makeDiskPath( "scavenger/data/threats.spawns.xml" ), kScavengerGameModeThreats ) &&
                   FileUtil::writeTextFile( _domain.makeDiskPath( "scavenger/data/terminal.shops.xml" ), kScavengerGameModeShops ) &&
                   FileUtil::writeTextFile( _domain.makeDiskPath( "scavenger/data/moon.weather.xml" ), kScavengerGameModeWeather ) &&
                   _items.loadFromXMLText( kScavengerGameModeItems, "ScavengerGameModeTest" ) && _scope.bind<ISoundPlayer>( &_soundPlayer ) && _scope.bind<ItemCatalog>( &_items ) &&
                   _catalog.loadFromXMLText( kScavengerGameModeXML, "ScavengerGameModeTest" ) && _scope.bind<ScavengerCatalog>( &_catalog );
        }

        static bool setProperty( ScavengerGameModeComponent& director, const utf8* pName, const string& text )
        {
            const PropertyInfo* const pProperty = ScavengerGameModeComponent::StaticType()->findPropertyInHierarchy( hashed_string( pName ) );
            return pProperty != nullptr && SerializerUtil::applyPropertyText( *pProperty, MutableObjectAddress{ &director }, text, SerializeContext::getDefault( SerializePurpose::Authoring ) );
        }

        static ScavengerGameModeComponent* addDirector( GameObjectManager& manager, const utf8* pCompanyMoon, bool bAutoPlay )
        {
            GameObject* const           pObject   = manager.createGameObject( hashed_string( "Director" ) );
            const bool                  bState    = pObject->addComponent<GameStateComponent>() != nullptr; // 시계 · 날씨는 디렉터 앞에 붙은 월드 상태의 것이다
            ScavengerGameModeComponent* pDirector = pObject->addComponent<ScavengerGameModeComponent>();
            const bool                  bSet      = bState && pDirector != nullptr && setProperty( *pDirector, "_companyMoon", pCompanyMoon ) &&
                              setProperty( *pDirector, "_threatTable", test::ScopedScratchDomain::makeID( "scavenger/data/threats.spawns.xml" ).str() ) && setProperty( *pDirector, "_crewCount", "1" ) &&
                              setProperty( *pDirector, "_weatherCatalog", test::ScopedScratchDomain::makeID( "scavenger/data/moon.weather.xml" ).str() ) &&
                              setProperty( *pDirector, "_shopCatalog", test::ScopedScratchDomain::makeID( "scavenger/data/terminal.shops.xml" ).str() ) && setProperty( *pDirector, "_unlockItem", "lockpick" ) &&
                              setProperty( *pDirector, "_speedItem", "boots" ) && setProperty( *pDirector, "_speedItemStepScale", "0.5" ) &&
                              setProperty( *pDirector, "_stepSeconds", "0.1" ) && setProperty( *pDirector, "_resultHoldSeconds", "1000" ) && setProperty( *pDirector, "_bAutoPlay", bAutoPlay ? "true" : "false" );
            return bSet ? pDirector : nullptr; // 결과 유지 시간이 길어 미니게임 흐름이 없는 테스트가 끝내기를 부르지 않는다
        }
    };
} // namespace

/** @brief [ScavengerGameModeTest] 판이 열리기 전의 명령은 거절한다 */
SW_TEST_CASE( ScavengerGameModeTest, CommandsBeforeTheExpeditionOpensAreRefused )
{
    GameObjectManager           manager;
    ScavengerGameModeComponent* pDirector = manager.createGameObject( hashed_string( "Director" ) )->addComponent<ScavengerGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pDirector );
    SW_EXPECT_FALSE( pDirector->isAcceptingInput() );
    SW_EXPECT_FALSE( pDirector->submitCrewCommand( 0, ScavengerCommand{} ) );
}

/** @brief [ScavengerGameModeTest] 회사 위성이 아닌 위성을 회사로 적으면 오류를 남기고 판을 열지 않는다 */
SW_TEST_CASE( ScavengerGameModeTest, CompanyMustBeACompanyMoon )
{
    SW_EXPECT_LOG( "Scavenger.NoCompanyMoon" );
    ScavengerGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope       scopedScope( bench._scope );
    GameObjectManager           manager;
    ScavengerGameModeComponent* pDirector = ScavengerGameModeBench::addDirector( manager, "rust", false );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_EXPECT_FALSE( pDirector->isStarted() );
    manager.endPlay();
}

/** @brief [ScavengerGameModeTest] 레버로 내리고, 이웃 칸으로만 걸으며, 지도에 없는 쪽의 이웃은 없다 */
SW_TEST_CASE( ScavengerGameModeTest, CrewLandsAndWalksToNeighbours )
{
    ScavengerGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope       scopedScope( bench._scope );
    GameObjectManager           manager;
    ScavengerGameModeComponent* pDirector = ScavengerGameModeBench::addDirector( manager, "company", false );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    ScavengerCommand lever;
    lever._bLeverTriggered = SW_TRUE;
    SW_EXPECT_TRUE( pDirector->submitCrewCommand( 0, lever ) );
    SW_EXPECT_FALSE( pDirector->submitCrewCommand( 1, lever ) ); // 승무원은 하나다
    manager.update( kScavengerGameModeStep );
    SW_EXPECT_EQUAL( static_cast<int32>( ScavengerPhase::Landed ), pDirector->getPhase() );
    SW_EXPECT_EQUAL( 3, pDirector->getGroundScrapCount() );
    SW_EXPECT_TRUE( pDirector->isPlayerInShip() );
    SW_EXPECT_TRUE( pDirector->findNeighborToward( 0, float2{ -1.0f, 0.0f } ).empty() );
    ScavengerCommand walk;
    walk._moveTo = pDirector->findNeighborToward( 0, float2{ 1.0f, 0.0f } );
    SW_EXPECT_TRUE( walk._moveTo == hashed_string( ScavengerExpedition::kOutsideAreaID ) );
    SW_EXPECT_TRUE( pDirector->submitCrewCommand( 0, walk ) );
    manager.update( kScavengerGameModeStep );
    SW_EXPECT_FALSE( pDirector->isPlayerInShip() );
    manager.endPlay();
}

/** @brief [ScavengerGameModeTest] 자동 플레이 봇 혼자 자물쇠 따개를 사고 잠긴 문을 열어 고철을 모두 싣고 회사에 팔아 할당량을 채우면 이기고, 번 돈이 결과로 나간다 */
SW_TEST_CASE( ScavengerGameModeTest, BotFillsTheQuota )
{
    ScavengerGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope       scopedScope( bench._scope );
    GameObjectManager           manager;
    ScavengerGameModeComponent* pDirector = ScavengerGameModeBench::addDirector( manager, "company", true );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    constexpr int32 kMaxSteps = 3000;
    for ( int32 step = 0; step < kMaxSteps && pDirector->getResult() == 0; ++step )
    {
        manager.update( kScavengerGameModeStep );
    }
    SW_EXPECT_EQUAL( static_cast<int32>( ScavengerResult::Win ), pDirector->getResult() );
    vector<MinigameResultValue> listValue;
    pDirector->collectMinigameResult( listValue );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listValue.size() );
    SW_EXPECT_EQUAL( int64{ 51 }, listValue[0]._amount ); // 날씨로 두 배가 된 볼트 셋(60) + 넘긴 30 의 보너스(6) - 자물쇠 따개(15)
    SW_EXPECT_TRUE( pDirector->canUnlockDoor() );
    manager.endPlay();
}

/** @brief [ScavengerGameModeTest] 착륙해 짐을 나르는 판의 상태를 다른 디렉터가 받으면 같은 바이트를 내고 이어서 이긴다 */
SW_TEST_CASE( ScavengerGameModeTest, StateRoundTripsMidExpedition )
{
    ScavengerGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope       scopedScope( bench._scope );
    GameObjectManager           manager;
    ScavengerGameModeComponent* pDirector = ScavengerGameModeBench::addDirector( manager, "company", true );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    constexpr int32 kMidSteps = 40;
    for ( int32 step = 0; step < kMidSteps; ++step )
    {
        manager.update( kScavengerGameModeStep );
    }
    Archive written;
    pDirector->writeState( written );

    GameObjectManager           restoredManager;
    ScavengerGameModeComponent* pRestored = ScavengerGameModeBench::addDirector( restoredManager, "company", true );
    SW_REQUIRE_NOT_NULL( pRestored );
    restoredManager.beginPlay();
    pRestored->restoreState( vector<uint8>( written.getData(), written.getData() + written.getSize() ) );
    SW_EXPECT_EQUAL( static_cast<int32>( ScavengerPhase::Landed ), pRestored->getPhase() );
    SW_EXPECT_TRUE( StateBytesTestUtil::hasSameState( *pDirector, *pRestored ) );
    constexpr int32 kMaxSteps = 3000;
    for ( int32 step = 0; step < kMaxSteps && pRestored->getResult() == 0; ++step )
    {
        restoredManager.update( kScavengerGameModeStep );
    }
    SW_EXPECT_EQUAL( static_cast<int32>( ScavengerResult::Win ), pRestored->getResult() );
    restoredManager.endPlay();
    manager.endPlay();
}

/** @brief [ScavengerGameModeTest] 궤도의 터미널은 크레딧으로 팔고, 모자라거나 재고가 없으면 거절하며, 자물쇠 따개가 없으면 잠긴 문이 열리지 않는다 */
SW_TEST_CASE( ScavengerGameModeTest, TerminalSellsInOrbit )
{
    ScavengerGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope       scopedScope( bench._scope );
    GameObjectManager           manager;
    ScavengerGameModeComponent* pDirector = ScavengerGameModeBench::addDirector( manager, "company", false );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    SW_EXPECT_FALSE( pDirector->canUnlockDoor() );
    ScavengerCommand buy;
    buy._buyItemID = hashed_string( "boots" ); // 100 — 시작 크레딧 60 으로 모자란다
    SW_EXPECT_TRUE( pDirector->submitCrewCommand( 0, buy ) );
    manager.update( kScavengerGameModeStep );
    SW_EXPECT_EQUAL( 60, pDirector->getCredits() );
    for ( int32 press = 0; press < 2; ++press )
    {
        buy._buyItemID = hashed_string( "lockpick" ); // 하나뿐이라 두 번째는 거절한다
        SW_EXPECT_TRUE( pDirector->submitCrewCommand( 0, buy ) );
        manager.update( kScavengerGameModeStep );
        SW_EXPECT_EQUAL( 45, pDirector->getCredits() );
    }
    SW_EXPECT_TRUE( pDirector->canUnlockDoor() );
    manager.endPlay();
}

/** @brief [ScavengerGameModeTest] 시계는 내려 있는 동안만 흐르고 이륙하면 다음 날 도착 시각이며, 위성의 날씨가 고철 값과 위협에 걸린다 */
SW_TEST_CASE( ScavengerGameModeTest, ClockRunsOnlyOnTheMoonAndWeatherScalesTheScrap )
{
    ScavengerGameModeBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    game::ScopedGameScope       scopedScope( bench._scope );
    GameObjectManager           manager;
    ScavengerGameModeComponent* pDirector = ScavengerGameModeBench::addDirector( manager, "company", false );
    SW_REQUIRE_NOT_NULL( pDirector );
    manager.beginPlay();
    SW_REQUIRE_TRUE( pDirector->isStarted() );
    const GameStateComponent* const pState = GameStateComponent::findOnOwner( *pDirector );
    SW_REQUIRE_NOT_NULL( pState );
    constexpr int32 kSteps = 30;
    for ( int32 step = 0; step < kSteps; ++step )
    {
        manager.update( kScavengerGameModeStep );
    }
    SW_EXPECT_NEAR( 6.0f, pState->getClock().getHour(), 1.0e-3f );
    ScavengerCommand lever;
    lever._bLeverTriggered = SW_TRUE;
    SW_EXPECT_TRUE( pDirector->submitCrewCommand( 0, lever ) );
    manager.update( kScavengerGameModeStep );
    SW_EXPECT_TRUE( pState->getWeather().getCurrent() == hashed_string( "eclipse" ) );
    SW_EXPECT_EQUAL( 200, pDirector->computeWeatherPercent( "scrapValue" ) );
    SW_EXPECT_EQUAL( 300, pDirector->computeWeatherPercent( "threat" ) );
    SW_EXPECT_EQUAL( 60, pDirector->getExpedition().getFacility().computeGroundValue() ); // 볼트 셋 × 10 × 2
    for ( int32 step = 0; step < kSteps; ++step )
    {
        manager.update( kScavengerGameModeStep );
    }
    SW_EXPECT_TRUE( pState->getClock().getHour() > 6.0f );
    SW_EXPECT_TRUE( pDirector->submitCrewCommand( 0, lever ) ); // 우주선을 떠나지 않았다
    manager.update( kScavengerGameModeStep );
    SW_EXPECT_EQUAL( static_cast<int32>( ScavengerPhase::InOrbit ), pDirector->getPhase() );
    SW_EXPECT_EQUAL( 1, pState->getClock().getDay() );
    SW_EXPECT_NEAR( 6.0f, pState->getClock().getHour(), 1.0e-2f );
    manager.endPlay();
}
