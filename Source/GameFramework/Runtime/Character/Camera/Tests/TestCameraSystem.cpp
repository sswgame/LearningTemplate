#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraManagerComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraSystem.h"

using namespace sw;

// CameraSystemTest — 디렉터 · 매니저의 미룬 포즈 계산이 시스템 한 곳에서 풀리는지, 매니저가 레지스트리에 오르는지(nogpu, 물리 없음).

SW_TEST_CASE( CameraSystemTest, SystemRunsRegisteredCameraComponents )
{
    GameObjectManager manager;
    SW_EXPECT_TRUE( CameraSystem::find( manager ) == nullptr );

    GameObject* pRig = manager.createGameObject( hashed_string( "Rig" ) );
    SW_REQUIRE_NOT_NULL( pRig );
    (void)pRig->addComponent<CameraComponent>();
    CameraDirectorComponent* pDirector = pRig->addComponent<CameraDirectorComponent>();
    SW_REQUIRE_NOT_NULL( pDirector );
    SW_REQUIRE_TRUE( CameraSystem::find( manager ) != nullptr ); // 첫 카메라 컴포넌트가 등록될 때 붙는다

    GameObject* pPlayer = manager.createGameObject( hashed_string( "Player" ) );
    SW_REQUIRE_NOT_NULL( pPlayer );
    (void)pPlayer->addComponent<CameraComponent>();
    CameraManagerComponent* pCameraManager = pPlayer->addComponent<CameraManagerComponent>();
    SW_REQUIRE_NOT_NULL( pCameraManager );
    // 매니저는 컴포넌트 레지스트리에 오른다 — 예전에는 오르지 않아 `isViewTarget` 이 늘 false 였다.
    pCameraManager->setViewTarget( pRig->getHandle() );
    SW_EXPECT_TRUE( CameraManagerComponent::isViewTarget( manager, pRig->getHandle() ) );

    // 틱 밖의 갱신은 바로 푼다 — 시스템에 남길 것이 없다.
    pDirector->updateCamera( 0.0f );
    pCameraManager->updateCamera( 0.0f );
    SW_EXPECT_FALSE( pDirector->isResolvePending() );
    SW_EXPECT_FALSE( pCameraManager->isResolvePending() );
    CameraSystem::find( manager )->runFramePass( manager ); // 미룬 것이 없으면 아무것도 하지 않고 지난다
    SW_EXPECT_FALSE( pCameraManager->isResolvePending() );
}

/** @brief [CameraSystemTest] 화면 시점을 모는 디렉터(게임 시점 카메라 또는 뷰 타깃)는 씬에 하나다 — 캡처 카메라의 디렉터는 세지 않고, 화면을 모는 것이 둘이면 오류와 nullptr 이다 */
SW_TEST_CASE( CameraSystemTest, ViewDirectorIsUnique )
{
    SW_EXPECT_LOG( "ComponentRegistry.DuplicateUnique" );
    GameObjectManager manager;
    SW_EXPECT_NULL( CameraDirectorComponent::findViewDirector( manager ) );

    GameObject* pGameRig = manager.createGameObject( hashed_string( "GameRig" ) );
    SW_REQUIRE_NOT_NULL( pGameRig );
    pGameRig->addComponent<CameraComponent>()->setRole( CameraRole::Game );
    CameraDirectorComponent* pGameDirector = pGameRig->addComponent<CameraDirectorComponent>();
    SW_REQUIRE_NOT_NULL( pGameDirector );

    GameObject* pCaptureRig = manager.createGameObject( hashed_string( "CaptureRig" ) );
    SW_REQUIRE_NOT_NULL( pCaptureRig );
    pCaptureRig->addComponent<CameraComponent>()->setRole( CameraRole::Capture );
    SW_REQUIRE_NOT_NULL( pCaptureRig->addComponent<CameraDirectorComponent>() );
    SW_EXPECT_TRUE( CameraDirectorComponent::findViewDirector( manager ) == pGameDirector ); // 캡처 카메라는 화면을 몰지 않는다

    GameObject* pSecondRig = manager.createGameObject( hashed_string( "SecondGameRig" ) );
    SW_REQUIRE_NOT_NULL( pSecondRig );
    pSecondRig->addComponent<CameraComponent>()->setRole( CameraRole::Game );
    SW_REQUIRE_NOT_NULL( pSecondRig->addComponent<CameraDirectorComponent>() );
    SW_EXPECT_NULL( CameraDirectorComponent::findViewDirector( manager ) );
}
