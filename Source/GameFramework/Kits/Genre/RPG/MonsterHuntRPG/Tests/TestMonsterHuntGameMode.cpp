// MonsterHuntGameModeTest — 몬스터 사냥 디렉터: 명령은 판이 열린 동안만 받고, 씬 데이터가 비었으면 판을 열지 않으며, 흥정 → 단서 → 사냥(표식 · 오일 · 공격) → 승리와 상태 왕복(nogpu).
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

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Component/MonsterHuntGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Presentation/Tests/RecordingSoundPlayer.h"

using namespace sw;

namespace
{
    // 괴물 HP 90: 표식 15 → 75, 오일 공격 20 × 1.5 = 30 두 번 → 15, 세 번째에 쓰러진다. 괴물은 제자리(속도 0)에서 2 초마다 친다.
    constexpr const utf8* kHuntCatalogXML = R"(<MonsterHuntCatalog>
  <Alchemy maxToxicity="100" toxicityDecay="0" alcohol="spirit"/>
  <Combat stamina="100" regen="0" delay="1" fast="0" strong="10" dodge="5" roll="10" adrenalineMax="3" adrenalinePerHit="0.1" adrenalineLoss="1" adrenalineBonus="0.1"/>
  <Monster id="howler" category="Beast" elements="Beast" maxKnowledge="3" readLevel="3" killsPerLevel="1" killCap="2" investigateLevel="1"><Weakness kind="Oil" id="beast_oil" knowledge="1"/></Monster>
  <Item id="beast_oil" kind="Oil" element="BeastOil" hits="6" charges="1"/>
  <Item id="tonic" kind="Potion" toxicity="20" duration="10" charges="1"/>
  <Sign id="gust" element="Gust" cost="30" power="15" altSkill="twin" altCost="40" altPowerScale="2"/>
  <SlotGroup level="1" slots="1"/>
  <SlotGroup level="5" slots="1"/>
  <SkillColor skill="twin" color="Blue"/>
  <Mutagen id="gland" color="Blue" stat="signIntensity" value="20" matchValue="10"/>
  <Contract id="ferry" reward="100" limit="1.5" angerMax="1" angerScale="1" angerPerRound="0.1">
    <Step id="tracks"><Clue id="net" position="1,0,0" radius="2" order="0"/></Step>
    <Step id="lair"><Clue id="bones" position="1,0,0" radius="2" order="0"/></Step>
  </Contract>
  <Contract id="sworn" quest="oath" reward="100"><Step id="tracks"><Clue id="net" position="1,0,0" radius="2" order="0"/></Step></Contract>
</MonsterHuntCatalog>)";

    constexpr const utf8* kHuntChartXML =
        R"(<ElementChart><Element id="Beast"/><Element id="BeastOil"/><Element id="Gust"/><Rule attack="BeastOil" defend="Beast" multiplier="1.5"/></ElementChart>)";

    constexpr const utf8* kHuntRecipeXML = R"(<RecipeCatalog><Recipe id="beast_oil"><In item="fat" count="1"/><Out item="beast_oil"/></Recipe></RecipeCatalog>)";

    constexpr const utf8* kHuntSkillTreeXML = R"(<SkillTreeCatalog><Tree id="hunter">
  <Skill id="twin"/>
  <Skill id="edge" requires="twin"><Stats attackPower="50"/></Skill>
