#include "pch.h"

#include "Games/StarSkirmish/SkirmishDirectorComponent.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Probe/AutomationProbe.h"
#include "Engine/Config/GameAutoplay.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/UI/Input/UIInputMapView.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Base/Actor/Camera/CameraDirectorComponent.h"
#include "GameFramework/Base/Actor/Control/InputActionCheckUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

#include "Games/StarSkirmish/SkirmishUnitComponent.h"

namespace sw
{
    SW_LOG_CALLER( "SkirmishDirector" );

    namespace
    {
        struct SkirmishDirectorComponentInternal
        {
            static constexpr int32  kUnitLookCategory = 5; ///< 0번 팀, 1번 팀, 주인 없음, 광물, 가스.
            static constexpr uint32 kStateTag         = FourCcUtil::make( "RTMS" );
            static constexpr uint32 kStateVersion     = 2;

            // 사운드 이벤트 이름(starskirmish.audioevents.xml).
            static constexpr const utf8* kSoundSelect   = "Select";
            static constexpr const utf8* kSoundBuilt    = "Built";
            static constexpr const utf8* kSoundBlocked  = "Blocked";
            static constexpr const utf8* kSoundUnitDied = "UnitDied";

            /** @brief 모습 종류입니다. 팀(0번, 1번, 그 밖)이거나 자원(광물, 가스)입니다. */
            static int32 computeLookCategory( const RTSUnit& unit )
            {
                if ( unit.isResource() )
                    return unit._pDef->_resourceType == RTSResourceType::Gas ? 4 : 3;
                if ( unit._owner == 0 || unit._owner == 1 )
                    return unit._owner;
                return 2;
            }

            /**
             * @brief 해당 모습의 모델에 곱할 색입니다. 키트의 흰색과 회색 몸체가 팀 색(파랑, 빨강)으로, 자원은 광물이 하늘색, 가스가 초록색으로 보입니다. 선택하면 흰색 쪽으로 절반 섞습니다.
             * @details 건물과 일꾼을 어둡게, 밝게 나누던 일은 모델이 대신하므로 팀 색을 그대로 곱한다(곱하면 이미 어두워진다).
             */
            static float4 computeLookColor( const float4 ( &arrCategoryColor )[kUnitLookCategory], int32 category, bool bSelected )
            {
                float4 color = arrCategoryColor[MathUtil::clamp( category, 0, kUnitLookCategory - 1 )];
                if ( bSelected )
                    color = float4{ 0.5f + color._x * 0.5f, 0.5f + color._y * 0.5f, 0.5f + color._z * 0.5f, 1.0f };
                return color;
            }
        };

        /** @brief 프로브가 읽는 디렉터입니다. 씬의 첫 디렉터이며, 프로브는 단언 단계에서만 불리므로 프레임 경로가 아닙니다. */
        const SkirmishDirectorComponent* findProbeDirector( const GameObjectManager* pManager )
        {
            const SkirmishDirectorComponent* pFound = nullptr;
            if ( pManager != nullptr )
            {
                pManager->forEachComponentOfType<SkirmishDirectorComponent>( [&pFound]( const SkirmishDirectorComponent* pDirector )
                {
                    if ( pFound == nullptr )
                        pFound = pDirector;
                } );
            }
            return pFound;
        }

        /** @brief 선택한 유닛 중 @p bMoveOnly 면 앞 명령이 이동인 것만 셉니다. */
        [[nodiscard]] bool countSelected( const GameObjectManager* pManager, bool bMoveOnly, float64& outValue )
        {
            const SkirmishDirectorComponent* pDirector = findProbeDirector( pManager );
            if ( pDirector == nullptr )
                return false;
            int32 count = 0;
            pDirector->getWorld().forEachUnit( [pDirector, bMoveOnly, &count]( const RTSUnit& unit )
            {
                const RTSOrder* pOrder = unit.findOrder();
                if ( pDirector->isSelected( unit._id ) && ( bMoveOnly == false || ( pOrder != nullptr && pOrder->_type == RTSOrderType::Move ) ) )
                    ++count;
            } );
            outValue = static_cast<float64>( count );
            return true;
        }

        [[nodiscard]] bool readSelectedCount( const GameObjectManager* pManager, float64& outValue )
        {
            return countSelected( pManager, false, outValue );
        }

        [[nodiscard]] bool readSelectedMovingCount( const GameObjectManager* pManager, float64& outValue )
        {
            return countSelected( pManager, true, outValue );
        }

