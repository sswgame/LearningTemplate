#include "pch.h"

#include "Games/StarSkirmish/SkirmishWorld.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Window/IWindow.h"

#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Utility/RayMath.h"

namespace sw
{
    SW_LOG_CALLER( "SkirmishWorld" );

    namespace
    {
        struct SkirmishWorldInternal
        {
            static constexpr float32 kPi             = 3.14159265f;
            static constexpr float32 kCameraPitch    = 60.0f * kPi / 180.0f;
            static constexpr float32 kCameraYaw      = 0.0f; ///< 북쪽(+Z)을 본다
            static constexpr float32 kCameraDistance = 150.0f;
            static constexpr float32 kPanSpeed       = 1.2f; ///< 화면 높이에 곱한다(초당)
            static constexpr float32 kClickSlop      = 0.6f; ///< 이보다 짧게 끌면 클릭
            static constexpr float32 kAirHeight      = 2.5f;

            static float4 computePlayerColor( int32 owner )
            {
                if ( owner == 0 )
                    return float4{ 0.25f, 0.45f, 0.95f, 1.0f };
                if ( owner == 1 )
                    return float4{ 0.9f, 0.25f, 0.2f, 1.0f };
                return float4{ 0.7f, 0.7f, 0.7f, 1.0f };
            }

            static float4 computeUnitColor( const RtsUnit& unit, bool bSelected )
            {
                float4 color{};
                if ( unit.isResource() )
                    color = unit._pDef->_resourceType == RtsResourceType::Gas ? float4{ 0.3f, 0.8f, 0.4f, 1.0f } : float4{ 0.35f, 0.85f, 1.0f, 1.0f };
                else
                {
                    color = computePlayerColor( unit._owner );
                    if ( unit.isBuilding() )
                        color = float4{ color._x * 0.7f, color._y * 0.7f, color._z * 0.7f, 1.0f };
                    else if ( unit._pDef->_bWorker != SW_FALSE )
                        color = float4{ 0.5f + color._x * 0.5f, 0.5f + color._y * 0.5f, 0.5f + color._z * 0.5f, 1.0f };
                }
                if ( bSelected )
                    color = float4{ 0.5f + color._x * 0.5f, 0.5f + color._y * 0.5f, 0.5f + color._z * 0.5f, 1.0f };
                return color;
            }

            static const utf8* findShape( const RtsUnit& unit )
            {
                if ( unit.isResource() )
                    return unit._pDef->_resourceType == RtsResourceType::Gas ? "Cylinder" : "Cube";
                if ( unit.isBuilding() )
                    return "Cube";
                return unit._pDef->_bWorker != SW_FALSE ? "Sphere" : "Capsule";
            }

            /** @brief 유닛의 크기 · 자리입니다(건물은 지은 만큼 솟고, 광물은 남은 만큼 낮아진다). */
            static void computePlacement( const RtsUnit& unit, float3& outPosition, float3& outScale )
            {
                const RtsUnitDef& def = *unit._pDef;
                if ( unit.isResource() )
                {
                    const float32 left   = def._resourceAmount > 0 ? static_cast<float32>( unit._resourceLeft ) / static_cast<float32>( def._resourceAmount ) : 1.0f;
                    const float32 height = def._resourceType == RtsResourceType::Gas ? 0.4f : 0.3f + 0.6f * left;
                    const float32 width  = static_cast<float32>( def._footprint ) * 0.8f;
                    outScale             = float3{ width, height, width };
                    outPosition          = float3{ unit._position._x, height * 0.5f, unit._position._z };
                    return;
                }
                if ( unit.isBuilding() )
                {
                    const float32 fullHeight = 0.6f + 0.35f * static_cast<float32>( def._footprint );
                    const float32 height     = fullHeight * ( 0.2f + 0.8f * MathUtil::clamp( unit._buildProgress, 0.0f, 1.0f ) );
                    const float32 width      = static_cast<float32>( def._footprint ) - 0.2f;
                    outScale                 = float3{ width, height, width };
                    outPosition              = float3{ unit._position._x, height * 0.5f, unit._position._z };
                    return;
                }
                const float32 size = def._radius * 2.0f;
                outScale           = float3{ size, size, size };
                outPosition        = float3{ unit._position._x, ( def._bAir != SW_FALSE ? kAirHeight : 0.0f ) + size * 0.5f, unit._position._z };
            }

