#include "pch.h"

#include "Games/NileCity/NileGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "Games/NileCity/NileBuildingComponent.h"
#include "Games/NileCity/NileWalkerComponent.h"

namespace sw
{
    namespace
    {
        struct NileGameModeComponentInternal
        {
            static constexpr int32  kWalkerLookStride = 16; ///< 일꾼 모습 번호 = 종류 × 16 + 서비스
            static constexpr int32  kWalkerKindCount  = 3;
            static constexpr uint32 kStateTag         = FourCCUtil::make( "NILE" );
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

            /** @brief 감시 값 `Nile.CursorCellX` · `Nile.CursorCellZ` 입니다. 커서가 땅 밖이면 값이 없습니다. */
            static bool readCursorCellX( const NileGameModeComponent& mode, float64& outValue )
            {
                int2 tile{};
                if ( mode.findCursorTile( tile ) == false )
                    return false;
                outValue = static_cast<float64>( tile._x );
                return true;
            }
            static bool readCursorCellZ( const NileGameModeComponent& mode, float64& outValue )
            {
                int2 tile{};
                if ( mode.findCursorTile( tile ) == false )
                    return false;
                outValue = static_cast<float64>( tile._y );
                return true;
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
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( nilePopulation, "Nile.Population", "Population of the city", NileGameModeComponent, &NileGameModeComponent::getPopulation );
    SW_AUTOMATION_GETTER_WATCH( nileMoney, "Nile.Money", "Money in the city treasury", NileGameModeComponent, &NileGameModeComponent::getMoney );
    SW_AUTOMATION_GETTER_WATCH( nileMonth, "Nile.Month", "Months the city has run", NileGameModeComponent, &NileGameModeComponent::getMonthCount );
    SW_AUTOMATION_GETTER_WATCH( nileTool, "Nile.Tool", "Selected build tool (0 road, then the catalog buildings in order)", NileGameModeComponent, &NileGameModeComponent::getSelectedTool );
    SW_AUTOMATION_GETTER_WATCH( nileBuildCount, "Nile.BuildCount", "Roads laid and buildings placed this game (successes only)", NileGameModeComponent, &NileGameModeComponent::getBuildCount );
    SW_AUTOMATION_GETTER_WATCH( nileDemolishCount, "Nile.DemolishCount", "Tiles demolished this game", NileGameModeComponent, &NileGameModeComponent::getDemolishCount );
    SW_AUTOMATION_COMPONENT_WATCH( nileCursorCellX, "Nile.CursorCellX", "Ground tile X under the pointer; absent while the pointer is off the ground", NileGameModeComponent,
                                   &NileGameModeComponentInternal::readCursorCellX );
    SW_AUTOMATION_COMPONENT_WATCH( nileCursorCellZ, "Nile.CursorCellZ", "Ground tile Z (city row) under the pointer; absent while the pointer is off the ground", NileGameModeComponent,
                                   &NileGameModeComponentInternal::readCursorCellZ );
} // namespace sw

namespace sw
{
    NileGameModeComponent::NileGameModeComponent()
        : _cityDataPath{}
        , _buildingViewPath{}
        , _autoPlanPath{}
        , _groundPrefab{}
        , _roadPrefab{}
        , _modelBuildingPrefab{}
        , _blockBuildingPrefab{}
        , _walkerPrefab{}
        , _cameraRig{}
        , _startingMoney{ 1500 }
        , _roadTileScale{ 4.8f }
        , _secondsPerMonth{ 20.0f }
        , _riverCenterX{ 9.0f }
        , _riverMeander{ 2.0f }
        , _riverMeanderRate{ Radians::fromRadians( 0.22f ) }
        , _riverHalfWidth{ 1.5f }
        , _eastFloodWidth{ 8.0f }
        , _westFloodWidth{ 3.0f }
        , _desertDuneHeight{ 1.5f }
        , _desertDuneRate{ Radians::fromRadians( 0.3f ) }
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
        , _pCatalog{}
        , _pBuildingViews{}
        , _city{}
        , _wallet{}
        , _clock{}
        , _planner{}
        , _listEvent{}
        , _listClockEvent{}
        , _listTool{}
        , _roadViews{}
        , _buildingViews{}
        , _listWalkerObject{}
        , _tintCache{}
        , _listWalkerLook{}
        , _listModelMesh{}
        , _wantedWalkerCount{ 0 }
        , _cursorTile{ -1, -1 }
        , _speed{}
        , _selectedTool{ 0 }
        , _monthCount{ 0 }
        , _evolvedCount{ 0 }
        , _buildCount{}
        , _demolishCount{}
        , _bCursorValid{ SW_FALSE }
        , _bAutoPlanToggle{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    NileGameModeComponent::~NileGameModeComponent() = default;

    void NileGameModeComponent::onBeginPlay()
    {
        GameModeComponent::onBeginPlay(); // 패드 가상 커서는 입력 맵(`virtualCursor`)이 켠다
    }

    void NileGameModeComponent::onEndPlay()
    {
        GameModeComponent::onEndPlay();
    }

    void NileGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "NileGameMode.CityFoundedNileWASD", "[Nile] the city is founded on the Nile - WASD pan, wheel zoom, Q/E pick building, R road, left click build, right click demolish, "
                                                            "P auto plan, Space pause, - = speed, F1 status" );
    }

