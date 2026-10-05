#include "pch.h"

#include "Games/NileCity/NileDirectorComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/GameAutoplay.h"
#include "Engine/Window/IWindow.h"

#include "GameFramework/Base/Camera/OrthoCameraRigComponent.h"
#include "GameFramework/Base/Framework/GameService.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"

#include "Games/NileCity/NileBuildingComponent.h"
#include "Games/NileCity/NileWalkerComponent.h"

namespace sw
{
    SW_LOG_CALLER( "NileDirector" );

    namespace
    {
        struct NileDirectorComponentInternal
        {
            static constexpr int32   kWalkerLookStride = 16; ///< 일꾼 모습 번호 = 종류 × 16 + 서비스
            static constexpr int32   kWalkerKindCount  = 3;
            static constexpr float32 kRoadTileScale    = 4.8f;        ///< 보도 조각(`path_short`, 0.2) × 4.8 = 0.96 m(칸 사이 틈은 옛 상자와 같다)
            static constexpr uint32  kStateTag         = 0x454C494Eu; ///< 'NILE'
            static constexpr uint32  kStateVersion     = 2;

            static constexpr const utf8* kSoundSelect  = "game/nilecity/sounds/select_003.ogg";
            static constexpr const utf8* kSoundBuilt   = "game/nilecity/sounds/confirmation_002.ogg";
            static constexpr const utf8* kSoundError   = "game/nilecity/sounds/error_002.ogg";
            static constexpr const utf8* kSoundEvolved = "game/nilecity/sounds/drop_003.ogg";

            static float4 computeServiceColor( CityService service )
            {
                switch ( service )
                {
                    case CityService::Water:
                        return float4{ 0.25f, 0.5f, 0.95f, 1.0f };
                    case CityService::Religion:
                        return float4{ 0.95f, 0.8f, 0.2f, 1.0f };
                    case CityService::Entertainment:
                        return float4{ 0.7f, 0.35f, 0.85f, 1.0f };
                    case CityService::Health:
                        return float4{ 0.9f, 0.3f, 0.3f, 1.0f };
                    case CityService::Education:
                        return float4{ 0.2f, 0.75f, 0.7f, 1.0f };
                    case CityService::Tax:
                        return float4{ 0.95f, 0.55f, 0.15f, 1.0f };
                    case CityService::Count:
                        break;
                }
                return float4{ 0.6f, 0.6f, 0.6f, 1.0f };
            }

            /** @brief 모델에 곱할 색입니다 — 서비스는 서비스 색을 흰 쪽으로 반쯤 섞어 벽이 그 색으로 읽히고, 집 · 나무 · 화분은 팔레트 그대로입니다. */
            static float4 computeModelTint( const CityBuildingDef& def )
            {
                const float4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
                switch ( def._kind )
                {
                    case CityBuildingKind::Service:
                        return ( computeServiceColor( def._service ) + white ) * 0.5f;
                    case CityBuildingKind::Producer:
                        return float4{ 0.85f, 0.65f, 0.5f, 1.0f };
                    case CityBuildingKind::Storage:
                        return float4{ 0.8f, 0.68f, 0.55f, 1.0f };
                    case CityBuildingKind::Market:
                        return float4{ 1.0f, 0.75f, 0.55f, 1.0f };
                    case CityBuildingKind::House:
                    case CityBuildingKind::Decoration:
                        break;
                }
                return white;
            }

            /** @brief 내장 도형으로 남는 건물(밭 · 조각상)의 색입니다. 집은 늘 모델이라 단계 색을 쓰지 않는다. */
            static float4 computeBlockColor( const CityBuildingDef& def )
            {
                switch ( def._kind )
                {
                    case CityBuildingKind::House:
                        return float4{ 0.75f, 0.62f, 0.42f, 1.0f };
                    case CityBuildingKind::Service:
                        return computeServiceColor( def._service );
                    case CityBuildingKind::Producer:
                        return def._bRequiresTerrain != SW_FALSE ? float4{ 0.78f, 0.8f, 0.3f, 1.0f } : float4{ 0.6f, 0.42f, 0.3f, 1.0f };
                    case CityBuildingKind::Storage:
                        return float4{ 0.5f, 0.35f, 0.2f, 1.0f };
                    case CityBuildingKind::Market:
                        return float4{ 0.9f, 0.45f, 0.2f, 1.0f };
                    case CityBuildingKind::Decoration:
                        return float4{ 0.3f, 0.75f, 0.35f, 1.0f };
                }
                return float4{ 1.0f, 0.0f, 1.0f, 1.0f };
            }

