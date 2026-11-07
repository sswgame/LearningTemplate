#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Socket/SocketSetComponent.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Graphics/Shader/Binding/GPUInstanceHighlight.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Rendering/MeshComponent.h"

#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionAuthority.h"

namespace sw
{
} // namespace sw

namespace sw
{
    InteractableComponent::InteractableComponent()
        : _interactionID{}
        , _catalogPath{}
        , _lastInteractor{}
        , _cooldownRemaining{ 0.0f }
        , _priority{ 0 }
        , _bEnabled{ SW_TRUE }
        , _overrideDef{}
        , _pCatalog{}
        , _pDef{ nullptr }
        , _seenCatalogReloadCount{ 0 }
        , _appliedHighlight{ 0 }
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
        _pDef = nullptr;
        _pCatalog.reset();
        _seenCatalogReloadCount = SharedAssetCache<InteractionCatalog>::getReloadCount();
        if ( _interactionID.empty() )
            return;
        if ( _catalogPath.empty() )
        {
            SW_LOG_ERROR_ID( "InteractableComponent.InteractionNoCatalogPath", "Interaction '%#' has no catalog path - set Catalog", _interactionID.c_str() );
            return;
        }
        _pCatalog = SharedAssetCache<InteractionCatalog>::acquire( _catalogPath );
        _pDef     = _pCatalog != nullptr ? _pCatalog->findInteraction( _interactionID ) : nullptr;
        if ( _pDef == nullptr )
            SW_LOG_ERROR_ID( "InteractableComponent.InteractionNot", "Interaction '%#' is not in %#", _interactionID.c_str(), _catalogPath.c_str() );
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
        // 상호작용 테이블 파일이 바뀌었으므로 새 테이블에서 정의를 다시 찾는다. 진행 중인 세션은 옛 테이블을 쥐고 있어(`shareDefinition`) 끝까지 옛 정의를 읽는다.
        if ( _bHasOverride == SW_FALSE && _seenCatalogReloadCount != SharedAssetCache<InteractionCatalog>::getReloadCount() )
            resolveDefinition();
        if ( _cooldownRemaining > 0.0f )
            _cooldownRemaining = MathUtil::max( 0.0f, _cooldownRemaining - deltaTime );
        applyHighlightToMeshes();
    }

    void InteractableComponent::applyHighlightToMeshes()
    {
        const InteractionHighlight   request = getHighlightRequest();
        const InteractionDef*        pDef    = getDefinition();
        const InstanceHighlightStyle style   = request == InteractionHighlight::Outline ? InstanceHighlightStyle::Outline
                                             : request == InteractionHighlight::Sense   ? InstanceHighlightStyle::Sense
                                                                                        : InstanceHighlightStyle::None;
        const float3                 color   = pDef != nullptr ? pDef->_highlightColor : float3{};
        const uint32                 packed  = GPUInstanceHighlight::pack( style, color );
        if ( packed == _appliedHighlight )
            return;
        _appliedHighlight  = packed;
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        pOwner->forEachComponentOfType<MeshComponent>( [style, &color]( MeshComponent* pMesh )
        { pMesh->setHighlight( style, color ); } );
    }

    void InteractableComponent::setDefinition( const InteractionDef& def )
    {
        _overrideDef  = def;
        _bHasOverride = SW_TRUE;
        _pDef         = &_overrideDef;
    }

    shared_ptr<const InteractionDef> InteractableComponent::shareDefinition() const
    {
        // 코드로 넣은 정의는 이 컴포넌트가 가진다 — 쥘 주인이 없어 빈 주인으로 가리킨다(예전 날 포인터와 같은 수명).
        return _bHasOverride == SW_TRUE ? shared_ptr<const InteractionDef>{ shared_ptr<const InteractionDef>{}, &_overrideDef } : shared_ptr<const InteractionDef>{ _pCatalog, _pDef };
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
        _catalogPath = path;
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
            pDef != nullptr && pDef->_alignmentMarker.empty() == false && SocketSetComponent::findSocketWorldTransform( *pOwner, pDef->_alignmentMarker, world );
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
