#include "pch.h"

#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Progression/Reputation.h"
#include "GameFramework/Base/Quest/QuestCatalog.h"
#include "GameFramework/Base/World/WeatherSystem.h"
#include "GameFramework/Base/World/WorldClock.h"
#include "GameFramework/Kits/Simulation/CreatureLife/CreatureLifeCatalog.h"
#include "GameFramework/Kits/Simulation/CreatureLife/CreatureTown.h"

#include "TestFramework/TestFramework.h"

// 생물 생활 키트(포코피아 류) — 서식지 패턴(회전 · 큰 것 먼저 · 칸 독점), 때 · 날씨 방문(결정적), 호감도 하루 제한, 부탁, 능력 편집 횟수, 집 배정, 매력도 단계.

using namespace sw;

namespace
{
    constexpr const utf8* kCreatureLifeTestXml = R"(
<CreatureLifeCatalog>
  <Habitat id="tall_grass" name="Tall Grass">
    <Key symbol="G" object="grass"/><Key symbol="T" object="tree"/>
    <Row cells="GGT"/><Row cells="GG."/>
  </Habitat>
  <Habitat id="flower_patch"><Key symbol="G" object="grass"/><Row cells="GG"/></Habitat>
  <Habitat id="pond" capacity="2"><Key symbol="W" object="water"/><Row cells="WW"/></Habitat>
  <Habitat id="broken"><Key symbol="G" object="grass"/><Row cells="GG"/><Row cells="G"/></Habitat>
  <Ability id="plant_tree" uses="2"><Rule from="empty" to="tree"/><Rule from="grass" to="tree"/></Ability>
  <Ability id="water_gun" uses="3"><Rule from="empty" to="water"/></Ability>
  <Ability id="rock_smash" uses="1"><Rule from="rock" to="empty" yield="stone" count="2"/></Ability>
  <Species id="sprout" habitats="tall_grass" foods="berry" gifts="flower" phases="Dawn,Day" weathers="sunny" chance="1"
           abilities="plant_tree,rock_smash" requests="sprout_berries,sprout_pond"/>
  <Species id="frog" habitats="pond" phases="Night" weathers="rain" chance="1" abilities="water_gun"/>
  <Species id="newt" habitats="pond" chance="0.35"/>
  <Appeal diversity="10" creature="5" friendship="0.1"><Tier name="Camp" min="0"/><Tier name="Village" min="30"/><Tier name="Town" min="60"/></Appeal>
</CreatureLifeCatalog>
)";

    constexpr const utf8* kCreatureLifeTestReputationXml = R"(
<ReputationCatalog>
  <Faction id="sprout" min="0" max="1000" start="0"><Tier name="Stranger" min="0"/><Tier name="Friend" min="50"/><Tier name="Best" min="150"/></Faction>
</ReputationCatalog>
)";

    constexpr const utf8* kCreatureLifeTestQuestXml = R"(
<QuestCatalog>
  <Quest id="sprout_berries">
    <Stage id="bring" next="done"><Objective kind="Deliver" target="berry" count="3"/></Stage>
    <Stage id="done" complete="true"/>
  </Quest>
  <Quest id="sprout_pond" level="1">
    <Stage id="make" next="done"><Objective kind="Habitat" target="pond" count="1"/></Stage>
    <Stage id="done" complete="true"/>
  </Quest>
</QuestCatalog>
)";

    constexpr const utf8* kCreatureLifeTestWeatherXml = R"(
