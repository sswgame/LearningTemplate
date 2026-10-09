#include "pch.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/GameAutoplay.h"

#include "GameFramework/Base/Actor/Camera/OrthoCameraRigComponent.h"
#include "GameFramework/Base/Actor/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Intent/ControlIntent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/World/Environment/WorldClock.h"

#include "Games/HarvestValley/FarmAutoFarmerAiComponent.h"
#include "Games/HarvestValley/FarmCropComponent.h"
#include "Games/HarvestValley/FarmSoilComponent.h"

namespace sw
{
    SW_LOG_CALLER( "FarmDirector" );

    namespace
    {
        struct FarmDirectorComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCcUtil::make( "FARM" );
            static constexpr uint32 kStateVersion = 3;

            /** @brief 폰 버튼 이름 — `FarmButton` 순서입니다(입력 맵 `data/farm.input.xml` 의 액션 이름과 같다). */
            static constexpr const utf8* kArrButtonName[] = { "Farm.Tool1", "Farm.Tool2", "Farm.Tool3", "Farm.Tool4", "Farm.SeedPrev", "Farm.SeedNext",
                                                              "Farm.Use", "Farm.Ship", "Farm.Buy", "Farm.Sleep", "Farm.Status" };
            static_assert( sizeof( kArrButtonName ) / sizeof( kArrButtonName[0] ) == static_cast<size_t>( FarmButton::Count ), "one name per farm button" );

            static constexpr float3 kDefaultShippingBinPosition{ 13.4f, 0.0f, 1.0f };
            static constexpr float3 kDefaultShopPosition{ -1.6f, 0.0f, 5.0f };

            static constexpr const utf8* kSoundSelect  = "game/harvestvalley/sounds/select_002.ogg";
            static constexpr const utf8* kSoundPlant   = "game/harvestvalley/sounds/drop_002.ogg";
            static constexpr const utf8* kSoundHarvest = "game/harvestvalley/sounds/pluck_001.ogg";
            static constexpr const utf8* kSoundShip    = "game/harvestvalley/sounds/confirmation_001.ogg";

            /** @brief 공유 시계의 계절 이름(시계 설정 · 작물 카탈로그가 함께 쓴다)입니다. */
            static vector<hashed_string> makeSeasons() { return { hashed_string( "Spring" ), hashed_string( "Summer" ), hashed_string( "Fall" ), hashed_string( "Winter" ) }; }

            static int32 getToolStaminaCost( FarmTool tool )
            {
                switch ( tool )
                {
                    case FarmTool::Hoe:
                        return 4;
                    case FarmTool::WateringCan:
                        return 2;
                    case FarmTool::Seeds:
                    case FarmTool::Hand:
                        return 1;
                }
                return 1;
            }

            static const utf8* toToolName( FarmTool tool )
            {
                switch ( tool )
                {
                    case FarmTool::Hoe:
                        return "Hoe";
                    case FarmTool::WateringCan:
                        return "WateringCan";
                    case FarmTool::Seeds:
                        return "Seeds";
                    case FarmTool::Hand:
                        return "Hand";
                }
                return "?";
            }

            /** @brief 다 자란 작물의 색입니다. 모르는 작물은 초록이다. */
            static float4 findCropColor( const hashed_string& cropId )
            {
                struct CropColor
                {
                    const utf8* _pId;
                    float4      _color;
                };
                static constexpr CropColor kArrCropColor[] = {
                    {      "turnip", { 0.92f, 0.88f, 0.95f, 1.0f }},
                    {      "potato", { 0.72f, 0.56f, 0.34f, 1.0f }},
                    {    "cucumber", { 0.25f, 0.55f, 0.20f, 1.0f }},
                    {  "strawberry", { 0.90f, 0.15f, 0.20f, 1.0f }},
                    {      "tomato", { 0.95f, 0.25f, 0.15f, 1.0f }},
                    {        "corn", { 0.98f, 0.85f, 0.25f, 1.0f }},
                    {       "onion", { 0.85f, 0.70f, 0.45f, 1.0f }},
                    {     "pumpkin", { 0.95f, 0.50f, 0.10f, 1.0f }},
                    {    "eggplant", { 0.40f, 0.15f, 0.50f, 1.0f }},
                    {      "carrot", { 0.95f, 0.45f, 0.10f, 1.0f }},
                    {"sweet_potato", { 0.70f, 0.30f, 0.40f, 1.0f }},
                    {     "spinach", { 0.15f, 0.45f, 0.15f, 1.0f }},
                };
                for ( const CropColor& entry : kArrCropColor )
                {
                    if ( cropId == hashed_string( entry._pId ) )
                        return entry._color;
                }
                return float4{ 0.3f, 0.7f, 0.3f, 1.0f };
            }

            static float32 computeDistanceXz( const float3& lhs, const float3& rhs )
            {
                const float32 dx = lhs._x - rhs._x;
                const float32 dz = lhs._z - rhs._z;
                return MathUtil::sqrt( dx * dx + dz * dz );
            }
        };

