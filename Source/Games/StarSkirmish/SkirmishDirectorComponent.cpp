#include "pch.h"

#include "Games/StarSkirmish/SkirmishDirectorComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/GameAutoplay.h"
#include "Engine/Window/IWindow.h"

#include "GameFramework/Components/OrthoCameraRigComponent.h"
#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Utility/StateArchiveUtil.h"

#include "Games/StarSkirmish/SkirmishUnitComponent.h"

namespace sw
{
    SW_LOG_CALLER( "SkirmishDirector" );

    namespace
    {
        struct SkirmishDirectorComponentInternal
        {
            static constexpr float32 kClickSlop        = 0.6f;        ///< 이보다 짧게 끌면 클릭
            static constexpr int32   kUnitLookCategory = 5;           ///< 0 번 · 1 번 · 주인 없음 · 광물 · 가스
            static constexpr uint32  kStateTag         = 0x534D5452u; ///< 'RTMS'
            static constexpr uint32  kStateVersion     = 1;

            // 사운드 이벤트 이름(starskirmish.audioevents.xml).
            static constexpr const utf8* kSoundSelect   = "Select";
            static constexpr const utf8* kSoundBuilt    = "Built";
            static constexpr const utf8* kSoundBlocked  = "Blocked";
            static constexpr const utf8* kSoundUnitDied = "UnitDied";

            /** @brief 모습 칸 — 편(0 · 1 · 그 밖)이거나 자원(광물 · 가스)입니다. */
            static int32 computeLookCategory( const RtsUnit& unit )
            {
                if ( unit.isResource() )
                    return unit._pDef->_resourceType == RtsResourceType::Gas ? 4 : 3;
                if ( unit._owner == 0 || unit._owner == 1 )
                    return unit._owner;
                return 2;
            }

            /**
             * @brief 칸의 모델에 곱할 색입니다 — 키트의 흰 · 회색 몸체가 편 색(파랑 · 빨강)으로, 자원은 광물 하늘색 · 가스 초록으로 읽힌다. 고르면 흰 쪽으로 반.
             * @details 건물 · 일꾼을 어둡게 · 밝게 가르던 것은 모델이 대신하므로 편 색을 그대로 곱한다(곱하면 이미 어두워진다).
             */
            static float4 computeLookColor( int32 category, bool bSelected )
            {
                constexpr float4 kArrCategoryColor[kUnitLookCategory] = {
                    {0.25f, 0.45f, 0.95f, 1.0f},
                    { 0.9f, 0.25f,  0.2f, 1.0f},
                    { 0.7f,  0.7f,  0.7f, 1.0f},
                    {0.35f, 0.85f,  1.0f, 1.0f},
                    { 0.3f,  0.8f,  0.4f, 1.0f},
                };
                const float4 color = kArrCategoryColor[MathUtil::clamp( category, 0, kUnitLookCategory - 1 )];
                if ( bSelected )
                    return float4{ 0.5f + color._x * 0.5f, 0.5f + color._y * 0.5f, 0.5f + color._z * 0.5f, 1.0f };
                return color;
            }
        };
    } // namespace

    /**
     * @brief `-gv_skirmishAutoPlay=1` — 디렉터를 컴퓨터 대 컴퓨터(러시 대 운영)로 엽니다(씬의 `_bAutoPlay` 가 꺼져 있어도). 입력 없이 승패까지 가는 확인 · 화면 녹화용.
     * @details 배포본으로도 돌릴 수 있게 남긴다: `App -gv_skirmishAutoPlay=1`. 30 초마다 `[Skirmish] t=.. p0 workers .. army ..` 와 끝에 승패가 로그에 남는다.
     */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_skirmishAutoPlay, 0, "StarSkirmish: 두 플레이어 모두 AI 로 돌리기 (1=켜기)", SW_KEEP_IN_SHIPPING );
    SW_GAME_AUTOPLAY( gv_skirmishAutoPlay, "StarSkirmish", "Both players are AI" );
} // namespace sw

