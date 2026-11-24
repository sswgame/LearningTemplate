#include "pch.h"

#include "GameFramework/Kits/Feature/World/Voxel/Client/VoxelHighlightComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Feature/World/Voxel/Component/VoxelPlayerComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    VoxelHighlightComponent::VoxelHighlightComponent()
        : _player{}
        , _color{ 1.0f, 1.0f, 1.0f, 0.18f }
        , _colorPerLevel{ 0.0f, -0.2f, -0.2f, 0.15f }
        , _outlinePrefab{}
        , _outlineColor{ 0.05f, 0.05f, 0.05f, 1.0f }
        , _outlineHalfExtent{ 0.505f }
        , _outlineWidth{ 0.015f }
        , _arrLevelLook{}
        , _outlineViews{}
        , _targetCenter{}
        , _level{ -1 }
        , _bTargeted{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
        // 플레이어(DuringPhysics)가 이번 프레임에 겨눈 블록을 정한 뒤에 읽습니다. 편집 중인 씬은 플레이 시작 없이 틱하므로 시작이 아니라 생성 때 정합니다(같은 그룹이면 병렬 읽기가 겹친다).
        setTickGroup( TickGroup::PostUpdate );
    }

    VoxelHighlightComponent::~VoxelHighlightComponent() = default;

    void VoxelHighlightComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        // 단계마다 하나씩 만듭니다. 블록을 부수는 동안 단계가 올라가며 색이 진해집니다. 플레이 시작은 틱 밖이라서 여기서 만듭니다.
        for ( int32 level = 0; level < kLevelCount && pMesh->getMaterial() != nullptr; ++level )
        {
            const float32 step   = static_cast<float32>( level );
            _arrLevelLook[level] = MaterialInstance::create( pMesh->getMaterial() );
            if ( _arrLevelLook[level] != nullptr )
                _arrLevelLook[level]->setVectorParameter( hashed_string( kMaterialColorParameter ), _color + _colorPerLevel * step );
        }
        _level = -1;
        pMesh->setVisible( false );
    }

    void VoxelHighlightComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _outlineViews.destroyAll( *pManager );
        Component::onEndPlay();
    }

    void VoxelHighlightComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*                 pOwner  = getOwner();
        MeshComponent*              pMesh   = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        const GameObject*           pObject = context.getManager().resolveGameObject( _player );
        const VoxelPlayerComponent* pPlayer = pObject != nullptr ? pObject->getComponent<VoxelPlayerComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        VoxelCoord block{};
        const bool bHasTarget = pPlayer != nullptr && pPlayer->findTarget( block );
        if ( pMesh->isVisible() != bHasTarget )
            pMesh->setVisible( bHasTarget );
        // 블록은 절대 자리다 — 메시도 테두리도 원점으로 옮긴다
        _targetCenter = AbsolutePosition{
            float3{ static_cast<float32>( block._x ) + 0.5f, static_cast<float32>( block._y ) + 0.5f, static_cast<float32>( block._z ) + 0.5f }
        };
        _bTargeted = bHasTarget ? SW_TRUE : SW_FALSE;
        context.deferPostTickOnComponent( *this, []( VoxelHighlightComponent& highlight, GameObjectManager& manager )
        { highlight.showOutline( manager ); } );
        if ( bHasTarget == false )
            return;
        pMesh->setLocalPosition( context.getManager().getWorldOrigin().toLocal( _targetCenter ) );
        const int32 level = static_cast<int32>( MathUtil::clamp( pPlayer->getBreakProgress(), 0.0f, 0.99f ) * static_cast<float32>( kLevelCount ) );
        if ( level != _level && _arrLevelLook[level] != nullptr )
        {
            _level = level;
            pMesh->setMaterialInstance( _arrLevelLook[level] );
        }
    }

    void VoxelHighlightComponent::showOutline( GameObjectManager& manager )
    {
        _outlineViews.begin();
        if ( _bTargeted == SW_TRUE )
        {
            const float32 length = _outlineHalfExtent * 2.0f + _outlineWidth; // 모서리가 꼭짓점에서 맞물리게 굵기만큼 길다
            for ( int32 edge = 0; edge < 4; ++edge )
            {
                const float32 sideA = ( edge & 1 ) != 0 ? _outlineHalfExtent : -_outlineHalfExtent;
                const float32 sideB = ( edge & 2 ) != 0 ? _outlineHalfExtent : -_outlineHalfExtent;
                (void)_outlineViews.show( manager, _outlinePrefab, _targetCenter + float3{ 0.0f, sideA, sideB }, float3{ length, _outlineWidth, _outlineWidth }, 0.0f, _outlineColor );
                (void)_outlineViews.show( manager, _outlinePrefab, _targetCenter + float3{ sideA, 0.0f, sideB }, float3{ _outlineWidth, length, _outlineWidth }, 0.0f, _outlineColor );
                (void)_outlineViews.show( manager, _outlinePrefab, _targetCenter + float3{ sideA, sideB, 0.0f }, float3{ _outlineWidth, _outlineWidth, length }, 0.0f, _outlineColor );
            }
        }
        _outlineViews.end( manager );
    }
} // namespace sw