            static float3 computeCameraForward() { return RayMath::computeLookDirection( kCameraYaw, -kCameraPitch ); }
        };
    } // namespace

    /**
     * @brief `-gv_skirmishAutoPlay=1` — 두 플레이어 모두 AI(러시 대 운영)로 한 판을 끝까지 돌립니다(입력 없이 승패까지 가는 확인 · 화면 녹화용).
     * @details 배포본으로도 돌릴 수 있게 남긴다: `App -gv_skirmishAutoPlay=1`. 30 초마다 `[Skirmish] t=.. p0 workers .. army ..` 와 끝에 승패가 로그에 남는다.
     */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_skirmishAutoPlay, 0, "StarSkirmish: 두 플레이어 모두 AI 로 돌리기 (1=켜기)", SW_KEEP_IN_SHIPPING );
} // namespace sw

namespace sw
{
    SkirmishWorld::SkirmishWorld()
        : _stage{}
        , _match{}
        , _selection{}
        , _listUnitView{}
        , _listEvent{}
        , _pCatalog{ nullptr }
        , _dragObject{}
        , _cameraFocus{ 14.0f, 0.0f, 14.0f }
        , _dragStart{}
        , _cameraHeight{ 30.0f }
        , _timeScale{ 1.0f }
        , _frameStamp{ 0 }
        , _bHuman{ SW_TRUE }
        , _bDragging{ SW_FALSE }
        , _bAttackMovePending{ SW_FALSE }
        , _bPaused{ SW_FALSE }
        , _bSpawned{ SW_FALSE }
    {
    }

    SkirmishWorld::~SkirmishWorld() = default;

    void SkirmishWorld::initialize( const RtsCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _bHuman   = gv_skirmishAutoPlay == 0 ? SW_TRUE : SW_FALSE;
        _match.initialize( pCatalog, _bHuman != SW_FALSE );
        _selection.clear();
        _selection.setPlayer( 0 );
        _selection.setMaxCount( 12 );
        _listEvent.clear();
        if ( _bHuman == SW_FALSE )
        {
            // 둘 다 AI — 맵 전체가 보이게.
            _cameraFocus  = float3{ 32.0f, 0.0f, 30.0f };
            _cameraHeight = 70.0f;
        }
    }

    bool SkirmishWorld::spawn()
    {
        if ( _bSpawned != SW_FALSE )
            return true;
        if ( _pCatalog == nullptr || _stage.begin( "StarSkirmish" ) == false )
            return false;
        spawnTerrain();
        (void)_stage.createSun( float3{ 0.95f, 0.5f, 0.0f }, 1.5f, 80.0f );
        _listUnitView.clear();
        GameObject* pDrag = _stage.createPrimitiveObject( "SkirmishDrag", "Cube", PrimitiveLook::makeTranslucent( float4{ 0.3f, 1.0f, 0.4f, 0.3f } ), float3{ 0.0f, -5.0f, 0.0f },
                                                          float3{ 1.0f, 0.05f, 1.0f } );
        _dragObject       = pDrag != nullptr ? pDrag->getHandle() : GameObjectHandle{};
        _bSpawned         = SW_TRUE;
        syncUnits();
        updateCamera();
        if ( _bHuman != SW_FALSE )
        {
            SW_LOG_INFO( "[Skirmish] you are blue - drag to select, right click to move/attack/gather, A attack-move, S stop, H hold, Q/W/E train, "
                         "B depot · N barracks · G refinery · Y academy · F factory · T starport · U bunker at the cursor, Ctrl+1..9 groups, arrows pan, "
                         "wheel zoom, - = speed, P pause, F1 status" );
        }
        else
            SW_LOG_INFO( "[Skirmish] watching computer vs computer - arrows/WASD pan, wheel zoom, - = speed, P pause, F1 status" );
        return true;
    }

    void SkirmishWorld::despawn()
    {
        _stage.clear();
        _listUnitView.clear();
        _dragObject = GameObjectHandle{};
        _bDragging  = SW_FALSE;
        _bSpawned   = SW_FALSE;
    }

