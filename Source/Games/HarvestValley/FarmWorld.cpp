#include "pch.h"

#include "Games/HarvestValley/FarmWorld.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/GameService.h"
#include "GameFramework/Kits/Farming/CropCatalog.h"

namespace sw
{
    SW_LOG_CALLER( "FarmWorld" );

    namespace
    {
        struct FarmWorldInternal
        {
            static constexpr float32 kPi                 = 3.14159265f;
            static constexpr float32 kWalkSpeed          = 4.0f; ///< 농부 걸음(m/s)
            static constexpr float32 kReach              = 0.9f; ///< 바라보는 칸을 고르는 거리(m)
            static constexpr float32 kNearDistance       = 1.7f; ///< 출하함 · 가게 옆으로 치는 거리(m)
            static constexpr float32 kAutoActionInterval = 0.25f;
            static constexpr float32 kRainChance         = 0.25f;
            static constexpr int32   kAutoCultivateLimit = 32; ///< 자동 농부가 가꾸는 칸 수(체력이 하루에 감당하는 만큼)

            static constexpr float3 kHousePosition{ 6.0f, 0.0f, -3.5f };
            static constexpr float3 kShippingBinPosition{ 13.4f, 0.0f, 1.0f };
            static constexpr float3 kShopPosition{ -1.6f, 0.0f, 5.0f };
            static constexpr float3 kPlayerStart{ 6.0f, 0.0f, -1.2f };

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

            static float32 getDistanceXz( const float3& lhs, const float3& rhs )
            {
                const float32 dx = lhs._x - rhs._x;
                const float32 dz = lhs._z - rhs._z;
                return MathUtil::sqrt( dx * dx + dz * dz );
            }

            static float3 getTileCenter( int32 x, int32 y ) { return float3{ static_cast<float32>( x ) + 0.5f, 0.0f, static_cast<float32>( y ) + 0.5f }; }
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
     * @brief `-gv_farmAutoPlay=1` — 농부도 AI 가 움직입니다(갈기 → 심기 → 물 → 거두기 → 출하 → 잠). 입력 없이 계절을 넘겨 보는 확인용입니다.
     * @details 배포본으로도 돌린다: `App -gv_farmAutoPlay=1 -gv_profileFrames=36000`(약 10 분 = 하루 다섯).
     */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_farmAutoPlay, 0, "HarvestValley: 농부도 AI 가 조종 (1=켜기)", SW_KEEP_IN_SHIPPING );
} // namespace sw

namespace sw
{
    FarmWorld::FarmWorld()
        : _stage{}
        , _calendar{}
        , _field{}
        , _inventory{}
        , _listTileView{}
        , _listSeed{}
        , _pCatalog{ nullptr }
        , _playerPosition{ FarmWorldInternal::kPlayerStart }
        , _facing{ 0.0f, 0.0f, 1.0f }
        , _player{}
        , _marker{}
        , _sun{}
        , _autoTimer{ 0.0f }
        , _stamina{ kMaxStamina }
        , _selectedSeedIndex{ 0 }
        , _lastLoggedHour{ -1 }
        , _randomState{ 0x2545f491u }
        , _tool{ FarmTool::Hoe }
        , _bRaining{ SW_FALSE }
        , _bSpawned{ SW_FALSE }
    {
    }

    FarmWorld::~FarmWorld() = default;

    void FarmWorld::initialize( const CropCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _calendar.reset();
        _field.initialize( kFieldWidth, kFieldHeight, pCatalog );
        _inventory = FarmInventory{};
        _inventory.addGold( kStartingGold );
        _listSeed.clear();
        if ( pCatalog != nullptr )
        {
            for ( const CropDef& crop : pCatalog->getCrops() )
                _listSeed.push_back( crop._seedItem );
            if ( pCatalog->getCrops().empty() == false )
                _inventory.addItem( pCatalog->getCrops().front()._seedItem, 10 );
        }
        _playerPosition = FarmWorldInternal::kPlayerStart;
        _stamina        = kMaxStamina;
        _tool           = FarmTool::Hoe;
    }

