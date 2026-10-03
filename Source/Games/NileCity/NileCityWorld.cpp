#include "pch.h"

#include "Games/NileCity/NileCityWorld.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Window/IWindow.h"

#include "GameFramework/Base/GameService.h"
#include "GameFramework/Base/RayMath.h"

namespace sw
{
    SW_LOG_CALLER( "NileCityWorld" );

    namespace
    {
        struct NileCityWorldInternal
        {
            static constexpr float32 kPi             = 3.14159265f;
            static constexpr float32 kCameraPitch    = 55.0f * kPi / 180.0f;
            static constexpr float32 kCameraYaw      = 0.0f; ///< 북쪽(+Z)을 본다
            static constexpr float32 kCameraDistance = 120.0f;
            static constexpr float32 kPanSpeed       = 1.2f; ///< 화면 높이에 곱한다(초당)
            static constexpr float32 kWalkerHeight   = 0.35f;

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

            static float4 computeBuildingColor( const CityBuildingDef& def, int32 level, bool bInhabited )
            {
                switch ( def._kind )
                {
                    case CityBuildingKind::House:
                    {
                        if ( bInhabited == false )
                            return float4{ 0.55f, 0.45f, 0.35f, 1.0f };
                        const float32 alpha = MathUtil::clamp( static_cast<float32>( level ) / 7.0f, 0.0f, 1.0f );
                        return float4{ 0.75f + 0.2f * alpha, 0.62f + 0.3f * alpha, 0.42f + 0.43f * alpha, 1.0f };
                    }
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

            static float32 computeBuildingHeight( const CityBuildingDef& def, int32 level, bool bInhabited )
            {
                switch ( def._kind )
                {
                    case CityBuildingKind::House:
                        return bInhabited ? 0.5f + 0.2f * static_cast<float32>( level ) : 0.15f;
                    case CityBuildingKind::Service:
                        return 1.2f;
                    case CityBuildingKind::Producer:
                        return def._bRequiresTerrain != SW_FALSE ? 0.25f : 0.9f;
                    case CityBuildingKind::Storage:
                        return 1.3f;
                    case CityBuildingKind::Market:
                        return 0.8f;
                    case CityBuildingKind::Decoration:
                        return 0.4f;
                }
                return 1.0f;
            }

            static float4 computeWalkerColor( const CityWalker& walker )
            {
                switch ( walker._kind )
                {
                    case CityWalkerKind::Service:
                        return computeServiceColor( walker._service );
                    case CityWalkerKind::Trader:
                        return float4{ 1.0f, 0.6f, 0.2f, 1.0f };
                    case CityWalkerKind::Cart:
                        return float4{ 0.45f, 0.3f, 0.15f, 1.0f };
                }
                return float4{ 1.0f, 1.0f, 1.0f, 1.0f };
            }

            /** @brief 칸 (x, y) 의 가운데 월드 자리입니다(시뮬레이션 y 가 월드 +Z). */
            static float3 computeTileCenter( float32 x, float32 y, float32 height ) { return float3{ x, height, y }; }

            /** @brief 카메라가 보는 방향입니다(피치 + 가 아래). */
            static float3 computeCameraForward() { return RayMath::computeLookDirection( kCameraYaw, -kCameraPitch ); }
        };
    } // namespace

    /**
     * @brief `-gv_nileAutoPlay=1` — 자동 계획표대로 도로 고리 · 우물 · 농장 · 창고 · 시장 · 집을 지으며 도시를 돌립니다(입력 없이 도시가 크는 확인).
     * @details 배포본으로도 돌릴 수 있게 남긴다: `App -gv_nileAutoPlay=1`. 달마다 `[Nile] month N pop P money M` 이 로그에 남는다.
     */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_nileAutoPlay, 0, "NileCity: 자동 계획으로 도시를 짓고 돌리기 (1=켜기)", SW_KEEP_IN_SHIPPING );
} // namespace sw

namespace sw
{
    NileCityWorld::NileCityWorld()
        : _stage{}
        , _city{}
        , _planner{}
        , _listEvent{}
        , _listRoadObject{}
        , _listBuildingView{}
        , _listWalkerObject{}
        , _listWalkerLookKey{}
        , _listTool{}
        , _pCatalog{ nullptr }
        , _cursorObject{}
        , _cameraFocus{ 26.0f, 0.0f, 14.0f }
        , _cursorTile{ -1, -1 }
        , _cameraHeight{ 34.0f }
        , _timeScale{ 1.0f }
        , _selectedTool{ 0 }
        , _monthCount{ 0 }
        , _evolvedCount{ 0 }
        , _bCursorValid{ SW_FALSE }
        , _bPaused{ SW_FALSE }
        , _bAutoPlan{ SW_FALSE }
        , _bSpawned{ SW_FALSE }
    {
    }

