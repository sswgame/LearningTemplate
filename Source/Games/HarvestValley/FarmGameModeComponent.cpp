#include "pch.h"

#include "Games/HarvestValley/FarmGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Grid/GridTopology.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Runtime/World/Environment/WorldClock.h"

#include "Games/HarvestValley/FarmAutoFarmerAIComponent.h"
#include "Games/HarvestValley/FarmCropComponent.h"
#include "Games/HarvestValley/FarmSoilComponent.h"

namespace sw
{
    namespace
    {
        struct FarmGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "FARM" );
            static constexpr uint32 kStateVersion = 4; ///< 4: 비 · 날씨 난수를 판 상태 날씨로 옮겼다(이 칸에서 빠졌다)

            /** @brief 비를 정하는 날씨 값 이름입니다(날씨 카탈로그 `<Values wetness>` — 표면 젖음과 같은 값). 이 값이 반 이상이면 비다. */
            static constexpr const utf8* kWetnessValue     = "wetness";
            static constexpr float32     kRainingWetness   = 0.5f;
            static constexpr float32     kGameSecondsInDay = 86400.0f; ///< 판 상태 날씨의 시간 단위(하루 = 86400 게임 초, `SessionStateComponent::update`)

            /** @brief 판 상태의 지금 날씨가 비인지입니다. 날씨가 없는 판이면 false 다(시작이 카탈로그를 요구하므로 없을 수 없다). */
            static bool isRaining( const SessionStateComponent& state ) { return state.getWeather().computeValue( hashed_string( kWetnessValue ) ) >= kRainingWetness; }

            /** @brief 폰 버튼 이름입니다. `FarmButton` 순서이며 입력 맵 묶음의 액션 이름과 같습니다. */
            static constexpr const utf8* kArrButtonName[] = { "Farm.Tool1", "Farm.Tool2", "Farm.Tool3", "Farm.Tool4", "Farm.SeedPrev", "Farm.SeedNext",
                                                              "Farm.Use", "Farm.Ship", "Farm.Buy", "Farm.Sleep", CommonInputActions::kStatus };
            static_assert( sizeof( kArrButtonName ) / sizeof( kArrButtonName[0] ) == static_cast<size_t>( FarmButton::Count ), "one name per farm button" );

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 harvestvalley.audioevents.xml 이 정한다.
            static constexpr const utf8* kEventSelect  = "ToolSelect";
            static constexpr const utf8* kEventPlant   = "Plant";
            static constexpr const utf8* kEventHarvest = "Harvest";
            static constexpr const utf8* kEventShip    = "Ship";

            /** @brief 공유 시계의 계절 이름입니다(시계 설정과 작물 카탈로그가 함께 씁니다). */
            static vector<hashed_string> makeSeasons() { return { hashed_string( "Spring" ), hashed_string( "Summer" ), hashed_string( "Fall" ), hashed_string( "Winter" ) }; }
        };

        [[nodiscard]] bool readFarmerX( const FarmGameModeComponent& gameMode, float64& outValue )
        {
            outValue = static_cast<float64>( gameMode.getPlayerPosition()._x );
            return true;
        }

        [[nodiscard]] bool readTilledCount( const FarmGameModeComponent& gameMode, float64& outValue )
        {
            int32 tilledCount = 0;
            for ( int32 y = 0; y < FarmGameModeComponent::kFieldHeight; ++y )
            {
                for ( int32 x = 0; x < FarmGameModeComponent::kFieldWidth; ++x )
                {
                    const FarmTile* pTile = gameMode.getField().findTile( x, y );
                    tilledCount += pTile != nullptr && pTile->_bTilled != SW_FALSE ? 1 : 0;
                }
            }
            outValue = tilledCount;
            return true;
        }

        [[nodiscard]] bool readCropCount( const FarmGameModeComponent& gameMode, float64& outValue )
        {
            outValue = gameMode.getField().getCropCount();
            return true;
        }

    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( farmFarmerX, "Farm.FarmerX", "World X of the farmer", FarmGameModeComponent, &readFarmerX );
    SW_AUTOMATION_GETTER_WATCH( farmFarmerTool, "Farm.FarmerTool", "Tool in the farmer's hand (0 hoe, 1 watering can, 2 seeds, 3 hand)", FarmGameModeComponent, &FarmGameModeComponent::getTool );
    SW_AUTOMATION_COMPONENT_WATCH( farmTilledCount, "Farm.TilledCount", "Tilled field tiles", FarmGameModeComponent, &readTilledCount );
    SW_AUTOMATION_COMPONENT_WATCH( farmCropCount, "Farm.CropCount", "Field tiles with a crop", FarmGameModeComponent, &readCropCount );
    SW_AUTOMATION_GETTER_WATCH( farmRaining, "Farm.Raining", "1 while the shared session weather is rain (its wetness is half or more)", FarmGameModeComponent, &FarmGameModeComponent::isRaining );
    SW_AUTOMATION_GETTER_WATCH( farmFarmerControllerKind, "Farm.FarmerControllerKind", "0 = the player controller holds the farmer, 1 = the auto farmer AI", FarmGameModeComponent, &FarmGameModeComponent::isFarmerDrivenByAI );
} // namespace sw