    bool FarmWorld::spawn()
    {
        if ( _bSpawned != SW_FALSE )
            return true;
        if ( _stage.begin( "HarvestValley" ) == false )
            return false;

        // 땅 · 집 · 출하함 · 가게.
        (void)_stage.createPrimitiveObject( "FarmMeadow", "Plane", PrimitiveLook::makeColor( float4{ 0.42f, 0.62f, 0.30f, 1.0f } ), float3{ 6.0f, -0.01f, 3.0f },
                                            float3{ 34.0f, 1.0f, 26.0f } );
        (void)_stage.createPrimitiveObject( "FarmHouse", "Cube", PrimitiveLook::makeColor( float4{ 0.85f, 0.75f, 0.60f, 1.0f } ),
                                            FarmWorldInternal::kHousePosition + float3{ 0.0f, 1.25f, 0.0f }, float3{ 4.0f, 2.5f, 3.0f } );
        (void)_stage.createPrimitiveObject( "FarmRoof", "Cone", PrimitiveLook::makeColor( float4{ 0.70f, 0.22f, 0.18f, 1.0f } ),
                                            FarmWorldInternal::kHousePosition + float3{ 0.0f, 3.2f, 0.0f }, float3{ 5.0f, 1.4f, 4.0f } );
        (void)_stage.createPrimitiveObject( "FarmShippingBin", "Cube", PrimitiveLook::makeColor( float4{ 0.55f, 0.35f, 0.18f, 1.0f } ),
                                            FarmWorldInternal::kShippingBinPosition + float3{ 0.0f, 0.4f, 0.0f }, float3{ 1.2f, 0.8f, 1.0f } );
        (void)_stage.createPrimitiveObject( "FarmShop", "Cube", PrimitiveLook::makeColor( float4{ 0.30f, 0.45f, 0.85f, 1.0f } ),
                                            FarmWorldInternal::kShopPosition + float3{ 0.0f, 0.75f, 0.0f }, float3{ 2.0f, 1.5f, 2.0f } );

        // 칸마다 흙 · 작물. 모습은 refreshViews 가 상태에 맞춰 칠한다.
        _listTileView.assign( static_cast<size_t>( kFieldWidth * kFieldHeight ), TileView{} );
        for ( int32 y = 0; y < kFieldHeight; ++y )
        {
            for ( int32 x = 0; x < kFieldWidth; ++x )
            {
                TileView&    view   = _listTileView[static_cast<size_t>( y * kFieldWidth + x )];
                const float3 center = FarmWorldInternal::getTileCenter( x, y );
                GameObject*  pSoil  = _stage.createPrimitiveObject( "FarmSoil", "Cube", PrimitiveLook{}, center + float3{ 0.0f, 0.04f, 0.0f }, float3{ 0.96f, 0.08f, 0.96f } );
                GameObject*  pCrop  = _stage.createPrimitiveObject( "FarmCrop", "Sphere", PrimitiveLook{}, center + float3{ 0.0f, 0.2f, 0.0f }, float3{ 0.2f } );
                view._soil          = pSoil != nullptr ? pSoil->getHandle() : GameObjectHandle{};
                view._crop          = pCrop != nullptr ? pCrop->getHandle() : GameObjectHandle{};
            }
        }

        GameObject* pPlayer = _stage.createPrimitiveObject( "Farmer", "Capsule", PrimitiveLook::makeColor( float4{ 0.25f, 0.45f, 0.95f, 1.0f } ), _playerPosition,
                                                            float3{ 0.5f } );
        GameObject* pMarker = _stage.createPrimitiveObject( "FarmTarget", "Cube", PrimitiveLook::makeTranslucent( float4{ 1.0f, 0.95f, 0.3f, 0.45f } ),
                                                            float3{ 0.0f, 0.1f, 0.0f }, float3{ 1.0f, 0.04f, 1.0f } );
        GameObject* pSun    = _stage.createSun( float3{ 0.9f, 0.6f, 0.0f }, 1.4f, 20.0f );
        _player             = pPlayer != nullptr ? pPlayer->getHandle() : GameObjectHandle{};
        _marker             = pMarker != nullptr ? pMarker->getHandle() : GameObjectHandle{};
        _sun                = pSun != nullptr ? pSun->getHandle() : GameObjectHandle{};
        _bSpawned           = SW_TRUE;

        refreshViews();
        updateCameraAndSun();
        SW_LOG_INFO( "[Farm] farm is ready - WASD move, Space use tool, 1-4 tool (hoe/can/seeds/hand), Q/E seed, B buy, F ship, Z sleep" );
        logStatus( true );
        return true;
    }

