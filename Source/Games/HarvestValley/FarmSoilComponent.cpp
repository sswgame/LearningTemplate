#include "pch.h"

#include "Games/HarvestValley/FarmSoilComponent.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"

namespace sw
{
    FarmSoilComponent::FarmSoilComponent()
        : _director{}
        , _tileIndex{ -1 }
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

    void FarmSoilComponent::assignTile( GameObjectHandle director, int32 tileIndex )
    {
        _director  = director;
        _tileIndex = tileIndex;
        _soilState = -1;
    }

    void FarmSoilComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr || _tileIndex < 0 )
            return;
        const FarmDirectorComponent* pDirector = GameDirectorComponent::resolve<FarmDirectorComponent>( *pManager, _director );
        if ( pDirector == nullptr )
            return;
        const FarmField& field = pDirector->getField();
        const FarmTile*  pTile = field.getWidth() > 0 ? field.findTile( _tileIndex % field.getWidth(), _tileIndex / field.getWidth() ) : nullptr;
        if ( pTile == nullptr )
            return;
        const int32 soilState = pTile->_bTilled == SW_FALSE ? 0 : ( pTile->_bWatered != SW_FALSE ? 2 : 1 );
        if ( soilState == _soilState )
            return;
        const shared_ptr<MaterialInstance>& look = pDirector->getSoilLook( soilState );
        if ( look == nullptr )
            return;
        pMesh->setMaterialInstance( look );
        _soilState = soilState;
    }
} // namespace sw