    NileCityWorld::~NileCityWorld() = default;

    void NileCityWorld::initialize( const CityCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        // 순회 서비스가 남는 시간을 키트 기본(30 초)보다 길게 — 이 도시의 도로망에서 일꾼 하나가 고리를 다 도는 데 그만큼 걸린다.
        CitySettings settings;
        settings._serviceDuration = 60.0f;
        _city.initialize( pCatalog, NileCityPlanner::kMapWidth, NileCityPlanner::kMapHeight, settings, kStartingMoney );
        NileCityPlanner::paintTerrain( _city );
        _planner.reset();
        _listEvent.clear();
        _monthCount   = 0;
        _evolvedCount = 0;

        _listTool.clear();
        _listTool.push_back( nullptr );
        for ( const CityBuildingDef& def : pCatalog->getBuildings() )
            _listTool.push_back( &def );
        _selectedTool = 0;
    }

    bool NileCityWorld::spawn()
    {
        if ( _bSpawned != SW_FALSE )
            return true;
        if ( _pCatalog == nullptr || _stage.begin( "NileCity" ) == false )
            return false;
        spawnTerrain();
        (void)_stage.createSun( float3{ 0.95f, 0.6f, 0.0f }, 1.6f, 60.0f );
        _listRoadObject.assign( static_cast<size_t>( _city.getWidth() * _city.getHeight() ), GameObjectHandle{} );
        _listBuildingView.clear();
        _listWalkerObject.clear();
        _listWalkerLookKey.clear();
        GameObject* pCursor = _stage.createPrimitiveObject( "NileCursor", "Cube", PrimitiveLook::makeTranslucent( float4{ 1.0f, 0.9f, 0.2f, 0.45f } ), float3{ 0.0f, -5.0f, 0.0f },
                                                            float3{ 1.0f, 0.12f, 1.0f } );
        _cursorObject       = pCursor != nullptr ? pCursor->getHandle() : GameObjectHandle{};
        _bSpawned           = SW_TRUE;
        syncRoads();
        syncBuildings();
        updateCamera();
        SW_LOG_INFO( "[Nile] the city is founded on the Nile - WASD pan, wheel zoom, Q/E pick building, R road, left click build, right click demolish, "
                     "P auto plan, Space pause, - = speed, F1 status" );
        return true;
    }

    void NileCityWorld::despawn()
    {
        _stage.clear();
        _listRoadObject.clear();
        _listBuildingView.clear();
        _listWalkerObject.clear();
        _listWalkerLookKey.clear();
        _cursorObject = GameObjectHandle{};
        _bSpawned     = SW_FALSE;
    }

    void NileCityWorld::update( float32 deltaTime )
    {
        if ( _stage.isSceneChanged() )
        {
            _stage.forget();
            _listRoadObject.clear();
            _listBuildingView.clear();
            _listWalkerObject.clear();
            _listWalkerLookKey.clear();
            _cursorObject = GameObjectHandle{};
            _bSpawned     = SW_FALSE;
        }
        if ( _bSpawned == SW_FALSE && spawn() == false )
            return;
        if ( deltaTime <= 0.0f )
            return;

        const InputManager* pInput = game::getService<InputManager>();
        if ( pInput != nullptr )
            updateInput( deltaTime, *pInput );
        if ( gv_nileAutoPlay != 0 || _bAutoPlan != SW_FALSE )
            (void)_planner.advance( _city, _pCatalog->getRoadCost() );
        if ( _bPaused == SW_FALSE )
            _city.update( deltaTime * _timeScale );
        drainEvents();
        syncRoads();
        syncBuildings();
        syncWalkers();
        updateCamera();
    }

