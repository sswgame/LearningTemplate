// JRPGGameModeTest — 클래식 JRPG 디렉터: 명령은 판이 열린 동안만 받고, 씬 데이터가 비었으면 판을 열지 않으며, 걸음 조우 → 멤버마다 명령 → 승리 → 보스 → 승리와 상태 왕복(nogpu, 에셋 없음).
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
  <Enemy id="bramble" name="Bramble" hp="10" str="4" agi="1" vit="0" exp="12" gold="3"/>
  <Enemy id="hollow" name="Hollow" hp="100" str="10" agi="1" vit="0" exp="50" gold="20" boss="true"/>
  <Area id="trail" rate="1" grace="3"><Group enemies="bramble,bramble" weight="1"/></Area>
</JRPGCatalog>)";

    constexpr const utf8* kJRPGSceneXML = R"(<JRPGGameModeComponent _area="trail" _startPosition="0,0" _goalPosition="8,0" _trailMin="0,-1" _trailMax="10,1" _moveSpeed="4" _stepLength="1" _startGold="5">
  <_listHero><item>guard</item><item>lamp</item></_listHero>
  <_listBoss><item>hollow</item></_listBoss>
</JRPGGameModeComponent>)";

    constexpr float32 kJRPGFrame = 0.125f; ///< 한 프레임에 0.5 m — 걸음(1 m) 하나가 두 프레임이고 float 오차가 없다

    /** @brief 카탈로그를 건 게임 범위와 씬 데이터를 읽은 디렉터입니다. 테스트 본문이 범위를 `ScopedGameScope` 로 겁니다. */
    struct JRPGGameModeFixture
    {
        JRPGCatalog            _catalog;
        SubsystemScope         _scope;
        GameObjectManager      _manager;
        JRPGGameModeComponent* _pGameMode{ nullptr };

        [[nodiscard]] bool initialize()
        {
            if ( _catalog.loadFromXMLText( kJRPGGameModeXML, "JRPGGameModeTest" ) == false || _scope.bind<const JRPGCatalog>( &_catalog ) == false )
                return false;
            _pGameMode = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<JRPGGameModeComponent>();
            return _pGameMode != nullptr && XMLSerializer::deserialize( _pGameMode, *JRPGGameModeComponent::StaticType(), kJRPGSceneXML );
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
