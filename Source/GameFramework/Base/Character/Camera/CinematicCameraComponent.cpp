#include "pch.h"

#include "GameFramework/Base/Character/Camera/CinematicCameraComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "GameFramework/Base/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Base/Character/Camera/CameraManagerComponent.h"
#include "GameFramework/Base/Character/Camera/CameraPoseUtil.h"

namespace sw
{
    SW_LOG_CALLER( "CinematicCamera" );

    namespace
    {
        struct CinematicCameraComponentInternal
        {
            /** @brief 시퀀서 흔들림의 기본 위상 속도(rad/s)입니다. 충격의 기본값과 같은 빠르기입니다. */
            static constexpr float32 kDefaultShakeFrequency = 30.0f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    CinematicCameraComponent::CinematicCameraComponent()
        : _cinematicBlendCurve{ BlendCurve::SmoothStep }
        , _localPlayerIndex{ 0 }
        , _shakeFrequency{ CinematicCameraComponentInternal::kDefaultShakeFrequency }
        , _currentCutCamera{}
        , _signalSubscription{}
    {
    }

    void CinematicCameraComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameObject*              pOwner  = getOwner();
        SequencePlayerComponent* pPlayer = pOwner != nullptr ? pOwner->getComponent<SequencePlayerComponent>() : nullptr;
        if ( pPlayer == nullptr )
        {
            SW_LOG_WARNING( "Cinematic camera on '%#' has no SequencePlayerComponent next to it - it hears nothing",
                            pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        _signalSubscription = pPlayer->registerCameraSignal(
            SW_DELEGATE_METHOD( OnSequenceCameraSignalDelegate, &CinematicCameraComponent::handleCameraSignal, this ) );
    }

    void CinematicCameraComponent::onEndPlay()
    {
        _signalSubscription.reset();
        _currentCutCamera = GameObjectHandle{};
        Component::onEndPlay();
    }

    void CinematicCameraComponent::handleCameraSignal( const SequenceCameraSignal& signal )
    {
        switch ( signal._kind )
        {
            case SequenceCameraSignalKind::CutBegin:
            {
                applyCut( signal );
                break;
            }
            case SequenceCameraSignalKind::CinematicBegin:
            {
                beginCinematic( signal );
                break;
            }
            case SequenceCameraSignalKind::CinematicEnd:
            {
                endCinematic( signal );
                break;
            }
            case SequenceCameraSignalKind::CutPrewarm:
            {
                forwardPendingView( signal );
                break;
            }
            case SequenceCameraSignalKind::Shake:
            {
                applyShake( signal );
                break;
            }
        }
    }

    GameObject* CinematicCameraComponent::findSceneObject( string_view name ) const
    {
        const GameObject*  pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || name.empty() )
            return nullptr;
        return pManager->findGameObjectByName( hashed_string{ name } );
    }

    CameraDirectorComponent* CinematicCameraComponent::findDirector( const GameObject* pObject ) const
    {
        return pObject != nullptr ? pObject->getComponent<CameraDirectorComponent>() : nullptr;
    }

    CameraComponent* CinematicCameraComponent::findGameCamera() const
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        // 컷과 상관없이 레지스트리 규칙이 고르는 게임 카메라다(컷 카메라가 덮기 전과 덮은 뒤에 화면에 오는 카메라).
        return pManager != nullptr ? pManager->getCameraRegistry().selectCamera( CameraRole::Game ) : nullptr;
    }

    BlendCurveDef CinematicCameraComponent::makeCinematicBlend( float32 seconds ) const
    {
        BlendCurveDef blend;
        SW_ASSERT( seconds > 0.0f ); // 호출부가 0 이하(끊어 바꿈)를 먼저 걸러 보낸다. 0 으로 맞춰 컷으로 돌지 않는다
        blend._curve    = _cinematicBlendCurve;
        blend._duration = seconds;
        return blend;
    }

    void CinematicCameraComponent::applyCut( const SequenceCameraSignal& signal )
    {
        GameObject* pTarget = findSceneObject( signal._item._targetObject );
        if ( pTarget == nullptr )
            return;
        const GameObject*        pOwner          = getOwner();
        const GameObjectManager* pManager        = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*        pPrevious       = pManager != nullptr ? pManager->resolveGameObject( _currentCutCamera ) : nullptr;
        const CameraComponent*   pPreviousCamera = pPrevious != nullptr ? pPrevious->getComponent<CameraComponent>() : nullptr;
        CameraDirectorComponent* pDirector       = findDirector( pTarget );
        if ( pDirector != nullptr && signal._item._preset.empty() == false )
        {
            const hashed_string presetID( string_view{ signal._item._preset.c_str(), signal._item._preset.size() } );
            const bool          bActivated = pDirector->activatePreset( presetID );
            if ( bActivated == false )
                SW_LOG_WARNING( "Sequence cut '%#' asks for camera preset '%#' that '%#' does not have", signal._item._name.c_str(), signal._item._preset.c_str(),
                                pTarget->getName().c_str() );
            // 다른 카메라에서 넘어오면 그 화면에서 이 프리셋의 블렌드로 섞는다. 같은 카메라면 디렉터가 이미 지금 화면에서 섞는다.
            const bool bFromOtherCamera = bActivated && pPreviousCamera != nullptr && pPrevious != pTarget;
            if ( bFromOtherCamera )
                pDirector->blendFromPose( CameraPoseUtil::makePoseFromCamera( *pPreviousCamera ), pDirector->getCatalog().getBlend( hashed_string{}, presetID ) );
        }
        _currentCutCamera = pTarget->getHandle();

        // 대기 뷰로 올려 둔 카메라가 이제 화면에 왔다 — 대기 뷰를 푼다.
        CameraManagerComponent* pCameraManager = pManager != nullptr ? CameraManagerComponent::findForPlayer( *pManager, _localPlayerIndex ) : nullptr;
        if ( pCameraManager != nullptr && pCameraManager->getPendingView()._camera == _currentCutCamera )
            pCameraManager->clearPendingView();
    }