</Tree></SkillTreeCatalog>)";

    constexpr float32 kHuntFrame = 0.125f;

    /** @brief 카탈로그를 건 게임 범위와 씬 데이터를 읽은 디렉터입니다. 상성표는 임시 도메인의 파일이다. 테스트 본문이 범위를 `ScopedGameScope` 로 겁니다. */
    struct MonsterHuntGameModeFixture
    {
        MonsterHuntCatalog            _catalog;
        RecipeCatalog                 _recipes;
        ItemCatalog                   _items;
        RecordingSoundPlayer          _soundPlayer{};
        SubsystemScope                _scope;
        GameObjectManager             _manager;
        MonsterHuntGameModeComponent* _pGameMode{ nullptr };

        [[nodiscard]] bool initialize( const test::ScopedScratchDomain& domain )
        {
            (void)domain.makeDiskFolder( "marsh" ); // 폴더만 만든다
            if ( FileUtil::writeTextFile( domain.makeDiskPath( "marsh/oils.elementchart.xml" ), kHuntChartXML ) == false )
                return false;
            if ( _recipes.loadFromXMLText( kHuntRecipeXML, "MonsterHuntGameModeTest" ) == false || FileUtil::writeTextFile( domain.makeDiskPath( "marsh/hunter.skilltrees.xml" ), kHuntSkillTreeXML ) == false || _scope.bind<ISoundPlayer>( &_soundPlayer ) == false || _scope.bind<const RecipeCatalog>( &_recipes ) == false ||
                 _scope.bind<const ItemCatalog>( &_items ) == false )
                return false;
            if ( _catalog.loadFromXMLText( kHuntCatalogXML, "MonsterHuntGameModeTest" ) == false || _scope.bind<const MonsterHuntCatalog>( &_catalog ) == false )
                return false;
            StringBuilder<constant::buffer::kMax2048> sceneText;
            sceneText.appendFormat( "<MonsterHuntGameModeComponent _contract=\"ferry\" _monster=\"howler\" _askStep=\"30\" _elementChart=\"%#\" _oil=\"beast_oil\" _potion=\"tonic\" _sign=\"gust\" "
                                    "_startPosition=\"0,0\" _spawnPosition=\"1,0\" _playerMaxHP=\"40\" _fastDamage=\"20\" _attackReach=\"2\" _signReach=\"5\" _attackCooldownSeconds=\"0.25\" "
                                    "_dodgeSeconds=\"0.5\" _monsterMaxHP=\"90\" _monsterDamage=\"15\" _monsterReach=\"1.5\" _monsterSpeed=\"0\" _monsterAttackSeconds=\"2\" "
                                    "_skillTreeCatalog=\"%#\" _skillTree=\"hunter\" _skillPoints=\"2\" _attackStat=\"attackPower\" _brewRecipe=\"beast_oil\"><_listBrewItem><item>fat</item></_listBrewItem><_listMutagen><item>gland</item></_listMutagen></MonsterHuntGameModeComponent>",
                                    test::ScopedScratchDomain::makeID( "marsh/oils.elementchart.xml" ).str(), test::ScopedScratchDomain::makeID( "marsh/hunter.skilltrees.xml" ).str() );
            _pGameMode = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<MonsterHuntGameModeComponent>();
            return _pGameMode != nullptr && XMLSerializer::deserialize( _pGameMode, *MonsterHuntGameModeComponent::StaticType(), sceneText.view() );
        }

        /** @brief 판을 열고 빈 프레임 하나를 돌립니다(첫 프레임은 틱 그룹을 옮기는 일이 든다). */
        void beginPlay()
        {
            _manager.beginPlay();
            _manager.update( kHuntFrame );
        }

        void update( int32 frameCount )
        {
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                _manager.update( kHuntFrame );
            }
        }

        /** @brief 보상을 올려 흥정을 끝내고 두 단계의 단서를 찾아 사냥을 엽니다. */
        [[nodiscard]] bool beginHunt() { return _pGameMode->submitRaise() && _pGameMode->submitInvestigate() && _pGameMode->submitInvestigate(); }
    };
} // namespace

/** @brief [MonsterHuntGameModeTest] 판이 열리기 전의 명령은 모두 거절한다 — 클라이언트가 먼저 돌아도 판을 바꾸지 않는다 */
SW_TEST_CASE( MonsterHuntGameModeTest, CommandsBeforeTheContractOpensAreRefused )
{
    GameObjectManager             manager;
    MonsterHuntGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<MonsterHuntGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->submitRaise() );
    SW_EXPECT_FALSE( pGameMode->submitAccept() );
    SW_EXPECT_FALSE( pGameMode->submitMove( float2{ 1.0f, 0.0f } ) );
    SW_EXPECT_FALSE( pGameMode->submitInvestigate() );
    SW_EXPECT_FALSE( pGameMode->submitAttack( false ) );
    SW_EXPECT_FALSE( pGameMode->submitDodge() );
    SW_EXPECT_FALSE( pGameMode->submitSign( false ) );
    SW_EXPECT_FALSE( pGameMode->submitOil() );
    SW_EXPECT_FALSE( pGameMode->submitPotion() );
    SW_EXPECT_FALSE( pGameMode->submitBrew() );
    SW_EXPECT_FALSE( pGameMode->submitLearnSkill() );
    SW_EXPECT_FALSE( pGameMode->submitMutagen() );
}