    void SkirmishWorld::update( float32 deltaTime )
    {
        if ( _stage.isSceneChanged() )
        {
            _stage.forget();
            _listUnitView.clear();
            _dragObject = GameObjectHandle{};
            _bDragging  = SW_FALSE;
            _bSpawned   = SW_FALSE;
        }
        if ( _bSpawned == SW_FALSE && spawn() == false )
            return;
        if ( deltaTime <= 0.0f )
            return;

        const InputManager* pInput = game::getService<InputManager>();
        if ( pInput != nullptr )
            updateInput( deltaTime, *pInput );
        if ( _bPaused == SW_FALSE )
            _match.update( deltaTime * _timeScale );
        handleEvents();
        _selection.prune( _match.getWorld() );
        syncUnits();
        updateCamera();
    }

    // ------------------------------------------------------------------------------
    // 모습
    // ------------------------------------------------------------------------------
    void SkirmishWorld::spawnTerrain()
    {
        const int32 size = SkirmishMatch::kMapSize;
        (void)_stage.createPrimitiveObject( "SkirmishGround", "Cube", PrimitiveLook::makeColor( float4{ 0.42f, 0.4f, 0.36f, 1.0f } ),
                                            float3{ static_cast<float32>( size ) * 0.5f, -0.1f, static_cast<float32>( size ) * 0.5f },
                                            float3{ static_cast<float32>( size ), 0.2f, static_cast<float32>( size ) } );
        // 절벽 — 줄마다 이어진 칸을 상자 하나로.
        const PrimitiveLook cliffLook = PrimitiveLook::makeColor( float4{ 0.3f, 0.24f, 0.18f, 1.0f } );
        for ( int32 y = 0; y < size; ++y )
        {
            int32 runStart = -1;
            for ( int32 x = 0; x <= size; ++x )
            {
                const bool bCliff = x < size && _match.isCliff( x, y );
                if ( bCliff && runStart < 0 )
                    runStart = x;
                if ( bCliff || runStart < 0 )
                    continue;
                const float32 length = static_cast<float32>( x - runStart );
                (void)_stage.createPrimitiveObject( "SkirmishCliff", "Cube", cliffLook, float3{ static_cast<float32>( runStart ) + length * 0.5f, 0.75f, static_cast<float32>( y ) + 0.5f },
                                                    float3{ length, 1.5f, 1.0f } );
                runStart = -1;
            }
        }
    }

    void SkirmishWorld::syncUnits()
    {
        ++_frameStamp;
        const RtsWorld& world  = _match.getWorld();
        const bool      bHuman = _bHuman != SW_FALSE;
        world.forEachUnit( [&]( const RtsUnit& unit )
        {
            // 사람 쪽 화면 — 안 보이는 적은 그리지 않는다(자원은 늘 보인다).
            if ( bHuman && unit._owner != 0 && unit._owner != RtsWorld::kNoOwner && world.isVisibleTo( 0, unit._id ) == false )
                return;
            const size_t slot = static_cast<size_t>( unit._id.index() );
            if ( _listUnitView.size() <= slot )
                _listUnitView.resize( slot + 1 );
            UnitView&   view      = _listUnitView[slot];
            const bool  bSelected = _selection.isSelected( unit._id );
            const int32 lookKey   = bSelected ? 1 : 0;
            float3      position{};
            float3      scale{};
            SkirmishWorldInternal::computePlacement( unit, position, scale );
            if ( view._id != unit._id || view._object.isValid() == false )
            {
                if ( view._object.isValid() )
                    _stage.destroyObject( view._object );
                GameObject* pObject = _stage.createPrimitiveObject( "SkirmishUnit", SkirmishWorldInternal::findShape( unit ),
                                                                    PrimitiveLook::makeColor( SkirmishWorldInternal::computeUnitColor( unit, bSelected ) ), position, scale );
                view._id            = unit._id;
                view._object        = pObject != nullptr ? pObject->getHandle() : GameObjectHandle{};
                view._lookKey       = lookKey;
            }
            view._stamp          = _frameStamp;
            MeshComponent* pMesh = _stage.findMesh( view._object );
            if ( pMesh == nullptr )
                return;
            pMesh->setLocalPosition( position );
            pMesh->setLocalScale( scale );
            if ( view._lookKey != lookKey )
            {
                _stage.setLook( *pMesh, PrimitiveLook::makeColor( SkirmishWorldInternal::computeUnitColor( unit, bSelected ) ) );
                view._lookKey = lookKey;
            }
        } );
        // 이번에 못 본 자리(죽음 · 안개)는 지운다.
        for ( UnitView& view : _listUnitView )
        {
            if ( view._stamp == _frameStamp || view._object.isValid() == false )
                continue;
            _stage.destroyObject( view._object );
            view = UnitView{};
        }
    }

