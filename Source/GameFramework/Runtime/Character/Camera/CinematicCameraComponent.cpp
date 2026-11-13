#include "pch.h"

#include "GameFramework/Runtime/Character/Camera/CinematicCameraComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Sequencer/SequenceDiagnostics.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraManagerComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraPose.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/SaveLock.h"

namespace sw
{
    namespace
    {
        struct CinematicCameraComponentInternal
        {
            /** @brief 시퀀서 흔들림의 기본 위상 속도(rad/s)입니다. 충격의 기본값과 같은 빠르기입니다. */
            static constexpr float32 kDefaultShakeFrequency = 30.0f;

            /** @brief 감시 값 `Cinematic.CutCamera` 입니다. 대상의 시네마틱 컴포넌트가 지금 화면을 덮는다고 아는 컷 카메라의 오브젝트 이름이고, 덮지 않으면 `none` 입니다. */
            [[nodiscard]] static bool readCutCamera( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*               pTarget    = query.findTargetObject();
                const CinematicCameraComponent* pCinematic = pTarget != nullptr ? pTarget->getComponent<CinematicCameraComponent>() : nullptr;
                if ( pCinematic == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "cinematic camera" ) );
                    return false;
                }
                const GameObject* pCut = pCinematic->isCinematicActive() ? query._pManager->resolveGameObject( pCinematic->getCurrentCutCamera() ) : nullptr;
                outValue.setText( pCut != nullptr ? pCut->getName().c_str() : "none" );
                return true;
            }

            /**
             * @brief 컷씬 디버거의 시네마틱 카메라 해석입니다 — 플레이어 옆 시네마틱 컴포넌트가 아는 컷 카메라와, 플레이어 카메라 매니저의 실제 블렌드 진행량(가중치)입니다.
             * @details 엔진 시퀀서는 블렌드 시간만 알고 실제 섞임은 게임 프레임워크 카메라가 한다 — 그래서 이 키트가 제공자로 보탠다(`SW_SEQUENCE_DIAGNOSTIC`).
             */
            [[nodiscard]] static bool describeForSequenceDebugger( const SequencePlayerComponent& player, string& outText )
            {
                const GameObject*               pOwner     = player.getOwner();
                const CinematicCameraComponent* pCinematic = pOwner != nullptr ? pOwner->getComponent<CinematicCameraComponent>() : nullptr;
                const GameObjectManager*        pManager   = pOwner != nullptr ? pOwner->getManager() : nullptr;
                if ( pCinematic == nullptr || pManager == nullptr )
                    return false;
                const GameObject*                        pCut           = pCinematic->isCinematicActive() ? pManager->resolveGameObject( pCinematic->getCurrentCutCamera() ) : nullptr;
                const CameraManagerComponent*            pCameraManager = CameraManagerComponent::findForPlayer( *pManager, pCinematic->getLocalPlayerIndex() );
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "Cut camera: %#", pCut != nullptr ? pCut->getName().c_str() : "none" );
                outText = string( textBuilder.view() );
                if ( pCameraManager != nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> textPart;
                    textPart.appendFormat( "\nPlayer camera blend: %# (weight %#)", pCameraManager->isBlending() ? "blending" : "settled", pCameraManager->getBlendWeight() );
                    outText += textPart.c_str();
                }
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( cinematicCutCamera, "Cinematic.CutCamera", "Name of the cut camera the target's cinematic component knows covers the screen; none between cinematics",
                               &CinematicCameraComponentInternal::readCutCamera, true, false );
    SW_SEQUENCE_DIAGNOSTIC( cinematicCamera, "Cinematic camera", &CinematicCameraComponentInternal::describeForSequenceDebugger );
} // namespace sw

namespace sw
{
    CinematicCameraComponent::CinematicCameraComponent()
        : _cinematicBlendCurve{ BlendCurve::SmoothStep }
        , _localPlayerIndex{ 0 }
        , _shakeFrequency{ CinematicCameraComponentInternal::kDefaultShakeFrequency }
        , _currentCutCamera{}
        , _signalSubscription{}
        , _saveBlock{}
    {
    }