/** @brief [MonsterHuntGameModeTest] 씬이 상성표를 적지 않았으면 오류를 남기고 판을 열지 않는다 — C++ 기본값으로 열지 않는다 */
SW_TEST_CASE( MonsterHuntGameModeTest, ContractWithoutElementChartDoesNotOpen )
{
    SW_EXPECT_LOG( "GameMode.RequiredAssetMissing" );
    MonsterHuntCatalog catalog;
    RecipeCatalog      recipes;
    ItemCatalog        items;
    SubsystemScope     scope;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kHuntCatalogXML, "MonsterHuntGameModeTest" ) );
    SW_REQUIRE_TRUE( scope.bind<const MonsterHuntCatalog>( &catalog ) && scope.bind<const RecipeCatalog>( &recipes ) && scope.bind<const ItemCatalog>( &items ) );
    game::ScopedGameScope         scopedScope( scope );
    GameObjectManager             manager;
    MonsterHuntGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<MonsterHuntGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    manager.beginPlay();
    SW_EXPECT_FALSE( pGameMode->isStarted() );
    manager.endPlay();
}

/** @brief [MonsterHuntGameModeTest] 퀘스트를 적은 계약인데 씬이 퀘스트 카탈로그를 주지 않았으면 오류를 남기고 판을 열지 않는다 — 퀘스트 없이 대신 돌지 않는다 */
SW_TEST_CASE( MonsterHuntGameModeTest, ContractQuestWithoutQuestCatalogDoesNotOpen )
{
    SW_EXPECT_LOG( "GameMode.RequiredAssetMissing" );
    const test::ScopedScratchDomain domain;
    MonsterHuntGameModeFixture      fixture;
    SW_REQUIRE_TRUE( fixture.initialize( domain ) );
    game::ScopedGameScope scopedScope( fixture._scope );
    SW_REQUIRE_TRUE( XMLSerializer::deserialize( fixture._pGameMode, *MonsterHuntGameModeComponent::StaticType(), R"(<MonsterHuntGameModeComponent _contract="sworn"/>)" ) );
    fixture._manager.beginPlay();
    SW_EXPECT_FALSE( fixture._pGameMode->isStarted() );
    fixture._manager.endPlay();
}

/** @brief [MonsterHuntGameModeTest] 흥정으로 올린 보상, 단서가 올린 지식, 표식 · 오일 공격으로 쓰러뜨린 괴물이 승리로 이어진다 */
SW_TEST_CASE( MonsterHuntGameModeTest, HaggleInvestigateAndHuntToVictory )
{
    const test::ScopedScratchDomain domain;
    MonsterHuntGameModeFixture      fixture;
    SW_REQUIRE_TRUE( fixture.initialize( domain ) );
    game::ScopedGameScope scopedScope( fixture._scope );
    fixture.beginPlay();
    SW_REQUIRE_TRUE( fixture._pGameMode->isStarted() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitInvestigate() ); // 흥정이 먼저다
    SW_EXPECT_EQUAL( 100, fixture._pGameMode->getReward() );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitRaise() );
    SW_EXPECT_EQUAL( 130, fixture._pGameMode->getReward() );
    SW_REQUIRE_EQUAL( 1, fixture._pGameMode->getPhase() );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitInvestigate() );
    SW_EXPECT_FALSE( fixture._pGameMode->isWeaknessKnown() );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitInvestigate() );
    SW_REQUIRE_EQUAL( 2, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 2, fixture._pGameMode->getFoundClueCount() );
    SW_EXPECT_EQUAL( 1, fixture._pGameMode->getKnowledge() );
    SW_EXPECT_TRUE( fixture._pGameMode->isWeaknessKnown() );

    SW_REQUIRE_TRUE( fixture._pGameMode->submitSign( false ) );
    SW_EXPECT_EQUAL( 75, fixture._pGameMode->getMonsterHP() );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitOil() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitOil() ); // 한 번뿐이다
    SW_REQUIRE_TRUE( fixture._pGameMode->submitAttack( false ) );
    SW_EXPECT_EQUAL( 45, fixture._pGameMode->getMonsterHP() );
    SW_EXPECT_EQUAL( 5, fixture._pGameMode->getOilHits() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitAttack( false ) ); // 쿨다운
    fixture.update( 2 );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitAttack( false ) );
    fixture.update( 2 );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitAttack( false ) );
    SW_EXPECT_EQUAL( 3, fixture._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 0, fixture._pGameMode->getMonsterHP() );
    SW_EXPECT_EQUAL( 40, fixture._pGameMode->getPlayerHP() ); // 괴물의 첫 공격(2 초) 전이다
    SW_EXPECT_EQUAL( 2, fixture._soundPlayer.countPosted( "MonsterHuntClue" ) );
    SW_EXPECT_EQUAL( 2, fixture._soundPlayer.countPosted( "MonsterHuntStep" ) );
    SW_EXPECT_EQUAL( 1, fixture._soundPlayer.countPosted( "MonsterHuntSolved" ) );
    SW_EXPECT_TRUE( fixture._soundPlayer.countPosted( "MonsterHuntKnowledge" ) >= 1 );
    fixture._manager.endPlay();
}

