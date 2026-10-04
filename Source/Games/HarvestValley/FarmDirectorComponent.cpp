#include "pch.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Utility/GameAutoplay.h"

#include "GameFramework/Components/OrthoCameraRigComponent.h"
#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameSound.h"

#include "Games/HarvestValley/FarmCropComponent.h"
#include "Games/HarvestValley/FarmSoilComponent.h"

namespace sw
{
    SW_LOG_CALLER( "FarmDirector" );

    namespace
    {
        struct FarmDirectorComponentInternal
        {
            static constexpr float32 kWalkSpeed          = 4.0f; ///< 농부 걸음(m/s)
            static constexpr float32 kReach              = 0.9f; ///< 바라보는 칸을 고르는 거리(m)
            static constexpr float32 kNearDistance       = 1.7f; ///< 출하함 · 가게 옆으로 치는 거리(m)
            static constexpr float32 kAutoActionInterval = 0.25f;
            static constexpr float32 kRainChance         = 0.25f;
            static constexpr int32   kAutoCultivateLimit = 32;   ///< 자동 농부가 가꾸는 칸 수(체력이 하루에 감당하는 만큼)
            static constexpr float32 kCameraFollow       = 0.4f; ///< 카메라 초점이 밭 가운데에서 농부 쪽으로 가는 비율

            static constexpr float3 kDefaultShippingBinPosition{ 13.4f, 0.0f, 1.0f };
            static constexpr float3 kDefaultShopPosition{ -1.6f, 0.0f, 5.0f };

            static constexpr const utf8* kSoundSelect  = "game/harvestvalley/sounds/select_002.ogg";
            static constexpr const utf8* kSoundPlant   = "game/harvestvalley/sounds/drop_002.ogg";
            static constexpr const utf8* kSoundHarvest = "game/harvestvalley/sounds/pluck_001.ogg";
            static constexpr const utf8* kSoundShip    = "game/harvestvalley/sounds/confirmation_001.ogg";

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

        /** @brief 자동 농부가 하려는 일입니다. */
        struct FarmAutoTask
        {
            float3   _standPosition{};
            FarmTool _tool{ FarmTool::Hand };
            int32    _kind{ 0 }; ///< 0 = 없음(잔다), 1 = 칸에 도구, 2 = 출하, 3 = 가게
        };
    } // namespace

    /**
     * @brief `-gv_farmAutoPlay=1` — 디렉터의 자동 농부를 켭니다(씬의 `_bAutoPlay` 가 꺼져 있어도). 입력 없이 계절을 넘겨 보는 확인용입니다.
     * @details 배포본으로도 돌린다: `App -gv_farmAutoPlay=1 -gv_profileFrames=36000`(약 10 분 = 하루 다섯).
     */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_farmAutoPlay, 0, "HarvestValley: 농부도 AI 가 조종 (1=켜기)", SW_KEEP_IN_SHIPPING );
    SW_GAME_AUTOPLAY( gv_farmAutoPlay, "HarvestValley", "The farmer is driven by the AI" );
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
        , _playerStart{ 6.0f, 0.0f, -1.2f }
        , _bAutoPlay{ false }
        , _cropCatalog{}
        , _calendar{}
        , _field{}
        , _inventory{}
        , _listSeed{}
        , _listSpawned{}
        , _listPendingSound{}
        , _listColorLook{}
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
        , _autoTimer{ 0.0f }
        , _stamina{ kMaxStamina }
        , _selectedSeedIndex{ 0 }
        , _lastLoggedHour{ -1 }
        , _tool{ FarmTool::Hoe }
        , _bRaining{ SW_FALSE }
        , _bLoaded{ SW_FALSE }
        , _bViewsSpawned{ SW_FALSE }
        , _bFlushScheduled{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    FarmDirectorComponent::~FarmDirectorComponent() = default;

    void FarmDirectorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 규칙 · 입력은 앞 그룹 — 뷰 · 카메라 리그(PostUpdate)가 같은 프레임에 이 결과를 읽는다.
        setTickGroup( TickGroup::PrePhysics );
        if ( loadData() == false )
        {
            SW_LOG_WARNING( "[Farm] %# could not be loaded - the farm cannot start", _cropDataPath.c_str() );
            return;
        }
        scheduleFlush();
        updateCameraFocus();
        SW_LOG_INFO( "[Farm] farm is ready - WASD move, Space use tool, 1-4 tool (hoe/can/seeds/hand), Q/E seed, B buy, F ship, Z sleep" );
        logStatus( true );
    }