    void FarmWorld::despawn()
    {
        _stage.clear();
        _listTileView.clear();
        _bSpawned = SW_FALSE;
    }

    void FarmWorld::update( float32 deltaTime )
    {
        if ( _stage.isSceneChanged() )
        {
            _stage.forget();
            _listTileView.clear();
            _bSpawned = SW_FALSE;
        }
        if ( _bSpawned == SW_FALSE && spawn() == false )
            return;
        if ( deltaTime <= 0.0f )
            return;

        const InputManager* pInput = game::getService<InputManager>();
        if ( gv_farmAutoPlay != 0 || pInput == nullptr )
            updateAutoFarmer( deltaTime );
        else
            updatePlayerInput( deltaTime, *pInput );

        // 시간 — 26:00 이 되면 그 자리에서 쓰러져 다음 날 아침이다(체력 반).
        if ( _calendar.advanceMinutes( deltaTime * kMinutesPerSecond ) )
        {
            SW_LOG_INFO( "[Farm] it is 2 AM - the farmer passed out" );
            endDay( true );
        }
        refreshViews();
        updateCameraAndSun();
        logStatus( false );
    }

    // ------------------------------------------------------------------------------
    // 입력
    // ------------------------------------------------------------------------------
    void FarmWorld::updatePlayerInput( float32 deltaTime, const InputManager& input )
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
                SW_LOG_INFO( "[Farm] tool: %#", FarmWorldInternal::toToolName( _tool ) );
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

    void FarmWorld::updateAutoFarmer( float32 deltaTime )
    {
        if ( _pCatalog == nullptr )
            return;
        _autoTimer -= deltaTime;

        // 할 일을 고른다 — 늦었거나 지쳤으면 출하하고 잔다, 거둘 것 → 물 → 심기 → 갈기 → 씨앗 사기.
        FarmAutoTask task;
        const bool   bTired       = _stamina < 8 || _calendar.getHour() >= 22;
        int32        produceCount = 0;
        for ( const CropDef& crop : _pCatalog->getCrops() )
            produceCount += _inventory.getItemCount( crop._produceItem );

        // 이번 계절 씨앗(가진 것 먼저).
        int32 seasonalSeed = -1;
        for ( int32 seedIndex = 0; seedIndex < static_cast<int32>( _listSeed.size() ); ++seedIndex )
        {
            const CropDef* pCrop = _pCatalog->findCropBySeed( _listSeed[static_cast<size_t>( seedIndex )] );
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
            task._standPosition = FarmWorldInternal::kShippingBinPosition + float3{ -1.0f, 0.0f, 0.0f };
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
                    else if ( pTile->_bTilled == SW_FALSE && cultivatedCount < FarmWorldInternal::kAutoCultivateLimit && seasonalSeed >= 0 )
                    {
                        priority = 2;
                        tool     = FarmTool::Hoe;
                    }
                    if ( priority > bestPriority )
                    {
                        bestPriority        = priority;
                        task._kind          = 1;
                        task._tool          = tool;
                        task._standPosition = FarmWorldInternal::getTileCenter( x, y ) + float3{ 0.0f, 0.0f, -1.0f };
                    }
                }
            }
            // 심을 칸이 있는데 씨앗이 없으면 산다.
            if ( bestPriority < 3 && bHasSeed == false && seasonalSeed >= 0 )
            {
                const CropDef* pCrop = _pCatalog->findCropBySeed( getSelectedSeed() );
                if ( pCrop != nullptr && _inventory.getGold() >= pCrop->_seedPrice )
                {
                    task._kind          = 3;
                    task._standPosition = FarmWorldInternal::kShopPosition + float3{ 1.4f, 0.0f, 0.0f };
                }
            }
        }

