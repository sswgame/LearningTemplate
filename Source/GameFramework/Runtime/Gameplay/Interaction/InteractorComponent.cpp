#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractorComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"

#include "Engine/Animation/Component/MotionWarpingComponent.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSystem.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneSpatialGrid.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionAuthority.h"

namespace sw
{
    namespace
    {
        struct InteractorComponentInternal
        {
            /**
             * @brief 상호작용 정의가 이름 붙인 이펙트를 하는 쪽에 겁니다(건 길 @p sourceName). 이름이 비면 아무것도 하지 않습니다.
             * @details 하는 쪽에 AbilityComponent 가 없는데 정의가 이펙트를 적었으면 데이터와 개체가 맞지 않는 것이라 오류다(대신 아무 일도 없는 것으로 넘기지 않는다).
             */
            static void applyInteractionEffect( GameObject& owner, const InteractionDef& def, const hashed_string& effectID, const hashed_string& sourceName )
            {
                if ( effectID.size() == 0 )
                    return;
                AbilityComponent* const pAbility = owner.getComponent<AbilityComponent>();
                if ( pAbility == nullptr )
                {
                    SW_LOG_ERROR_ID( "Interactor.EffectWithoutAbilityComponent", "interaction '%#' applies effect '%#' but the interactor '%#' has no AbilityComponent", def._id.c_str(),
                                     effectID.c_str(), owner.getName().c_str() );
                    return;
                }
                AbilityEffectSpec spec = pAbility->makeOutgoingSpecByID( effectID ); // 카탈로그에 없으면 경고를 남기고 빈 스펙이다
                if ( spec.isValid() == false )
                    return;
                spec._context._sourceName = sourceName;
                (void)pAbility->applyGameplayEffectSpecToTarget( spec, pAbility ); // 거부되면 까닭이 대상의 이펙트 기록에 남는다
            }

            /** @brief 상호작용 @p def 의 건 길입니다 — 시작 효과는 `interaction:<id>`(끝날 때 지운다), 완료 · 취소 효과는 뒤에 `:complete` · `:cancel`(남는다). */
            static hashed_string makeSourceName( const InteractionDef& def, const utf8* pSuffix )
            {
                return hashed_string( string( "interaction:" ) + def._id.c_str() + pSuffix );
            }

            /** @brief 진행 동안만 걸린 시작 효과를 지웁니다(`_bEndStartEffect`, 채널링 중 이동 금지 같은 것). */
            static void endStartEffect( GameObject& owner, const InteractionDef& def )
            {
                if ( def._startEffect.size() == 0 || def._bEndStartEffect == SW_FALSE )
                    return;
                if ( AbilityComponent* const pAbility = owner.getComponent<AbilityComponent>() )
                    (void)pAbility->removeActiveEffectsFromSource( makeSourceName( def, "" ) );
            }

            /**
             * @brief 하는 쪽에 모션 워핑이 있으면 대상의 맞춤 지점을 마커 이름(없으면 "Interaction")의 워프 목표로 넣습니다. 상호작용 클립의
             *        `MotionWarp` 창이 그 위치와 방향에 닿도록 이동을 보정합니다.
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

            /**
             * @brief 단계에 몽타주(상태 · 클립 이름)가 있으면 하는 쪽의 애니메이터가 한 번 재생합니다. 처음 틀 때 그 전 상태를 @p inoutResumeState 에 적어 둡니다.
             * @details 상태는 그래프에서 끝나면 나가는 전환을 가진 것이어야 합니다 — 몽타주가 끝난 뒤 돌아가는 일은 애니메이터 그래프가 한다. 취소만 `restoreMontage` 가 되돌린다.
             */
            static void playStepMontage( GameObject& interactor, const InteractionStepDef& step, hashed_string& inoutResumeState )
            {
                if ( step._montage.empty() )
                    return;
                SkeletalAnimatorComponent* pAnimator = interactor.getComponent<SkeletalAnimatorComponent>();
                if ( pAnimator == nullptr )
                    return;
                if ( inoutResumeState.empty() )
                    inoutResumeState = pAnimator->getCurrentStateName();
                (void)pAnimator->play( step._montage, false ); // 그래프에 없는 이름이면 재생하지 않는다 — 상호작용은 애니메이션 없이도 간다
            }