<WeatherCatalog transition="0"><Weather id="sunny"/><Weather id="rain"/></WeatherCatalog>
)";

    /** @brief 시험이 함께 쓰는 카탈로그들입니다. */
    struct CreatureLifeTestWorld
    {
        CreatureLifeCatalog _catalog;
        ReputationCatalog   _reputation;
        QuestCatalog        _quests;
        ItemCatalog         _items;

        bool load()
        {
            for ( const utf8* pItemId : { "flower", "berry", "stone" } )
            {
                ItemDef item;
                item._id       = hashed_string( pItemId );
                item._maxStack = 99;
                _items.addItem( item );
            }
            return _catalog.loadFromXmlText( kCreatureLifeTestXml, "CreatureLifeTest" ) && _reputation.loadFromXmlText( kCreatureLifeTestReputationXml, "CreatureLifeTest" ) &&
                   _quests.loadFromXmlText( kCreatureLifeTestQuestXml, "CreatureLifeTest" );
        }
    };

    /** @brief 4 칸 풀 + 나무 1 을 회전 없이 (@p x, @p y) 에 놓습니다(나무는 오른쪽 아래 줄 끝). */
    void placeTallGrass( CreatureTown& town, int32 x, int32 y, bool bWithTree )
    {
        (void)town.setObject( x, y, "grass" );
        (void)town.setObject( x + 1, y, "grass" );
        (void)town.setObject( x, y + 1, "grass" );
        (void)town.setObject( x + 1, y + 1, "grass" );
        if ( bWithTree )
            (void)town.setObject( x + 2, y, "tree" );
    }

    int32 countTownEvents( const vector<CreatureTownEvent>& listEvent, CreatureTownEvent::Kind kind )
    {
        int32 count = 0;
        for ( const CreatureTownEvent& townEvent : listEvent )
        {
            if ( townEvent._kind == kind )
                ++count;
        }
        return count;
    }

    /** @brief 풀숲을 만들고 낮 · 맑음에 sprout 를 부릅니다. */
    void makeTownWithSprout( CreatureTown& town, const CreatureLifeTestWorld& world )
    {
        town.initialize( &world._catalog, &world._reputation, &world._quests, 10, 10, CreatureTownSettings{} );
        placeTallGrass( town, 0, 0, true );
        (void)town.attractVisitorsAt( 0, 9, DayPhase::Day, "sunny" );
    }
} // namespace

/**
 * @brief [CreatureLifeTest] 카탈로그 — 행 길이가 다른 서식지는 빠지고, 차지 칸이 많은 서식지가 먼저 맞춰지며, 종의 때 · 날씨 조건을 읽는다
 */
SW_TEST_CASE( CreatureLifeTest, CatalogReadsPatternsAndOrdersBySize )
{
    CreatureLifeTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), world._catalog.getHabitats().size() ); // broken 은 빠졌다
    SW_EXPECT_TRUE( world._catalog.findHabitat( "broken" ) == nullptr );

    const HabitatDef* pTallGrass = world._catalog.findHabitat( "tall_grass" );
    SW_ASSERT_NOT_NULL( pTallGrass );
    SW_EXPECT_EQUAL( 3, pTallGrass->_width );
    SW_EXPECT_EQUAL( 2, pTallGrass->_height );
    SW_EXPECT_EQUAL( 5, pTallGrass->countClaimedCells() );
    SW_EXPECT_EQUAL( 0, world._catalog.getHabitatMatchOrder().front() ); // 5 칸이 2 칸보다 먼저

    const CreatureSpeciesDef* pSprout = world._catalog.findSpecies( "sprout" );
    SW_ASSERT_NOT_NULL( pSprout );
    SW_EXPECT_TRUE( pSprout->comesIn( DayPhase::Day, "sunny" ) );
    SW_EXPECT_FALSE( pSprout->comesIn( DayPhase::Night, "sunny" ) );
    SW_EXPECT_FALSE( pSprout->comesIn( DayPhase::Day, "rain" ) );
    SW_EXPECT_TRUE( world._catalog.findSpecies( "newt" )->comesIn( DayPhase::Dusk, "storm" ) ); // 조건이 없으면 언제나

    const CreatureAbilityDef* pSmash = world._catalog.findAbility( "rock_smash" );
    SW_ASSERT_NOT_NULL( pSmash );
    SW_EXPECT_TRUE( pSmash->findRule( "rock" ) != nullptr && pSmash->findRule( "rock" )->_to.empty() );
    SW_EXPECT_EQUAL( 2, pSmash->findRule( "rock" )->_yieldCount );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), world._catalog.getAppeal()._listTier.size() );
}

/**
 * @brief [CreatureLifeTest] 방문 조건(때 · 날씨)은 데이터에 적은 그대로다 — 모든 때 × 날씨 × 종에서 "때 목록이 비었거나 그 때를 담고, 날씨 목록이 비었거나
 *        그 날씨를 담는다" 와 같다(일정 조건 `ScheduleCondition` 으로 옮겨도 판정이 바뀌지 않았다는 증거)
 */
