#include "pch.h"

#include "Games/ThemeParkTycoon/ParkWorld.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameSound.h"
#include "GameFramework/Utility/OrientationUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ParkWorld" );

    namespace
    {
        struct ParkWorldInternal
        {
            static constexpr float32 kPi                = 3.14159265f;
            static constexpr float32 kDegToRad          = kPi / 180.0f;
            static constexpr float32 kRailSpacing       = 2.0f; ///< 레일 조각 간격(m)
            static constexpr float32 kSupportSpacing    = 8.0f; ///< 기둥 간격(m)
            static constexpr float32 kCarSpacing        = 2.4f;
            static constexpr uint32  kCarCount          = 4;
            static constexpr float32 kCameraPitch       = 30.0f * kDegToRad; ///< 아이소메트릭(다이메트릭) 내려다보는 각
            static constexpr float32 kCameraDistance    = 250.0f;
            static constexpr float32 kPanSpeed          = 40.0f;
            static constexpr float32 kAutoBuildInterval = 20.0f;
            /**
             * @brief Kenney Coaster Kit 모델의 배율입니다. 키트는 열차 폭 0.7 이고 이 공원의 차 폭은 1.4 다.
             * @details 레일 조각은 시작점에서 +Z 로 4 만큼 뻗고, 레일 윗면이 원점보다 0.7 아래다(열차 바닥이 원점). 그래서 레일은 트랙 점에서
             *          `kModelScale × 0.7` 만큼 올려 윗면을 트랙 점에 맞추고, 차는 바닥을 트랙 점에 둔다. 팔레트 텍스처라 늘려도 색이 번지지 않는다.
             */
            static constexpr float32     kModelScale      = 2.0f;
            static constexpr float32     kRailPieceLength = 4.0f;
            static constexpr float32     kRailTopDepth    = 0.7f;
            static constexpr float32     kRailBottomDepth = 1.0f;
            static constexpr const utf8* kPaletteTexture  = "game/themepark/textures/coaster_colormap.dds";

            static PrimitiveLook makeModelLook()
            {
                PrimitiveLook look{};
                look._texturePath = kPaletteTexture;
                return look;
            }

            static string makeModelPath( const utf8* pName )
            {
                return string( "game/themepark/models/" ) + pName + ".mesh";
            }

            /** @brief 행복도 → 색 칸(0 초록 · 1 노랑 · 2 빨강)입니다. */
            static int32 findHappinessBucket( float32 happiness )
            {
                if ( happiness >= 0.6f )
                    return 0;
                return happiness >= 0.35f ? 1 : 2;
            }
        };
    } // namespace

    /** @brief `-gv_parkAutoBuild=1` — 돈이 모이는 대로 남은 놀이기구 중 가장 싼 것을 짓습니다(입력 없이 공원이 크는 확인). */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_parkAutoBuild, 0, "ThemeParkTycoon: 돈이 모이면 자동으로 짓기 (1=켜기)", SW_KEEP_IN_SHIPPING );
} // namespace sw

namespace sw
{
    ParkWorld::ParkWorld()
        : _stage{}
        , _simulation{}
        , _layoutCatalog{}
        , _listPlacement{}
        , _listCoasterView{}
        , _listFlatView{}
        , _listGuestView{}
        , _settings{}
        , _cameraFocus{ 20.0f, 0.0f, 20.0f }
        , _cameraYaw{ 45.0f * ParkWorldInternal::kDegToRad }
        , _cameraHeight{ 110.0f }
        , _statusTimer{ 0.0f }
        , _autoBuildTimer{ 0.0f }
        , _startingCash{ 12000 }
        , _selectedPlacement{ 0 }
        , _ridingCoaster{ -1 }
        , _bLoaded{ SW_FALSE }
        , _bSpawned{ SW_FALSE }
    {
    }

    ParkWorld::~ParkWorld() = default;

