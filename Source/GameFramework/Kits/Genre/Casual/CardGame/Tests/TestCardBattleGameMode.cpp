// CardBattleGameModeTest — 덱 전투 디렉터: 열기 전 명령 거절, 없는 덱 거절, 카드 · 턴 명령과 상태 왕복, 이긴 판의 상금 · 끝내기, 카탈로그의 덱 · 상대 읽기(nogpu).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Component/CardBattleGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"

using namespace sw;

namespace
{
    constexpr float32 kCardFrame = 1.0f / 60.0f;

    constexpr const utf8* kCardCatalogXML = R"(<CardBattleCatalog><Card id="strike" name="Strike" cost="1" effects="Damage:6"/>
        <Deck id="starter" cards="strike,strike,strike,strike,strike"/>
        <Enemy id="dummy" name="Dummy" hp="30" intents="Damage:5"/></CardBattleCatalog>)";

    /** @brief 카드 카탈로그를 건 게임 범위와 디렉터 하나입니다. */
    struct CardBattleGameModeTest
    {
        CardBattleCatalog                 _catalog{};
        SubsystemScope                    _scope{};
        unique_ptr<game::ScopedGameScope> _pScopeBinding{};
        GameObjectManager                 _manager{};
        CardBattleGameModeComponent*      _pGameMode{ nullptr };

        CardBattleGameModeTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kCardCatalogXML, "CardBattleGameModeTest" ) );
            SW_REQUIRE_TRUE( _scope.bind<const CardBattleCatalog>( &_catalog ) );
            _pScopeBinding = make_unique<game::ScopedGameScope>( _scope );
            _pGameMode     = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<CardBattleGameModeComponent>();
            SW_REQUIRE_NOT_NULL( _pGameMode );
            set( "_deckID", "starter" );
            set( "_enemyID", "dummy" );
            set( "_resultHoldSeconds", "0.5" );
            set( "_winReward", "30" );
        }

        void set( const utf8* pProperty, string_view text )
        {
            const PropertyInfo* const pInfo = CardBattleGameModeComponent::StaticType()->findPropertyInHierarchy( hashed_string( pProperty ) );
            SW_REQUIRE_NOT_NULL( pInfo );
            SW_REQUIRE_TRUE( SerializerUtil::applyPropertyText( *pInfo, MutableObjectAddress{ _pGameMode }, text, SerializeContext::getDefault( SerializePurpose::Authoring ) ) );
        }

        void advance( int32 frameCount )
        {
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                _manager.update( kCardFrame );
            }
        }

        vector<uint8> saveState() const
        {
            Archive saved;
            _pGameMode->writeState( saved );
            vector<uint8> bytes;
            saved.writeData( bytes );
            return bytes;
        }
    };
} // namespace

/** @brief [CardBattleGameModeTest] 판이 열리기 전의 카드 · 턴 명령은 거절한다 */
SW_TEST_CASE( CardBattleGameModeTest, CommandsBeforeTheBattleOpensAreRefused )
{
    GameObjectManager            manager;
    CardBattleGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<CardBattleGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->isAwaitingCommand() );
    SW_EXPECT_FALSE( pGameMode->playCard( 0 ) );
    SW_EXPECT_FALSE( pGameMode->endTurn() );
    SW_EXPECT_FALSE( pGameMode->isPlayable( 0 ) );
}

/** @brief [CardBattleGameModeTest] 카탈로그는 덱 · 상대를 읽고, 없는 덱이면 오류를 남기고 판을 열지 않는다 */
SW_TEST_CASE_F( CardBattleGameModeTest, UnknownDeckDoesNotOpen )
{
    const CardBattleEnemyDef* const pEnemy = _catalog.findEnemy( CatalogKey<CardBattleEnemyDef>( hashed_string( "dummy" ) ) );
    SW_REQUIRE_NOT_NULL( pEnemy );
    SW_EXPECT_EQUAL( 30, pEnemy->_hp );
    SW_EXPECT_EQUAL( size_t{ 1 }, pEnemy->_listIntent.size() );
    SW_EXPECT_LOG( "CardBattle.DeckOrEnemyMissing" );
    set( "_deckID", "nowhere" );
    _manager.beginPlay();
    SW_EXPECT_FALSE( _pGameMode->isStarted() );
    _manager.endPlay();
}

/** @brief [CardBattleGameModeTest] 카드는 에너지만큼 내고 턴을 마치면 상대가 때린다. 쓴 상태를 되살리면 그 턴으로 이어진다 */
SW_TEST_CASE_F( CardBattleGameModeTest, CardsAndTurnsFollowTheRulesAndTheStateRoundTrips )
{
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 5, _pGameMode->getHandCount() );
    SW_EXPECT_TRUE( _pGameMode->playCard( 0 ) );
    SW_EXPECT_TRUE( _pGameMode->playCard( 0 ) );
    SW_EXPECT_TRUE( _pGameMode->playCard( 0 ) );
    SW_EXPECT_FALSE( _pGameMode->playCard( 0 ) ); // 에너지가 다했다
    SW_EXPECT_FALSE( _pGameMode->playCard( 9 ) ); // 없는 자리
    SW_EXPECT_EQUAL( 12, _pGameMode->getEnemyHP() );
    SW_EXPECT_TRUE( _pGameMode->endTurn() );
    SW_EXPECT_EQUAL( 45, _pGameMode->getPlayerHP() );
    SW_EXPECT_EQUAL( 2, _pGameMode->getTurn() );

    vector<uint8> bytes = saveState();
    SW_EXPECT_TRUE( _pGameMode->playCard( 0 ) );
    _pGameMode->restoreState( std::move( bytes ) );
    SW_EXPECT_EQUAL( 12, _pGameMode->getEnemyHP() );
    SW_EXPECT_EQUAL( 5, _pGameMode->getHandCount() );
    SW_EXPECT_EQUAL( 3, _pGameMode->getEnergy() );
    _manager.endPlay();
}

/** @brief [CardBattleGameModeTest] 자동 플레이가 이기면 상금을 내고, 결과를 보인 뒤 미니게임을 한 번 끝낸다(흐름이 없으면 오류) */
SW_TEST_CASE_F( CardBattleGameModeTest, AutoPlayedBattleIsWonWithTheReward )
{
    SW_EXPECT_LOG( "GameMode.FinishWithoutMinigame" );
    set( "_bAutoPlay", "true" );
    set( "_autoPlayInterval", "0.1" );
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    SW_EXPECT_FALSE( _pGameMode->playCard( 0 ) ); // 사람이 하지 않는다
    advance( 60 * 2 );                            // 여섯 수(세 장 · 턴 끝 · 두 장)와 결과 0.5 초를 넘긴다
    SW_EXPECT_EQUAL( static_cast<int32>( CardBattlePhase::Won ), _pGameMode->getPhase() );
    SW_EXPECT_EQUAL( 2, _pGameMode->getTurn() );
    SW_EXPECT_EQUAL( 45, _pGameMode->getPlayerHP() );
    vector<MinigameResultValue> listValue;
    _pGameMode->collectMinigameResult( listValue );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listValue.size() );
    SW_EXPECT_EQUAL( int64{ 30 }, listValue[0]._amount );
    _manager.endPlay();
}