SW_TEST_CASE( CreatureLifeTest, VisitConditionMatchesThePhaseWeatherTable )
{
    CreatureLifeTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    struct Expectation
    {
        const utf8*     _pSpecies;
        const DayPhase* _pPhaseBegin;
        size_t          _phaseCount;
        const utf8*     _pWeather; ///< 비면 언제나
    };
    const DayPhase    arrSproutPhase[] = { DayPhase::Dawn, DayPhase::Day };
    const DayPhase    arrFrogPhase[]   = { DayPhase::Night };
    const Expectation arrExpectation[] = {
        {"sprout", arrSproutPhase, 2, "sunny"},
        {  "frog",   arrFrogPhase, 1,  "rain"},
        {  "newt",        nullptr, 0,      ""},
    };
    const utf8* const arrWeather[] = { "sunny", "rain", "storm", "" };
    for ( const Expectation& expectation : arrExpectation )
    {
        const CreatureSpeciesDef* pSpecies = world._catalog.findSpecies( hashed_string( expectation._pSpecies ) );
        SW_ASSERT_NOT_NULL( pSpecies );
        for ( uint32 phaseIndex = 0; phaseIndex <= static_cast<uint32>( DayPhase::Dusk ); ++phaseIndex )
        {
            const DayPhase phase       = static_cast<DayPhase>( phaseIndex );
            bool           bPhaseMatch = expectation._phaseCount == 0;
            for ( size_t listIndex = 0; listIndex < expectation._phaseCount; ++listIndex )
                bPhaseMatch = bPhaseMatch || expectation._pPhaseBegin[listIndex] == phase;
            for ( const utf8* pWeather : arrWeather )
            {
                const bool bWeatherMatch = expectation._pWeather[0] == '\0' || StringUtil::equals( expectation._pWeather, pWeather );
                const bool bExpected     = bPhaseMatch && bWeatherMatch;
                SW_EXPECT_EQUAL( bExpected, pSpecies->comesIn( phase, hashed_string( pWeather ) ) );
            }
        }
    }
}

/**
 * @brief [CreatureLifeTest] 풀 4 칸은 꽃밭 둘이지만 나무가 더해지면 큰 풀숲 하나가 칸을 모두 차지한다 — 90° 돌린 모양도 같은 서식지다
 */
SW_TEST_CASE( CreatureLifeTest, HabitatsMatchRotatedPatternsAndLargestWins )
{
    CreatureLifeTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    CreatureTown town;
    town.initialize( &world._catalog, nullptr, nullptr, 8, 8, CreatureTownSettings{} );

    // 돌린 풀숲: 2×2 풀 + 그 위 줄 오른쪽에 나무(돌리지 않은 패턴은 나무가 풀 줄의 오른쪽 끝에 있어야 한다).
    (void)town.setObject( 2, 2, "grass" );
    (void)town.setObject( 3, 2, "grass" );
    (void)town.setObject( 2, 3, "grass" );
    (void)town.setObject( 3, 3, "grass" );
    SW_EXPECT_EQUAL( 2, town.countHabitats( "flower_patch" ) ); // 칸은 한 서식지에만 — 넷이 둘로
    SW_EXPECT_EQUAL( 0, town.countHabitats( "tall_grass" ) );

    vector<CreatureTownEvent> listEvent;
    town.drainEvents( listEvent );
    listEvent.clear();
    (void)town.setObject( 3, 4, "tree" );
    town.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, town.countHabitats( "tall_grass" ) );
    SW_EXPECT_EQUAL( 0, town.countHabitats( "flower_patch" ) );
    SW_EXPECT_EQUAL( 2, countTownEvents( listEvent, CreatureTownEvent::Kind::HabitatLost ) );
    SW_EXPECT_EQUAL( 1, countTownEvents( listEvent, CreatureTownEvent::Kind::HabitatFormed ) );
    SW_ASSERT_TRUE( town.getHabitats().size() == 1 );
    SW_EXPECT_EQUAL( 1, town.getHabitats()[0]._rotation );
    SW_EXPECT_EQUAL( 5, static_cast<int32>( town.getHabitats()[0]._listTile.size() ) );

    // 무관한 칸을 바꿔도 서식지 번호는 그대로다.
    const int32 habitatId = town.getHabitats()[0]._id;
    (void)town.setObject( 7, 7, "rock" );
    SW_EXPECT_EQUAL( habitatId, town.getHabitats()[0]._id );

    // 나무를 치우면 풀숲은 사라지고 꽃밭 둘로 돌아간다.
    (void)town.setObject( 3, 4, {} );
    SW_EXPECT_EQUAL( 0, town.countHabitats( "tall_grass" ) );
    SW_EXPECT_EQUAL( 2, town.countHabitats( "flower_patch" ) );
    SW_EXPECT_FALSE( town.setObject( 8, 0, "grass" ) );
}

