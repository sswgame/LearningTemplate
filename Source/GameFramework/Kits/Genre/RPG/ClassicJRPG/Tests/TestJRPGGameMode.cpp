// JRPGGameModeTest — 클래식 JRPG 디렉터(타이밍 · 장비 · 전직 · 자동 플레이 포함): 명령은 판이 열린 동안만 받고, 씬 데이터가 비었으면 판을 열지 않으며, 걸음 조우 → 멤버마다 명령 → 승리 → 보스 → 승리와 상태 왕복(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/Component/JRPGGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

using namespace sw;

namespace
{
    // 영웅이 늘 먼저 움직이고(민첩) 풀숲의 적은 한 번, 보스(HP 100)는 두 라운드에 쓰러진다 — 물리 피해는 (공격 / 2) × 0.875..1.125 라 80 은 35..45, 60 은 26..33 이다.
    constexpr const utf8* kJRPGGameModeXML = R"(<JRPGCatalog>
  <ExperienceCurve base="1000" exponent="0" maxLevel="10"/>
  <Class id="guard" name="Guard" hp="100" mp="0" str="80" agi="50" vit="20"/>
  <Class id="lamp" name="Lamp" hp="80" mp="10" str="60" agi="40" vit="20"/>
  <Class id="sage" name="Sage" hp="70" mp="20" str="30" agi="36" vit="10" requires="tome"/>
  <Enemy id="bramble" name="Bramble" hp="10" str="4" agi="1" vit="0" exp="12" gold="3"/>
  <Enemy id="hollow" name="Hollow" hp="100" str="10" agi="1" vit="0" exp="50" gold="20" boss="true"/>
  <Area id="trail" rate="1" grace="3"><Group enemies="bramble,bramble" weight="1"/></Area>
</JRPGCatalog>)";

    constexpr const utf8* kJRPGSceneXML = R"(<JRPGGameModeComponent _area="trail" _startPosition="0,0" _goalPosition="8,0" _trailMin="0,-1" _trailMax="10,1" _moveSpeed="4" _stepLength="1" _startGold="5">
  <_listHero><item>guard</item><item>lamp</item></_listHero>
  <_listBoss><item>hollow</item></_listBoss>
</JRPGGameModeComponent>)";

    constexpr const utf8* kJRPGItemXML = R"(<ItemCatalog>
  <Item id="blade" slot="Weapon"><Stats attack="20"/></Item>
  <Item id="tome"/>
</ItemCatalog>)";

    // 타이밍 · 장비 · 전직을 쓰는 판 — 쉼터는 시작 자리다.
    constexpr const utf8* kJRPGFullSceneXML = R"(<JRPGGameModeComponent _area="trail" _startPosition="0,0" _goalPosition="8,0" _restPosition="0,0.5" _trailMin="0,-1" _trailMax="10,1" _moveSpeed="4" _stepLength="1" _startGold="5"
  _timingTarget="0.25" _timingDuration="0.5" _restClass="sage" _classChangeLevel="1">
  <_listHero><item>guard</item><item>lamp</item></_listHero>
  <_listBoss><item>hollow</item></_listBoss>
  <_listTimingWindow><TimingWindow _grade="Hit" early="0.1" late="0.1"/></_listTimingWindow>
  <_listStartItem><item>blade</item><item>tome</item></_listStartItem>
