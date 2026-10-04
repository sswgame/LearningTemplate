#include "pch.h"

#include "Games/HarvestValley/FarmCropComponent.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"

namespace sw
{
    namespace
    {
        struct FarmCropComponentInternal
        {
            /**
             * @brief Kenney Nature Kit 모델의 배율입니다. 키트는 밭 한 줄(`crops_dirt_row`)이 1 이라 키트 1 칸이 밭 1 칸이지만, 위에서 비스듬히 보는
             *        직교 카메라에서 작물이 콩알만 해서 1.6 배로 키운다(작물 폭 0.35 → 0.56 m, 칸 안에 들어간다).
             * @details 모델은 바닥이 원점보다 0.05 아래다(키트 노드가 내려 둔 값) — 흙 윗면에 얹을 때 그만큼 올린다.
             */
            static constexpr float32 kModelScale = 1.6f;
            static constexpr float32 kModelFloor = 0.05f;
            static constexpr float32 kSoilTop    = 0.08f;

            static constexpr const utf8* kArrModelName[] = { "crop_turnip", "crop_carrot", "crop_pumpkin", "crops_corn_stage_a",
                                                             "crops_corn_stage_b", "crops_corn_stage_c", "crops_corn_stage_d", "crops_leafs_stage_a",
                                                             "crops_leafs_stage_b" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    FarmCropComponent::FarmCropComponent()
        : _director{}
        , _tileIndex{ -1 }
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

    void FarmCropComponent::collectModelPaths( vector<string>& outListPath )
    {
        for ( const utf8* pName : FarmCropComponentInternal::kArrModelName )
            outListPath.push_back( makeModelPath( pName ) );
    }

    const utf8* FarmCropComponent::findReadyModel( const hashed_string& cropId )
    {
        if ( cropId == hashed_string( "turnip" ) || cropId == hashed_string( "onion" ) )
            return "crop_turnip";
        if ( cropId == hashed_string( "carrot" ) )
            return "crop_carrot";
        if ( cropId == hashed_string( "pumpkin" ) )
            return "crop_pumpkin";
        if ( cropId == hashed_string( "corn" ) )
            return "crops_corn_stage_d";
        return nullptr;
    }

    const utf8* FarmCropComponent::findGrowingModel( const hashed_string& cropId, int32 stage )
    {
        if ( cropId == hashed_string( "corn" ) )
            return stage <= 1 ? "crops_corn_stage_a" : ( stage == 2 ? "crops_corn_stage_b" : "crops_corn_stage_c" );
        return stage <= 1 ? "crops_leafs_stage_a" : "crops_leafs_stage_b";
    }

    string FarmCropComponent::makeModelPath( const utf8* pName )
    {
        return string( "game/harvestvalley/models/" ) + pName + ".mesh";
    }

    void FarmCropComponent::onTick( float32 deltaTime )
    {
        using Internal = FarmCropComponentInternal;
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
                                                                static_cast<int32>( pTile->_cropId.getHash() % 997u ) * 32;
        if ( cropState == _cropState )
            return;
        if ( pTile->hasCrop() == false )
        {
            pMesh->setVisible( false );
            _cropState = cropState;
            return;
        }
        // 모델은 단계마다 바꾸고, 색은 정점 색 그대로(흰 모습). 시든 작물은 갈색으로, 제 모델이 없는 작물은 다 자라면 잎에 작물 색을 입힌다.
        const utf8*                         pReadyModel = bReady ? findReadyModel( pTile->_cropId ) : nullptr;
        const utf8*                         pModel      = pReadyModel != nullptr ? pReadyModel : findGrowingModel( pTile->_cropId, stage );
        const shared_ptr<MaterialInstance>& look        = pDirector->findCropLook( pTile->_cropId, bWithered, bReady && pReadyModel == nullptr );
        if ( look == nullptr )
            return;
        const float32 size = Internal::kModelScale * ( bReady ? 1.0f : 0.7f + 0.3f * ratio );
        pMesh->setMeshId( makeModelPath( pModel ) );
        pMesh->setMaterialInstance( look );
        pMesh->setLocalScale( float3{ size } );
        pMesh->setLocalPosition( FarmDirectorComponent::computeTileCenter( x, y ) + float3{ 0.0f, Internal::kSoilTop + Internal::kModelFloor * size, 0.0f } );
        pMesh->setVisible( true );
        _cropState = cropState;
    }
} // namespace sw