    bool ParkWorld::loadData( string_view coasterPath, string_view parkPath )
    {
        if ( _layoutCatalog.loadFromResource( coasterPath ) == false || loadParkLayout( parkPath ) == false )
            return false;
        _simulation.initialize( _settings, _startingCash );
        _bLoaded = SW_TRUE;

        // 처음 공원 — 가장 싼 평지 놀이기구 하나와 가장 싼 코스터 하나.
        int32 cheapestFlat    = -1;
        int32 cheapestCoaster = -1;
        for ( int32 placementIndex = 0; placementIndex < static_cast<int32>( _listPlacement.size() ); ++placementIndex )
        {
            const RidePlacement& placement = _listPlacement[static_cast<size_t>( placementIndex )];
            int32&               cheapest  = placement._layoutId.empty() ? cheapestFlat : cheapestCoaster;
            if ( cheapest < 0 || placement._buildCost < _listPlacement[static_cast<size_t>( cheapest )]._buildCost )
                cheapest = placementIndex;
        }
        if ( cheapestFlat >= 0 )
            (void)buildPlacement( cheapestFlat );
        if ( cheapestCoaster >= 0 )
            (void)buildPlacement( cheapestCoaster );
        return true;
    }

    bool ParkWorld::loadParkLayout( string_view parkPath )
    {
        ParkLayout layout;
        if ( layout.loadFromResource( parkPath, _layoutCatalog ) == false )
            return false;
        _settings._gatePosition = layout.getGatePosition();
        _settings._entryFee     = layout.getEntryFee();
        _startingCash           = layout.getStartingCash();
        _listPlacement.clear();
        _listPlacement.reserve( layout.getPlacements().size() );
        for ( const ParkRidePlacement& placement : layout.getPlacements() )
        {
            RidePlacement ride{};
            static_cast<ParkRidePlacement&>( ride ) = placement;
            _listPlacement.push_back( ride );
        }
        return _listPlacement.empty() == false;
    }

    bool ParkWorld::spawn()
    {
        if ( _bSpawned != SW_FALSE )
            return true;
        if ( _bLoaded == SW_FALSE || _stage.begin( "ThemeParkTycoon" ) == false )
            return false;

        (void)_stage.createPrimitiveObject( "ParkGrass", "Plane", PrimitiveLook::makeColor( float4{ 0.38f, 0.62f, 0.32f, 1.0f } ), float3{ 20.0f, -0.02f, 110.0f },
                                            float3{ 260.0f, 1.0f, 340.0f } );
        const PrimitiveLook modelLook = ParkWorldInternal::makeModelLook();
        (void)_stage.createModelObject( "ParkGate", ParkWorldInternal::makeModelPath( "park_entrance" ), modelLook, _settings._gatePosition,
                                        float3{ 2.6f, 2.6f, 2.6f } );
        spawnDecoration();
        (void)_stage.createSun( float3{ 0.9f, 0.8f, 0.0f }, 1.5f, 180.0f );

        _listCoasterView.clear();
        _listFlatView.clear();
        for ( int32 placementIndex = 0; placementIndex < static_cast<int32>( _listPlacement.size() ); ++placementIndex )
        {
            if ( _listPlacement[static_cast<size_t>( placementIndex )]._rideIndex >= 0 )
                spawnRideView( placementIndex );
        }

        // 손님 — 최대 손님 수만큼 캡슐을 미리 세우고 숨긴다(오브젝트를 매 프레임 만들고 지우지 않는다).
        _listGuestView.assign( static_cast<size_t>( _settings._maxGuests ), GuestView{} );
        for ( GuestView& view : _listGuestView )
        {
            GameObject* pGuest = _stage.createPrimitiveObject( "ParkGuest", "Capsule", PrimitiveLook::makeColor( float4{ 0.3f, 0.85f, 0.35f, 1.0f } ),
                                                               _settings._gatePosition, float3{ 0.5f, 0.45f, 0.5f } );
            if ( pGuest == nullptr )
                continue;
            view._object = pGuest->getHandle();
            if ( MeshComponent* pMesh = pGuest->getComponent<MeshComponent>() )
                pMesh->setVisible( false );
        }
        _bSpawned = SW_TRUE;
        updateCamera();
        SW_LOG_INFO( "[Park] park is open - WASD pan, Q/E rotate, wheel zoom, Tab select ride, O open/close, [ ] ride price, - = entry fee, "
                     "B build next, V ride the coaster, G guest thoughts" );
        logStatus( 0.0f, true );
        return true;
    }

    void ParkWorld::despawn()
    {
        _stage.clear();
        _listCoasterView.clear();
        _listFlatView.clear();
        _listGuestView.clear();
        _bSpawned = SW_FALSE;
    }