    void NileGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        const InputManager* pInput = game::findSubsystem<InputManager>();
        if ( pInput != nullptr )
            updateInput( deltaTime, *pInput );
        if ( isAutoPlanOn() )
            (void)_planner.advance( _city, _wallet, _pCatalog->getRoadCost() );
        if ( _speed.isPaused() == false )
        {
            // 시계는 도시가 진행한 시간으로 민다 — 따로 곱하면 프레임 상한에 잘린 도시와 어긋난다(감사 K-6). 배속은 디렉터가 쥔다(시계 배율은 1).
            _clock.update( _city.update( deltaTime, _speed.getTimeScale() ) );
            settleMonths();
        }
        pollEvents();
        if ( areViewsSpawned() )
            collectViewChanges();
    }

    bool NileGameModeComponent::hasPendingSpawn() const
    {
        return _roadViews.hasPending() || _buildingViews.hasPending() || _wantedWalkerCount > _listWalkerObject.size();
    }

    void NileGameModeComponent::onViewsDespawned()
    {
        _roadViews.clear();
        _buildingViews.clear();
        _listWalkerObject.clear();
    }

    bool NileGameModeComponent::findCursor( int2& outTile, int32& outSize ) const
    {
        if ( _bCursorValid == SW_FALSE || _listTool.empty() )
            return false;
        // 뷰가 워커에서 부른다. 컨테이너 인덱스 대신 포인터로 읽는다.
        const CityBuildingDef* pDef = _listTool.data()[_selectedTool];
        outTile                     = _cursorTile;
        outSize                     = pDef != nullptr ? pDef->_size : 1;
        return true;
    }

    const shared_ptr<MaterialInstance>& NileGameModeComponent::findWalkerLook( const CityWalker& walker ) const
    {
        static const shared_ptr<MaterialInstance> kNoLook{};
        const int32                               key = NileGameModeComponentInternal::makeWalkerLookKey( walker._kind, walker._service );
        if ( key < 0 || key >= static_cast<int32>( _listWalkerLook.size() ) )
            return kNoLook;
        return _listWalkerLook.data()[key];
    }

