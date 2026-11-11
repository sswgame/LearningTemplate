#include "pch.h"

#include "GameFramework/Runtime/Character/Camera/CameraManagerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Scene/AnimationLOD.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Visual/MeshLODSystem.h"

#include "GameFramework/Runtime/Character/Camera/CameraPose.h"
#include "GameFramework/Runtime/Character/Camera/CameraSystem.h"
#include "GameFramework/Runtime/Character/Camera/CameraUserSettings.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct CameraManagerComponentInternal
        {
            /** @brief 질의 대상의 카메라 매니저입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const CameraManagerComponent* findTargetManager( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*             pTarget  = query.findTargetObject();
                const CameraManagerComponent* pManager = pTarget != nullptr ? pTarget->getComponent<CameraManagerComponent>() : nullptr;
                if ( pManager == nullptr )
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "camera manager" ) );
                return pManager;
            }

            /** @brief 감시 값 `CameraManager.Blending` 입니다. 대상 매니저가 블렌드 중이면 1 입니다. */
            [[nodiscard]] static bool readBlending( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CameraManagerComponent* pManager = findTargetManager( query, outValue );
                if ( pManager == nullptr )
                    return false;
                outValue.setNumber( pManager->isBlending() ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 감시 값 `CameraManager.PendingCamera` 입니다. 대기 뷰의 오브젝트 이름이고, 대기 뷰가 없으면 `none` 입니다. */
            [[nodiscard]] static bool readPendingCamera( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CameraManagerComponent* pManager = findTargetManager( query, outValue );
                if ( pManager == nullptr )
                    return false;
                const GameObject* pPending = pManager->hasPendingView() ? query._pManager->resolveGameObject( pManager->getPendingView()._camera ) : nullptr;
                outValue.setText( pPending != nullptr ? pPending->getName().c_str() : "none" );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( cameraManagerBlending, "CameraManager.Blending", "1 while the target's camera manager blends between views", &CameraManagerComponentInternal::readBlending, true, false );
    SW_AUTOMATION_QUERY_WATCH( cameraManagerPendingCamera, "CameraManager.PendingCamera", "Name of the object the target's camera manager is about to look through; none without a pending view",
                               &CameraManagerComponentInternal::readPendingCamera, true, false );
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
        , _pendingView{}
        , _pendingViewMulticast{}
        , _blender{}
        , _impulseListener{}
        , _shakeSubscription{}
        , _pendingDeltaTime{ 0.0f }
        , _bResolvePending{ false }
    {
        setCanEverTick( true );
    }

    void CameraManagerComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<CameraManagerComponent>( this ); // `isViewTarget` 과 카메라 시스템이 씬을 훑지 않고 찾는다
        (void)CameraSystem::findOrCreate( manager );
    }

    void CameraManagerComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<CameraManagerComponent>( this );
        CameraSystem::releaseIfUnused( manager );
        Component::onUnregister( manager );
    }

    void CameraManagerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 뷰 타깃을 움직이는 디렉터 · 리그(PostPhysics 까지)의 틱 다음에 읽는다 — 디렉터가 미룬 계산은 카메라 시스템이 이 매니저보다 먼저 푼다.
        setTickGroup( TickGroup::PostUpdate );
        updateCamera( 0.0f );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
            _shakeSubscription = pManager->getCameraRegistry().getShakeChannel().add(
                SW_DELEGATE_METHOD( CameraShakeRequestCallback, &CameraManagerComponent::onCameraShakeRequested, this ) );
    }

    void CameraManagerComponent::onEndPlay()
    {
        _shakeSubscription.reset();
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

    void CameraManagerComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const InputManager* pInput = _cycleAction.empty() == false ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput != nullptr && GameInputView( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() ).wasActionTriggered( _cycleAction ) )
            (void)cycleViewTarget( _cycleRole );
        updateCamera( context.getDeltaTime() );
    }

    void CameraManagerComponent::setViewTarget( const GameObjectHandle& target, const BlendCurveDef& blend )
    {
        if ( target == _viewTarget )
            return;
        // 블렌드가 없던 때 바꾸면 나가는 타깃 카메라를 블렌드 동안 계속 읽는다(움직이는 카메라에서 출발).
        _previousTarget = _blender.start( blend ) ? _viewTarget : GameObjectHandle{};
        _viewTarget     = target;
    }

    void CameraManagerComponent::blendFromPose( const CameraPose& from, const BlendCurveDef& blend )
    {
        _previousTarget = GameObjectHandle{};
        _blender.startFromPose( from, blend );
    }

    void CameraManagerComponent::setPendingView( const CameraPendingView& view )
    {
        _pendingView = view;
        _pendingViewMulticast.broadcast( _pendingView );
    }

    void CameraManagerComponent::clearPendingView()
    {
        if ( hasPendingView() == false )
            return;
        _pendingView = CameraPendingView{};
        _pendingViewMulticast.broadcast( _pendingView );
    }

    Subscription CameraManagerComponent::registerPendingViewListener( const OnCameraPendingViewDelegate& delegate )
    {
        return _pendingViewMulticast.add( delegate );
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

    bool CameraManagerComponent::isViewTarget( const GameObjectManager& manager, const GameObjectHandle& object )
    {
        for ( const CameraManagerComponent* pCameraManager : manager.getComponentRegistry().getAll<CameraManagerComponent>() )
        {
            if ( pCameraManager != nullptr && pCameraManager->isPendingDestroy() == false && pCameraManager->getViewTarget() == object )
                return true;
        }
        return false;
    }

    void CameraManagerComponent::updateCamera( float32 deltaTime )
    {
        _pendingDeltaTime += MathUtil::max( 0.0f, deltaTime );
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
        {
            _bResolvePending = true; // 카메라 시스템이 늦은 틱 뒤에 디렉터 다음으로 푼다
            return;
        }
        resolveCamera();
    }

    void CameraManagerComponent::resolvePendingCamera()
    {
        if ( _bResolvePending )
            resolveCamera();
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
        _bResolvePending           = false;
        if ( pCamera == nullptr )
            return;
        CameraComponent* pTargetCamera = findTargetCamera( _viewTarget );
        // 타깃이 없으면 지금 화면을 지킨다(타깃이 사라진 프레임에 원점으로 튀지 않게).
        CameraPose incoming = _blender.hasPose() ? _blender.getPose() : CameraPose::makePoseFromCamera( *pCamera );
        if ( pTargetCamera != nullptr )
            incoming = CameraPose::makePoseFromCamera( *pTargetCamera );
        CameraPose             liveFrom{};
        const CameraPose*      pLiveFrom       = nullptr;
        const CameraComponent* pPreviousCamera = _blender.isFromLive() ? findTargetCamera( _previousTarget ) : nullptr;
        if ( pPreviousCamera != nullptr )
        {
            liveFrom  = CameraPose::makePoseFromCamera( *pPreviousCamera );
            pLiveFrom = &liveFrom;
        }
        const CameraPose& blended = _blender.step( deltaTime, incoming, pLiveFrom );
        _impulseListener.step( deltaTime );
        const float32    shakeScale = CameraUserSettings::makeFromGlobalVariables()._shakeScale;
        const CameraPose output     = _impulseListener.isActive() ? applyCameraShake( blended, _impulseListener.computeOffset( blended._position ) * shakeScale ) : blended;
        CameraPose::applyToCamera( *pCamera, output );
        // 블렌드 없는 전환, 또는 타깃 카메라 자신의 컷(디렉터의 컷 전환)은 이 카메라의 화면도 끊는다.
        const bool bTargetCut = pTargetCamera != nullptr && pTargetCamera->consumeCut( CameraCutReader::Follow ); // 타깃 카메라가 자기 뷰로도 그려지면 렌더 쪽 표시는 그대로 남는다
        if ( _blender.consumeCut() || bTargetCut )
            pCamera->markCut();
        updatePendingLODView();
    }

    void CameraManagerComponent::updatePendingLODView()
    {
        // 대기 뷰(컷 프리롤)의 카메라를 애니메이션 LOD 에 넣는다 — 그 시점에서 보일 캐릭터가 컷 첫 프레임부터 제 LOD · 새 포즈다.
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        AnimationSystem&       animation      = pManager->getSubsystem<AnimationSystem>();
        MeshLODSystem&         meshLOD        = pManager->getSubsystem<MeshLODSystem>();
        const CameraComponent* pPendingCamera = hasPendingView() ? findTargetCamera( _pendingView._camera ) : nullptr;
        // 대기 뷰는 컷 뒤 이 매니저의 카메라가 그린다 — 그 카메라가 실제로 그린 뷰의 비로 투영한다(분할 화면 칸 · 세로 화면). 아직 그린 적이 없으면 비를 모르므로 미리 보지 않는다.
        const CameraComponent* pOwnCamera  = pOwner->getComponent<CameraComponent>();
        const float32          drawnAspect = pOwnCamera != nullptr ? pOwnCamera->getDrawnAspect() : 0.0f;
        if ( pPendingCamera == nullptr || drawnAspect <= 0.0f )
        {
            if ( animation.hasPendingLODView() )
                animation.clearPendingLODView();
            if ( meshLOD.hasPendingView() )
                meshLOD.clearPendingView();
            return;
        }
        const LODView pendingView = LODView::make( pPendingCamera->getViewProjectionMatrix( drawnAspect ), pPendingCamera->getCameraPosition() );
        animation.setPendingLODView( pendingView );
        // 메시 LOD 도 그 시점의 단계로 미리 둔다 — 컷 첫 프레임에 단계가 바뀌어 보이지 않게.
        meshLOD.setPendingView( pendingView );
    }
} // namespace sw