            /** @brief 취소된 상호작용이 튼 몽타주를 끊고 그 전 상태로 돌아갑니다. */
            static void restoreMontage( GameObject& interactor, hashed_string& inoutResumeState )
            {
                if ( inoutResumeState.empty() )
                    return;
                SkeletalAnimatorComponent* pAnimator = interactor.getComponent<SkeletalAnimatorComponent>();
                if ( pAnimator != nullptr )
                    (void)pAnimator->play( inoutResumeState, true ); // 돌아갈 상태가 그래프에서 사라졌으면 그대로 둔다
                inoutResumeState = hashed_string{};
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
        , _resumeState{}
        , _montageStep{ constant::invalid_index::kInt32 }
        , _pairedSessionID{ 0 }
        , _prompt{}
        , _listCandidate{}
        , _listCandidateComponent{}
        , _listSessionEvent{}
        , _focus{}
        , _focusComponent{}
        , _bHeld{ SW_FALSE }
        , _bWasHeld{ SW_FALSE }
        , _bPressed{ SW_FALSE }
        , _bPairedStartPending{ SW_FALSE }
        , _bSpatialIndexUser{ SW_FALSE }
        , _listGridTarget{}
    {
    }

    void InteractorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostPhysics );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
        {
            pManager->getSubsystem<SceneSpatialGrid>().addUser( InteractableComponent::getSpatialLayer( _space ) );
            _bSpatialIndexUser = SW_TRUE;
        }
    }

    void InteractorComponent::onEndPlay()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
            setFocus( *pManager, GameObjectHandle{}, TComponentHandle<InteractableComponent>{} );
        if ( pManager != nullptr && _bSpatialIndexUser == SW_TRUE )
            pManager->getSubsystem<SceneSpatialGrid>().removeUser( InteractableComponent::getSpatialLayer( _space ) );
        _bSpatialIndexUser = SW_FALSE;
        Component::onEndPlay();
    }

    void InteractorComponent::setSpace( InteractionSpace space )
    {
        if ( space == _space )
            return;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        // 세어진 공간과 `_space` 가 어긋나면 뗄 때 다른 공간의 수를 줄인다. 사용자를 함께 옮긴다.
        if ( pManager != nullptr && _bSpatialIndexUser == SW_TRUE )
        {
            SceneSpatialGrid& grid = pManager->getSubsystem<SceneSpatialGrid>();
            grid.addUser( InteractableComponent::getSpatialLayer( space ) );
            grid.removeUser( InteractableComponent::getSpatialLayer( _space ) );
        }
        _space = space;
    }

    void InteractorComponent::setInput( bool bHeld )
    {
        _bHeld = bHeld ? SW_TRUE : SW_FALSE;
        if ( bHeld && _bWasHeld == SW_FALSE )
            _bPressed = SW_TRUE;
        _bWasHeld = _bHeld;
    }

    void InteractorComponent::cancelInteraction()
    {
        _session.cancel();
    }

    void InteractorComponent::makeView( InteractionView& outViewer ) const
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