</JRPGGameModeComponent>)";

    constexpr float32 kJRPGFrame = 0.125f; ///< 한 프레임에 0.5 m — 걸음(1 m) 하나가 두 프레임이고 float 오차가 없다

    /** @brief 카탈로그를 건 게임 범위와 씬 데이터를 읽은 디렉터입니다. 테스트 본문이 범위를 `ScopedGameScope` 로 겁니다. */
    struct JRPGGameModeFixture
    {
        JRPGCatalog            _catalog;
        ItemCatalog            _itemCatalog;
        SubsystemScope         _scope;
        GameObjectManager      _manager;
        JRPGGameModeComponent* _pGameMode{ nullptr };

        [[nodiscard]] bool initialize( const utf8* pSceneXML = kJRPGSceneXML )
        {
            if ( _catalog.loadFromXMLText( kJRPGGameModeXML, "JRPGGameModeTest" ) == false || _scope.bind<const JRPGCatalog>( &_catalog ) == false )
                return false;
            if ( _itemCatalog.loadFromXMLText( kJRPGItemXML, "JRPGGameModeTest" ) == false || _scope.bind<const ItemCatalog>( &_itemCatalog ) == false )
                return false;
            _pGameMode = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<JRPGGameModeComponent>();
            return _pGameMode != nullptr && XMLSerializer::deserialize( _pGameMode, *JRPGGameModeComponent::StaticType(), pSceneXML );
        }

        /** @brief 판을 열고 빈 프레임 하나를 돌립니다(첫 프레임은 틱 그룹을 옮기는 일이 든다). */
        void beginPlay()
        {
            _manager.beginPlay();
            _manager.update( kJRPGFrame );
        }

        /** @brief 동쪽으로 걸으며 @p frameCount 프레임을 돌립니다. 전투가 열리면 걷기 명령은 거절된다. */
        void walkEast( int32 frameCount )
        {
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                (void)_pGameMode->submitMove( float2{ 1.0f, 0.0f } );
                _manager.update( kJRPGFrame );
            }
        }

        /** @brief 두 멤버가 모두 공격해 한 라운드를 돌립니다. */
        [[nodiscard]] bool attackWithAll() { return _pGameMode->submitCommand( JRPGCommand::makeAttack( 0 ) ) && _pGameMode->submitCommand( JRPGCommand::makeAttack( 0 ) ); }
    };
} // namespace

/** @brief [JRPGGameModeTest] 판이 열리기 전의 명령은 모두 거절한다 — 클라이언트가 먼저 돌아도 판을 바꾸지 않는다 */
SW_TEST_CASE( JRPGGameModeTest, CommandsBeforeTheTrailOpensAreRefused )
{
    GameObjectManager      manager;
    JRPGGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<JRPGGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->submitMove( float2{ 1.0f, 0.0f } ) );
    SW_EXPECT_FALSE( pGameMode->submitCommand( JRPGCommand::makeDefend() ) );
    SW_EXPECT_FALSE( pGameMode->submitFlee() );
    SW_EXPECT_FALSE( pGameMode->submitRest() );
    SW_EXPECT_FALSE( pGameMode->submitTiming() );
    SW_EXPECT_FALSE( pGameMode->submitEquip() );
    SW_EXPECT_FALSE( pGameMode->submitClassChange( 0 ) );
    SW_EXPECT_EQUAL( 0, pGameMode->getGold() );
    SW_EXPECT_EQUAL( -1, pGameMode->getCommandMember() );
}

/** @brief [JRPGGameModeTest] 카탈로그는 걸렸지만 씬이 영웅 · 보스 · 지역을 적지 않았으면 오류를 남기고 판을 열지 않는다 */
SW_TEST_CASE( JRPGGameModeTest, TrailWithoutSceneDataDoesNotOpen )
{
    SW_EXPECT_LOG( "JRPG.SetupIncomplete" );
    JRPGCatalog    catalog;
    SubsystemScope scope;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kJRPGGameModeXML, "JRPGGameModeTest" ) );
    SW_REQUIRE_TRUE( scope.bind<const JRPGCatalog>( &catalog ) );
    game::ScopedGameScope  scopedScope( scope );
    GameObjectManager      manager;
    JRPGGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<JRPGGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_EXPECT_FALSE( pGameMode->isStarted() );
    manager.endPlay();
}