    void CinematicCameraComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameObject*              pOwner  = getOwner();
        SequencePlayerComponent* pPlayer = pOwner != nullptr ? pOwner->getComponent<SequencePlayerComponent>() : nullptr;
        if ( pPlayer == nullptr )
        {
            SW_LOG_WARNING_ID( "CinematicCamera.CinematicCameraNoSequencePlayerComponent", "Cinematic camera on '%#' has no SequencePlayerComponent next to it - it hears nothing",
                               pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        _signalSubscription = pPlayer->registerCameraSignal(
            SW_DELEGATE_METHOD( OnSequenceCameraSignalDelegate, &CinematicCameraComponent::handleCameraSignal, this ) );
    }

    void CinematicCameraComponent::onEndPlay()
    {
        _signalSubscription.reset();
        _saveBlock.reset();
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

    GameObject* CinematicCameraComponent::findSignalTarget( const SequenceCameraSignal& signal ) const
    {
        const GameObject*  pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        return pManager != nullptr ? pManager->resolveGameObject( signal._target ) : nullptr;
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
        GameObject* pTarget = findSignalTarget( signal );
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
                SW_LOG_WARNING_ID( "CinematicCamera.SequenceCutAsksCamera", "Sequence cut '%#' asks for camera preset '%#' that '%#' does not have", signal._item._name.c_str(), signal._item._preset.c_str(),
                                   pTarget->getName().c_str() );
            // 다른 카메라에서 넘어오면 그 화면에서 이 프리셋의 블렌드로 섞는다. 같은 카메라면 디렉터가 이미 지금 화면에서 섞는다.
            const bool bFromOtherCamera = bActivated && pPreviousCamera != nullptr && pPrevious != pTarget;
            if ( bFromOtherCamera )
                pDirector->blendFromPose( CameraPose::makePoseFromCamera( *pPreviousCamera ), pDirector->getCatalog().getBlend( hashed_string{}, presetID ) );
        }
        _currentCutCamera = pTarget->getHandle();

        // 대기 뷰로 올려 둔 카메라가 이제 화면에 왔다 — 대기 뷰를 푼다.
        CameraManagerComponent* pCameraManager = pManager != nullptr ? CameraManagerComponent::findForPlayer( *pManager, _localPlayerIndex ) : nullptr;
        if ( pCameraManager != nullptr && pCameraManager->getPendingView()._camera == _currentCutCamera )
            pCameraManager->clearPendingView();
    }

    void CinematicCameraComponent::beginCinematic( const SequenceCameraSignal& signal )
    {
        GameObject* pTarget = findSignalTarget( signal );
        if ( pTarget == nullptr )
            return;
        _currentCutCamera = pTarget->getHandle();
        // 연출 동안 세이브를 막는다(언리얼 · 유니티 게임도 컷신 중 세이브를 받지 않는다). 게임 인스턴스가 없는 씬(에디터 미리보기)에는 막을 세이브가 없다.
        if ( SaveLock* const pSaveLock = game::findSubsystem<SaveLock>() )
            _saveBlock = pSaveLock->block( "cinematic" );
        if ( signal._blendSeconds <= 0.0f )
            return;
        // 게임 카메라 화면에서 첫 컷으로 섞어 들어간다(언리얼 시퀀스 Blend In). 디렉터가 없는 컷 카메라는 섞을 수 없어 끊어 바꾼다.
        const CameraComponent*   pGameCamera          = findGameCamera();
        CameraDirectorComponent* pDirector            = findDirector( pTarget );
        const bool               bBlendFromGameCamera = pGameCamera != nullptr && pDirector != nullptr && pGameCamera->getOwner() != pTarget;
        if ( bBlendFromGameCamera )
            pDirector->blendFromPose( CameraPose::makePoseFromCamera( *pGameCamera ), makeCinematicBlend( signal._blendSeconds ) );
    }

    void CinematicCameraComponent::endCinematic( const SequenceCameraSignal& signal )
    {
        const GameObject*        pOwner     = getOwner();
        const GameObjectManager* pManager   = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*        pLastCut   = pManager != nullptr ? pManager->resolveGameObject( _currentCutCamera ) : nullptr;
        const CameraComponent*   pCutCamera = pLastCut != nullptr ? pLastCut->getComponent<CameraComponent>() : nullptr;
        _currentCutCamera                   = GameObjectHandle{};
        _saveBlock.reset();
        if ( signal._blendSeconds <= 0.0f || pCutCamera == nullptr )
            return;
        // 마지막 컷 화면에서 게임 카메라로 섞어 나온다(언리얼 시퀀스 Blend Out). 게임 카메라를 움직이는 쪽(디렉터 · 카메라 매니저)이 섞는다.
        CameraComponent* pGameCamera = findGameCamera();
        GameObject*      pGameObject = pGameCamera != nullptr ? pGameCamera->getOwner() : nullptr;
        if ( pGameObject == nullptr || pGameObject == pLastCut )
            return;
        const CameraPose         from      = CameraPose::makePoseFromCamera( *pCutCamera );
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
        const GameObject*        pTarget  = findSignalTarget( signal );
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
        GameObject* pTarget = findSignalTarget( signal );
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