    // ------------------------------------------------------------------------------
    // 모습
    // ------------------------------------------------------------------------------
    void NileCityWorld::spawnTerrain()
    {
        // 줄마다 같은 땅이 이어지는 구간을 상자 하나로(칸마다 세우면 2000 개 — 땅은 바뀌지 않는다). 바위는 칸마다 솟은 상자.
        for ( int32 y = 0; y < _city.getHeight(); ++y )
        {
            int32 runStart = 0;
            for ( int32 x = 1; x <= _city.getWidth(); ++x )
            {
                const CityTerrain runTerrain = _city.findTile( runStart, y )->_terrain;
                if ( x < _city.getWidth() && _city.findTile( x, y )->_terrain == runTerrain )
                    continue;
                const float32 length = static_cast<float32>( x - runStart );
                const float32 top    = runTerrain == CityTerrain::Water ? -0.15f : 0.0f;
                (void)_stage.createPrimitiveObject( "NileGround", "Cube", PrimitiveLook::makeColor( NileCityWorldInternal::computeTerrainColor( runTerrain ) ),
                                                    float3{ static_cast<float32>( runStart ) + length * 0.5f, top - 0.1f, static_cast<float32>( y ) + 0.5f },
                                                    float3{ length, 0.2f, 1.0f } );
                if ( runTerrain == CityTerrain::Rock )
                {
                    for ( int32 rockX = runStart; rockX < x; ++rockX )
                    {
                        (void)_stage.createPrimitiveObject( "NileRock", "Cube", PrimitiveLook::makeColor( NileCityWorldInternal::computeTerrainColor( CityTerrain::Rock ) ),
                                                            float3{ static_cast<float32>( rockX ) + 0.5f, 0.4f, static_cast<float32>( y ) + 0.5f }, float3{ 0.9f, 0.8f, 0.9f } );
                    }
                }
                runStart = x;
            }
        }
    }

    void NileCityWorld::syncRoads()
    {
        const PrimitiveLook roadLook = PrimitiveLook::makeColor( float4{ 0.72f, 0.66f, 0.55f, 1.0f } );
        for ( int32 y = 0; y < _city.getHeight(); ++y )
        {
            for ( int32 x = 0; x < _city.getWidth(); ++x )
            {
                GameObjectHandle& handle = _listRoadObject[static_cast<size_t>( y * _city.getWidth() + x )];
                const bool        bRoad  = _city.findTile( x, y )->_bRoad != SW_FALSE;
                if ( bRoad == handle.isValid() )
                    continue;
                if ( bRoad == false )
                {
                    _stage.destroyObject( handle );
                    handle = GameObjectHandle{};
                    continue;
                }
                GameObject* pRoad = _stage.createPrimitiveObject( "NileRoad", "Cube", roadLook, float3{ static_cast<float32>( x ) + 0.5f, 0.03f, static_cast<float32>( y ) + 0.5f },
                                                                  float3{ 0.96f, 0.06f, 0.96f } );
                handle            = pRoad != nullptr ? pRoad->getHandle() : GameObjectHandle{};
            }
        }
    }