    // ------------------------------------------------------------------------------
    // 데이터
    // ------------------------------------------------------------------------------
    bool NileGameModeComponent::startGame()
    {
        if ( acquireRequired( _cityDataPath, _pCatalog ) == false || acquireRequired( _buildingViewPath, _pBuildingViews ) == false )
            return false;
        // 집은 단계마다 모델이 바뀐다 — 뷰 표에 빠진 단계가 있으면 그 단계만 조용히 색 상자가 되므로 시작할 때 막는다.
        for ( const CityBuildingDef& def : _pCatalog->getBuildings() )
        {
            if ( def._kind != CityBuildingKind::House )
                continue;
            bool bComplete = NileBuildingComponent::findView( *_pBuildingViews, def, 0, false ) != nullptr; // 빈 땅
            for ( int32 level = 0; level < static_cast<int32>( _pCatalog->getHouseLevels().size() ); ++level )
            {
                bComplete = bComplete && NileBuildingComponent::findView( *_pBuildingViews, def, level, true ) != nullptr;
            }
            if ( bComplete )
                continue;
            SW_LOG_ERROR_ID( "NileGameMode.HouseViewMissing", "[Nile] %# needs '%#.empty' and '%#.<level>' rows for every house level - the city does not open", _buildingViewPath.c_str(),
                             def._id.c_str(), def._id.c_str() );
            return false;
        }
        if ( _clock.initialize( makeClockSettings() ) == false )
            return false; // 범위 밖(달 길이 `_secondsPerMonth` 등)은 시계가 오류를 남겼다
        _wallet.clear();
        _wallet.add( _pCatalog->getRules()._currency, _startingMoney );
        if ( _city.initialize( _pCatalog.get(), NileCityPlanner::kMapWidth, NileCityPlanner::kMapHeight, makeRefs() ) == false )
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
        _buildCount.reset();
        _demolishCount.reset();
        _listTool.clear();
        _listTool.push_back( nullptr );
        for ( const CityBuildingDef& def : _pCatalog->getBuildings() )
        {
            _listTool.push_back( &def );
        }
        _selectedTool = 0;
        return true;
    }

    WorldClockSettings NileGameModeComponent::makeClockSettings() const
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

    void NileGameModeComponent::settleMonths()
    {
        _listClockEvent.clear();
        _clock.pollEvents( _listClockEvent );
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

    GameStateRefs NileGameModeComponent::makeRefs()
    {
        GameStateRefs refs;
        refs._pWallet = &_wallet;
        return refs;
    }

    NileCityLayout NileGameModeComponent::makeCityLayout() const
    {
        NileCityLayout layout;
        layout._riverCenterX      = _riverCenterX;
        layout._riverMeander      = _riverMeander;
        layout._riverMeanderRate  = _riverMeanderRate.radians();
        layout._riverHalfWidth    = _riverHalfWidth;
        layout._eastFloodWidth    = _eastFloodWidth;
        layout._westFloodWidth    = _westFloodWidth;
        layout._desertDuneHeight  = _desertDuneHeight;
        layout._desertDuneRate    = _desertDuneRate.radians();
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
    float4 NileGameModeComponent::computeServiceColor( CityService service ) const
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

    float4 NileGameModeComponent::computeModelTint( const CityBuildingDef& def ) const
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

    float4 NileGameModeComponent::computeBlockColor( const CityBuildingDef& def ) const
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

    float4 NileGameModeComponent::computeTerrainColor( CityTerrain terrain ) const
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

    float4 NileGameModeComponent::computeWalkerColor( CityWalkerKind kind, CityService service ) const
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
    int32 NileGameModeComponent::getPopulation() const
    {
        return _city.getPopulation();
    }

    int64 NileGameModeComponent::getMoney() const
    {
        return _wallet.getBalance( _city.getCurrency() );
    }

    void NileGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( NileGameModeComponentInternal::kStateTag, NileGameModeComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        CitySimulation  restoredCity;
        NileCityPlanner restoredPlanner;
        Wallet          restoredWallet;
        WorldClock      restoredClock;
        if ( io.isReading() )
        {
            // 자동 계획을 읽지 못하면 이유는 플래너가 남겼다
            if ( io.check( restoredPlanner.reset( makeCityLayout(), AssetID{ _autoPlanPath } ) ) == false || io.check( restoredClock.initialize( makeClockSettings() ) ) == false ||
                 io.check( restoredCity.initialize( _pCatalog.get(), NileCityPlanner::kMapWidth, NileCityPlanner::kMapHeight, makeRefs() ) ) == false )
                return;
        }
        io.value( io.isWriting() ? _city : restoredCity );
        io.value( io.isWriting() ? _planner : restoredPlanner );
        io.value( io.isWriting() ? _wallet : restoredWallet );
        io.value( io.isWriting() ? _clock : restoredClock );
        // 고른 도구 · 달 수 · 진화 수 · 자동 계획 토글은 상태 칸이다(`GameModeComponent::serializeState`). 배속은 `GameSpeedControl` 이 검사하며 되돌린다.
        float32 timeScale = _speed.getTimeScale();
        uint8   bPaused   = _speed.isPaused() ? SW_TRUE : SW_FALSE;
        io.value( timeScale );
        io.value( bPaused );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        GameSpeedControl restoredSpeed;
        if ( io.check( restoredSpeed.restore( timeScale, bPaused != SW_FALSE ) ) == false )
            return; // 범위 밖 배속(깨진 세이브)은 거절한다
        _city         = std::move( restoredCity );
        _planner      = std::move( restoredPlanner );
        _wallet       = std::move( restoredWallet );
        _clock        = std::move( restoredClock );
        _speed        = restoredSpeed;
        _selectedTool = _listTool.empty() ? 0 : MathUtil::clamp( _selectedTool, 0, static_cast<int32>( _listTool.size() ) - 1 );
        _listEvent.clear();
    }

    void NileGameModeComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
            SW_LOG_INFO_ID( "NileGameMode.CityStateRestoredMonth", "[Nile] city state restored - month %#, population %#, money %#", _monthCount, _city.getPopulation(), _wallet.getBalance( _city.getCurrency() ) );
        else
            SW_LOG_WARNING_ID( "NileGameMode.SavedCityStateDoes", "[Nile] the saved city state does not match this build - founding a new city" );
    }

    // ------------------------------------------------------------------------------
    // 스폰(틱 뒤 · 게임 스레드)
    // ------------------------------------------------------------------------------
    void NileGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews )
        {
            spawnAll( manager );
            return;
        }
        flushViews( manager );
        spawnWalkers( manager, _wantedWalkerCount );
    }

