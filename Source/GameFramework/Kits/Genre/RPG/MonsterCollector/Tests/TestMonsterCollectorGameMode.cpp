// MonsterCollectorGameModeTest — 몬스터 수집 디렉터: 명령은 판이 열린 동안만 받고, 씬 데이터가 비었으면 판을 열지 않으며, 걸음 조우 → 기술 승리 · 볼 포획 → 목표 승리와 상태 왕복(nogpu).
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

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Component/MonsterCollectorGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/Tests/RecordingSoundPlayer.h"

using namespace sw;

namespace
{
    // 시작 개체는 늘 먼저 움직이고(스피드) 야생(레벨 3)을 한 번에 쓰러뜨린다. 야생의 포획률 255 × 볼 3 은 반드시 잡는다.
    constexpr const utf8* kCollectorGameModeXML = R"(<MonsterCollectorCatalog>
  <Move id="leafslap" name="Leaf Slap" type="Moss" power="40" accuracy="0" pp="30"/>
  <Move id="nudge" name="Nudge" type="Spark" power="10" accuracy="0" pp="30"/>
  <Species id="fernling" name="Fernling" types="Moss" stats="50 90 50 50 50 100"><Learn level="1" move="leafslap"/></Species>
  <Species id="glintbug" name="Glintbug" types="Spark" stats="40 30 30 30 30 30" catchRate="255" baseExp="56"><Learn level="1" move="nudge"/></Species>
  <Encounter id="glade" area="glade"><Slot species="glintbug" min="3" max="3" weight="1"/></Encounter>
</MonsterCollectorCatalog>)";

    constexpr const utf8* kCollectorChartXML = R"(<ElementChart><Element id="Moss"/><Element id="Spark"/></ElementChart>)";

    constexpr float32 kCollectorFrame = 0.125f; ///< 한 프레임에 0.5 m — 걸음(1 m) 하나가 두 프레임이고 float 오차가 없다

    /** @brief 카탈로그를 건 게임 범위와 씬 데이터를 읽은 디렉터입니다. 상성표는 임시 도메인의 파일이다. 테스트 본문이 범위를 `ScopedGameScope` 로 겁니다. */
    struct MonsterCollectorGameModeFixture
    {
        MonsterCollectorCatalog            _catalog;
        RecordingSoundPlayer               _soundPlayer{};
        SubsystemScope                     _scope;
        GameObjectManager                  _manager;
        MonsterCollectorGameModeComponent* _pGameMode{ nullptr };

        [[nodiscard]] bool initialize( const test::ScopedScratchDomain& domain )
        {
            (void)domain.makeDiskFolder( "glade" ); // 폴더만 만든다
            if ( FileUtil::writeTextFile( domain.makeDiskPath( "glade/types.elementchart.xml" ), kCollectorChartXML ) == false )
                return false;
            if ( _catalog.loadFromXMLText( kCollectorGameModeXML, "MonsterCollectorGameModeTest" ) == false || _scope.bind<ISoundPlayer>( &_soundPlayer ) == false || _scope.bind<const MonsterCollectorCatalog>( &_catalog ) == false )
                return false;
            StringBuilder<constant::buffer::kMax1024> sceneText;
            sceneText.appendFormat( "<MonsterCollectorGameModeComponent _elementChart=\"%#\" _starterLevel=\"10\" _area=\"glade\" _encounterRate=\"1\" _graceSteps=\"3\" _ballCount=\"3\" _ballMultiplier=\"3\" "
                                    "_captureGoal=\"2\" _captureReward=\"10\" _startPosition=\"0,0\" _fieldMin=\"0,-1\" _fieldMax=\"40,1\" _moveSpeed=\"4\" _stepLength=\"1\">"
                                    "<_listStarter><item>fernling</item></_listStarter></MonsterCollectorGameModeComponent>",
                                    test::ScopedScratchDomain::makeID( "glade/types.elementchart.xml" ).str() );
            _pGameMode = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<MonsterCollectorGameModeComponent>();
            return _pGameMode != nullptr && XMLSerializer::deserialize( _pGameMode, *MonsterCollectorGameModeComponent::StaticType(), sceneText.view() );
        }

        /** @brief 판을 열고 빈 프레임 하나를 돌립니다(첫 프레임은 틱 그룹을 옮기는 일이 든다). */
        void beginPlay()
        {
            _manager.beginPlay();
            _manager.update( kCollectorFrame );
        }

        /** @brief 다음 조우까지 동쪽으로 4 m 걷습니다(유예 세 걸음 뒤의 걸음). */
        void walkToEncounter()
        {
            for ( int32 frame = 0; frame < 8; ++frame )
            {
                (void)_pGameMode->submitMove( float2{ 1.0f, 0.0f } );
                _manager.update( kCollectorFrame );
            }
        }
    };
} // namespace

/** @brief [MonsterCollectorGameModeTest] 판이 열리기 전의 명령은 모두 거절한다 — 클라이언트가 먼저 돌아도 판을 바꾸지 않는다 */
SW_TEST_CASE( MonsterCollectorGameModeTest, CommandsBeforeTheOutingOpensAreRefused )
{
    GameObjectManager                  manager;
    MonsterCollectorGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<MonsterCollectorGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->submitMove( float2{ 1.0f, 0.0f } ) );
    SW_EXPECT_FALSE( pGameMode->submitAttack( 0 ) );
    SW_EXPECT_FALSE( pGameMode->submitBall() );
    SW_EXPECT_FALSE( pGameMode->submitSwitch( 0 ) );
    SW_EXPECT_FALSE( pGameMode->submitRun() );
    SW_EXPECT_FALSE( pGameMode->submitRest() );
    SW_EXPECT_NULL( pGameMode->findLead() );
    SW_EXPECT_EQUAL( 0, pGameMode->getFoeHP() );
}