/** @brief [MonsterHuntGameModeTest] 괴물은 간격마다 치고, 회피 중에는 맞지 않으며, 체력이 다하면 패배다 */
SW_TEST_CASE( MonsterHuntGameModeTest, MonsterStrikesUntilTheHunterFallsUnlessDodging )
{
    const test::ScopedScratchDomain domain;
    MonsterHuntGameModeFixture      fixture;
    SW_REQUIRE_TRUE( fixture.initialize( domain ) );
    game::ScopedGameScope scopedScope( fixture._scope );
    fixture.beginPlay();
    SW_REQUIRE_TRUE( fixture.beginHunt() );
    fixture.update( 15 );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitDodge() );
    fixture.update( 1 ); // 2 초 — 회피 중이라 빗나간다
    SW_EXPECT_EQUAL( 40, fixture._pGameMode->getPlayerHP() );
    fixture.update( 16 );
    SW_EXPECT_EQUAL( 25, fixture._pGameMode->getPlayerHP() );
    fixture.update( 16 );
    SW_EXPECT_EQUAL( 10, fixture._pGameMode->getPlayerHP() );
    fixture.update( 16 );
    SW_EXPECT_EQUAL( 4, fixture._pGameMode->getPhase() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitAttack( false ) );
    fixture._manager.endPlay();
}

/** @brief [MonsterHuntGameModeTest] 사냥 중의 상태를 새 디렉터에 복원하면 같은 체력 · 오일 · 보상으로 이어지고, 잘린 바이트는 거절해 판이 그대로다 */
SW_TEST_CASE( MonsterHuntGameModeTest, HuntStateSurvivesARoundTrip )
{
    const test::ScopedScratchDomain domain;
    MonsterHuntGameModeFixture      source;
    SW_REQUIRE_TRUE( source.initialize( domain ) );
    game::ScopedGameScope scopedScope( source._scope );
    source.beginPlay();
    SW_REQUIRE_TRUE( source.beginHunt() );
    SW_REQUIRE_TRUE( source._pGameMode->submitOil() );
    SW_REQUIRE_TRUE( source._pGameMode->submitPotion() );
    SW_REQUIRE_TRUE( source._pGameMode->submitAttack( false ) );
    Archive state;
    source._pGameMode->writeState( state );
    vector<uint8> bytes( state.getData(), state.getData() + state.getSize() );

    MonsterHuntGameModeFixture restored;
    SW_REQUIRE_TRUE( restored.initialize( domain ) );
    restored.beginPlay();
    vector<uint8> truncated( bytes.begin(), bytes.end() - 4 );
    restored._pGameMode->restoreState( std::move( truncated ) );
    SW_EXPECT_EQUAL( 0, restored._pGameMode->getPhase() );
    restored._pGameMode->restoreState( std::move( bytes ) );
    SW_REQUIRE_EQUAL( 2, restored._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 60, restored._pGameMode->getMonsterHP() );
    SW_EXPECT_EQUAL( 5, restored._pGameMode->getOilHits() );
    SW_EXPECT_EQUAL( 130, restored._pGameMode->getReward() );
    SW_EXPECT_EQUAL( 20.0f, restored._pGameMode->getToxicity() );
    SW_EXPECT_FALSE( restored._pGameMode->submitPotion() );        // 쓴 횟수가 이어진다
    SW_EXPECT_FALSE( restored._pGameMode->submitAttack( false ) ); // 쿨다운이 이어진다
    restored._manager.endPlay();
    source._manager.endPlay();
}

