#include "pch.h"

#include "Games/NileCity/NileDirectorComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/FourCcUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameAutoplay.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/UI/Input/UIInputMapView.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Base/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

#include "Games/NileCity/NileBuildingComponent.h"
#include "Games/NileCity/NileWalkerComponent.h"

namespace sw
{
    SW_LOG_CALLER( "NileDirector" );

    namespace
    {
        struct NileDirectorComponentInternal
        {
            static constexpr int32  kWalkerLookStride = 16; ///< 일꾼 모습 번호 = 종류 × 16 + 서비스
            static constexpr int32  kWalkerKindCount  = 3;
            static constexpr uint32 kStateTag         = FourCcUtil::make( "NILE" );
            static constexpr uint32 kStateVersion     = 3;

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 nilecity.audioevents.xml 이 정한다.
            static constexpr const utf8* kEventSelect  = "Select";
            static constexpr const utf8* kEventBuilt   = "Built";
            static constexpr const utf8* kEventBlocked = "Blocked";
            static constexpr const utf8* kEventEvolved = "Evolved";

            static int32 makeWalkerLookKey( CityWalkerKind kind, CityService service )
            {
                return static_cast<int32>( kind ) * kWalkerLookStride + static_cast<int32>( service );
            }

            /** @brief 오브젝트 메시를 위치와 크기로 둡니다. 메시가 없으면 nullptr 입니다. */
            static MeshComponent* placeMesh( GameObject* pObject, const float3& position, const float3& scale )
            {
                MeshComponent* pMesh = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
                if ( pMesh == nullptr )
                    return nullptr;
                pMesh->setLocalPosition( position );
                pMesh->setLocalScale( scale );
                return pMesh;
            }

            /** @brief 활성 씬의 첫 나일 디렉터입니다(감시 값). */
            static const NileDirectorComponent* findProbeDirector( const GameObjectManager* pManager )
            {
                const NileDirectorComponent* pFound = nullptr;
                if ( pManager != nullptr )
                {
                    pManager->forEachComponentOfType<NileDirectorComponent>( [&pFound]( const NileDirectorComponent* pDirector )
                    {
                        if ( pFound == nullptr )
                            pFound = pDirector;
                    } );
                }
                return pFound;
            }

            [[nodiscard]] static bool readPopulation( const GameObjectManager* pManager, float64& outValue )
            {
                const NileDirectorComponent* pDirector = findProbeDirector( pManager );
                if ( pDirector == nullptr )
                    return false;
                outValue = static_cast<float64>( pDirector->getPopulation() );
                return true;
            }

            [[nodiscard]] static bool readMoney( const GameObjectManager* pManager, float64& outValue )
            {
                const NileDirectorComponent* pDirector = findProbeDirector( pManager );
                if ( pDirector == nullptr )
                    return false;
                outValue = static_cast<float64>( pDirector->getMoney() );
                return true;
            }

            [[nodiscard]] static bool readMonth( const GameObjectManager* pManager, float64& outValue )
            {
                const NileDirectorComponent* pDirector = findProbeDirector( pManager );
                if ( pDirector == nullptr )
                    return false;
                outValue = static_cast<float64>( pDirector->getMonthCount() );
                return true;
            }
        };
    } // namespace