    void SkirmishWorld::updateCamera()
    {
        const float3 forward = SkirmishWorldInternal::computeCameraForward();
        _stage.placeCameras( _cameraFocus - forward * SkirmishWorldInternal::kCameraDistance, _cameraFocus, _cameraHeight, SkirmishWorldInternal::kCameraDistance * 2.5f );
    }

    // ------------------------------------------------------------------------------
    // 입력
    // ------------------------------------------------------------------------------
    void SkirmishWorld::updateInput( float32 deltaTime, const InputManager& input )
    {
        const bool bWasd = _bHuman == SW_FALSE; // 사람 쪽은 글자 키가 명령이다
        float3     pan{ 0.0f, 0.0f, 0.0f };
        if ( input.isKeyDown( Key::Up ) || ( bWasd && input.isKeyDown( Key::W ) ) )
            pan._z += 1.0f;
        if ( input.isKeyDown( Key::Down ) || ( bWasd && input.isKeyDown( Key::S ) ) )
            pan._z -= 1.0f;
        if ( input.isKeyDown( Key::Right ) || ( bWasd && input.isKeyDown( Key::D ) ) )
            pan._x += 1.0f;
        if ( input.isKeyDown( Key::Left ) || ( bWasd && input.isKeyDown( Key::A ) ) )
            pan._x -= 1.0f;
        const float32 mapSize = static_cast<float32>( SkirmishMatch::kMapSize );
        _cameraFocus          = _cameraFocus + pan * ( SkirmishWorldInternal::kPanSpeed * _cameraHeight * deltaTime );
        _cameraFocus._x       = MathUtil::clamp( _cameraFocus._x, 0.0f, mapSize );
        _cameraFocus._z       = MathUtil::clamp( _cameraFocus._z, 0.0f, mapSize );
        const float32 wheel   = input.getMouseWheel();
        if ( wheel != 0.0f )
            _cameraHeight = MathUtil::clamp( _cameraHeight * ( wheel > 0.0f ? 0.85f : 1.0f / 0.85f ), 12.0f, 90.0f );

        if ( input.wasKeyPressed( Key::Minus ) || input.wasKeyPressed( Key::Equal ) )
        {
            _timeScale = MathUtil::clamp( _timeScale * ( input.wasKeyPressed( Key::Equal ) ? 2.0f : 0.5f ), 0.25f, 8.0f );
            SW_LOG_INFO( "[Skirmish] speed x%#", _timeScale );
        }
        if ( input.wasKeyPressed( Key::P ) )
        {
            _bPaused = _bPaused != SW_FALSE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO( "[Skirmish] %#", _bPaused != SW_FALSE ? "paused" : "running" );
        }
        if ( input.wasKeyPressed( Key::F1 ) )
            _match.logStatus();
        if ( _bHuman != SW_FALSE && _match.isOver() == false )
            updateHumanCommands( input );
    }