namespace sw
{
    FarmGameModeComponent::FarmGameModeComponent()
        : _cropDataPath{}
        , _soilPrefab{}
        , _cropPrefab{}
        , _grassSoilColor{ 0.40f, 0.62f, 0.28f, 1.0f }
        , _tilledSoilColor{ 0.58f, 0.40f, 0.24f, 1.0f }
        , _wateredSoilColor{ 0.33f, 0.22f, 0.13f, 1.0f }
        , _witheredCropColor{ 0.55f, 0.40f, 0.22f, 1.0f }
        , _cameraRig{}
        , _shippingBin{}
        , _shop{}
        , _farmer{}
        , _autoFarmerPrefab{}
        , _playerStart{ 6.0f, 0.0f, -1.2f }
        , _walkSpeed{ 4.0f }
        , _reach{ 0.9f }
        , _nearDistance{ 1.7f }
        , _weatherPath{}
        , _cameraFollow{ 0.4f }
        , _maxStamina{ 100 }
        , _startingGold{ 500 }
        , _secondsPerDay{ 144.0f }
        , _daysPerSeason{ 28 }
        , _wakeHour{ 6.0f }
        , _passOutHour{ 2.0f }
        , _hoeStaminaCost{ 4 }
        , _wateringStaminaCost{ 2 }
        , _handStaminaCost{ 1 }
        , _startingSeedCount{ 10 }
        , _itemStackSize{ 99 }
        , _listCropModel{}
        , _listDefaultGrowingModel{}
        , _pCropCatalog{}
        , _pWeatherCatalog{}
        , _itemCatalog{}
        , _field{}
        , _shipment{}
        , _listSeed{}
        , _tintCache{}
        , _listCropLook{}
        , _listCropMesh{}
        , _arrSoilLook{}
        , _plainCropLook{}
        , _witheredCropLook{}
        , _playerPosition{ _playerStart }
        , _facing{ 0.0f, 0.0f, 1.0f }
        , _shippingBinPosition{}
        , _shopPosition{}
        , _autoFarmerObject{}
        , _stamina{ _maxStamina }
        , _selectedSeedIndex{ 0 }
        , _lastLoggedHour{ -1 }
        , _dayStarted{ 0 }
        , _hourOfDay{ _wakeHour }
        , _tool{ FarmTool::Hoe }
        , _lastUsedTool{ FarmTool::Hoe }
        , _toolUseCount{ 0 }
        , _bRaining{ SW_FALSE }
        , _bPossessionDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    FarmGameModeComponent::~FarmGameModeComponent() = default;

    void FarmGameModeComponent::onGameStarted()
    {
        updateCameraFocus();
        SW_LOG_INFO_ID( "FarmGameMode.FarmReadyWASDMove", "[Farm] farm is ready - WASD move, Space use tool, 1-4 tool (hoe/can/seeds/hand), Q/E seed, B buy, F ship, Z sleep" );
        logStatus( true );
    }

    void FarmGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        // 농부는 폰의 의도대로 움직인다. 누가 냈는지(플레이어 또는 자동 농부)는 모른다. 빙의는 틱이 끝난 뒤 플러시가 스위치에 맞춘다.
        const PawnComponent* pFarmerPawn = findFarmerPawn();
        if ( pFarmerPawn != nullptr )
        {
            applyFarmerIntent( deltaTime, *pFarmerPawn );
            _bPossessionDirty = pFarmerPawn->isPossessed() == false || isAutoPlayOn() != isFarmerDrivenByAI() ? SW_TRUE : SW_FALSE;
        }