    void ParkWorld::update( float32 deltaTime )
    {
        if ( _stage.isSceneChanged() )
        {
            _stage.forget();
            _listCoasterView.clear();
            _listFlatView.clear();
            _listGuestView.clear();
            _bSpawned = SW_FALSE;
        }
        if ( _bSpawned == SW_FALSE && spawn() == false )
            return;
        if ( deltaTime <= 0.0f )
            return;

        _simulation.update( deltaTime );
        const InputManager* pInput = game::getService<InputManager>();
        if ( pInput != nullptr )
            updateInput( deltaTime, *pInput );
        if ( gv_parkAutoBuild != 0 )
        {
            _autoBuildTimer += deltaTime;
            if ( _autoBuildTimer >= ParkWorldInternal::kAutoBuildInterval )
            {
                _autoBuildTimer = 0.0f;
                (void)buildCheapestRemaining();
            }
        }
        updateRides( deltaTime );
        updateGuests();
        updateCamera();
        logStatus( deltaTime, false );
    }

    // ------------------------------------------------------------------------------
    // 짓기
    // ------------------------------------------------------------------------------
    bool ParkWorld::buildPlacement( int32 placementIndex )
    {
        if ( placementIndex < 0 || placementIndex >= static_cast<int32>( _listPlacement.size() ) )
            return false;
        RidePlacement& placement = _listPlacement[static_cast<size_t>( placementIndex )];
        if ( placement._rideIndex >= 0 )
            return false;
        if ( _simulation.getCash() < placement._buildCost )
        {
            SW_LOG_INFO( "[Park] not enough cash for %# ($%# needed, $%# in the bank)", placement._ride._name.c_str(), placement._buildCost, _simulation.getCash() );
            (void)GameSound::play( "game/themepark/sounds/error_001.ogg" );
            return false;
        }

        ParkRide ride = placement._ride;
        if ( placement._layoutId.empty() == false )
        {
            // 코스터 — 짓고 시험 운행으로 평가를 받는다. 못 돌면(언덕을 못 넘는다) 짓지 않는다.
            const CoasterLayoutDef* pLayout = _layoutCatalog.findLayout( placement._layoutId );
            if ( pLayout == nullptr )
                return false;
            CoasterTrackBuilder builder;
            builder.reset( placement._position + float3{ 0.0f, pLayout->_startHeight, 0.0f }, placement._heading );
            builder.appendPieces( pLayout->_listPiece );
            const CoasterTrack     track = builder.makeTrack( true );
            const CoasterRideStats stats = CoasterRideAnalyzer::analyze( track, CoasterPhysicsParams{} );
            if ( stats._bCompleted == SW_FALSE )
            {
                SW_LOG_WARNING( "[Park] %# failed its test run (stalled) - not built", pLayout->_name.c_str() );
                return false;
            }
            ride = ThemeParkSimulation::makeRideFromCoaster( placement._layoutId, pLayout->_name, stats, placement._ride._capacity, placement._loadTime );
            // 줄 입구 — 스테이션 가운데 옆(바깥쪽).
            const CoasterTrackFrame stationFrame = track.sample( 6.0f );
            ride._entrance                       = stationFrame._position - stationFrame._right * 5.0f;
            ride._entrance._y                    = 0.0f;
            ride._runningCostPerMinute           = 8 + static_cast<int32>( stats._length / 60.0f );
            SW_LOG_INFO( "[Park] test run of %#: %# m, %# s lap, top speed %# m/s, %# inversions, airtime %# s -> excitement %# · intensity %# · nausea %#",
                         pLayout->_name.c_str(), static_cast<int32>( stats._length ), static_cast<int32>( stats._lapTime ), static_cast<int32>( stats._maxSpeed ),
                         stats._inversionCount, stats._airtime, stats._excitement, stats._intensity, stats._nausea );
        }

        const int32 rideIndex = _simulation.buildRide( ride, placement._buildCost );
        if ( rideIndex < 0 )
            return false;
        placement._rideIndex = rideIndex;
        placement._ride      = ride;
        SW_LOG_INFO( "[Park] built %# for $%# - ticket $%#, $%# left", ride._name.c_str(), placement._buildCost, ride._price, _simulation.getCash() );
        (void)GameSound::play( "game/themepark/sounds/confirmation_001.ogg" );
        if ( _bSpawned != SW_FALSE )
            spawnRideView( placementIndex );
        return true;
    }