    /**
     * @brief `-gv_nileAutoPlay=1`: 디렉터의 자동 계획을 켭니다(씬의 `_bAutoPlay` 가 꺼져 있어도 켭니다). 입력 없이 도시가 커지는지 확인합니다.
     * @details 배포본으로도 돌릴 수 있게 남긴다: `App -gv_nileAutoPlay=1`. 달마다 `[Nile] month N pop P money M` 이 로그에 남는다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_nileAutoPlay, 0, "NileCity: 자동 계획으로 도시를 짓고 돌리기 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_nileAutoPlay, "NileCity", "Build and run the city from the auto plan" );

    SW_AUTOMATION_WATCH( nilePopulation, "Nile.Population", "Population of the city", &NileDirectorComponentInternal::readPopulation );
    SW_AUTOMATION_WATCH( nileMoney, "Nile.Money", "Money in the city treasury", &NileDirectorComponentInternal::readMoney );
    SW_AUTOMATION_WATCH( nileMonth, "Nile.Month", "Months the city has run", &NileDirectorComponentInternal::readMonth );
} // namespace sw

namespace sw
{
    NileDirectorComponent::NileDirectorComponent()
        : _cityDataPath{}
        , _autoPlanPath{}
        , _groundPrefab{}
        , _roadPrefab{}
        , _modelBuildingPrefab{}
        , _blockBuildingPrefab{}
        , _walkerPrefab{}
        , _cameraRig{}
        , _startingMoney{ 1500 }
        , _serviceDuration{ 60.0f }
        , _roadTileScale{ 4.8f }
        , _secondsPerMonth{ 20.0f }
        , _riverCenterX{ 9.0f }
        , _riverMeander{ 2.0f }
        , _riverMeanderRate{ 0.22f }
        , _riverHalfWidth{ 1.5f }
        , _eastFloodWidth{ 8.0f }
        , _westFloodWidth{ 3.0f }
        , _desertDuneHeight{ 1.5f }
        , _desertDuneRate{ 0.3f }
        , _desertStartX{ 40 }
        , _rockRidgeX{ 45 }
        , _roadX{ 17 }
        , _farmX{ 14 }
        , _secondBlockPeople{ 60 }
        , _thirdBlockPeople{ 160 }
        , _waterServiceColor{ 0.25f, 0.5f, 0.95f, 1.0f }
        , _religionServiceColor{ 0.95f, 0.8f, 0.2f, 1.0f }
        , _entertainmentServiceColor{ 0.7f, 0.35f, 0.85f, 1.0f }
        , _healthServiceColor{ 0.9f, 0.3f, 0.3f, 1.0f }
        , _educationServiceColor{ 0.2f, 0.75f, 0.7f, 1.0f }
        , _taxServiceColor{ 0.95f, 0.55f, 0.15f, 1.0f }
        , _producerModelTint{ 0.85f, 0.65f, 0.5f, 1.0f }
        , _storageModelTint{ 0.8f, 0.68f, 0.55f, 1.0f }
        , _marketModelTint{ 1.0f, 0.75f, 0.55f, 1.0f }
        , _houseBlockColor{ 0.75f, 0.62f, 0.42f, 1.0f }
        , _fieldBlockColor{ 0.78f, 0.8f, 0.3f, 1.0f }
        , _workshopBlockColor{ 0.6f, 0.42f, 0.3f, 1.0f }
        , _storageBlockColor{ 0.5f, 0.35f, 0.2f, 1.0f }
        , _marketBlockColor{ 0.9f, 0.45f, 0.2f, 1.0f }
        , _decorationBlockColor{ 0.3f, 0.75f, 0.35f, 1.0f }
        , _grassColor{ 0.45f, 0.62f, 0.3f, 1.0f }
        , _sandColor{ 0.86f, 0.76f, 0.5f, 1.0f }
        , _floodplainColor{ 0.36f, 0.42f, 0.2f, 1.0f }
        , _waterColor{ 0.2f, 0.45f, 0.75f, 1.0f }
        , _rockColor{ 0.5f, 0.47f, 0.44f, 1.0f }
        , _traderColor{ 1.0f, 0.6f, 0.2f, 1.0f }
        , _cartColor{ 0.45f, 0.3f, 0.15f, 1.0f }
        , _catalog{}
        , _city{}
        , _wallet{}
        , _clock{}
        , _planner{}
        , _listEvent{}
        , _listClockEvent{}
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

    void NileDirectorComponent::onBeginPlay()
    {
        GameDirectorComponent::onBeginPlay(); // 패드 가상 커서는 입력 맵(`virtualCursor`)이 켠다
    }

    void NileDirectorComponent::onEndPlay()
    {
        GameDirectorComponent::onEndPlay();
    }

    void NileDirectorComponent::onGameStarted()
    {
        SW_LOG_INFO( "[Nile] the city is founded on the Nile - WASD pan, wheel zoom, Q/E pick building, R road, left click build, right click demolish, "
                     "P auto plan, Space pause, - = speed, F1 status" );
    }

    void NileDirectorComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        const InputManager* pInput = game::findSubsystem<InputManager>();
        if ( pInput != nullptr )
            updateInput( deltaTime, *pInput );
        if ( isAutoPlanOn() )
            (void)_planner.advance( _city, _wallet, _catalog.getRoadCost() );
        if ( _bPaused == SW_FALSE )
        {
            _city.update( deltaTime * _timeScale );
            _clock.update( deltaTime * _timeScale ); // 배속은 디렉터가 곱한다(시계 배율은 1)
            settleMonths();
        }
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
        // 뷰가 워커에서 부른다. 컨테이너 인덱스 대신 포인터로 읽는다.
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
        if ( _catalog.loadFromAsset( AssetID{ _cityDataPath } ) == false )
        {
            SW_LOG_ERROR( "[Nile] %# could not be loaded - the city cannot be founded", _cityDataPath.c_str() );
            return false;
        }
        if ( _clock.initialize( makeClockSettings() ) == false )
            return false; // 범위 밖(달 길이 `_secondsPerMonth` 등)은 시계가 오류를 남겼다
        const CitySettings citySettings = makeCitySettings();
        _wallet.clear();
        _wallet.add( citySettings._currency, _startingMoney );
        if ( _city.initialize( &_catalog, NileCityPlanner::kMapWidth, NileCityPlanner::kMapHeight, citySettings, makeRefs() ) == false )
            return false; // 범위 밖 도시 설정은 도시가 오류를 남겼다
        if ( _city.claimEvents( _cityEventLease ) == false )
            return false; // 이 디렉터의 표뿐이라 일어날 수 없다 — 둘째 소비자가 생기면 게이트가 어서션을 남긴다
        const NileCityLayout layout = makeCityLayout();
        NileCityPlanner::paintTerrain( _city, layout );
        if ( _planner.reset( layout, AssetID{ _autoPlanPath } ) == false )
            return false; // 자동 계획을 읽지 못했다 — 이유는 플래너가 남겼다

        _listEvent.clear();
        _monthCount   = 0;
        _evolvedCount = 0;
        _listTool.clear();
        _listTool.push_back( nullptr );
        for ( const CityBuildingDef& def : _catalog.getBuildings() )
        {
            _listTool.push_back( &def );
        }
        _selectedTool = 0;
        return true;
    }

    WorldClockSettings NileDirectorComponent::makeClockSettings() const
    {
        WorldClockSettings settings;
        for ( const utf8* pMonth : { "Akhet1", "Akhet2", "Akhet3", "Akhet4", "Peret1", "Peret2", "Peret3", "Peret4", "Shemu1", "Shemu2", "Shemu3", "Shemu4" } )
        {
            settings._listSeason.push_back( hashed_string( pMonth ) );
        }
        settings._daysPerSeason = 1;
        settings._startHour     = 0.0f; // 첫 달도 꼭 한 달
        settings._secondsPerDay = _secondsPerMonth;
        return settings;
    }

    void NileDirectorComponent::settleMonths()
    {
        _listClockEvent.clear();
        _clock.drainEvents( _listClockEvent );
        for ( size_t index = 0; index < _listClockEvent.size(); ++index )
        {
            if ( _listClockEvent[index]._kind != WorldClockEvent::Kind::DayChanged )
                continue;
            // 같은 넘김의 알림(다음 날 넘김 전까지)에 해 넘김이 있으면 새 해다.
            bool bNewYear = false;
            for ( size_t next = index + 1; next < _listClockEvent.size() && _listClockEvent[next]._kind != WorldClockEvent::Kind::DayChanged; ++next )
            {
                bNewYear = bNewYear || _listClockEvent[next]._kind == WorldClockEvent::Kind::YearChanged;
            }
            _city.settleMonth( bNewYear );
        }
    }

    SessionStateRefs NileDirectorComponent::makeRefs()
    {
        SessionStateRefs refs;
        refs._pWallet = &_wallet;
        return refs;
    }

    CitySettings NileDirectorComponent::makeCitySettings() const
    {
        // 순회 서비스가 남는 시간을 키트 기본값(30초)보다 길게 잡는다. 이 도시의 도로망에서 일꾼 하나가 고리를 다 도는 데 그만큼 걸리기 때문이다.
        CitySettings settings;
        settings._serviceDuration = _serviceDuration;
        return settings;
    }

    NileCityLayout NileDirectorComponent::makeCityLayout() const
    {
        NileCityLayout layout;
        layout._riverCenterX      = _riverCenterX;
        layout._riverMeander      = _riverMeander;
        layout._riverMeanderRate  = _riverMeanderRate;
        layout._riverHalfWidth    = _riverHalfWidth;
        layout._eastFloodWidth    = _eastFloodWidth;
        layout._westFloodWidth    = _westFloodWidth;
        layout._desertDuneHeight  = _desertDuneHeight;
        layout._desertDuneRate    = _desertDuneRate;
        layout._desertStartX      = _desertStartX;
        layout._rockRidgeX        = _rockRidgeX;
        layout._roadX             = _roadX;
        layout._farmX             = _farmX;
        layout._secondBlockPeople = _secondBlockPeople;
        layout._thirdBlockPeople  = _thirdBlockPeople;
        return layout;
    }

    // ------------------------------------------------------------------------------
    // 색(PROPERTY)
    // ------------------------------------------------------------------------------
    float4 NileDirectorComponent::computeServiceColor( CityService service ) const
    {
        switch ( service )
        {
            case CityService::Water:
                return _waterServiceColor;
            case CityService::Religion:
                return _religionServiceColor;
            case CityService::Entertainment:
                return _entertainmentServiceColor;
            case CityService::Health:
                return _healthServiceColor;
            case CityService::Education:
                return _educationServiceColor;
            case CityService::Tax:
                return _taxServiceColor;
            case CityService::Count:
                break;
        }
        return float4{ 0.6f, 0.6f, 0.6f, 1.0f }; // 서비스 없음 — 칠할 일이 없는 칸이다.
    }

    float4 NileDirectorComponent::computeModelTint( const CityBuildingDef& def ) const
    {
        const float4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
        switch ( def._kind )
        {
            case CityBuildingKind::Service:
                return ( computeServiceColor( def._service ) + white ) * 0.5f;
            case CityBuildingKind::Producer:
                return _producerModelTint;
            case CityBuildingKind::Storage:
                return _storageModelTint;
            case CityBuildingKind::Market:
                return _marketModelTint;
            case CityBuildingKind::House:
            case CityBuildingKind::Decoration:
                break;
        }
        return white;
    }

    float4 NileDirectorComponent::computeBlockColor( const CityBuildingDef& def ) const
    {
        switch ( def._kind )
        {
            case CityBuildingKind::House:
                return _houseBlockColor;
            case CityBuildingKind::Service:
                return computeServiceColor( def._service );
            case CityBuildingKind::Producer:
                return def._bRequiresTerrain != SW_FALSE ? _fieldBlockColor : _workshopBlockColor;
            case CityBuildingKind::Storage:
                return _storageBlockColor;
            case CityBuildingKind::Market:
                return _marketBlockColor;
            case CityBuildingKind::Decoration:
                return _decorationBlockColor;
        }
        return float4{ 1.0f, 0.0f, 1.0f, 1.0f }; // 모르는 종류 — 눈에 띄는 자홍색
    }

    float4 NileDirectorComponent::computeTerrainColor( CityTerrain terrain ) const
    {
        switch ( terrain )
        {
            case CityTerrain::Grass:
                return _grassColor;
            case CityTerrain::Sand:
                return _sandColor;
            case CityTerrain::Floodplain:
                return _floodplainColor;
            case CityTerrain::Water:
                return _waterColor;
            case CityTerrain::Rock:
                return _rockColor;
        }
        return float4{ 1.0f, 0.0f, 1.0f, 1.0f }; // 모르는 땅 — 눈에 띄는 자홍색
    }

    float4 NileDirectorComponent::computeWalkerColor( CityWalkerKind kind, CityService service ) const
    {
        switch ( kind )
        {
            case CityWalkerKind::Service:
                return computeServiceColor( service );
            case CityWalkerKind::Trader:
                return _traderColor;
            case CityWalkerKind::Cart:
                return _cartColor;
        }
        return float4{ 1.0f, 1.0f, 1.0f, 1.0f };
    }

    // ------------------------------------------------------------------------------
    // 상태 쓰기 · 복원하기(핫 리로드 · 세이브)
    // ------------------------------------------------------------------------------
    int32 NileDirectorComponent::getPopulation() const
    {
        return _city.getPopulation();
    }

    int64 NileDirectorComponent::getMoney() const
    {
        return _wallet.getBalance( _city.getCurrency() );
    }

    void NileDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, NileDirectorComponentInternal::kStateTag, NileDirectorComponentInternal::kStateVersion );
        _city.writeState( outArchive );
        _planner.writeState( outArchive );
        _wallet.writeState( outArchive );
        _clock.writeState( outArchive );
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
        WorldClock      clock;
        if ( planner.reset( makeCityLayout(), AssetID{ _autoPlanPath } ) == false )
            return false; // 자동 계획을 읽지 못했다 — 이유는 플래너가 남겼다
        if ( clock.initialize( makeClockSettings() ) == false )
            return false;
        if ( city.initialize( &_catalog, NileCityPlanner::kMapWidth, NileCityPlanner::kMapHeight, makeCitySettings(), makeRefs() ) == false )
            return false;
        const bool bStateReadFailed = city.readState( archive ) == false || planner.readState( archive ) == false || wallet.readState( archive ) == false || clock.readState( archive ) == false;
        if ( bStateReadFailed )
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
        const bool bInvalidDirectorState = archive.isError() || archive.getRemainingBytes() != 0 || timeScale <= 0.0f;
        if ( bInvalidDirectorState )
            return false;
        _city            = std::move( city );
        _planner         = std::move( planner );
        _wallet          = std::move( wallet );
        _clock           = std::move( clock );
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
        const GridTopology& topology  = _city.getTopology();
        const size_t        tileCount = static_cast<size_t>( topology.getCellCount() );
        _listRoadObject.assign( tileCount, GameObjectHandle{} );
        _listRoadShown.assign( tileCount, SW_FALSE );
        for ( int32 tileIndex = 0; tileIndex < static_cast<int32>( tileCount ); ++tileIndex )
        {
            const int2 cell = topology.toCell( tileIndex );
            if ( _city.findTile( cell._x, cell._y )->_bRoad == SW_FALSE )
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
        // 줄마다 같은 땅이 이어지는 구간을 상자 하나로 만든다(타일마다 스폰하면 2000개가 되며, 땅은 바뀌지 않는다). 바위는 타일마다 솟은 상자다.
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
                    _tintCache.apply( *pMesh, computeTerrainColor( runTerrain ) );
                if ( runTerrain == CityTerrain::Rock )
                {
                    for ( int32 rockX = runStart; rockX < x; ++rockX )
                    {
                        GameObject*    pRock     = spawnPrefab( manager, _groundPrefab, "NileRock" );
                        MeshComponent* pRockMesh = Internal::placeMesh( pRock, float3{ static_cast<float32>( rockX ) + 0.5f, 0.4f, static_cast<float32>( y ) + 0.5f },
                                                                        float3{ 0.9f, 0.8f, 0.9f } );
                        if ( pRockMesh != nullptr )
                            _tintCache.apply( *pRockMesh, computeTerrainColor( CityTerrain::Rock ) );
                    }
                }
                runStart = x;
            }
        }
    }

    void NileDirectorComponent::spawnRoad( GameObjectManager& manager, int32 tileIndex )
    {
        // 도로 타일은 보도 조각을 타일 크기만큼 늘려 쓴다(팔레트 머티리얼 그대로).
        const int2  cell  = _city.getTopology().toCell( tileIndex );
        GameObject* pRoad = spawnPrefab( manager, _roadPrefab, "NileRoad" );
        (void)NileDirectorComponentInternal::placeMesh( pRoad, float3{ static_cast<float32>( cell._x ) + 0.5f, 0.0f, static_cast<float32>( cell._y ) + 0.5f },
                                                        float3{ _roadTileScale } );
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
            _tintCache.apply( *pMesh, bModel ? computeModelTint( *pDef ) : computeBlockColor( *pDef ) );
        NileBuildingComponent* pView = pBuilding->getComponent<NileBuildingComponent>();
        if ( pView != nullptr )
            pView->assignBuilding( getOwner()->getHandle(), buildingIndex );
    }

    void NileDirectorComponent::spawnWalkers( GameObjectManager& manager, size_t count )
    {
        // 일꾼 수만큼 미리 스폰하고 남으면 뷰가 숨긴다(오브젝트를 매 프레임 만들고 지우지 않는다). 목록 슬롯은 다른 일꾼에게 넘어갈 수 있다.
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
                    _tintCache.acquire( pMaterial, computeWalkerColor( walkerKind, walkerService ) );
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
    // 갱신(PrePhysics, 워커)
    // ------------------------------------------------------------------------------
    void NileDirectorComponent::collectViewChanges()
    {
        // 도로 타일은 깔렸거나 철거된 타일만 처리한다. 건물 타일은 새 건물이거나 허물렸거나 다른 건물로 다시 쓰인 타일만 처리한다. 단계와 사람은 뷰가 맞춘다.
        const GridTopology& topology = _city.getTopology();
        for ( int32 tileIndex = 0; tileIndex < static_cast<int32>( _listRoadShown.size() ); ++tileIndex )
        {
            const int2  cell  = topology.toCell( tileIndex );
            const uint8 bRoad = _city.findTile( cell._x, cell._y )->_bRoad != SW_FALSE ? SW_TRUE : SW_FALSE;
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
        // 카메라 이동과 확대는 리그(PostUpdate)가 읽는다. 여기서는 도구, 속도, 건설을 처리한다.
        (void)deltaTime;
        // 키는 입력 맵(`data/nile.input.xml`)이 정한다. 일시정지 메뉴 같은 UI 가 먹은 입력은 보이지 않는다.
        const UIInputMapView inputMap( input.getInputMap(), game::findSubsystem<UISystem>() );
        const int32          toolCount = static_cast<int32>( _listTool.size() );
        const bool           bNextTool = inputMap.wasActionTriggered( hashed_string( "Nile.NextTool" ) );
        const bool           bPrevTool = inputMap.wasActionTriggered( hashed_string( "Nile.PrevTool" ) );
        const bool           bRoadTool = inputMap.wasActionTriggered( hashed_string( "Nile.RoadTool" ) );
        if ( bNextTool )
            _selectedTool = ( _selectedTool + 1 ) % toolCount;
        if ( bPrevTool )
            _selectedTool = ( _selectedTool + toolCount - 1 ) % toolCount;
        if ( bRoadTool )
            _selectedTool = 0;
        const bool bToolChanged = bNextTool || bPrevTool || bRoadTool;
        if ( bToolChanged )
        {
            [[maybe_unused]] const CityBuildingDef* pDef = _listTool[static_cast<size_t>( _selectedTool )];
            SW_LOG_INFO( "[Nile] tool: %# ($%#)", getToolName(), pDef != nullptr ? pDef->_cost : _catalog.getRoadCost() );
            getSoundQueue().queueEvent( NileDirectorComponentInternal::kEventSelect );
        }
        if ( inputMap.wasActionTriggered( hashed_string( "Nile.Pause" ) ) )
        {
            _bPaused = _bPaused == SW_TRUE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO( "[Nile] %#", _bPaused == SW_TRUE ? "paused" : "running" );
        }
        const bool bFaster = inputMap.wasActionTriggered( hashed_string( "Nile.Faster" ) );
        if ( bFaster || inputMap.wasActionTriggered( hashed_string( "Nile.Slower" ) ) )
        {
            _timeScale = MathUtil::clamp( _timeScale * ( bFaster ? 2.0f : 0.5f ), 0.25f, 8.0f );
            SW_LOG_INFO( "[Nile] speed x%#", _timeScale );
        }
        if ( inputMap.wasActionTriggered( hashed_string( "Nile.ToggleAutoPlan" ) ) )
        {
            _bAutoPlanToggle = _bAutoPlanToggle == SW_TRUE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO( "[Nile] auto plan %# (step %# of %#)", _bAutoPlanToggle == SW_TRUE ? "on" : "off", _planner.getNextStep(), _planner.getStepCount() );
        }
        if ( inputMap.wasActionTriggered( hashed_string( "Nile.Status" ) ) )
            logStatus();

        updateCursor( input );
        if ( _bCursorValid == SW_FALSE )
            return;
        // 커서 타일은 포인터 위치로 정하고, 누르는 것은 액션이 정한다. 도로 도구는 누르는 동안 끌어서 연결한다.
        const hashed_string placeAction( "Nile.Place" );
        if ( inputMap.wasActionPressed( placeAction ) || ( _selectedTool == 0 && inputMap.isActionDown( placeAction ) ) )
            placeSelected();
        if ( inputMap.wasActionTriggered( hashed_string( "Nile.Demolish" ) ) && _city.demolish( _cursorTile._x, _cursorTile._y ) )
            SW_LOG_INFO( "[Nile] demolished (%#, %#)", _cursorTile._x, _cursorTile._y );
    }

    void NileDirectorComponent::updateCursor( const InputManager& input )
    {
        // 카메라 디렉터가 지금 낸 포즈로 포인터 아래 땅 타일을 찾는다. 디렉터는 PostPhysics 틱 뒤에 쓰므로 여기서 읽는 것은 지난 프레임의 시점이다.
        _bCursorValid                             = SW_FALSE;
        GameObjectManager*             pManager   = getObjectManager();
        const GameObject*              pRigObject = pManager != nullptr ? pManager->resolveGameObject( _cameraRig ) : nullptr;
        const CameraDirectorComponent* pRig       = pRigObject != nullptr ? pRigObject->getComponent<CameraDirectorComponent>() : nullptr;
        if ( pRig == nullptr )
            return;
        float3 point{};
        if ( pRig->findGroundPoint( input.getMousePositionNormalized(), 0.0f, point ) == false )
            return;
        _cursorTile   = int2{ static_cast<int32>( MathUtil::floor( point._x ) ), static_cast<int32>( MathUtil::floor( point._z ) ) };
        _bCursorValid = _city.findTile( _cursorTile._x, _cursorTile._y ) != nullptr ? SW_TRUE : SW_FALSE;
    }

    void NileDirectorComponent::placeSelected()
    {
        const CityBuildingDef* pDef = _listTool[static_cast<size_t>( _selectedTool )];
        if ( pDef == nullptr )
        {
            // 도로는 끌어서 깐다. 이미 도로인 타일은 조용히 넘어간다.
            const CityPlaceResult result = _city.placeRoad( _cursorTile._x, _cursorTile._y );
            if ( result == CityPlaceResult::NotEnoughMoney || result == CityPlaceResult::BadTerrain )
                SW_LOG_INFO( "[Nile] cannot lay road at (%#, %#): %#", _cursorTile._x, _cursorTile._y, getEnumName( result ) );
            return;
        }
        const CityPlaceResult result = _city.placeBuilding( pDef->_id, _cursorTile._x, _cursorTile._y );
        if ( result == CityPlaceResult::Ok )
        {
            SW_LOG_INFO( "[Nile] built %# at (%#, %#) - $%# left", pDef->_name.c_str(), _cursorTile._x, _cursorTile._y, _wallet.getBalance( _city.getCurrency() ) );
            getSoundQueue().queueEvent( NileDirectorComponentInternal::kEventBuilt );
        }
        else
        {
            SW_LOG_INFO( "[Nile] cannot build %# at (%#, %#): %#", pDef->_name.c_str(), _cursorTile._x, _cursorTile._y, getEnumName( result ) );
            getSoundQueue().queueEvent( NileDirectorComponentInternal::kEventBlocked );
        }
    }

    void NileDirectorComponent::drainEvents()
    {
        _listEvent.clear(); // drainEvents 는 뒤에 붙인다
        _city.drainEvents( _cityEventLease, _listEvent );
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
                    SW_LOG_INFO( "[Nile] year %# - the Nile flooded, farm fertility %#%%", _clock.getYear(), event._value );
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
            getSoundQueue().queueEvent( NileDirectorComponentInternal::kEventEvolved );
    }

    void NileDirectorComponent::logStatus() const
    {
        SW_LOG_INFO( "[Nile] year %# month %# · pop %# · money $%# · workers %#/%# · avg house level %# · culture %#%% · fertility %#%% · tool %# · plan %#/%#",
                     _clock.getYear(), _clock.getSeasonIndex() + 1, _city.getPopulation(), _wallet.getBalance( _city.getCurrency() ), _city.getEmployed(), _city.getWorkforce(),
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