    void NileGameModeComponent::flushViews( GameObjectManager& manager )
    {
        const auto destroy = [&manager]( GameObjectHandle& inoutHandle )
        { destroySpawned( manager, inoutHandle ); };
        _roadViews.flush( destroy, [this, &manager]( size_t tileIndex, uint8 )
        { return spawnRoad( manager, tileIndex ); } );
        _buildingViews.flush( destroy, [this, &manager]( size_t buildingIndex, const CityBuildingDef* pDef )
        { return spawnBuilding( manager, buildingIndex, *pDef ); } );
    }

    void NileGameModeComponent::spawnAll( GameObjectManager& manager )
    {
        preloadModels();
        spawnTerrain( manager );
        // 처음이거나 정리한 뒤다(슬롯은 `onViewsDespawned` 가 비웠다) — 지금 도시를 모두 쌓고 곧바로 맞춘다.
        _roadViews.clear();
        _buildingViews.clear();
        collectViewChanges();
        flushViews( manager );
        _wantedWalkerCount = _city.getWalkers().size();
        spawnWalkers( manager, _wantedWalkerCount );
    }

    void NileGameModeComponent::spawnTerrain( GameObjectManager& manager )
    {
        // 줄마다 같은 땅이 이어지는 구간을 상자 하나로 만든다(타일마다 스폰하면 2000개가 되며, 땅은 바뀌지 않는다). 바위는 타일마다 솟은 상자다.
        using Internal = NileGameModeComponentInternal;
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

    GameObjectHandle NileGameModeComponent::spawnRoad( GameObjectManager& manager, size_t tileIndex )
    {
        // 도로 타일은 보도 조각을 타일 크기만큼 늘려 쓴다(팔레트 머티리얼 그대로).
        const int2  cell  = _city.getTopology().toCell( static_cast<int32>( tileIndex ) );
        GameObject* pRoad = spawnPrefab( manager, _roadPrefab, "NileRoad" );
        (void)NileGameModeComponentInternal::placeMesh( pRoad, float3{ static_cast<float32>( cell._x ) + 0.5f, 0.0f, static_cast<float32>( cell._y ) + 0.5f },
                                                        float3{ _roadTileScale } );
        return pRoad != nullptr ? pRoad->getHandle() : GameObjectHandle{};
    }

    GameObjectHandle NileGameModeComponent::spawnBuilding( GameObjectManager& manager, size_t buildingIndex, const CityBuildingDef& def )
    {
        // 모델이 있는 건물은 팔레트 머티리얼, 밭 · 조각상은 색 상자다. 단계 · 사람에 따른 모델 · 높이는 뷰가 맞춘다.
        const bool  bModel    = NileBuildingComponent::hasModel( *_pBuildingViews, def );
        GameObject* pBuilding = spawnPrefab( manager, bModel ? _modelBuildingPrefab : _blockBuildingPrefab, "NileBuilding" );
        if ( pBuilding == nullptr )
            return GameObjectHandle{};
        MeshComponent* pMesh = pBuilding->getComponent<MeshComponent>();
        if ( pMesh != nullptr )
            _tintCache.apply( *pMesh, bModel ? computeModelTint( def ) : computeBlockColor( def ) );
        NileBuildingComponent* pView = pBuilding->getComponent<NileBuildingComponent>();
        if ( pView != nullptr )
            pView->assignBuilding( getOwner()->getHandle(), static_cast<int32>( buildingIndex ) );
        return pBuilding->getHandle();
    }

    void NileGameModeComponent::spawnWalkers( GameObjectManager& manager, size_t count )
    {
        // 일꾼 수만큼 미리 스폰하고 남으면 뷰가 숨긴다(오브젝트를 매 프레임 만들고 지우지 않는다). 목록 슬롯은 다른 일꾼에게 넘어갈 수 있다.
        const GameObjectHandle gameMode = getOwner()->getHandle();
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
                pView->assignWalker( gameMode, static_cast<int32>( _listWalkerObject.size() ) );
            _listWalkerObject.push_back( pWalker->getHandle() );
        }
    }