    void SkirmishWorld::updateHumanCommands( const InputManager& input )
    {
        RtsWorld&  world = _match.getWorld();
        float3     point{};
        const bool bPointValid = findGroundPoint( input, point );
        const bool bShift      = input.isKeyDown( Key::LeftShift ) || input.isKeyDown( Key::RightShift );
        const bool bControl    = input.isKeyDown( Key::LeftControl ) || input.isKeyDown( Key::RightControl );
        updateDrag( input, point, bPointValid );

        if ( bPointValid && input.wasMouseButtonPressed( MouseButton::Right ) )
            issueRightClick( point, bShift );

        // 명령 단축키 — 고른 것이 모두 내 것일 때만.
        if ( _selection.isCommandable( world ) && _selection.getSelected().empty() == false )
        {
            if ( input.wasKeyPressed( Key::A ) )
            {
                _bAttackMovePending = SW_TRUE;
                SW_LOG_INFO( "[Skirmish] attack-move - left click a target point" );
            }
            if ( input.wasKeyPressed( Key::S ) || input.wasKeyPressed( Key::H ) )
            {
                const bool bHold = input.wasKeyPressed( Key::H );
                for ( const RtsUnitId unitId : _selection.getSelected() )
                    (void)( bHold ? world.issueHold( unitId ) : world.issueStop( unitId ) );
            }
            if ( input.wasKeyPressed( Key::Q ) )
                trainFromPrimary( 0 );
            if ( input.wasKeyPressed( Key::W ) )
                trainFromPrimary( 1 );
            if ( input.wasKeyPressed( Key::E ) )
                trainFromPrimary( 2 );
            if ( bPointValid )
            {
                struct BuildKey
                {
                    Key         _key;
                    const utf8* _pBuildingId;
                };
                constexpr BuildKey kArrBuildKey[] = {
                    {Key::B, "supply_depot"},
                    {Key::N,     "barracks"},
                    {Key::G,     "refinery"},
                    {Key::Y,      "academy"},
                    {Key::F,      "factory"},
                    {Key::T,     "starport"},
                    {Key::U,       "bunker"},
                };
                for ( const BuildKey& entry : kArrBuildKey )
                {
                    if ( input.wasKeyPressed( entry._key ) )
                        orderBuild( entry._pBuildingId, point );
                }
            }
        }

        // 부대 — Ctrl + 숫자 정하기, Shift + 숫자 더하기, 숫자 부르기.
        for ( int32 group = 0; group < RtsSelection::kGroupCount; ++group )
        {
            const Key key = static_cast<Key>( static_cast<int32>( Key::Digit0 ) + group );
            if ( input.wasKeyPressed( key ) == false )
                continue;
            if ( bControl )
                _selection.assignGroup( group );
            else if ( bShift )
                _selection.addToGroup( group );
            else if ( _selection.recallGroup( world, group ) )
                SW_LOG_INFO( "[Skirmish] group %# - %# units", group, static_cast<int32>( _selection.getSelected().size() ) );
        }
        if ( input.wasKeyPressed( Key::Space ) )
        {
            const RtsUnit* pPrimary = world.findUnit( _selection.getPrimary() );
            if ( pPrimary != nullptr )
                _cameraFocus = float3{ pPrimary->_position._x, 0.0f, pPrimary->_position._z };
        }
    }

    void SkirmishWorld::updateDrag( const InputManager& input, const float3& point, bool bPointValid )
    {
        RtsWorld&  world  = _match.getWorld();
        const bool bShift = input.isKeyDown( Key::LeftShift ) || input.isKeyDown( Key::RightShift );
        if ( bPointValid && input.wasMouseButtonPressed( MouseButton::Left ) )
        {
            if ( _bAttackMovePending != SW_FALSE )
            {
                // A 다음 왼쪽 클릭 — 고른 병력을 그 자리로 공격 이동.
                _bAttackMovePending                = SW_FALSE;
                [[maybe_unused]] const int32 count = world.issueGroupMove( _selection.getSelected(), point, true, bShift );
                SW_LOG_INFO( "[Skirmish] %# units attack-move to (%#, %#)", count, static_cast<int32>( point._x ), static_cast<int32>( point._z ) );
                return;
            }
            _bDragging = SW_TRUE;
            _dragStart = point;
        }
        MeshComponent* pDragMesh = _stage.findMesh( _dragObject );
        if ( _bDragging != SW_FALSE && bPointValid && pDragMesh != nullptr )
        {
            pDragMesh->setVisible( true );
            pDragMesh->setLocalPosition( float3{ ( _dragStart._x + point._x ) * 0.5f, 0.05f, ( _dragStart._z + point._z ) * 0.5f } );
            pDragMesh->setLocalScale( float3{ MathUtil::abs( point._x - _dragStart._x ) + 0.05f, 0.05f, MathUtil::abs( point._z - _dragStart._z ) + 0.05f } );
        }
        if ( _bDragging == SW_FALSE || input.isMouseButtonDown( MouseButton::Left ) )
            return;

        // 놓았다 — 짧으면 클릭(그 자리 유닛 하나), 길면 사각형.
        _bDragging = SW_FALSE;
        if ( pDragMesh != nullptr )
            pDragMesh->setVisible( false );
        const float3  end     = bPointValid ? point : _dragStart;
        const float32 dragged = MathUtil::max( MathUtil::abs( end._x - _dragStart._x ), MathUtil::abs( end._z - _dragStart._z ) );
        if ( dragged < SkirmishWorldInternal::kClickSlop )
        {
            const RtsUnitId pickedId = world.pickUnit( end );
            if ( pickedId.isValid() )
                _selection.selectUnit( world, pickedId, bShift );
            else if ( bShift == false )
                _selection.clear();
        }
        else
            _selection.selectInRect( world, _dragStart, end, bShift );
        const RtsUnit* pPrimary = world.findUnit( _selection.getPrimary() );
        if ( pPrimary != nullptr )
            SW_LOG_INFO( "[Skirmish] selected %# (%# units)", pPrimary->_pDef->_name.c_str(), static_cast<int32>( _selection.getSelected().size() ) );
    }