    bool ParkWorld::buildCheapestRemaining()
    {
        int32 cheapest = -1;
        for ( int32 placementIndex = 0; placementIndex < static_cast<int32>( _listPlacement.size() ); ++placementIndex )
        {
            const RidePlacement& placement = _listPlacement[static_cast<size_t>( placementIndex )];
            if ( placement._rideIndex < 0 && ( cheapest < 0 || placement._buildCost < _listPlacement[static_cast<size_t>( cheapest )]._buildCost ) )
                cheapest = placementIndex;
        }
        if ( cheapest < 0 )
            return false;
        if ( _simulation.getCash() < _listPlacement[static_cast<size_t>( cheapest )]._buildCost )
            return false;
        return buildPlacement( cheapest );
    }

    void ParkWorld::spawnRideView( int32 placementIndex )
    {
        const RidePlacement& placement = _listPlacement[static_cast<size_t>( placementIndex )];
        spawnPath( _settings._gatePosition, placement._ride._entrance );
        (void)_stage.createModelObject( "RideEntrance", ParkWorldInternal::makeModelPath( "ride_entrance" ), ParkWorldInternal::makeModelLook(),
                                        placement._ride._entrance, float3{ 1.6f, 1.6f, 1.6f } );
        if ( placement._layoutId.empty() )
        {
            FlatRideView view;
            view._placementIndex = placementIndex;
            GameObject* pObject  = _stage.createPrimitiveObject( "FlatRide", placement._shape, PrimitiveLook::makeColor( placement._color ),
                                                                 placement._position + float3{ 0.0f, placement._size._y * 0.5f, 0.0f }, placement._size );
            view._object         = pObject != nullptr ? pObject->getHandle() : GameObjectHandle{};
            _listFlatView.push_back( view );
            return;
        }
        unique_ptr<CoasterView> pView = make_unique<CoasterView>();
        pView->_placementIndex        = placementIndex;
        spawnCoasterView( *pView, placement );
        _listCoasterView.push_back( std::move( pView ) );
    }

    void ParkWorld::spawnCoasterView( CoasterView& view, const RidePlacement& placement )
    {
        const CoasterLayoutDef* pLayout = _layoutCatalog.findLayout( placement._layoutId );
        if ( pLayout == nullptr )
            return;
        CoasterTrackBuilder builder;
        builder.reset( placement._position + float3{ 0.0f, pLayout->_startHeight, 0.0f }, placement._heading );
        builder.appendPieces( pLayout->_listPiece );
        view._pTrack = make_unique<CoasterTrack>( builder.makeTrack( true ) );
        view._train.initialize( view._pTrack.get(), CoasterPhysicsParams{}, 0.0f );

        // 레일 — 2 m 마다 트랙 좌표계로 돌린 직선 레일 조각(조각 시작점이 원점). 기둥 — 8 m 마다 레일 밑에서 땅까지(뒤집힌 구간은 없다).
        // 스테이션 구간은 레일 옆에 승강장을 깐다.
        using Internal                  = ParkWorldInternal;
        const PrimitiveLook modelLook   = Internal::makeModelLook();
        const string        railPath    = Internal::makeModelPath( "coaster_steel_straight" );
        const string        stationPath = Internal::makeModelPath( "station" );
        const string        supportPath = Internal::makeModelPath( "support_small" );
        const float3        railScale{ Internal::kModelScale, Internal::kModelScale, Internal::kRailSpacing * 1.05f / Internal::kRailPieceLength };
        float32             nextSupport = 0.0f;
        for ( float32 distance = 0.0f; distance < view._pTrack->getLength(); distance += Internal::kRailSpacing )
        {
            const CoasterTrackFrame frame    = view._pTrack->sample( distance );
            const float3            rotation = OrientationUtil::computeEulerFromForwardUp( frame._forward, frame._up );
            const bool              bStation = ( frame._flags & CoasterSegmentFlag::kStation ) != 0;
            (void)_stage.createModelObject( "CoasterRail", railPath, modelLook, frame._position + frame._up * ( Internal::kModelScale * Internal::kRailTopDepth ),
                                            railScale, rotation );
            if ( bStation )
                (void)_stage.createModelObject( "CoasterStation", stationPath, modelLook,
                                                frame._position - frame._right * 2.2f - frame._up * ( Internal::kModelScale * Internal::kRailTopDepth ),
                                                float3{ 2.0f, 2.0f, Internal::kRailSpacing * 1.05f }, rotation );
            const float32 railBottom = frame._position._y - Internal::kModelScale * ( Internal::kRailBottomDepth - Internal::kRailTopDepth );
            if ( distance >= nextSupport && frame._up._y > 0.6f && railBottom > 1.5f )
            {
                nextSupport = distance + Internal::kSupportSpacing;
                (void)_stage.createModelObject( "CoasterSupport", supportPath, modelLook, float3{ frame._position._x, 0.0f, frame._position._z },
                                                float3{ Internal::kModelScale, railBottom, Internal::kModelScale } );
            }
        }
        view._listCar.clear();
        for ( uint32 carIndex = 0; carIndex < Internal::kCarCount; ++carIndex )
        {
            GameObject* pCar = _stage.createModelObject( "CoasterCar", Internal::makeModelPath( carIndex == 0 ? "coaster_train_front" : "coaster_train" ), modelLook,
                                                         placement._position, float3{ Internal::kModelScale, Internal::kModelScale, Internal::kModelScale } );
            if ( pCar != nullptr )
                view._listCar.push_back( pCar->getHandle() );
        }
    }