    void NileGameModeComponent::prepareWalkerLooks( Material* pMaterial )
    {
        using Internal = NileGameModeComponentInternal;
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

    void NileGameModeComponent::preloadModels()
    {
        if ( _listModelMesh.empty() == false )
            return;
        const vector<string> listModelPath = _pBuildingViews->makeAllMeshPaths();
        // 게임 스레드가 디스크를 기다리지 않게 워커가 읽는다 — 미리 쥐는 것이 목적이라 지금 쓰지 않는다(뷰의 `MeshComponent` 가 같은 로드를 받는다).
        // 읽지 못한 모델은 캐시가 경로마다 한 번 경고한다.
        for ( const string& path : listModelPath )
        {
            _listModelMesh.push_back( MeshCache::request( path ) );
        }
    }

    // ------------------------------------------------------------------------------
    // 갱신(PrePhysics, 워커)
    // ------------------------------------------------------------------------------
    void NileGameModeComponent::collectViewChanges()
    {
        // 도로 타일은 깔렸거나 철거된 타일만 처리한다. 건물 타일은 새 건물이거나 허물렸거나 다른 건물로 다시 쓰인 타일만 처리한다. 단계와 사람은 뷰가 맞춘다.
        const GridTopology& topology = _city.getTopology();
        for ( int32 tileIndex = 0; tileIndex < topology.getCellCount(); ++tileIndex )
        {
            const int2 cell = topology.toCell( tileIndex );
            _roadViews.show( static_cast<size_t>( tileIndex ), _city.findTile( cell._x, cell._y )->_bRoad != SW_FALSE ? SW_TRUE : SW_FALSE );
        }
        const vector<CityBuilding>& listBuilding = _city.getBuildings();
        for ( size_t buildingIndex = 0; buildingIndex < listBuilding.size(); ++buildingIndex )
        {
            const CityBuilding& building = listBuilding[buildingIndex];
            _buildingViews.show( buildingIndex, building._bAlive != SW_FALSE ? building._pDef : nullptr );
        }
        _wantedWalkerCount = MathUtil::max( _wantedWalkerCount, _city.getWalkers().size() );
    }

    void NileGameModeComponent::updateInput( float32 deltaTime, const InputManager& input )
    {
        // 카메라 이동과 확대는 리그(PostUpdate)가 읽는다. 여기서는 도구, 속도, 건설을 처리한다.
        (void)deltaTime;
        // 키는 입력 맵(`data/nile.input.xml` 과 공통 묶음)이 정한다. 일시정지 메뉴 같은 UI 가 먹은 입력은 보이지 않는다.
        const GameInputView inputMap( input.getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
        const int32         toolCount = static_cast<int32>( _listTool.size() );
        const bool          bNextTool = inputMap.wasActionTriggered( hashed_string( "Nile.NextTool" ) );
        const bool          bPrevTool = inputMap.wasActionTriggered( hashed_string( "Nile.PrevTool" ) );
        const bool          bRoadTool = inputMap.wasActionTriggered( hashed_string( "Nile.RoadTool" ) );
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
            SW_LOG_INFO_ID( "Nile.ToolSelected", "[Nile] tool: %# ($%#)", getToolName(), pDef != nullptr ? pDef->_cost : _pCatalog->getRoadCost() );
            getSoundQueue().queueEvent( NileGameModeComponentInternal::kEventSelect );
        }
        _speed.applyInput( inputMap.wasActionTriggered( hashed_string( CommonInputActions::kPause ) ), inputMap.wasActionTriggered( hashed_string( CommonInputActions::kFaster ) ),
                           inputMap.wasActionTriggered( hashed_string( CommonInputActions::kSlower ) ) );
        if ( inputMap.wasActionTriggered( hashed_string( "Nile.ToggleAutoPlan" ) ) )
        {
            _bAutoPlanToggle = _bAutoPlanToggle == SW_TRUE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO_ID( "NileGameMode.AutoPlanStep", "[Nile] auto plan %# (step %# of %#)", _bAutoPlanToggle == SW_TRUE ? "on" : "off", _planner.getNextStep(), _planner.getStepCount() );
        }
        if ( inputMap.wasActionTriggered( hashed_string( CommonInputActions::kStatus ) ) )
            logStatus();

        updateCursor( input );
        if ( _bCursorValid == SW_FALSE )
            return;
        // 커서 타일은 포인터 위치로 정하고, 누르는 것은 액션이 정한다. 도로 도구는 누르는 동안 끌어서 연결한다.
        const hashed_string placeAction( "Nile.Place" );
        if ( inputMap.wasActionPressed( placeAction ) || ( _selectedTool == 0 && inputMap.isActionDown( placeAction ) ) )
            placeSelected();
        if ( inputMap.wasActionTriggered( hashed_string( "Nile.Demolish" ) ) && _city.demolish( _cursorTile._x, _cursorTile._y ) )
        {
            _demolishCount.add();
            SW_LOG_INFO_ID( "Nile.Demolished", "[Nile] demolished (%#, %#)", _cursorTile._x, _cursorTile._y );
        }
    }

    void NileGameModeComponent::updateCursor( const InputManager& input )
    {
        // 카메라 디렉터가 지금 낸 포즈로 포인터 아래 땅 타일을 찾는다. 디렉터는 PostPhysics 틱 뒤에 쓰므로 여기서 읽는 것은 지난 프레임의 시점이다.
        _bCursorValid                       = SW_FALSE;
        const CameraDirectorComponent* pRig = findOn<CameraDirectorComponent>( _cameraRig );
        if ( pRig == nullptr )
            return;
        float3 point{};
        if ( pRig->findGroundPoint( input.getMousePositionNormalized(), 0.0f, point ) == false )
            return;
        _cursorTile   = int2{ static_cast<int32>( MathUtil::floor( point._x ) ), static_cast<int32>( MathUtil::floor( point._z ) ) };
        _bCursorValid = _city.findTile( _cursorTile._x, _cursorTile._y ) != nullptr ? SW_TRUE : SW_FALSE;
    }

    bool NileGameModeComponent::findCursorTile( int2& outTile ) const
    {
        if ( _bCursorValid == SW_FALSE )
            return false;
        outTile = _cursorTile;
        return true;
    }

    void NileGameModeComponent::placeSelected()
    {
        const CityBuildingDef* pDef = _listTool[static_cast<size_t>( _selectedTool )];
        if ( pDef == nullptr )
        {
            // 도로는 끌어서 깐다. 이미 도로인 타일은 조용히 넘어간다.
            const CityPlaceResult result = _city.placeRoad( _cursorTile._x, _cursorTile._y );
            if ( result == CityPlaceResult::Ok )
                _buildCount.add();
            if ( result == CityPlaceResult::NotEnoughMoney || result == CityPlaceResult::BadTerrain )
                SW_LOG_INFO_ID( "NileGameMode.CannotLayRoad", "[Nile] cannot lay road at (%#, %#): %#", _cursorTile._x, _cursorTile._y, getEnumName( result ) );
            return;
        }
        const CityPlaceResult result = _city.placeBuilding( pDef->_id, _cursorTile._x, _cursorTile._y );
        if ( result == CityPlaceResult::Ok )
        {
            _buildCount.add();
            SW_LOG_INFO_ID( "NileGameMode.BuiltLeft", "[Nile] built %# at (%#, %#) - $%# left", pDef->_name.c_str(), _cursorTile._x, _cursorTile._y, _wallet.getBalance( _city.getCurrency() ) );
            getSoundQueue().queueEvent( NileGameModeComponentInternal::kEventBuilt );
        }
        else
        {
            SW_LOG_INFO_ID( "NileGameMode.CannotBuild", "[Nile] cannot build %# at (%#, %#): %#", pDef->_name.c_str(), _cursorTile._x, _cursorTile._y, getEnumName( result ) );
            getSoundQueue().queueEvent( NileGameModeComponentInternal::kEventBlocked );
        }
    }

    void NileGameModeComponent::pollEvents()
    {
        _listEvent.clear(); // pollEvents 는 뒤에 붙인다
        _city.pollEvents( _cityEventLease, _listEvent );
        bool bHouseEvolved = false;
        for ( const CityEvent& event : _listEvent )
        {
            switch ( event._kind )
            {
                case CityEvent::Kind::MonthEnded:
                {
                    ++_monthCount;
                    SW_LOG_INFO_ID( "NileGameMode.MonthPopMoneyNet", "[Nile] month %# pop %# money %# (net %#, workers %#/%#, houses up %#, avg level %#, culture %#%%)", _monthCount, _city.getPopulation(),
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
                    SW_LOG_INFO_ID( "NileGameMode.YearNileFloodedFarm", "[Nile] year %# - the Nile flooded, farm fertility %#%%", _clock.getYear(), event._value );
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
            getSoundQueue().queueEvent( NileGameModeComponentInternal::kEventEvolved );
    }

    void NileGameModeComponent::logStatus() const
    {
        SW_LOG_INFO_ID( "NileGameMode.YearMonthPopMoney", "[Nile] year %# month %# · pop %# · money $%# · workers %#/%# · avg house level %# · culture %#%% · fertility %#%% · tool %# · plan %#/%#",
                        _clock.getYear(), _clock.getSeasonIndex() + 1, _city.getPopulation(), _wallet.getBalance( _city.getCurrency() ), _city.getEmployed(), _city.getWorkforce(),
                        _city.computeAverageHouseLevel(), static_cast<int32>( _city.computeCultureCoverage() * 100.0f ), static_cast<int32>( _city.getFloodFertility() * 100.0f ),
                        getToolName(), _planner.getNextStep(), _planner.getStepCount() );
    }

    const utf8* NileGameModeComponent::getToolName() const
    {
        const CityBuildingDef* pDef = _listTool.empty() ? nullptr : _listTool[static_cast<size_t>( _selectedTool )];
        return pDef != nullptr ? pDef->_name.c_str() : "Road";
    }

    bool NileGameModeComponent::isAutoPlanOn() const
    {
        return isAutoPlayOn() || _bAutoPlanToggle == SW_TRUE;
    }

} // namespace sw