    void FarmDirectorComponent::onEndPlay()
    {
        despawnViews();
        Component::onEndPlay();
    }

    void FarmDirectorComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bLoaded == SW_FALSE )
            return;
        if ( _bViewsSpawned == SW_FALSE )
            scheduleFlush(); // 상태 저장 전에 걷었다 — 지금 상태대로 다시 세운다
        if ( deltaTime <= 0.0f )
            return;

        const InputManager* pInput = game::getService<InputManager>();
        if ( isAutoPlayOn() || pInput == nullptr )
            updateAutoFarmer( deltaTime );
        else
            updatePlayerInput( deltaTime, *pInput );

        // 시간 — 26:00 이 되면 그 자리에서 쓰러져 다음 날 아침이다(체력 반).
        if ( _calendar.advanceMinutes( deltaTime * kMinutesPerSecond ) )
        {
            SW_LOG_INFO( "[Farm] it is 2 AM - the farmer passed out" );
            endDay( true );
        }
        updateCameraFocus();
        logStatus( false );
        if ( _listPendingSound.empty() == false )
            scheduleFlush();
    }

    void FarmDirectorComponent::despawnViews()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager != nullptr )
        {
            for ( const GameObjectHandle& handle : _listSpawned )
            {
                GameObject* pObject = pManager->resolveGameObject( handle );
                if ( pObject != nullptr )
                    pManager->destroyObject( pObject );
            }
        }
        _listSpawned.clear();
        _bViewsSpawned = SW_FALSE;
    }

    bool FarmDirectorComponent::findTargetTile( int32& outX, int32& outY ) const
    {
        const float3 target = _playerPosition + _facing * FarmDirectorComponentInternal::kReach;
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

    const FarmDirectorComponent* FarmDirectorComponent::resolveDirector( const GameObjectManager& manager, GameObjectHandle director )
    {
        const GameObject* pObject = manager.resolveGameObject( director );
        return pObject != nullptr ? pObject->getComponent<FarmDirectorComponent>() : nullptr;
    }

    // ------------------------------------------------------------------------------
    // 데이터
    // ------------------------------------------------------------------------------
    bool FarmDirectorComponent::loadData()
    {
        if ( _cropCatalog.loadFromResource( _cropDataPath ) == false || _cropCatalog.getCrops().empty() )
            return false;
        _calendar.reset();
        _field.initialize( kFieldWidth, kFieldHeight, &_cropCatalog );
        _inventory = FarmInventory{};
        _inventory.addGold( kStartingGold );
        _listSeed.clear();
        for ( const CropDef& crop : _cropCatalog.getCrops() )
            _listSeed.push_back( crop._seedItem );
        _inventory.addItem( _cropCatalog.getCrops().front()._seedItem, 10 );
        // 씬에 놓인 출하함 · 가게가 있으면 그 자리다 — 에디터에서 옮기면 농부가 서는 자리가 따라온다.
        _shippingBinPosition = findObjectPosition( _shippingBin, FarmDirectorComponentInternal::kDefaultShippingBinPosition );
        _shopPosition        = findObjectPosition( _shop, FarmDirectorComponentInternal::kDefaultShopPosition );
        _playerPosition      = _playerStart;
        _facing              = float3{ 0.0f, 0.0f, 1.0f };
        _stamina             = kMaxStamina;
        _tool                = FarmTool::Hoe;
        _bLoaded             = SW_TRUE;
        return true;
    }

    // ------------------------------------------------------------------------------
    // 스폰(틱 뒤 · 게임 스레드)
    // ------------------------------------------------------------------------------
    void FarmDirectorComponent::scheduleFlush()
    {
        if ( _bFlushScheduled == SW_TRUE )
            return;
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        _bFlushScheduled = SW_TRUE;
        // 틱 안이면 틱 뒤로 미뤄진다. 그 사이에 디렉터가 사라질 수 있으니 핸들로 다시 찾는다.
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            FarmDirectorComponent* pDirector = static_cast<FarmDirectorComponent*>( pManager->resolveComponent( self ) );
            if ( pDirector != nullptr )
                pDirector->flushPending();
        } );
    }

    void FarmDirectorComponent::flushPending()
    {
        _bFlushScheduled            = SW_FALSE;
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr || _bLoaded == SW_FALSE )
            return;
        if ( _bViewsSpawned == SW_FALSE )
        {
            spawnField( *pManager );
            _bViewsSpawned = SW_TRUE;
        }
        for ( const utf8* pPath : _listPendingSound )
            (void)GameSound::play( pPath );
        _listPendingSound.clear();
    }

    GameObject* FarmDirectorComponent::spawnPrefab( GameObjectManager& manager, const string& prefabPath, const utf8* pName )
    {
        AssetManager* pAssetManager = game::getService<AssetManager>();
        if ( pAssetManager == nullptr || prefabPath.empty() )
            return nullptr;
        GameObject* pObject = pAssetManager->getPrefabCache().spawn( &manager, prefabPath, pName );
        if ( pObject != nullptr )
            _listSpawned.push_back( pObject->getHandle() );
        return pObject;
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
            _arrSoilLook[soilState] = acquireColorLook( pSoilMaterial, arrSoilColor[soilState] );
        _plainCropLook    = acquireColorLook( pCropMaterial, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
        _witheredCropLook = acquireColorLook( pCropMaterial, _witheredCropColor );
        _listCropLook.clear();
        for ( const CropDef& crop : _cropCatalog.getCrops() )
        {
            CropLook look;
            look._cropId   = crop._id;
            look._instance = acquireColorLook( pCropMaterial, FarmDirectorComponentInternal::findCropColor( crop._id ) );
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

    shared_ptr<MaterialInstance> FarmDirectorComponent::acquireColorLook( Material* pMaterial, const float4& color )
    {
        if ( pMaterial == nullptr )
            return nullptr;
        for ( const ColorLook& look : _listColorLook )
        {
            const bool bSameColor = look._color._x == color._x && look._color._y == color._y && look._color._z == color._z && look._color._w == color._w;
            if ( bSameColor && look._instance->getParent() == pMaterial )
                return look._instance;
        }
        ColorLook look;
        look._instance = MaterialInstance::create( pMaterial );
        if ( look._instance == nullptr )
            return nullptr;
        look._instance->setVectorParameter( hashed_string( "color" ), color );
        look._color = color;
        _listColorLook.push_back( look );
        return look._instance;
    }

    void FarmDirectorComponent::playSound( const utf8* pPath )
    {
        _listPendingSound.push_back( pPath );
    }

    // ------------------------------------------------------------------------------
    // 입력(PrePhysics — 워커)
    // ------------------------------------------------------------------------------
    void FarmDirectorComponent::updatePlayerInput( float32 deltaTime, const InputManager& input )
    {
        float3 direction{ 0.0f, 0.0f, 0.0f };
        if ( input.isKeyDown( Key::W ) || input.isKeyDown( Key::Up ) )
            direction._z += 1.0f;
        if ( input.isKeyDown( Key::S ) || input.isKeyDown( Key::Down ) )
            direction._z -= 1.0f;
        if ( input.isKeyDown( Key::D ) || input.isKeyDown( Key::Right ) )
            direction._x += 1.0f;
        if ( input.isKeyDown( Key::A ) || input.isKeyDown( Key::Left ) )
            direction._x -= 1.0f;
        if ( direction.getLengthSquared() > 0.0f )
            movePlayer( direction, deltaTime );

        constexpr Key      kArrToolKey[] = { Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4 };
        constexpr FarmTool kArrTool[]    = { FarmTool::Hoe, FarmTool::WateringCan, FarmTool::Seeds, FarmTool::Hand };
        for ( int32 toolIndex = 0; toolIndex < 4; ++toolIndex )
        {
            if ( input.wasKeyPressed( kArrToolKey[toolIndex] ) )
            {
                _tool = kArrTool[toolIndex];
                SW_LOG_INFO( "[Farm] tool: %#", FarmDirectorComponentInternal::toToolName( _tool ) );
                playSound( FarmDirectorComponentInternal::kSoundSelect );
            }
        }
        if ( input.wasKeyPressed( Key::Q ) )
            selectSeed( -1 );
        if ( input.wasKeyPressed( Key::E ) )
            selectSeed( 1 );
        if ( input.wasKeyPressed( Key::Space ) || input.wasKeyPressed( Key::J ) )
            useTool();
        if ( input.wasKeyPressed( Key::F ) )
            (void)shipAllProduce();
        if ( input.wasKeyPressed( Key::B ) )
            (void)buySelectedSeed();
        if ( input.wasKeyPressed( Key::Z ) )
            endDay( false );
        if ( input.wasKeyPressed( Key::Tab ) )
            logStatus( true );
    }

    void FarmDirectorComponent::updateAutoFarmer( float32 deltaTime )
    {
        using Internal = FarmDirectorComponentInternal;
        _autoTimer -= deltaTime;

        // 할 일을 고른다 — 늦었거나 지쳤으면 출하하고 잔다, 거둘 것 → 물 → 심기 → 갈기 → 씨앗 사기.
        FarmAutoTask task;
        const bool   bTired       = _stamina < 8 || _calendar.getHour() >= 22;
        int32        produceCount = 0;
        for ( const CropDef& crop : _cropCatalog.getCrops() )
            produceCount += _inventory.getItemCount( crop._produceItem );

        // 이번 계절 씨앗(가진 것 먼저).
        int32 seasonalSeed = -1;
        for ( int32 seedIndex = 0; seedIndex < static_cast<int32>( _listSeed.size() ); ++seedIndex )
        {
            const CropDef* pCrop = _cropCatalog.findCropBySeed( _listSeed[static_cast<size_t>( seedIndex )] );
            if ( pCrop == nullptr || pCrop->growsIn( _calendar.getSeason() ) == false )
                continue;
            if ( seasonalSeed < 0 || _inventory.getItemCount( pCrop->_seedItem ) > 0 )
                seasonalSeed = seedIndex;
            if ( _inventory.getItemCount( pCrop->_seedItem ) > 0 )
                break;
        }
        if ( seasonalSeed >= 0 )
            _selectedSeedIndex = seasonalSeed;
        const bool bHasSeed = seasonalSeed >= 0 && _inventory.getItemCount( getSelectedSeed() ) > 0;

        if ( produceCount > 0 && ( bTired || produceCount >= 6 ) )
        {
            task._kind          = 2;
            task._standPosition = _shippingBinPosition + float3{ -1.0f, 0.0f, 0.0f };
        }
        else if ( bTired == false )
        {
            int32 cultivatedCount = 0;
            for ( int32 y = 0; y < kFieldHeight; ++y )
            {
                for ( int32 x = 0; x < kFieldWidth; ++x )
                    cultivatedCount += _field.findTile( x, y )->_bTilled != SW_FALSE ? 1 : 0;
            }
            // 칸 순서로 훑어 첫 할 일. 우선순위가 같은 칸이면 앞 칸.
            int32 bestPriority = 0;
            for ( int32 y = 0; y < kFieldHeight; ++y )
            {
                for ( int32 x = 0; x < kFieldWidth; ++x )
                {
                    const FarmTile* pTile    = _field.findTile( x, y );
                    int32           priority = 0;
                    FarmTool        tool     = FarmTool::Hand;
                    if ( pTile->_bReady != SW_FALSE || pTile->_bWithered != SW_FALSE )
                    {
                        priority = 5;
                        tool     = FarmTool::Hand;
                    }
                    else if ( pTile->hasCrop() && pTile->_bWatered == SW_FALSE )
                    {
                        priority = 4;
                        tool     = FarmTool::WateringCan;
                    }
                    else if ( pTile->_bTilled != SW_FALSE && pTile->hasCrop() == false && bHasSeed )
                    {
                        priority = 3;
                        tool     = FarmTool::Seeds;
                    }
                    else if ( pTile->_bTilled == SW_FALSE && cultivatedCount < Internal::kAutoCultivateLimit && seasonalSeed >= 0 )
                    {
                        priority = 2;
                        tool     = FarmTool::Hoe;
                    }
                    if ( priority > bestPriority )
                    {
                        bestPriority        = priority;
                        task._kind          = 1;
                        task._tool          = tool;
                        task._standPosition = computeTileCenter( x, y ) + float3{ 0.0f, 0.0f, -1.0f };
                    }
                }
            }
            // 심을 칸이 있는데 씨앗이 없으면 산다.
            if ( bestPriority < 3 && bHasSeed == false && seasonalSeed >= 0 )
            {
                const CropDef* pCrop = _cropCatalog.findCropBySeed( getSelectedSeed() );
                if ( pCrop != nullptr && _inventory.getGold() >= pCrop->_seedPrice )
                {
                    task._kind          = 3;
                    task._standPosition = _shopPosition + float3{ 1.4f, 0.0f, 0.0f };
                }
            }
        }

        // 밭에 할 일이 없으면 남은 수확물을 넣고, 그것도 없으면 잔다.
        if ( task._kind == 0 && produceCount > 0 )
        {
            task._kind          = 2;
            task._standPosition = _shippingBinPosition + float3{ -1.0f, 0.0f, 0.0f };
        }
        if ( task._kind == 0 )
        {
            endDay( false );
            return;
        }

        const float3  toStand  = task._standPosition - _playerPosition;
        const float32 distance = Internal::computeDistanceXz( task._standPosition, _playerPosition );
        if ( distance > 0.1f )
        {
            movePlayer( float3{ toStand._x, 0.0f, toStand._z }, MathUtil::min( deltaTime, distance / Internal::kWalkSpeed ) );
            return;
        }
        if ( _autoTimer > 0.0f )
            return;
        _autoTimer = Internal::kAutoActionInterval;

        if ( task._kind == 2 )
        {
            (void)shipAllProduce();
            return;
        }
        if ( task._kind == 3 )
        {
            for ( int32 buyIndex = 0; buyIndex < 6; ++buyIndex )
            {
                if ( buySelectedSeed() == false )
                    break;
            }
            return;
        }
        _facing = float3{ 0.0f, 0.0f, 1.0f };
        _tool   = task._tool;
        useTool();
    }

    void FarmDirectorComponent::movePlayer( const float3& direction, float32 deltaTime )
    {
        const float32 length = direction.getLength();
        if ( length < 1.0e-4f )
            return;
        const float3 step  = direction * ( FarmDirectorComponentInternal::kWalkSpeed * deltaTime / length );
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

        FarmActionResult result = FarmActionResult::Done;
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
                if ( _inventory.getItemCount( seed ) <= 0 )
                {
                    SW_LOG_INFO( "[Farm] no %# left - buy more at the shop (B)", seed.c_str() );
                    return;
                }
                result = _field.plant( x, y, seed, _calendar.getSeason() );
                if ( result == FarmActionResult::Done && _inventory.removeItem( seed, 1 ) == false )
                    SW_LOG_WARNING( "[Farm] planted without a seed in the bag" );
                if ( result == FarmActionResult::Done )
                    playSound( Internal::kSoundPlant );
                break;
            }
            case FarmTool::Hand:
            {
                hashed_string produce;
                int32         count = 0;
                result              = _field.harvest( x, y, produce, count );
                if ( result == FarmActionResult::Done && count > 0 )
                {
                    _inventory.addItem( produce, count );
                    SW_LOG_INFO( "[Farm] harvested %# x%#", produce.c_str(), count );
                    playSound( Internal::kSoundHarvest );
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
        int32 shipped = 0;
        for ( const CropDef& crop : _cropCatalog.getCrops() )
        {
            const int32 count = _inventory.getItemCount( crop._produceItem );
            if ( count > 0 && _inventory.shipItem( crop._produceItem, count ) )
                shipped += count;
        }
        if ( shipped > 0 )
        {
            SW_LOG_INFO( "[Farm] shipped %# items - paid tonight", shipped );
            playSound( FarmDirectorComponentInternal::kSoundShip );
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
        if ( _inventory.buyItem( pCrop->_seedItem, 1, pCrop->_seedPrice ) == false )
        {
            SW_LOG_INFO( "[Farm] not enough gold for %# (%#G)", pCrop->_seedItem.c_str(), pCrop->_seedPrice );
            return false;
        }
        SW_LOG_INFO( "[Farm] bought %# (%#G) - %#G left", pCrop->_seedItem.c_str(), pCrop->_seedPrice, _inventory.getGold() );
        return true;
    }

    void FarmDirectorComponent::endDay( bool bPassedOut )
    {
        [[maybe_unused]] const int32 earned        = _inventory.settleShipping( _cropCatalog );
        [[maybe_unused]] const bool  bSeasonChange = _calendar.startNextDay();
        const bool                   bRaining      = _calendar.getSeason() != FarmSeason::Winter && _random.nextChance( FarmDirectorComponentInternal::kRainChance );
        _bRaining                                  = bRaining ? SW_TRUE : SW_FALSE;
        _field.advanceDay( _calendar.getSeason(), bRaining );
        _stamina        = bPassedOut ? kMaxStamina / 2 : kMaxStamina;
        _playerPosition = _playerStart;
        _facing         = float3{ 0.0f, 0.0f, 1.0f };
        SW_LOG_INFO( "[Farm] good morning - %# %#, year %# · shipped for %#G · gold %#G · %#%#", toString( _calendar.getSeason() ), _calendar.getDay(),
                     _calendar.getYear(), earned, _inventory.getGold(), bRaining ? "rain" : "sunny",
                     bSeasonChange ? " · a new season - out-of-season crops withered" : "" );
        _lastLoggedHour = -1;
    }

    void FarmDirectorComponent::selectSeed( int32 offset )
    {
        if ( _listSeed.empty() )
            return;
        const int32 count                     = static_cast<int32>( _listSeed.size() );
        _selectedSeedIndex                    = ( ( _selectedSeedIndex + offset ) % count + count ) % count;
        [[maybe_unused]] const CropDef* pCrop = _cropCatalog.findCropBySeed( getSelectedSeed() );
        SW_LOG_INFO( "[Farm] seed: %# (have %#, %#G)%#", getSelectedSeed().c_str(), _inventory.getItemCount( getSelectedSeed() ),
                     pCrop != nullptr ? pCrop->_seedPrice : 0, pCrop != nullptr && pCrop->growsIn( _calendar.getSeason() ) ? "" : " - not this season" );
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
        pRig->setFocus( fieldCenter + ( _playerPosition - fieldCenter ) * FarmDirectorComponentInternal::kCameraFollow );
    }

    void FarmDirectorComponent::logStatus( bool bForce )
    {
        const int32 hour = _calendar.getHour();
        if ( bForce == false && ( hour == _lastLoggedHour || hour % 6 != 0 ) )
            return;
        _lastLoggedHour                       = hour;
        [[maybe_unused]] const CropDef* pCrop = _cropCatalog.findCropBySeed( getSelectedSeed() );
        SW_LOG_INFO( "[Farm] %# %# Y%# %#:%# · gold %#G · stamina %#/%# · tool %# · seed %# x%# · crops %# (ready %#)", toString( _calendar.getSeason() ),
                     _calendar.getDay(), _calendar.getYear(), hour % 24, _calendar.getMinute(), _inventory.getGold(), _stamina, kMaxStamina,
                     FarmDirectorComponentInternal::toToolName( _tool ), pCrop != nullptr ? pCrop->_name.c_str() : "-", _inventory.getItemCount( getSelectedSeed() ),
                     _field.getCropCount(), _field.getReadyCount() );
    }

    bool FarmDirectorComponent::isNearShippingBin() const
    {
        return FarmDirectorComponentInternal::computeDistanceXz( _playerPosition, _shippingBinPosition ) < FarmDirectorComponentInternal::kNearDistance;
    }

    bool FarmDirectorComponent::isNearShop() const
    {
        return FarmDirectorComponentInternal::computeDistanceXz( _playerPosition, _shopPosition ) < FarmDirectorComponentInternal::kNearDistance + 0.5f;
    }

    bool FarmDirectorComponent::isAutoPlayOn() const
    {
        return _bAutoPlay || GameAutoplay::isOn();
    }

    const hashed_string& FarmDirectorComponent::getSelectedSeed() const
    {
        static const hashed_string kNoSeed{};
        if ( _listSeed.empty() )
            return kNoSeed;
        return _listSeed[static_cast<size_t>( MathUtil::clamp( _selectedSeedIndex, 0, static_cast<int32>( _listSeed.size() ) - 1 ) )];
    }

    GameObjectManager* FarmDirectorComponent::getObjectManager() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getManager() : nullptr;
    }

    float3 FarmDirectorComponent::findObjectPosition( GameObjectHandle handle, const float3& fallback ) const
    {
        GameObjectManager*    pManager = getObjectManager();
        const GameObject*     pObject  = pManager != nullptr ? pManager->resolveGameObject( handle ) : nullptr;
        const SceneComponent* pScene   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
        return pScene != nullptr ? pScene->getWorldPosition() : fallback;
    }
} // namespace sw
