#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Condition/IGameFlagsOwner.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/StateSectionTableUtil.h"
#include "GameFramework/Runtime/Gameplay/GameState/Tests/FlagsOwnerTestComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

using namespace sw;

/**
 * @brief [GameStateOpenTest] 오브젝트에 붙지 않은(판 상태를 찾을 수 없는) 디렉터는 판을 열지 못하고 오류와 Refused 를 받는다
 */
SW_TEST_CASE( GameStateOpenTest, MissingStateIsRefused )
{
    SW_EXPECT_LOG( "GameState.MissingOnOwner" );
    GameStateComponent  loose;
    GameStateInitResult result = GameStateInitResult::Fresh;

    SW_EXPECT_TRUE( GameStateComponent::openOnOwner( loose, GameStateSettings{}, result ) == nullptr );
    SW_EXPECT_TRUE( result == GameStateInitResult::Refused );
    SW_EXPECT_TRUE( GameStateComponent::openOnOwner( loose, GameStateSettings{} ) == nullptr );
}

/**
 * @brief [GameStateOpenTest] 월드 상태는 씬에 하나다 — 같은 씬의 둘째 판 상태는 등록에서 오류로 거절되어(`onRegister`) 레지스트리에 오르지 않고 열리지도 않으며,
 *        찾는 쪽(`findOpen`)은 첫 판만 본다(하위 레벨 미니게임이 바탕 씬에 판 상태를 두어도 시계 · 날씨가 갈리지 않는다)
 */
SW_TEST_CASE( GameStateOpenTest, SecondStateInSceneIsRefused )
{
    SW_EXPECT_LOG( "GameState.SecondInScene" );
    GameObjectManager         manager;
    GameStateComponent* const pFirst  = manager.createGameObject( hashed_string( "Town" ) )->addComponent<GameStateComponent>();
    GameStateComponent* const pSecond = manager.createGameObject( hashed_string( "CardTable" ) )->addComponent<GameStateComponent>();
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_NOT_NULL( pSecond );
    SW_REQUIRE_TRUE( pFirst->initialize( GameStateSettings{} ) == GameStateInitResult::Fresh );
    SW_EXPECT_TRUE( pSecond->initialize( GameStateSettings{} ) == GameStateInitResult::Refused );
    SW_EXPECT_FALSE( pSecond->isInitialized() );
    SW_EXPECT_TRUE( GameStateComponent::findOpen( &manager ) == pFirst );
    size_t registeredCount = 0;
    for ( const GameStateComponent* pRegistered : manager.getComponentRegistry().getAll<GameStateComponent>() )
    {
        SW_EXPECT_TRUE( pRegistered == pFirst );
        ++registeredCount;
    }
    SW_EXPECT_EQUAL( size_t{ 1 }, registeredCount );
}

/**
 * @brief [GameStateOpenTest] 월드 플래그 소유자가 씬에 둘 열려 있으면 찾는 쪽(`IGameFlagsOwner::findOpen` — 활성자 볼륨의 진행 플래그)이 오류로 멈춘다 — 등록 순서로 고르지 않는다
 * @details 판 상태는 둘째 열기를 거절하므로, 같은 계약을 내보내는 다른 컴포넌트(`FlagsOwnerTestComponent`)로 "둘 열림" 을 만든다(하위 레벨이 둘째 소유자를 연 경우).
 */
SW_TEST_CASE( GameStateOpenTest, SecondOpenFlagsOwnerIsAnError )
{
    SW_EXPECT_LOG( "GameFlagsOwner.SeveralOpen" );
    GameObjectManager         manager;
    GameStateComponent* const pScene = manager.createGameObject( hashed_string( "Town" ) )->addComponent<GameStateComponent>();
    SW_REQUIRE_NOT_NULL( pScene );
    SW_REQUIRE_TRUE( pScene->initialize( GameStateSettings{} ) == GameStateInitResult::Fresh );
    SW_EXPECT_TRUE( IGameFlagsOwner::findOpen( &manager ) == &pScene->getFlags() );

    GameObject* const              pLevel = manager.createGameObject( hashed_string( "SubLevel" ) );
    FlagsOwnerTestComponent* const pLoose = pLevel->addComponent<FlagsOwnerTestComponent>(); // 같은 계약의 둘째 주인 — 레지스트리 인터페이스 칸에 저절로 오른다
    SW_REQUIRE_NOT_NULL( pLoose );
    SW_EXPECT_NULL( IGameFlagsOwner::findOpen( &manager ) );
    SW_REQUIRE_TRUE( pLevel->removeComponent( pLoose ) );
    SW_EXPECT_TRUE( IGameFlagsOwner::findOpen( &manager ) == &pScene->getFlags() );
}

/** @brief [GameStateOpenTest] 판 1 세이브(플레이어 구간이 월드 상태 안에 있던 판)는 플레이어 구간을 버리고 경고하며 월드 구간(플래그)은 그대로 읽는다 */
SW_TEST_CASE( GameStateOpenTest, VersionOneSaveDropsPlayerSections )
{
    SW_EXPECT_LOG( "GameState.PlayerSectionsDropped" );
    Wallet wallet;
    wallet.add( Wallet::getDefaultCurrency(), 50 );
    GameFlags flags;
    flags.setFlag( hashed_string( "town.gateOpen" ) );
    Archive written;
    StateIO::writeHeader( written, GameStateComponent::kStateTag, 1 );
    written << uint32{ 2 };
    StateSectionTableUtil::writeStateSection( written, wallet );
    StateSectionTableUtil::writeStateSection( written, flags );

    GameStateComponent state;
    SW_REQUIRE_TRUE( state.initialize( GameStateSettings{} ) == GameStateInitResult::Fresh );
    Archive reader( written.getData(), written.getSize() );
    SW_EXPECT_TRUE( state.readState( reader ) );
    SW_EXPECT_EQUAL( 1, state.getFlags().getFlag( hashed_string( "town.gateOpen" ) ) );
}
