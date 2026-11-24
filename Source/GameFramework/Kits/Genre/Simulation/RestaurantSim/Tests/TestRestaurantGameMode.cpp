// RestaurantGameModeTest — 식당 디렉터: 명령은 판이 열린 동안만 받고, 씬 데이터가 비었으면 판을 열지 않으며, 장보기 → 영업 → 결산 → 다음 날 → 목표 판정과 상태 왕복(nogpu).
#include "pch.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/Component/RestaurantGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Presentation/Tests/RecordingSoundPlayer.h"

using namespace sw;

namespace
{
    // 한 시간 영업에 손님은 2 분에 하나(시간당 30) — 재료가 떨어지면 시키지 않고 나간다. 팁 0 이라 이익 = 낸 손님 × 40 - 재료 5 - 급여다.
    constexpr const utf8* kRestaurantGameModeXML = R"(<RestaurantCatalog open="11" close="12" window="10">
  <Dish id="bowl" name="Bowl" recipe="bowl" price="40"/>
  <Customer id="porter" patience="60" budget="1.2" tipRate="0" eat="2"/>
  <Arrival hour="11" rate="30"/>
  <ExperienceCurve base="20" exponent="0" maxLevel="5"/>
</RestaurantCatalog>)";

    constexpr const utf8* kRestaurantRecipeXML = R"(<RecipeCatalog><Recipe id="bowl" station="Stove" time="1"><In item="noodle"/><Out item="bowl"/></Recipe></RecipeCatalog>)";

    constexpr const utf8* kRestaurantItemXML = R"(<ItemCatalog><Item id="noodle" maxStack="10"/></ItemCatalog>)";

    constexpr const utf8* kRestaurantShopXML = R"(<ShopCatalog><Shop id="market" buybackLimit="0"><Stock item="noodle" price="5"/></Shop></ShopCatalog>)";

    constexpr float32 kRestaurantFrame = 0.125f; ///< 분당 8 초 설정이면 한 프레임이 1 분이다

    /** @brief 카탈로그를 건 게임 범위와 씬 데이터를 읽은 디렉터입니다. 가게는 임시 도메인의 파일이다. 테스트 본문이 범위를 `ScopedGameScope` 로 겁니다. */
    struct RestaurantGameModeFixture
    {
        RestaurantCatalog            _catalog;
        RecipeCatalog                _recipes;
        ItemCatalog                  _items;
        RecordingSoundPlayer         _soundPlayer{};
        SubsystemScope               _scope;
        GameObjectManager            _manager;
        RestaurantGameModeComponent* _pGameMode{ nullptr };

        [[nodiscard]] bool initialize( const test::ScopedScratchDomain& domain, int32 dayCount, int32 targetProfit )
        {
            (void)domain.makeDiskFolder( "diner" ); // 폴더만 만든다
            if ( FileUtil::writeTextFile( domain.makeDiskPath( "diner/market.shops.xml" ), kRestaurantShopXML ) == false )
                return false;
            const bool bCatalogs = _catalog.loadFromXMLText( kRestaurantGameModeXML, "RestaurantGameModeTest" ) && _recipes.loadFromXMLText( kRestaurantRecipeXML, "RestaurantGameModeTest" ) &&
                                   _items.loadFromXMLText( kRestaurantItemXML, "RestaurantGameModeTest" );
            if ( bCatalogs == false || _scope.bind<ISoundPlayer>( &_soundPlayer ) == false || _scope.bind<const ItemCatalog>( &_items ) == false || _scope.bind<const RestaurantCatalog>( &_catalog ) == false || _scope.bind<const RecipeCatalog>( &_recipes ) == false )
                return false;
            StringBuilder<constant::buffer::kMax1024> sceneText;
            sceneText.appendFormat( "<RestaurantGameModeComponent _shops=\"%#\" _shop=\"market\" _buyCount=\"5\" _pantrySlotCount=\"20\" _startGold=\"100\" _dailyWage=\"10\" _priceStep=\"4\" "
                                    "_minutesPerSecond=\"8\" _dayCount=\"%#\" _targetProfit=\"%#\" _stationPrice=\"15\"><_listIngredient><item>noodle</item></_listIngredient><_listStation><item>Stove</item></_listStation>"
                                    "<_settings _marketVolatility=\"0\"/></RestaurantGameModeComponent>",
                                    test::ScopedScratchDomain::makeID( "diner/market.shops.xml" ).str(), dayCount, targetProfit );
            _pGameMode = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<RestaurantGameModeComponent>();
            return _pGameMode != nullptr && XMLSerializer::deserialize( _pGameMode, *RestaurantGameModeComponent::StaticType(), sceneText.view() );
        }

        /** @brief 판을 열고 빈 프레임 하나를 돌립니다(첫 프레임은 틱 그룹을 옮기는 일이 든다). */
        void beginPlay()
        {
            _manager.beginPlay();
            _manager.update( kRestaurantFrame );
        }

        /** @brief 문을 닫을 때까지(많아도 @p maxFrameCount 프레임) 시간을 흘립니다. */
        void runUntilClosed( int32 maxFrameCount )
        {
            for ( int32 frame = 0; frame < maxFrameCount && _pGameMode->getGamePhase() == RestaurantPhase::Open; ++frame )
            {
                _manager.update( kRestaurantFrame );
            }
        }
    };
} // namespace

