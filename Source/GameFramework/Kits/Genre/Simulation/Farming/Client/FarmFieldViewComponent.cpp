#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/Client/FarmFieldViewComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/Client/FarmCropComponent.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/Client/FarmSoilComponent.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/Component/FarmGameModeComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        /** @brief 칸 오브젝트는 상태에서 다시 세우는 모습이라 상태를 저장하기 전에 걷는다 — 걷지 않으면 복원한 씬에서 다시 세운 것과 겹친다. */
        const StatefulComponentRegistrar<FarmFieldViewComponent> s_fieldViewRegistration{ "FarmFieldViewComponent" };
    } // namespace
} // namespace sw

namespace sw
{
    FarmFieldViewComponent::FarmFieldViewComponent()
        : _soilPrefab{}
        , _cropPrefab{}
        , _grassSoilColor{ 0.40f, 0.62f, 0.28f, 1.0f }
        , _tilledSoilColor{ 0.58f, 0.40f, 0.24f, 1.0f }
        , _wateredSoilColor{ 0.33f, 0.22f, 0.13f, 1.0f }
        , _witheredCropColor{ 0.55f, 0.40f, 0.22f, 1.0f }
        , _cameraRig{}
        , _cameraFollow{ 0.4f }
        , _listCropModel{}
        , _listDefaultGrowingModel{}
        , _director{}
        , _listTileObject{}
        , _tintCache{}
        , _listCropLook{}
        , _listCropMesh{}
        , _arrSoilLook{}
        , _plainCropLook{}
        , _witheredCropLook{}
        , _bSpawned{ SW_FALSE }
        , _bRejected{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    FarmFieldViewComponent::~FarmFieldViewComponent() = default;

    void FarmFieldViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                  pOwner    = getOwner();
        const FarmGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<FarmGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "FarmFieldView.WithoutGameMode", "[Farm] field view on '%#': no FarmGameModeComponent in front of it on the same object - the field is not shown",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<FarmGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<FarmGameModeComponent>( pDirector );
    }

    void FarmFieldViewComponent::onEndPlay()
    {
        despawnViews();
        Component::onEndPlay();
    }