/**
 * @brief [CreatureLifeTest] 생물은 좋아하는 서식지가 있고 때 · 날씨가 맞을 때만 오며, 같은 시를 두 번 굴려도 한 번이고, 확률은 씨앗으로 되풀이된다
 */
SW_TEST_CASE( CreatureLifeTest, VisitorsFollowPhaseWeatherAndAreDeterministic )
{
    CreatureLifeTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    WeatherCatalog weatherCatalog;
    SW_ASSERT_TRUE( weatherCatalog.loadFromXmlText( kCreatureLifeTestWeatherXml, "CreatureLifeTest" ) );
    WeatherSystem weather;
    weather.initialize( &weatherCatalog, 11u, "Default" );
    WorldClock clock;
    clock.initialize( WorldClockSettings{} );

    CreatureTown town;
    town.initialize( &world._catalog, &world._reputation, &world._quests, 10, 10, CreatureTownSettings{} );
    weather.forceWeather( "sunny", 0.0f, true );
    clock.setTime( 0, 9.0f );
    SW_EXPECT_EQUAL( 0, town.attractVisitors( clock, weather ) ); // 서식지가 없다

    placeTallGrass( town, 0, 0, true );
    clock.setTime( 0, 22.0f ); // 밤
    SW_EXPECT_EQUAL( 0, town.attractVisitors( clock, weather ) );
    weather.forceWeather( "rain", 0.0f, true );
    clock.setTime( 1, 9.0f ); // 낮인데 비
    SW_EXPECT_EQUAL( 0, town.attractVisitors( clock, weather ) );
    weather.forceWeather( "sunny", 0.0f, true );
    SW_EXPECT_EQUAL( 0, town.attractVisitors( clock, weather ) ); // 같은 시는 다시 굴리지 않는다
    clock.setTime( 1, 10.0f );
    SW_EXPECT_EQUAL( 1, town.attractVisitors( clock, weather ) );
    SW_ASSERT_NOT_NULL( town.findCreature( "sprout" ) );
    SW_EXPECT_EQUAL( town.getHabitats()[0]._id, town.findCreature( "sprout" )->_habitat );
    clock.setTime( 1, 11.0f );
    SW_EXPECT_EQUAL( 0, town.attractVisitors( clock, weather ) ); // 종마다 한 마리, 풀숲 자리 1

    // 연못(자리 2): 밤 · 비의 frog 는 늘, newt 는 35 % — 같은 씨앗이면 두 마을이 같은 시각에 받는다.
    CreatureTown townB;
    townB.initialize( &world._catalog, nullptr, nullptr, 10, 10, CreatureTownSettings{} );
    (void)town.setObject( 0, 5, "water" );
    (void)town.setObject( 1, 5, "water" );
    (void)townB.setObject( 0, 5, "water" );
    (void)townB.setObject( 1, 5, "water" );
    int32 newtKeyA = -1;
    int32 newtKeyB = -1;
    for ( int32 hourKey = 48; hourKey < 48 + 24 * 6; ++hourKey )
    {
        (void)town.attractVisitorsAt( hourKey / 24, hourKey % 24, DayPhase::Night, "rain" );
        (void)townB.attractVisitorsAt( hourKey / 24, hourKey % 24, DayPhase::Night, "rain" );
        if ( newtKeyA < 0 && town.findCreature( "newt" ) != nullptr )
            newtKeyA = hourKey;
        if ( newtKeyB < 0 && townB.findCreature( "newt" ) != nullptr )
            newtKeyB = hourKey;
    }
    SW_EXPECT_TRUE( town.findCreature( "frog" ) != nullptr );
    SW_EXPECT_TRUE( newtKeyA >= 48 );
    SW_EXPECT_EQUAL( newtKeyA, newtKeyB );
    SW_EXPECT_EQUAL( 2, town.countResidents( town.findCreature( "frog" )->_habitat ) );
}

