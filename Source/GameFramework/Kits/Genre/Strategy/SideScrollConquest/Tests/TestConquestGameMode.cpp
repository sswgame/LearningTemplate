// ConquestGameModeTest — 횡스크롤 정복 디렉터: 열기 전 명령 거절, 짝이 맞지 않는 시작 배치 거절, 지휘 명령과 상태 왕복, 자동 플레이로 이긴 판의 상금 · 끝내기(nogpu).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/Component/ConquestGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Presentation/Tests/RecordingSoundPlayer.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

using namespace sw;

namespace
{
    constexpr float32 kConquestFrame     = 1.0f / 60.0f;
    constexpr int32   kMaxConquestFrames = 60 * 120; ///< 이긴 판이 끝나고도 남는 시뮬레이션 시간(프레임) — 벽시계와 무관하다

    constexpr const utf8* kConquestXML = R"(<ConquestCatalog><Rules fixedStep="0.05" captureTime="1" waveInterval="0" incomeInterval="5"/><Start gold="30"/>
        <Building id="barracks" trains="spear"><Cost gold="10"/></Building>
        <Unit id="spear" hp="40" damage="8" range="1" speed="3" train="1"><Cost gold="10"/></Unit>
        <Site id="home" x="0" kind="Village" owner="Player" slots="1" housing="5"><Income gold="5"/></Site>
        <Site id="outpost" x="12" kind="Outpost"/><Site id="keep" x="24" kind="Fortress" owner="Enemy"/></ConquestCatalog>)";

    /** @brief 거점 셋의 카탈로그를 건 게임 범위와 디렉터 하나입니다. */
    struct ConquestGameModeTest
    {
        ConquestCatalog                   _catalog{};
        RecordingSoundPlayer              _soundPlayer{};
        SubsystemScope                    _scope{};
        unique_ptr<game::ScopedGameScope> _pScopeBinding{};
        GameObjectManager                 _manager{};
        ConquestGameModeComponent*        _pGameMode{ nullptr };

        ConquestGameModeTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kConquestXML, "ConquestGameModeTest" ) );
            SW_REQUIRE_TRUE( _scope.bind<ISoundPlayer>( &_soundPlayer ) && _scope.bind<const ConquestCatalog>( &_catalog ) );
            _pScopeBinding = make_unique<game::ScopedGameScope>( _scope );
            _pGameMode     = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<ConquestGameModeComponent>();
            SW_REQUIRE_NOT_NULL( _pGameMode );
            set( "_startBuilding", "barracks" );
            set( "_startSite", "home" );
            set( "_trainUnit", "spear" );
            set( "_resultHoldSeconds", "0.5" );
            set( "_winReward", "60" );
        }

        void set( const utf8* pProperty, string_view text )
        {
            const PropertyInfo* const pInfo = ConquestGameModeComponent::StaticType()->findPropertyInHierarchy( hashed_string( pProperty ) );
            SW_REQUIRE_NOT_NULL( pInfo );
            SW_REQUIRE_TRUE( SerializerUtil::applyPropertyText( *pInfo, MutableObjectAddress{ _pGameMode }, text, SerializeContext::getDefault( SerializePurpose::Authoring ) ) );
        }

        void advance( int32 frameCount )
        {
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                _manager.update( kConquestFrame );
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

/** @brief [ConquestGameModeTest] 판이 열리기 전의 지휘 명령은 거절한다 */
SW_TEST_CASE( ConquestGameModeTest, CommandsBeforeTheMatchOpensAreRefused )
{
    GameObjectManager          manager;
    ConquestGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<ConquestGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->submitCommanderMove( 1.0f ) );
    SW_EXPECT_FALSE( pGameMode->submitOrder( ConquestOrder::Charge ) );
    SW_EXPECT_TRUE( pGameMode->submitTrain() == ConquestResult::InvalidBuilding );
    SW_EXPECT_EQUAL( 0, pGameMode->getTerritory() );
}

/** @brief [ConquestGameModeTest] 시작 건물이 훈련하지 않는 병종이면 오류를 남기고 판을 열지 않는다 */
SW_TEST_CASE_F( ConquestGameModeTest, LayoutThatCannotTrainDoesNotOpen )
{
    SW_EXPECT_LOG( "Conquest.LayoutUnusable" );
    set( "_trainUnit", "nobody" );
    _manager.beginPlay();
    SW_EXPECT_FALSE( _pGameMode->isStarted() );
    _manager.endPlay();
}

/** @brief [ConquestGameModeTest] 훈련한 병사가 부대에 들고 지휘관은 명령으로 나아가며, 쓴 상태를 되살리면 그 자리로 이어진다 */
SW_TEST_CASE_F( ConquestGameModeTest, CommandsTrainAndMoveAndTheStateRoundTrips )
{
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    SW_EXPECT_EQUAL( 1, _pGameMode->getTerritory() );
    SW_EXPECT_EQUAL( 20, _pGameMode->getWorld().getResource( hashed_string( "gold" ) ) ); // 시작 30 - 건물 10
    SW_EXPECT_TRUE( _pGameMode->submitTrain() == ConquestResult::Ok );
    SW_EXPECT_TRUE( _pGameMode->submitTrain() == ConquestResult::Ok );
    SW_EXPECT_TRUE( _pGameMode->submitTrain() == ConquestResult::NotEnoughResources );
    SW_EXPECT_TRUE( _pGameMode->submitCommanderMove( 1.0f ) );
    advance( 90 );
    SW_EXPECT_EQUAL( 1, _pGameMode->getSquadSize() ); // 1 초에 하나 — 1.5 초면 하나다
    const float32 x = _pGameMode->getCommanderX();
    SW_EXPECT_TRUE( x > 4.0f );

    vector<uint8> bytes = saveState();
    advance( 60 );
    _pGameMode->restoreState( std::move( bytes ) );
    SW_EXPECT_NEAR( x, _pGameMode->getCommanderX(), 1.0e-4f );
    _manager.endPlay();
}

/** @brief [ConquestGameModeTest] 자동 플레이가 거점을 모두 얻으면 상금을 내고, 결과를 보인 뒤 미니게임을 한 번 끝낸다(흐름이 없으면 오류) */
SW_TEST_CASE_F( ConquestGameModeTest, AutoPlayedMatchIsWonWithTheReward )
{
    SW_EXPECT_LOG( "GameMode.FinishWithoutMinigame" );
    set( "_bAutoPlay", "true" );
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    SW_EXPECT_FALSE( _pGameMode->submitCommanderMove( 1.0f ) ); // 사람이 지휘하지 않는다
    for ( int32 frame = 0; frame < kMaxConquestFrames && _pGameMode->getVictory() == 0; ++frame )
    {
        advance( 1 );
    }
    SW_REQUIRE_EQUAL( 1, _pGameMode->getVictory() );
    vector<MinigameResultValue> listValue;
    _pGameMode->collectMinigameResult( listValue );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listValue.size() );
    SW_EXPECT_EQUAL( int64{ 60 }, listValue[0]._amount );
    advance( 60 ); // 결과 0.5 초를 넘긴다
    _manager.endPlay();
}