/** @brief [RestaurantGameModeTest] 판이 열리기 전의 명령은 모두 거절한다 — 클라이언트가 먼저 돌아도 판을 바꾸지 않는다 */
SW_TEST_CASE( RestaurantGameModeTest, CommandsBeforeTheRestaurantOpensAreRefused )
{
    GameObjectManager            manager;
    RestaurantGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<RestaurantGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->submitBuy( 0 ) );
    SW_EXPECT_FALSE( pGameMode->submitPriceChange( 0, 1 ) );
    SW_EXPECT_FALSE( pGameMode->submitAddStation( 0 ) );
    SW_EXPECT_FALSE( pGameMode->submitOpen() );
    SW_EXPECT_FALSE( pGameMode->submitNextDay() );
    SW_EXPECT_EQUAL( 0, pGameMode->getFirstMenuPrice() );
}

/** @brief [RestaurantGameModeTest] 씬이 가게 카탈로그를 적지 않았으면 오류를 남기고 판을 열지 않는다 — C++ 기본값으로 열지 않는다 */
SW_TEST_CASE( RestaurantGameModeTest, RestaurantWithoutShopsDoesNotOpen )
{
    SW_EXPECT_LOG( "GameMode.RequiredAssetMissing" );
    RestaurantCatalog catalog;
    RecipeCatalog     recipes;
    ItemCatalog       items;
    SubsystemScope    scope;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kRestaurantGameModeXML, "RestaurantGameModeTest" ) && recipes.loadFromXMLText( kRestaurantRecipeXML, "RestaurantGameModeTest" ) );
    SW_REQUIRE_TRUE( scope.bind<const RestaurantCatalog>( &catalog ) && scope.bind<const RecipeCatalog>( &recipes ) && scope.bind<const ItemCatalog>( &items ) );
    game::ScopedGameScope        scopedScope( scope );
    GameObjectManager            manager;
    RestaurantGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<RestaurantGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_EXPECT_FALSE( pGameMode->isStarted() );
    manager.endPlay();
}

/** @brief [RestaurantGameModeTest] 산 재료만큼 손님을 받고, 결산 뒤 다음 날로 넘어가며, 마지막 날의 이익 합이 목표 이상이면 승리다 */
SW_TEST_CASE( RestaurantGameModeTest, TwoDaysOfBusinessReachTheTarget )
{
    const test::ScopedScratchDomain domain;
    RestaurantGameModeFixture       fixture;
    SW_REQUIRE_TRUE( fixture.initialize( domain, 2, 100 ) );
    game::ScopedGameScope scopedScope( fixture._scope );
    fixture.beginPlay();
    SW_REQUIRE_TRUE( fixture._pGameMode->isStarted() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitBuy( 1 ) ); // 없는 줄
    SW_REQUIRE_TRUE( fixture._pGameMode->submitBuy( 0 ) );
    SW_EXPECT_EQUAL( 75, fixture._pGameMode->getGold() );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitPriceChange( 0, 1 ) );
    SW_EXPECT_EQUAL( 44, fixture._pGameMode->getFirstMenuPrice() );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitPriceChange( 0, -1 ) );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitOpen() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitPriceChange( 0, 1 ) ); // 영업 중에는 메뉴를 바꾸지 않는다
    fixture.runUntilClosed( 200 );
    SW_REQUIRE_EQUAL( 2, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 5, fixture._pGameMode->getServed() );
    SW_EXPECT_TRUE( fixture._soundPlayer.countPosted( "RestaurantOrder" ) >= 5 ); // 낸 손님은 모두 시켰다
    SW_EXPECT_EQUAL( 155, fixture._pGameMode->getTotalProfit() );                 // 5 × 40 - 5 × 5 - 급여 20
    SW_EXPECT_EQUAL( 255, fixture._pGameMode->getGold() );                        // 75 + 200 - 20

    SW_REQUIRE_TRUE( fixture._pGameMode->submitNextDay() );
    SW_EXPECT_EQUAL( 2, fixture._pGameMode->getDay() );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitOpen() ); // 재료 없이 연다 — 손님은 시키지 않고 나간다
    fixture.runUntilClosed( 200 );
    SW_EXPECT_EQUAL( 3, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 0, fixture._pGameMode->getServed() );
    SW_EXPECT_EQUAL( 135, fixture._pGameMode->getTotalProfit() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitBuy( 0 ) );
    fixture._manager.endPlay();
}