namespace sw
{
    SkirmishDirectorComponent::SkirmishDirectorComponent()
        : _unitDataPath{ "game/starskirmish/data/units.xml" }
        , _cliffPrefab{}
        , _unitPrefab{}
        , _cameraRig{}
        , _watchFocus{ 32.0f, 0.0f, 30.0f }
        , _watchOrthoHeight{ 70.0f }
        , _catalog{}
        , _match{}
        , _selection{}
        , _listEvent{}
        , _listUnitSlot{}
        , _listPendingUnit{}
        , _tintCache{}
        , _listUnitLook{}
        , _dragStart{}
        , _dragPoint{}
        , _timeScale{ 1.0f }
        , _frameStamp{ 0 }
        , _bHuman{ SW_TRUE }
        , _bDragging{ SW_FALSE }
        , _bDragPointValid{ SW_FALSE }
        , _bAttackMovePending{ SW_FALSE }
        , _bPaused{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SkirmishDirectorComponent::~SkirmishDirectorComponent() = default;

    void SkirmishDirectorComponent::onGameStarted()
    {
        // 사람 쪽은 글자 키가 명령이라 카메라는 방향키만. 둘 다 AI 면 맵 전체가 보이게 물러서고 WASD 도 카메라다.
        OrthoCameraRigComponent* pRig = findCameraRig();
        if ( pRig != nullptr )
        {
            pRig->setWasdPanEnabled( _bHuman == SW_FALSE );
            if ( _bHuman == SW_FALSE )
            {
                pRig->setFocus( _watchFocus );
                pRig->setOrthoHeight( _watchOrthoHeight );
                pRig->applyToCamera();
            }
        }
        if ( _bHuman == SW_TRUE )
        {
            SW_LOG_INFO( "[Skirmish] you are blue - drag to select, right click to move/attack/gather, A attack-move, S stop, H hold, Q/W/E train, "
                         "B depot · N barracks · G refinery · Y academy · F factory · T starport · U bunker at the cursor, Ctrl+1..9 groups, arrows pan, "
                         "wheel zoom, - = speed, P pause, F1 status" );
        }
        else
            SW_LOG_INFO( "[Skirmish] watching computer vs computer - arrows/WASD pan, wheel zoom, - = speed, P pause, F1 status" );
    }

    void SkirmishDirectorComponent::tickGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        const InputManager* pInput = game::getService<InputManager>();
        if ( pInput != nullptr )
            updateInput( *pInput );
        if ( _bPaused == SW_FALSE )
            _match.update( deltaTime * _timeScale );
        handleEvents();
        _selection.prune( _match.getWorld() );
        if ( areViewsSpawned() )
            collectUnitChanges();
    }

    void SkirmishDirectorComponent::onViewsDespawned()
    {
        _listUnitSlot.clear();
        _listPendingUnit.clear();
        _bDragging = SW_FALSE;
    }

    const shared_ptr<MaterialInstance>& SkirmishDirectorComponent::findUnitLook( const RtsUnit& unit, bool bSelected ) const
    {
        static const shared_ptr<MaterialInstance> kNoLook{};
        const int32                               lookIndex = SkirmishDirectorComponentInternal::computeLookCategory( unit ) * 2 + ( bSelected ? 1 : 0 );
        if ( lookIndex >= static_cast<int32>( _listUnitLook.size() ) )
            return kNoLook;
        // 뷰가 워커에서 부른다 — 컨테이너 첨자 대신 포인터로 읽는다.
        return _listUnitLook.data()[lookIndex];
    }

    bool SkirmishDirectorComponent::findDragBox( float3& outCenter, float3& outScale ) const
    {
        if ( _bDragging == SW_FALSE || _bDragPointValid == SW_FALSE )
            return false;
        outCenter = float3{ ( _dragStart._x + _dragPoint._x ) * 0.5f, 0.05f, ( _dragStart._z + _dragPoint._z ) * 0.5f };
        outScale  = float3{ MathUtil::abs( _dragPoint._x - _dragStart._x ) + 0.05f, 0.05f, MathUtil::abs( _dragPoint._z - _dragStart._z ) + 0.05f };
        return true;
    }