        /** @brief 탐침이 읽는 디렉터 — 씬의 첫 농장 디렉터입니다(탐침은 단언 단계에서만 불린다 — 프레임 경로가 아니다). */
        const FarmDirectorComponent* findProbeDirector( const GameObjectManager* pManager )
        {
            const FarmDirectorComponent* pFound = nullptr;
            if ( pManager != nullptr )
            {
                pManager->forEachComponentOfType<FarmDirectorComponent>( [&pFound]( const FarmDirectorComponent* pDirector )
                {
                    if ( pFound == nullptr )
                        pFound = pDirector;
                } );
            }
            return pFound;
        }

        [[nodiscard]] bool readFarmerX( const GameObjectManager* pManager, float64& outValue )
        {
            const FarmDirectorComponent* pDirector = findProbeDirector( pManager );
            if ( pDirector == nullptr )
                return false;
            outValue = static_cast<float64>( pDirector->getPlayerPosition()._x );
            return true;
        }

        [[nodiscard]] bool readFarmerTool( const GameObjectManager* pManager, float64& outValue )
        {
            const FarmDirectorComponent* pDirector = findProbeDirector( pManager );
            if ( pDirector == nullptr )
                return false;
            outValue = static_cast<float64>( pDirector->getTool() );
            return true;
        }

        [[nodiscard]] bool readTilledCount( const GameObjectManager* pManager, float64& outValue )
        {
            const FarmDirectorComponent* pDirector = findProbeDirector( pManager );
            if ( pDirector == nullptr )
                return false;
            int32 tilledCount = 0;
            for ( int32 y = 0; y < FarmDirectorComponent::kFieldHeight; ++y )
            {
                for ( int32 x = 0; x < FarmDirectorComponent::kFieldWidth; ++x )
                {
                    const FarmTile* pTile = pDirector->getField().findTile( x, y );
                    tilledCount += pTile != nullptr && pTile->_bTilled != SW_FALSE ? 1 : 0;
                }
            }
            outValue = tilledCount;
            return true;
        }

        [[nodiscard]] bool readCropCount( const GameObjectManager* pManager, float64& outValue )
        {
            const FarmDirectorComponent* pDirector = findProbeDirector( pManager );
            if ( pDirector == nullptr )
                return false;
            outValue = pDirector->getField().getCropCount();
            return true;
        }