            static float4 computeTerrainColor( CityTerrain terrain )
            {
                switch ( terrain )
                {
                    case CityTerrain::Grass:
                        return float4{ 0.45f, 0.62f, 0.3f, 1.0f };
                    case CityTerrain::Sand:
                        return float4{ 0.86f, 0.76f, 0.5f, 1.0f };
                    case CityTerrain::Floodplain:
                        return float4{ 0.36f, 0.42f, 0.2f, 1.0f };
                    case CityTerrain::Water:
                        return float4{ 0.2f, 0.45f, 0.75f, 1.0f };
                    case CityTerrain::Rock:
                        return float4{ 0.5f, 0.47f, 0.44f, 1.0f };
                }
                return float4{ 1.0f, 0.0f, 1.0f, 1.0f };
            }

            static float4 computeWalkerColor( CityWalkerKind kind, CityService service )
            {
                switch ( kind )
                {
                    case CityWalkerKind::Service:
                        return computeServiceColor( service );
                    case CityWalkerKind::Trader:
                        return float4{ 1.0f, 0.6f, 0.2f, 1.0f };
                    case CityWalkerKind::Cart:
                        return float4{ 0.45f, 0.3f, 0.15f, 1.0f };
                }
                return float4{ 1.0f, 1.0f, 1.0f, 1.0f };
            }

            static int32 makeWalkerLookKey( CityWalkerKind kind, CityService service )
            {
                return static_cast<int32>( kind ) * kWalkerLookStride + static_cast<int32>( service );
            }

            /** @brief 오브젝트 메시를 자리 · 크기로 둡니다. 메시가 없으면 nullptr 입니다. */
            static MeshComponent* placeMesh( GameObject* pObject, const float3& position, const float3& scale )
            {
                MeshComponent* pMesh = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
                if ( pMesh == nullptr )
                    return nullptr;
                pMesh->setLocalPosition( position );
                pMesh->setLocalScale( scale );
                return pMesh;
            }
        };
    } // namespace