/** @brief [JRPGGameModeTest] 유예 걸음 뒤의 걸음이 전투를 열고, 멤버마다 명령 하나 뒤에 라운드가 돌아 이기면 보상을 받고 다시 걷는다 */
SW_TEST_CASE( JRPGGameModeTest, StepEncounterRunsOneRoundPerPartyAndPaysRewards )
{
    JRPGGameModeFixture fixture;
    SW_REQUIRE_TRUE( fixture.initialize() );
    game::ScopedGameScope scopedScope( fixture._scope );
    fixture.beginPlay();
    SW_REQUIRE_TRUE( fixture._pGameMode->isStarted() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitCommand( JRPGCommand::makeAttack( 0 ) ) ); // 걷는 동안은 전투 명령이 없다
    fixture.walkEast( 6 );                                                                // 유예 세 걸음
    SW_EXPECT_EQUAL( 0, fixture._pGameMode->getPhase() );
    fixture.walkEast( 2 );
    SW_REQUIRE_EQUAL( 1, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 2, fixture._pGameMode->getEnemiesAlive() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitMove( float2{ 1.0f, 0.0f } ) );
    SW_EXPECT_EQUAL( 0, fixture._pGameMode->getCommandMember() );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitCommand( JRPGCommand::makeAttack( 0 ) ) );
    SW_EXPECT_EQUAL( 1, fixture._pGameMode->getCommandMember() );
    SW_EXPECT_EQUAL( 0, fixture._pGameMode->getBattleRound() ); // 라운드는 마지막 멤버의 명령이 돌린다
    SW_REQUIRE_TRUE( fixture._pGameMode->submitCommand( JRPGCommand::makeAttack( 1 ) ) );
    SW_EXPECT_EQUAL( 0, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 11, fixture._pGameMode->getGold() );      // 시작 5 + 3 × 2
    SW_EXPECT_EQUAL( 12, fixture._pGameMode->getLeaderXP() );  // 24 를 둘이 나눈다
    SW_EXPECT_EQUAL( 100, fixture._pGameMode->getLeaderHP() ); // 적은 움직이기 전에 쓰러졌다
    fixture._manager.endPlay();
}

/** @brief [JRPGGameModeTest] 길 끝에 닿으면 보스전이 열리고 도망칠 수 없으며, 이기면 승리로 끝난다 */
SW_TEST_CASE( JRPGGameModeTest, ReachingTheGoalOpensTheBossBattleToVictory )
{
    JRPGGameModeFixture fixture;
    SW_REQUIRE_TRUE( fixture.initialize() );
    game::ScopedGameScope scopedScope( fixture._scope );
    fixture.beginPlay();
    fixture.walkEast( 8 ); // 4 m — 첫 조우
    SW_REQUIRE_TRUE( fixture.attackWithAll() );
    fixture.walkEast( 6 ); // 7 m — 목표(8 m)의 반지름 안
    SW_REQUIRE_EQUAL( 1, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 1, fixture._pGameMode->getEnemiesAlive() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitFlee() );
    SW_REQUIRE_TRUE( fixture.attackWithAll() );
    SW_EXPECT_EQUAL( 1, fixture._pGameMode->getPhase() );
    SW_REQUIRE_TRUE( fixture.attackWithAll() );
    SW_EXPECT_EQUAL( 2, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 31, fixture._pGameMode->getGold() );
    fixture._manager.endPlay();
}

