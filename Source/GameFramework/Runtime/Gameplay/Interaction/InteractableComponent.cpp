#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Socket/SocketSetComponent.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneSpatialGrid.h"
#include "Engine/Presentation/InstanceHighlight.h"
#include "Engine/Visual/Component/MeshComponent.h"

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
        , _catalogReload{}
        , _appliedHighlight{}
        , _completedCount{ 0 }
        , _bHighlightRequested{ SW_FALSE }
        , _bHasOverride{ SW_FALSE }
        , _arrSpatialEntry{}
    {
    }

    namespace
    {
        // 3D 는 XZ(위 = Y), 2D 는 XY 다(Z 는 그리기 순서). 평면에 내린 거리는 그 공간의 거리보다 길지 않아 반 크기 = 도달 거리 상자가 닿는 하는 쪽을 빠뜨리지 않는다.
        const SpatialGridLayerDesc kSpatialLayer3D{ "Interactable3D", SpatialGridPlane::XZ, InteractableComponent::kMinInteractorCountForGrid };
        const SpatialGridLayerDesc kSpatialLayer2D{ "Interactable2D", SpatialGridPlane::XY, InteractableComponent::kMinInteractorCountForGrid };
    } // namespace

    const SpatialGridLayerDesc& InteractableComponent::getSpatialLayer( InteractionSpace space )
    {
        return space == InteractionSpace::Space2D ? kSpatialLayer2D : kSpatialLayer3D;
    }

    float32 InteractableComponent::getSpatialExtent() const
    {
        const InteractionDef* pDef = getDefinition();
        return pDef != nullptr ? MathUtil::max( pDef->_maxDistance, 0.0f ) : 0.0f;
    }

    void InteractableComponent::syncSpatialExtent()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _arrSpatialEntry[0].isValid() == false )
            return;
        // 테이블을 다시 읽은 틱(워커)에서도 불린다 — 색인은 게임 스레드에서만 고친다.
        pManager->executeOrDeferPostTickOnComponent( *this, []( InteractableComponent& component )
        {
            GameObjectManager* pTarget = component.getOwner() != nullptr ? component.getOwner()->getManager() : nullptr;
            if ( pTarget == nullptr )
                return;
            SceneSpatialGrid& grid = pTarget->getSubsystem<SceneSpatialGrid>();
            for ( const SlotHandle entry : component._arrSpatialEntry )
            {
                if ( entry.isValid() )
                    grid.setExtent( entry, component.getSpatialExtent() );
            }
        } );
    }

    void InteractableComponent::resolveDefinition()
    {
        findDefinition();
        syncSpatialExtent();
    }

    void InteractableComponent::findDefinition()
    {
        if ( _bHasOverride == SW_TRUE )
        {
            _pDef = &_overrideDef;
            return;
        }
        _pDef = nullptr;
        _pCatalog.reset();
        _catalogReload.markSeen();
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
        // 위치가 없는 대상(씬 컴포넌트 없음)은 격자에 오르지 않는다 — 하는 쪽도 그런 대상은 고르지 않는다.
        const SceneComponent* pScene = getOwner() != nullptr ? getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return;
        SceneSpatialGrid& grid = manager.getSubsystem<SceneSpatialGrid>();
        _arrSpatialEntry[0]    = grid.add( kSpatialLayer3D, *this, *pScene, getSpatialExtent() );
        _arrSpatialEntry[1]    = grid.add( kSpatialLayer2D, *this, *pScene, getSpatialExtent() );
    }

    void InteractableComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<InteractableComponent>( this );
        SceneSpatialGrid& grid = manager.getSubsystem<SceneSpatialGrid>();
        for ( SlotHandle& entry : _arrSpatialEntry )
        {
            grid.remove( entry );
        }
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
        if ( _bHasOverride == SW_FALSE && _catalogReload.isStale() )
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
        const InstanceHighlight      applied{ style, color };
        if ( applied == _appliedHighlight )
            return;
        _appliedHighlight  = applied;
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
        syncSpatialExtent();
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

    InteractionBlockReason InteractableComponent::findBlockReason( const GameObject& interactor ) const
    {
        const InteractionDef* pDef = getDefinition();
        if ( pDef == nullptr )
            return InteractionBlockReason::NoDefinition;
        if ( _bEnabled == false )
            return InteractionBlockReason::Disabled;
        if ( _cooldownRemaining > 0.0f )
            return InteractionBlockReason::Cooldown;
        if ( interactor.getTags().hasAllTags( pDef->_requiredTags ) == false )
            return InteractionBlockReason::MissingRequiredTag;
        if ( pDef->allowsInteractor( interactor.getTags() ) == false )
            return InteractionBlockReason::ForbiddenTag; // 필수는 맞았으니 금지 태그다(판정은 allowsInteractor 하나 — 사유만 나눈다)
        return InteractionBlockReason::None;
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