    void SkirmishWorld::issueRightClick( const float3& point, bool bQueue )
    {
        RtsWorld& world = _match.getWorld();
        if ( _selection.getSelected().empty() || _selection.isCommandable( world ) == false )
            return;
        // 유닛 · 자원을 눌렀으면 하나씩 똑똑한 명령(공격 · 채취 · 이어 짓기), 빈 땅이면 움직이는 것은 무리 이동 · 건물은 집결지.
        const RtsUnitId   targetId = world.pickUnit( point );
        vector<RtsUnitId> listMobile;
        for ( const RtsUnitId unitId : _selection.getSelected() )
        {
            const RtsUnit* pUnit = world.findUnit( unitId );
            if ( pUnit == nullptr )
                continue;
            if ( pUnit->isBuilding() )
                world.setRallyPoint( unitId, point );
            else if ( targetId.isValid() && targetId != unitId )
                (void)world.issueSmart( unitId, point, targetId, bQueue );
            else
                listMobile.push_back( unitId );
        }
        if ( listMobile.empty() == false )
            (void)world.issueGroupMove( listMobile, point, false, bQueue );
    }

    void SkirmishWorld::orderBuild( const utf8* pBuildingId, const float3& point )
    {
        RtsWorld&         world = _match.getWorld();
        const RtsUnitDef* pDef  = _pCatalog->findUnit( hashed_string( pBuildingId ) );
        if ( pDef == nullptr )
            return;
        // 고른 것 중 첫 일꾼이 짓는다. 정제소는 커서 가까운 빈 간헐천, 다른 건물은 커서가 가운데가 되는 자리.
        RtsUnitId workerId{};
        for ( const RtsUnitId unitId : _selection.getSelected() )
        {
            const RtsUnit* pUnit = world.findUnit( unitId );
            if ( workerId.isValid() == false && pUnit != nullptr && pUnit->_pDef->_bWorker != SW_FALSE )
                workerId = unitId;
        }
        if ( workerId.isValid() == false )
        {
            SW_LOG_INFO( "[Skirmish] select a worker to build %#", pDef->_name.c_str() );
            return;
        }
        int2 cell{ static_cast<int32>( MathUtil::floor( point._x ) ) - pDef->_footprint / 2, static_cast<int32>( MathUtil::floor( point._z ) ) - pDef->_footprint / 2 };
        if ( pDef->_bExtractor != SW_FALSE && world.findBuildSite( pDef->_id, point, 0, 8, cell ) == false )
        {
            SW_LOG_INFO( "[Skirmish] no free geyser near the cursor for %#", pDef->_name.c_str() );
            return;
        }
        [[maybe_unused]] const RtsCommandResult result = world.issueBuild( workerId, pDef->_id, cell );
        SW_LOG_INFO( "[Skirmish] build %# at (%#, %#): %#", pDef->_name.c_str(), cell._x, cell._y, toString( result ) );
    }

    void SkirmishWorld::trainFromPrimary( int32 productIndex )
    {
        RtsWorld&      world    = _match.getWorld();
        const RtsUnit* pPrimary = world.findUnit( _selection.getPrimary() );
        if ( pPrimary == nullptr || pPrimary->isBuilding() == false )
            return;
        vector<const RtsUnitDef*> listProduct;
        _pCatalog->findProducts( pPrimary->_pDef->_id, listProduct );
        if ( productIndex >= static_cast<int32>( listProduct.size() ) )
            return;
        const RtsUnitDef&                       product = *listProduct[static_cast<size_t>( productIndex )];
        [[maybe_unused]] const RtsCommandResult result  = world.train( pPrimary->_id, product._id );
        SW_LOG_INFO( "[Skirmish] train %# at %#: %#", product._name.c_str(), pPrimary->_pDef->_name.c_str(), toString( result ) );
    }