        [[nodiscard]] bool readFarmerControllerKind( const GameObjectManager* pManager, float64& outValue )
        {
            const FarmDirectorComponent* pDirector = findProbeDirector( pManager );
            if ( pDirector == nullptr )
                return false;
            outValue = pDirector->isFarmerDrivenByAi() ? 1.0 : 0.0;
            return true;
        }
    } // namespace

    /**
     * @brief `-gv_farmAutoPlay=1` — 디렉터의 자동 농부를 켭니다(씬의 `_bAutoPlay` 가 꺼져 있어도). 입력 없이 계절을 넘겨 보는 확인용입니다.
     * @details 배포본으로도 돌린다: `App -gv_farmAutoPlay=1 -gv_profileFrames=36000`(약 10 분 = 하루 다섯).
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_farmAutoPlay, 0, "HarvestValley: 농부도 AI 가 조종 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_farmAutoPlay, "HarvestValley", "The auto farmer AI controller possesses the farmer" );

    SW_AUTOMATION_PROBE( farmFarmerX, "Farm.FarmerX", "World X of the farmer", &readFarmerX );
    SW_AUTOMATION_PROBE( farmFarmerTool, "Farm.FarmerTool", "Tool in the farmer's hand (0 hoe, 1 watering can, 2 seeds, 3 hand)", &readFarmerTool );
    SW_AUTOMATION_PROBE( farmTilledCount, "Farm.TilledCount", "Tilled field tiles", &readTilledCount );
    SW_AUTOMATION_PROBE( farmCropCount, "Farm.CropCount", "Field tiles with a crop", &readCropCount );
    SW_AUTOMATION_PROBE( farmFarmerControllerKind, "Farm.FarmerControllerKind", "0 = the player controller holds the farmer, 1 = the auto farmer AI", &readFarmerControllerKind );
} // namespace sw

namespace sw
{
    FarmDirectorComponent::FarmDirectorComponent()
        : _cropDataPath{ "game/harvestvalley/data/crops.xml" }
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
        , _rainChance{ 0.25f }
        , _cameraFollow{ 0.4f }
        , _maxStamina{ 100 }
        , _startingGold{ 500 }
        , _secondsPerDay{ 144.0f }
        , _cropCatalog{}
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
        , _playerPosition{ 6.0f, 0.0f, -1.2f }
        , _facing{ 0.0f, 0.0f, 1.0f }
        , _shippingBinPosition{ FarmDirectorComponentInternal::kDefaultShippingBinPosition }
        , _shopPosition{ FarmDirectorComponentInternal::kDefaultShopPosition }
        , _random{ 0x2545f491u }
        , _autoFarmerObject{}
        , _stamina{ _maxStamina }
        , _selectedSeedIndex{ 0 }
        , _lastLoggedHour{ -1 }
        , _dayStarted{ 0 }
        , _hourOfDay{ 6.0f }
        , _tool{ FarmTool::Hoe }
        , _bRaining{ SW_FALSE }
        , _bPossessionDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    FarmDirectorComponent::~FarmDirectorComponent() = default;

    void FarmDirectorComponent::onGameStarted()
    {
        updateCameraFocus();
        SW_LOG_INFO( "[Farm] farm is ready - WASD move, Space use tool, 1-4 tool (hoe/can/seeds/hand), Q/E seed, B buy, F ship, Z sleep" );
        logStatus( true );
    }

    void FarmDirectorComponent::tickGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        // 농부는 폰의 의도대로 — 누가 냈는지(플레이어 · 자동 농부)는 모른다. 빙의는 틱 뒤 플러시가 스위치에 맞춘다.
        const PawnComponent* pFarmerPawn = findFarmerPawn();
        if ( pFarmerPawn != nullptr )
        {
            applyFarmerIntent( deltaTime, *pFarmerPawn );
            _bPossessionDirty = pFarmerPawn->isPossessed() == false || isAutoPlayOn() != isFarmerDrivenByAi() ? SW_TRUE : SW_FALSE;
        }

        // 시간 — 공유 시계는 공유 상태가 흘린다. 다음 날 2 시가 되면 그 자리에서 쓰러져 아침이다(체력 반).
        const GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState != nullptr )
        {
            const WorldClock& clock = pState->getClock();
            _hourOfDay              = clock.getHour();
            const bool bPassedOut   = _dayStarted < clock.getDay() && 2.0f <= clock.getHour();
            if ( bPassedOut )
            {
                SW_LOG_INFO( "[Farm] it is 2 AM - the farmer passed out" );
                endDay( true );
            }
        }
        updateCameraFocus();
        logStatus( false );
    }

    void FarmDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, FarmDirectorComponentInternal::kStateTag, FarmDirectorComponentInternal::kStateVersion );
        Archive fieldBody;
        _field.writeState( fieldBody );
        StateArchiveUtil::writeSection( outArchive, FarmField::kStateTag, FarmField::kStateVersion, fieldBody );
        Archive shipmentBody;
        _shipment.writeState( shipmentBody );
        StateArchiveUtil::writeSection( outArchive, FarmShippingBin::kStateTag, FarmShippingBin::kStateVersion, shipmentBody );
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _playerPosition;
        outArchive << _facing;
        outArchive << _stamina;
        outArchive << _selectedSeedIndex;
        outArchive << static_cast<uint8>( _tool );
        outArchive << static_cast<uint8>( _bRaining );
        outArchive << _dayStarted;
    }

    bool FarmDirectorComponent::readState( Archive& archive )
    {
        if ( StateArchiveUtil::readHeader( archive, FarmDirectorComponentInternal::kStateTag, FarmDirectorComponentInternal::kStateVersion ) == false )
            return false;
        FarmField       field;
        FarmShippingBin shipment;
        GameRandom      random;
        field.initialize( kFieldWidth, kFieldHeight, &_cropCatalog );
        uint32     fieldTag        = 0;
        uint32     fieldVersion    = 0;
        uint32     shipmentTag     = 0;
        uint32     shipmentVersion = 0;
        Archive    fieldBody;
        Archive    shipmentBody;
        const bool bSectionsRead = StateArchiveUtil::readSection( archive, fieldTag, fieldVersion, fieldBody ) &&
                                   StateArchiveUtil::readSection( archive, shipmentTag, shipmentVersion, shipmentBody );
        const bool bSimulationRead = bSectionsRead && fieldTag == FarmField::kStateTag && fieldVersion == FarmField::kStateVersion && field.readState( fieldBody ) &&
                                     fieldBody.getRemainingBytes() == 0 && shipmentTag == FarmShippingBin::kStateTag &&
                                     shipmentVersion == FarmShippingBin::kStateVersion && shipment.readState( shipmentBody ) && shipmentBody.getRemainingBytes() == 0 &&
                                     StateArchiveUtil::readRandom( archive, random );
        if ( bSimulationRead == false )
            return false;
        float3 playerPosition{};
        float3 facing{};
        int32  stamina           = 0;
        int32  selectedSeedIndex = 0;
        uint8  tool              = 0;
        uint8  bRaining          = SW_FALSE;
        archive >> playerPosition;
        archive >> facing;
        archive >> stamina;
        archive >> selectedSeedIndex;
        archive >> tool;
        archive >> bRaining;
        int32 dayStarted = 0;
        archive >> dayStarted;
        const bool bToolValid = tool <= static_cast<uint8>( FarmTool::Hand );
        if ( archive.isError() || archive.getRemainingBytes() != 0 || bToolValid == false )
            return false;
        _field             = std::move( field );
        _shipment          = std::move( shipment );
        _dayStarted        = dayStarted;
        _random            = random;
        _playerPosition    = playerPosition;
        _facing            = facing;
        _stamina           = MathUtil::clamp( stamina, 0, _maxStamina );
        _selectedSeedIndex = _listSeed.empty() ? 0 : MathUtil::clamp( selectedSeedIndex, 0, static_cast<int32>( _listSeed.size() ) - 1 );
        _tool              = static_cast<FarmTool>( tool );
        _bRaining          = bRaining != SW_FALSE ? SW_TRUE : SW_FALSE;
        return true;
    }

    void FarmDirectorComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
            SW_LOG_INFO( "[Farm] farm state restored - started on day %#", _dayStarted );
        else
            SW_LOG_WARNING( "[Farm] the saved farm state does not match this build - starting a new farm" );
        _lastLoggedHour = -1;
    }

    bool FarmDirectorComponent::findTargetTile( int32& outX, int32& outY ) const
    {
        const float3 target = _playerPosition + _facing * _reach;
        outX                = static_cast<int32>( MathUtil::floor( target._x ) );
        outY                = static_cast<int32>( MathUtil::floor( target._z ) );
        return _field.findTile( outX, outY ) != nullptr;
    }

    const shared_ptr<MaterialInstance>& FarmDirectorComponent::getSoilLook( int32 soilState ) const
    {
        return _arrSoilLook[MathUtil::clamp( soilState, 0, 2 )];
    }

    const shared_ptr<MaterialInstance>& FarmDirectorComponent::findCropLook( const hashed_string& cropId, bool bWithered, bool bCropColored ) const
    {
        if ( bWithered )
            return _witheredCropLook;
        if ( bCropColored )
        {
            // 뷰가 워커에서 부른다 — 컨테이너 첨자 대신 포인터로 읽는다.
            const CropLook* pLook = _listCropLook.data();
            for ( size_t lookIndex = 0; lookIndex < _listCropLook.size(); ++lookIndex )
            {
                if ( pLook[lookIndex]._cropId == cropId )
                    return pLook[lookIndex]._instance;
            }
        }
        return _plainCropLook;
    }

    float3 FarmDirectorComponent::computeTileCenter( int32 x, int32 y )
    {
        return float3{ static_cast<float32>( x ) + 0.5f, 0.0f, static_cast<float32>( y ) + 0.5f };
    }

    // ------------------------------------------------------------------------------
    // 데이터
    // ------------------------------------------------------------------------------
    bool FarmDirectorComponent::startGame()
    {
        _cropCatalog.setKnownSeasons( FarmDirectorComponentInternal::makeSeasons() );
        if ( _cropCatalog.loadFromResource( _cropDataPath ) == false || _cropCatalog.getCrops().empty() )
        {
            SW_LOG_WARNING( "[Farm] %# could not be loaded - the farm cannot start", _cropDataPath.c_str() );
            return false;
        }
        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
        {
            SW_LOG_WARNING( "[Farm] no GameStateComponent before the director on this object - the farm cannot start" );
            return false;
        }
        _itemCatalog = ItemCatalog{};
        _cropCatalog.fillItemCatalog( _itemCatalog, 99 );
        GameStateSettings settings;
        settings._clock._listSeason    = FarmDirectorComponentInternal::makeSeasons();
        settings._clock._secondsPerDay = _secondsPerDay;
        settings._clock._startHour     = 6.0f;
        settings._clock._daysPerSeason = 28;
        settings._pItemCatalog         = &_itemCatalog;
        settings._inventorySlotCount   = kBagSlotCount;
        const bool bFresh              = pState->initialize( settings ) == GameStateInitResult::Fresh;
        _field.initialize( kFieldWidth, kFieldHeight, &_cropCatalog );
        _shipment = FarmShippingBin{};
        _listSeed.clear();
        for ( const CropDef& crop : _cropCatalog.getCrops() )
        {
            _listSeed.push_back( crop._seedItem );
        }
        // 시작 돈 · 씨앗은 새 판에만 — 되살린 판에 덧쌓이지 않게.
        if ( bFresh )
        {
            pState->getWallet().add( _shipment.getCurrency(), _startingGold );
            (void)pState->getInventory().addItem( _cropCatalog.getCrops().front()._seedItem, 10 );
        }
        _dayStarted = pState->getClock().getDay();
        _hourOfDay  = pState->getClock().getHour();
        // 씬에 놓인 출하함 · 가게가 있으면 그 자리다 — 에디터에서 옮기면 농부가 서는 자리가 따라온다.
        _shippingBinPosition = findObjectPosition( _shippingBin, FarmDirectorComponentInternal::kDefaultShippingBinPosition );
        _shopPosition        = findObjectPosition( _shop, FarmDirectorComponentInternal::kDefaultShopPosition );
        _playerPosition      = _playerStart;
        _facing              = float3{ 0.0f, 0.0f, 1.0f };
        _stamina             = _maxStamina;
        _tool                = FarmTool::Hoe;
        return true;
    }

    // ------------------------------------------------------------------------------
    // 스폰(틱 뒤 · 게임 스레드)
    // ------------------------------------------------------------------------------
    void FarmDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews )
            spawnField( manager );
        syncFarmerPossession( manager );
    }

    void FarmDirectorComponent::onViewsDespawned()
    {
        _autoFarmerObject = GameObjectHandle{};
        _bPossessionDirty = SW_FALSE;
    }

    PawnComponent* FarmDirectorComponent::findFarmerPawn() const
    {
        GameObjectManager* pManager = getObjectManager();
        GameObject*        pObject  = pManager != nullptr ? pManager->resolveGameObject( _farmer ) : nullptr;
        return pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
    }

    bool FarmDirectorComponent::isFarmerDrivenByAi() const
    {
        GameObjectManager*   pManager    = getObjectManager();
        const PawnComponent* pPawn       = findFarmerPawn();
        const GameObject*    pAutoFarmer = pManager != nullptr ? pManager->resolveGameObject( _autoFarmerObject ) : nullptr;
        const Component*     pController = pPawn != nullptr && pManager != nullptr ? pManager->resolveComponent( pPawn->getController() ) : nullptr;
        return pController != nullptr && pAutoFarmer != nullptr && pController->getOwner() == pAutoFarmer;
    }

    const utf8* FarmDirectorComponent::getButtonName( FarmButton button )
    {
        const size_t index = static_cast<size_t>( button );
        return index < static_cast<size_t>( FarmButton::Count ) ? FarmDirectorComponentInternal::kArrButtonName[index] : "";
    }

    void FarmDirectorComponent::syncFarmerPossession( GameObjectManager& manager )
    {
        _bPossessionDirty    = SW_FALSE;
        PawnComponent* pPawn = findFarmerPawn();
        if ( pPawn == nullptr || isStarted() == false )
            return;
        if ( isAutoPlayOn() )
        {
            GameObject*                pObject = manager.resolveGameObject( _autoFarmerObject );
            FarmAutoFarmerAiComponent* pAi     = pObject != nullptr ? pObject->getComponent<FarmAutoFarmerAiComponent>() : nullptr;
            if ( pAi == nullptr )
            {
                pObject           = spawnPrefab( manager, _autoFarmerPrefab, "FarmAutoFarmer" );
                pAi               = pObject != nullptr ? pObject->getComponent<FarmAutoFarmerAiComponent>() : nullptr;
                _autoFarmerObject = pObject != nullptr ? pObject->getHandle() : GameObjectHandle{};
            }
            if ( pAi == nullptr )
            {
                SW_LOG_WARNING( "[Farm] auto farmer prefab '%#' has no FarmAutoFarmerAiComponent - auto play cannot take the farmer", _autoFarmerPrefab.c_str() );
                return;
            }
            pAi->assignDirector( getOwner()->getHandle() );
            if ( isFarmerDrivenByAi() == false )
                pAi->possess( *pPawn );
            return;
        }
        // 자동 플레이를 끄면 플레이어 0 의 조종자가 되찾는다 — 씬에 없으면 세운다(조종 시스템이 자동 빙의로 세우는 것과 같은 자리).
        PlayerControllerComponent* pPlayer = nullptr;
        for ( PlayerControllerComponent* pCandidate : manager.getComponentRegistry().getAll<PlayerControllerComponent>() )
        {
            if ( pCandidate != nullptr && pCandidate->getPlayerIndex() == 0 )
            {
                pPlayer = pCandidate;
                break;
            }
        }
        if ( pPlayer == nullptr )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "PlayerController" ) );
            pPlayer             = pObject != nullptr ? pObject->addComponent<PlayerControllerComponent>() : nullptr;
        }
        if ( pPlayer != nullptr && pPlayer->getPawn() != pPawn->getHandle() )
            pPlayer->possess( *pPawn );
    }

    void FarmDirectorComponent::spawnField( GameObjectManager& manager )
    {
        // 칸마다 흙 · 작물 하나씩. 각 뷰가 밭의 같은 칸을 따라 모습을 맞춘다(PostUpdate).
        const GameObjectHandle director = getOwner()->getHandle();
        for ( int32 y = 0; y < kFieldHeight; ++y )
        {
            for ( int32 x = 0; x < kFieldWidth; ++x )
            {
                const int32  tileIndex = y * kFieldWidth + x;
                const float3 center    = computeTileCenter( x, y );
                GameObject*  pSoil     = spawnPrefab( manager, _soilPrefab, "FarmSoil" );
                GameObject*  pCrop     = spawnPrefab( manager, _cropPrefab, "FarmCrop" );
                if ( pSoil == nullptr || pCrop == nullptr )
                    continue;
                MeshComponent* pSoilMesh = pSoil->getComponent<MeshComponent>();
                MeshComponent* pCropMesh = pCrop->getComponent<MeshComponent>();
                if ( _arrSoilLook[0] == nullptr && pSoilMesh != nullptr && pCropMesh != nullptr )
                    prepareLooks( pSoilMesh->getMaterial(), pCropMesh->getMaterial() );
                if ( pSoilMesh != nullptr )
                    pSoilMesh->setLocalPosition( center + float3{ 0.0f, pSoilMesh->getLocalScale()._y * 0.5f, 0.0f } );
                if ( pCropMesh != nullptr )
                    pCropMesh->setLocalPosition( center );
                FarmSoilComponent* pSoilView = pSoil->getComponent<FarmSoilComponent>();
                if ( pSoilView != nullptr )
                    pSoilView->assignTile( director, tileIndex );
                FarmCropComponent* pCropView = pCrop->getComponent<FarmCropComponent>();
                if ( pCropView != nullptr )
                    pCropView->assignTile( director, tileIndex );
            }
        }
    }

    void FarmDirectorComponent::prepareLooks( Material* pSoilMaterial, Material* pCropMaterial )
    {
        const float4 arrSoilColor[3] = { _grassSoilColor, _tilledSoilColor, _wateredSoilColor };
        for ( int32 soilState = 0; soilState < 3; ++soilState )
        {
            _arrSoilLook[soilState] = _tintCache.acquire( pSoilMaterial, arrSoilColor[soilState] );
        }
        _plainCropLook    = _tintCache.acquire( pCropMaterial, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
        _witheredCropLook = _tintCache.acquire( pCropMaterial, _witheredCropColor );
        _listCropLook.clear();
        for ( const CropDef& crop : _cropCatalog.getCrops() )
        {
            CropLook look;
            look._cropId   = crop._id;
            look._instance = _tintCache.acquire( pCropMaterial, FarmDirectorComponentInternal::findCropColor( crop._id ) );
            _listCropLook.push_back( look );
        }
        _listCropMesh.clear();
        vector<string> listModelPath;
        FarmCropComponent::collectModelPaths( listModelPath );
        for ( const string& path : listModelPath )
        {
            shared_ptr<Mesh> mesh = MeshCache::acquire( path );
            if ( mesh != nullptr )
                _listCropMesh.push_back( std::move( mesh ) );
        }
    }

    // ------------------------------------------------------------------------------
    // 농부 폰의 의도(PrePhysics — 워커, 의도는 조종 시스템이 틱 전에 채웠다)
    // ------------------------------------------------------------------------------
    void FarmDirectorComponent::applyFarmerIntent( float32 deltaTime, const PawnComponent& pawn )
    {
        using Internal              = FarmDirectorComponentInternal;
        const ControlIntent& intent = pawn.getIntent();
        const float3         move   = intent.computeWorldMove();
        movePlayer( float3{ move._x, 0.0f, move._z }, deltaTime );

        // 버튼 — 도구를 먼저 바꾸고 쓴다(같은 틱에 둘 다 누를 수 있다).
        int32 arrButtonIndex[kButtonCount];
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
                    SW_LOG_INFO( "[Farm] tool: %#", Internal::toToolName( _tool ) );
                    getSoundQueue().queueClip( Internal::kSoundSelect );
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

    void FarmDirectorComponent::movePlayer( const float3& direction, float32 deltaTime )
    {
        // 길이는 걷는 빠르기의 비율(스틱 반만 기울이면 반 빠르기) — 1 을 넘으면(대각선 키) 1 로 자른다.
        const float32 length = direction.getLength();
        if ( length < 1.0e-4f )
            return;
        const float3 step  = direction * ( _walkSpeed * deltaTime * MathUtil::min( length, 1.0f ) / length );
        _playerPosition._x = MathUtil::clamp( _playerPosition._x + step._x, -5.0f, static_cast<float32>( kFieldWidth ) + 4.0f );
        _playerPosition._z = MathUtil::clamp( _playerPosition._z + step._z, -5.0f, static_cast<float32>( kFieldHeight ) + 4.0f );
        // 네 방향 — 대각선이면 더 큰 축.
        if ( MathUtil::abs( direction._x ) > MathUtil::abs( direction._z ) )
            _facing = float3{ direction._x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f };
        else
            _facing = float3{ 0.0f, 0.0f, direction._z > 0.0f ? 1.0f : -1.0f };
    }

    // ------------------------------------------------------------------------------
    // 행동
    // ------------------------------------------------------------------------------
    void FarmDirectorComponent::useTool()
    {
        using Internal = FarmDirectorComponentInternal;
        int32 x        = 0;
        int32 y        = 0;
        if ( findTargetTile( x, y ) == false )
            return;
        const int32 cost = Internal::getToolStaminaCost( _tool );
        if ( _stamina < cost )
        {
            SW_LOG_INFO( "[Farm] too tired - press Z to sleep" );
            return;
        }

        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        FarmActionResult    result = FarmActionResult::Done;
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
                    SW_LOG_INFO( "[Farm] no %# left - buy more at the shop (B)", seed.c_str() );
                    return;
                }
                result = _field.plant( x, y, seed, pState->getClock().getSeasonName() );
                if ( result == FarmActionResult::Done && pState->getInventory().removeItem( seed, 1 ) == false )
                    SW_LOG_WARNING( "[Farm] planted without a seed in the bag" );
                if ( result == FarmActionResult::Done )
                    getSoundQueue().queueClip( Internal::kSoundPlant );
                break;
            }
            case FarmTool::Hand:
            {
                hashed_string produce;
                int32         count = 0;
                result              = _field.harvest( x, y, produce, count );
                if ( result == FarmActionResult::Done && count > 0 )
                {
                    // 가방이 차면 덜 들어간다(남은 것은 밭에서 사라진다) — 넣은 수를 적는다.
                    const int32 added = pState != nullptr ? pState->getInventory().addItem( produce, count ) : 0;
                    SW_LOG_INFO( "[Farm] harvested %# x%# (%# into the bag)", produce.c_str(), count, added );
                    getSoundQueue().queueClip( Internal::kSoundHarvest );
                }
                break;
            }
        }
        if ( result == FarmActionResult::Done )
            _stamina -= cost;
        else if ( isAutoPlayOn() == false )
            SW_LOG_INFO( "[Farm] %# at (%#, %#): %#", Internal::toToolName( _tool ), x, y, toString( result ) );
    }

    int32 FarmDirectorComponent::shipAllProduce()
    {
        if ( isNearShippingBin() == false )
        {
            SW_LOG_INFO( "[Farm] stand next to the shipping bin to ship" );
            return 0;
        }
        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return 0;
        Inventory& bag     = pState->getInventory();
        int32      shipped = 0;
        for ( const CropDef& crop : _cropCatalog.getCrops() )
        {
            const int32 count = bag.getItemCount( crop._produceItem );
            if ( count > 0 && _shipment.shipItem( bag, crop._produceItem, count ) )
                shipped += count;
        }
        if ( shipped > 0 )
        {
            SW_LOG_INFO( "[Farm] shipped %# items - paid tonight", shipped );
            getSoundQueue().queueClip( FarmDirectorComponentInternal::kSoundShip );
        }
        return shipped;
    }

    bool FarmDirectorComponent::buySelectedSeed()
    {
        if ( isNearShop() == false )
        {
            SW_LOG_INFO( "[Farm] stand next to the shop to buy seeds" );
            return false;
        }
        const CropDef* pCrop = _cropCatalog.findCropBySeed( getSelectedSeed() );
        if ( pCrop == nullptr )
            return false;
        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return false;
        Inventory& bag    = pState->getInventory();
        Wallet&    wallet = pState->getWallet();
        if ( bag.hasRoomFor( pCrop->_seedItem, 1 ) == false )
        {
            SW_LOG_INFO( "[Farm] the bag is full - no room for %#", pCrop->_seedItem.c_str() );
            return false;
        }
        if ( wallet.trySpend( _shipment.getCurrency(), pCrop->_seedPrice ) == false )
        {
            SW_LOG_INFO( "[Farm] not enough gold for %# (%#G)", pCrop->_seedItem.c_str(), pCrop->_seedPrice );
            return false;
        }
        (void)bag.addItem( pCrop->_seedItem, 1 );
        SW_LOG_INFO( "[Farm] bought %# (%#G) - %#G left", pCrop->_seedItem.c_str(), pCrop->_seedPrice, wallet.getBalance( _shipment.getCurrency() ) );
        return true;
    }

    void FarmDirectorComponent::endDay( bool bPassedOut )
    {
        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return;
        WorldClock&                  clock        = pState->getClock();
        [[maybe_unused]] const int32 earned       = _shipment.settleShipping( _cropCatalog, pState->getWallet() );
        const hashed_string          seasonBefore = clock.getSeasonName();
        clock.advanceToHour( 6.0f ); // 잠 — 다음 날 아침
        _dayStarted                               = clock.getDay();
        _hourOfDay                                = clock.getHour();
        [[maybe_unused]] const bool bSeasonChange = clock.getSeasonName() != seasonBefore;
        const bool                  bRaining      = clock.getSeasonName() != hashed_string( "Winter" ) && _random.nextChance( _rainChance );
        _bRaining                                 = bRaining ? SW_TRUE : SW_FALSE;
        _field.advanceDay( clock.getSeasonName(), bRaining );
        _stamina        = bPassedOut ? _maxStamina / 2 : _maxStamina;
        _playerPosition = _playerStart;
        _facing         = float3{ 0.0f, 0.0f, 1.0f };
        SW_LOG_INFO( "[Farm] good morning - %# %#, year %# · shipped for %#G · gold %#G · %#%#", clock.getSeasonName().c_str(), clock.getDayOfSeason() + 1,
                     clock.getYear(), earned, pState->getWallet().getBalance( _shipment.getCurrency() ), bRaining ? "rain" : "sunny",
                     bSeasonChange ? " · a new season - out-of-season crops withered" : "" );
        _lastLoggedHour = -1;
    }

    void FarmDirectorComponent::selectSeed( int32 offset )
    {
        if ( _listSeed.empty() )
            return;
        const int32 count  = static_cast<int32>( _listSeed.size() );
        _selectedSeedIndex = ( ( _selectedSeedIndex + offset ) % count + count ) % count;
        if ( isAutoPlayOn() )
            return; // 자동 농부는 씨앗을 한 칸씩 넘겨 고른다 — 그 줄은 남기지 않는다
        [[maybe_unused]] const CropDef*            pCrop  = _cropCatalog.findCropBySeed( getSelectedSeed() );
        [[maybe_unused]] const GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        SW_LOG_INFO( "[Farm] seed: %# (have %#, %#G)%#", getSelectedSeed().c_str(), pState != nullptr ? pState->getInventory().getItemCount( getSelectedSeed() ) : 0,
                     pCrop != nullptr ? pCrop->_seedPrice : 0, pCrop != nullptr && pState != nullptr && pCrop->growsIn( pState->getClock().getSeasonName() ) ? "" : " - not this season" );
    }

    void FarmDirectorComponent::updateCameraFocus()
    {
        // 비스듬히 내려다보는 직교 시점 — 밭 가운데와 농부 사이를 본다. 리그는 PostUpdate 에서 이 초점을 읽고, 리그의 오브젝트에는 다른 쓰기가 없다.
        GameObjectManager*       pManager   = getObjectManager();
        const GameObject*        pRigObject = pManager != nullptr ? pManager->resolveGameObject( _cameraRig ) : nullptr;
        OrthoCameraRigComponent* pRig       = pRigObject != nullptr ? pRigObject->getComponent<OrthoCameraRigComponent>() : nullptr;
        if ( pRig == nullptr )
            return;
        const float3 fieldCenter{ static_cast<float32>( kFieldWidth ) * 0.5f, 0.0f, static_cast<float32>( kFieldHeight ) * 0.5f };
        pRig->setFocus( fieldCenter + ( _playerPosition - fieldCenter ) * _cameraFollow );
    }

    void FarmDirectorComponent::logStatus( bool bForce )
    {
        const GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return;
        const WorldClock& clock = pState->getClock();
        const int32       hour  = clock.getHourInt();
        if ( bForce == false && ( hour == _lastLoggedHour || hour % 6 != 0 ) )
            return;
        _lastLoggedHour                       = hour;
        [[maybe_unused]] const CropDef* pCrop = _cropCatalog.findCropBySeed( getSelectedSeed() );
        SW_LOG_INFO( "[Farm] %# %# Y%# %#:%# · gold %#G · stamina %#/%# · tool %# · seed %# x%# · crops %# (ready %#)", clock.getSeasonName().c_str(),
                     clock.getDayOfSeason() + 1, clock.getYear(), hour, clock.getMinute(), pState->getWallet().getBalance( _shipment.getCurrency() ), _stamina, _maxStamina,
                     FarmDirectorComponentInternal::toToolName( _tool ), pCrop != nullptr ? pCrop->_name.c_str() : "-", pState->getInventory().getItemCount( getSelectedSeed() ),
                     _field.getCropCount(), _field.getReadyCount() );
    }

    bool FarmDirectorComponent::isNearShippingBin() const
    {
        return FarmDirectorComponentInternal::computeDistanceXz( _playerPosition, _shippingBinPosition ) < _nearDistance;
    }

    bool FarmDirectorComponent::isNearShop() const
    {
        return FarmDirectorComponentInternal::computeDistanceXz( _playerPosition, _shopPosition ) < _nearDistance + 0.5f;
    }

    const hashed_string& FarmDirectorComponent::getSelectedSeed() const
    {
        static const hashed_string kNoSeed{};
        if ( _listSeed.empty() )
            return kNoSeed;
        return _listSeed[static_cast<size_t>( MathUtil::clamp( _selectedSeedIndex, 0, static_cast<int32>( _listSeed.size() ) - 1 ) )];
    }

    float3 FarmDirectorComponent::findObjectPosition( GameObjectHandle handle, const float3& fallback ) const
    {
        GameObjectManager*    pManager = getObjectManager();
        const GameObject*     pObject  = pManager != nullptr ? pManager->resolveGameObject( handle ) : nullptr;
        const SceneComponent* pScene   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
        return pScene != nullptr ? pScene->getWorldPosition() : fallback;
    }
} // namespace sw