/** @brief [MonsterHuntGameModeTest] 배운 스킬이 대체 표식을 열고 무기 피해를 올리며, 색이 맞는 변이원이 표식을 세게 한다 — 배운 것은 저장을 거쳐 이어진다 */
SW_TEST_CASE( MonsterHuntGameModeTest, SkillsAndMutagensStrengthenTheHunterAndSurviveARoundTrip )
{
    const test::ScopedScratchDomain domain;
    MonsterHuntGameModeFixture      source;
    SW_REQUIRE_TRUE( source.initialize( domain ) );
    game::ScopedGameScope scopedScope( source._scope );
    source.beginPlay();
    SW_EXPECT_FALSE( source._pGameMode->submitLearnSkill() ); // 의뢰를 받은 뒤다
    SW_REQUIRE_TRUE( source.beginHunt() );
    SW_EXPECT_FALSE( source._pGameMode->submitSign( true ) ); // 아직 배우지 않았다
    SW_REQUIRE_TRUE( source._pGameMode->submitMutagen() );
    SW_EXPECT_EQUAL( 20, source._pGameMode->getSignIntensity() ); // 색이 맞는 스킬이 없다
    SW_EXPECT_FALSE( source._pGameMode->submitMutagen() );        // 하나뿐이고 둘째 그룹은 레벨 5 다
    SW_REQUIRE_TRUE( source._pGameMode->submitLearnSkill() );
    SW_EXPECT_EQUAL( 30, source._pGameMode->getSignIntensity() );
    SW_REQUIRE_TRUE( source._pGameMode->submitSign( true ) ); // 15 × 1.3 × 2
    SW_EXPECT_EQUAL( 51, source._pGameMode->getMonsterHP() );
    SW_REQUIRE_TRUE( source._pGameMode->submitLearnSkill() );
    SW_EXPECT_FALSE( source._pGameMode->submitLearnSkill() );    // 점수가 다했다
    SW_REQUIRE_TRUE( source._pGameMode->submitAttack( false ) ); // 20 × 1.5
    SW_EXPECT_EQUAL( 21, source._pGameMode->getMonsterHP() );
    Archive state;
    source._pGameMode->writeState( state );

    MonsterHuntGameModeFixture restored;
    SW_REQUIRE_TRUE( restored.initialize( domain ) );
    restored.beginPlay();
    restored._pGameMode->restoreState( vector<uint8>( state.getData(), state.getData() + state.getSize() ) );
    SW_REQUIRE_EQUAL( 2, restored._pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 2, restored._pGameMode->getLearnedSkillCount() );
    SW_EXPECT_EQUAL( 30, restored._pGameMode->getSignIntensity() );
    SW_EXPECT_FALSE( restored._pGameMode->submitLearnSkill() );
    restored._manager.endPlay();
    source._manager.endPlay();
}

/** @brief [MonsterHuntGameModeTest] 재료로 만든 오일은 사용 횟수가 다시 차 한 번 더 바를 수 있고, 재료가 다하면 만들지 못한다 */
SW_TEST_CASE( MonsterHuntGameModeTest, BrewRefillsTheOil )
{
    const test::ScopedScratchDomain domain;
    MonsterHuntGameModeFixture      fixture;
    SW_REQUIRE_TRUE( fixture.initialize( domain ) );
    game::ScopedGameScope scopedScope( fixture._scope );
    fixture.beginPlay();
    SW_EXPECT_FALSE( fixture._pGameMode->submitBrew() ); // 의뢰를 받은 뒤다
    SW_REQUIRE_TRUE( fixture.beginHunt() );
    SW_REQUIRE_TRUE( fixture._pGameMode->submitOil() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitOil() ); // 횟수가 다했다
    SW_REQUIRE_TRUE( fixture._pGameMode->submitBrew() );
    SW_EXPECT_FALSE( fixture._pGameMode->submitBrew() ); // 재료가 다했다
    SW_EXPECT_TRUE( fixture._pGameMode->submitOil() );
    fixture._manager.endPlay();
}
