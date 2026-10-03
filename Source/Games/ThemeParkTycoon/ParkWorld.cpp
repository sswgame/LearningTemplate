#include "pch.h"

#include "Games/ThemeParkTycoon/ParkWorld.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/GameService.h"
#include "GameFramework/Base/OrientationUtil.h"

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

            /** @brief "r g b [a]" 를 읽습니다. */
            static float4 parseColor( string_view text, const float4& fallback )
            {
                float32 arrValue[4] = { fallback._x, fallback._y, fallback._z, fallback._w };
                size_t  tokenStart  = 0;
                int32   valueIndex  = 0;
                while ( tokenStart < text.size() && valueIndex < 4 )
                {
                    size_t tokenEnd = text.find_first_of( ", ", tokenStart );
                    if ( tokenEnd == string_view::npos )
                        tokenEnd = text.size();
                    const string_view token = text.substr( tokenStart, tokenEnd - tokenStart );
                    if ( token.empty() == false )
                    {
                        float32 value = 0.0f;
                        if ( StringUtil::parseFloat( token, value ) )
                            arrValue[valueIndex] = value;
                        ++valueIndex;
                    }
                    tokenStart = tokenEnd + 1;
                }
                return float4{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
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
        , _listBlueprint{}
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
        , _selectedBlueprint{ 0 }
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
        for ( int32 blueprintIndex = 0; blueprintIndex < static_cast<int32>( _listBlueprint.size() ); ++blueprintIndex )
        {
            const RideBlueprint& blueprint = _listBlueprint[static_cast<size_t>( blueprintIndex )];
            int32&               cheapest  = blueprint._layoutId.empty() ? cheapestFlat : cheapestCoaster;
            if ( cheapest < 0 || blueprint._buildCost < _listBlueprint[static_cast<size_t>( cheapest )]._buildCost )
                cheapest = blueprintIndex;
        }
        if ( cheapestFlat >= 0 )
            (void)buildBlueprint( cheapestFlat );
        if ( cheapestCoaster >= 0 )
            (void)buildBlueprint( cheapestCoaster );
        return true;
    }

    bool ParkWorld::loadParkLayout( string_view parkPath )
    {
        XmlDocument doc;
        string      absPath;
        if ( doc.loadPath( parkPath, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to read park layout %#", parkPath );
            return false;
        }
        const XmlNode root = doc.getRoot( "ParkLayout" );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <ParkLayout> root in %#", absPath );
            return false;
        }
        _settings._gatePosition = float3{ root.getAttributeFloat( "gateX", 0.0f ), 0.0f, root.getAttributeFloat( "gateZ", -30.0f ) };
        _settings._entryFee     = MathUtil::max( 0, root.getAttributeInt( "entryFee", 0 ) );
        _startingCash           = root.getAttributeInt( "startingCash", _startingCash );

        _listBlueprint.clear();
        for ( XmlNode node = root.findChild( "FlatRide" ); node; node = node.findNextSibling( "FlatRide" ) )
        {
            const utf8* pId = node.findAttribute( "id" );
            if ( StringUtil::isNullOrEmpty( pId ) )
                continue;
            RideBlueprint blueprint;
            ParkRide&     ride         = blueprint._ride;
            ride._id                   = hashed_string( pId );
            const utf8* pName          = node.findAttribute( "name" );
            ride._name                 = pName != nullptr ? pName : pId;
            ride._excitement           = node.getAttributeFloat( "excitement", ride._excitement );
            ride._intensity            = node.getAttributeFloat( "intensity", ride._intensity );
            ride._nausea               = node.getAttributeFloat( "nausea", ride._nausea );
            ride._cycleTime            = MathUtil::max( 5.0f, node.getAttributeFloat( "cycleTime", ride._cycleTime ) );
            ride._capacity             = MathUtil::max( 1, node.getAttributeInt( "capacity", ride._capacity ) );
            ride._price                = MathUtil::max( 0, node.getAttributeInt( "price", ride._price ) );
            ride._runningCostPerMinute = MathUtil::max( 0, node.getAttributeInt( "runningCost", ride._runningCostPerMinute ) );
            blueprint._position        = float3{ node.getAttributeFloat( "x", 0.0f ), 0.0f, node.getAttributeFloat( "z", 0.0f ) };
            ride._entrance             = float3{ node.getAttributeFloat( "entranceX", blueprint._position._x ), 0.0f,
                                     node.getAttributeFloat( "entranceZ", blueprint._position._z - 4.0f - node.getAttributeFloat( "sizeZ", 4.0f ) * 0.5f ) };
            blueprint._buildCost       = MathUtil::max( 0, node.getAttributeInt( "cost", blueprint._buildCost ) );
            const utf8* pShape         = node.findAttribute( "shape" );
            blueprint._shape           = pShape != nullptr ? pShape : "Cylinder";
            blueprint._size            = float3{ node.getAttributeFloat( "sizeX", 4.0f ), node.getAttributeFloat( "sizeY", 1.0f ), node.getAttributeFloat( "sizeZ", 4.0f ) };
            blueprint._color           = ParkWorldInternal::parseColor( node.getAttributeText( "color" ), blueprint._color );
            blueprint._spin            = node.getAttributeFloat( "spin", 0.0f );
            _listBlueprint.push_back( blueprint );
        }
        for ( XmlNode node = root.findChild( "Coaster" ); node; node = node.findNextSibling( "Coaster" ) )
        {
            const utf8*             pLayoutId = node.findAttribute( "layout" );
            const CoasterLayoutDef* pLayout   = StringUtil::isNullOrEmpty( pLayoutId ) ? nullptr : _layoutCatalog.findLayout( hashed_string( pLayoutId ) );
            if ( pLayout == nullptr )
            {
                SW_LOG_WARNING( "%#: <Coaster> layout '%#' is not in the coaster catalog - skipped", absPath, pLayoutId != nullptr ? pLayoutId : "" );
                continue;
            }
            RideBlueprint blueprint;
            blueprint._layoutId       = pLayout->_id;
            blueprint._ride._id       = pLayout->_id;
            blueprint._ride._name     = pLayout->_name;
            blueprint._ride._capacity = MathUtil::max( 1, node.getAttributeInt( "capacity", 24 ) );
            blueprint._position       = float3{ node.getAttributeFloat( "x", 0.0f ), 0.0f, node.getAttributeFloat( "z", 0.0f ) };
            blueprint._heading        = node.getAttributeFloat( "heading", 0.0f );
            blueprint._loadTime       = MathUtil::max( 0.0f, node.getAttributeFloat( "loadTime", 15.0f ) );
            blueprint._buildCost      = MathUtil::max( 0, node.getAttributeInt( "cost", 5000 ) );
            blueprint._color          = ParkWorldInternal::parseColor( node.getAttributeText( "color" ), blueprint._color );
            _listBlueprint.push_back( blueprint );
        }
        return _listBlueprint.empty() == false;
    }

    bool ParkWorld::spawn()
    {
        if ( _bSpawned != SW_FALSE )
            return true;
        if ( _bLoaded == SW_FALSE || _stage.begin( "ThemeParkTycoon" ) == false )
            return false;

        (void)_stage.createPrimitiveObject( "ParkGrass", "Plane", PrimitiveLook::makeColor( float4{ 0.38f, 0.62f, 0.32f, 1.0f } ), float3{ 20.0f, -0.02f, 110.0f },
                                            float3{ 260.0f, 1.0f, 340.0f } );
        (void)_stage.createPrimitiveObject( "ParkGate", "Cube", PrimitiveLook::makeColor( float4{ 0.9f, 0.85f, 0.3f, 1.0f } ),
                                            _settings._gatePosition + float3{ 0.0f, 2.0f, 0.0f }, float3{ 8.0f, 4.0f, 1.5f } );
        (void)_stage.createSun( float3{ 0.9f, 0.8f, 0.0f }, 1.5f, 180.0f );

        _listCoasterView.clear();
        _listFlatView.clear();
        for ( int32 blueprintIndex = 0; blueprintIndex < static_cast<int32>( _listBlueprint.size() ); ++blueprintIndex )
        {
            if ( _listBlueprint[static_cast<size_t>( blueprintIndex )]._rideIndex >= 0 )
                spawnRideView( blueprintIndex );
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
    bool ParkWorld::buildBlueprint( int32 blueprintIndex )
    {
        if ( blueprintIndex < 0 || blueprintIndex >= static_cast<int32>( _listBlueprint.size() ) )
            return false;
        RideBlueprint& blueprint = _listBlueprint[static_cast<size_t>( blueprintIndex )];
        if ( blueprint._rideIndex >= 0 )
            return false;
        if ( _simulation.getCash() < blueprint._buildCost )
        {
            SW_LOG_INFO( "[Park] not enough cash for %# ($%# needed, $%# in the bank)", blueprint._ride._name.c_str(), blueprint._buildCost, _simulation.getCash() );
            return false;
        }

        ParkRide ride = blueprint._ride;
        if ( blueprint._layoutId.empty() == false )
        {
            // 코스터 — 짓고 시험 운행으로 평가를 받는다. 못 돌면(언덕을 못 넘는다) 짓지 않는다.
            const CoasterLayoutDef* pLayout = _layoutCatalog.findLayout( blueprint._layoutId );
            if ( pLayout == nullptr )
                return false;
            CoasterTrackBuilder builder;
            builder.reset( blueprint._position + float3{ 0.0f, pLayout->_startHeight, 0.0f }, blueprint._heading );
            builder.appendPieces( pLayout->_listPiece );
            const CoasterTrack     track = builder.build( true );
            const CoasterRideStats stats = CoasterRideAnalyzer::analyze( track, CoasterPhysicsParams{} );
            if ( stats._bCompleted == SW_FALSE )
            {
                SW_LOG_WARNING( "[Park] %# failed its test run (stalled) - not built", pLayout->_name.c_str() );
                return false;
            }
            ride = ThemeParkSimulation::makeRideFromCoaster( blueprint._layoutId, pLayout->_name, stats, blueprint._ride._capacity, blueprint._loadTime );
            // 줄 입구 — 스테이션 가운데 옆(바깥쪽).
            const CoasterTrackFrame stationFrame = track.sample( 6.0f );
            ride._entrance                       = stationFrame._position - stationFrame._right * 5.0f;
            ride._entrance._y                    = 0.0f;
            ride._runningCostPerMinute           = 8 + static_cast<int32>( stats._length / 60.0f );
            SW_LOG_INFO( "[Park] test run of %#: %# m, %# s lap, top speed %# m/s, %# inversions, airtime %# s -> excitement %# · intensity %# · nausea %#",
                         pLayout->_name.c_str(), static_cast<int32>( stats._length ), static_cast<int32>( stats._lapTime ), static_cast<int32>( stats._maxSpeed ),
                         stats._inversionCount, stats._airtime, stats._excitement, stats._intensity, stats._nausea );
        }

        const int32 rideIndex = _simulation.buildRide( ride, blueprint._buildCost );
        if ( rideIndex < 0 )
            return false;
        blueprint._rideIndex = rideIndex;
        blueprint._ride      = ride;
        SW_LOG_INFO( "[Park] built %# for $%# - ticket $%#, $%# left", ride._name.c_str(), blueprint._buildCost, ride._price, _simulation.getCash() );
        if ( _bSpawned != SW_FALSE )
            spawnRideView( blueprintIndex );
        return true;
    }

    bool ParkWorld::buildCheapestRemaining()
    {
        int32 cheapest = -1;
        for ( int32 blueprintIndex = 0; blueprintIndex < static_cast<int32>( _listBlueprint.size() ); ++blueprintIndex )
        {
            const RideBlueprint& blueprint = _listBlueprint[static_cast<size_t>( blueprintIndex )];
            if ( blueprint._rideIndex < 0 && ( cheapest < 0 || blueprint._buildCost < _listBlueprint[static_cast<size_t>( cheapest )]._buildCost ) )
                cheapest = blueprintIndex;
        }
        if ( cheapest < 0 )
            return false;
        if ( _simulation.getCash() < _listBlueprint[static_cast<size_t>( cheapest )]._buildCost )
            return false;
        return buildBlueprint( cheapest );
    }

    void ParkWorld::spawnRideView( int32 blueprintIndex )
    {
        const RideBlueprint& blueprint = _listBlueprint[static_cast<size_t>( blueprintIndex )];
        spawnPath( _settings._gatePosition, blueprint._ride._entrance );
        (void)_stage.createPrimitiveObject( "RideEntrance", "Cube", PrimitiveLook::makeColor( blueprint._color ), blueprint._ride._entrance + float3{ 0.0f, 1.0f, 0.0f },
                                            float3{ 1.5f, 2.0f, 1.5f } );
        if ( blueprint._layoutId.empty() )
        {
            FlatRideView view;
            view._blueprintIndex = blueprintIndex;
            GameObject* pObject  = _stage.createPrimitiveObject( "FlatRide", blueprint._shape, PrimitiveLook::makeColor( blueprint._color ),
                                                                 blueprint._position + float3{ 0.0f, blueprint._size._y * 0.5f, 0.0f }, blueprint._size );
            view._object         = pObject != nullptr ? pObject->getHandle() : GameObjectHandle{};
            _listFlatView.push_back( view );
            return;
        }
        unique_ptr<CoasterView> pView = make_unique<CoasterView>();
        pView->_blueprintIndex        = blueprintIndex;
        spawnCoasterView( *pView, blueprint );
        _listCoasterView.push_back( std::move( pView ) );
    }

    void ParkWorld::spawnCoasterView( CoasterView& view, const RideBlueprint& blueprint )
    {
        const CoasterLayoutDef* pLayout = _layoutCatalog.findLayout( blueprint._layoutId );
        if ( pLayout == nullptr )
            return;
        CoasterTrackBuilder builder;
        builder.reset( blueprint._position + float3{ 0.0f, pLayout->_startHeight, 0.0f }, blueprint._heading );
        builder.appendPieces( pLayout->_listPiece );
        view._pTrack = make_unique<CoasterTrack>( builder.build( true ) );
        view._train.initialize( view._pTrack.get(), CoasterPhysicsParams{}, 0.0f );

        // 레일 — 2 m 마다 트랙 좌표계로 돌린 납작한 상자. 기둥 — 8 m 마다 땅까지(뒤집힌 구간은 없다).
        const PrimitiveLook railLook    = PrimitiveLook::makeColor( blueprint._color );
        const PrimitiveLook supportLook = PrimitiveLook::makeColor( float4{ 0.75f, 0.75f, 0.78f, 1.0f } );
        float32             nextSupport = 0.0f;
        for ( float32 distance = 0.0f; distance < view._pTrack->getLength(); distance += ParkWorldInternal::kRailSpacing )
        {
            const CoasterTrackFrame frame    = view._pTrack->sample( distance + ParkWorldInternal::kRailSpacing * 0.5f );
            const float3            rotation = OrientationUtil::computeEulerFromForwardUp( frame._forward, frame._up );
            const bool              bStation = ( frame._flags & CoasterSegmentFlag::kStation ) != 0;
            (void)_stage.createPrimitiveObject( "CoasterRail", "Cube", bStation ? PrimitiveLook::makeColor( float4{ 0.55f, 0.45f, 0.35f, 1.0f } ) : railLook,
                                                frame._position, float3{ bStation ? 4.0f : 1.6f, 0.25f, ParkWorldInternal::kRailSpacing * 1.05f }, rotation );
            if ( distance >= nextSupport && frame._up._y > 0.6f && frame._position._y > 1.5f )
            {
                nextSupport = distance + ParkWorldInternal::kSupportSpacing;
                (void)_stage.createPrimitiveObject( "CoasterSupport", "Cube", supportLook, float3{ frame._position._x, frame._position._y * 0.5f, frame._position._z },
                                                    float3{ 0.35f, frame._position._y, 0.35f } );
            }
        }
        view._listCar.clear();
        for ( uint32 carIndex = 0; carIndex < ParkWorldInternal::kCarCount; ++carIndex )
        {
            GameObject* pCar = _stage.createPrimitiveObject( "CoasterCar", "Cube", PrimitiveLook::makeColor( float4{ 0.95f, 0.95f, 0.95f, 1.0f } ),
                                                             blueprint._position, float3{ 1.4f, 0.9f, 2.1f } );
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
        (void)_stage.createPrimitiveObject( "ParkPath", "Cube", PrimitiveLook::makeColor( float4{ 0.62f, 0.58f, 0.52f, 1.0f } ),
                                            ( from + to ) * 0.5f + float3{ 0.0f, 0.03f, 0.0f }, float3{ 2.4f, 0.06f, length + 2.4f }, float3{ 0.0f, yaw, 0.0f } );
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

        if ( input.wasKeyPressed( Key::Tab ) && _listBlueprint.empty() == false )
        {
            _selectedBlueprint                              = ( _selectedBlueprint + 1 ) % static_cast<int32>( _listBlueprint.size() );
            [[maybe_unused]] const RideBlueprint& blueprint = _listBlueprint[static_cast<size_t>( _selectedBlueprint )];
            SW_LOG_INFO( "[Park] selected %# (%#)", blueprint._ride._name.c_str(), blueprint._rideIndex >= 0 ? "built" : "not built - B to build" );
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
            if ( _listBlueprint[static_cast<size_t>( _selectedBlueprint )]._rideIndex < 0 )
                (void)buildBlueprint( _selectedBlueprint );
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
                    if ( _listCoasterView[static_cast<size_t>( viewIndex )]->_blueprintIndex == _selectedBlueprint )
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
                pCar->setLocalPosition( frame._position + frame._up * 0.6f );
                pCar->setLocalRotation( OrientationUtil::computeEulerFromForwardUp( frame._forward, frame._up ) );
            }
        }
        // 평지 놀이기구는 탄 사람이 있을 때 돈다.
        for ( FlatRideView& view : _listFlatView )
        {
            const RideBlueprint& blueprint = _listBlueprint[static_cast<size_t>( view._blueprintIndex )];
            if ( blueprint._rideIndex < 0 || blueprint._spin == 0.0f )
                continue;
            const ParkRide& ride = _simulation.getRides()[static_cast<size_t>( blueprint._rideIndex )];
            if ( ride._listRider.empty() )
                continue;
            view._angle += blueprint._spin * deltaTime;
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
        if ( _selectedBlueprint < 0 || _selectedBlueprint >= static_cast<int32>( _listBlueprint.size() ) )
            return -1;
        return _listBlueprint[static_cast<size_t>( _selectedBlueprint )]._rideIndex;
    }
} // namespace sw
