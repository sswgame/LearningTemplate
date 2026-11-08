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