        // 밭에 할 일이 없으면 남은 수확물을 넣고, 그것도 없으면 잔다.
        if ( task._kind == 0 && produceCount > 0 )
        {
            task._kind          = 2;
            task._standPosition = FarmWorldInternal::kShippingBinPosition + float3{ -1.0f, 0.0f, 0.0f };
        }
        if ( task._kind == 0 )
        {
            endDay( false );
            return;
        }

        const float3  toStand  = task._standPosition - _playerPosition;
        const float32 distance = FarmWorldInternal::getDistanceXz( task._standPosition, _playerPosition );
        if ( distance > 0.1f )
        {
            movePlayer( float3{ toStand._x, 0.0f, toStand._z }, MathUtil::min( deltaTime, distance / FarmWorldInternal::kWalkSpeed ) );
            return;
        }
        if ( _autoTimer > 0.0f )
            return;
        _autoTimer = FarmWorldInternal::kAutoActionInterval;

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

    void FarmWorld::movePlayer( const float3& direction, float32 deltaTime )
    {
        const float32 length = direction.getLength();
        if ( length < 1.0e-4f )
            return;
        const float3 step  = direction * ( FarmWorldInternal::kWalkSpeed * deltaTime / length );
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
    void FarmWorld::useTool()
    {
        int32 x = 0;
        int32 y = 0;
        if ( findTargetTile( x, y ) == false )
            return;
        const int32 cost = FarmWorldInternal::getToolStaminaCost( _tool );
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
                }
                break;
            }
        }
        if ( result == FarmActionResult::Done )
            _stamina -= cost;
        else if ( gv_farmAutoPlay == 0 )
            SW_LOG_INFO( "[Farm] %# at (%#, %#): %#", FarmWorldInternal::toToolName( _tool ), x, y, toString( result ) );
    }

    int32 FarmWorld::shipAllProduce()
    {
        if ( isNearShippingBin() == false || _pCatalog == nullptr )
        {
            SW_LOG_INFO( "[Farm] stand next to the shipping bin to ship" );
            return 0;
        }
        int32 shipped = 0;
        for ( const CropDef& crop : _pCatalog->getCrops() )
        {
            const int32 count = _inventory.getItemCount( crop._produceItem );
            if ( count > 0 && _inventory.shipItem( crop._produceItem, count ) )
                shipped += count;
        }
        if ( shipped > 0 )
            SW_LOG_INFO( "[Farm] shipped %# items - paid tonight", shipped );
        return shipped;
    }

    bool FarmWorld::buySelectedSeed()
    {
        if ( isNearShop() == false || _pCatalog == nullptr )
        {
            SW_LOG_INFO( "[Farm] stand next to the shop to buy seeds" );
            return false;
        }
        const CropDef* pCrop = _pCatalog->findCropBySeed( getSelectedSeed() );
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

    void FarmWorld::endDay( bool bPassedOut )
    {
        [[maybe_unused]] const int32 earned        = _pCatalog != nullptr ? _inventory.settleShipping( *_pCatalog ) : 0;
        [[maybe_unused]] const bool  bSeasonChange = _calendar.startNextDay();
        _bRaining                                  = ( _calendar.getSeason() != FarmSeason::Winter && nextRandom() < FarmWorldInternal::kRainChance ) ? SW_TRUE : SW_FALSE;
        _field.advanceDay( _calendar.getSeason(), _bRaining != SW_FALSE );
        _stamina        = bPassedOut ? kMaxStamina / 2 : kMaxStamina;
        _playerPosition = FarmWorldInternal::kPlayerStart;
        _facing         = float3{ 0.0f, 0.0f, 1.0f };
        SW_LOG_INFO( "[Farm] good morning - %# %#, year %# · shipped for %#G · gold %#G · %#%#", toString( _calendar.getSeason() ), _calendar.getDay(),
                     _calendar.getYear(), earned, _inventory.getGold(), _bRaining != SW_FALSE ? "rain" : "sunny",
                     bSeasonChange ? " · a new season - out-of-season crops withered" : "" );
        _lastLoggedHour = -1;
    }

    void FarmWorld::selectSeed( int32 offset )
    {
        if ( _listSeed.empty() )
            return;
        const int32 count                     = static_cast<int32>( _listSeed.size() );
        _selectedSeedIndex                    = ( ( _selectedSeedIndex + offset ) % count + count ) % count;
        [[maybe_unused]] const CropDef* pCrop = _pCatalog != nullptr ? _pCatalog->findCropBySeed( getSelectedSeed() ) : nullptr;
        SW_LOG_INFO( "[Farm] seed: %# (have %#, %#G)%#", getSelectedSeed().c_str(), _inventory.getItemCount( getSelectedSeed() ),
                     pCrop != nullptr ? pCrop->_seedPrice : 0, pCrop != nullptr && pCrop->growsIn( _calendar.getSeason() ) ? "" : " - not this season" );
    }

    bool FarmWorld::isNearShippingBin() const
    {
        return FarmWorldInternal::getDistanceXz( _playerPosition, FarmWorldInternal::kShippingBinPosition ) < FarmWorldInternal::kNearDistance;
    }

    bool FarmWorld::isNearShop() const
    {
        return FarmWorldInternal::getDistanceXz( _playerPosition, FarmWorldInternal::kShopPosition ) < FarmWorldInternal::kNearDistance + 0.5f;
    }

    bool FarmWorld::findTargetTile( int32& outX, int32& outY ) const
    {
        const float3 target = _playerPosition + _facing * FarmWorldInternal::kReach;
        outX                = static_cast<int32>( MathUtil::floor( target._x ) );
        outY                = static_cast<int32>( MathUtil::floor( target._z ) );
        return _field.findTile( outX, outY ) != nullptr;
    }

    const hashed_string& FarmWorld::getSelectedSeed() const
    {
        static const hashed_string kNoSeed{};
        if ( _listSeed.empty() )
            return kNoSeed;
        return _listSeed[static_cast<size_t>( MathUtil::clamp( _selectedSeedIndex, 0, static_cast<int32>( _listSeed.size() ) - 1 ) )];
    }

    // ------------------------------------------------------------------------------
    // 모습
    // ------------------------------------------------------------------------------
    void FarmWorld::refreshViews()
    {
        for ( int32 y = 0; y < kFieldHeight && _listTileView.empty() == false; ++y )
        {
            for ( int32 x = 0; x < kFieldWidth; ++x )
            {
                TileView&       view      = _listTileView[static_cast<size_t>( y * kFieldWidth + x )];
                const FarmTile* pTile     = _field.findTile( x, y );
                const int32     soilState = pTile->_bTilled == SW_FALSE ? 0 : ( pTile->_bWatered != SW_FALSE ? 2 : 1 );
                if ( soilState != view._soilState )
                {
                    MeshComponent* pSoil = _stage.findMesh( view._soil );
                    if ( pSoil != nullptr )
                    {
                        constexpr float4 kArrSoilColor[3] = {
                            {0.40f, 0.62f, 0.28f, 1.0f},
                            {0.58f, 0.40f, 0.24f, 1.0f},
                            {0.33f, 0.22f, 0.13f, 1.0f}
                        };
                        _stage.setLook( *pSoil, PrimitiveLook::makeColor( kArrSoilColor[soilState] ) );
                    }
                    view._soilState = soilState;
                }

                // 작물 — 단계(0..4) · 다 자람 · 시듦이 바뀔 때만 다시 칠한다.
                const float32 ratio     = _field.computeGrowthRatio( x, y );
                const int32   stage     = static_cast<int32>( ratio * 4.0f );
                const int32   cropState = pTile->hasCrop() == false ? 0
                                                                    : 1 + stage * 4 + ( pTile->_bReady != SW_FALSE ? 1 : 0 ) + ( pTile->_bWithered != SW_FALSE ? 2 : 0 ) +
                                                                        static_cast<int32>( pTile->_cropId.getHash() % 997u ) * 32;
                if ( cropState == view._cropState )
                    continue;
                view._cropState      = cropState;
                MeshComponent* pCrop = _stage.findMesh( view._crop );
                if ( pCrop == nullptr )
                    continue;
                pCrop->setVisible( pTile->hasCrop() );
                if ( pTile->hasCrop() == false )
                    continue;
                float4  color = float4{ 0.35f, 0.75f, 0.30f, 1.0f };
                float32 size  = 0.18f + 0.35f * ratio;
                if ( pTile->_bWithered != SW_FALSE )
                    color = float4{ 0.45f, 0.35f, 0.20f, 1.0f };
                else if ( pTile->_bReady != SW_FALSE )
                {
                    color = FarmWorldInternal::findCropColor( pTile->_cropId );
                    size  = 0.6f;
                }
                _stage.setLook( *pCrop, PrimitiveLook::makeColor( color ) );
                pCrop->setLocalScale( float3{ size } );
                pCrop->setLocalPosition( FarmWorldInternal::getTileCenter( x, y ) + float3{ 0.0f, 0.08f + size * 0.5f, 0.0f } );
            }
        }

        if ( MeshComponent* pPlayer = _stage.findMesh( _player ) )
            pPlayer->setLocalPosition( _playerPosition + float3{ 0.0f, 0.5f, 0.0f } );
        if ( MeshComponent* pMarker = _stage.findMesh( _marker ) )
        {
            int32      x       = 0;
            int32      y       = 0;
            const bool bOnTile = findTargetTile( x, y );
            pMarker->setVisible( bOnTile );
            if ( bOnTile )
                pMarker->setLocalPosition( FarmWorldInternal::getTileCenter( x, y ) + float3{ 0.0f, 0.1f, 0.0f } );
        }
    }

    void FarmWorld::updateCameraAndSun()
    {
        // 비스듬히 내려다보는 직교 시점 — 밭 가운데와 농부 사이를 본다.
        const float3 fieldCenter{ static_cast<float32>( kFieldWidth ) * 0.5f, 0.0f, static_cast<float32>( kFieldHeight ) * 0.5f };
        const float3 focus = fieldCenter + ( _playerPosition - fieldCenter ) * 0.4f;
        _stage.placeCameras( focus + float3{ 0.0f, 16.0f, -12.0f }, focus, 15.0f, 80.0f );

        // 해 — 6 시에 동쪽 낮게, 정오에 높게, 20 시 넘으면 어둡다. 비 오는 날은 반.
        GameObject*                pSunObject = _stage.resolveObject( _sun );
        DirectionalLightComponent* pSun       = pSunObject != nullptr ? pSunObject->getComponent<DirectionalLightComponent>() : nullptr;
        if ( pSun == nullptr )
            return;
        const float32 hour     = _calendar.getMinuteOfDay() / 60.0f;
        const float32 dayRatio = MathUtil::clamp( ( hour - 6.0f ) / 14.0f, 0.0f, 1.0f );
        const float32 height   = MathUtil::sin( dayRatio * FarmWorldInternal::kPi );
        const float32 light    = ( 0.25f + 1.25f * height ) * ( _bRaining != SW_FALSE ? 0.55f : 1.0f );
        pSun->setIntensity( hour > 20.0f ? 0.2f : light );
        pSun->setLocalRotation( float3{ 0.25f + 1.1f * height, -1.2f + 2.4f * dayRatio, 0.0f } );
    }

    void FarmWorld::logStatus( bool bForce )
    {
        const int32 hour = _calendar.getHour();
        if ( bForce == false && ( hour == _lastLoggedHour || hour % 6 != 0 ) )
            return;
        _lastLoggedHour                       = hour;
        [[maybe_unused]] const CropDef* pCrop = _pCatalog != nullptr ? _pCatalog->findCropBySeed( getSelectedSeed() ) : nullptr;
        SW_LOG_INFO( "[Farm] %# %# Y%# %#:%# · gold %#G · stamina %#/%# · tool %# · seed %# x%# · crops %# (ready %#)", toString( _calendar.getSeason() ),
                     _calendar.getDay(), _calendar.getYear(), hour % 24, _calendar.getMinute(), _inventory.getGold(), _stamina, kMaxStamina,
                     FarmWorldInternal::toToolName( _tool ), pCrop != nullptr ? pCrop->_name.c_str() : "-", _inventory.getItemCount( getSelectedSeed() ),
                     _field.getCropCount(), _field.getReadyCount() );
    }

    float32 FarmWorld::nextRandom()
    {
        _randomState ^= _randomState << 13;
        _randomState ^= _randomState >> 17;
        _randomState ^= _randomState << 5;
        return static_cast<float32>( _randomState & 0xffffffu ) / static_cast<float32>( 0x1000000u );
    }
} // namespace sw
