#include "pch.h"

#include "Games/HarvestValley/FarmSoilComponent.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "Games/HarvestValley/FarmGameModeComponent.h"

namespace sw
{
    FarmSoilComponent::FarmSoilComponent()
        : _tileIndex{ -1 }
        , _soilState{ -1 }
    {
        setCanEverTick( true );
    }

    void FarmSoilComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 행동을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void FarmSoilComponent::assignTile( GameObjectHandle gameMode, int32 tileIndex )
    {
        bindGameMode( gameMode );
        _tileIndex = tileIndex;
        _soilState = -1;
    }

    void FarmSoilComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*        pOwner       = getOwner();
        GameObjectManager* pManager     = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh        = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        const bool         bMissingTile = pManager == nullptr || pMesh == nullptr || _tileIndex < 0;
        if ( bMissingTile )
            return;
        const FarmGameModeComponent* pGameMode = findGameMode<FarmGameModeComponent>();
        if ( pGameMode == nullptr )
            return;
        const FarmField&    field    = pGameMode->getField();
        const GridTopology& topology = field.getTopology();
        if ( _tileIndex >= topology.getCellCount() )
            return;
        const int2      cell  = topology.toCell( _tileIndex );
        const FarmTile* pTile = field.findTile( cell._x, cell._y );
        if ( pTile == nullptr )
            return;
        const int32 soilState = pTile->_bTilled == SW_FALSE ? 0 : ( pTile->_bWatered != SW_FALSE ? 2 : 1 );
        if ( soilState == _soilState )
            return;
        const shared_ptr<MaterialInstance>& look = pGameMode->getSoilLook( soilState );
        if ( look == nullptr )
            return;
        pMesh->setMaterialInstance( look );
        _soilState = soilState;
    }
} // namespace sw