    // ------------------------------------------------------------------------------
    // 데이터
    // ------------------------------------------------------------------------------
    void SkirmishDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, SkirmishDirectorComponentInternal::kStateTag, SkirmishDirectorComponentInternal::kStateVersion );
        _match.writeState( outArchive );
        _selection.writeState( outArchive );
        outArchive << _timeScale;
        outArchive << static_cast<uint8>( _bPaused );
    }

    bool SkirmishDirectorComponent::readState( Archive& archive )
    {
        if ( StateArchiveUtil::readHeader( archive, SkirmishDirectorComponentInternal::kStateTag, SkirmishDirectorComponentInternal::kStateVersion ) == false )
            return false;
        if ( _match.readState( archive ) == false || _selection.readState( archive ) == false )
            return false;
        float32 timeScale = 1.0f;
        uint8   bPaused   = SW_FALSE;
        archive >> timeScale;
        archive >> bPaused;
        if ( archive.isError() || archive.getRemainingBytes() != 0 || timeScale <= 0.0f )
            return false;
        _timeScale = timeScale;
        _bPaused   = bPaused != SW_FALSE ? SW_TRUE : SW_FALSE;
        _listEvent.clear();
        return true;
    }

    void SkirmishDirectorComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
        {
            SW_LOG_INFO( "[Skirmish] match state restored - %#s in", static_cast<int32>( _match.getWorld().getTime() ) );
        }
        else
        {
            // 판이 반쯤 바뀌었을 수 있다 — 새 판으로 되돌린다.
            SW_LOG_WARNING( "[Skirmish] the saved match state does not match this build - starting a new match" );
            (void)startGame();
        }
    }

    bool SkirmishDirectorComponent::startGame()
    {
        if ( _catalog.loadFromResource( _unitDataPath ) == false )
        {
            SW_LOG_WARNING( "[Skirmish] %# could not be loaded - the match cannot start", _unitDataPath.c_str() );
            return false;
        }
        _bHuman = isAutoPlayOn() ? SW_FALSE : SW_TRUE;
        _match.initialize( &_catalog, _bHuman == SW_TRUE );
        _selection.clear();
        _selection.setPlayer( 0 );
        _selection.setMaxCount( 12 );
        _listEvent.clear();
        return true;
    }

    // ------------------------------------------------------------------------------
    // 스폰(틱 뒤 · 게임 스레드)
    // ------------------------------------------------------------------------------
    void SkirmishDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews )
        {
            // 처음(또는 걷은 뒤) — 절벽과 지금 보이는 유닛 모두.
            spawnCliffs( manager );
            _listPendingUnit.clear();
            collectUnitChanges();
        }
        for ( const int32 slotIndex : _listPendingUnit )
        {
            UnitSlot& slot = _listUnitSlot[static_cast<size_t>( slotIndex )];
            destroySpawned( manager, slot._object );
            if ( slot._shownId.isValid() )
                spawnUnit( manager, slotIndex );
        }
        _listPendingUnit.clear();
    }

    void SkirmishDirectorComponent::spawnCliffs( GameObjectManager& manager )
    {
        // 절벽 — 줄마다 이어진 칸을 상자 하나로.
        const int32 size = SkirmishMatch::kMapSize;
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
                const float32  length = static_cast<float32>( x - runStart );
                GameObject*    pCliff = spawnPrefab( manager, _cliffPrefab, "SkirmishCliff" );
                MeshComponent* pMesh  = pCliff != nullptr ? pCliff->getComponent<MeshComponent>() : nullptr;
                if ( pMesh != nullptr )
                {
                    pMesh->setLocalPosition( float3{ static_cast<float32>( runStart ) + length * 0.5f, pMesh->getLocalScale()._y * 0.5f, static_cast<float32>( y ) + 0.5f } );
                    pMesh->setLocalScale( float3{ length, pMesh->getLocalScale()._y, 1.0f } );
                }
                runStart = -1;
            }
        }
    }

    void SkirmishDirectorComponent::spawnUnit( GameObjectManager& manager, int32 slotIndex )
    {
        UnitSlot&      slot  = _listUnitSlot[static_cast<size_t>( slotIndex )];
        const RtsUnit* pUnit = _match.getWorld().findUnit( slot._shownId );
        if ( pUnit == nullptr )
            return;
        const SkirmishUnitModel* pModel = SkirmishUnitComponent::findUnitModel( pUnit->_pDef->_id );
        if ( pModel == nullptr )
            return;
        GameObject* pObject = spawnPrefab( manager, _unitPrefab, "SkirmishUnit" );
        if ( pObject == nullptr )
            return;
        slot._object         = pObject->getHandle();
        MeshComponent* pMesh = pObject->getComponent<MeshComponent>();
        if ( pMesh != nullptr )
        {
            // 모델은 유닛 종류마다 정해져 바뀌지 않는다 — 여기(게임 스레드)서 건다. 색 · 자리 · 크기 · 방향은 뷰가 맞춘다.
            if ( _listUnitLook.empty() )
                prepareUnitLooks( pMesh->getMaterial() );
            pMesh->setMeshId( SkirmishUnitComponent::makeModelPath( pModel->_pModel ) );
        }
        SkirmishUnitComponent* pView = pObject->getComponent<SkirmishUnitComponent>();
        if ( pView != nullptr )
            pView->assignUnit( getOwner()->getHandle(), slot._shownId, pModel->_width );
    }

    void SkirmishDirectorComponent::prepareUnitLooks( Material* pMaterial )
    {
        using Internal = SkirmishDirectorComponentInternal;
        _listUnitLook.assign( static_cast<size_t>( Internal::kUnitLookCategory * 2 ), nullptr );
        for ( int32 category = 0; category < Internal::kUnitLookCategory; ++category )
        {
            for ( int32 selected = 0; selected < 2; ++selected )
                _listUnitLook[static_cast<size_t>( category * 2 + selected )] = _tintCache.acquire( pMaterial, Internal::computeLookColor( category, selected == 1 ) );
        }
    }

    // ------------------------------------------------------------------------------
    // 갱신(PrePhysics — 워커)
    // ------------------------------------------------------------------------------
    void SkirmishDirectorComponent::collectUnitChanges()
    {
        // 보일 유닛 — 사람 쪽 화면은 안 보이는 적을 그리지 않는다(자원은 늘 보인다). 자리의 유닛이 바뀌었거나 이번에 못 본 자리(죽음 · 안개)만 쌓는다.
        ++_frameStamp;
        const RtsWorld& world  = _match.getWorld();
        const bool      bHuman = _bHuman == SW_TRUE;
        world.forEachUnit( [this, &world, bHuman]( const RtsUnit& unit )
        {
            if ( bHuman && unit._owner != 0 && unit._owner != RtsWorld::kNoOwner && world.isVisibleTo( 0, unit._id ) == false )
                return;
            if ( SkirmishUnitComponent::findUnitModel( unit._pDef->_id ) == nullptr )
                return;
            const size_t slotIndex = static_cast<size_t>( unit._id.index() );
            if ( _listUnitSlot.size() <= slotIndex )
                _listUnitSlot.resize( slotIndex + 1 );
            UnitSlot& slot = _listUnitSlot[slotIndex];
            slot._stamp    = _frameStamp;
            if ( slot._shownId == unit._id )
                return;
            slot._shownId = unit._id;
            _listPendingUnit.push_back( static_cast<int32>( slotIndex ) );
        } );
        for ( size_t slotIndex = 0; slotIndex < _listUnitSlot.size(); ++slotIndex )
        {
            UnitSlot& slot = _listUnitSlot[slotIndex];
            if ( slot._stamp == _frameStamp || slot._shownId.isValid() == false )
                continue;
            slot._shownId = RtsUnitId{};
            _listPendingUnit.push_back( static_cast<int32>( slotIndex ) );
        }
    }

    void SkirmishDirectorComponent::updateInput( const InputManager& input )
    {
        // 카메라 이동 · 확대는 리그(PostUpdate)가 읽는다. 여기는 속도 · 멈춤 · 명령.
        if ( input.wasKeyPressed( Key::Minus ) || input.wasKeyPressed( Key::Equal ) )
        {
            _timeScale = MathUtil::clamp( _timeScale * ( input.wasKeyPressed( Key::Equal ) ? 2.0f : 0.5f ), 0.25f, 8.0f );
            SW_LOG_INFO( "[Skirmish] speed x%#", _timeScale );
        }
        if ( input.wasKeyPressed( Key::P ) )
        {
            _bPaused = _bPaused == SW_TRUE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO( "[Skirmish] %#", _bPaused == SW_TRUE ? "paused" : "running" );
        }
        if ( input.wasKeyPressed( Key::F1 ) )
            _match.logStatus();
        if ( _bHuman == SW_TRUE && _match.isOver() == false )
            updateHumanCommands( input );
    }

    void SkirmishDirectorComponent::updateHumanCommands( const InputManager& input )
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
            // 고른 유닛으로 — 리그는 PostUpdate 에서 이 초점을 읽는다(리그의 오브젝트에는 다른 쓰기가 없다).
            const RtsUnit*           pPrimary = world.findUnit( _selection.getPrimary() );
            OrthoCameraRigComponent* pRig     = findCameraRig();
            if ( pPrimary != nullptr && pRig != nullptr )
                pRig->setFocus( float3{ pPrimary->_position._x, 0.0f, pPrimary->_position._z } );
        }
    }

    void SkirmishDirectorComponent::updateDrag( const InputManager& input, const float3& point, bool bPointValid )
    {
        RtsWorld&  world  = _match.getWorld();
        const bool bShift = input.isKeyDown( Key::LeftShift ) || input.isKeyDown( Key::RightShift );
        if ( bPointValid && input.wasMouseButtonPressed( MouseButton::Left ) )
        {
            if ( _bAttackMovePending == SW_TRUE )
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
        _bDragPointValid = bPointValid ? SW_TRUE : SW_FALSE;
        if ( bPointValid )
            _dragPoint = point;
        if ( _bDragging == SW_FALSE || input.isMouseButtonDown( MouseButton::Left ) )
            return;

        // 놓았다 — 짧으면 클릭(그 자리 유닛 하나), 길면 사각형.
        _bDragging            = SW_FALSE;
        const float3  end     = bPointValid ? point : _dragStart;
        const float32 dragged = MathUtil::max( MathUtil::abs( end._x - _dragStart._x ), MathUtil::abs( end._z - _dragStart._z ) );
        if ( dragged < SkirmishDirectorComponentInternal::kClickSlop )
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
        {
            SW_LOG_INFO( "[Skirmish] selected %# (%# units)", pPrimary->_pDef->_name.c_str(), static_cast<int32>( _selection.getSelected().size() ) );
            getSoundQueue().queueEvent( SkirmishDirectorComponentInternal::kSoundSelect );
        }
    }

    void SkirmishDirectorComponent::issueRightClick( const float3& point, bool bQueue )
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

    void SkirmishDirectorComponent::orderBuild( const utf8* pBuildingId, const float3& point )
    {
        RtsWorld&         world = _match.getWorld();
        const RtsUnitDef* pDef  = _catalog.findUnit( hashed_string( pBuildingId ) );
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

    void SkirmishDirectorComponent::trainFromPrimary( int32 productIndex )
    {
        RtsWorld&      world    = _match.getWorld();
        const RtsUnit* pPrimary = world.findUnit( _selection.getPrimary() );
        if ( pPrimary == nullptr || pPrimary->isBuilding() == false )
            return;
        vector<const RtsUnitDef*> listProduct;
        _catalog.findProducts( pPrimary->_pDef->_id, listProduct );
        if ( productIndex >= static_cast<int32>( listProduct.size() ) )
            return;
        const RtsUnitDef&                       product = *listProduct[static_cast<size_t>( productIndex )];
        [[maybe_unused]] const RtsCommandResult result  = world.train( pPrimary->_id, product._id );
        SW_LOG_INFO( "[Skirmish] train %# at %#: %#", product._name.c_str(), pPrimary->_pDef->_name.c_str(), toString( result ) );
    }

    bool SkirmishDirectorComponent::findGroundPoint( const InputManager& input, float3& outPoint ) const
    {
        // 리그의 직교 시점으로 마우스 아래 땅을 찾는다. 리그는 PostUpdate 에서 쓰므로 여기서 읽는 것은 지난 프레임의 시점이다.
        const OrthoCameraRigComponent* pRig = findCameraRig();
        if ( pRig == nullptr )
            return false;
        const IWindow* pWindow = IWindow::getActiveWindow();
        const float32  aspect  = pWindow != nullptr && pWindow->getHeight() > 0
                                   ? static_cast<float32>( pWindow->getWidth() ) / static_cast<float32>( pWindow->getHeight() )
                                   : 16.0f / 9.0f;
        if ( pRig->findGroundPoint( input.getMousePositionNormalized(), aspect, 0.0f, outPoint ) == false )
            return false;
        const float32 mapSize = static_cast<float32>( SkirmishMatch::kMapSize );
        return 0.0f <= outPoint._x && outPoint._x < mapSize && 0.0f <= outPoint._z && outPoint._z < mapSize;
    }

    // ------------------------------------------------------------------------------
    // 알림
    // ------------------------------------------------------------------------------
    void SkirmishDirectorComponent::handleEvents()
    {
        using Internal = SkirmishDirectorComponentInternal;
        _listEvent.clear(); // drainEvents 는 뒤에 붙인다
        _match.drainEvents( _listEvent );
        // 유닛이 부서지면 누구 것이든 그 자리에서 쇳소리 — 한 프레임에 여럿이어도 한 번(구경하는 AI 대 AI 판에서도 들린다). 동시 재생 상한은 이벤트 데이터가 정한다.
        for ( const RtsEvent& event : _listEvent )
        {
            if ( event._kind == RtsEvent::Kind::UnitDied )
            {
                getSoundQueue().queueEventAt( Internal::kSoundUnitDied, event._position );
                break;
            }
        }
        if ( _bHuman == SW_FALSE )
            return;
        // 사람 쪽(0 번)에 알릴 것만 — 나머지는 판이 로그로 남긴다.
        for ( const RtsEvent& event : _listEvent )
        {
            if ( event._player != 0 )
                continue;
            const RtsUnitDef*            pDef  = _catalog.findUnit( event._defId );
            [[maybe_unused]] const utf8* pName = pDef != nullptr ? pDef->_name.c_str() : "?";
            switch ( event._kind )
            {
                case RtsEvent::Kind::ConstructionComplete:
                {
                    SW_LOG_INFO( "[Skirmish] %# complete", pName );
                    getSoundQueue().queueEvent( Internal::kSoundBuilt );
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
                    getSoundQueue().queueEvent( Internal::kSoundBlocked );
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

    OrthoCameraRigComponent* SkirmishDirectorComponent::findCameraRig() const
    {
        GameObjectManager* pManager   = getObjectManager();
        const GameObject*  pRigObject = pManager != nullptr ? pManager->resolveGameObject( _cameraRig ) : nullptr;
        return pRigObject != nullptr ? pRigObject->getComponent<OrthoCameraRigComponent>() : nullptr;
    }

} // namespace sw
