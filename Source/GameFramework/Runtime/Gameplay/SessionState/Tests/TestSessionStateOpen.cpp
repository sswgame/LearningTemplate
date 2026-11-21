#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlagsOwnerComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Runtime/Gameplay/SessionState/StateSectionTableUtil.h"

using namespace sw;

/**
 * @brief [SessionStateOpenTest] 오브젝트에 붙지 않은(판 상태를 찾을 수 없는) 디렉터는 판을 열지 못하고 오류와 Refused 를 받는다
 */
SW_TEST_CASE( SessionStateOpenTest, MissingStateIsRefused )
{
    SW_EXPECT_LOG( "SessionState.MissingOnOwner" );
    SessionStateComponent  loose;
    SessionStateInitResult result = SessionStateInitResult::Fresh;

    SW_EXPECT_TRUE( SessionStateComponent::openOnOwner( loose, SessionStateSettings{}, result ) == nullptr );
    SW_EXPECT_TRUE( result == SessionStateInitResult::Refused );
    SW_EXPECT_TRUE( SessionStateComponent::openOnOwner( loose, SessionStateSettings{} ) == nullptr );
}

/**
 * @brief [SessionStateOpenTest] 월드 상태는 씬에 하나다 — 같은 씬의 둘째 판 상태는 열리지 않고, 찾는 쪽(`findOpen`)은 첫 판만 본다(하위 레벨 미니게임이 바탕 씬에 판 상태를 열어도 시계 · 날씨가 갈리지 않는다)
 */
SW_TEST_CASE( SessionStateOpenTest, SecondStateInSceneIsRefused )
{
    SW_EXPECT_LOG( "SessionState.SecondInScene" );
    GameObjectManager            manager;
    SessionStateComponent* const pFirst  = manager.createGameObject( hashed_string( "Town" ) )->addComponent<SessionStateComponent>();
    SessionStateComponent* const pSecond = manager.createGameObject( hashed_string( "CardTable" ) )->addComponent<SessionStateComponent>();
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_NOT_NULL( pSecond );
    SW_REQUIRE_TRUE( pFirst->initialize( SessionStateSettings{} ) == SessionStateInitResult::Fresh );
    SW_EXPECT_TRUE( pSecond->initialize( SessionStateSettings{} ) == SessionStateInitResult::Refused );
    SW_EXPECT_FALSE( pSecond->isInitialized() );
    SW_EXPECT_TRUE( SessionStateComponent::findOpen( &manager ) == pFirst );
}

/**
 * @brief [SessionStateOpenTest] 월드 플래그 소유자가 씬에 둘 열려 있으면 찾는 쪽(`GameFlagsOwnerComponent::findOpen` — 활성자 볼륨의 진행 플래그)이 오류로 멈춘다 — 등록 순서로 고르지 않는다
 * @details 판 상태는 둘째 열기를 거절하므로, 오브젝트 밖에서 연 판 상태를 레지스트리에 직접 올려 "둘 열림" 을 만든다(하위 레벨이 둘째 소유자를 연 경우).
 */
SW_TEST_CASE( SessionStateOpenTest, SecondOpenFlagsOwnerIsAnError )
{
    SW_EXPECT_LOG( "GameFlagsOwner.SeveralOpen" );
    GameObjectManager            manager;
    SessionStateComponent* const pScene = manager.createGameObject( hashed_string( "Town" ) )->addComponent<SessionStateComponent>();
    SW_REQUIRE_NOT_NULL( pScene );
    SW_REQUIRE_TRUE( pScene->initialize( SessionStateSettings{} ) == SessionStateInitResult::Fresh );
    SW_EXPECT_TRUE( GameFlagsOwnerComponent::findOpen( &manager ) == &pScene->getFlags() );

    SessionStateComponent loose;
    SW_REQUIRE_TRUE( loose.initialize( SessionStateSettings{} ) == SessionStateInitResult::Fresh ); // 오브젝트 밖이라 씬 검사를 지나지 않는다
    manager.getComponentRegistry().add<GameFlagsOwnerComponent>( &loose );
    SW_EXPECT_NULL( GameFlagsOwnerComponent::findOpen( &manager ) );
    manager.getComponentRegistry().remove<GameFlagsOwnerComponent>( &loose );
    SW_EXPECT_TRUE( GameFlagsOwnerComponent::findOpen( &manager ) == &pScene->getFlags() );
}

/** @brief [SessionStateOpenTest] 판 1 세이브(플레이어 구간이 월드 상태 안에 있던 판)는 플레이어 구간을 버리고 경고하며 월드 구간(플래그)은 그대로 읽는다 */
SW_TEST_CASE( SessionStateOpenTest, VersionOneSaveDropsPlayerSections )
{
    SW_EXPECT_LOG( "SessionState.PlayerSectionsDropped" );
    Wallet wallet;
    wallet.add( Wallet::getDefaultCurrency(), 50 );
    GameFlags flags;
    flags.setFlag( hashed_string( "town.gateOpen" ) );
    Archive written;
    StateIO::writeHeader( written, SessionStateComponent::kStateTag, 1 );
    written << uint32{ 2 };
    StateSectionTableUtil::writeStateSection( written, wallet );
    StateSectionTableUtil::writeStateSection( written, flags );

    SessionStateComponent state;
    SW_REQUIRE_TRUE( state.initialize( SessionStateSettings{} ) == SessionStateInitResult::Fresh );
    Archive reader( written.getData(), written.getSize() );
    SW_EXPECT_TRUE( state.readState( reader ) );
    SW_EXPECT_EQUAL( 1, state.getFlags().getFlag( hashed_string( "town.gateOpen" ) ) );
}
