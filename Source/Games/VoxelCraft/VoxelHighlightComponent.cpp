#include "pch.h"

#include "Games/VoxelCraft/VoxelHighlightComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Framework/GameService.h"
#include "GameFramework/Base/Framework/MaterialTintCache.h"

#include "Games/VoxelCraft/VoxelPlayerComponent.h"

namespace sw
{
    VoxelHighlightComponent::VoxelHighlightComponent()
        : _player{}
        , _arrLevelLook{}
        , _level{ -1 }
    {
        setCanEverTick( true );
    }

    VoxelHighlightComponent::~VoxelHighlightComponent() = default;

    void VoxelHighlightComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 플레이어(DuringPhysics)가 이번 프레임의 겨눔을 정한 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        // 단계마다 하나 — 부수는 동안 진해진다. 플레이 시작은 틱 밖이라 여기서 만든다.
        for ( int32 level = 0; level < kLevelCount && pMesh->getMaterial() != nullptr; ++level )
        {
            const float32 step   = static_cast<float32>( level );
            _arrLevelLook[level] = MaterialInstance::create( pMesh->getMaterial() );
            if ( _arrLevelLook[level] != nullptr )
                _arrLevelLook[level]->setVectorParameter( hashed_string( kMaterialColorParameter ), float4{ 1.0f, 1.0f - 0.2f * step, 1.0f - 0.2f * step, 0.18f + 0.15f * step } );
        }
        _level = -1;
        pMesh->setVisible( false );
    }

    void VoxelHighlightComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*                 pOwner   = getOwner();
        GameObjectManager*          pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*              pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        const GameObject*           pObject  = pManager != nullptr ? pManager->resolveGameObject( _player ) : nullptr;
        const VoxelPlayerComponent* pPlayer  = pObject != nullptr ? pObject->getComponent<VoxelPlayerComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        VoxelCoord block{};
        const bool bHasTarget = pPlayer != nullptr && pPlayer->findTarget( block );
        if ( pMesh->isVisible() != bHasTarget )
            pMesh->setVisible( bHasTarget );
        if ( bHasTarget == false )
            return;
        const float3 center{ static_cast<float32>( block._x ) + 0.5f, static_cast<float32>( block._y ) + 0.5f, static_cast<float32>( block._z ) + 0.5f };
        pMesh->setLocalPosition( center );
        const int32 level = static_cast<int32>( MathUtil::clamp( pPlayer->getBreakProgress(), 0.0f, 0.99f ) * static_cast<float32>( kLevelCount ) );
        if ( level != _level && _arrLevelLook[level] != nullptr )
        {
            _level = level;
            pMesh->setMaterialInstance( _arrLevelLook[level] );
        }
        scheduleOutline( center );
    }

    void VoxelHighlightComponent::scheduleOutline( const float3& center ) const
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 디버그 선 큐는 게임 스레드의 것이다 — 에디터 게임 뷰에만 보인다.
        pManager->executeOrDeferPostTick( [center]()
        {
            DebugDrawQueue* pDebugDraw = game::getService<DebugDrawQueue>();
            if ( pDebugDraw == nullptr )
                return;
            const float3 minCorner = center - float3{ 0.505f };
            const float3 maxCorner = center + float3{ 0.505f };
            const float4 color{ 0.05f, 0.05f, 0.05f, 1.0f };
            for ( int32 edge = 0; edge < 4; ++edge )
            {
                const float32 x = ( edge & 1 ) != 0 ? maxCorner._x : minCorner._x;
                const float32 z = ( edge & 2 ) != 0 ? maxCorner._z : minCorner._z;
                const float32 y = ( edge & 1 ) != 0 ? maxCorner._y : minCorner._y;
                const float32 w = ( edge & 2 ) != 0 ? maxCorner._y : minCorner._y;
                pDebugDraw->drawLine( float3{ x, minCorner._y, z }, float3{ x, maxCorner._y, z }, color );
                pDebugDraw->drawLine( float3{ minCorner._x, y, z }, float3{ maxCorner._x, y, z }, color );
                pDebugDraw->drawLine( float3{ x, w, minCorner._z }, float3{ x, w, maxCorner._z }, color );
            }
        } );
    }
} // namespace sw