/** @brief [MonsterCollectorGameModeTest] 씬이 상성표를 적지 않았으면 오류를 남기고 판을 열지 않는다 — C++ 기본값으로 열지 않는다 */
SW_TEST_CASE( MonsterCollectorGameModeTest, OutingWithoutElementChartDoesNotOpen )
{
    SW_EXPECT_LOG( "GameMode.RequiredAssetMissing" );
    MonsterCollectorCatalog catalog;
    SubsystemScope          scope;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kCollectorGameModeXML, "MonsterCollectorGameModeTest" ) );
    SW_REQUIRE_TRUE( scope.bind<const MonsterCollectorCatalog>( &catalog ) );
    game::ScopedGameScope              scopedScope( scope );
    GameObjectManager                  manager;
    MonsterCollectorGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<MonsterCollectorGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_EXPECT_FALSE( pGameMode->isStarted() );
    manager.endPlay();
}

/** @brief [MonsterCollectorGameModeTest] 걸음 조우가 전투를 열고, 이기면 경험치가 파티로 돌아오며, 볼로 정한 수를 잡으면 승리한다 */
SW_TEST_CASE( MonsterCollectorGameModeTest, WinsAndCapturesLeadToVictory )
{
    const test::ScopedScratchDomain domain;
    MonsterCollectorGameModeFixture fixture;
    SW_REQUIRE_TRUE( fixture.initialize( domain ) );
    game::ScopedGameScope scopedScope( fixture._scope );
    fixture.beginPlay();
    SW_REQUIRE_TRUE( fixture._pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 1000, fixture._pGameMode->getLeaderXP() ); // 레벨 10 의 총 경험치(보통 10³)
    SW_EXPECT_FALSE( fixture._pGameMode->submitBall() );        // 걷는 동안은 전투 명령이 없다

    fixture.walkToEncounter();
    SW_REQUIRE_EQUAL( 1, fixture._pGameMode->getPhase() );
    SW_EXPECT_TRUE( fixture._pGameMode->getFoeHP() > 0 );
    SW_EXPECT_FALSE( fixture._pGameMode->submitAttack( 1 ) ); // 빈 슬롯
    SW_REQUIRE_TRUE( fixture._pGameMode->submitAttack( 0 ) );
    SW_EXPECT_EQUAL( 0, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 1024, fixture._pGameMode->getLeaderXP() ); // 56 × 3 / 7 = 24

    fixture.walkToEncounter();
    SW_REQUIRE_TRUE( fixture._pGameMode->submitBall() );
    SW_EXPECT_EQUAL( 0, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 1, fixture._pGameMode->getCaptureCount() );
    SW_EXPECT_EQUAL( 2, fixture._pGameMode->getPartySize() );
    SW_EXPECT_EQUAL( 2, fixture._pGameMode->getRemainingBallCount() );

    fixture.walkToEncounter();
    SW_REQUIRE_TRUE( fixture._pGameMode->submitSwitch( 1 ) ); // 잡은 개체로 바꾼다 — 라운드를 쓴다
    SW_EXPECT_EQUAL( 1, fixture._pGameMode->getBattle().getActiveIndex( MonsterBattle::kPlayerSide ) );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitBall() );
    SW_EXPECT_EQUAL( 2, fixture._pGameMode->getPhase() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitMove( float2{ 1.0f, 0.0f } ) );
    fixture._manager.endPlay();
}

/** @brief [MonsterCollectorGameModeTest] 전투 중의 상태를 새 디렉터에 복원하면 같은 상대 · 볼로 이어지고, 잘린 바이트는 거절해 판이 그대로다 */
SW_TEST_CASE( MonsterCollectorGameModeTest, BattleStateSurvivesARoundTrip )
{
    const test::ScopedScratchDomain domain;
    MonsterCollectorGameModeFixture source;
    SW_REQUIRE_TRUE( source.initialize( domain ) );
    game::ScopedGameScope scopedScope( source._scope );
    source.beginPlay();
    source.walkToEncounter();
    SW_REQUIRE_EQUAL( 1, source._pGameMode->getPhase() );
    Archive state;
    source._pGameMode->writeState( state );
    vector<uint8> bytes( state.getData(), state.getData() + state.getSize() );

    MonsterCollectorGameModeFixture restored;
    SW_REQUIRE_TRUE( restored.initialize( domain ) );
    restored.beginPlay();
    vector<uint8> truncated( bytes.begin(), bytes.end() - 4 );
    restored._pGameMode->restoreState( std::move( truncated ) );
    SW_EXPECT_EQUAL( 0, restored._pGameMode->getPhase() );
    restored._pGameMode->restoreState( std::move( bytes ) );
    SW_REQUIRE_EQUAL( 1, restored._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( source._pGameMode->getFoeHP(), restored._pGameMode->getFoeHP() );
    SW_EXPECT_EQUAL( source._pGameMode->getPlayerX(), restored._pGameMode->getPlayerX() );
    SW_REQUIRE_TRUE( restored._pGameMode->submitBall() );
    SW_EXPECT_EQUAL( 1, restored._pGameMode->getCaptureCount() );
    restored._manager.endPlay();
    source._manager.endPlay();
}