    bool SkirmishWorld::findGroundPoint( const InputManager& input, float3& outPoint ) const
    {
        // 직교 카메라 — 화면의 점마다 광선이 카메라 앞 방향과 나란하다. 화면 높이 = `_cameraHeight`, 너비 = 높이 × 화면비.
        const IWindow* pWindow = IWindow::getActiveWindow();
        const float32  aspect  = pWindow != nullptr && pWindow->getHeight() > 0
                                   ? static_cast<float32>( pWindow->getWidth() ) / static_cast<float32>( pWindow->getHeight() )
                                   : 16.0f / 9.0f;
        const float2   mouse   = input.getMousePositionNormalized();
        const float3   forward = SkirmishWorldInternal::computeCameraForward();
        const float3   right{ MathUtil::cos( SkirmishWorldInternal::kCameraYaw ), 0.0f, -MathUtil::sin( SkirmishWorldInternal::kCameraYaw ) };
        const float3   up{ MathUtil::sin( SkirmishWorldInternal::kCameraPitch ) * MathUtil::sin( SkirmishWorldInternal::kCameraYaw ), MathUtil::cos( SkirmishWorldInternal::kCameraPitch ),
                         MathUtil::sin( SkirmishWorldInternal::kCameraPitch ) * MathUtil::cos( SkirmishWorldInternal::kCameraYaw ) };
        const float32  halfHeight = _cameraHeight * 0.5f;
        GameRay        ray;
        ray._origin = _cameraFocus - forward * SkirmishWorldInternal::kCameraDistance + right * ( ( mouse._x * 2.0f - 1.0f ) * halfHeight * aspect ) +
                      up * ( ( 1.0f - mouse._y * 2.0f ) * halfHeight );
        ray._direction   = forward;
        float32 distance = 0.0f;
        if ( RayMath::intersectHorizontalPlane( ray, 0.0f, SkirmishWorldInternal::kCameraDistance * 3.0f, distance ) == false )
            return false;
        outPoint              = ray._origin + ray._direction * distance;
        const float32 mapSize = static_cast<float32>( SkirmishMatch::kMapSize );
        return outPoint._x >= 0.0f && outPoint._z >= 0.0f && outPoint._x < mapSize && outPoint._z < mapSize;
    }

    // ------------------------------------------------------------------------------
    // 알림
    // ------------------------------------------------------------------------------
    void SkirmishWorld::handleEvents()
    {
        _listEvent.clear(); // drainEvents 는 뒤에 붙인다
        _match.drainEvents( _listEvent );
        if ( _bHuman == SW_FALSE )
            return;
        // 사람 쪽(0 번)에 알릴 것만 — 나머지는 판이 로그로 남긴다.
        for ( const RtsEvent& event : _listEvent )
        {
            if ( event._player != 0 )
                continue;
            const RtsUnitDef*            pDef  = _pCatalog->findUnit( event._defId );
            [[maybe_unused]] const utf8* pName = pDef != nullptr ? pDef->_name.c_str() : "?";
            switch ( event._kind )
            {
                case RtsEvent::Kind::ConstructionComplete:
                {
                    SW_LOG_INFO( "[Skirmish] %# complete", pName );
                    break;
                }
                case RtsEvent::Kind::ProductionComplete:
                {
                    SW_LOG_INFO( "[Skirmish] %# ready", pName );
                    break;
                }
                case RtsEvent::Kind::SupplyBlocked:
                {
                    SW_LOG_INFO( "[Skirmish] not enough supply - build a Supply Depot (B)" );
                    break;
                }
                case RtsEvent::Kind::UnderAttack:
                {
                    SW_LOG_INFO( "[Skirmish] we are under attack at (%#, %#)", static_cast<int32>( event._position._x ), static_cast<int32>( event._position._z ) );
                    break;
                }
                case RtsEvent::Kind::UnitCreated:
                case RtsEvent::Kind::UnitDied:
                case RtsEvent::Kind::ResourceDepleted:
                case RtsEvent::Kind::ResourcesDeposited:
                case RtsEvent::Kind::PlayerDefeated:
                case RtsEvent::Kind::GameOver:
                {
                    break;
                }
            }
        }
    }
} // namespace sw