    void InteractorComponent::gatherCandidates( GameObjectManager& manager, const GameObject& owner, const InteractionView& viewer )
    {
        _listCandidate.clear();
        _listCandidateComponent.clear();
        // 하는 쪽이 여럿이면 씬 공간 색인(`SceneSpatialGrid` — 대상마다 반 크기 = 도달 거리 상자)에서 눈이 든 상자만 받는다. 아니면 등록된 대상 목록(`ComponentRegistry`)을
        // 한 번 돈다. 둘 다 씬 전체는 돌지 않고, 닿는지 · 쓸 수 있는지는 `considerCandidate` 한 곳이 현재 위치로 판정한다. 색인의 자리는 지난 `SpatialIndex` 단계의 것이다.
        const SceneSpatialGrid&     grid  = manager.getSubsystem<SceneSpatialGrid>();
        const SpatialGridLayerDesc& layer = InteractableComponent::getSpatialLayer( viewer._space );
        if ( grid.isActive( layer ) )
        {
            float32 planeX{ 0.0f };
            float32 planeY{ 0.0f };
            SceneSpatialGrid::projectToPlane( layer, viewer._position, planeX, planeY );
            grid.query( layer, planeX, planeY, planeX, planeY, _listGridTarget );
            for ( Component* pTarget : _listGridTarget )
            {
                // 이 층의 열쇠는 대상 자신이다(`InteractableComponent::onRegister`). 해제가 항목을 빼므로 살아 있다.
                considerCandidate( *static_cast<InteractableComponent*>( pTarget ), owner, viewer );
            }
            return;
        }
        for ( InteractableComponent* pInteractable : manager.getComponentRegistry().getAll<InteractableComponent>() )
        {
            if ( pInteractable != nullptr )
                considerCandidate( *pInteractable, owner, viewer );
        }
    }

    void InteractorComponent::considerCandidate( InteractableComponent& interactable, const GameObject& owner, const InteractionView& viewer )
    {
        const GameObject*     pObject = interactable.getOwner();
        const SceneComponent* pScene  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
        const bool            bUsable = pScene != nullptr && pObject != &owner && interactable.isPendingDestroy() == false && interactable.isActive() &&
                             pObject->isActiveInHierarchy() && interactable.getDefinition() != nullptr;
        if ( bUsable == false || interactable.isAvailableFor( owner ) == false )
            return;
        const InteractionDef* pDef = interactable.getDefinition();
        InteractionCandidate  candidate;
        candidate._position             = pScene->getWorldPosition();
        candidate._objectID             = pObject->getObjectID();
        candidate._maxDistance          = pDef->_maxDistance;
        candidate._maxAngle             = pDef->_maxAngle;
        candidate._priority             = interactable.getPriority();
        candidate._bRequiresLineOfSight = pDef->_bLineOfSight;
        float32 distance{ 0.0f };
        if ( InteractionView::isInReach( viewer, candidate, distance ) == false )
            return;
        _listCandidate.push_back( candidate );
        _listCandidateComponent.push_back( TComponentHandle<InteractableComponent>{ &interactable } );
    }

    void InteractorComponent::setFocus( GameObjectManager& manager, GameObjectHandle focus, TComponentHandle<InteractableComponent> focusComponent )
    {
        if ( focusComponent == _focusComponent )
            return;
        InteractableComponent* pOld = manager.resolveComponent( _focusComponent );
        if ( pOld != nullptr )
            pOld->setHighlightRequested( false );
        _focus                      = focus;
        _focusComponent             = focusComponent;
        InteractableComponent* pNew = manager.resolveComponent( _focusComponent );
        if ( pNew != nullptr )
            pNew->setHighlightRequested( true );
    }

    void InteractorComponent::finishSession( GameObjectManager& manager, const GameObject& owner )
    {
        const TComponentHandle<InteractableComponent> target     = _focusComponent;
        const GameObjectHandle                        interactor = owner.getHandle();
        GameObjectManager*                            pManager   = &manager;
        manager.executeOrDeferPostTick( [pManager, target, interactor]()
        {
            InteractableComponent* pInteractable = pManager->resolveComponent( target );
            const GameObject*      pInteractor   = pManager->resolveGameObject( interactor );
            if ( pInteractable != nullptr && pInteractor != nullptr )
                pInteractable->completeInteraction( *pInteractor );
        } );
    }

