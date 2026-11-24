#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityBuildCursorComponent.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityBuildInputComponent.h"

namespace sw
{
    CityBuildCursorComponent::CityBuildCursorComponent()
        : _height{ 0.1f }
        , _thickness{ 0.12f }
    {
        setCanEverTick( true );
    }

    void CityBuildCursorComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        const CityBuildInputComponent* pBuildInput = findGameMode<CityBuildInputComponent>();
        int2                           tile{};
        int32                          size   = 1;
        const bool                     bShown = pBuildInput != nullptr && pBuildInput->findCursor( tile, size );
        if ( pMesh->isVisible() != bShown )
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        const float32 extent = static_cast<float32>( size );
        const float3  center{ static_cast<float32>( tile._x ) + extent * 0.5f, _height, static_cast<float32>( tile._y ) + extent * 0.5f };
        pMesh->setLocalPosition( pOwner->getManager()->getWorldOrigin().toLocal( center ) );
        pMesh->setLocalScale( float3{ extent, _thickness, extent } );
    }
} // namespace sw