/**
 * @brief [CreatureLifeTest] 대화 · 선물은 하루 한 번씩 — 좋아하는 선물 30, 좋아하는 음식 20, 단계를 넘으면 알린다. 없는 아이템은 하루 한 번을 쓰지 않는다
 */
SW_TEST_CASE( CreatureLifeTest, FriendshipTalkAndGiftOncePerDay )
{
    CreatureLifeTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    CreatureTown town;
    makeTownWithSprout( town, world );
    SW_ASSERT_NOT_NULL( town.findCreature( "sprout" ) );
    Inventory inventory;
    inventory.initialize( &world._items, 8 );
    (void)inventory.addItem( "flower", 2 );
    (void)inventory.addItem( "berry", 5 );

    SW_EXPECT_TRUE( town.talkTo( "frog" ) == CreatureInteractResult::UnknownCreature );
    SW_EXPECT_TRUE( town.talkTo( "sprout" ) == CreatureInteractResult::Ok );
    SW_EXPECT_TRUE( town.talkTo( "sprout" ) == CreatureInteractResult::AlreadyToday );
    SW_EXPECT_EQUAL( 5, town.getFriendship( "sprout" ) );
    SW_EXPECT_TRUE( town.giveGift( "sprout", "stone", inventory ) == CreatureInteractResult::MissingItem );
    SW_EXPECT_TRUE( town.giveGift( "sprout", "flower", inventory ) == CreatureInteractResult::Ok );
    SW_EXPECT_EQUAL( 35, town.getFriendship( "sprout" ) );
    SW_EXPECT_TRUE( town.giveGift( "sprout", "flower", inventory ) == CreatureInteractResult::AlreadyToday );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( "flower" ) ); // 거절된 선물은 빼지 않는다
    SW_EXPECT_TRUE( town.getFriendshipTier( "sprout" ) == hashed_string( "Stranger" ) );

    vector<CreatureTownEvent> listEvent;
    town.drainEvents( listEvent );
    listEvent.clear();
    town.advanceDay();
    SW_EXPECT_TRUE( town.giveGift( "sprout", "berry", inventory ) == CreatureInteractResult::Ok );
    SW_EXPECT_EQUAL( 55, town.getFriendship( "sprout" ) );
    SW_EXPECT_TRUE( town.getFriendshipTier( "sprout" ) == hashed_string( "Friend" ) );
    town.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countTownEvents( listEvent, CreatureTownEvent::Kind::FriendshipTierChanged ) );
    SW_EXPECT_TRUE( town.talkTo( "sprout" ) == CreatureInteractResult::Ok );
    SW_EXPECT_EQUAL( 60, town.getFriendship( "sprout" ) );
}

/**
 * @brief [CreatureLifeTest] 부탁 — 하지 않는 부탁 · 호감도 단계가 모자란 부탁은 못 받고, 물건 건네기와 서식지 만들기로 끝내면 호감도가 오른다
 */
