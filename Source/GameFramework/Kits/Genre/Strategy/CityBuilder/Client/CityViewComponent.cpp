#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityViewComponent.h"

#include "Core/Log/LogOnce.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityBuildingViewComponent.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityWalkerViewComponent.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityBuilderGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {

        struct CityViewComponentInternal
        {
            static constexpr float32 kHalfCell        = 0.5f;
            static constexpr float32 kGroundThickness = 0.2f; ///< 땅 상자의 두께(m)
            static constexpr float32 kRockWidth       = 0.9f; ///< 바위 상자가 차지하는 칸 비율
            static constexpr int32   kWalkerKindCount = 3;
            static constexpr float32 kNoServiceGray   = 0.6f; ///< 서비스 없음의 회색

            /** @brief 일꾼 모습 번호입니다 = 종류 × @p stride + 서비스 행 번호 + 1(서비스 없음이 0). */
            static int32 makeWalkerLookKey( int32 stride, CityWalkerKind kind, int32 serviceIndex )
            {
                return static_cast<int32>( kind ) * stride + serviceIndex + 1;
            }

            static void destroyView( GameObjectManager& manager, GameObjectHandle handle )
            {
                GameObject* const pView = manager.resolveGameObject( handle );
                if ( pView != nullptr )
                    manager.destroyGameObject( pView );
            }

            static void destroyViews( GameObjectManager& manager, vector<GameObjectHandle>& inoutListView )
            {
                for ( const GameObjectHandle handle : inoutListView )
                {
                    destroyView( manager, handle );
                }
                inoutListView.clear();
            }

            template <typename TID>
            static void destroyViews( GameObjectManager& manager, SimulationViewSync<TID>& inoutViews )
            {
                for ( const typename SimulationViewSync<TID>::Slot& slot : inoutViews.getSlots() )
                {
                    destroyView( manager, slot._object );
                }
                inoutViews.clear();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CityViewComponent::CityViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&CityViewComponent::updateView>() )
        , _buildingViewPath{}
        , _groundPrefab{}
        , _roadPrefab{}
        , _modelBuildingPrefab{}
        , _blockBuildingPrefab{}
        , _walkerPrefab{}
        , _roadColor{ 1.0f, 1.0f, 1.0f, 1.0f }
        , _traderColor{ 1.0f, 0.6f, 0.2f, 1.0f }
        , _cartColor{ 0.45f, 0.3f, 0.15f, 1.0f }
        , _pBuildingViewCatalog{}
        , _shownBuildingViews{}
        , _buildingModels{}
        , _tintCache{}
        , _buildingViews{}
        , _roadViews{}
        , _listGroundObject{}
        , _listWalkerObject{}
        , _listWalkerLook{}
        , _shownCatalog{}
        , _walkerLookStride{ 0 }
        , _shownRevision{ 0 }
        , _bViewsValid{ SW_FALSE }
        , _bTerrainSpawned{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    CityViewComponent::~CityViewComponent() = default;

    void CityViewComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _shownCatalog.reset();
    }

    void CityViewComponent::onEndPlay()
    {
        despawnViews();
        _buildingModels.clear();
        GameModeViewComponent::onEndPlay();
    }

    void CityViewComponent::despawnViews()
    {
        using Internal                    = CityViewComponentInternal;
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            Internal::destroyViews( *pManager, _buildingViews );
            Internal::destroyViews( *pManager, _roadViews );
            Internal::destroyViews( *pManager, _listGroundObject );
            Internal::destroyViews( *pManager, _listWalkerObject );
        }
        _shownRevision   = 0;
        _bTerrainSpawned = SW_FALSE;
    }

    const shared_ptr<MaterialInstance>& CityViewComponent::findWalkerLook( const CityWalker& walker ) const
    {
        static const shared_ptr<MaterialInstance> kNoLook{};
        const int32                               key = CityViewComponentInternal::makeWalkerLookKey( _walkerLookStride, walker._kind, walker._service );
        if ( key < 0 || key >= static_cast<int32>( _listWalkerLook.size() ) )
            return kNoLook;
        // 일꾼 뷰가 워커에서 부른다. 컨테이너 인덱스 대신 포인터로 읽는다.
        return _listWalkerLook.data()[key];
    }

    void CityViewComponent::updateView( GameObjectManager& manager, const CityBuilderGameModeComponent& director )
    {
        const CityCatalog& catalog = director->findCatalog(); // 열린 판은 카탈로그를 쥐고 있다
        if ( _shownCatalog.get() != &catalog )
        {
            despawnViews(); // 도시가 새 판에 묶였다 — 땅 · 서비스의 색과 집 단계가 바뀌었을 수 있다
            _listWalkerLook.clear();
            _shownCatalog = director.getCatalogSnapshot();
            _bViewsValid  = acquireBuildingViews( catalog ) ? SW_TRUE : SW_FALSE;
        }
        if ( _bViewsValid == SW_FALSE )
            return;
        if ( _pBuildingViewCatalog.get() != _shownBuildingViews.get() )
        {
            despawnViews();
            _shownBuildingViews = _pBuildingViewCatalog.snapshot();
        }
        const CitySimulation& city = director.getCity();
        if ( _bTerrainSpawned == SW_FALSE )
            spawnTerrain( manager, city, catalog );
        if ( _shownRevision != director.getLayoutRevision() )
        {
            syncTiles( manager, city );
            _shownRevision = director.getLayoutRevision();
        }
        spawnWalkers( manager, catalog, city.getWalkers().size() );
    }

    bool CityViewComponent::acquireBuildingViews( const CityCatalog& catalog )
    {
        if ( _buildingViewPath.empty() )
            return true; // 모두 색 상자인 도시
        _pBuildingViewCatalog = _buildingViewPath.acquire();
        if ( _pBuildingViewCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "City.BuildingViewsMissing", "City view: the building model table '%#' could not be read - nothing is drawn", _buildingViewPath.c_str() );
            return false;
        }
        // 집은 단계마다 모델이 바뀐다 — 표에 빠진 단계가 있으면 그 단계만 조용히 색 상자가 되므로 처음에 막는다.
        for ( const CityBuildingDef& def : catalog.getBuildings() )
        {
            if ( def._kind != CityBuildingKind::House )
                continue;
            bool bComplete = CityBuildingViewComponent::findView( *_pBuildingViewCatalog, def, 0, false ) != nullptr; // 빈 땅
            for ( int32 level = 0; level < static_cast<int32>( catalog.getHouseLevels().size() ); ++level )
            {
                bComplete = bComplete && CityBuildingViewComponent::findView( *_pBuildingViewCatalog, def, level, true ) != nullptr;
            }
            if ( bComplete )
                continue;
            SW_LOG_ERROR_ID( "City.HouseViewMissing", "City view: %# needs '%#.empty' and '%#.<level>' rows for every house level - nothing is drawn", _buildingViewPath.c_str(), def._id.c_str(),
                             def._id.c_str() );
            return false;
        }
        // 게임 스레드가 디스크를 기다리지 않게 워커가 읽는다 — 미리 쥐는 것이 목적이다(뷰의 `MeshComponent` 가 같은 로드를 받는다). 읽지 못한 모델은 캐시가 경로마다 한 번 경고한다.
        _buildingModels.clear();
        for ( const string& path : _pBuildingViewCatalog->makeAllMeshPaths() )
        {
            _buildingModels.add( MeshCache::request( path, MeshCPUAccess::ReleaseAfterUpload ) );
        }
        return true;
    }

    // ------------------------------------------------------------------------------
    // 스폰(틱 뒤 · 게임 스레드). 도시는 절대 자리에서 돈다 — 메시에 쓸 때 월드 원점 기준 로컬로 옮긴다.
    // ------------------------------------------------------------------------------
    void CityViewComponent::syncTiles( GameObjectManager& manager, const CitySimulation& city )
    {
        const vector<CityBuilding>& listBuilding = city.getBuildings();
        _buildingViews.beginCollect();
        for ( size_t buildingIndex = 0; buildingIndex < listBuilding.size(); ++buildingIndex )
        {
            const CityBuilding& building = listBuilding[buildingIndex];
            if ( building._bAlive != SW_FALSE )
                _buildingViews.show( buildingIndex, BuildingView{ building._pDef, building._origin } );
        }
        _buildingViews.endCollect();
        const GridTopology& topology = city.getTopology();
        for ( int32 tileIndex = 0; tileIndex < topology.getCellCount(); ++tileIndex )
        {
            const int2      cell  = topology.toCell( tileIndex );
            const CityTile* pTile = city.findTile( cell._x, cell._y );
            _roadViews.show( static_cast<size_t>( tileIndex ), pTile != nullptr && pTile->_bRoad != SW_FALSE ? SW_TRUE : SW_FALSE );
        }
        const auto destroy = [&manager]( GameObjectHandle& inoutHandle )
        {
            CityViewComponentInternal::destroyView( manager, inoutHandle );
            inoutHandle = GameObjectHandle{};
        };
        _buildingViews.flush( destroy, [this, &manager]( size_t buildingIndex, const BuildingView& view )
        { return spawnBuilding( manager, buildingIndex, *view._pDef ); } );
        _roadViews.flush( destroy, [this, &manager, &topology]( size_t tileIndex, uint8 )
        { return spawnRoad( manager, topology.toCell( static_cast<int32>( tileIndex ) ) ); } );
    }

    void CityViewComponent::spawnTerrain( GameObjectManager& manager, const CitySimulation& city, const CityCatalog& catalog )
    {
        using Internal   = CityViewComponentInternal;
        _bTerrainSpawned = SW_TRUE;
        if ( _groundPrefab.empty() )
            return;
        // 줄마다 같은 땅이 이어지는 구간을 상자 하나로 만든다(타일마다 스폰하면 2000개가 되며, 땅은 바뀌지 않는다). 솟은 돌은 타일마다 상자다.
        for ( int32 y = 0; y < city.getHeight(); ++y )
        {
            const float32 centerZ  = static_cast<float32>( y ) + Internal::kHalfCell;
            int32         runStart = 0;
            for ( int32 x = 1; x <= city.getWidth(); ++x )
            {
                const uint8 runTerrain = city.findTile( runStart, y )->_terrain;
                if ( x < city.getWidth() && city.findTile( x, y )->_terrain == runTerrain )
                    continue;
                const float32         length  = static_cast<float32>( x - runStart );
                const CityTerrainDef& terrain = catalog.getTerrainRows()[runTerrain];
                spawnGroundBox( manager, float3{ static_cast<float32>( runStart ) + length * 0.5f, terrain._surfaceHeight - Internal::kGroundThickness * 0.5f, centerZ },
                                float3{ length, Internal::kGroundThickness, 1.0f }, terrain._color );
                for ( int32 rockX = runStart; terrain._rockHeight > 0.0f && rockX < x; ++rockX )
                {
                    spawnGroundBox( manager, float3{ static_cast<float32>( rockX ) + Internal::kHalfCell, terrain._rockHeight * 0.5f, centerZ },
                                    float3{ Internal::kRockWidth, terrain._rockHeight, Internal::kRockWidth }, terrain._color );
                }
                runStart = x;
            }
        }
    }

    void CityViewComponent::spawnGroundBox( GameObjectManager& manager, const float3& center, const float3& scale, const float4& color )
    {
        GameObject* const    pBox  = spawnView( manager, _groundPrefab, "CityGround" );
        MeshComponent* const pMesh = pBox != nullptr ? pBox->getComponent<MeshComponent>() : nullptr;
        if ( pBox != nullptr )
            _listGroundObject.push_back( pBox->getHandle() );
        if ( pMesh == nullptr )
            return;
        pMesh->setLocalPosition( manager.getWorldOrigin().toLocal( center ) );
        pMesh->setLocalScale( scale );
        if ( _tintCache.apply( *pMesh, color ) == false )
            SW_LOG_ERROR_ONCE_ID( "CityView.GroundWithoutMaterial", "[City]: prefab '%#' has a mesh without a material - it keeps the prefab colour", _groundPrefab.c_str() );
    }

    GameObjectHandle CityViewComponent::spawnRoad( GameObjectManager& manager, const int2& tile )
    {
        // 도로 조각의 크기 · 높이는 프리팹이 정한다(팔레트 머티리얼 그대로면 `_roadColor` 는 흰색이다).
        GameObject* const    pRoad = spawnView( manager, _roadPrefab, "CityRoad" );
        MeshComponent* const pMesh = pRoad != nullptr ? pRoad->getComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr )
        {
            const float3 center{ static_cast<float32>( tile._x ) + CityViewComponentInternal::kHalfCell, 0.0f, static_cast<float32>( tile._y ) + CityViewComponentInternal::kHalfCell };
            pMesh->setLocalPosition( manager.getWorldOrigin().toLocal( center + pMesh->getLocalPosition() ) );
            if ( _tintCache.apply( *pMesh, _roadColor ) == false )
                SW_LOG_ERROR_ONCE_ID( "CityView.RoadWithoutMaterial", "[City]: prefab '%#' has a mesh without a material - it keeps the prefab colour", _roadPrefab.c_str() );
        }
        return pRoad != nullptr ? pRoad->getHandle() : GameObjectHandle{};
    }

    GameObjectHandle CityViewComponent::spawnBuilding( GameObjectManager& manager, size_t buildingIndex, const CityBuildingDef& def )
    {
        // 모델이 있는 건물은 팔레트 머티리얼, 밭 · 조각상은 색 상자다. 자리 · 단계 · 사람에 따른 모델 · 높이는 뷰가 맞춘다.
        const RuleViewCatalog* const pViews    = getBuildingViews();
        const bool                   bModel    = pViews != nullptr && CityBuildingViewComponent::hasModel( *pViews, def );
        GameObject* const            pBuilding = spawnView( manager, bModel ? _modelBuildingPrefab : _blockBuildingPrefab, "CityBuilding" );
        if ( pBuilding == nullptr )
            return GameObjectHandle{};
        MeshComponent* const pMesh = pBuilding->getComponent<MeshComponent>();
        if ( pMesh != nullptr && _tintCache.apply( *pMesh, bModel ? def._modelTint : def._blockColor ) == false )
            SW_LOG_ERROR_ONCE_ID( "CityView.BuildingWithoutMaterial", "[City]: prefab '%#' has a mesh without a material - it keeps the prefab colour", ( bModel ? _modelBuildingPrefab : _blockBuildingPrefab ).c_str() );
        CityBuildingViewComponent* const pView = pBuilding->getComponent<CityBuildingViewComponent>();
        if ( pView != nullptr )
            pView->assignBuilding( getOwner()->getHandle(), static_cast<int32>( buildingIndex ) );
        return pBuilding->getHandle();
    }

    void CityViewComponent::spawnWalkers( GameObjectManager& manager, const CityCatalog& catalog, size_t count )
    {
        if ( _walkerPrefab.empty() )
            return;
        while ( _listWalkerObject.size() < count )
        {
            GameObject* const pWalker = spawnView( manager, _walkerPrefab, "CityWalker" );
            if ( pWalker == nullptr )
                return;
            MeshComponent* const pMesh = pWalker->getComponent<MeshComponent>();
            if ( pMesh != nullptr && pMesh->getMaterial() != nullptr && _listWalkerLook.empty() ) // 머티리얼 없이 만들면 빈 모습이 남아 다시 만들지 않는다
                prepareWalkerLooks( catalog, pMesh->getMaterial() );
            CityWalkerViewComponent* const pView = pWalker->getComponent<CityWalkerViewComponent>();
            if ( pView != nullptr )
                pView->assignWalker( getOwner()->getHandle(), static_cast<int32>( _listWalkerObject.size() ) );
            _listWalkerObject.push_back( pWalker->getHandle() );
        }
    }

    void CityViewComponent::prepareWalkerLooks( const CityCatalog& catalog, Material* pMaterial )
    {
        using Internal           = CityViewComponentInternal;
        const int32 serviceCount = static_cast<int32>( catalog.getServiceRows().size() );
        _walkerLookStride        = serviceCount + 1;
        _listWalkerLook.assign( static_cast<size_t>( Internal::kWalkerKindCount * _walkerLookStride ), nullptr );
        for ( int32 kind = 0; kind < Internal::kWalkerKindCount; ++kind )
        {
            for ( int32 service = constant::invalid_index::kInt32; service < serviceCount; ++service )
            {
                const CityWalkerKind walkerKind = static_cast<CityWalkerKind>( kind );
                _listWalkerLook[static_cast<size_t>( Internal::makeWalkerLookKey( _walkerLookStride, walkerKind, service ) )] =
                    _tintCache.acquire( pMaterial, computeWalkerColor( catalog, walkerKind, service ) );
            }
        }
    }

    GameObject* CityViewComponent::spawnView( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const utf8* pName )
    {
        AssetManager* const pAssets = game::findSubsystem<AssetManager>();
        return pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( &manager, prefab.path(), SpawnLifetime::Derived, pName ) : nullptr;
    }

    // ------------------------------------------------------------------------------
    // 색 — 땅 · 서비스 · 건물은 카탈로그의 행, 일꾼 종류(코드 동작이 다른 열거형)는 PROPERTY 다
    // ------------------------------------------------------------------------------
    float4 CityViewComponent::computeServiceColor( const CityCatalog& catalog, int32 serviceIndex )
    {
        if ( serviceIndex >= 0 )
            return catalog.getServiceRows()[static_cast<size_t>( serviceIndex )]._color;
        return float4{ CityViewComponentInternal::kNoServiceGray, CityViewComponentInternal::kNoServiceGray, CityViewComponentInternal::kNoServiceGray, 1.0f }; // 서비스 없음 — 칠할 일이 없는 칸이다.
    }

    float4 CityViewComponent::computeWalkerColor( const CityCatalog& catalog, CityWalkerKind kind, int32 serviceIndex ) const
    {
        switch ( kind )
        {
            case CityWalkerKind::Service:
                return computeServiceColor( catalog, serviceIndex );
            case CityWalkerKind::Trader:
                return _traderColor;
            case CityWalkerKind::Cart:
                return _cartColor;
        }
        return float4{ 1.0f, 1.0f, 1.0f, 1.0f };
    }
} // namespace sw