    /**
     * @brief `-gv_nileAutoPlay=1` — 디렉터의 자동 계획을 켭니다(씬의 `_bAutoPlay` 가 꺼져 있어도). 입력 없이 도시가 크는 확인.
     * @details 배포본으로도 돌릴 수 있게 남긴다: `App -gv_nileAutoPlay=1`. 달마다 `[Nile] month N pop P money M` 이 로그에 남는다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_nileAutoPlay, 0, "NileCity: 자동 계획으로 도시를 짓고 돌리기 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_nileAutoPlay, "NileCity", "Build and run the city from the auto plan" );
} // namespace sw

namespace sw
{
    NileDirectorComponent::NileDirectorComponent()
        : _cityDataPath{ "game/nilecity/data/city.xml" }
        , _groundPrefab{}
        , _roadPrefab{}
        , _modelBuildingPrefab{}
        , _blockBuildingPrefab{}
        , _walkerPrefab{}
        , _cameraRig{}
        , _startingMoney{ 1500 }
        , _serviceDuration{ 60.0f }
        , _catalog{}
        , _city{}
        , _wallet{}
        , _planner{}
        , _listEvent{}
        , _listTool{}
        , _listRoadObject{}
        , _listRoadShown{}
        , _listBuildingSlot{}
        , _listWalkerObject{}
        , _listPendingRoad{}
        , _listPendingBuilding{}
        , _tintCache{}
        , _listWalkerLook{}
        , _listModelMesh{}
        , _wantedWalkerCount{ 0 }
        , _cursorTile{ -1, -1 }
        , _timeScale{ 1.0f }
        , _selectedTool{ 0 }
        , _monthCount{ 0 }
        , _evolvedCount{ 0 }
        , _bCursorValid{ SW_FALSE }
        , _bPaused{ SW_FALSE }
        , _bAutoPlanToggle{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    NileDirectorComponent::~NileDirectorComponent() = default;

    void NileDirectorComponent::onGameStarted()
    {
        SW_LOG_INFO( "[Nile] the city is founded on the Nile - WASD pan, wheel zoom, Q/E pick building, R road, left click build, right click demolish, "
                     "P auto plan, Space pause, - = speed, F1 status" );
    }

    void NileDirectorComponent::tickGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        const InputManager* pInput = game::getService<InputManager>();
        if ( pInput != nullptr )
            updateInput( deltaTime, *pInput );
        if ( isAutoPlanOn() )
            (void)_planner.advance( _city, _wallet, _catalog.getRoadCost() );
        if ( _bPaused == SW_FALSE )
            _city.update( deltaTime * _timeScale );
        drainEvents();
        if ( areViewsSpawned() )
            collectViewChanges();
    }

    bool NileDirectorComponent::hasPendingSpawn() const
    {
        return _listPendingRoad.empty() == false || _listPendingBuilding.empty() == false || _wantedWalkerCount > _listWalkerObject.size();
    }

    void NileDirectorComponent::onViewsDespawned()
    {
        _listRoadObject.clear();
        _listRoadShown.clear();
        _listBuildingSlot.clear();
        _listWalkerObject.clear();
        _listPendingRoad.clear();
        _listPendingBuilding.clear();
    }

    bool NileDirectorComponent::findCursor( int2& outTile, int32& outSize ) const
    {
        if ( _bCursorValid == SW_FALSE || _listTool.empty() )
            return false;
        // 뷰가 워커에서 부른다 — 컨테이너 첨자 대신 포인터로 읽는다.
        const CityBuildingDef* pDef = _listTool.data()[_selectedTool];
        outTile                     = _cursorTile;
        outSize                     = pDef != nullptr ? pDef->_size : 1;
        return true;
    }

    const shared_ptr<MaterialInstance>& NileDirectorComponent::findWalkerLook( const CityWalker& walker ) const
    {
        static const shared_ptr<MaterialInstance> kNoLook{};
        const int32                               key = NileDirectorComponentInternal::makeWalkerLookKey( walker._kind, walker._service );
        if ( key < 0 || key >= static_cast<int32>( _listWalkerLook.size() ) )
            return kNoLook;
        return _listWalkerLook.data()[key];
    }

    // ------------------------------------------------------------------------------
    // 데이터
    // ------------------------------------------------------------------------------
    bool NileDirectorComponent::startGame()
    {
        if ( _catalog.loadFromResource( _cityDataPath ) == false )
        {
            SW_LOG_WARNING( "[Nile] %# could not be loaded - the city cannot be founded", _cityDataPath.c_str() );
            return false;
        }
        const CitySettings citySettings = makeCitySettings();
        _wallet.clear();
        _wallet.add( citySettings._currency, _startingMoney );
        _city.initialize( &_catalog, NileCityPlanner::kMapWidth, NileCityPlanner::kMapHeight, citySettings, makeRefs() );
        NileCityPlanner::paintTerrain( _city );
        _planner.reset();
        _listEvent.clear();
        _monthCount   = 0;
        _evolvedCount = 0;
        _listTool.clear();
        _listTool.push_back( nullptr );
        for ( const CityBuildingDef& def : _catalog.getBuildings() )
            _listTool.push_back( &def );
        _selectedTool = 0;
        return true;
    }

    GameStateRefs NileDirectorComponent::makeRefs()
    {
        GameStateRefs refs;
        refs._pWallet = &_wallet;
        return refs;
    }

    CitySettings NileDirectorComponent::makeCitySettings() const
    {
        // 순회 서비스가 남는 시간을 키트 기본(30 초)보다 길게 — 이 도시의 도로망에서 일꾼 하나가 고리를 다 도는 데 그만큼 걸린다.
        CitySettings settings;
        settings._serviceDuration = _serviceDuration;
        return settings;
    }

    // ------------------------------------------------------------------------------
    // 상태 쓰기 · 되살리기(핫 리로드 · 세이브)
    // ------------------------------------------------------------------------------
    void NileDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, NileDirectorComponentInternal::kStateTag, NileDirectorComponentInternal::kStateVersion );
        _city.writeState( outArchive );
        _planner.writeState( outArchive );
        _wallet.writeState( outArchive );
        outArchive << _timeScale;
        outArchive << _selectedTool;
        outArchive << _monthCount;
        outArchive << _evolvedCount;
        outArchive << static_cast<uint8>( _bPaused );
        outArchive << static_cast<uint8>( _bAutoPlanToggle );
    }

    bool NileDirectorComponent::readState( Archive& archive )
    {
        if ( StateArchiveUtil::readHeader( archive, NileDirectorComponentInternal::kStateTag, NileDirectorComponentInternal::kStateVersion ) == false )
            return false;
        CitySimulation  city;
        NileCityPlanner planner;
        Wallet          wallet;
        city.initialize( &_catalog, NileCityPlanner::kMapWidth, NileCityPlanner::kMapHeight, makeCitySettings(), makeRefs() );
        if ( city.readState( archive ) == false || planner.readState( archive ) == false || wallet.readState( archive ) == false )
            return false;
        float32 timeScale       = 1.0f;
        int32   selectedTool    = 0;
        int32   monthCount      = 0;
        int32   evolvedCount    = 0;
        uint8   bPaused         = SW_FALSE;
        uint8   bAutoPlanToggle = SW_FALSE;
        archive >> timeScale;
        archive >> selectedTool;
        archive >> monthCount;
        archive >> evolvedCount;
        archive >> bPaused;
        archive >> bAutoPlanToggle;
        if ( archive.isError() || archive.getRemainingBytes() != 0 || timeScale <= 0.0f )
            return false;
        _city            = std::move( city );
        _planner         = std::move( planner );
        _wallet          = std::move( wallet );
        _timeScale       = timeScale;
        _selectedTool    = _listTool.empty() ? 0 : MathUtil::clamp( selectedTool, 0, static_cast<int32>( _listTool.size() ) - 1 );
        _monthCount      = monthCount;
        _evolvedCount    = evolvedCount;
        _bPaused         = bPaused != SW_FALSE ? SW_TRUE : SW_FALSE;
        _bAutoPlanToggle = bAutoPlanToggle != SW_FALSE ? SW_TRUE : SW_FALSE;
        _listEvent.clear();
        return true;
    }

    void NileDirectorComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
            SW_LOG_INFO( "[Nile] city state restored - month %#, population %#, money %#", _monthCount, _city.getPopulation(), _wallet.getBalance( _city.getCurrency() ) );
        else
            SW_LOG_WARNING( "[Nile] the saved city state does not match this build - founding a new city" );
    }

    // ------------------------------------------------------------------------------
    // 스폰(틱 뒤 · 게임 스레드)
    // ------------------------------------------------------------------------------
    void NileDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews )
        {
            spawnAll( manager );
        }
        else
        {
            for ( const int32 tileIndex : _listPendingRoad )
            {
                GameObjectHandle& handle = _listRoadObject[static_cast<size_t>( tileIndex )];
                destroySpawned( manager, handle );
                if ( _listRoadShown[static_cast<size_t>( tileIndex )] == SW_TRUE )
                    spawnRoad( manager, tileIndex );
            }
            for ( const int32 buildingIndex : _listPendingBuilding )
            {
                destroySpawned( manager, _listBuildingSlot[static_cast<size_t>( buildingIndex )]._object );
                spawnBuilding( manager, buildingIndex );
            }
            spawnWalkers( manager, _wantedWalkerCount );
        }
        _listPendingRoad.clear();
        _listPendingBuilding.clear();
    }

    void NileDirectorComponent::spawnAll( GameObjectManager& manager )
    {
        preloadModels();
        spawnTerrain( manager );
        const size_t tileCount = static_cast<size_t>( _city.getWidth() * _city.getHeight() );
        _listRoadObject.assign( tileCount, GameObjectHandle{} );
        _listRoadShown.assign( tileCount, SW_FALSE );
        for ( int32 tileIndex = 0; tileIndex < static_cast<int32>( tileCount ); ++tileIndex )
        {
            if ( _city.findTile( tileIndex % _city.getWidth(), tileIndex / _city.getWidth() )->_bRoad == SW_FALSE )
                continue;
            _listRoadShown[static_cast<size_t>( tileIndex )] = SW_TRUE;
            spawnRoad( manager, tileIndex );
        }
        const vector<CityBuilding>& listBuilding = _city.getBuildings();
        _listBuildingSlot.assign( listBuilding.size(), BuildingSlot{} );
        for ( size_t buildingIndex = 0; buildingIndex < listBuilding.size(); ++buildingIndex )
        {
            const CityBuilding& building                = listBuilding[buildingIndex];
            _listBuildingSlot[buildingIndex]._pShownDef = building._bAlive != SW_FALSE ? building._pDef : nullptr;
            spawnBuilding( manager, static_cast<int32>( buildingIndex ) );
        }
        _wantedWalkerCount = _city.getWalkers().size();
        spawnWalkers( manager, _wantedWalkerCount );
    }

    void NileDirectorComponent::spawnTerrain( GameObjectManager& manager )
    {
        // 줄마다 같은 땅이 이어지는 구간을 상자 하나로(칸마다 세우면 2000 개 — 땅은 바뀌지 않는다). 바위는 칸마다 솟은 상자.
        using Internal = NileDirectorComponentInternal;
        for ( int32 y = 0; y < _city.getHeight(); ++y )
        {
            int32 runStart = 0;
            for ( int32 x = 1; x <= _city.getWidth(); ++x )
            {
                const CityTerrain runTerrain = _city.findTile( runStart, y )->_terrain;
                if ( x < _city.getWidth() && _city.findTile( x, y )->_terrain == runTerrain )
                    continue;
                const float32  length = static_cast<float32>( x - runStart );
                const float32  top    = runTerrain == CityTerrain::Water ? -0.15f : 0.0f;
                GameObject*    pRun   = spawnPrefab( manager, _groundPrefab, "NileGround" );
                MeshComponent* pMesh  = Internal::placeMesh( pRun, float3{ static_cast<float32>( runStart ) + length * 0.5f, top - 0.1f, static_cast<float32>( y ) + 0.5f },
                                                             float3{ length, 0.2f, 1.0f } );
                if ( pMesh != nullptr )
                    _tintCache.apply( *pMesh, Internal::computeTerrainColor( runTerrain ) );
                if ( runTerrain == CityTerrain::Rock )
                {
                    for ( int32 rockX = runStart; rockX < x; ++rockX )
                    {
                        GameObject*    pRock     = spawnPrefab( manager, _groundPrefab, "NileRock" );
                        MeshComponent* pRockMesh = Internal::placeMesh( pRock, float3{ static_cast<float32>( rockX ) + 0.5f, 0.4f, static_cast<float32>( y ) + 0.5f },
                                                                        float3{ 0.9f, 0.8f, 0.9f } );
                        if ( pRockMesh != nullptr )
                            _tintCache.apply( *pRockMesh, Internal::computeTerrainColor( CityTerrain::Rock ) );
                    }
                }
                runStart = x;
            }
        }
    }

    void NileDirectorComponent::spawnRoad( GameObjectManager& manager, int32 tileIndex )
    {
        // 도로 칸 — 보도 조각을 칸만 하게 늘린다(팔레트 머티리얼 그대로).
        const int32 x     = tileIndex % _city.getWidth();
        const int32 y     = tileIndex / _city.getWidth();
        GameObject* pRoad = spawnPrefab( manager, _roadPrefab, "NileRoad" );
        (void)NileDirectorComponentInternal::placeMesh( pRoad, float3{ static_cast<float32>( x ) + 0.5f, 0.0f, static_cast<float32>( y ) + 0.5f },
                                                        float3{ NileDirectorComponentInternal::kRoadTileScale } );
        _listRoadObject[static_cast<size_t>( tileIndex )] = pRoad != nullptr ? pRoad->getHandle() : GameObjectHandle{};
    }

    void NileDirectorComponent::spawnBuilding( GameObjectManager& manager, int32 buildingIndex )
    {
        BuildingSlot&          slot = _listBuildingSlot[static_cast<size_t>( buildingIndex )];
        const CityBuildingDef* pDef = slot._pShownDef;
        if ( pDef == nullptr )
            return;
        // 모델이 있는 건물은 팔레트 머티리얼, 밭 · 조각상은 색 상자다. 단계 · 사람에 따른 모델 · 높이는 뷰가 맞춘다.
        const bool  bModel    = NileBuildingComponent::hasModel( *pDef );
        GameObject* pBuilding = spawnPrefab( manager, bModel ? _modelBuildingPrefab : _blockBuildingPrefab, "NileBuilding" );
        if ( pBuilding == nullptr )
            return;
        slot._object         = pBuilding->getHandle();
        MeshComponent* pMesh = pBuilding->getComponent<MeshComponent>();
        if ( pMesh != nullptr )
        {
            using Internal = NileDirectorComponentInternal;
            _tintCache.apply( *pMesh, bModel ? Internal::computeModelTint( *pDef ) : Internal::computeBlockColor( *pDef ) );
        }
        NileBuildingComponent* pView = pBuilding->getComponent<NileBuildingComponent>();
        if ( pView != nullptr )
            pView->assignBuilding( getOwner()->getHandle(), buildingIndex );
    }

    void NileDirectorComponent::spawnWalkers( GameObjectManager& manager, size_t count )
    {
        // 일꾼 수만큼 미리 세우고 남으면 뷰가 숨긴다(오브젝트를 매 프레임 만들고 지우지 않는다). 목록 자리는 다른 일꾼에게 넘어갈 수 있다.
        const GameObjectHandle director = getOwner()->getHandle();
        while ( _listWalkerObject.size() < count )
        {
            GameObject* pWalker = spawnPrefab( manager, _walkerPrefab, "NileWalker" );
            if ( pWalker == nullptr )
                return;
            MeshComponent* pMesh = pWalker->getComponent<MeshComponent>();
            if ( pMesh != nullptr && _listWalkerLook.empty() )
                prepareWalkerLooks( pMesh->getMaterial() );
            NileWalkerComponent* pView = pWalker->getComponent<NileWalkerComponent>();
            if ( pView != nullptr )
                pView->assignWalker( director, static_cast<int32>( _listWalkerObject.size() ) );
            _listWalkerObject.push_back( pWalker->getHandle() );
        }
    }

    void NileDirectorComponent::prepareWalkerLooks( Material* pMaterial )
    {
        using Internal = NileDirectorComponentInternal;
        _listWalkerLook.assign( static_cast<size_t>( Internal::kWalkerKindCount * Internal::kWalkerLookStride ), nullptr );
        for ( int32 kind = 0; kind < Internal::kWalkerKindCount; ++kind )
        {
            for ( int32 service = 0; service <= static_cast<int32>( CityService::Count ); ++service )
            {
                const CityWalkerKind walkerKind    = static_cast<CityWalkerKind>( kind );
                const CityService    walkerService = static_cast<CityService>( service );
                _listWalkerLook[static_cast<size_t>( Internal::makeWalkerLookKey( walkerKind, walkerService ) )] =
                    _tintCache.acquire( pMaterial, Internal::computeWalkerColor( walkerKind, walkerService ) );
            }
        }
    }

    void NileDirectorComponent::preloadModels()
    {
        if ( _listModelMesh.empty() == false )
            return;
        vector<string> listModelPath;
        NileBuildingComponent::collectModelPaths( listModelPath );
        for ( const string& path : listModelPath )
        {
            shared_ptr<Mesh> mesh = MeshCache::acquire( path );
            if ( mesh != nullptr )
                _listModelMesh.push_back( std::move( mesh ) );
        }
    }

    // ------------------------------------------------------------------------------
    // 갱신(PrePhysics — 워커)
    // ------------------------------------------------------------------------------
    void NileDirectorComponent::collectViewChanges()
    {
        // 도로 칸 — 깔렸거나 걷힌 칸만. 건물 칸 — 새 건물이거나 허물렸거나 다른 건물로 다시 쓰인 칸만. 단계 · 사람은 뷰가 맞춘다.
        const int32 width = _city.getWidth();
        for ( int32 tileIndex = 0; tileIndex < static_cast<int32>( _listRoadShown.size() ); ++tileIndex )
        {
            const uint8 bRoad = _city.findTile( tileIndex % width, tileIndex / width )->_bRoad != SW_FALSE ? SW_TRUE : SW_FALSE;
            uint8&      shown = _listRoadShown[static_cast<size_t>( tileIndex )];
            if ( shown == bRoad )
                continue;
            shown = bRoad;
            _listPendingRoad.push_back( tileIndex );
        }
        const vector<CityBuilding>& listBuilding = _city.getBuildings();
        if ( _listBuildingSlot.size() < listBuilding.size() )
            _listBuildingSlot.resize( listBuilding.size() );
        for ( size_t buildingIndex = 0; buildingIndex < listBuilding.size(); ++buildingIndex )
        {
            const CityBuilding&    building = listBuilding[buildingIndex];
            const CityBuildingDef* pWanted  = building._bAlive != SW_FALSE ? building._pDef : nullptr;
            BuildingSlot&          slot     = _listBuildingSlot[buildingIndex];
            if ( slot._pShownDef == pWanted )
                continue;
            slot._pShownDef = pWanted;
            _listPendingBuilding.push_back( static_cast<int32>( buildingIndex ) );
        }
        _wantedWalkerCount = MathUtil::max( _wantedWalkerCount, _city.getWalkers().size() );
    }

    void NileDirectorComponent::updateInput( float32 deltaTime, const InputManager& input )
    {
        // 카메라 이동 · 확대는 리그(PostUpdate)가 읽는다. 여기는 도구 · 속도 · 짓기.
        (void)deltaTime;
        const int32 toolCount = static_cast<int32>( _listTool.size() );
        if ( input.wasKeyPressed( Key::E ) || input.wasKeyPressed( Key::Tab ) )
            _selectedTool = ( _selectedTool + 1 ) % toolCount;
        if ( input.wasKeyPressed( Key::Q ) )
            _selectedTool = ( _selectedTool + toolCount - 1 ) % toolCount;
        if ( input.wasKeyPressed( Key::R ) )
            _selectedTool = 0;
        if ( input.wasKeyPressed( Key::E ) || input.wasKeyPressed( Key::Tab ) || input.wasKeyPressed( Key::Q ) || input.wasKeyPressed( Key::R ) )
        {
            [[maybe_unused]] const CityBuildingDef* pDef = _listTool[static_cast<size_t>( _selectedTool )];
            SW_LOG_INFO( "[Nile] tool: %# ($%#)", getToolName(), pDef != nullptr ? pDef->_cost : _catalog.getRoadCost() );
            getSoundQueue().queueClip( NileDirectorComponentInternal::kSoundSelect );
        }
        if ( input.wasKeyPressed( Key::Space ) )
        {
            _bPaused = _bPaused == SW_TRUE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO( "[Nile] %#", _bPaused == SW_TRUE ? "paused" : "running" );
        }
        if ( input.wasKeyPressed( Key::Minus ) || input.wasKeyPressed( Key::Equal ) )
        {
            _timeScale = MathUtil::clamp( _timeScale * ( input.wasKeyPressed( Key::Equal ) ? 2.0f : 0.5f ), 0.25f, 8.0f );
            SW_LOG_INFO( "[Nile] speed x%#", _timeScale );
        }
        if ( input.wasKeyPressed( Key::P ) )
        {
            _bAutoPlanToggle = _bAutoPlanToggle == SW_TRUE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO( "[Nile] auto plan %# (step %# of %#)", _bAutoPlanToggle == SW_TRUE ? "on" : "off", _planner.getNextStep(), _planner.getStepCount() );
        }
        if ( input.wasKeyPressed( Key::F1 ) )
            logStatus();

        updateCursor( input );
        if ( _bCursorValid == SW_FALSE )
            return;
        if ( input.wasMouseButtonPressed( MouseButton::Left ) || ( _selectedTool == 0 && input.isMouseButtonDown( MouseButton::Left ) ) )
            placeSelected();
        if ( input.wasMouseButtonPressed( MouseButton::Right ) && _city.demolish( _cursorTile._x, _cursorTile._y ) )
            SW_LOG_INFO( "[Nile] demolished (%#, %#)", _cursorTile._x, _cursorTile._y );
    }

    void NileDirectorComponent::updateCursor( const InputManager& input )
    {
        // 리그의 직교 시점으로 마우스 아래 땅 칸을 찾는다. 리그는 PostUpdate 에서 쓰므로 여기서 읽는 것은 지난 프레임의 시점이다.
        _bCursorValid                             = SW_FALSE;
        GameObjectManager*             pManager   = getObjectManager();
        const GameObject*              pRigObject = pManager != nullptr ? pManager->resolveGameObject( _cameraRig ) : nullptr;
        const OrthoCameraRigComponent* pRig       = pRigObject != nullptr ? pRigObject->getComponent<OrthoCameraRigComponent>() : nullptr;
        if ( pRig == nullptr )
            return;
        const IWindow* pWindow = IWindow::getActiveWindow();
        const float32  aspect  = pWindow != nullptr && pWindow->getHeight() > 0
                                   ? static_cast<float32>( pWindow->getWidth() ) / static_cast<float32>( pWindow->getHeight() )
                                   : 16.0f / 9.0f;
        float3         point{};
        if ( pRig->findGroundPoint( input.getMousePositionNormalized(), aspect, 0.0f, point ) == false )
            return;
        _cursorTile   = int2{ static_cast<int32>( MathUtil::floor( point._x ) ), static_cast<int32>( MathUtil::floor( point._z ) ) };
        _bCursorValid = _city.findTile( _cursorTile._x, _cursorTile._y ) != nullptr ? SW_TRUE : SW_FALSE;
    }

    void NileDirectorComponent::placeSelected()
    {
        const CityBuildingDef* pDef = _listTool[static_cast<size_t>( _selectedTool )];
        if ( pDef == nullptr )
        {
            // 도로는 끌어서 깐다 — 이미 도로인 칸은 조용히 넘어간다.
            const CityPlaceResult result = _city.placeRoad( _cursorTile._x, _cursorTile._y );
            if ( result == CityPlaceResult::NotEnoughMoney || result == CityPlaceResult::BadTerrain )
                SW_LOG_INFO( "[Nile] cannot lay road at (%#, %#): %#", _cursorTile._x, _cursorTile._y, toString( result ) );
            return;
        }
        const CityPlaceResult result = _city.placeBuilding( pDef->_id, _cursorTile._x, _cursorTile._y );
        if ( result == CityPlaceResult::Ok )
        {
            SW_LOG_INFO( "[Nile] built %# at (%#, %#) - $%# left", pDef->_name.c_str(), _cursorTile._x, _cursorTile._y, _wallet.getBalance( _city.getCurrency() ) );
            getSoundQueue().queueClip( NileDirectorComponentInternal::kSoundBuilt );
        }
        else
        {
            SW_LOG_INFO( "[Nile] cannot build %# at (%#, %#): %#", pDef->_name.c_str(), _cursorTile._x, _cursorTile._y, toString( result ) );
            getSoundQueue().queueClip( NileDirectorComponentInternal::kSoundError );
        }
    }

    void NileDirectorComponent::drainEvents()
    {
        _listEvent.clear(); // drainEvents 는 뒤에 붙인다
        _city.drainEvents( _listEvent );
        bool bHouseEvolved = false;
        for ( const CityEvent& event : _listEvent )
        {
            switch ( event._kind )
            {
                case CityEvent::Kind::MonthEnded:
                {
                    ++_monthCount;
                    SW_LOG_INFO( "[Nile] month %# pop %# money %# (net %#, workers %#/%#, houses up %#, avg level %#, culture %#%%)", _monthCount, _city.getPopulation(),
                                 _wallet.getBalance( _city.getCurrency() ), event._value, _city.getEmployed(), _city.getWorkforce(), _evolvedCount, _city.computeAverageHouseLevel(),
                                 static_cast<int32>( _city.computeCultureCoverage() * 100.0f ) );
                    _evolvedCount = 0;
                    break;
                }
                case CityEvent::Kind::HouseEvolved:
                {
                    ++_evolvedCount;
                    bHouseEvolved = true;
                    break;
                }
                case CityEvent::Kind::Flood:
                {
                    SW_LOG_INFO( "[Nile] year %# - the Nile flooded, farm fertility %#%%", _city.getYear(), event._value );
                    break;
                }
                case CityEvent::Kind::HouseDevolved:
                case CityEvent::Kind::GoodsDelivered:
                {
                    break;
                }
            }
        }
        // 한 프레임에 여럿이 올라도 소리는 한 번.
        if ( bHouseEvolved )
            getSoundQueue().queueClip( NileDirectorComponentInternal::kSoundEvolved );
    }

    void NileDirectorComponent::logStatus() const
    {
        SW_LOG_INFO( "[Nile] year %# month %# · pop %# · money $%# · workers %#/%# · avg house level %# · culture %#%% · fertility %#%% · tool %# · plan %#/%#",
                     _city.getYear(), _city.getMonth() + 1, _city.getPopulation(), _wallet.getBalance( _city.getCurrency() ), _city.getEmployed(), _city.getWorkforce(),
                     _city.computeAverageHouseLevel(), static_cast<int32>( _city.computeCultureCoverage() * 100.0f ), static_cast<int32>( _city.getFloodFertility() * 100.0f ),
                     getToolName(), _planner.getNextStep(), _planner.getStepCount() );
    }

    const utf8* NileDirectorComponent::getToolName() const
    {
        const CityBuildingDef* pDef = _listTool.empty() ? nullptr : _listTool[static_cast<size_t>( _selectedTool )];
        return pDef != nullptr ? pDef->_name.c_str() : "Road";
    }

    bool NileDirectorComponent::isAutoPlanOn() const
    {
        return isAutoPlayOn() || _bAutoPlanToggle == SW_TRUE;
    }

} // namespace sw