        /** @brief 탐침 `Skirmish.AITask` 입니다. 플레이어 `key`(0 · 1)를 맡은 컴퓨터 상대의 행동 트리가 도는 잎(`GatherIdle` · `TrainWorkers` · `Defend` …)입니다. */
        [[nodiscard]] bool readAITask( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
        {
            const SkirmishDirectorComponent* pDirector = findProbeDirector( query._pManager );
            int32                            player    = -1;
            if ( pDirector == nullptr || StringUtil::parseInt( query._key, player ) == false )
            {
                outValue.setText( pDirector == nullptr ? string( "no skirmish director" ) : "key must be a player index, got '" + string( query._key ) + "'" );
                return false;
            }
            const RTSCommanderAI* pAI = pDirector->getMatch().findAI( player );
            if ( pAI == nullptr )
            {
                outValue.setText( "player " + string( query._key ) + " is not played by the AI" );
                return false;
            }
            const utf8* pTask = pAI->getActiveTaskName();
            outValue.setText( pTask != nullptr && pTask[0] != '\0' ? string_view{ pTask } : string_view{ "none" } );
            return true;
        }
    } // namespace

    /**
     * @brief `-gv_skirmishAutoPlay=1`: 디렉터를 컴퓨터 대 컴퓨터(러시 대 운영)로 엽니다(씬의 `_bAutoPlay` 가 꺼져 있어도 엽니다). 입력 없이 승패까지 가므로 확인과 화면 녹화에 씁니다.
     * @details 배포본으로도 돌릴 수 있게 남겨 둔다. `App -gv_skirmishAutoPlay=1` 로 실행하면 30초마다 `[Skirmish] t=.. p0 workers .. army ..` 가, 끝에는 승패가 로그에 남는다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_skirmishAutoPlay, 0, "StarSkirmish: 두 플레이어 모두 AI 로 돌리기 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_skirmishAutoPlay, "StarSkirmish", "Both players are AI" );

    SW_AUTOMATION_PROBE( skirmishSelectedCount, "Skirmish.SelectedCount", "Units in the human selection", &readSelectedCount );
    SW_AUTOMATION_PROBE( skirmishSelectedMovingCount, "Skirmish.SelectedMovingCount", "Selected units whose current order is a move", &readSelectedMovingCount );
    SW_AUTOMATION_QUERY_PROBE( skirmishAITask, "Skirmish.AITask", "Active behavior tree task of the AI commander playing player key (0 or 1)", &readAITask, false, true );
} // namespace sw

namespace sw
{
    SkirmishDirectorComponent::SkirmishDirectorComponent()
        : _unitDataPath{}
        , _unitModelPath{}
        , _commanderTreePath{}
        , _cliffPrefab{}
        , _unitPrefab{}
        , _cameraRig{}
        , _watchFocus{ 32.0f, 0.0f, 30.0f }
        , _watchCameraPreset{}
        , _huntIntervalSeconds{ 4.0f }
        , _arrivedRadius{ 10.0f }
        , _startMinerals{ 50 }
        , _statusLogInterval{ 30.0f }
        , _clickSlop{ 0.6f }
        , _maxSelectionCount{ 12 }
        , _selectAction{}
        , _orderAction{}
        , _addToSelectionAction{}
        , _groupModifierAction{}
        , _attackMoveAction{}
        , _holdAction{}
        , _stopAction{}
        , _jumpToSelectionAction{}
        , _fasterAction{}
        , _slowerAction{}
        , _pauseAction{}
        , _statusAction{}
        , _playerPanAction{}
        , _spectatorPanAction{}
        , _listTrainAction{}
        , _listGroupAction{}
        , _listBuildAction{}
        , _listBuildUnitID{}
        , _workerUnitID{}
        , _depotUnitID{}
        , _supplyUnitID{}
        , _productionUnitID{}
        , _armyUnitID{}
        , _rushAI{}
        , _macroAI{}
        , _opponentAI{}
        , _playerZeroColor{ 0.25f, 0.45f, 0.95f, 1.0f }
        , _playerOneColor{ 0.9f, 0.25f, 0.2f, 1.0f }
        , _neutralColor{ 0.7f, 0.7f, 0.7f, 1.0f }
        , _mineralColor{ 0.35f, 0.85f, 1.0f, 1.0f }
        , _gasColor{ 0.3f, 0.8f, 0.4f, 1.0f }
        , _catalog{}
        , _unitModelCatalog{}
        , _commanderTree{}
        , _commanderDebugSource{ *this }
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

    void SkirmishDirectorComponent::CommanderDebugSource::collectBehaviorTreeRunners( vector<BehaviorTreeDebugRunner>& outList ) const
    {
        for ( int32 player = 0; player < SkirmishMatch::kPlayerCount; ++player )
        {
            const RTSCommanderAI* pAI = _owner._match.findAI( player );
            if ( pAI == nullptr )
                continue;
            BehaviorTreeDebugRunner& entry = outList.emplace_back();
            entry._name                    = player == 0 ? "Player 0" : "Player 1"; // 판은 두 플레이어(kPlayerCount)
            entry._treePath                = _owner._commanderTreePath;
            entry._pRunner                 = &pAI->getRunner();
        }
    }

    void SkirmishDirectorComponent::onBeginPlay()
    {
        GameDirectorComponent::onBeginPlay(); // 패드 가상 커서는 입력 맵(`virtualCursor`)이 켠다
    }

    void SkirmishDirectorComponent::onEndPlay()
    {
        _unitModels.clear();
        GameDirectorComponent::onEndPlay();
    }

    void SkirmishDirectorComponent::onGameStarted()
    {
        // 처음 짓는 건물 · 유닛의 모델을 게임 스레드가 플레이 중에 디스크에서 읽지 않게 판을 열 때 워커로 미리 읽는다(느린 디스크의 히치).
        if ( _unitModels.getCount() == 0 )
        {
            for ( const string& path : _unitModelCatalog.makeAllModelPaths() )
            {
                _unitModels.add( MeshCache::request( path ) );
            }
        }
        // 사람 쪽은 글자 키가 명령이라 카메라는 방향키만 쓴다. 둘 다 AI 면 맵 전체가 보이게 물러선 프리셋(`_watchCameraPreset`, 씬 데이터)으로 시작하고 WASD 도 카메라를 움직인다.
        // 프리셋 고르기는 카메라 오브젝트의 시작 순서에 기대지 않는다(`startWithPreset` — 시작 전이면 시작 프리셋, 뒤면 컷).
        CameraDirectorComponent* pRig = findCameraRig();
        if ( pRig != nullptr )
        {
            pRig->setPanAction( _bHuman == SW_TRUE ? _playerPanAction : _spectatorPanAction ); // 플레이할 때 WASD 는 명령 단축키다.
            if ( _bHuman == SW_FALSE )
            {
                pRig->setFocusPoint( _watchFocus );
                pRig->startWithPreset( _watchCameraPreset );
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

    void SkirmishDirectorComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        const InputManager* pInput = game::findSubsystem<InputManager>();
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

    const shared_ptr<MaterialInstance>& SkirmishDirectorComponent::findUnitLook( const RTSUnit& unit, bool bSelected ) const
    {
        static const shared_ptr<MaterialInstance> kNoLook{};
        const int32                               lookIndex = SkirmishDirectorComponentInternal::computeLookCategory( unit ) * 2 + ( bSelected ? 1 : 0 );
        if ( lookIndex >= static_cast<int32>( _listUnitLook.size() ) )
            return kNoLook;
        // 뷰가 워커에서 부른다. 컨테이너 인덱스 대신 포인터로 읽는다.
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
        const bool bInvalidDirectorState = archive.isError() || archive.getRemainingBytes() != 0 || timeScale <= 0.0f;
        if ( bInvalidDirectorState )
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
            // 판이 반쯤 바뀌었을 수 있다. 새 판으로 되돌린다.
            SW_LOG_WARNING( "[Skirmish] the saved match state does not match this build - starting a new match" );
            (void)startGame();
        }
    }

    bool SkirmishDirectorComponent::startGame()
    {
        if ( _catalog.loadFromResource( ResourceID{ _unitDataPath } ) == false )
        {
            SW_LOG_WARNING( "[Skirmish] %# could not be loaded - the match cannot start", _unitDataPath.c_str() );
            return false;
        }
        _unitModelCatalog.clear();
        if ( _unitModelCatalog.loadFromResource( ResourceID{ _unitModelPath } ) == false )
        {
            SW_LOG_ERROR( "[Skirmish] the unit model table '%#' could not be loaded - the match cannot start", _unitModelPath.c_str() );
            return false;
        }
        if ( _commanderTree.loadFromResource( ResourceID{ _commanderTreePath } ) == false )
        {
            SW_LOG_ERROR( "[Skirmish] the commander behavior tree '%#' could not be loaded - the match cannot start", _commanderTreePath.c_str() );
            return false;
        }
        if ( hasValidNames() == false )
        {
            SW_LOG_ERROR( "[Skirmish] the director's input actions or unit ids do not match the data - the match cannot start" );
            return false;
        }
        const bool bAIStylesSet = _rushAI.isSet() && _macroAI.isSet() && _opponentAI.isSet();
        if ( bAIStylesSet == false )
        {
            SW_LOG_ERROR( "[Skirmish] the director needs _rushAI, _macroAI and _opponentAI (worker, production and attack targets) - the match cannot start" );
            return false;
        }
        _bHuman = isAutoPlayOn() ? SW_FALSE : SW_TRUE;
        SkirmishMatchSettings matchSettings;
        matchSettings._huntIntervalSeconds   = _huntIntervalSeconds;
        matchSettings._arrivedRadius         = _arrivedRadius;
        matchSettings._startMinerals         = _startMinerals;
        matchSettings._statusIntervalSeconds = _statusLogInterval;
        matchSettings._workerID              = _workerUnitID;
        matchSettings._depotID               = _depotUnitID;
        matchSettings._supplyID              = _supplyUnitID;
        matchSettings._productionID          = _productionUnitID;
        matchSettings._armyUnitID            = _armyUnitID;
        matchSettings._rushAI                = _rushAI;
        matchSettings._macroAI               = _macroAI;
        matchSettings._opponentAI            = _opponentAI;
        matchSettings._pCommanderTree        = &_commanderTree.getTree();
        if ( _match.initialize( &_catalog, _bHuman == SW_TRUE, matchSettings ) == false )
            return false;
        _selection.clear();
        _selection.setPlayer( 0 );
        _selection.setMaxCount( _maxSelectionCount );
        _listEvent.clear();
        return true;
    }

    // ------------------------------------------------------------------------------
    // 스폰(틱이 끝난 뒤, 게임 스레드)
    // ------------------------------------------------------------------------------
    void SkirmishDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews )
        {
            // 처음이거나 정리한 뒤다. 절벽과 지금 보이는 유닛을 모두 스폰한다.
            spawnCliffs( manager );
            _listPendingUnit.clear();
            collectUnitChanges();
        }
        for ( const int32 slotIndex : _listPendingUnit )
        {
            UnitSlot& slot = _listUnitSlot[static_cast<size_t>( slotIndex )];
            destroySpawned( manager, slot._object );
            if ( slot._shownID.isValid() )
                spawnUnit( manager, slotIndex );
        }
        _listPendingUnit.clear();
    }

    void SkirmishDirectorComponent::spawnCliffs( GameObjectManager& manager )
    {
        // 절벽은 줄마다 이어진 타일을 상자 하나로 만든다.
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
        const RTSUnit* pUnit = _match.getWorld().findUnit( slot._shownID );
        if ( pUnit == nullptr )
            return;
        const SkirmishUnitModel* pModel = _unitModelCatalog.find( pUnit->_pDef->_id );
        if ( pModel == nullptr )
            return;
        GameObject* pObject = spawnPrefab( manager, _unitPrefab, "SkirmishUnit" );
        if ( pObject == nullptr )
            return;
        slot._object         = pObject->getHandle();
        MeshComponent* pMesh = pObject->getComponent<MeshComponent>();
        if ( pMesh != nullptr )
        {
            // 모델은 유닛 종류마다 정해져 바뀌지 않는다. 그래서 여기(게임 스레드)에서 지정한다. 색, 위치, 크기, 방향은 뷰가 맞춘다.
            if ( _listUnitLook.empty() )
                prepareUnitLooks( pMesh->getMaterial() );
            pMesh->setMeshID( SkirmishUnitComponent::makeModelPath( pModel->_model.c_str() ) );
        }
        SkirmishUnitComponent* pView = pObject->getComponent<SkirmishUnitComponent>();
        if ( pView != nullptr )
            pView->assignUnit( getOwner()->getHandle(), slot._shownID, pModel->_width );
    }

    void SkirmishDirectorComponent::prepareUnitLooks( Material* pMaterial )
    {
        using Internal = SkirmishDirectorComponentInternal;
        _listUnitLook.assign( static_cast<size_t>( Internal::kUnitLookCategory * 2 ), nullptr );
        const float4 arrCategoryColor[Internal::kUnitLookCategory] = { _playerZeroColor, _playerOneColor, _neutralColor, _mineralColor, _gasColor };
        for ( int32 category = 0; category < Internal::kUnitLookCategory; ++category )
        {
            for ( int32 selected = 0; selected < 2; ++selected )
            {
                _listUnitLook[static_cast<size_t>( category * 2 + selected )] = _tintCache.acquire( pMaterial, Internal::computeLookColor( arrCategoryColor, category, selected == 1 ) );
            }
        }
    }

    // ------------------------------------------------------------------------------
    // 갱신(PrePhysics, 워커)
    // ------------------------------------------------------------------------------
    void SkirmishDirectorComponent::collectUnitChanges()
    {
        // 보일 유닛은 사람 쪽 화면이 안 보이는 적을 그리지 않게 정한다(자원은 항상 보인다). 슬롯의 유닛이 바뀌었거나 이번에 못 본 슬롯(죽음, 안개)만 쌓는다.
        ++_frameStamp;
        const RTSWorld& world  = _match.getWorld();
        const bool      bHuman = _bHuman == SW_TRUE;
        world.forEachUnit( [this, &world, bHuman]( const RTSUnit& unit )
        {
            const bool bHiddenFromHuman = bHuman && unit._owner != 0 && unit._owner != RTSWorld::kNoOwner && world.isVisibleTo( 0, unit._id ) == false;
            if ( bHiddenFromHuman )
                return;
            if ( _unitModelCatalog.find( unit._pDef->_id ) == nullptr )
                return; // 모든 유닛에 모델 줄이 있음은 hasValidNames 가 시작할 때 본다
            const size_t slotIndex = static_cast<size_t>( unit._id.index() );
            if ( _listUnitSlot.size() <= slotIndex )
                _listUnitSlot.resize( slotIndex + 1 );
            UnitSlot& slot = _listUnitSlot[slotIndex];
            slot._stamp    = _frameStamp;
            if ( slot._shownID == unit._id )
                return;
            slot._shownID = unit._id;
            _listPendingUnit.push_back( static_cast<int32>( slotIndex ) );
        } );
        for ( size_t slotIndex = 0; slotIndex < _listUnitSlot.size(); ++slotIndex )
        {
            UnitSlot& slot = _listUnitSlot[slotIndex];
            if ( slot._stamp == _frameStamp || slot._shownID.isValid() == false )
                continue;
            slot._shownID = RTSUnitID{};
            _listPendingUnit.push_back( static_cast<int32>( slotIndex ) );
        }
    }

    void SkirmishDirectorComponent::updateInput( const InputManager& input )
    {
        // 카메라 이동과 확대는 리그(PostUpdate)가 읽는다. 여기서는 속도, 일시정지, 명령을 처리한다.
        // 키는 입력 맵(`data/skirmish.input.xml`)이 정한다. 일시정지 메뉴 같은 UI 가 먹은 입력은 보이지 않는다.
        const UIInputMapView inputMap( input.getInputMap(), game::findSubsystem<UISystem>() );
        const bool           bFaster = inputMap.wasActionTriggered( _fasterAction );
        if ( bFaster || inputMap.wasActionTriggered( _slowerAction ) )
        {
            _timeScale = MathUtil::clamp( _timeScale * ( bFaster ? 2.0f : 0.5f ), 0.25f, 8.0f );
            SW_LOG_INFO( "[Skirmish] speed x%#", _timeScale );
        }
        if ( inputMap.wasActionTriggered( _pauseAction ) )
        {
            _bPaused = _bPaused == SW_TRUE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO( "[Skirmish] %#", _bPaused == SW_TRUE ? "paused" : "running" );
        }
        if ( inputMap.wasActionTriggered( _statusAction ) )
            _match.logStatus();
        if ( _bHuman == SW_TRUE && _match.isOver() == false )
            updateHumanCommands( input );
    }

    void SkirmishDirectorComponent::updateHumanCommands( const InputManager& input )
    {
        RTSWorld&            world = _match.getWorld();
        const UIInputMapView inputMap( input.getInputMap(), game::findSubsystem<UISystem>() );
        float3               point{};
        const bool           bPointValid = findGroundPoint( input, point );
        const bool           bShift      = inputMap.isActionDown( _addToSelectionAction );
        const bool           bControl    = inputMap.isActionDown( _groupModifierAction );
        updateDrag( input, point, bPointValid );

        if ( bPointValid && inputMap.wasActionTriggered( _orderAction ) )
            issueOrder( point, bShift );

        // 명령 단축키는 선택한 것이 모두 내 것일 때만 동작한다.
        if ( _selection.isCommandable( world ) && _selection.getSelected().empty() == false )
        {
            if ( inputMap.wasActionTriggered( _attackMoveAction ) )
            {
                _bAttackMovePending = SW_TRUE;
                SW_LOG_INFO( "[Skirmish] attack-move - left click a target point" );
            }
            const bool bHold = inputMap.wasActionTriggered( _holdAction );
            if ( bHold || inputMap.wasActionTriggered( _stopAction ) )
            {
                for ( const RTSUnitID unitID : _selection.getSelected() )
                {
                    (void)( bHold ? world.issueHold( unitID ) : world.issueStop( unitID ) );
                }
            }
            for ( size_t productIndex = 0; productIndex < _listTrainAction.size(); ++productIndex )
            {
                if ( inputMap.wasActionTriggered( _listTrainAction[productIndex] ) )
                    trainFromPrimary( static_cast<int32>( productIndex ) );
            }
            // 건설 단축키 표는 씬 데이터다(액션 · 지을 유닛 id 가 같은 자리). 길이가 같은지는 판을 열 때 봤다.
            for ( size_t buildIndex = 0; bPointValid && buildIndex < _listBuildAction.size(); ++buildIndex )
            {
                if ( inputMap.wasActionTriggered( _listBuildAction[buildIndex] ) )
                    orderBuild( _listBuildUnitID[buildIndex], point );
            }
        }

        // 부대 입력: Ctrl + 숫자는 지정, Shift + 숫자는 추가, 숫자는 호출이다. 부대 액션 수는 판을 열 때 `RTSSelection::kGroupCount` 와 맞는지 봤다.
        for ( int32 group = 0; group < static_cast<int32>( _listGroupAction.size() ); ++group )
        {
            if ( inputMap.wasActionTriggered( _listGroupAction[static_cast<size_t>( group )] ) == false )
                continue;
            if ( bControl )
                _selection.assignGroup( group );
            else if ( bShift )
                _selection.addToGroup( group );
            else if ( _selection.recallGroup( world, group ) )
                SW_LOG_INFO( "[Skirmish] group %# - %# units", group, static_cast<int32>( _selection.getSelected().size() ) );
        }
        if ( inputMap.wasActionTriggered( _jumpToSelectionAction ) )
        {
            // 선택한 유닛으로 이동한다. 카메라 디렉터는 PostPhysics 틱 뒤에 이 초점을 읽는다(카메라 오브젝트에는 다른 쓰기가 없다).
            const RTSUnit*           pPrimary = world.findUnit( _selection.getPrimary() );
            CameraDirectorComponent* pRig     = findCameraRig();
            if ( pPrimary != nullptr && pRig != nullptr )
                pRig->setFocusPoint( float3{ pPrimary->_position._x, 0.0f, pPrimary->_position._z } );
        }
    }

    void SkirmishDirectorComponent::updateDrag( const InputManager& input, const float3& point, bool bPointValid )
    {
        RTSWorld&            world = _match.getWorld();
        const UIInputMapView inputMap( input.getInputMap(), game::findSubsystem<UISystem>() );
        const hashed_string& selectAction = _selectAction;
        const bool           bShift       = inputMap.isActionDown( _addToSelectionAction );
        if ( bPointValid && inputMap.wasActionPressed( selectAction ) )
        {
            if ( _bAttackMovePending == SW_TRUE )
            {
                // A 다음 선택(왼쪽 클릭)이면 선택한 병력을 그 위치로 공격 이동시킨다.
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
        if ( _bDragging == SW_FALSE || inputMap.isActionDown( selectAction ) )
            return;

        // 버튼을 놓았다. 짧게 끌었으면 클릭(그 위치의 유닛 하나)이고, 길게 끌었으면 사각형 선택이다.
        _bDragging            = SW_FALSE;
        const float3  end     = bPointValid ? point : _dragStart;
        const float32 dragged = MathUtil::max( MathUtil::abs( end._x - _dragStart._x ), MathUtil::abs( end._z - _dragStart._z ) );
        if ( dragged < _clickSlop )
        {
            const RTSUnitID pickedID = world.pickUnit( end );
            if ( pickedID.isValid() )
                _selection.selectUnit( world, pickedID, bShift );
            else if ( bShift == false )
                _selection.clear();
        }
        else
            _selection.selectInRect( world, _dragStart, end, bShift );
        const RTSUnit* pPrimary = world.findUnit( _selection.getPrimary() );
        if ( pPrimary != nullptr )
        {
            SW_LOG_INFO( "[Skirmish] selected %# (%# units)", pPrimary->_pDef->_name.c_str(), static_cast<int32>( _selection.getSelected().size() ) );
            getSoundQueue().queueEvent( SkirmishDirectorComponentInternal::kSoundSelect );
        }
    }

    void SkirmishDirectorComponent::issueOrder( const float3& point, bool bQueue )
    {
        RTSWorld& world = _match.getWorld();
        if ( _selection.getSelected().empty() || _selection.isCommandable( world ) == false )
            return;
        // 유닛이나 자원을 눌렀으면 하나씩 상황에 맞는 명령(공격, 채취, 이어 짓기)을 낸다. 빈 땅이면 움직이는 것은 무리 이동, 건물은 집결지 지정이다.
        const RTSUnitID   targetID = world.pickUnit( point );
        vector<RTSUnitID> listMobile;
        for ( const RTSUnitID unitID : _selection.getSelected() )
        {
            const RTSUnit* pUnit = world.findUnit( unitID );
            if ( pUnit == nullptr )
                continue;
            if ( pUnit->isBuilding() )
                world.setRallyPoint( unitID, point );
            else if ( targetID.isValid() && targetID != unitID )
                (void)world.issueSmart( unitID, point, targetID, bQueue );
            else
                listMobile.push_back( unitID );
        }
        if ( listMobile.empty() == false )
            (void)world.issueGroupMove( listMobile, point, false, bQueue );
    }

    bool SkirmishDirectorComponent::hasValidNames() const
    {
        static constexpr const utf8* kOwner = "SkirmishDirectorComponent";
        bool                         bValid = true;
        // AI 와 건설 단축키가 쓰는 유닛 id 는 유닛 카탈로그에 있어야 한다.
        for ( const hashed_string* pUnitID : { &_workerUnitID, &_depotUnitID, &_supplyUnitID, &_productionUnitID, &_armyUnitID } )
        {
            if ( _catalog.find( *pUnitID ) != nullptr )
                continue;
            SW_LOG_ERROR( "%#: unit id '%#' is not in %#", kOwner, pUnitID->c_str(), _unitDataPath.c_str() );
            bValid = false;
        }
        // 모델 표와 유닛 카탈로그는 서로 같은 id 를 가져야 한다. 모델이 없는 유닛은 조용히 그려지지 않으므로 시작할 때 막는다.
        for ( const RTSUnitDef& def : _catalog.getAll() )
        {
            if ( _unitModelCatalog.find( def._id ) != nullptr )
                continue;
            SW_LOG_ERROR( "%#: unit '%#' of %# has no row in %#", kOwner, def._id.c_str(), _unitDataPath.c_str(), _unitModelPath.c_str() );
            bValid = false;
        }
        for ( const SkirmishUnitModel& model : _unitModelCatalog.getAll() )
        {
            if ( _catalog.find( model._id ) != nullptr )
                continue;
            SW_LOG_ERROR( "%#: model row '%#' of %# is not in %#", kOwner, model._id.c_str(), _unitModelPath.c_str(), _unitDataPath.c_str() );
            bValid = false;
        }
        if ( _listBuildAction.size() != _listBuildUnitID.size() )
        {
            SW_LOG_ERROR( "%#: %# build actions but %# build units - they pair by index", kOwner, static_cast<uint32>( _listBuildAction.size() ),
                          static_cast<uint32>( _listBuildUnitID.size() ) );
            bValid = false;
        }
        for ( const hashed_string& unitID : _listBuildUnitID )
        {
            if ( _catalog.find( unitID ) != nullptr )
                continue;
            SW_LOG_ERROR( "%#: build unit '%#' is not in %#", kOwner, unitID.c_str(), _unitDataPath.c_str() );
            bValid = false;
        }
        // 관전 카메라 프리셋은 씬 데이터가 준다(코드에 기본 이름이 없다). 카탈로그에 있는지는 카메라가 시작할 때 본다(`CameraDirectorComponent::startWithPreset`).
        if ( _watchCameraPreset.empty() )
        {
            SW_LOG_ERROR( "%#: _watchCameraPreset is empty - the scene must name the camera preset for AI vs AI", kOwner );
            bValid = false;
        }
        // 입력 액션은 입력 맵에 있어야 한다. 입력이 없는 실행(전용 서버 · 헤드리스)은 보지 않는다.
        const InputManager* pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
            return bValid;
        const InputMap& inputMap = pInput->getInputMap();
        bValid                   = InputActionCheckUtil::hasAllActions( inputMap,
                                                                        { &_selectAction, &_orderAction, &_addToSelectionAction, &_groupModifierAction, &_attackMoveAction, &_holdAction,
                                                                          &_stopAction, &_jumpToSelectionAction, &_fasterAction, &_slowerAction, &_pauseAction, &_statusAction,
                                                                          &_playerPanAction, &_spectatorPanAction },
                                                                        kOwner ) &&
                 bValid;
        bValid = InputActionCheckUtil::hasAllActions( inputMap, _listGroupAction, static_cast<size_t>( RTSSelection::kGroupCount ), kOwner ) && bValid;
        bValid = InputActionCheckUtil::hasAllActions( inputMap, _listTrainAction, _listTrainAction.size(), kOwner ) && bValid;
        bValid = InputActionCheckUtil::hasAllActions( inputMap, _listBuildAction, _listBuildAction.size(), kOwner ) && bValid;
        return bValid;
    }

    void SkirmishDirectorComponent::orderBuild( const hashed_string& buildingID, const float3& point )
    {
        RTSWorld&         world = _match.getWorld();
        const RTSUnitDef* pDef  = _catalog.find( buildingID );
        if ( pDef == nullptr )
            return;
        // 선택한 것 중 첫 일꾼이 짓는다. 정제소는 커서에 가까운 빈 간헐천에, 다른 건물은 커서가 중심이 되는 위치에 짓는다.
        RTSUnitID workerID{};
        for ( const RTSUnitID unitID : _selection.getSelected() )
        {
            const RTSUnit* pUnit        = world.findUnit( unitID );
            const bool     bFirstWorker = workerID.isValid() == false && pUnit != nullptr && pUnit->_pDef->_bWorker != SW_FALSE;
            if ( bFirstWorker )
                workerID = unitID;
        }
        if ( workerID.isValid() == false )
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
        [[maybe_unused]] const RTSCommandResult result = world.issueBuild( workerID, pDef->_id, cell );
        SW_LOG_INFO( "[Skirmish] build %# at (%#, %#): %#", pDef->_name.c_str(), cell._x, cell._y, getEnumName( result ) );
    }

    void SkirmishDirectorComponent::trainFromPrimary( int32 productIndex )
    {
        RTSWorld&      world    = _match.getWorld();
        const RTSUnit* pPrimary = world.findUnit( _selection.getPrimary() );
        if ( pPrimary == nullptr || pPrimary->isBuilding() == false )
            return;
        vector<const RTSUnitDef*> listProduct;
        _catalog.findProducts( pPrimary->_pDef->_id, listProduct );
        if ( productIndex >= static_cast<int32>( listProduct.size() ) )
            return;
        const RTSUnitDef&                       product = *listProduct[static_cast<size_t>( productIndex )];
        [[maybe_unused]] const RTSCommandResult result  = world.train( pPrimary->_id, product._id );
        SW_LOG_INFO( "[Skirmish] train %# at %#: %#", product._name.c_str(), pPrimary->_pDef->_name.c_str(), getEnumName( result ) );
    }

    bool SkirmishDirectorComponent::findGroundPoint( const InputManager& input, float3& outPoint ) const
    {
        // 카메라 디렉터가 지금 낸 포즈로 포인터 아래 땅을 찾는다. 디렉터는 PostPhysics 틱 뒤에 쓰므로 여기서 읽는 것은 지난 프레임의 시점이다.
        const CameraDirectorComponent* pRig = findCameraRig();
        if ( pRig == nullptr )
            return false;
        if ( pRig->findGroundPoint( input.getMousePositionNormalized(), 0.0f, outPoint ) == false )
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
        _listEvent.clear(); // drainEvents 는 뒤에 붙인다.
        _match.drainEvents( _listEvent );
        // 유닛이 부서지면 누구 것이든 그 위치에서 쇳소리를 낸다. 한 프레임에 여럿이어도 한 번만 낸다(구경하는 AI 대 AI 판에서도 들린다). 동시 재생 상한은 이벤트 데이터가 정한다.
        for ( const RTSEvent& event : _listEvent )
        {
            if ( event._kind == RTSEvent::Kind::UnitDied )
            {
                getSoundQueue().queueEventAt( Internal::kSoundUnitDied, event._position );
                break;
            }
        }
        if ( _bHuman == SW_FALSE )
            return;
        // 사람 쪽(0번)에 알릴 것만 다룬다. 나머지는 판이 로그로 남긴다.
        for ( const RTSEvent& event : _listEvent )
        {
            if ( event._player != 0 )
                continue;
            const RTSUnitDef*            pDef  = _catalog.find( event._defID );
            [[maybe_unused]] const utf8* pName = pDef != nullptr ? pDef->_name.c_str() : "?";
            switch ( event._kind )
            {
                case RTSEvent::Kind::ConstructionComplete:
                {
                    SW_LOG_INFO( "[Skirmish] %# complete", pName );
                    getSoundQueue().queueEvent( Internal::kSoundBuilt );
                    break;
                }
                case RTSEvent::Kind::ProductionComplete:
                {
                    SW_LOG_INFO( "[Skirmish] %# ready", pName );
                    break;
                }
                case RTSEvent::Kind::SupplyBlocked:
                {
                    SW_LOG_INFO( "[Skirmish] not enough supply - build a Supply Depot (B)" );
                    getSoundQueue().queueEvent( Internal::kSoundBlocked );
                    break;
                }
                case RTSEvent::Kind::UnderAttack:
                {
                    SW_LOG_INFO( "[Skirmish] we are under attack at (%#, %#)", static_cast<int32>( event._position._x ), static_cast<int32>( event._position._z ) );
                    break;
                }
                case RTSEvent::Kind::UnitCreated:
                case RTSEvent::Kind::UnitDied:
                case RTSEvent::Kind::ResourceDepleted:
                case RTSEvent::Kind::ResourcesDeposited:
                case RTSEvent::Kind::PlayerDefeated:
                case RTSEvent::Kind::GameOver:
                {
                    break;
                }
            }
        }
    }

    CameraDirectorComponent* SkirmishDirectorComponent::findCameraRig() const
    {
        GameObjectManager* pManager   = getObjectManager();
        const GameObject*  pRigObject = pManager != nullptr ? pManager->resolveGameObject( _cameraRig ) : nullptr;
        return pRigObject != nullptr ? pRigObject->getComponent<CameraDirectorComponent>() : nullptr;
    }
} // namespace sw
