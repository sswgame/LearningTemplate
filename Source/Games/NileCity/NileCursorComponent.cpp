#include "pch.h"

#include "Games/NileCity/NileCursorComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "Games/NileCity/NileGameModeComponent.h"

namespace sw
{
    NileCursorComponent::NileCursorComponent()
        : _height{ 0.1f }
        , _thickness{ 0.12f }
    {
        setCanEverTick( true );
    }

    void NileCursorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 커서 칸을 정한 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void NileCursorComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr )
            return;
        const NileGameModeComponent* pGameMode = findGameMode<NileGameModeComponent>();
        int2                         tile{};
        int32                        size   = 1;
        const bool                   bShown = pGameMode != nullptr && pGameMode->findCursor( tile, size );
        if ( pMesh->isVisible() != bShown )
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        const float32 extent = static_cast<float32>( size );
        pMesh->setLocalPosition( float3{ static_cast<float32>( tile._x ) + extent * 0.5f, _height, static_cast<float32>( tile._y ) + extent * 0.5f } );
        pMesh->setLocalScale( float3{ extent, _thickness, extent } );
    }
} // namespace sw
