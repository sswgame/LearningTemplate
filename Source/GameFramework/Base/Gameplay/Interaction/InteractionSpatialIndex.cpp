#include "pch.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractionSpatialIndex.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractableComponent.h"

namespace sw
{
    InteractionSpatialIndex::InteractionSpatialIndex()
        : _arrSpace{}
    {
    }

    InteractionSpatialIndex* InteractionSpatialIndex::find( const GameObjectManager& manager )
    {
        // 이 키로는 이 타입만 붙인다(`registerInteractor`).
        return static_cast<InteractionSpatialIndex*>( manager.findFrameSystem( hashed_string( kFrameSystemKey ) ) );
    }

    void InteractionSpatialIndex::registerInteractor( GameObjectManager& manager, InteractionSpace space )
    {
        InteractionSpatialIndex* pIndex = find( manager );
        if ( pIndex == nullptr )
        {
            unique_ptr<InteractionSpatialIndex> index = make_unique<InteractionSpatialIndex>();
            pIndex                                    = index.get();
            const bool bAdded                         = manager.addFrameSystem( hashed_string( kFrameSystemKey ), std::move( index ) );
            SW_ASSERT( bAdded ); // 같은 키가 없음을 바로 위에서 보았다
            (void)bAdded;
        }
        ++pIndex->_arrSpace[toSpaceIndex( space )]._interactorCount;
    }

    void InteractionSpatialIndex::unregisterInteractor( GameObjectManager& manager, InteractionSpace space )
    {
        InteractionSpatialIndex* pIndex = find( manager );
        SW_ASSERT( pIndex != nullptr );
        if ( pIndex == nullptr )
            return;
        SpaceGrid& spaceGrid = pIndex->_arrSpace[toSpaceIndex( space )];
        SW_ASSERT( spaceGrid._interactorCount > 0 );
        --spaceGrid._interactorCount;
        spaceGrid._bBuilt = SW_FALSE;
        // 마지막 사용자가 떠나면 뗀다(모듈을 내리면 컴포넌트가 먼저 지워진다 — `ISceneFrameSystem` 의 약속).
        const bool bNoUser = pIndex->_arrSpace[0]._interactorCount == 0 && pIndex->_arrSpace[1]._interactorCount == 0;
        if ( bNoUser )
            manager.removeFrameSystem( hashed_string( kFrameSystemKey ) );
    }

    size_t InteractionSpatialIndex::toSpaceIndex( InteractionSpace space )
    {
        return space == InteractionSpace::Space2D ? 1u : 0u;
    }

    void InteractionSpatialIndex::projectToPlane( InteractionSpace space, const float3& position, float32& outPlaneX, float32& outPlaneY )
    {
        outPlaneX = position._x;
        outPlaneY = space == InteractionSpace::Space2D ? position._y : position._z;
    }

    void InteractionSpatialIndex::runBeforeTick( GameObjectManager& manager, float32 deltaTime )
    {
        (void)deltaTime;
        rebuildSpace( manager, InteractionSpace::Space3D, _arrSpace[0] );
        rebuildSpace( manager, InteractionSpace::Space2D, _arrSpace[1] );
    }

    void InteractionSpatialIndex::rebuildSpace( GameObjectManager& manager, InteractionSpace space, SpaceGrid& inoutSpace )
    {
        inoutSpace._bBuilt = SW_FALSE;
        inoutSpace._listTarget.clear();
        inoutSpace._listPosition.clear();
        inoutSpace._maxReach = 0.0f;
        if ( inoutSpace._interactorCount < kMinInteractorCountForGrid )
            return;

        // 첫 번 훑기: 넣을 대상과 최대 도달 거리. 쓸 수 있는지(켜짐 · 태그 · 하는 쪽 자신) 는 하는 쪽이 고를 때 본다 — 여기는 위치가 있는 대상만 거른다.
        for ( InteractableComponent* pInteractable : manager.getComponentRegistry().getAll<InteractableComponent>() )
        {
            const GameObject*     pObject = pInteractable != nullptr ? pInteractable->getOwner() : nullptr;
            const SceneComponent* pScene  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pScene == nullptr || pInteractable->getDefinition() == nullptr )
                continue;
            inoutSpace._maxReach = MathUtil::max( inoutSpace._maxReach, pInteractable->getDefinition()->_maxDistance );
            inoutSpace._listTarget.push_back( TComponentHandle<InteractableComponent>{ pInteractable } );
            inoutSpace._listPosition.push_back( pScene->getWorldPosition() );
        }
        // 도달 거리가 모두 0 이하면 셀을 정할 수 없다. 격자 없이 목록을 도는 길이 같은 답을 낸다.
        if ( inoutSpace._maxReach <= 0.0f )
            return;

        if ( inoutSpace._grid.getCellSize() != inoutSpace._maxReach )
            inoutSpace._grid = SpatialHashGrid2D{ inoutSpace._maxReach };
        else
            inoutSpace._grid.clear();
        for ( size_t targetIndex = 0; targetIndex < inoutSpace._listTarget.size(); ++targetIndex )
        {
            float32 planeX{ 0.0f };
            float32 planeY{ 0.0f };
            projectToPlane( space, inoutSpace._listPosition[targetIndex], planeX, planeY );
            // 세대 0 은 무효라 1 로 둔다. 인덱스가 `_listTarget` 의 번호다.
            inoutSpace._grid.insert( SlotHandle::make( static_cast<uint32>( targetIndex ), 1u ), planeX, planeY, planeX, planeY );
        }
        inoutSpace._bBuilt = SW_TRUE;
    }

    bool InteractionSpatialIndex::hasGrid( InteractionSpace space ) const
    {
        return _arrSpace[toSpaceIndex( space )]._bBuilt == SW_TRUE;
    }

    void InteractionSpatialIndex::queryTargets( const InteractionView& viewer, vector<TComponentHandle<InteractableComponent>>& outListTarget ) const
    {
        outListTarget.clear();
        const SpaceGrid& spaceGrid = _arrSpace[toSpaceIndex( viewer._space )];
        SW_ASSERT( spaceGrid._bBuilt == SW_TRUE );
        float32 planeX{ 0.0f };
        float32 planeY{ 0.0f };
        projectToPlane( viewer._space, viewer._position, planeX, planeY );
        // 질의마다 새 목록이다 — 병렬 틱의 하는 쪽들이 같은 색인을 동시에 읽는다.
        vector<SlotHandle> listHandle;
        spaceGrid._grid.queryCircle( planeX, planeY, spaceGrid._maxReach, listHandle );
        outListTarget.reserve( listHandle.size() );
        for ( const SlotHandle handle : listHandle )
        {
            outListTarget.push_back( spaceGrid._listTarget[handle.index()] );
        }
    }

    uint32 InteractionSpatialIndex::getInteractorCount( InteractionSpace space ) const
    {
        return _arrSpace[toSpaceIndex( space )]._interactorCount;
    }
} // namespace sw
