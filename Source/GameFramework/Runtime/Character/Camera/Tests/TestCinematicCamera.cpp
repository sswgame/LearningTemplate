#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "GameFramework/Runtime/Character/Camera/CameraManagerComponent.h"
#include "GameFramework/Runtime/Character/Camera/CinematicCameraComponent.h"

using namespace sw;

namespace
{
    struct CinematicCameraTestUtil
    {
        /** @brief 플레이어가 바인딩을 풀어 대상 핸들을 채운 신호입니다(신호는 이름이 아니라 묶인 오브젝트를 싣는다). */
        static SequenceCameraSignal makeSignal( SequenceCameraSignalKind kind, GameObjectHandle target, float32 blendSeconds )
        {
            SequenceCameraSignal signal;
            signal._kind          = kind;
            signal._item._binding = 1;
            signal._target        = target;
            signal._blendSeconds  = blendSeconds;
            return signal;
        }
    };
} // namespace

/** @brief [CinematicCameraTest] 대기 뷰는 플레이어의 카메라 매니저로 가고 컷이 오면 풀리며, 끝 신호는 마지막 컷 화면에서 게임 카메라로 섞어 나온다 */
SW_TEST_CASE( CinematicCameraTest, SignalsReachThePlayerCameraManager )
{
    using Util = CinematicCameraTestUtil;
    GameObjectManager manager;

    GameObject* pPlayer = manager.createGameObject( hashed_string( "Player" ) );
    SW_REQUIRE_NOT_NULL( pPlayer );
    CameraComponent* pPlayerCamera = pPlayer->addComponent<CameraComponent>();
    SW_REQUIRE_NOT_NULL( pPlayerCamera );
    pPlayerCamera->setRole( CameraRole::Game );
    CameraManagerComponent* pCameraManager = pPlayer->addComponent<CameraManagerComponent>();
    SW_REQUIRE_NOT_NULL( pCameraManager );

    GameObject* pCut = manager.createGameObject( hashed_string( "Cut" ) );
    SW_REQUIRE_NOT_NULL( pCut );
    CameraComponent* pCutCamera = pCut->addComponent<CameraComponent>();
    SW_REQUIRE_NOT_NULL( pCutCamera );
    pCutCamera->setRole( CameraRole::Custom );

    GameObject* pDirector = manager.createGameObject( hashed_string( "Director" ) );
    SW_REQUIRE_NOT_NULL( pDirector );
    CinematicCameraComponent* pCinematic = pDirector->addComponent<CinematicCameraComponent>();
    SW_REQUIRE_NOT_NULL( pCinematic );

    SW_EXPECT_TRUE( CameraManagerComponent::findForPlayer( manager, 0 ) == pCameraManager );

    pCinematic->handleCameraSignal( Util::makeSignal( SequenceCameraSignalKind::CutPrewarm, pCut->getHandle(), 0.0f ) );
    SW_EXPECT_TRUE( pCameraManager->hasPendingView() );
    SW_EXPECT_TRUE( pCameraManager->getPendingView()._camera == pCut->getHandle() );
    SW_EXPECT_FALSE( pCinematic->isCinematicActive() );

    pCinematic->handleCameraSignal( Util::makeSignal( SequenceCameraSignalKind::CutBegin, pCut->getHandle(), 0.0f ) );
    SW_EXPECT_FALSE( pCameraManager->hasPendingView() );
    SW_EXPECT_TRUE( pCinematic->isCinematicActive() );
    SW_EXPECT_TRUE( pCinematic->getCurrentCutCamera() == pCut->getHandle() );

    SW_EXPECT_FALSE( pCameraManager->isBlending() );
    pCinematic->handleCameraSignal( Util::makeSignal( SequenceCameraSignalKind::CinematicEnd, pCut->getHandle(), 0.5f ) );
    SW_EXPECT_FALSE( pCinematic->isCinematicActive() );
    SW_EXPECT_TRUE( pCameraManager->isBlending() );
}
