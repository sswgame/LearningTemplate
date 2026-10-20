#include "pch.h"

#include "Games/HarvestValley/FarmCropComponent.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"

namespace sw
{
    FarmCropComponent::FarmCropComponent()
        : _director{}
        , _tileIndex{ -1 }
        , _modelScale{ 1.6f }
        , _modelFloor{ 0.05f }
        , _soilTop{ 0.08f }
        , _cropState{ -1 }
    {
        setCanEverTick( true );
    }

    void FarmCropComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 행동을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void FarmCropComponent::assignTile( GameObjectHandle director, int32 tileIndex )
    {
        _director  = director;
        _tileIndex = tileIndex;
        _cropState = -1;
    }

    void FarmCropComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner       = getOwner();
        GameObjectManager* pManager     = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh        = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        const bool         bMissingTile = pManager == nullptr || pMesh == nullptr || _tileIndex < 0;
        if ( bMissingTile )
            return;
        const FarmDirectorComponent* pDirector = GameDirectorComponent::resolve<FarmDirectorComponent>( *pManager, _director );
        if ( pDirector == nullptr )
            return;
        const FarmField& field = pDirector->getField();
        if ( field.getWidth() <= 0 )
            return;
        const int32     x     = _tileIndex % field.getWidth();
        const int32     y     = _tileIndex / field.getWidth();
        const FarmTile* pTile = field.findTile( x, y );
        if ( pTile == nullptr )
            return;

        // 단계(0..4) · 다 자람 · 시듦 · 작물이 바뀔 때만 다시 칠한다.
        const float32 ratio     = field.computeGrowthRatio( x, y );
        const int32   stage     = static_cast<int32>( ratio * 4.0f );
        const bool    bReady    = pTile->_bReady != SW_FALSE;
        const bool    bWithered = pTile->_bWithered != SW_FALSE;
        const int32   cropState = pTile->hasCrop() == false ? 0
                                                            : 1 + stage * 4 + ( bReady ? 1 : 0 ) + ( bWithered ? 2 : 0 ) +
                                                                static_cast<int32>( pTile->_cropID.getHash() % 997u ) * 32;
        if ( cropState == _cropState )
            return;
        if ( pTile->hasCrop() == false )
        {
            pMesh->setVisible( false );
            _cropState = cropState;
            return;
        }
        // 모델은 단계마다 바꾸고(디렉터의 작물 모델 표), 색은 정점 색 그대로(흰 모습). 시든 작물은 갈색으로, 제 모델이 없는 작물은 다 자라면 잎에 작물 색을 입힌다.
        const string*                       pReadyModel = bReady ? pDirector->findReadyModel( pTile->_cropID ) : nullptr;
        const string&                       model       = pReadyModel != nullptr ? *pReadyModel : pDirector->findGrowingModel( pTile->_cropID, stage );
        const shared_ptr<MaterialInstance>& look        = pDirector->findCropLook( pTile->_cropID, bWithered, bReady && pReadyModel == nullptr );
        if ( look == nullptr )
            return;
        const float32 size = _modelScale * ( bReady ? 1.0f : 0.7f + 0.3f * ratio );
        pMesh->setMeshID( model );
        pMesh->setMaterialInstance( look );
        pMesh->setLocalScale( float3{ size } );
        pMesh->setLocalPosition( FarmDirectorComponent::computeTileCenter( x, y ) + float3{ 0.0f, _soilTop + _modelFloor * size, 0.0f } );
        pMesh->setVisible( true );
        _cropState = cropState;
    }
} // namespace sw
