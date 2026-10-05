#include "pch.h"

#include "GameFramework/Base/Camera/CameraManagerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/AnimNotify/AnimNotifyHandlers.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Camera/CameraPoseUtil.h"
#include "GameFramework/Base/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "CameraManagerComponent" );
} // namespace sw

namespace sw
{
    CameraManagerComponent::CameraManagerComponent()
        : _viewTarget{}
        , _defaultBlend{}
        , _localPlayerIndex{ 0 }
        , _cycleAction{}
        , _cycleRole{ CameraRole::Custom }
        , _previousTarget{}
        , _blender{}
        , _impulseListener{}
        , _shakeSubscription{}
        , _pendingDeltaTime{ 0.0f }
    {
        setCanEverTick( true );
    }

    void CameraManagerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 뷰 타깃을 움직이는 디렉터 · 리그(PostPhysics 까지)가 미룬 쓰기 다음에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
        updateCamera( 0.0f );
        _shakeSubscription = AnimNotifyHandlerUtil::getCameraShakeRequested().add(
            SW_DELEGATE_METHOD( CameraShakeRequestCallback, &CameraManagerComponent::onCameraShakeRequested, this ) );
    }

    void CameraManagerComponent::onEndPlay()
    {
        AnimNotifyHandlerUtil::getCameraShakeRequested().remove( _shakeSubscription );
        _shakeSubscription = DelegateHandle{};
        Component::onEndPlay();
    }

    void CameraManagerComponent::onCameraShakeRequested( const CameraShakeRequest& request )
    {
        CameraImpulseDef def;
        def._amplitude     = request._amplitude;
        def._duration      = request._duration;
        def._frequency     = request._frequency;
        def._falloffRadius = request._falloffRadius;
        _impulseListener.addImpulse( def, request._origin );
    }

    void CameraManagerComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const InputManager* pInput = _cycleAction.empty() == false ? game::getService<InputManager>() : nullptr;
        if ( pInput != nullptr && pInput->getInputMap().wasActionTriggered( _cycleAction ) )
            (void)cycleViewTarget( _cycleRole );
        updateCamera( deltaTime );
    }

    void CameraManagerComponent::setViewTarget( const GameObjectHandle& target, const BlendCurveSpec& blend )
    {
        if ( target == _viewTarget )
            return;
        // 블렌드가 없던 때 바꾸면 나가는 타깃 카메라를 블렌드 동안 계속 읽는다(움직이는 카메라에서 출발).
        _previousTarget = _blender.start( blend ) ? _viewTarget : GameObjectHandle{};
        _viewTarget     = target;
    }

    GameObjectHandle CameraManagerComponent::cycleViewTarget( CameraRole role )
    {
        const GameObject*  pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return _viewTarget;
        vector<CameraComponent*> listCamera;
        findCamerasByRole( *pManager, role, listCamera );
        vector<GameObjectHandle> listCandidate;
        for ( const CameraComponent* pCamera : listCamera )
        {
            if ( pCamera->getOwner() != pOwner )
                listCandidate.push_back( pCamera->getOwner()->getHandle() );
        }
        if ( listCandidate.empty() )
            return _viewTarget;
        size_t nextIndex = 0;
        for ( size_t index = 0; index < listCandidate.size(); ++index )
        {
            if ( listCandidate[index] == _viewTarget )
            {
                nextIndex = ( index + 1 ) % listCandidate.size();
                break;
            }
        }
        setViewTarget( listCandidate[nextIndex] );
        return _viewTarget;
    }

    void CameraManagerComponent::findCamerasByRole( const GameObjectManager& manager, CameraRole role, vector<CameraComponent*>& outListCamera )
    {
        for ( CameraComponent* pCamera : manager.getCameraRegistry().getAll() )
        {
            if ( CameraRegistry::isUsableCamera( pCamera ) && pCamera->getRole() == role )
                outListCamera.push_back( pCamera );
        }
    }

    CameraManagerComponent* CameraManagerComponent::findForPlayer( const GameObjectManager& manager, uint32 playerIndex )
    {
        for ( CameraComponent* pCamera : manager.getCameraRegistry().getAll() )
        {
            GameObject*             pOwner         = pCamera != nullptr ? pCamera->getOwner() : nullptr;
            CameraManagerComponent* pCameraManager = pOwner != nullptr ? pOwner->getComponent<CameraManagerComponent>() : nullptr;
            if ( pCameraManager != nullptr && pCameraManager->getLocalPlayerIndex() == playerIndex )
                return pCameraManager;
        }
        return nullptr;
    }

    void CameraManagerComponent::updateCamera( float32 deltaTime )
    {
        _pendingDeltaTime += MathUtil::max( 0.0f, deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
        {
            resolveCamera();
            return;
        }
        pManager->executeOrDeferPostTick( SW_DELEGATE_METHOD( GameObjectManager::PostTickDelegate, &CameraManagerComponent::resolveCamera, this ) );
    }

    CameraComponent* CameraManagerComponent::findTargetCamera( const GameObjectHandle& target ) const
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*              pObject  = pManager != nullptr ? pManager->resolveGameObject( target ) : nullptr;
        if ( pObject == nullptr || pObject == pOwner )
            return nullptr;
        return pObject->getComponent<CameraComponent>();
    }

    void CameraManagerComponent::resolveCamera()
    {
        const float32    deltaTime = _pendingDeltaTime;
        GameObject*      pOwner    = getOwner();
        CameraComponent* pCamera   = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        _pendingDeltaTime          = 0.0f;
        if ( pCamera == nullptr )
            return;
        CameraComponent* pTargetCamera = findTargetCamera( _viewTarget );
        // 타깃이 없으면 지금 화면을 지킨다(타깃이 사라진 프레임에 원점으로 튀지 않게).
        CameraPose incoming = _blender.hasPose() ? _blender.getPose() : CameraPoseUtil::makePoseFromCamera( *pCamera );
        if ( pTargetCamera != nullptr )
            incoming = CameraPoseUtil::makePoseFromCamera( *pTargetCamera );
        CameraPose             liveFrom{};
        const CameraPose*      pLiveFrom       = nullptr;
        const CameraComponent* pPreviousCamera = _blender.isFromLive() ? findTargetCamera( _previousTarget ) : nullptr;
        if ( pPreviousCamera != nullptr )
        {
            liveFrom  = CameraPoseUtil::makePoseFromCamera( *pPreviousCamera );
            pLiveFrom = &liveFrom;
        }
        const CameraPose& blended = _blender.step( deltaTime, incoming, pLiveFrom );
        _impulseListener.step( deltaTime );
        const CameraPose output = _impulseListener.isActive() ? applyCameraShake( blended, _impulseListener.computeOffset( blended._position ) ) : blended;
        CameraPoseUtil::applyToCamera( *pCamera, output );
        // 블렌드 없는 전환, 또는 타깃 카메라 자신의 컷(디렉터의 컷 전환)은 이 카메라의 화면도 끊는다.
        const bool bTargetCut = pTargetCamera != nullptr && pTargetCamera->consumeCut();
        if ( _blender.consumeCut() || bTargetCut )
            pCamera->markCut();
    }
} // namespace sw
