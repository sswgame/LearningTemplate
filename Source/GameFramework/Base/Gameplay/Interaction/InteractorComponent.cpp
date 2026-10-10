#include "pch.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractorComponent.h"

#include "Engine/Object/Animation/MotionWarpingComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionAuthority.h"

namespace sw
{
    namespace
    {
        struct InteractorComponentInternal
        {

            /**
             * @brief 하는 쪽에 모션 워핑이 있으면 대상의 맞춤 지점을 마커 이름(없으면 "Interaction")의 워프 목표로 넣습니다 — 상호작용 클립의
             *        `MotionWarp` 창이 그 자리 · 방향에 닿게 휩니다.
             */
            static void setAlignmentWarpTarget( GameObject& interactor, const InteractableComponent& target, const InteractionDef& def )
            {
                MotionWarpingComponent* pWarping = interactor.getComponent<MotionWarpingComponent>();
                if ( pWarping == nullptr )
                    return;
                static const hashed_string s_defaultTarget( "Interaction" );
                float3                     position{};
                float32                    yaw{ 0.0f };
                (void)target.computeAlignmentPoint( position, yaw );
                pWarping->setWarpTarget( def._alignmentMarker.empty() ? s_defaultTarget : def._alignmentMarker, position, yaw );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    InteractorComponent::InteractorComponent()
        : _space{ InteractionSpace::Space3D }
        , _eyeOffset{ 0.0f, 1.6f, 0.0f }
        , _facing2D{ 1.0f, 0.0f, 0.0f }
        , _bUseLineOfSight{ true }
        , _keepDistanceScale{ 1.25f }
        , _session{}
        , _prompt{}
        , _listCandidate{}
        , _listCandidateComponent{}
        , _listSessionEvent{}
        , _focus{}
        , _focusComponent{}
        , _bHeld{ SW_FALSE }
        , _bWasHeld{ SW_FALSE }
        , _bPressed{ SW_FALSE }
    {
    }

    void InteractorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostPhysics );
    }

    void InteractorComponent::onEndPlay()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
            setFocus( *pManager, GameObjectHandle{}, ComponentHandle{} );
        Component::onEndPlay();
    }

    void InteractorComponent::setInput( bool bHeld )
    {
        _bHeld = bHeld ? SW_TRUE : SW_FALSE;
        if ( bHeld && _bWasHeld == SW_FALSE )
            _bPressed = SW_TRUE;
        _bWasHeld = _bHeld;
    }

    void InteractorComponent::cancelInteraction() { _session.cancel(); }

