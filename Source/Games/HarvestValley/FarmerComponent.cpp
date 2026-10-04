#include "pch.h"

#include "Games/HarvestValley/FarmerComponent.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"

namespace sw
{
    FarmerComponent::FarmerComponent()
        : _director{}
        , _heightOffset{ 0.5f }
        , _kind{ FarmerViewKind::Farmer }
    {
        setCanEverTick( true );
    }

    void FarmerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 이동을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void FarmerComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr )
            return;
        const FarmDirectorComponent* pDirector = GameDirectorComponent::resolve<FarmDirectorComponent>( *pManager, _director );
        if ( pDirector == nullptr )
            return;
        const float3 lift{ 0.0f, _heightOffset, 0.0f };
        if ( _kind == FarmerViewKind::Farmer )
        {
            pMesh->setLocalPosition( pDirector->getPlayerPosition() + lift );
            return;
        }
        int32      x       = 0;
        int32      y       = 0;
        const bool bOnTile = pDirector->findTargetTile( x, y );
        if ( pMesh->isVisible() != bOnTile )
            pMesh->setVisible( bOnTile );
        if ( bOnTile )
            pMesh->setLocalPosition( FarmDirectorComponent::computeTileCenter( x, y ) + lift );
    }
} // namespace sw