/** @brief [RestaurantGameModeTest] 마지막 날의 이익 합이 목표에 못 미치면 패배다 */
SW_TEST_CASE( RestaurantGameModeTest, MissingTheTargetIsADefeat )
{
    const test::ScopedScratchDomain domain;
    RestaurantGameModeFixture       fixture;
    SW_REQUIRE_TRUE( fixture.initialize( domain, 1, 100 ) );
    game::ScopedGameScope scopedScope( fixture._scope );
    fixture.beginPlay();
    SW_REQUIRE_TRUE( fixture._pGameMode->submitOpen() );
    fixture.runUntilClosed( 200 );
    SW_EXPECT_EQUAL( 4, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( -20, fixture._pGameMode->getTotalProfit() );
    fixture._manager.endPlay();
}

/** @brief [RestaurantGameModeTest] 영업 중의 상태를 새 디렉터에 복원하면 같은 결산으로 이어지고, 잘린 바이트는 거절해 판이 그대로다 */
SW_TEST_CASE( RestaurantGameModeTest, OpenDaySurvivesARoundTrip )
{
    const test::ScopedScratchDomain domain;
    RestaurantGameModeFixture       source;
    SW_REQUIRE_TRUE( source.initialize( domain, 2, 100 ) );
    game::ScopedGameScope scopedScope( source._scope );
    source.beginPlay();
    SW_REQUIRE_TRUE( source._pGameMode->submitBuy( 0 ) );
    SW_REQUIRE_TRUE( source._pGameMode->submitOpen() );
    for ( int32 frame = 0; frame < 10; ++frame )
    {
        source._manager.update( kRestaurantFrame );
    }
    SW_REQUIRE_EQUAL( 1, source._pGameMode->getPhase() );
    Archive state;
    source._pGameMode->writeState( state );
    vector<uint8> bytes( state.getData(), state.getData() + state.getSize() );
    source.runUntilClosed( 200 );

    RestaurantGameModeFixture restored;
    SW_REQUIRE_TRUE( restored.initialize( domain, 2, 100 ) );
    restored.beginPlay();
    vector<uint8> truncated( bytes.begin(), bytes.end() - 4 );
    restored._pGameMode->restoreState( std::move( truncated ) );
    SW_EXPECT_EQUAL( 0, restored._pGameMode->getPhase() );
    restored._pGameMode->restoreState( std::move( bytes ) );
    SW_REQUIRE_EQUAL( 1, restored._pGameMode->getPhase() );
    restored.runUntilClosed( 200 );
    SW_EXPECT_EQUAL( 2, restored._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( source._pGameMode->getTotalProfit(), restored._pGameMode->getTotalProfit() );
    SW_EXPECT_EQUAL( source._pGameMode->getGold(), restored._pGameMode->getGold() );
    SW_EXPECT_EQUAL( source._pGameMode->getReputation(), restored._pGameMode->getReputation() );
    SW_EXPECT_EQUAL( source._pGameMode->getBestCookLevel(), restored._pGameMode->getBestCookLevel() );
    restored._manager.endPlay();
    source._manager.endPlay();
}

/** @brief [RestaurantGameModeTest] 준비 중에 들인 조리 스테이션은 돈과 이익에서 빠지고, 요리한 만큼 요리사가 자라며 만족한 손님이 평판을 올린다 */
SW_TEST_CASE( RestaurantGameModeTest, StationsStaffAndReputationGrow )
{
    const test::ScopedScratchDomain domain;
    RestaurantGameModeFixture       fixture;
    SW_REQUIRE_TRUE( fixture.initialize( domain, 2, 100 ) );
    game::ScopedGameScope scopedScope( fixture._scope );
    fixture.beginPlay();
    SW_EXPECT_EQUAL( 1, fixture._pGameMode->getFirstStationCount() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitAddStation( 1 ) ); // 없는 요리
    SW_REQUIRE_TRUE( fixture._pGameMode->submitAddStation( 0 ) );
    SW_EXPECT_EQUAL( 2, fixture._pGameMode->getFirstStationCount() );
    SW_EXPECT_EQUAL( 85, fixture._pGameMode->getGold() );
    SW_EXPECT_EQUAL( -15, fixture._pGameMode->getTotalProfit() );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitBuy( 0 ) );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitOpen() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitAddStation( 0 ) ); // 영업 중에는 들이지 않는다
    fixture.runUntilClosed( 200 );
    SW_EXPECT_EQUAL( 140, fixture._pGameMode->getTotalProfit() ); // 5 × 40 - 5 × 5 - 급여 20 - 15
    SW_EXPECT_EQUAL( 3, fixture._pGameMode->getBestCookLevel() ); // 요리 다섯 × 경험치 10 = 50, 레벨마다 20
    SW_EXPECT_TRUE( fixture._pGameMode->getReputation() > 0 );
    fixture._manager.endPlay();
}