    void ParkWorld::spawnPath( const float3& from, const float3& to )
    {
        const float3  delta  = float3{ to._x - from._x, 0.0f, to._z - from._z };
        const float32 length = delta.getLength();
        if ( length < 0.5f )
            return;
        const float32 yaw = MathUtil::atan2( delta._x, delta._z );
        (void)_stage.createModelObject( "ParkPath", ParkWorldInternal::makeModelPath( "path_straight" ), ParkWorldInternal::makeModelLook(),
                                        float3{ ( from._x + to._x ) * 0.5f, 0.0f, ( from._z + to._z ) * 0.5f }, float3{ 2.4f, 0.3f, length + 2.4f },
                                        float3{ 0.0f, yaw, 0.0f } );
    }

    void ParkWorld::spawnDecoration()
    {
        // 정문 안쪽 광장 — 매점 넷과 벤치 · 휴지통. 놀이기구 자리(배치 데이터)는 정문에서 15 m 넘게 떨어져 있어 겹치지 않는다.
        const PrimitiveLook modelLook = ParkWorldInternal::makeModelLook();
        const float3        gate      = _settings._gatePosition;
        const float3        stallScale{ 2.4f, 2.4f, 2.4f };
        const utf8* const   arrStall[] = { "stall_food", "stall_drinks", "stall_information", "stall_toilets" };
        for ( int32 stallIndex = 0; stallIndex < 4; ++stallIndex )
        {
            const float32 side = ( stallIndex % 2 == 0 ) ? -1.0f : 1.0f;
            const float32 row  = static_cast<float32>( stallIndex / 2 );
            const float3  position{ gate._x + side * 7.0f, 0.0f, gate._z + 6.0f + row * 5.0f };
            // 매점의 판매대(-Z 쪽)가 가운데 길을 보게 돌린다.
            (void)_stage.createModelObject( "ParkStall", ParkWorldInternal::makeModelPath( arrStall[stallIndex] ), modelLook, position, stallScale,
                                            float3{ 0.0f, side * ParkWorldInternal::kPi * 0.5f, 0.0f } );
            (void)_stage.createModelObject( "ParkBench", ParkWorldInternal::makeModelPath( "bench" ), modelLook,
                                            position + float3{ -side * 3.0f, 0.0f, 2.5f }, float3{ 2.0f, 2.0f, 2.0f },
                                            float3{ 0.0f, side * ParkWorldInternal::kPi * 0.5f, 0.0f } );
            (void)_stage.createModelObject( "ParkTrash", ParkWorldInternal::makeModelPath( "trash" ), modelLook, position + float3{ -side * 3.0f, 0.0f, -1.5f },
                                            float3{ 2.0f, 2.0f, 2.0f } );
        }

        // 잔디 가장자리를 따라 나무 · 꽃 — 씨앗 고정 해시로 흩어 실행마다 같다.
        const float3 grassMin{ -100.0f, 0.0f, -45.0f };
        const float3 grassMax{ 140.0f, 0.0f, 265.0f };
        uint32       seed     = 0x9E3779B9u;
        const auto   nextUnit = [&seed]()
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return static_cast<float32>( seed & 0xFFFFu ) / 65535.0f;
        };
        const auto plant = [&]( const float3& position )
        {
            const float32 pick = nextUnit();
            // 정문 앞은 비운다(손님이 들어오는 길).
            if ( float3::getDistanceSquared( position, gate ) < 14.0f * 14.0f )
                return;
            const utf8*   pModel = pick < 0.45f ? "tree" : ( pick < 0.8f ? "tree_large" : "flowers" );
            const float32 scale  = 3.0f + nextUnit() * 1.5f;
            (void)_stage.createModelObject( "ParkTree", ParkWorldInternal::makeModelPath( pModel ), modelLook, position, float3{ scale, scale, scale },
                                            float3{ 0.0f, nextUnit() * ParkWorldInternal::kPi * 2.0f, 0.0f } );
        };
        for ( float32 x = grassMin._x; x <= grassMax._x; x += 9.0f )
        {
            plant( float3{ x + nextUnit() * 3.0f, 0.0f, grassMin._z + nextUnit() * 4.0f } );
            plant( float3{ x + nextUnit() * 3.0f, 0.0f, grassMax._z - nextUnit() * 4.0f } );
        }
        for ( float32 z = grassMin._z + 9.0f; z < grassMax._z; z += 9.0f )
        {
            plant( float3{ grassMin._x + nextUnit() * 4.0f, 0.0f, z + nextUnit() * 3.0f } );
            plant( float3{ grassMax._x - nextUnit() * 4.0f, 0.0f, z + nextUnit() * 3.0f } );
        }
    }

    // ------------------------------------------------------------------------------
    // 갱신
    // ------------------------------------------------------------------------------
    void ParkWorld::updateInput( float32 deltaTime, const InputManager& input )
    {
        // 화면 기준 이동 — 카메라 요에 맞춰 돌린다.
        const float3 forward{ MathUtil::sin( _cameraYaw ), 0.0f, MathUtil::cos( _cameraYaw ) };
        const float3 right{ forward._z, 0.0f, -forward._x };
        float3       pan{ 0.0f, 0.0f, 0.0f };
        if ( input.isKeyDown( Key::W ) || input.isKeyDown( Key::Up ) )
            pan = pan + forward;
        if ( input.isKeyDown( Key::S ) || input.isKeyDown( Key::Down ) )
            pan = pan - forward;
        if ( input.isKeyDown( Key::D ) || input.isKeyDown( Key::Right ) )
            pan = pan + right;
        if ( input.isKeyDown( Key::A ) || input.isKeyDown( Key::Left ) )
            pan = pan - right;
        _cameraFocus = _cameraFocus + pan * ( ParkWorldInternal::kPanSpeed * deltaTime * _cameraHeight / 110.0f );
        if ( input.wasKeyPressed( Key::Q ) )
            _cameraYaw -= 90.0f * ParkWorldInternal::kDegToRad;
        if ( input.wasKeyPressed( Key::E ) )
            _cameraYaw += 90.0f * ParkWorldInternal::kDegToRad;
        const float32 wheel = input.getMouseWheel();
        if ( wheel != 0.0f )
            _cameraHeight = MathUtil::clamp( _cameraHeight * ( wheel > 0.0f ? 0.85f : 1.0f / 0.85f ), 25.0f, 220.0f );

        if ( input.wasKeyPressed( Key::Tab ) && _listPlacement.empty() == false )
        {
            _selectedPlacement = ( _selectedPlacement + 1 ) % static_cast<int32>( _listPlacement.size() );
            (void)GameSound::play( "game/themepark/sounds/select_001.ogg" );
            [[maybe_unused]] const RidePlacement& placement = _listPlacement[static_cast<size_t>( _selectedPlacement )];
            SW_LOG_INFO( "[Park] selected %# (%#)", placement._ride._name.c_str(), placement._rideIndex >= 0 ? "built" : "not built - B to build" );
        }
        const int32 rideIndex = findSelectedRideIndex();
        if ( rideIndex >= 0 )
        {
            const ParkRide& ride = _simulation.getRides()[static_cast<size_t>( rideIndex )];
            if ( input.wasKeyPressed( Key::O ) )
            {
                _simulation.setRideOpen( rideIndex, ride._bOpen == SW_FALSE );
                SW_LOG_INFO( "[Park] %# is now %#", ride._name.c_str(), ride._bOpen != SW_FALSE ? "open" : "closed" );
            }
            if ( input.wasKeyPressed( Key::LeftBracket ) || input.wasKeyPressed( Key::RightBracket ) )
            {
                _simulation.setRidePrice( rideIndex, ride._price + ( input.wasKeyPressed( Key::RightBracket ) ? 1 : -1 ) );
                SW_LOG_INFO( "[Park] %# ticket $%# (worth about $%#)", ride._name.c_str(), ride._price, static_cast<int32>( ThemeParkSimulation::computeRideValue( ride ) ) );
            }
        }
        if ( input.wasKeyPressed( Key::Minus ) || input.wasKeyPressed( Key::Equal ) )
        {
            _simulation.setEntryFee( _simulation.getSettings()._entryFee + ( input.wasKeyPressed( Key::Equal ) ? 5 : -5 ) );
            SW_LOG_INFO( "[Park] entry fee $%#", _simulation.getSettings()._entryFee );
        }
        if ( input.wasKeyPressed( Key::B ) )
        {
            if ( _listPlacement[static_cast<size_t>( _selectedPlacement )]._rideIndex < 0 )
                (void)buildPlacement( _selectedPlacement );
            else
                (void)buildCheapestRemaining();
        }
        if ( input.wasKeyPressed( Key::V ) )
        {
            // 타고 있으면 내린다. 아니면 고른 코스터(고른 것이 코스터가 아니면 첫 코스터)에 탄다.
            if ( _ridingCoaster >= 0 )
                _ridingCoaster = -1;
            else if ( _listCoasterView.empty() == false )
            {
                _ridingCoaster = 0;
                for ( int32 viewIndex = 0; viewIndex < static_cast<int32>( _listCoasterView.size() ); ++viewIndex )
                {
                    if ( _listCoasterView[static_cast<size_t>( viewIndex )]->_placementIndex == _selectedPlacement )
                        _ridingCoaster = viewIndex;
                }
            }
            SW_LOG_INFO( "[Park] %#", _ridingCoaster >= 0 ? "riding the coaster - V again to get off" : "back to the park view" );
        }
        if ( input.wasKeyPressed( Key::G ) )
            logThoughts();
        if ( input.wasKeyPressed( Key::F1 ) )
            logStatus( 0.0f, true );
    }

    void ParkWorld::updateRides( float32 deltaTime )
    {
        for ( unique_ptr<CoasterView>& pView : _listCoasterView )
        {
            CoasterView& view = *pView;
            if ( view._pTrack == nullptr )
                continue;
            view._train.step( deltaTime );
            for ( uint32 carIndex = 0; carIndex < view._listCar.size(); ++carIndex )
            {
                MeshComponent* pCar = _stage.findMesh( view._listCar[carIndex] );
                if ( pCar == nullptr )
                    continue;
                const CoasterTrackFrame frame = view._train.getCarFrame( carIndex, ParkWorldInternal::kCarSpacing );
                pCar->setLocalPosition( frame._position );
                pCar->setLocalRotation( OrientationUtil::computeEulerFromForwardUp( frame._forward, frame._up ) );
            }
        }
        // 평지 놀이기구는 탄 사람이 있을 때 돈다.
        for ( FlatRideView& view : _listFlatView )
        {
            const RidePlacement& placement = _listPlacement[static_cast<size_t>( view._placementIndex )];
            if ( placement._rideIndex < 0 || placement._spin == 0.0f )
                continue;
            const ParkRide& ride = _simulation.getRides()[static_cast<size_t>( placement._rideIndex )];
            if ( ride._listRider.empty() )
                continue;
            view._angle += placement._spin * deltaTime;
            if ( MeshComponent* pMesh = _stage.findMesh( view._object ) )
                pMesh->setLocalRotation( float3{ 0.0f, view._angle, 0.0f } );
        }
    }

    void ParkWorld::updateGuests()
    {
        const vector<ParkGuest>& listGuest = _simulation.getGuests();
        for ( size_t viewIndex = 0; viewIndex < _listGuestView.size(); ++viewIndex )
        {
            GuestView&     view  = _listGuestView[viewIndex];
            MeshComponent* pMesh = _stage.findMesh( view._object );
            if ( pMesh == nullptr )
                continue;
            const bool bShown = viewIndex < listGuest.size() && listGuest[viewIndex]._state != ParkGuestState::Riding &&
                                listGuest[viewIndex]._state != ParkGuestState::Left;
            pMesh->setVisible( bShown );
            if ( bShown == false )
                continue;
            const ParkGuest& guest = listGuest[viewIndex];
            pMesh->setLocalPosition( guest._position + float3{ 0.0f, 0.45f, 0.0f } );
            const int32 bucket = ParkWorldInternal::findHappinessBucket( guest._happiness );
            if ( bucket != view._colorBucket )
            {
                constexpr float4 kArrBucketColor[3] = {
                    { 0.3f, 0.85f, 0.35f, 1.0f},
                    {0.95f, 0.85f, 0.25f, 1.0f},
                    { 0.9f, 0.25f,  0.2f, 1.0f}
                };
                _stage.setLook( *pMesh, PrimitiveLook::makeColor( kArrBucketColor[bucket] ) );
                view._colorBucket = bucket;
            }
        }
    }

    void ParkWorld::updateCamera()
    {
        if ( _ridingCoaster >= 0 && _ridingCoaster < static_cast<int32>( _listCoasterView.size() ) )
        {
            // 맨 앞 차량에 탄 시점 — 롤까지 따라간다(루프에서 하늘이 아래로 온다).
            const CoasterView&      view  = *_listCoasterView[static_cast<size_t>( _ridingCoaster )];
            const CoasterTrackFrame frame = view._train.getFrame();
            _stage.placeCamerasWithRotation( frame._position + frame._up * 1.6f, OrientationUtil::computeEulerFromForwardUp( frame._forward, frame._up ),
                                             85.0f * ParkWorldInternal::kDegToRad, 600.0f );
            return;
        }
        const float3 toCamera{ -MathUtil::sin( _cameraYaw ) * MathUtil::cos( ParkWorldInternal::kCameraPitch ), MathUtil::sin( ParkWorldInternal::kCameraPitch ),
                               -MathUtil::cos( _cameraYaw ) * MathUtil::cos( ParkWorldInternal::kCameraPitch ) };
        _stage.placeCameras( _cameraFocus + toCamera * ParkWorldInternal::kCameraDistance, _cameraFocus, _cameraHeight, ParkWorldInternal::kCameraDistance * 2.5f );
    }

    void ParkWorld::logStatus( float32 deltaTime, bool bForce )
    {
        _statusTimer += deltaTime;
        if ( bForce == false && _statusTimer < 10.0f )
            return;
        _statusTimer = 0.0f;
        SW_LOG_INFO( "[Park] %# min · cash $%# · guests %# (visited %#) · happiness %#%% · rating %# · rides %# · entry $%#",
                     static_cast<int32>( _simulation.getElapsedTime() / 60.0f ), _simulation.getCash(), _simulation.getGuestCount(),
                     _simulation.getTotalVisitorCount(), static_cast<int32>( _simulation.getAverageHappiness() * 100.0f ), _simulation.getParkRating(),
                     static_cast<uint32>( _simulation.getRides().size() ), _simulation.getSettings()._entryFee );
        for ( [[maybe_unused]] const ParkRide& ride : _simulation.getRides() )
        {
            SW_LOG_INFO( "[Park]   %# - %# · queue %# · riders %# · total %# · income $%# · ticket $%#", ride._name.c_str(), ride._bOpen != SW_FALSE ? "open" : "closed",
                         static_cast<uint32>( ride._listQueue.size() ), static_cast<uint32>( ride._listRider.size() ), ride._totalRiders, ride._totalIncome, ride._price );
        }
    }

    void ParkWorld::logThoughts() const
    {
        constexpr ParkGuestThought kArrThought[] = { ParkGuestThought::GreatRide, ParkGuestThought::TooIntense, ParkGuestThought::TooTame,
                                                     ParkGuestThought::TooExpensive, ParkGuestThought::QueueTooLong, ParkGuestThought::Sick,
                                                     ParkGuestThought::Tired, ParkGuestThought::OutOfCash, ParkGuestThought::NothingToRide };
        for ( const ParkGuestThought thought : kArrThought )
        {
            const uint32 count = _simulation.countGuestsThinking( thought );
            if ( count > 0 )
                SW_LOG_INFO( "[Park] %# guests think: %#", count, toString( thought ) );
        }
    }

    int32 ParkWorld::findSelectedRideIndex() const
    {
        if ( _selectedPlacement < 0 || _selectedPlacement >= static_cast<int32>( _listPlacement.size() ) )
            return -1;
        return _listPlacement[static_cast<size_t>( _selectedPlacement )]._rideIndex;
    }
} // namespace sw