SW_TEST_CASE( CreatureLifeTest, RequestsDeliverItemsAndHabitats )
{
    CreatureLifeTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    CreatureTown town;
    makeTownWithSprout( town, world );
    Inventory inventory;
    inventory.initialize( &world._items, 8 );
    (void)inventory.addItem( "berry", 4 );
    (void)inventory.addItem( "flower", 1 );

    SW_EXPECT_TRUE( town.startRequest( "sprout", "frog_errand" ) == CreatureRequestResult::NotOffered );
    SW_EXPECT_TRUE( town.startRequest( "frog", "sprout_berries" ) == CreatureRequestResult::UnknownCreature );
    SW_EXPECT_TRUE( town.startRequest( "sprout", "sprout_pond" ) == CreatureRequestResult::Unavailable ); // 단계 1(Friend) 필요
    SW_EXPECT_TRUE( town.startRequest( "sprout", "sprout_berries" ) == CreatureRequestResult::Ok );
    SW_EXPECT_TRUE( town.startRequest( "sprout", "sprout_berries" ) == CreatureRequestResult::AlreadyActive );

    SW_EXPECT_EQUAL( 0, town.deliverItem( "flower", 1, inventory ) ); // 세는 목표가 없다 — 빼지 않는다
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( "flower" ) );
    SW_EXPECT_EQUAL( 0, town.deliverItem( "berry", 9, inventory ) ); // 가진 것보다 많다
    vector<CreatureTownEvent> listEvent;
    town.drainEvents( listEvent );
    listEvent.clear();
    SW_EXPECT_EQUAL( 1, town.deliverItem( "berry", 3, inventory ) );
    SW_EXPECT_EQUAL( 1, inventory.getItemCount( "berry" ) );
    SW_EXPECT_TRUE( town.getQuestLog().getStatus( "sprout_berries" ) == QuestStatus::Completed );
    SW_EXPECT_EQUAL( 50, town.getFriendship( "sprout" ) );
    town.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countTownEvents( listEvent, CreatureTownEvent::Kind::RequestCompleted ) );

    SW_EXPECT_TRUE( town.startRequest( "sprout", "sprout_pond" ) == CreatureRequestResult::Ok );
    (void)town.setObject( 6, 6, "water" );
    SW_EXPECT_TRUE( town.getQuestLog().getStatus( "sprout_pond" ) == QuestStatus::Active );
    (void)town.setObject( 6, 7, "water" ); // 세로로 돌린 연못
    SW_EXPECT_TRUE( town.getQuestLog().getStatus( "sprout_pond" ) == QuestStatus::Completed );
    SW_EXPECT_EQUAL( 100, town.getFriendship( "sprout" ) );
}

/**
 * @brief [CreatureLifeTest] 능력 — 칸 변환 규칙(빈 칸 → 나무, 바위 → 빈 칸 + 돌 2)과 하루 횟수, 능력으로 놓은 나무가 서식지를 완성한다
 */
SW_TEST_CASE( CreatureLifeTest, AbilitiesEditTilesWithinDailyUses )
{
    CreatureLifeTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    CreatureTown town;
    makeTownWithSprout( town, world );
    Inventory inventory;
    inventory.initialize( &world._items, 8 );

    placeTallGrass( town, 5, 5, false ); // 나무 자리(7, 5)가 비었다
    (void)town.setObject( 0, 8, "rock" );
    (void)town.setObject( 9, 9, "water" );
    SW_EXPECT_EQUAL( 1, town.countHabitats( "tall_grass" ) );

    SW_EXPECT_TRUE( town.useAbility( "sprout", "water_gun", 4, 4, nullptr ) == CreatureAbilityResult::NotKnown );
    SW_EXPECT_TRUE( town.useAbility( "frog", "water_gun", 4, 4, nullptr ) == CreatureAbilityResult::UnknownCreature );
    SW_EXPECT_TRUE( town.useAbility( "sprout", "plant_tree", -1, 0, nullptr ) == CreatureAbilityResult::OutOfBounds );
    SW_EXPECT_TRUE( town.useAbility( "sprout", "plant_tree", 9, 9, nullptr ) == CreatureAbilityResult::NoRule ); // 물 위
    SW_EXPECT_EQUAL( 2, town.countAbilityUsesLeft( "sprout", "plant_tree" ) );                                   // 실패는 횟수를 쓰지 않는다

    SW_EXPECT_TRUE( town.useAbility( "sprout", "plant_tree", 7, 5, nullptr ) == CreatureAbilityResult::Ok );
    SW_EXPECT_TRUE( *town.findObject( 7, 5 ) == hashed_string( "tree" ) );
    SW_EXPECT_EQUAL( 2, town.countHabitats( "tall_grass" ) );
    SW_EXPECT_TRUE( town.useAbility( "sprout", "plant_tree", 4, 4, nullptr ) == CreatureAbilityResult::Ok );
    SW_EXPECT_TRUE( town.useAbility( "sprout", "plant_tree", 3, 4, nullptr ) == CreatureAbilityResult::NoUsesLeft );

    SW_EXPECT_TRUE( town.useAbility( "sprout", "rock_smash", 0, 8, &inventory ) == CreatureAbilityResult::Ok );
    SW_EXPECT_TRUE( town.findObject( 0, 8 )->empty() );
    SW_EXPECT_EQUAL( 2, inventory.getItemCount( "stone" ) );

    town.advanceDay();
    SW_EXPECT_EQUAL( 2, town.countAbilityUsesLeft( "sprout", "plant_tree" ) );
    SW_EXPECT_TRUE( town.useAbility( "sprout", "plant_tree", 3, 4, nullptr ) == CreatureAbilityResult::Ok );
}