        // 시간은 공유 시계를 공유 상태가 흘린다. 다음 날 2시가 되면 그 위치에서 쓰러져 아침이 된다(체력 반).
        const SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState != nullptr )
        {
            const WorldClock& clock = pState->getClock();
            _hourOfDay              = clock.getHour();
            _bRaining               = FarmGameModeComponentInternal::isRaining( *pState ) ? SW_TRUE : SW_FALSE; // 뷰가 읽는 사본 — 날씨는 판 상태가 흘린다
            const bool bPassedOut   = _dayStarted < clock.getDay() && _passOutHour <= clock.getHour();
            if ( bPassedOut )
            {
                SW_LOG_INFO_ID( "FarmGameMode.AMFarmerPassedOut", "[Farm] it is 2 AM - the farmer passed out" );
                endDay( true );
            }
        }
        updateCameraFocus();
        logStatus( false );
    }

    void FarmGameModeComponent::foldStateHash( StateHashBuilder& inoutBuilder ) const
    {
        GameModeComponent::foldStateHash( inoutBuilder ); // 날씨 난수는 판 상태(`SessionStateComponent` 의 날씨)가 쥐고 싣는다
    }

    void FarmGameModeComponent::serializeState( StateIO& io )
    {
        io.header( FarmGameModeComponentInternal::kStateTag, FarmGameModeComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        FarmField       restoredField;
        FarmShippingBin restoredShipment;
        if ( io.isReading() )
        {
            restoredField.initialize( kFieldWidth, kFieldHeight, _pCropCatalog.get() );
        }
        FarmField&       field    = io.isWriting() ? _field : restoredField;
        FarmShippingBin& shipment = io.isWriting() ? _shipment : restoredShipment;
        io.section( FarmField::kStateTag, FarmField::kStateVersion, [&field]( StateIO& bodyIO )
        { bodyIO.value( field ); } );
        io.section( FarmShippingBin::kStateTag, FarmShippingBin::kStateVersion, [&shipment]( StateIO& bodyIO )
        { bodyIO.value( shipment ); } );
        // 날씨(난수 · 지금 날씨)는 판 상태가 싣는다 — 판 4 부터 이 칸에 비 · 난수가 없다.
        float3   playerPosition    = _playerPosition;
        float3   facing            = _facing;
        int32    stamina           = _stamina;
        int32    selectedSeedIndex = _selectedSeedIndex;
        FarmTool tool              = _tool;
        int32    dayStarted        = _dayStarted;
        io.value( playerPosition );
        io.value( facing );
        io.value( stamina );
        io.value( selectedSeedIndex );
        io.enumValue( tool, FarmTool::Hand );
        io.value( dayStarted );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _field             = std::move( restoredField );
        _shipment          = std::move( restoredShipment );
        _dayStarted        = dayStarted;
        _playerPosition    = playerPosition;
        _facing            = facing;
        _stamina           = MathUtil::clamp( stamina, 0, _maxStamina );
        _selectedSeedIndex = _listSeed.empty() ? 0 : MathUtil::clamp( selectedSeedIndex, 0, static_cast<int32>( _listSeed.size() ) - 1 );
        _tool              = tool;
    }

    void FarmGameModeComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
            SW_LOG_INFO_ID( "FarmGameMode.FarmStateRestoredStarted", "[Farm] farm state restored - started on day %#", _dayStarted );
        else
            SW_LOG_WARNING_ID( "FarmGameMode.SavedFarmStateDoes", "[Farm] the saved farm state does not match this build - starting a new farm" );
        _lastLoggedHour = -1;
    }

    bool FarmGameModeComponent::findTargetTile( int32& outX, int32& outY ) const
    {
        const float3 target = _playerPosition + _facing * _reach;
        outX                = static_cast<int32>( MathUtil::floor( target._x ) );
        outY                = static_cast<int32>( MathUtil::floor( target._z ) );
        return _field.findTile( outX, outY ) != nullptr;
    }

    const shared_ptr<MaterialInstance>& FarmGameModeComponent::getSoilLook( int32 soilState ) const
    {
        return _arrSoilLook[MathUtil::clamp( soilState, 0, 2 )];
    }

    const shared_ptr<MaterialInstance>& FarmGameModeComponent::findCropLook( const hashed_string& cropID, bool bWithered, bool bCropColored ) const
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

    float3 FarmGameModeComponent::computeTileCenter( int32 x, int32 y )
    {
        return float3{ static_cast<float32>( x ) + 0.5f, 0.0f, static_cast<float32>( y ) + 0.5f };
    }

    const string* FarmGameModeComponent::findReadyModel( const hashed_string& cropID ) const
    {
        for ( const FarmCropModel& model : _listCropModel )
        {
            if ( model._cropID == cropID )
                return model._readyModel.empty() ? nullptr : &model._readyModel.path();
        }
        return nullptr;
    }

    const string& FarmGameModeComponent::findGrowingModel( const hashed_string& cropID, int32 stage ) const
    {
        const vector<AssetRef<MeshAssetFormat>>* pList = &_listDefaultGrowingModel;
        for ( const FarmCropModel& model : _listCropModel )
        {
            if ( model._cropID == cropID && model._listGrowingModel.empty() == false )
                pList = &model._listGrowingModel;
        }
        // 첫 모델이 0 · 1 단계, 그다음부터 한 단계에 하나, 모자라면 마지막을 쓴다.
        const int32 index = MathUtil::clamp( stage - 1, 0, static_cast<int32>( pList->size() ) - 1 );
        return ( *pList )[static_cast<size_t>( index )].path();
    }

    // ------------------------------------------------------------------------------
    // 데이터
    // ------------------------------------------------------------------------------
    bool FarmGameModeComponent::startGame()
    {
        // 작물이 적을 수 있는 계절은 작물 파일의 루트(`seasons`)가 정한다 — 공유 캐시가 읽으므로 읽기 전에 알려 줄 자리가 없다.
        if ( acquireRequired( _weatherPath, _pWeatherCatalog ) == false )
            return false;
        if ( acquireRequired( _cropDataPath, _pCropCatalog ) == false )
            return false;
        if ( _pCropCatalog->getCrops().empty() )
        {
            SW_LOG_ERROR_ID( "FarmGameMode.ReadButListsNo", "[Farm] %# was read but lists no crop - the farm cannot start", _cropDataPath.c_str() );
            return false;
        }
        // 출하함과 가게는 씬에 놓인 오브젝트다(필수 참조). 에디터에서 옮기면 농부가 서는 위치가 따라온다.
        if ( tryGetObjectPosition( _shippingBin, _shippingBinPosition ) == false || tryGetObjectPosition( _shop, _shopPosition ) == false )
        {
            SW_LOG_ERROR_ID( "FarmGameMode.DirectorShippingBinShop", "[Farm] the game mode's Shipping Bin and Shop must point at objects in the scene - the farm cannot start" );
            return false;
        }
        // 작물 모델 표는 씬 데이터다. 기본 자라는 모델이 없으면 뷰가 고를 모델이 없다 — 시작하지 않는다.
        if ( _listDefaultGrowingModel.empty() )
        {
            SW_LOG_ERROR_ID( "FarmGameMode.DirectorDefaultGrowingModels", "[Farm] the game mode's Default Growing Models is empty - the farm cannot start" );
            return false;
        }
        for ( const FarmCropModel& model : _listCropModel )
        {
            if ( _pCropCatalog->findCrop( model._cropID ) == nullptr )
            {
                SW_LOG_ERROR_ID( "FarmGameMode.DirectorCropModelsNames", "[Farm] the game mode's Crop Models names crop '%#', which %# does not have - the farm cannot start", model._cropID.c_str(), _cropDataPath.c_str() );
                return false;
            }
        }
        _itemCatalog = ItemCatalog{};
        _pCropCatalog->fillItemCatalog( _itemCatalog, _itemStackSize );
        SessionStateSettings settings;
        settings._clock._listSeason       = FarmGameModeComponentInternal::makeSeasons();
        settings._clock._secondsPerDay    = _secondsPerDay;
        settings._clock._startHour        = _wakeHour;
        settings._clock._daysPerSeason    = _daysPerSeason;
        settings._pItemCatalog            = &_itemCatalog;
        settings._inventorySlotCount      = kBagSlotCount;
        settings._pWeatherCatalog         = _pWeatherCatalog.get(); // 비는 판 상태 날씨다(계절 가중치 · 하루 길이 — 카탈로그 데이터)
        SessionStateInitResult initResult = SessionStateInitResult::Refused;
        SessionStateComponent* pState     = SessionStateComponent::openOnOwner( *this, settings, initResult );
        if ( pState == nullptr )
            return false; // 범위 밖(하루 길이 · 계절 날 수 등)은 판 상태가 오류를 남겼다
        const bool bFresh = initResult == SessionStateInitResult::Fresh;
        _field.initialize( kFieldWidth, kFieldHeight, _pCropCatalog.get() );
        _shipment = FarmShippingBin{};
        _listSeed.clear();
        for ( const CropDef& crop : _pCropCatalog->getCrops() )
        {
            _listSeed.push_back( crop._seedItem );
        }
        // 시작 돈과 씨앗은 새 판에만 준다. 복원한 판에 덧쌓이지 않게 하기 위해서다.
        if ( bFresh )
        {
            pState->getWallet().add( _shipment.getCurrency(), _startingGold );
            (void)pState->getInventory().addItem( _pCropCatalog->getCrops().front()._seedItem, _startingSeedCount );
        }
        _dayStarted     = pState->getClock().getDay();
        _hourOfDay      = pState->getClock().getHour();
        _playerPosition = _playerStart;
        _facing         = float3{ 0.0f, 0.0f, 1.0f };
        _stamina        = _maxStamina;
        _tool           = FarmTool::Hoe;
        return true;
    }

    // ------------------------------------------------------------------------------
    // 스폰(틱이 끝난 뒤, 게임 스레드)
    // ------------------------------------------------------------------------------
    void FarmGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews )
            spawnField( manager );
        syncFarmerPossession( manager );
    }

    void FarmGameModeComponent::onViewsDespawned()
    {
        _autoFarmerObject = GameObjectHandle{};
        _bPossessionDirty = SW_FALSE;
    }

    PawnComponent* FarmGameModeComponent::findFarmerPawn() const
    {
        return findOn<PawnComponent>( _farmer );
    }

    bool FarmGameModeComponent::isFarmerDrivenByAI() const
    {
        const GameObjectManager* pManager = getObjectManager();
        const PawnComponent*     pPawn    = findFarmerPawn();
        return pManager != nullptr && pPawn != nullptr && ControlSystem::isPawnControlledBy( *pManager, *pPawn, _autoFarmerObject );
    }

    const utf8* FarmGameModeComponent::getButtonName( FarmButton button )
    {
        const size_t index = static_cast<size_t>( button );
        return index < static_cast<size_t>( FarmButton::Count ) ? FarmGameModeComponentInternal::kArrButtonName[index] : "";
    }

    void FarmGameModeComponent::syncFarmerPossession( GameObjectManager& manager )
    {
        _bPossessionDirty    = SW_FALSE;
        PawnComponent* pPawn = findFarmerPawn();
        if ( pPawn == nullptr || isStarted() == false )
            return;
        if ( isAutoPlayOn() )
        {
            GameObject*                pObject = manager.resolveGameObject( _autoFarmerObject );
            FarmAutoFarmerAIComponent* pAI     = pObject != nullptr ? pObject->getComponent<FarmAutoFarmerAIComponent>() : nullptr;
            if ( pAI == nullptr )
            {
                pObject           = spawnPrefab( manager, _autoFarmerPrefab, "FarmAutoFarmer" );
                pAI               = pObject != nullptr ? pObject->getComponent<FarmAutoFarmerAIComponent>() : nullptr;
                _autoFarmerObject = pObject != nullptr ? pObject->getHandle() : GameObjectHandle{};
            }
            if ( pAI == nullptr )
            {
                SW_LOG_WARNING_ID( "FarmGameMode.AutoFarmerPrefabNo", "[Farm] auto farmer prefab '%#' has no FarmAutoFarmerAIComponent - auto play cannot take the farmer", _autoFarmerPrefab.c_str() );
                return;
            }
            pAI->assignGameMode( getOwner()->getHandle() );
            if ( isFarmerDrivenByAI() == false )
                pAI->possess( *pPawn );
            return;
        }
        // 자동 플레이를 끄면 플레이어 0 의 조종자가 되찾는다(씬에 없으면 만든다).
        (void)ControlSystem::returnPawnToPlayer( manager, *pPawn, _autoFarmerObject );
    }

    void FarmGameModeComponent::spawnField( GameObjectManager& manager )
    {
        // 타일마다 흙과 작물을 하나씩 둔다. 각 뷰가 밭의 같은 타일을 따라 모습을 맞춘다(PostUpdate).
        const GameObjectHandle gameMode = getOwner()->getHandle();
        for ( int32 y = 0; y < kFieldHeight; ++y )
        {
            for ( int32 x = 0; x < kFieldWidth; ++x )
            {
                const int32  tileIndex = GridTopology( kFieldWidth, kFieldHeight ).toIndex( x, y );
                const float3 center    = computeTileCenter( x, y );
                GameObject*  pSoil     = spawnPrefab( manager, _soilPrefab, "FarmSoil" );
                GameObject*  pCrop     = spawnPrefab( manager, _cropPrefab, "FarmCrop" );
                if ( pSoil == nullptr || pCrop == nullptr )
                    continue;
                MeshComponent* pSoilMesh           = pSoil->getComponent<MeshComponent>();
                MeshComponent* pCropMesh           = pCrop->getComponent<MeshComponent>();
                const bool     bShouldPrepareLooks = _arrSoilLook[0] == nullptr && pSoilMesh != nullptr && pCropMesh != nullptr;
                if ( bShouldPrepareLooks )
                    prepareLooks( pSoilMesh->getMaterial(), pCropMesh->getMaterial() );
                if ( pSoilMesh != nullptr )
                    pSoilMesh->setLocalPosition( center + float3{ 0.0f, pSoilMesh->getLocalScale()._y * 0.5f, 0.0f } );
                if ( pCropMesh != nullptr )
                    pCropMesh->setLocalPosition( center );
                FarmSoilComponent* pSoilView = pSoil->getComponent<FarmSoilComponent>();
                if ( pSoilView != nullptr )
                    pSoilView->assignTile( gameMode, tileIndex );
                FarmCropComponent* pCropView = pCrop->getComponent<FarmCropComponent>();
                if ( pCropView != nullptr )
                    pCropView->assignTile( gameMode, tileIndex );
            }
        }
    }

    void FarmGameModeComponent::prepareLooks( Material* pSoilMaterial, Material* pCropMaterial )
    {
        const float4 arrSoilColor[3] = { _grassSoilColor, _tilledSoilColor, _wateredSoilColor };
        for ( int32 soilState = 0; soilState < 3; ++soilState )
        {
            _arrSoilLook[soilState] = _tintCache.acquire( pSoilMaterial, arrSoilColor[soilState] );
        }
        _plainCropLook    = _tintCache.acquire( pCropMaterial, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
        _witheredCropLook = _tintCache.acquire( pCropMaterial, _witheredCropColor );
        _listCropLook.clear();
        for ( const CropDef& crop : _pCropCatalog->getCrops() )
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
            _listCropMesh.push_back( MeshCache::request( path ) );
        }
    }

    // ------------------------------------------------------------------------------
    // 농부 폰의 의도(PrePhysics, 워커). 의도는 조종 시스템이 틱 전에 채웠다.
    // ------------------------------------------------------------------------------
    void FarmGameModeComponent::applyFarmerIntent( float32 deltaTime, const PawnComponent& pawn )
    {
        using Internal              = FarmGameModeComponentInternal;
        const ControlIntent& intent = pawn.getIntent();
        const float3         move   = intent.computeWorldMove();
        movePlayer( float3{ move._x, 0.0f, move._z }, deltaTime );

        // 버튼은 도구를 먼저 바꾸고 쓴다(같은 틱에 둘 다 누를 수 있다).
        optional<int32> arrButtonIndex[kButtonCount];
        for ( int32 button = 0; button < kButtonCount; ++button )
        {
            arrButtonIndex[button] = pawn.findButton( hashed_string( Internal::kArrButtonName[button] ) );
        }
        const auto wasTriggered = [&intent, &arrButtonIndex]( FarmButton button )
        { return intent.wasTriggered( arrButtonIndex[static_cast<int32>( button )] ); };

        constexpr FarmButton kArrToolButton[] = { FarmButton::Tool1, FarmButton::Tool2, FarmButton::Tool3, FarmButton::Tool4 };
        constexpr FarmTool   kArrTool[]       = { FarmTool::Hoe, FarmTool::WateringCan, FarmTool::Seeds, FarmTool::Hand };
        for ( int32 toolIndex = 0; toolIndex < 4; ++toolIndex )
        {
            if ( wasTriggered( kArrToolButton[toolIndex] ) )
            {
                _tool = kArrTool[toolIndex];
                if ( isAutoPlayOn() == false )
                {
                    SW_LOG_INFO_ID( "FarmGameMode.Tool", "[Farm] tool: %#", getEnumName( _tool ) );
                    getSoundQueue().queueEvent( Internal::kEventSelect );
                }
            }
        }
        if ( wasTriggered( FarmButton::SeedPrev ) )
            selectSeed( -1 );
        if ( wasTriggered( FarmButton::SeedNext ) )
            selectSeed( 1 );
        if ( wasTriggered( FarmButton::Use ) )
            useTool();
        if ( wasTriggered( FarmButton::Ship ) )
            (void)shipAllProduce();
        if ( wasTriggered( FarmButton::Buy ) )
            (void)buySelectedSeed();
        if ( wasTriggered( FarmButton::Sleep ) )
            endDay( false );
        if ( wasTriggered( FarmButton::Status ) )
            logStatus( true );
    }

    void FarmGameModeComponent::movePlayer( const float3& direction, float32 deltaTime )
    {
        // 길이는 걷는 속도의 비율이다(스틱을 반만 기울이면 반 속도). 1 을 넘으면(대각선 키) 1 로 자른다.
        const float32 length = direction.getLength();
        if ( length < 1.0e-4f )
            return;
        const float3 step  = direction * ( _walkSpeed * deltaTime * MathUtil::min( length, 1.0f ) / length );
        _playerPosition._x = MathUtil::clamp( _playerPosition._x + step._x, -5.0f, static_cast<float32>( kFieldWidth ) + 4.0f );
        _playerPosition._z = MathUtil::clamp( _playerPosition._z + step._z, -5.0f, static_cast<float32>( kFieldHeight ) + 4.0f );
        // 네 방향으로 맞춘다. 대각선이면 더 큰 축을 쓴다.
        if ( MathUtil::abs( direction._x ) > MathUtil::abs( direction._z ) )
            _facing = float3{ direction._x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f };
        else
            _facing = float3{ 0.0f, 0.0f, direction._z > 0.0f ? 1.0f : -1.0f };
    }

    // ------------------------------------------------------------------------------
    // 행동
    // ------------------------------------------------------------------------------
    void FarmGameModeComponent::useTool()
    {
        using Internal = FarmGameModeComponentInternal;
        int32 x        = 0;
        int32 y        = 0;
        if ( findTargetTile( x, y ) == false )
            return;
        const int32 cost = getToolStaminaCost( _tool );
        if ( _stamina < cost )
        {
            SW_LOG_INFO_ID( "FarmGameMode.TooTiredPressZ", "[Farm] too tired - press Z to sleep" );
            return;
        }

        SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        FarmActionResult       result = FarmActionResult::Done;
        switch ( _tool )
        {
            case FarmTool::Hoe:
            {
                result = _field.till( x, y );
                break;
            }
            case FarmTool::WateringCan:
            {
                result = _field.water( x, y );
                break;
            }
            case FarmTool::Seeds:
            {
                const hashed_string& seed = getSelectedSeed();
                if ( pState == nullptr || pState->getInventory().getItemCount( seed ) <= 0 )
                {
                    SW_LOG_INFO_ID( "FarmGameMode.NoLeftBuyMore", "[Farm] no %# left - buy more at the shop (B)", seed.c_str() );
                    return;
                }
                result = _field.plant( x, y, seed, pState->getClock().getSeasonName() );
                if ( result == FarmActionResult::Done && pState->getInventory().removeItem( seed, 1 ) == false )
                    SW_LOG_WARNING_ID( "FarmGameMode.PlantedWithoutSeedBag", "[Farm] planted without a seed in the bag" );
                if ( result == FarmActionResult::Done )
                    getSoundQueue().queueEvent( Internal::kEventPlant );
                break;
            }
            case FarmTool::Hand:
            {
                hashed_string produce;
                int32         count = 0;
                result              = _field.harvest( x, y, produce, count );
                if ( result == FarmActionResult::Done && count > 0 )
                {
                    // 가방이 차면 덜 들어간다(남은 것은 밭에서 사라진다). 실제로 넣은 수를 적는다.
                    const int32 added = pState != nullptr ? pState->getInventory().addItem( produce, count ) : 0;
                    SW_LOG_INFO_ID( "FarmGameMode.HarvestedXBag", "[Farm] harvested %# x%# (%# into the bag)", produce.c_str(), count, added );
                    getSoundQueue().queueEvent( Internal::kEventHarvest );
                }
                break;
            }
        }
        if ( result == FarmActionResult::Done )
        {
            _stamina -= cost;
            _lastUsedTool = _tool;
            ++_toolUseCount;
        }
        else if ( isAutoPlayOn() == false )
            SW_LOG_INFO_ID( "FarmGameMode.ToolUsed", "[Farm] %# at (%#, %#): %#", getEnumName( _tool ), x, y, getEnumName( result ) );
    }

    int32 FarmGameModeComponent::shipAllProduce()
    {
        if ( isNearShippingBin() == false )
        {
            SW_LOG_INFO_ID( "FarmGameMode.StandNextShippingBin", "[Farm] stand next to the shipping bin to ship" );
            return 0;
        }
        SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return 0;
        Inventory& bag     = pState->getInventory();
        int32      shipped = 0;
        for ( const CropDef& crop : _pCropCatalog->getCrops() )
        {
            const int32 count = bag.getItemCount( crop._produceItem );
            if ( count > 0 && _shipment.shipItem( bag, crop._produceItem, count ) )
                shipped += count;
        }
        if ( shipped > 0 )
        {
            SW_LOG_INFO_ID( "FarmGameMode.ShippedItemsPaidTonight", "[Farm] shipped %# items - paid tonight", shipped );
            getSoundQueue().queueEvent( FarmGameModeComponentInternal::kEventShip );
        }
        return shipped;
    }

    bool FarmGameModeComponent::buySelectedSeed()
    {
        if ( isNearShop() == false )
        {
            SW_LOG_INFO_ID( "FarmGameMode.StandNextShopBuy", "[Farm] stand next to the shop to buy seeds" );
            return false;
        }
        const CropDef* pCrop = _pCropCatalog->findCropBySeed( getSelectedSeed() );
        if ( pCrop == nullptr )
            return false;
        SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return false;
        Inventory& bag    = pState->getInventory();
        Wallet&    wallet = pState->getWallet();
        if ( bag.hasRoomFor( pCrop->_seedItem, 1 ) == false )
        {
            SW_LOG_INFO_ID( "FarmGameMode.BagFullNoRoom", "[Farm] the bag is full - no room for %#", pCrop->_seedItem.c_str() );
            return false;
        }
        if ( wallet.trySpend( _shipment.getCurrency(), pCrop->_seedPrice ) == false )
        {
            SW_LOG_INFO_ID( "FarmGameMode.NotEnoughGoldG", "[Farm] not enough gold for %# (%#G)", pCrop->_seedItem.c_str(), pCrop->_seedPrice );
            return false;
        }
        (void)bag.addItem( pCrop->_seedItem, 1 );
        SW_LOG_INFO_ID( "FarmGameMode.BoughtGGLeft", "[Farm] bought %# (%#G) - %#G left", pCrop->_seedItem.c_str(), pCrop->_seedPrice, wallet.getBalance( _shipment.getCurrency() ) );
        return true;
    }

    void FarmGameModeComponent::endDay( bool bPassedOut )
    {
        SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return;
        WorldClock&                  clock        = pState->getClock();
        [[maybe_unused]] const int64 earned       = _shipment.settleShipping( *_pCropCatalog, pState->getWallet() );
        const hashed_string          seasonBefore = clock.getSeasonName();
        const float32                hoursBefore  = static_cast<float32>( clock.getDay() ) * 24.0f + clock.getHour();
        clock.advanceToHour( _wakeHour ); // 수면. 다음 날 아침이 된다.
        _dayStarted                               = clock.getDay();
        _hourOfDay                                = clock.getHour();
        [[maybe_unused]] const bool bSeasonChange = clock.getSeasonName() != seasonBefore;
        // 잠은 시계를 건너뛴다 — 판 상태 날씨도 같은 시간만큼 흘린다(틱으로는 흐르지 않은 몫). 오늘 비는 그 날씨가 정한다(계절 가중치는 카탈로그 데이터).
        const float32 sleptHours = static_cast<float32>( clock.getDay() ) * 24.0f + clock.getHour() - hoursBefore;
        (void)pState->getWeather().update( sleptHours * FarmGameModeComponentInternal::kGameSecondsInDay / 24.0f, clock.getSeasonName() );
        const bool bRaining = FarmGameModeComponentInternal::isRaining( *pState );
        _bRaining           = bRaining ? SW_TRUE : SW_FALSE;
        _field.advanceDay( clock.getSeasonName(), bRaining );
        _stamina        = bPassedOut ? _maxStamina / 2 : _maxStamina;
        _playerPosition = _playerStart;
        _facing         = float3{ 0.0f, 0.0f, 1.0f };
        SW_LOG_INFO_ID( "FarmGameMode.GoodMorningYearShipped", "[Farm] good morning - %# %#, year %# · shipped for %#G · gold %#G · %#%#", clock.getSeasonName().c_str(), clock.getDayOfSeason() + 1,
                        clock.getYear(), earned, pState->getWallet().getBalance( _shipment.getCurrency() ), bRaining ? "rain" : "sunny",
                        bSeasonChange ? " · a new season - out-of-season crops withered" : "" );
        _lastLoggedHour = -1;
    }

    void FarmGameModeComponent::selectSeed( int32 offset )
    {
        if ( _listSeed.empty() )
            return;
        const int32 count  = static_cast<int32>( _listSeed.size() );
        _selectedSeedIndex = ( ( _selectedSeedIndex + offset ) % count + count ) % count;
        if ( isAutoPlayOn() )
            return; // 자동 농부는 씨앗을 하나씩 넘겨 가며 고른다. 그 줄은 로그에 남기지 않는다.
        [[maybe_unused]] const CropDef*               pCrop  = _pCropCatalog->findCropBySeed( getSelectedSeed() );
        [[maybe_unused]] const SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        SW_LOG_INFO_ID( "FarmGameMode.SeedG", "[Farm] seed: %# (have %#, %#G)%#", getSelectedSeed().c_str(), pState != nullptr ? pState->getInventory().getItemCount( getSelectedSeed() ) : 0,
                        pCrop != nullptr ? pCrop->_seedPrice : 0, pCrop != nullptr && pState != nullptr && pCrop->canGrowIn( pState->getClock().getSeasonName() ) ? "" : " - not this season" );
    }

    void FarmGameModeComponent::updateCameraFocus()
    {
        // 비스듬히 내려다보는 직교 시점으로, 밭 중심과 농부 사이를 본다. 카메라 디렉터는 PostPhysics 틱 뒤에 이 초점을 읽고, 카메라 오브젝트에는 다른 쓰기가 없다.
        CameraDirectorComponent* pRig = findOn<CameraDirectorComponent>( _cameraRig );
        if ( pRig == nullptr )
            return;
        const float3 fieldCenter{ static_cast<float32>( kFieldWidth ) * 0.5f, 0.0f, static_cast<float32>( kFieldHeight ) * 0.5f };
        pRig->setFocusPoint( fieldCenter + ( _playerPosition - fieldCenter ) * _cameraFollow );
    }

    void FarmGameModeComponent::logStatus( bool bForce )
    {
        const SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return;
        const WorldClock& clock = pState->getClock();
        const int32       hour  = clock.getHourInt();
        if ( bForce == false && ( hour == _lastLoggedHour || hour % 6 != 0 ) )
            return;
        _lastLoggedHour                       = hour;
        [[maybe_unused]] const CropDef* pCrop = _pCropCatalog->findCropBySeed( getSelectedSeed() );
        SW_LOG_INFO_ID( "FarmGameMode.YGoldGStamina", "[Farm] %# %# Y%# %#:%# · gold %#G · stamina %#/%# · tool %# · seed %# x%# · crops %# (ready %#)", clock.getSeasonName().c_str(),
                        clock.getDayOfSeason() + 1, clock.getYear(), hour, clock.getMinute(), pState->getWallet().getBalance( _shipment.getCurrency() ), _stamina, _maxStamina,
                        getEnumName( _tool ), pCrop != nullptr ? pCrop->_name.c_str() : "-", pState->getInventory().getItemCount( getSelectedSeed() ),
                        _field.getCropCount(), _field.getReadyCount() );
    }

    bool FarmGameModeComponent::isNearShippingBin() const
    {
        return float3::getHorizontalDistance( _playerPosition, _shippingBinPosition ) < _nearDistance;
    }

    bool FarmGameModeComponent::isNearShop() const
    {
        return float3::getHorizontalDistance( _playerPosition, _shopPosition ) < _nearDistance + 0.5f;
    }

    const hashed_string& FarmGameModeComponent::getSelectedSeed() const
    {
        static const hashed_string kNoSeed{};
        if ( _listSeed.empty() )
            return kNoSeed;
        return _listSeed[static_cast<size_t>( MathUtil::clamp( _selectedSeedIndex, 0, static_cast<int32>( _listSeed.size() ) - 1 ) )];
    }

    int32 FarmGameModeComponent::getToolStaminaCost( FarmTool tool ) const
    {
        switch ( tool )
        {
            case FarmTool::Hoe:
                return _hoeStaminaCost;
            case FarmTool::WateringCan:
                return _wateringStaminaCost;
            case FarmTool::Seeds:
            case FarmTool::Hand:
                return _handStaminaCost;
        }
        return _handStaminaCost;
    }

    bool FarmGameModeComponent::tryGetObjectPosition( GameObjectHandle handle, float3& outPosition ) const
    {
        GameObjectManager*    pManager = getObjectManager();
        const GameObject*     pObject  = pManager != nullptr ? pManager->resolveGameObject( handle ) : nullptr;
        const SceneComponent* pScene   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return false;
        outPosition = pScene->getWorldPosition();
        return true;
    }
} // namespace sw
