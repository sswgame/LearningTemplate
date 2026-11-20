#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/PlayerStateComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

// LocalControlTest — "로컬이 조종하는 폰"(ControlSystem::isLocallyControlled) · 서버의 소유 연결(PawnComponent::findInputConnection) 정본 창구와
// 플레이어 상태(PlayerStateComponent — 조종자 오브젝트의 진행 플래그, 폰에서 찾기 · 상태 저장 왕복)를 본다(nogpu, 헤드리스).

using namespace sw;

/** @brief [LocalControlTest] 플레이어 조종자가 쥔 폰만 로컬이고, 원격 조종자가 쥔 폰은 그 연결을 내며, 쥔 조종자가 없는 폰은 둘 다 아니다 */
SW_TEST_CASE( LocalControlTest, PlayerControllerPawnIsLocalAndRemotePawnNamesItsConnection )
{
    GameObjectManager          manager;
    PawnComponent*             pLocalPawn  = manager.createGameObject( hashed_string( "LocalPawn" ) )->addComponent<PawnComponent>();
    PawnComponent*             pRemotePawn = manager.createGameObject( hashed_string( "RemotePawn" ) )->addComponent<PawnComponent>();
    PawnComponent*             pLoosePawn  = manager.createGameObject( hashed_string( "LoosePawn" ) )->addComponent<PawnComponent>();
    PlayerControllerComponent* pPlayer     = manager.createGameObject( hashed_string( "Player" ) )->addComponent<PlayerControllerComponent>();
    RemoteControllerComponent* pRemote     = manager.createGameObject( hashed_string( "Remote" ) )->addComponent<RemoteControllerComponent>();
    manager.beginPlay();

    pPlayer->possess( *pLocalPawn );
    pRemote->setInputConnection( 2 );
    pRemote->possess( *pRemotePawn );

    SW_EXPECT_TRUE( ControlSystem::isLocallyControlled( manager, *pLocalPawn ) );
    SW_EXPECT_FALSE( pLocalPawn->findInputConnection().has_value() );
    SW_EXPECT_FALSE( ControlSystem::isLocallyControlled( manager, *pRemotePawn ) );
    SW_REQUIRE_TRUE( pRemotePawn->findInputConnection().has_value() );
    SW_EXPECT_EQUAL( 2, *pRemotePawn->findInputConnection() );
    SW_EXPECT_FALSE( ControlSystem::isLocallyControlled( manager, *pLoosePawn ) );
    SW_EXPECT_FALSE( pLoosePawn->findInputConnection().has_value() );

    pRemote->unpossess(); // 놓인 폰은 연결을 잃는다(빙의를 따라간다)
    SW_EXPECT_FALSE( pRemotePawn->findInputConnection().has_value() );
}

/** @brief [LocalControlTest] 음수 연결 id 는 오류를 남기고 입력 피어를 바꾸지 않는다 */
SW_TEST_CASE( LocalControlTest, NegativeConnectionIsRefused )
{
    SW_EXPECT_LOG( "Controller.InputConnectionNegative" );
    GameObjectManager          manager;
    RemoteControllerComponent* pRemote = manager.createGameObject( hashed_string( "Remote" ) )->addComponent<RemoteControllerComponent>();
    pRemote->setInputConnection( -1 );
    SW_EXPECT_EQUAL( 0u, pRemote->getInputPeer() );
}

/** @brief [LocalControlTest] 플레이어 상태는 폰을 쥔 조종자 오브젝트에서 찾히고, 진행 플래그가 상태 저장 왕복을 넘는다 */
SW_TEST_CASE( LocalControlTest, PlayerStateIsFoundFromThePawnAndRoundTripsItsFlags )
{
    GameObjectManager          manager;
    GameObject*                pPawnObject = manager.createGameObject( hashed_string( "Pawn" ) );
    PawnComponent*             pPawn       = pPawnObject->addComponent<PawnComponent>();
    GameObject*                pController = manager.createGameObject( hashed_string( "Controller" ) );
    RemoteControllerComponent* pRemote     = pController->addComponent<RemoteControllerComponent>();
    PlayerStateComponent*      pState      = pController->addComponent<PlayerStateComponent>();
    manager.beginPlay();

    SW_EXPECT_NULL( PlayerStateComponent::findForPawnObject( manager, *pPawnObject ) ); // 아직 쥐지 않았다
    pRemote->possess( *pPawn );
    SW_EXPECT_TRUE( PlayerStateComponent::findForPawnObject( manager, *pPawnObject ) == pState );

    pState->getProgressFlags().setFlag( hashed_string( "town.arrivalSeen" ) );
    Archive written;
    pState->writeState( written );

    PlayerStateComponent* pRestored = manager.createGameObject( hashed_string( "Restored" ) )->addComponent<PlayerStateComponent>();
    Archive               reader( written.getData(), written.getSize() );
    SW_EXPECT_TRUE( pRestored->readState( reader ) );
    SW_EXPECT_EQUAL( 1, pRestored->getProgressFlags().getFlag( hashed_string( "town.arrivalSeen" ) ) );
}