    void NileCityWorld::syncBuildings()
    {
        const vector<CityBuilding>& listBuilding = _city.getBuildings();
        if ( _listBuildingView.size() < listBuilding.size() )
            _listBuildingView.resize( listBuilding.size() );
        for ( size_t buildingIndex = 0; buildingIndex < listBuilding.size(); ++buildingIndex )
        {
            const CityBuilding& building  = listBuilding[buildingIndex];
            BuildingView&       view      = _listBuildingView[buildingIndex];
            const bool          bAlive    = building._bAlive != SW_FALSE;
            const bool          bInhabit  = building._population > 0;
            const bool          bSameView = view._object.isValid() && view._pDef == building._pDef && view._level == building._level &&
                                   ( view._bInhabited != SW_FALSE ) == bInhabit;
            if ( bAlive && bSameView )
                continue;
            if ( view._object.isValid() )
                _stage.destroyObject( view._object );
            view = BuildingView{};
            if ( bAlive == false )
                continue;
            const CityBuildingDef& def    = *building._pDef;
            const float32          size   = static_cast<float32>( def._size );
            const float32          height = NileCityWorldInternal::computeBuildingHeight( def, building._level, bInhabit );
            const utf8*            pShape = def._kind == CityBuildingKind::Decoration ? "Cylinder" : "Cube";
            GameObject*            pBuilding =
                _stage.createPrimitiveObject( "NileBuilding", pShape, PrimitiveLook::makeColor( NileCityWorldInternal::computeBuildingColor( def, building._level, bInhabit ) ),
                                              float3{ static_cast<float32>( building._origin._x ) + size * 0.5f, height * 0.5f, static_cast<float32>( building._origin._y ) + size * 0.5f },
                                              float3{ size - 0.15f, height, size - 0.15f } );
            view._object     = pBuilding != nullptr ? pBuilding->getHandle() : GameObjectHandle{};
            view._pDef       = building._pDef;
            view._level      = building._level;
            view._bInhabited = bInhabit ? SW_TRUE : SW_FALSE;
        }
    }

    void NileCityWorld::syncWalkers()
    {
        const vector<CityWalker>& listWalker = _city.getWalkers();
        while ( _listWalkerObject.size() < listWalker.size() )
        {
            GameObject* pWalker = _stage.createPrimitiveObject( "NileWalker", "Capsule", PrimitiveLook::makeColor( float4{ 1.0f, 1.0f, 1.0f, 1.0f } ), float3{ 0.0f, -5.0f, 0.0f },
                                                                float3{ 0.3f, 0.3f, 0.3f } );
            if ( pWalker == nullptr )
                return;
            _listWalkerObject.push_back( pWalker->getHandle() );
            _listWalkerLookKey.push_back( -1 );
        }
        for ( size_t walkerIndex = 0; walkerIndex < _listWalkerObject.size(); ++walkerIndex )
        {
            MeshComponent* pMesh = _stage.findMesh( _listWalkerObject[walkerIndex] );
            if ( pMesh == nullptr )
                continue;
            const bool bShown = walkerIndex < listWalker.size();
            pMesh->setVisible( bShown );
            if ( bShown == false )
                continue;
            const CityWalker& walker   = listWalker[walkerIndex];
            const float2      position = CitySimulation::computeWalkerPosition( walker );
            pMesh->setLocalPosition( NileCityWorldInternal::computeTileCenter( position._x, position._y, NileCityWorldInternal::kWalkerHeight ) );
            // 오브젝트 자리는 일꾼 목록의 자리라 다른 일꾼에게 넘어갈 수 있다 — 종류가 바뀐 때만 색을 다시 입힌다.
            const int32 lookKey = static_cast<int32>( walker._kind ) * 16 + static_cast<int32>( walker._service );
            if ( _listWalkerLookKey[walkerIndex] != lookKey )
            {
                _stage.setLook( *pMesh, PrimitiveLook::makeColor( NileCityWorldInternal::computeWalkerColor( walker ) ) );
                _listWalkerLookKey[walkerIndex] = lookKey;
            }
        }
    }

    void NileCityWorld::updateCamera()
    {
        const float3 forward = NileCityWorldInternal::computeCameraForward();
        _stage.placeCameras( _cameraFocus - forward * NileCityWorldInternal::kCameraDistance, _cameraFocus, _cameraHeight, NileCityWorldInternal::kCameraDistance * 2.5f );
    }