/** @brief [JRPGGameModeTest] 전투 중의 상태를 새 디렉터에 복원하면 같은 단계 · 멤버 · 적으로 이어지고, 잘린 바이트는 거절해 판이 그대로다 */
SW_TEST_CASE( JRPGGameModeTest, BattleStateSurvivesARoundTrip )
{
    JRPGGameModeFixture source;
    SW_REQUIRE_TRUE( source.initialize() );
    game::ScopedGameScope scopedScope( source._scope );
    source.beginPlay();
    source.walkEast( 8 );
    SW_REQUIRE_TRUE( source._pGameMode->submitCommand( JRPGCommand::makeAttack( 0 ) ) );
    Archive state;
    source._pGameMode->writeState( state );
    vector<uint8> bytes( state.getData(), state.getData() + state.getSize() );

    JRPGGameModeFixture restored;
    SW_REQUIRE_TRUE( restored.initialize() );
    restored.beginPlay();
    vector<uint8> truncated( bytes.begin(), bytes.end() - 4 );
    restored._pGameMode->restoreState( std::move( truncated ) );
    SW_EXPECT_EQUAL( 0, restored._pGameMode->getPhase() );
    restored._pGameMode->restoreState( std::move( bytes ) );
    SW_REQUIRE_EQUAL( 1, restored._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 1, restored._pGameMode->getCommandMember() );
    SW_EXPECT_EQUAL( 2, restored._pGameMode->getEnemiesAlive() );
    SW_EXPECT_EQUAL( source._pGameMode->getPlayerX(), restored._pGameMode->getPlayerX() );
    SW_REQUIRE_TRUE( restored._pGameMode->submitCommand( JRPGCommand::makeAttack( 1 ) ) );
    SW_EXPECT_EQUAL( 0, restored._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 11, restored._pGameMode->getGold() );
    restored._manager.endPlay();
    source._manager.endPlay();
}

/** @brief [JRPGGameModeTest] 창이 있는 판은 공격마다 · 막기에 한 번 누를 때를 기다리고, 누를 때에 누른 공격은 한 번 더 친다 — 놓친 누름은 세지 않는다 */
SW_TEST_CASE( JRPGGameModeTest, TimedPressStrikesTwiceAndAMissedPressDoesNot )
{
    JRPGGameModeFixture fixture;
    SW_REQUIRE_TRUE( fixture.initialize( kJRPGFullSceneXML ) );
    game::ScopedGameScope scopedScope( fixture._scope );
    fixture.beginPlay();
    fixture.walkEast( 8 );
    SW_REQUIRE_TRUE( fixture.attackWithAll() );
    SW_REQUIRE_EQUAL( 4, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 0, fixture._pGameMode->getTimingStep() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitCommand( JRPGCommand::makeDefend() ) ); // 타이밍 단계에는 명령이 없다
    for ( int32 frame = 0; frame < 12; ++frame )                                       // 누르지 않으면 0.5 초(네 프레임)씩 세 차례가 지난다
    {
        fixture._manager.update( kJRPGFrame );
    }
    SW_REQUIRE_EQUAL( 0, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 0, fixture._pGameMode->getTimedCount() );

    fixture.walkEast( 6 ); // 보스
    SW_REQUIRE_EQUAL( 1, fixture._pGameMode->getPhase() );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitCommand( JRPGCommand::makeAttack( 0 ) ) );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitCommand( JRPGCommand::makeDefend() ) );
    fixture._manager.update( kJRPGFrame );
    fixture._manager.update( kJRPGFrame ); // 0.25 초 — 누를 때
    SW_REQUIRE_TRUE( fixture._pGameMode->submitTiming() );
    SW_EXPECT_EQUAL( 2, fixture._pGameMode->getTimingStep() ); // 방어한 멤버는 건너뛰고 막기
    SW_REQUIRE_TRUE( fixture._pGameMode->submitTiming() );     // 너무 이르다 — 막지 못한다
    SW_EXPECT_EQUAL( 1, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 1, fixture._pGameMode->getBattleRound() );
    SW_EXPECT_EQUAL( 1, fixture._pGameMode->getTimedCount() );
    fixture._manager.endPlay();
}

