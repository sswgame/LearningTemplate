#include "pch.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractableComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Socket/SocketSetComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionAuthority.h"

namespace sw
{
    SW_LOG_CALLER( "InteractableComponent" );
} // namespace sw

namespace sw
{
    InteractableComponent::InteractableComponent()
        : _interactionID{}
        , _catalogPath{}
        , _lastInteractor{}
        , _cooldownRemaining{ 0.0f }
        , _priority{ 0 }
        , _bEnabled{ true }
        , _overrideDef{}
        , _pDef{ nullptr }
        , _seenCatalogReloadCount{ 0 }
        , _completedCount{ 0 }
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
        _pDef                   = nullptr;
        _seenCatalogReloadCount = InteractionCatalog::getSharedReloadCount();
        if ( _interactionID.empty() )
            return;
        if ( _catalogPath.empty() )
        {
            SW_LOG_ERROR( "Interaction '%#' has no catalog path - set Catalog", _interactionID.c_str() );
            return;
        }
        const InteractionCatalog* pCatalog = InteractionCatalog::findShared( _catalogPath );
        _pDef                              = pCatalog != nullptr ? pCatalog->findInteraction( _interactionID ) : nullptr;
        if ( _pDef == nullptr )
            SW_LOG_ERROR( "Interaction '%#' is not in %#", _interactionID.c_str(), _catalogPath.c_str() );
    }

    void InteractableComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<InteractableComponent>( this );
    }

    void InteractableComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<InteractableComponent>( this );
        Component::onUnregister( manager );
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
        // 상호작용 테이블 파일이 바뀌었으므로 새 테이블에서 정의를 다시 찾는다. 이전 테이블은 캐시가 유지하므로, 이 프레임에 이전 정의를 읽은 쪽도 안전하다.
        if ( _bHasOverride == SW_FALSE && _seenCatalogReloadCount != InteractionCatalog::getSharedReloadCount() )
            resolveDefinition();
        if ( _cooldownRemaining > 0.0f )
            _cooldownRemaining = MathUtil::max( 0.0f, _cooldownRemaining - deltaTime );
    }

    void InteractableComponent::setDefinition( const InteractionDef& def )
    {
        _overrideDef  = def;
        _bHasOverride = SW_TRUE;
        _pDef         = &_overrideDef;
    }

    const InteractionDef* InteractableComponent::getDefinition() const
    {
        return _bHasOverride == SW_TRUE ? &_overrideDef : _pDef;
    }

    void InteractableComponent::setInteractionID( const hashed_string& id )
    {
        _interactionID = id;
        resolveDefinition();
    }

    void InteractableComponent::setCatalogPath( string_view path )
    {
        _catalogPath = string( path );
        resolveDefinition();
    }

    bool InteractableComponent::isAvailableFor( const GameObject& interactor ) const
    {
        const InteractionDef* pDef = getDefinition();
        return pDef != nullptr && _bEnabled && _cooldownRemaining <= 0.0f && pDef->allowsInteractor( interactor.getTags() );
    }

    bool InteractableComponent::computeAlignmentPoint( float3& outPosition, float32& outYaw ) const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
        {
            outPosition = float3{};
            outYaw      = 0.0f;
            return false;
        }
        // 정의의 마커를 이 오브젝트의 소켓과 마커 테이블에서 찾는다. 마커의 +Z 가 하는 쪽이 볼 방향이다. 없으면 오브젝트의 원점과 앞 방향을 쓴다.
        const InteractionDef* pDef  = getDefinition();
        float4x4              world = pScene->getWorldMatrix();
        const bool            bMarker =
            pDef != nullptr && pDef->_alignmentMarker.empty() == false && SocketLookupUtil::findSocketWorldTransform( *pOwner, pDef->_alignmentMarker, world );
        if ( bMarker == false )
            world = pScene->getWorldMatrix();
        outPosition          = world.getTranslation();
        const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
        outYaw               = MathUtil::atan2( forward._x, forward._z );
        return bMarker;
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
        _cooldownRemaining = pDef->_cooldown;
        _completedCount.fetch_add( 1, std::memory_order_relaxed );
        InteractionCompletedEvent event;
        event._request._interactor        = interactor.getHandle();
        event._request._interactable      = pOwner->getHandle();
        event._request._interaction       = pDef->_id;
        IInteractionAuthority* pAuthority = game::findSubsystem<IInteractionAuthority>();
        if ( pAuthority != nullptr )
            pAuthority->notifyInteractionCompleted( event._request );
        GameEventUtil::send( event );
    }
} // namespace sw
