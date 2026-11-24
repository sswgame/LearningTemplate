#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/Client/FarmSoilComponent.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/Client/FarmFieldViewComponent.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/Component/FarmGameModeComponent.h"

namespace sw
{
    FarmSoilComponent::FarmSoilComponent()
        : _tileIndex{ -1 }
        , _soilState{ -1 }
    {
        setCanEverTick( true );
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
        GameObject*    pOwner       = getOwner();
        MeshComponent* pMesh        = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        const bool     bMissingTile = pMesh == nullptr || _tileIndex < 0;
        if ( bMissingTile )
            return;
        const FarmGameModeComponent* pGameMode = findGameMode<FarmGameModeComponent>();
        // 모습을 나눠 주는 밭 뷰는 디렉터와 같은 오브젝트다(밭 뷰가 칸을 세우며 그 오브젝트를 적는다).
        const FarmFieldViewComponent* pFieldView = findGameMode<FarmFieldViewComponent>();
        if ( pGameMode == nullptr || pFieldView == nullptr )
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
        const shared_ptr<MaterialInstance>& look = pFieldView->getSoilLook( soilState );
        if ( look == nullptr )
            return;
        pMesh->setMaterialInstance( look );
        _soilState = soilState;
    }
} // namespace sw