    // ------------------------------------------------------------------------------
    // 입력
    // ------------------------------------------------------------------------------
    void NileCityWorld::updateInput( float32 deltaTime, const InputManager& input )
    {
        float3 pan{ 0.0f, 0.0f, 0.0f };
        if ( input.isKeyDown( Key::W ) || input.isKeyDown( Key::Up ) )
            pan._z += 1.0f;
        if ( input.isKeyDown( Key::S ) || input.isKeyDown( Key::Down ) )
            pan._z -= 1.0f;
        if ( input.isKeyDown( Key::D ) || input.isKeyDown( Key::Right ) )
            pan._x += 1.0f;
        if ( input.isKeyDown( Key::A ) || input.isKeyDown( Key::Left ) )
            pan._x -= 1.0f;
        _cameraFocus        = _cameraFocus + pan * ( NileCityWorldInternal::kPanSpeed * _cameraHeight * deltaTime );
        _cameraFocus._x     = MathUtil::clamp( _cameraFocus._x, 0.0f, static_cast<float32>( _city.getWidth() ) );
        _cameraFocus._z     = MathUtil::clamp( _cameraFocus._z, 0.0f, static_cast<float32>( _city.getHeight() ) );
        const float32 wheel = input.getMouseWheel();
        if ( wheel != 0.0f )
            _cameraHeight = MathUtil::clamp( _cameraHeight * ( wheel > 0.0f ? 0.85f : 1.0f / 0.85f ), 10.0f, 70.0f );

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
            SW_LOG_INFO( "[Nile] tool: %# ($%#)", getToolName(), pDef != nullptr ? pDef->_cost : _pCatalog->getRoadCost() );
        }
        if ( input.wasKeyPressed( Key::Space ) )
        {
            _bPaused = _bPaused != SW_FALSE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO( "[Nile] %#", _bPaused != SW_FALSE ? "paused" : "running" );
        }
        if ( input.wasKeyPressed( Key::Minus ) || input.wasKeyPressed( Key::Equal ) )
        {
            _timeScale = MathUtil::clamp( _timeScale * ( input.wasKeyPressed( Key::Equal ) ? 2.0f : 0.5f ), 0.25f, 8.0f );
            SW_LOG_INFO( "[Nile] speed x%#", _timeScale );
        }
        if ( input.wasKeyPressed( Key::P ) )
        {
            _bAutoPlan = _bAutoPlan != SW_FALSE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO( "[Nile] auto plan %# (step %# of %#)", _bAutoPlan != SW_FALSE ? "on" : "off", _planner.getNextStep(), _planner.getStepCount() );
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

    void NileCityWorld::updateCursor( const InputManager& input )
    {
        int2 tile{};
        _bCursorValid          = findCursorTile( input, tile ) ? SW_TRUE : SW_FALSE;
        _cursorTile            = tile;
        MeshComponent* pCursor = _stage.findMesh( _cursorObject );
        if ( pCursor == nullptr )
            return;
        pCursor->setVisible( _bCursorValid != SW_FALSE );
        if ( _bCursorValid == SW_FALSE )
            return;
        const CityBuildingDef* pDef = _listTool[static_cast<size_t>( _selectedTool )];
        const float32          size = pDef != nullptr ? static_cast<float32>( pDef->_size ) : 1.0f;
        pCursor->setLocalPosition( float3{ static_cast<float32>( tile._x ) + size * 0.5f, 0.1f, static_cast<float32>( tile._y ) + size * 0.5f } );
        pCursor->setLocalScale( float3{ size, 0.12f, size } );
    }

    bool NileCityWorld::findCursorTile( const InputManager& input, int2& outTile ) const
    {
        // 직교 카메라 — 화면의 점마다 광선이 카메라 앞 방향과 나란하다. 화면 높이 = `_cameraHeight`, 너비 = 높이 × 화면비.
        const IWindow* pWindow = IWindow::getActiveWindow();
        const float32  aspect  = pWindow != nullptr && pWindow->getHeight() > 0
                                   ? static_cast<float32>( pWindow->getWidth() ) / static_cast<float32>( pWindow->getHeight() )
                                   : 16.0f / 9.0f;
        const float2   mouse   = input.getMousePositionNormalized();
        const float3   forward = NileCityWorldInternal::computeCameraForward();
        const float3   right{ MathUtil::cos( NileCityWorldInternal::kCameraYaw ), 0.0f, -MathUtil::sin( NileCityWorldInternal::kCameraYaw ) };
        const float3   up{ MathUtil::sin( NileCityWorldInternal::kCameraPitch ) * MathUtil::sin( NileCityWorldInternal::kCameraYaw ), MathUtil::cos( NileCityWorldInternal::kCameraPitch ),
                         MathUtil::sin( NileCityWorldInternal::kCameraPitch ) * MathUtil::cos( NileCityWorldInternal::kCameraYaw ) };
        const float32  halfHeight = _cameraHeight * 0.5f;
        GameRay        ray;
        ray._origin = _cameraFocus - forward * NileCityWorldInternal::kCameraDistance + right * ( ( mouse._x * 2.0f - 1.0f ) * halfHeight * aspect ) +
                      up * ( ( 1.0f - mouse._y * 2.0f ) * halfHeight );
        ray._direction   = forward;
        float32 distance = 0.0f;
        if ( RayMath::intersectHorizontalPlane( ray, 0.0f, NileCityWorldInternal::kCameraDistance * 3.0f, distance ) == false )
            return false;
        const float3 point = ray._origin + ray._direction * distance;
        outTile            = int2{ static_cast<int32>( MathUtil::floor( point._x ) ), static_cast<int32>( MathUtil::floor( point._z ) ) };
        return _city.findTile( outTile._x, outTile._y ) != nullptr;
    }

    void NileCityWorld::placeSelected()
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
            SW_LOG_INFO( "[Nile] built %# at (%#, %#) - $%# left", pDef->_name.c_str(), _cursorTile._x, _cursorTile._y, _city.getMoney() );
        else
            SW_LOG_INFO( "[Nile] cannot build %# at (%#, %#): %#", pDef->_name.c_str(), _cursorTile._x, _cursorTile._y, toString( result ) );
    }