    void InteractorComponent::deferPairedAnimationStart( GameObjectManager& manager, const InteractionStepDef& step )
    {
        const TComponentHandle<InteractorComponent>   self{ this };
        const TComponentHandle<InteractableComponent> target   = _focusComponent;
        GameObjectManager*                            pManager = &manager;
        _bPairedStartPending                                   = SW_TRUE;
        // 세션은 다른 오브젝트(대상)의 애니메이터를 바꾸므로 틱 워커가 아닌 틱 뒤 게임 스레드에서 시작한다.
        manager.executeOrDeferPostTick( [pManager, self, target, step]()
        {
            InteractorComponent* pSelf = pManager->resolveComponent( self );
            if ( pSelf != nullptr )
                pSelf->startPairedAnimation( *pManager, target, step );
        } );
    }

    void InteractorComponent::startPairedAnimation( GameObjectManager& manager, TComponentHandle<InteractableComponent> target, const InteractionStepDef& step )
    {
        // 같은 틱에 상호작용이 취소됐으면 시작하지 않는다(누름 단계는 누름으로 이미 끝났을 수 있다 — 끝남은 막지 않는다).
        if ( _bPairedStartPending == SW_FALSE )
            return;
        _bPairedStartPending                       = SW_FALSE;
        GameObject*                pOwner          = getOwner();
        InteractableComponent*     pTarget         = manager.resolveComponent( target );
        GameObject*                pTargetObject   = pTarget != nullptr ? pTarget->getOwner() : nullptr;
        SkeletalAnimatorComponent* pAnimator       = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        SkeletalAnimatorComponent* pTargetAnimator = pTargetObject != nullptr ? pTargetObject->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( pAnimator == nullptr || pTargetAnimator == nullptr )
        {
            SW_LOG_ERROR_ID( "Interaction.InteractionPairedAnimationNeeds", "'%#': interaction paired animation needs a SkeletalAnimatorComponent on the interactor and the interactable", pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
            _session.cancel();
            return;
        }
        vector<shared_ptr<const PairedAnimationDef>> listDef;
        listDef.reserve( step._listPairedAnimationPath.size() );
        for ( const string& path : step._listPairedAnimationPath )
        {
            shared_ptr<const PairedAnimationDef> def = SharedAssetCache<PairedAnimationDef>::acquire( path );
            if ( def == nullptr )
            {
                // 읽기 오류는 로더가 줄마다 남겼다. 남은 세트로 고르지 않는다.
                SW_LOG_ERROR_ID( "Interaction.InteractionPairedAnimationCould", "'%#': interaction paired animation '%#' could not be loaded", pOwner->getName().c_str(), path.c_str() );
                _session.cancel();
                return;
            }
            listDef.push_back( std::move( def ) );
        }
        const vector<PairedAnimationBinding> listBinding = {
            PairedAnimationBinding{  step._interactorRole,       TComponentHandle<SkeletalAnimatorComponent>( pAnimator )},
            PairedAnimationBinding{step._interactableRole, TComponentHandle<SkeletalAnimatorComponent>( pTargetAnimator )},
        };
        uint32                           sessionID = 0;
        uint32                           setIndex  = 0;
        const PairedAnimationStartResult result    = PairedAnimationSystem::getOrCreate( manager ).startMatchingSession( manager, listDef, listBinding, sessionID, setIndex );
        // 거절(자리에 맞는 세트 없음, 너무 멂, 다른 세션 중)은 진행 중인 상호작용을 취소한다 — 애니메이션 없이 진행하지 않는다. 데이터 결함은 세션이 오류로 남겼다.
        if ( result != PairedAnimationStartResult::Started )
        {
            SW_LOG_WARNING_ID( "Interaction.InteractionPairedAnimationRefused", "'%#': interaction paired animation was refused (result %#)", pOwner->getName().c_str(), static_cast<uint32>( result ) );
            _session.cancel();
            return;
        }
        // 이미 끝난 상호작용(누름 단계)의 세션은 취소할 일이 없다 — 다음 상호작용의 취소가 이 세션을 끊지 않게 적지 않는다.
        if ( _session.isActive() )
            _pairedSessionID = sessionID;
    }

    void InteractorComponent::deferPairedAnimationAbort( GameObjectManager& manager )
    {
        _bPairedStartPending = SW_FALSE;
        if ( _pairedSessionID == 0 )
            return;
        const uint32       sessionID = _pairedSessionID;
        GameObjectManager* pManager  = &manager;
        _pairedSessionID             = 0;
        manager.executeOrDeferPostTick( [pManager, sessionID]()
        {
            PairedAnimationSystem* pSystem = PairedAnimationSystem::find( *pManager );
            if ( pSystem != nullptr )
                pSystem->abortSession( *pManager, sessionID );
        } );
    }

    void InteractorComponent::updateActiveSession( GameObjectManager& manager, const InteractionView& viewer, float32 deltaTime, bool bHeld, bool bPressed )
    {
        InteractableComponent* pTarget = manager.resolveComponent( _focusComponent );
        bool                   bKeep   = pTarget != nullptr && pTarget->isActive() && pTarget->getDefinition() != nullptr;
        if ( bKeep )
        {
            InteractionCandidate candidate;
            const GameObject*    pTargetObject = pTarget->getOwner();
            candidate._position                = pTargetObject->getPrimarySceneComponent() != nullptr ? pTargetObject->getPrimarySceneComponent()->getWorldPosition() : float3{};
            candidate._maxDistance             = pTarget->getDefinition()->_maxDistance * _keepDistanceScale;
            float32 distance{ 0.0f };
            bKeep = InteractionView::isInReach( viewer, candidate, distance );
        }
        if ( bKeep )
            _session.update( deltaTime, bHeld, bPressed );
        else
            _session.cancel();
    }

    void InteractorComponent::updateFocusAndBegin( GameObjectManager& manager, GameObject& owner, const InteractionView& viewer, bool bPressed )
    {
        const WorldLineOfSightQuery lineOfSight{ manager };
        const optional<int32>       best = InteractionView::selectBest( viewer, _listCandidate, _bUseLineOfSight ? &lineOfSight : nullptr );
        if ( best.has_value() )
        {
            const size_t index = static_cast<size_t>( *best );
            setFocus( manager, GameObjectHandle::make( _listCandidate[index]._objectID ), _listCandidateComponent[index] );
        }
        else
        {
            setFocus( manager, GameObjectHandle{}, TComponentHandle<InteractableComponent>{} );
        }
        InteractableComponent* pTarget = best.has_value() ? manager.resolveComponent( _focusComponent ) : nullptr;
        if ( bPressed == false || pTarget == nullptr )
            return;
        const InteractionDef*  pDef       = pTarget->getDefinition();
        IInteractionAuthority* pAuthority = pDef->_authority == InteractionAuthority::Server ? game::findSubsystem<IInteractionAuthority>() : nullptr;
        InteractionRequest     request;
        request._interactor   = owner.getHandle();
        request._interactable = _focus;
        request._interaction  = pDef->_id;
        const bool bAllowed   = pAuthority == nullptr || pAuthority->canBeginInteraction( request );
        if ( bAllowed && _session.begin( pTarget->shareDefinition(), static_cast<uint32>( owner.getObjectID() ) ) )
            InteractorComponentInternal::setAlignmentWarpTarget( owner, *pTarget, *pDef );
    }

    void InteractorComponent::handleSessionEvents( GameObjectManager& manager, GameObject& owner )
    {
        _listSessionEvent.clear();
        _session.pollEvents( _listSessionEvent );
        const InteractableComponent* pEventTarget = manager.resolveComponent( _focusComponent );
        const InteractionDef*        pEventDef    = pEventTarget != nullptr ? pEventTarget->getDefinition() : nullptr;
        for ( const InteractionSessionEvent event : _listSessionEvent )
        {
            if ( event == InteractionSessionEvent::Started )
            {
                _montageStep = 0;
                if ( pEventDef != nullptr )
                    InteractorComponentInternal::applyInteractionEffect( owner, *pEventDef, pEventDef->_startEffect, InteractorComponentInternal::makeSourceName( *pEventDef, "" ) );
            }
            else if ( event == InteractionSessionEvent::StepCompleted )
                ++_montageStep;
            const bool bStepStarts       = event == InteractionSessionEvent::Started || event == InteractionSessionEvent::StepCompleted;
            const bool bStartMontageStep = bStepStarts && pEventDef != nullptr && 0 <= _montageStep && _montageStep < static_cast<int32>( pEventDef->_listStep.size() );
            if ( bStartMontageStep )
            {
                const InteractionStepDef& step = pEventDef->_listStep[static_cast<size_t>( _montageStep )];
                InteractorComponentInternal::playStepMontage( owner, step, _resumeState );
                if ( step._listPairedAnimationPath.empty() == false )
                    deferPairedAnimationStart( manager, step );
            }
            if ( event == InteractionSessionEvent::Cancelled && pEventDef != nullptr )
            {
                InteractorComponentInternal::endStartEffect( owner, *pEventDef );
                InteractorComponentInternal::applyInteractionEffect( owner, *pEventDef, pEventDef->_cancelEffect, InteractorComponentInternal::makeSourceName( *pEventDef, ":cancel" ) );
            }
            if ( event == InteractionSessionEvent::Completed && pEventDef != nullptr )
            {
                InteractorComponentInternal::endStartEffect( owner, *pEventDef );
                InteractorComponentInternal::applyInteractionEffect( owner, *pEventDef, pEventDef->_completeEffect, InteractorComponentInternal::makeSourceName( *pEventDef, ":complete" ) );
            }
            if ( event == InteractionSessionEvent::Cancelled )
            {
                InteractorComponentInternal::restoreMontage( owner, _resumeState );
                _montageStep = constant::invalid_index::kInt32;
                deferPairedAnimationAbort( manager );
            }
            if ( event == InteractionSessionEvent::Completed )
            {
                // 페어드 애니메이션은 상호작용이 끝나도 자기 타임라인 끝까지 간다.
                _resumeState     = hashed_string{};
                _montageStep     = constant::invalid_index::kInt32;
                _pairedSessionID = 0;
                finishSession( manager, owner );
            }
        }
    }

    void InteractorComponent::updatePrompt( GameObjectManager& manager )
    {
        const InteractableComponent* pFocus = manager.resolveComponent( _focusComponent );
        const InteractionStepDef*    pStep  = _session.getStep();
        const InteractionDef*        pDef   = pFocus != nullptr ? pFocus->getDefinition() : nullptr;
        _prompt                             = InteractionPrompt{};
        _prompt._target                     = _focus;
        _prompt._bVisible                   = pDef != nullptr ? SW_TRUE : SW_FALSE;
        _prompt._bInteracting               = _session.isActive() ? SW_TRUE : SW_FALSE;
        _prompt._progress                   = _session.getStepProgress();
        _prompt._stepIndex                  = _session.isActive() ? _session.getStepIndex() : 0;
        _prompt._stepCount                  = pDef != nullptr ? static_cast<int32>( pDef->_listStep.size() ) : 0;
        const bool bFallbackToFirstStep     = pStep == nullptr && pDef != nullptr && pDef->_listStep.empty() == false;
        if ( bFallbackToFirstStep )
            pStep = &pDef->_listStep.front();
        if ( pStep != nullptr )
        {
            _prompt._prompt = pStep->_prompt;
            _prompt._mode   = pStep->_mode;
        }
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

        InteractionView viewer;
        makeView( viewer );
        gatherCandidates( *pManager, *pOwner, viewer );
        if ( _session.isActive() )
            updateActiveSession( *pManager, viewer, deltaTime, bHeld, bPressed );
        else
            updateFocusAndBegin( *pManager, *pOwner, viewer, bPressed );
        handleSessionEvents( *pManager, *pOwner );
        updatePrompt( *pManager );
    }
} // namespace sw