/**
 * @brief [CreatureLifeTest] 집 — 빈 칸에만 짓고, 집 없는 생물은 서식지에서 가장 가까운 빈 집으로. 매력도는 서식지 종류 · 생물 수 · 호감도로 단계가 오른다
 */
SW_TEST_CASE( CreatureLifeTest, HousesAndTownAppealTiers )
{
    CreatureLifeTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    CreatureTown town;
    town.initialize( &world._catalog, &world._reputation, nullptr, 12, 12, CreatureTownSettings{} );
    SW_EXPECT_TRUE( town.getAppealTierName() == hashed_string( "Camp" ) );

    placeTallGrass( town, 0, 0, true );
    SW_EXPECT_NEAR_EQUAL( 10.0f, town.computeAppealScore(), 1.0e-4f );
    (void)town.attractVisitorsAt( 0, 9, DayPhase::Day, "sunny" );
    (void)town.setObject( 10, 10, "water" );
    (void)town.setObject( 11, 10, "water" );
    SW_EXPECT_NEAR_EQUAL( 25.0f, town.computeAppealScore(), 1.0e-4f );
    SW_EXPECT_TRUE( town.getAppealTierName() == hashed_string( "Camp" ) );
    vector<CreatureTownEvent> listEvent;
    town.drainEvents( listEvent );
    listEvent.clear();
    (void)town.attractVisitorsAt( 0, 21, DayPhase::Night, "rain" ); // frog 는 늘 — newt(35 %)는 이 시의 해시가 빗나간다
    SW_EXPECT_TRUE( town.findCreature( "newt" ) == nullptr );
    SW_EXPECT_NEAR_EQUAL( 30.0f, town.computeAppealScore(), 1.0e-4f );
    SW_EXPECT_TRUE( town.getAppealTierName() == hashed_string( "Village" ) );
    town.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countTownEvents( listEvent, CreatureTownEvent::Kind::AppealTierChanged ) );
    SW_EXPECT_TRUE( town.talkTo( "sprout" ) == CreatureInteractResult::Ok );
    SW_EXPECT_NEAR_EQUAL( 30.25f, town.computeAppealScore(), 1.0e-4f ); // 호감도 평균 2.5 × 0.1

    SW_EXPECT_EQUAL( -1, town.placeHouse( 0, 0, 1 ) ); // 풀 위
    const int32 nearHouse = town.placeHouse( 1, 3, 1 );
    const int32 farHouse  = town.placeHouse( 6, 6, 2 );
    SW_ASSERT_TRUE( nearHouse >= 0 && farHouse >= 0 );
    SW_EXPECT_TRUE( *town.findObject( 1, 3 ) == hashed_string( "house" ) );
    SW_EXPECT_EQUAL( 2, town.assignHomeless() );
    SW_EXPECT_EQUAL( nearHouse, town.findCreature( "sprout" )->_house ); // 풀숲 옆
    SW_EXPECT_EQUAL( farHouse, town.findCreature( "frog" )->_house );    // 가까운 집은 찼다
    SW_EXPECT_TRUE( town.assignHouse( "frog", nearHouse ) == CreatureHouseResult::HouseFull );
    SW_EXPECT_TRUE( town.assignHouse( "frog", 9 ) == CreatureHouseResult::UnknownHouse );
    SW_EXPECT_TRUE( town.assignHouse( "sprout", farHouse ) == CreatureHouseResult::Ok );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( town.getHouses()[static_cast<size_t>( nearHouse )]._listResident.size() ) );
    SW_EXPECT_EQUAL( 0, town.assignHomeless() );
}