/** @brief [JRPGGameModeTest] 가방의 검은 첫 멤버의 빈 무기 칸으로 가 공격력을 올리고, 쉼터에서는 책을 가진 멤버가 전직한다 — 낄 것이 없거나 쉼터 밖이면 거절한다 */
SW_TEST_CASE( JRPGGameModeTest, GearRaisesAttackAndTheInnChangesClass )
{
    JRPGGameModeFixture fixture;
    SW_REQUIRE_TRUE( fixture.initialize( kJRPGFullSceneXML ) );
    game::ScopedGameScope scopedScope( fixture._scope );
    fixture.beginPlay();
    SW_REQUIRE_TRUE( fixture._pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 80, fixture._pGameMode->getLeaderAttack() );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitEquip() );
    SW_EXPECT_EQUAL( 100, fixture._pGameMode->getLeaderAttack() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitEquip() ); // 남은 것은 책뿐이다
    SW_REQUIRE_TRUE( fixture._pGameMode->submitClassChange( 1 ) );
    SW_EXPECT_EQUAL( 1, fixture._pGameMode->getClassChangeCount() );
    SW_EXPECT_EQUAL( 30, fixture._pGameMode->getParty().getMember( 1 ).getStat( JRPGStat::Strength ) ); // 60 의 절반
    SW_EXPECT_FALSE( fixture._pGameMode->submitClassChange( 1 ) );                                      // 이미 그 직업이다
    fixture.walkEast( 4 );                                                                              // 2 m — 쉼터 밖
    SW_EXPECT_FALSE( fixture._pGameMode->submitClassChange( 0 ) );
    fixture._manager.endPlay();
}

/** @brief [JRPGGameModeTest] 낀 장비 · 타이밍 단계의 누름이 저장을 거쳐 이어진다 */
SW_TEST_CASE( JRPGGameModeTest, GearAndTimingSurviveARoundTrip )
{
    JRPGGameModeFixture source;
    SW_REQUIRE_TRUE( source.initialize( kJRPGFullSceneXML ) );
    game::ScopedGameScope scopedScope( source._scope );
    source.beginPlay();
    SW_REQUIRE_TRUE( source._pGameMode->submitEquip() );
    source.walkEast( 8 );
    SW_REQUIRE_TRUE( source.attackWithAll() );
    source._manager.update( kJRPGFrame );
    source._manager.update( kJRPGFrame );
    SW_REQUIRE_TRUE( source._pGameMode->submitTiming() );
    Archive state;
    source._pGameMode->writeState( state );

    JRPGGameModeFixture restored;
    SW_REQUIRE_TRUE( restored.initialize( kJRPGFullSceneXML ) );
    restored.beginPlay();
    restored._pGameMode->restoreState( vector<uint8>( state.getData(), state.getData() + state.getSize() ) );
    SW_REQUIRE_EQUAL( 4, restored._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 1, restored._pGameMode->getTimingStep() );
    SW_EXPECT_EQUAL( 100, restored._pGameMode->getLeaderAttack() );
    restored._manager.endPlay();
    source._manager.endPlay();
}

/** @brief [JRPGGameModeTest] 자동 플레이는 장비를 끼고 조우 · 타이밍 · 보스를 플레이어와 같은 명령으로 넘어 이긴다 */
SW_TEST_CASE( JRPGGameModeTest, AutoPlayWinsTheTrail )
{
    JRPGGameModeFixture fixture;
    SW_REQUIRE_TRUE( fixture.initialize( kJRPGFullSceneXML ) );
    game::ScopedGameScope scopedScope( fixture._scope );
    SW_REQUIRE_TRUE( XMLSerializer::deserialize( fixture._pGameMode, *JRPGGameModeComponent::StaticType(), R"(<JRPGGameModeComponent _bAutoPlay="true"/>)" ) );
    fixture.beginPlay();
    for ( int32 frame = 0; frame < 200 && fixture._pGameMode->getPhase() != 2; ++frame )
    {
        fixture._manager.update( kJRPGFrame );
    }
    SW_EXPECT_EQUAL( 2, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 100, fixture._pGameMode->getLeaderAttack() );
    SW_EXPECT_TRUE( fixture._pGameMode->getTimedCount() > 0 );
    SW_EXPECT_FALSE( fixture._pGameMode->isAcceptingInput() );
    fixture._manager.endPlay();
}