    void CinematicCameraComponent::beginCinematic( const SequenceCameraSignal& signal )
    {
        GameObject* pTarget = findSceneObject( signal._item._targetObject );
        if ( pTarget == nullptr )
            return;
        _currentCutCamera = pTarget->getHandle();
        if ( signal._blendSeconds <= 0.0f )
            return;
        // 게임 카메라 화면에서 첫 컷으로 섞어 들어간다(언리얼 시퀀스 Blend In). 디렉터가 없는 컷 카메라는 섞을 수 없어 끊어 바꾼다.
        const CameraComponent*   pGameCamera          = findGameCamera();
        CameraDirectorComponent* pDirector            = findDirector( pTarget );
        const bool               bBlendFromGameCamera = pGameCamera != nullptr && pDirector != nullptr && pGameCamera->getOwner() != pTarget;
        if ( bBlendFromGameCamera )
            pDirector->blendFromPose( CameraPoseUtil::makePoseFromCamera( *pGameCamera ), makeCinematicBlend( signal._blendSeconds ) );
    }

    void CinematicCameraComponent::endCinematic( const SequenceCameraSignal& signal )
    {
        const GameObject*        pOwner     = getOwner();
        const GameObjectManager* pManager   = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*        pLastCut   = pManager != nullptr ? pManager->resolveGameObject( _currentCutCamera ) : nullptr;
        const CameraComponent*   pCutCamera = pLastCut != nullptr ? pLastCut->getComponent<CameraComponent>() : nullptr;
        _currentCutCamera                   = GameObjectHandle{};
        if ( signal._blendSeconds <= 0.0f || pCutCamera == nullptr )
            return;
        // 마지막 컷 화면에서 게임 카메라로 섞어 나온다(언리얼 시퀀스 Blend Out). 게임 카메라를 움직이는 쪽(디렉터 · 카메라 매니저)이 섞는다.
        CameraComponent* pGameCamera = findGameCamera();
        GameObject*      pGameObject = pGameCamera != nullptr ? pGameCamera->getOwner() : nullptr;
        if ( pGameObject == nullptr || pGameObject == pLastCut )
            return;
        const CameraPose         from      = CameraPoseUtil::makePoseFromCamera( *pCutCamera );
        const BlendCurveDef      blend     = makeCinematicBlend( signal._blendSeconds );
        CameraDirectorComponent* pDirector = findDirector( pGameObject );
        if ( pDirector != nullptr )
        {
            pDirector->blendFromPose( from, blend );
            return;
        }
        CameraManagerComponent* pCameraManager = pGameObject->getComponent<CameraManagerComponent>();
        if ( pCameraManager != nullptr )
            pCameraManager->blendFromPose( from, blend );
    }

    void CinematicCameraComponent::forwardPendingView( const SequenceCameraSignal& signal )
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*        pTarget  = findSceneObject( signal._item._targetObject );
        if ( pManager == nullptr || pTarget == nullptr )
            return;
        CameraManagerComponent* pCameraManager = CameraManagerComponent::findForPlayer( *pManager, _localPlayerIndex );
        if ( pCameraManager == nullptr )
            return;
        CameraPendingView view;
        view._camera       = pTarget->getHandle();
        view._secondsUntil = signal._secondsUntil;
        pCameraManager->setPendingView( view );
    }

    void CinematicCameraComponent::applyShake( const SequenceCameraSignal& signal )
    {
        GameObject* pTarget = findSceneObject( signal._item._targetObject );
        if ( pTarget == nullptr )
            return;
        CameraImpulseDef impulse;
        impulse._amplitude                 = signal._item._amplitude;
        impulse._duration                  = signal._durationSeconds;
        impulse._frequency                 = _shakeFrequency;
        const CameraComponent*   pCamera   = pTarget->getComponent<CameraComponent>();
        const float3             origin    = pCamera != nullptr ? pCamera->getWorldPosition() : float3{};
        CameraDirectorComponent* pDirector = findDirector( pTarget );
        if ( pDirector != nullptr )
        {
            pDirector->addImpulse( impulse, origin );
            return;
        }
        CameraManagerComponent* pCameraManager = pTarget->getComponent<CameraManagerComponent>();
        if ( pCameraManager != nullptr )
            pCameraManager->addImpulse( impulse, origin );
    }
} // namespace sw