    // ------------------------------------------------------------------------------
    // 알림 · 로그
    // ------------------------------------------------------------------------------
    void NileCityWorld::drainEvents()
    {
        _listEvent.clear(); // drainEvents 는 뒤에 붙인다
        _city.drainEvents( _listEvent );
        for ( const CityEvent& event : _listEvent )
        {
            switch ( event._kind )
            {
                case CityEvent::Kind::MonthEnded:
                {
                    ++_monthCount;
                    SW_LOG_INFO( "[Nile] month %# pop %# money %# (net %#, workers %#/%#, houses up %#, avg level %#, culture %#%%)", _monthCount, _city.getPopulation(),
                                 _city.getMoney(), event._value, _city.getEmployed(), _city.getWorkforce(), _evolvedCount, _city.computeAverageHouseLevel(),
                                 static_cast<int32>( _city.computeCultureCoverage() * 100.0f ) );
                    _evolvedCount = 0;
                    break;
                }
                case CityEvent::Kind::HouseEvolved:
                {
                    ++_evolvedCount;
                    break;
                }
                case CityEvent::Kind::Flood:
                {
                    SW_LOG_INFO( "[Nile] year %# - the Nile flooded, farm fertility %#%%", _city.getYear(), event._value );
                    break;
                }
                case CityEvent::Kind::HouseDevolved:
                case CityEvent::Kind::GoodsDelivered:
                    break;
            }
        }
    }

    void NileCityWorld::logStatus() const
    {
        SW_LOG_INFO( "[Nile] year %# month %# · pop %# · money $%# · workers %#/%# · avg house level %# · culture %#%% · fertility %#%% · tool %# · plan %#/%#",
                     _city.getYear(), _city.getMonth() + 1, _city.getPopulation(), _city.getMoney(), _city.getEmployed(), _city.getWorkforce(),
                     _city.computeAverageHouseLevel(), static_cast<int32>( _city.computeCultureCoverage() * 100.0f ), static_cast<int32>( _city.getFloodFertility() * 100.0f ),
                     getToolName(), _planner.getNextStep(), _planner.getStepCount() );
    }

    const utf8* NileCityWorld::getToolName() const
    {
        const CityBuildingDef* pDef = _listTool.empty() ? nullptr : _listTool[static_cast<size_t>( _selectedTool )];
        return pDef != nullptr ? pDef->_name.c_str() : "Road";
    }
} // namespace sw