    void FarmFieldViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        context.deferPostTickOnComponent( *this, []( FarmFieldViewComponent& view, GameObjectManager& manager )
        { view.updateView( manager ); } );
    }

    void FarmFieldViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            for ( const GameObjectHandle& handle : _listTileObject )
            {
                GameObject* const pObject = pManager->resolveGameObject( handle );
                if ( pObject != nullptr )
                    pManager->destroyGameObject( pObject );
            }
        }
        _listTileObject.clear();
        _bSpawned = SW_FALSE;
    }

    const shared_ptr<MaterialInstance>& FarmFieldViewComponent::getSoilLook( int32 soilState ) const
    {
        return _arrSoilLook[MathUtil::clamp( soilState, 0, 2 )];
    }

    const shared_ptr<MaterialInstance>& FarmFieldViewComponent::findCropLook( const hashed_string& cropID, bool bWithered, bool bCropColored ) const
    {
        if ( bWithered )
            return _witheredCropLook;
        if ( bCropColored )
        {
            // 뷰가 워커에서 부른다. 컨테이너 인덱스 대신 포인터로 읽는다.
            const CropLook* pLook = _listCropLook.data();
            for ( size_t lookIndex = 0; lookIndex < _listCropLook.size(); ++lookIndex )
            {
                if ( pLook[lookIndex]._cropID == cropID )
                    return pLook[lookIndex]._instance;
            }
        }
        return _plainCropLook;
    }

    const string* FarmFieldViewComponent::findReadyModel( const hashed_string& cropID ) const
    {
        for ( const FarmCropModel& model : _listCropModel )
        {
            if ( model._cropID.key() == cropID )
                return model._readyModel.empty() ? nullptr : &model._readyModel.path();
        }
        return nullptr;
    }

    const string& FarmFieldViewComponent::findGrowingModel( const hashed_string& cropID, int32 stage ) const
    {
        const vector<AssetRef<MeshAssetFormat>>* pList = &_listDefaultGrowingModel;
        for ( const FarmCropModel& model : _listCropModel )
        {
            if ( model._cropID.key() == cropID && model._listGrowingModel.empty() == false )
                pList = &model._listGrowingModel;
        }
        // 첫 모델이 0 · 1 단계, 그다음부터 한 단계에 하나, 모자라면 마지막을 쓴다.
        const int32 index = MathUtil::clamp( stage - 1, 0, static_cast<int32>( pList->size() ) - 1 );
        return ( *pList )[static_cast<size_t>( index )].path();
    }

    void FarmFieldViewComponent::updateView( GameObjectManager& manager )
    {
        const FarmGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        updateCameraFocus( manager, *pDirector );
        if ( _bSpawned == SW_TRUE || _bRejected == SW_TRUE )
            return;
        if ( validateModels( *pDirector ) == false )
        {
            _bRejected = SW_TRUE; // 씬 데이터가 틀렸다 — 오류는 한 번 남겼다
            return;
        }
        spawnField( manager, *pDirector );
        _bSpawned = SW_TRUE;
    }

    bool FarmFieldViewComponent::validateModels( const FarmGameModeComponent& director ) const
    {
        // 작물 모델 표는 씬 데이터다. 기본 자라는 모델이 없으면 칸 뷰가 고를 모델이 없다 — 세우지 않는다.
        if ( _listDefaultGrowingModel.empty() )
        {
            SW_LOG_ERROR_ID( "FarmFieldView.DefaultGrowingModelsMissing", "[Farm] the field view's Default Growing Models is empty - the field is not shown" );
            return false;
        }
        for ( const FarmCropModel& model : _listCropModel )
        {
            if ( director.getCropCatalog().findCrop( model._cropID.key() ) == nullptr )
            {
                SW_LOG_ERROR_ID( "FarmFieldView.UnknownCropModel", "[Farm] the field view's Crop Models names crop '%#', which the game mode's crop catalog does not have - the field is not shown",
                                 model._cropID.c_str() );
                return false;
            }
        }
        return true;
    }

    void FarmFieldViewComponent::spawnField( GameObjectManager& manager, const FarmGameModeComponent& director )
    {
        // 타일마다 흙과 작물을 하나씩 둔다. 각 뷰가 밭의 같은 타일을 따라 모습을 맞춘다(PostUpdate).
        const GameObjectHandle owner = getOwner()->getHandle();
        const FarmField&       field = director.getField();
        for ( int32 y = 0; y < field.getHeight(); ++y )
        {
            for ( int32 x = 0; x < field.getWidth(); ++x )
            {
                const int32  tileIndex = field.getTopology().toIndex( x, y );
                const float3 center    = manager.getWorldOrigin().toLocal( FarmGameModeComponent::computeTileCenter( x, y ) );
                GameObject*  pSoil     = spawnTile( manager, _soilPrefab, "FarmSoil" );
                GameObject*  pCrop     = spawnTile( manager, _cropPrefab, "FarmCrop" );
                if ( pSoil == nullptr || pCrop == nullptr )
                    continue;
                MeshComponent* pSoilMesh           = pSoil->getComponent<MeshComponent>();
                MeshComponent* pCropMesh           = pCrop->getComponent<MeshComponent>();
                const bool     bShouldPrepareLooks = _arrSoilLook[0] == nullptr && pSoilMesh != nullptr && pCropMesh != nullptr;
                if ( bShouldPrepareLooks )
                    prepareLooks( director, pSoilMesh->getMaterial(), pCropMesh->getMaterial() );
                if ( pSoilMesh != nullptr )
                    pSoilMesh->setLocalPosition( center + float3{ 0.0f, pSoilMesh->getLocalScale()._y * 0.5f, 0.0f } );
                if ( pCropMesh != nullptr )
                    pCropMesh->setLocalPosition( center );
                FarmSoilComponent* pSoilView = pSoil->getComponent<FarmSoilComponent>();
                if ( pSoilView != nullptr )
                    pSoilView->assignTile( owner, tileIndex );
                FarmCropComponent* pCropView = pCrop->getComponent<FarmCropComponent>();
                if ( pCropView != nullptr )
                    pCropView->assignTile( owner, tileIndex );
            }
        }
    }

    GameObject* FarmFieldViewComponent::spawnTile( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const utf8* pName )
    {
        AssetManager* const pAssetManager = game::findSubsystem<AssetManager>();
        if ( pAssetManager == nullptr || prefab.empty() )
            return nullptr;
        GameObject* const pObject = pAssetManager->getAssetCache<PrefabCache>().spawn( &manager, prefab.path(), pName );
        if ( pObject != nullptr )
            _listTileObject.push_back( pObject->getHandle() );
        return pObject;
    }

    void FarmFieldViewComponent::prepareLooks( const FarmGameModeComponent& director, Material* pSoilMaterial, Material* pCropMaterial )
    {
        const float4 arrSoilColor[3] = { _grassSoilColor, _tilledSoilColor, _wateredSoilColor };
        for ( int32 soilState = 0; soilState < 3; ++soilState )
        {
            _arrSoilLook[soilState] = _tintCache.acquire( pSoilMaterial, arrSoilColor[soilState] );
        }
        _plainCropLook    = _tintCache.acquire( pCropMaterial, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
        _witheredCropLook = _tintCache.acquire( pCropMaterial, _witheredCropColor );
        _listCropLook.clear();
        for ( const CropDef& crop : director.getCropCatalog().getCrops() )
        {
            CropLook look;
            look._cropID   = crop._id;
            look._instance = _tintCache.acquire( pCropMaterial, crop._color );
            _listCropLook.push_back( look );
        }
        _listCropMesh.clear();
        // 작물 모델 표(씬 데이터)의 모델을 모두 미리 잡는다.
        vector<string> listModelPath;
        for ( const AssetRef<MeshAssetFormat>& growing : _listDefaultGrowingModel )
        {
            listModelPath.push_back( growing.path() );
        }
        for ( const FarmCropModel& model : _listCropModel )
        {
            if ( model._readyModel.empty() == false )
                listModelPath.push_back( model._readyModel.path() );
            for ( const AssetRef<MeshAssetFormat>& growing : model._listGrowingModel )
            {
                listModelPath.push_back( growing.path() );
            }
        }
        // 게임 스레드가 디스크를 기다리지 않게 워커가 읽는다 — 미리 쥐는 것이 목적이라 지금 쓰지 않는다(뷰의 `MeshComponent` 가 같은 로드를 받는다).
        // 읽지 못한 모델은 캐시가 경로마다 한 번 경고한다.
        for ( const string& path : listModelPath )
        {
            _listCropMesh.push_back( MeshCache::request( path, MeshCPUAccess::ReleaseAfterUpload ) );
        }
    }

    void FarmFieldViewComponent::updateCameraFocus( GameObjectManager& manager, const FarmGameModeComponent& director ) const
    {
        // 비스듬히 내려다보는 직교 시점으로, 밭 중심과 농부 사이를 본다. 카메라 디렉터는 PostPhysics 틱 뒤에 이 초점을 읽고, 카메라 오브젝트에는 다른 쓰기가 없다.
        GameObject* const              pRigObject = manager.resolveGameObject( _cameraRig );
        CameraDirectorComponent* const pRig       = pRigObject != nullptr ? pRigObject->getComponent<CameraDirectorComponent>() : nullptr;
        if ( pRig == nullptr )
            return;
        const FarmField& field = director.getField();
        const float3     fieldCenter{ static_cast<float32>( field.getWidth() ) * 0.5f, 0.0f, static_cast<float32>( field.getHeight() ) * 0.5f };
        pRig->setFocusPoint( manager.getWorldOrigin().toLocal( fieldCenter + ( director.getPlayerPosition()._value - fieldCenter ) * _cameraFollow ) );
    }
} // namespace sw