    void InteractorComponent::makeViewer( InteractionViewer& outViewer ) const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        outViewer._space             = _space;
        outViewer._objectID          = pOwner != nullptr ? pOwner->getObjectID() : 0;
        if ( pScene == nullptr )
            return;
        outViewer._position = pScene->getWorldPosition() + _eyeOffset;
        outViewer._forward  = _space == InteractionSpace::Space2D ? _facing2D : float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pScene->getWorldMatrix() );
    }

    void InteractorComponent::gatherCandidates( GameObjectManager& manager, const GameObject& owner, const InteractionViewer& viewer )
    {
        _listCandidate.clear();
        _listCandidateComponent.clear();
        // 씬 전체를 훑지 않고 등록된 상호작용 대상만 본다(`ComponentRegistry`). 닿지 않는 것(거리 · 시야각)은 여기서 거른다 — 고르기와 같은 판정이다.
        for ( InteractableComponent* pInteractable : manager.getComponentRegistry().getAll<InteractableComponent>() )
        {
            const GameObject*     pObject = pInteractable != nullptr ? pInteractable->getOwner() : nullptr;
            const SceneComponent* pScene  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            const bool            bUsable = pScene != nullptr && pObject != &owner && pInteractable->isPendingDestroy() == false && pInteractable->isActive() &&
                                 pObject->isActiveInHierarchy() && pInteractable->getDefinition() != nullptr;
            if ( bUsable == false || pInteractable->isAvailableFor( owner ) == false )
                continue;
            const InteractionDef* pDef = pInteractable->getDefinition();
            InteractionCandidate  candidate;
            candidate._position             = pScene->getWorldPosition();
            candidate._objectID             = pObject->getObjectID();
            candidate._maxDistance          = pDef->_maxDistance;
            candidate._maxAngle             = pDef->_maxAngle;
            candidate._priority             = pInteractable->getPriority();
            candidate._bRequiresLineOfSight = pDef->_bLineOfSight;
            float32 distance{ 0.0f };
            if ( InteractionSelector::isInReach( viewer, candidate, distance ) == false )
                continue;
            _listCandidate.push_back( candidate );
            _listCandidateComponent.push_back( pInteractable->getHandle() );
        }
    }

    void InteractorComponent::setFocus( GameObjectManager& manager, GameObjectHandle focus, ComponentHandle focusComponent )
    {
        if ( focusComponent == _focusComponent )
            return;
        InteractableComponent* pOld = static_cast<InteractableComponent*>( manager.resolveComponent( _focusComponent ) );
        if ( pOld != nullptr )
            pOld->setHighlightRequested( false );
        _focus                      = focus;
        _focusComponent             = focusComponent;
        InteractableComponent* pNew = static_cast<InteractableComponent*>( manager.resolveComponent( _focusComponent ) );
        if ( pNew != nullptr )
            pNew->setHighlightRequested( true );
    }

    void InteractorComponent::finishSession( GameObjectManager& manager, const GameObject& owner )
    {
        const ComponentHandle  target     = _focusComponent;
        const GameObjectHandle interactor = owner.getHandle();
        GameObjectManager*     pManager   = &manager;
        manager.executeOrDeferPostTick( [pManager, target, interactor]()
        {
            InteractableComponent* pInteractable = static_cast<InteractableComponent*>( pManager->resolveComponent( target ) );
            const GameObject*      pInteractor   = pManager->resolveGameObject( interactor );
            if ( pInteractable != nullptr && pInteractor != nullptr )
                pInteractable->completeInteraction( *pInteractor );
        } );
    }

    void InteractorComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        const bool bPressed = _bPressed == SW_TRUE;
        const bool bHeld    = _bHeld == SW_TRUE;
        _bPressed           = SW_FALSE;

        InteractionViewer viewer;
        makeViewer( viewer );
        gatherCandidates( *pManager, *pOwner, viewer );

        if ( _session.isActive() )
        {
            // 진행 중에는 대상을 바꾸지 않는다 — 쓸 수 없게 됐거나 멀어지면 취소.
            InteractableComponent* pTarget = static_cast<InteractableComponent*>( pManager->resolveComponent( _focusComponent ) );
            bool                   bKeep   = pTarget != nullptr && pTarget->isActive() && pTarget->getDefinition() != nullptr;
            if ( bKeep )
            {
                InteractionCandidate candidate;
                const GameObject*    pTargetObject = pTarget->getOwner();
                candidate._position                = pTargetObject->getPrimarySceneComponent() != nullptr ? pTargetObject->getPrimarySceneComponent()->getWorldPosition() : float3{};
                candidate._maxDistance             = pTarget->getDefinition()->_maxDistance * _keepDistanceScale;
                float32 distance{ 0.0f };
                bKeep = InteractionSelector::isInReach( viewer, candidate, distance );
            }
            if ( bKeep )
                _session.update( deltaTime, bHeld, bPressed );
            else
                _session.cancel();
        }
        else
        {
            const WorldLineOfSightQuery lineOfSight{ *pManager };
            const int32                 best = InteractionSelector::selectBest( viewer, _listCandidate, _bUseLineOfSight ? &lineOfSight : nullptr );
            if ( best >= 0 )
            {
                const size_t index = static_cast<size_t>( best );
                setFocus( *pManager, GameObjectHandle::make( _listCandidate[index]._objectID ), _listCandidateComponent[index] );
            }
            else
            {
                setFocus( *pManager, GameObjectHandle{}, ComponentHandle{} );
            }
            InteractableComponent* pTarget = best >= 0 ? static_cast<InteractableComponent*>( pManager->resolveComponent( _focusComponent ) ) : nullptr;
            if ( bPressed && pTarget != nullptr )
            {
                const InteractionDef*  pDef       = pTarget->getDefinition();
                IInteractionAuthority* pAuthority = pDef->_authority == InteractionAuthority::Server ? game::getService<IInteractionAuthority>() : nullptr;
                InteractionRequest     request;
                request._interactor   = pOwner->getHandle();
                request._interactable = _focus;
                request._interaction  = pDef->_id;
                const bool bAllowed   = pAuthority == nullptr || pAuthority->canBeginInteraction( request );
                if ( bAllowed && _session.begin( pDef, static_cast<uint32>( pOwner->getObjectID() ) ) )
                    InteractorComponentInternal::setAlignmentWarpTarget( *pOwner, *pTarget, *pDef );
            }
        }

        _listSessionEvent.clear();
        _session.drainEvents( _listSessionEvent );
        for ( const InteractionSessionEvent event : _listSessionEvent )
        {
            if ( event == InteractionSessionEvent::Completed )
                finishSession( *pManager, *pOwner );
        }

        const InteractableComponent* pFocus = static_cast<const InteractableComponent*>( pManager->resolveComponent( _focusComponent ) );
        const InteractionStepDef*    pStep  = _session.getStep();
        const InteractionDef*        pDef   = pFocus != nullptr ? pFocus->getDefinition() : nullptr;
        _prompt                             = InteractionPrompt{};
        _prompt._target                     = _focus;
        _prompt._bVisible                   = pDef != nullptr ? SW_TRUE : SW_FALSE;
        _prompt._bInteracting               = _session.isActive() ? SW_TRUE : SW_FALSE;
        _prompt._progress                   = _session.getStepProgress();
        _prompt._stepIndex                  = _session.isActive() ? _session.getStepIndex() : 0;
        _prompt._stepCount                  = pDef != nullptr ? static_cast<int32>( pDef->_listStep.size() ) : 0;
        if ( pStep == nullptr && pDef != nullptr && pDef->_listStep.empty() == false )
            pStep = &pDef->_listStep.front();
        if ( pStep != nullptr )
        {
            _prompt._prompt = pStep->_prompt;
            _prompt._mode   = pStep->_mode;
        }
    }
} // namespace sw
