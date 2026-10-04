#include "pch.h"

#include "GameFramework/Interaction/InteractableComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Framework/GameEventUtil.h"
#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Interaction/InteractionAuthority.h"

namespace sw
{
    SW_LOG_CALLER( "InteractableComponent" );
} // namespace sw

namespace sw
{
    InteractableComponent::InteractableComponent()
        : _interactionId{}
        , _catalogPath{}
        , _alignmentOffset{}
        , _alignmentYaw{ 0.0f }
        , _lastInteractor{}
        , _cooldownRemaining{ 0.0f }
        , _priority{ 0 }
        , _bEnabled{ true }
        , _overrideDef{}
        , _pDef{ nullptr }
        , _bHighlightRequested{ SW_FALSE }
        , _bHasOverride{ SW_FALSE }
    {
    }

    void InteractableComponent::resolveDefinition()
    {
        if ( _bHasOverride == SW_TRUE )
        {
            _pDef = &_overrideDef;
            return;
        }
        _pDef = nullptr;
        if ( _interactionId.empty() )
            return;
        const InteractionCatalog* pCatalog = InteractionCatalog::findShared( _catalogPath.empty() ? string_view( InteractionCatalog::kDefaultPath ) : string_view( _catalogPath ) );
        _pDef                              = pCatalog != nullptr ? pCatalog->findInteraction( _interactionId ) : nullptr;
        if ( _pDef == nullptr )
            SW_LOG_ERROR( "Interaction '%#' is not in %#", _interactionId.c_str(), _catalogPath.empty() ? InteractionCatalog::kDefaultPath : _catalogPath.c_str() );
    }

    void InteractableComponent::onPostLoad()
    {
        Component::onPostLoad();
        resolveDefinition();
    }

    void InteractableComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _pDef == nullptr )
            resolveDefinition();
    }

    void InteractableComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        resolveDefinition();
    }

    void InteractableComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _cooldownRemaining > 0.0f )
            _cooldownRemaining = MathUtil::max( 0.0f, _cooldownRemaining - deltaTime );
    }

    void InteractableComponent::setDefinition( const InteractionDef& def )
    {
        _overrideDef  = def;
        _bHasOverride = SW_TRUE;
        _pDef         = &_overrideDef;
    }

    const InteractionDef* InteractableComponent::getDefinition() const { return _bHasOverride == SW_TRUE ? &_overrideDef : _pDef; }

    void InteractableComponent::setInteractionId( const hashed_string& id )
    {
        _interactionId = id;
        resolveDefinition();
    }

    void InteractableComponent::setAlignment( const float3& localOffset, float32 localYawRadians )
    {
        _alignmentOffset = localOffset;
        _alignmentYaw    = localYawRadians;
    }

    bool InteractableComponent::isAvailableFor( const GameObject& interactor ) const
    {
        const InteractionDef* pDef = getDefinition();
        return pDef != nullptr && _bEnabled && _cooldownRemaining <= 0.0f && pDef->allowsInteractor( interactor.getTags() );
    }

    void InteractableComponent::computeAlignmentPoint( float3& outPosition, float32& outYaw ) const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
        {
            outPosition = _alignmentOffset;
            outYaw      = _alignmentYaw;
            return;
        }
        const float4x4 world = pScene->getWorldMatrix();
        outPosition          = float3::transform( _alignmentOffset, world );
        // 오브젝트의 요(월드 회전의 앞 방향)에 로컬 요를 더한다.
        const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
        outYaw               = MathUtil::atan2( forward._x, forward._z ) + _alignmentYaw;
    }

    InteractionHighlight InteractableComponent::getHighlightRequest() const
    {
        const InteractionDef* pDef = getDefinition();
        if ( pDef == nullptr || _bHighlightRequested.load( std::memory_order_relaxed ) == SW_FALSE )
            return InteractionHighlight::None;
        return pDef->_highlight;
    }

    void InteractableComponent::completeInteraction( const GameObject& interactor )
    {
        const InteractionDef* pDef   = getDefinition();
        GameObject*           pOwner = getOwner();
        if ( pDef == nullptr || pOwner == nullptr )
            return;
        _cooldownRemaining              = pDef->_cooldown;
        GimmickSensorComponent* pSensor = pOwner->getComponent<GimmickSensorComponent>();
        if ( pSensor != nullptr )
            pSensor->notifyUsed();
        InteractionCompletedEvent event;
        event._request._interactor        = interactor.getHandle();
        event._request._interactable      = pOwner->getHandle();
        event._request._interaction       = pDef->_id;
        IInteractionAuthority* pAuthority = game::getService<IInteractionAuthority>();
        if ( pAuthority != nullptr )
            pAuthority->notifyInteractionCompleted( event._request );
        GameEventUtil::send( event );
    }
} // namespace sw
