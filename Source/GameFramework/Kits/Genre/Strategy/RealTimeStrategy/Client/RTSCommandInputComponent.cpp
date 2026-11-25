#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSCommandInputComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/SkirmishGameModeComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Control/InputActionCheckUtil.h"
#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        /** @brief 고른 유닛 · 부대를 모든 게임의 상태 스냅샷에 올립니다. */
        const StatefulComponentRegistrar<RTSCommandInputComponent> s_stateRegistration{ "RTSCommandInputComponent" };

        struct RTSCommandInputComponentInternal
        {
            static constexpr int32   kGeyserSearchRadius = 8;     ///< 정제소를 놓을 간헐천을 커서 둘레에서 찾는 반경(칸)
            static constexpr float32 kDragBoxThickness   = 0.05f; ///< 끌기 상자의 두께 · 가장 얇은 폭(m)

            // 사운드 이벤트 이름입니다. 클립, 범위, 동시 재생 상한은 게임의 사운드 이벤트 라이브러리가 정합니다.
            static constexpr const utf8* kSoundSelect   = "RTSSelect";
            static constexpr const utf8* kSoundBuilt    = "RTSBuilt";
            static constexpr const utf8* kSoundBlocked  = "RTSBlocked";
            static constexpr const utf8* kSoundUnitDied = "RTSUnitDied";

            /** @brief 감시 값 `Skirmish.SelectedCount` 입니다. */
            [[nodiscard]] static bool readSelectedCount( const RTSCommandInputComponent& commandInput, float64& outValue )
            {
                outValue = static_cast<float64>( commandInput.getSelection().getSelected().size() );
                return true;
            }

            /** @brief 감시 값 `Skirmish.SelectedMovingCount` 입니다. 고른 유닛 가운데 앞 명령이 이동인 수입니다. */
            [[nodiscard]] static bool readSelectedMovingCount( const RTSCommandInputComponent& commandInput, float64& outValue )
            {
                const SkirmishGameModeComponent* const pDirector = commandInput.findGameMode();
                if ( pDirector == nullptr )
                    return false;
                int32 count = 0;
                for ( const RTSUnitID unitID : commandInput.getSelection().getSelected() )
                {
                    const RTSUnit* const  pUnit  = pDirector->getWorld().findUnit( unitID );
                    const RTSOrder* const pOrder = pUnit != nullptr ? pUnit->findOrder() : nullptr;
                    if ( pOrder != nullptr && pOrder->_type == RTSOrderKind::Move )
                        ++count;
                }
                outValue = static_cast<float64>( count );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( skirmishSelectedCount, "Skirmish.SelectedCount", "Units in the human selection", RTSCommandInputComponent, &RTSCommandInputComponentInternal::readSelectedCount );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishSelectedMovingCount, "Skirmish.SelectedMovingCount", "Selected units whose current order is a move", RTSCommandInputComponent,
                                   &RTSCommandInputComponentInternal::readSelectedMovingCount );
} // namespace sw

namespace sw
{
    RTSCommandInputComponent::RTSCommandInputComponent()
        : _cameraRig{}
        , _watchFocus{ 32.0f, 0.0f, 30.0f }
        , _watchCameraPreset{}
        , _clickDragThreshold{ 0.6f }
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
        , _director{}
        , _selection{}
        , _dragStart{}
        , _dragPoint{}
        , _appliedMatchID{ 0 }
        , _selectionMatchID{ 0 }
        , _bInputLive{ SW_FALSE }
        , _bNamesValid{ SW_FALSE }
        , _bDragging{ SW_FALSE }
        , _bDragPointValid{ SW_FALSE }
        , _bAttackMovePending{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    RTSCommandInputComponent::~RTSCommandInputComponent() = default;

    void RTSCommandInputComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _director = TComponentHandle<SkirmishGameModeComponent>( findGameMode() );
        if ( _director.isValid() == false )
        {
            const GameObject* const pOwner = getOwner();
            SW_LOG_ERROR_ID( "RTSCommandInput.NoGameMode", "RTS command input on '%#': no SkirmishGameModeComponent in front of it on the same object - it reads no input",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
        }
    }

    const SkirmishGameModeComponent* RTSCommandInputComponent::findGameMode() const
    {
        const GameObject* const pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<SkirmishGameModeComponent>() : nullptr;
    }

    void RTSCommandInputComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( RTSCommandInputComponent& commandInput, GameObjectManager& manager )
        { commandInput.updateInput( manager ); } );
    }

    void RTSCommandInputComponent::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion );
        uint64       matchID   = _selectionMatchID;
        RTSSelection selection = _selection;
        io.value( matchID );
        io.value( selection );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _selectionMatchID = matchID;
        _selection        = std::move( selection );
    }

    void RTSCommandInputComponent::restoreState( vector<uint8>&& bytes )
    {
        _selection.setMaxCount( _maxSelectionCount ); // 상한을 넘는 고르기(꾸민 바이트)는 읽기가 거절한다
        Archive archive( bytes.data(), bytes.size() );
        if ( readState( archive ) == false )
            SW_LOG_WARNING_ID( "RTSCommandInput.SavedSelectionRefused", "[Skirmish] the saved unit selection does not match this build - the selection is kept as it is" );
    }

    bool RTSCommandInputComponent::findDragBox( float3& outCenter, float3& outScale ) const
    {
        if ( _bDragging == SW_FALSE || _bDragPointValid == SW_FALSE )
            return false;
        constexpr float32 thickness = RTSCommandInputComponentInternal::kDragBoxThickness;
        outCenter                   = float3{ ( _dragStart._x + _dragPoint._x ) * 0.5f, thickness, ( _dragStart._z + _dragPoint._z ) * 0.5f };
        outScale                    = float3{ MathUtil::abs( _dragPoint._x - _dragStart._x ) + thickness, thickness, MathUtil::abs( _dragPoint._z - _dragStart._z ) + thickness };
        return true;
    }

    void RTSCommandInputComponent::updateInput( GameObjectManager& manager )
    {
        SkirmishGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        const uint64 matchID = pDirector->getMatchID();
        if ( _appliedMatchID != matchID || _selectionMatchID != matchID )
            applyMatch( manager, *pDirector );
        playStepEvents( manager, *pDirector );
        _selection.prune( pDirector->getWorld() );
        const bool                bReadInput = _bInputLive == SW_TRUE && _bNamesValid == SW_TRUE;
        const InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput == nullptr )
            return;
        // 카메라 이동과 확대는 리그(PostPhysics)가 읽는다. 여기서는 속도, 일시정지, 명령을 처리한다.
        // 키는 입력 맵이 정한다. 일시정지 메뉴 같은 UI 가 먹은 입력은 보이지 않는다.
        const GameInputView inputMap( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
        (void)pDirector->submitTimeInput( inputMap.wasActionTriggered( _pauseAction ), inputMap.wasActionTriggered( _fasterAction ), inputMap.wasActionTriggered( _slowerAction ) ); // 거절은 디렉터가 알린다
        if ( inputMap.wasActionTriggered( _statusAction ) )
            pDirector->getMatch().logStatus();
        if ( pDirector->isLocalPlayerHuman() && pDirector->getMatch().isOver() == false )
            updateHumanCommands( manager, *pDirector, *pInput, inputMap );
    }

    void RTSCommandInputComponent::applyMatch( GameObjectManager& manager, const SkirmishGameModeComponent& director )
    {
        // 유닛 id 는 판마다 처음부터 다시 나간다 — 다른 판(세이브 판이 맞지 않아 새로 연 판 포함)의 고르기는 우연히 겹친 유닛을 가리키므로 버린다.
        if ( _selectionMatchID != director.getMatchID() )
            _selection = RTSSelection{};
        _selectionMatchID = director.getMatchID();
        _appliedMatchID   = director.getMatchID();
        _bDragging        = SW_FALSE;
        _selection.setPlayer( director.getLocalPlayer() );
        _selection.setMaxCount( _maxSelectionCount );
        _bNamesValid = hasValidNames( director ) ? SW_TRUE : SW_FALSE;

        // 사람 쪽은 글자 키가 명령이라 카메라는 방향키만 쓴다. 둘 다 AI 면 맵 전체가 보이게 물러선 프리셋(`_watchCameraPreset`, 씬 데이터)으로 시작하고 WASD 도 카메라를 움직인다.
        // 프리셋 고르기는 카메라 오브젝트의 시작 순서에 기대지 않는다(`startWithPreset` — 시작 전이면 시작 프리셋, 뒤면 컷).
        const bool                     bHuman = director.isLocalPlayerHuman();
        CameraDirectorComponent* const pRig   = findCameraRig( manager );
        if ( pRig != nullptr )
        {
            pRig->setPanAction( bHuman ? _playerPanAction : _spectatorPanAction ); // 플레이할 때 WASD 는 명령 단축키다.
            if ( bHuman == false )
            {
                pRig->setFocusPoint( manager.getWorldOrigin().toLocal( _watchFocus ) );
                pRig->startWithPreset( _watchCameraPreset.key() );
            }
        }
        // 2 인 대전의 클라이언트(1 번, 북동)는 씬의 카메라 초점(0 번 본진)이 아니라 자기 본진에서 시작한다.
        const RTSPlayer* pLocalPlayer = director.getWorld().findPlayer( director.getLocalPlayer() );
        if ( pRig != nullptr && bHuman && director.getLocalPlayer() != 0 && pLocalPlayer != nullptr )
            pRig->setFocusPoint( manager.getWorldOrigin().toLocal( float3{ pLocalPlayer->_startPosition._x, 0.0f, pLocalPlayer->_startPosition._z } ) );
        if ( bHuman )
        {
            SW_LOG_INFO_ID( "RTSCommandInput.YouBlueDragSelect", "[Skirmish] you are %# - drag to select, right click to move/attack/gather, A attack-move, S stop, H hold, Q/W/E train, "
                                                                 "B depot · N barracks · G refinery · Y academy · F factory · T starport · U bunker at the cursor, Ctrl+1..9 groups, arrows pan, "
                                                                 "wheel zoom, - = speed, P pause, F1 status",
                            director.getLocalPlayer() == 0 ? "blue" : "red" );
        }
        else
            SW_LOG_INFO_ID( "RTSCommandInput.WatchingComputerVsComputer", "[Skirmish] watching computer vs computer - arrows/WASD pan, wheel zoom, - = speed, P pause, F1 status" );
    }

    bool RTSCommandInputComponent::hasValidNames( const SkirmishGameModeComponent& director ) const
    {
        static constexpr const utf8* kOwner = "RTSCommandInputComponent";
        bool                         bValid = true;
        if ( _listBuildAction.size() != _listBuildUnitID.size() )
        {
            SW_LOG_ERROR_ID( "RTSCommandInput.BuildActionsButBuild", "%#: %# build actions but %# build units - they pair by index", kOwner, static_cast<uint32>( _listBuildAction.size() ),
                             static_cast<uint32>( _listBuildUnitID.size() ) );
            bValid = false;
        }
        for ( const CatalogKey<RTSUnitDef>& unitID : _listBuildUnitID )
        {
            if ( director.findCatalog()->find( unitID ) != nullptr ) // 열린 판은 카탈로그를 쥐고 있다
                continue;
            SW_LOG_ERROR_ID( "RTSCommandInput.BuildUnitNot", "%#: build unit '%#' is not in the game mode's unit catalog", kOwner, unitID.c_str() );
            bValid = false;
        }
        // 관전 카메라 프리셋은 씬 데이터가 준다(코드에 기본 이름이 없다). 카탈로그에 있는지는 카메라가 시작할 때 본다(`CameraDirectorComponent::startWithPreset`).
        if ( _watchCameraPreset.empty() )
        {
            SW_LOG_ERROR_ID( "RTSCommandInput.WatchCameraPresetEmptySceneMust", "%#: _watchCameraPreset is empty - the scene must name the camera preset for AI vs AI", kOwner );
            bValid = false;
        }
        // 입력 액션은 입력 맵에 있어야 한다. 입력이 없는 실행(헤드리스)은 보지 않는다.
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

    void RTSCommandInputComponent::updateHumanCommands( GameObjectManager& manager, SkirmishGameModeComponent& director, const InputManager& input, const GameInputView& inputMap )
    {
        const RTSWorld& world = director.getWorld();
        float3          point{};
        const bool      bPointValid = findGroundPoint( manager, director, input, point );
        const bool      bShift      = inputMap.isActionDown( _addToSelectionAction );
        const bool      bControl    = inputMap.isActionDown( _groupModifierAction );
        updateDrag( director, inputMap, point, bPointValid );

        if ( bPointValid && inputMap.wasActionTriggered( _orderAction ) )
            issueOrder( director, point, bShift );

        // 명령 단축키는 선택한 것이 모두 내 것일 때만 동작한다.
        if ( _selection.isCommandable( world ) && _selection.getSelected().empty() == false )
        {
            if ( inputMap.wasActionTriggered( _attackMoveAction ) )
            {
                _bAttackMovePending = SW_TRUE;
                SW_LOG_INFO_ID( "RTSCommandInput.AttackMoveLeftClick", "[Skirmish] attack-move - left click a target point" );
            }
            const bool bHold = inputMap.wasActionTriggered( _holdAction );
            if ( bHold || inputMap.wasActionTriggered( _stopAction ) )
            {
                RTSCommand command;
                command._kind     = bHold ? RTSCommandKind::Hold : RTSCommandKind::Stop;
                command._listUnit = _selection.getSelected();
                (void)director.submitCommand( std::move( command ) ); // 받은 수는 쓰지 않는다
            }
            for ( size_t productIndex = 0; productIndex < _listTrainAction.size(); ++productIndex )
            {
                if ( inputMap.wasActionTriggered( _listTrainAction[productIndex] ) )
                    trainFromPrimary( director, static_cast<int32>( productIndex ) );
            }
            // 건설 단축키 표는 씬 데이터다(액션 · 지을 유닛 id 가 같은 자리). 길이가 같은지는 판이 열릴 때 봤다.
            for ( size_t buildIndex = 0; bPointValid && buildIndex < _listBuildAction.size(); ++buildIndex )
            {
                if ( inputMap.wasActionTriggered( _listBuildAction[buildIndex] ) )
                    orderBuild( director, _listBuildUnitID[buildIndex].key(), point );
            }
        }

        // 부대 입력: Ctrl + 숫자는 지정, Shift + 숫자는 추가, 숫자는 호출이다. 부대 액션 수는 판이 열릴 때 `RTSSelection::kGroupCount` 와 맞는지 봤다.
        for ( int32 group = 0; group < static_cast<int32>( _listGroupAction.size() ); ++group )
        {
            if ( inputMap.wasActionTriggered( _listGroupAction[static_cast<size_t>( group )] ) == false )
                continue;
            if ( bControl )
                _selection.assignGroup( group );
            else if ( bShift )
                _selection.addToGroup( group );
            else if ( _selection.recallGroup( world, group ) )
                SW_LOG_INFO_ID( "RTSCommandInput.GroupUnits", "[Skirmish] group %# - %# units", group, static_cast<int32>( _selection.getSelected().size() ) );
        }
        if ( inputMap.wasActionTriggered( _jumpToSelectionAction ) )
        {
            // 선택한 유닛으로 이동한다. 카메라 디렉터는 다음 프레임의 PostPhysics 틱 뒤에 이 초점을 읽는다.
            const RTSUnit* const           pPrimary = world.findUnit( _selection.getPrimary() );
            CameraDirectorComponent* const pRig     = findCameraRig( manager );
            if ( pPrimary != nullptr && pRig != nullptr )
                pRig->setFocusPoint( manager.getWorldOrigin().toLocal( float3{ pPrimary->_position._x, 0.0f, pPrimary->_position._z } ) );
        }
    }

    void RTSCommandInputComponent::updateDrag( SkirmishGameModeComponent& director, const GameInputView& inputMap, const float3& point, bool bPointValid )
    {
        const RTSWorld&      world        = director.getWorld();
        const hashed_string& selectAction = _selectAction;
        const bool           bShift       = inputMap.isActionDown( _addToSelectionAction );
        if ( bPointValid && inputMap.wasActionPressed( selectAction ) )
        {
            if ( _bAttackMovePending == SW_TRUE )
            {
                // A 다음 선택(왼쪽 클릭)이면 선택한 병력을 그 위치로 공격 이동시킨다.
                _bAttackMovePending = SW_FALSE;
                RTSCommand command;
                command._kind                      = RTSCommandKind::GroupMove;
                command._listUnit                  = _selection.getSelected();
                command._target                    = point;
                command._bAttackMove               = SW_TRUE;
                command._bQueue                    = bShift ? SW_TRUE : SW_FALSE;
                [[maybe_unused]] const int32 count = director.submitCommand( std::move( command ) );
                SW_LOG_INFO_ID( "RTSCommandInput.UnitsAttackMove", "[Skirmish] %# units attack-move to (%#, %#)", count, static_cast<int32>( point._x ), static_cast<int32>( point._z ) );
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
        if ( dragged < _clickDragThreshold )
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
            SW_LOG_INFO_ID( "RTSCommandInput.SelectedUnits", "[Skirmish] selected %# (%# units)", pPrimary->_pDef->_name.c_str(), static_cast<int32>( _selection.getSelected().size() ) );
            (void)GameSound::postEvent( hashed_string( RTSCommandInputComponentInternal::kSoundSelect ) ); // 소리가 없는 구성이면 내지 않는다
        }
    }

    void RTSCommandInputComponent::issueOrder( SkirmishGameModeComponent& director, const float3& point, bool bQueue )
    {
        const RTSWorld& world = director.getWorld();
        if ( _selection.getSelected().empty() || _selection.isCommandable( world ) == false )
            return;
        // 유닛이나 자원을 눌렀으면 하나씩 상황에 맞는 명령(공격, 채취, 이어 짓기)을 낸다. 빈 땅이면 움직이는 것은 무리 이동, 건물은 집결지 지정이다.
        const RTSUnitID targetID = world.pickUnit( point );
        RTSCommand      rally;
        RTSCommand      smart;
        RTSCommand      groupMove;
        rally._kind     = RTSCommandKind::Rally;
        smart._kind     = RTSCommandKind::Smart;
        groupMove._kind = RTSCommandKind::GroupMove;
        for ( RTSCommand* pCommand : { &rally, &smart, &groupMove } )
        {
            pCommand->_target   = point;
            pCommand->_targetID = targetID;
            pCommand->_bQueue   = bQueue ? SW_TRUE : SW_FALSE;
        }
        for ( const RTSUnitID unitID : _selection.getSelected() )
        {
            const RTSUnit* pUnit = world.findUnit( unitID );
            if ( pUnit == nullptr )
                continue;
            if ( pUnit->isBuilding() )
                rally._listUnit.push_back( unitID );
            else if ( targetID.isValid() && targetID != unitID )
                smart._listUnit.push_back( unitID );
            else
                groupMove._listUnit.push_back( unitID );
        }
        for ( RTSCommand* pCommand : { &rally, &smart, &groupMove } )
        {
            if ( pCommand->_listUnit.empty() == false )
                (void)director.submitCommand( std::move( *pCommand ) ); // 받은 수는 쓰지 않는다
        }
    }

    void RTSCommandInputComponent::orderBuild( SkirmishGameModeComponent& director, const hashed_string& buildingID, const float3& point )
    {
        const RTSWorld&         world = director.getWorld();
        const RTSUnitDef* const pDef  = director.findCatalog()->find( buildingID );
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
            SW_LOG_INFO_ID( "RTSCommandInput.SelectWorkerBuild", "[Skirmish] select a worker to build %#", pDef->_name.c_str() );
            return;
        }
        int2 cell{ static_cast<int32>( MathUtil::floor( point._x ) ) - pDef->_footprint / 2, static_cast<int32>( MathUtil::floor( point._z ) ) - pDef->_footprint / 2 };
        if ( pDef->_bExtractor != SW_FALSE && world.findBuildSite( pDef->_id, point, 0, RTSCommandInputComponentInternal::kGeyserSearchRadius, cell ) == false )
        {
            SW_LOG_INFO_ID( "RTSCommandInput.NoFreeGeyserNear", "[Skirmish] no free geyser near the cursor for %#", pDef->_name.c_str() );
            return;
        }
        RTSCommand command;
        command._kind                      = RTSCommandKind::Build;
        command._listUnit                  = { workerID };
        command._defID                     = pDef->_id;
        command._cell                      = cell;
        [[maybe_unused]] const int32 count = director.submitCommand( std::move( command ) );
        SW_LOG_INFO_ID( "RTSCommandInput.Build", "[Skirmish] build %# at (%#, %#): %#", pDef->_name.c_str(), cell._x, cell._y, count > 0 ? "ok" : count < 0 ? "sent"
                                                                                                                                                            : "refused" );
    }

    void RTSCommandInputComponent::trainFromPrimary( SkirmishGameModeComponent& director, int32 productIndex )
    {
        const RTSUnit* const pPrimary = director.getWorld().findUnit( _selection.getPrimary() );
        if ( pPrimary == nullptr || pPrimary->isBuilding() == false )
            return;
        vector<const RTSUnitDef*> listProduct;
        director.findCatalog()->findProducts( pPrimary->_pDef->_id, listProduct );
        if ( productIndex >= static_cast<int32>( listProduct.size() ) )
            return;
        const RTSUnitDef& product = *listProduct[static_cast<size_t>( productIndex )];
        RTSCommand        command;
        command._kind                      = RTSCommandKind::Train;
        command._listUnit                  = { pPrimary->_id };
        command._defID                     = product._id;
        [[maybe_unused]] const int32 count = director.submitCommand( std::move( command ) );
        SW_LOG_INFO_ID( "RTSCommandInput.Train", "[Skirmish] train %# at %#: %#", product._name.c_str(), pPrimary->_pDef->_name.c_str(), count > 0 ? "ok" : count < 0 ? "sent"
                                                                                                                                                                      : "refused" );
    }

    bool RTSCommandInputComponent::findGroundPoint( GameObjectManager& manager, const SkirmishGameModeComponent& director, const InputManager& input, float3& outPoint ) const
    {
        // 카메라 디렉터가 이 프레임의 PostPhysics 틱 뒤에 낸 포즈로 포인터 아래 땅을 찾는다.
        const CameraDirectorComponent* const pRig = findCameraRig( manager );
        if ( pRig == nullptr || pRig->findGroundPoint( input.getMousePositionNormalized(), 0.0f, outPoint ) == false )
            return false;
        outPoint            = manager.getWorldOrigin().toAbsolute( outPoint ); // RTS 월드는 절대 자리다
        const NavGrid& grid = director.getWorld().getGrid();
        return 0.0f <= outPoint._x && outPoint._x < static_cast<float32>( grid.getWidth() ) && 0.0f <= outPoint._z && outPoint._z < static_cast<float32>( grid.getHeight() );
    }

    CameraDirectorComponent* RTSCommandInputComponent::findCameraRig( GameObjectManager& manager ) const
    {
        GameObject* const pRigObject = manager.resolveGameObject( _cameraRig );
        return pRigObject != nullptr ? pRigObject->getComponent<CameraDirectorComponent>() : nullptr;
    }

    void RTSCommandInputComponent::playStepEvents( const GameObjectManager& manager, const SkirmishGameModeComponent& director ) const
    {
        using Internal = RTSCommandInputComponentInternal;
        // 유닛이 부서지면 누구 것이든 그 위치에서 쇳소리를 낸다. 한 프레임에 여럿이어도 한 번만 낸다(구경하는 AI 대 AI 판에서도 들린다). 동시 재생 상한은 이벤트 데이터가 정한다.
        for ( const RTSEvent& event : director.getStepEvents() )
        {
            if ( event._kind == RTSEvent::Kind::UnitDied )
            {
                (void)GameSound::postEventAt( hashed_string( Internal::kSoundUnitDied ), manager.getWorldOrigin().toLocal( event._position ) ); // 소리가 없는 구성이면 내지 않는다
                break;
            }
        }
        if ( director.isLocalPlayerHuman() == false )
            return;
        // 이 화면의 사람에게 알릴 것만 다룬다. 나머지는 판이 로그로 남긴다.
        for ( const RTSEvent& event : director.getStepEvents() )
        {
            if ( event._player != director.getLocalPlayer() )
                continue;
            const RTSUnitDef*            pDef  = director.findCatalog()->find( event._defID );
            [[maybe_unused]] const utf8* pName = pDef != nullptr ? pDef->_name.c_str() : "?";
            switch ( event._kind )
            {
                case RTSEvent::Kind::ConstructionComplete:
                {
                    SW_LOG_INFO_ID( "RTSCommandInput.Complete", "[Skirmish] %# complete", pName );
                    (void)GameSound::postEvent( hashed_string( Internal::kSoundBuilt ) ); // 소리가 없는 구성이면 내지 않는다
                    break;
                }
                case RTSEvent::Kind::ProductionComplete:
                {
                    SW_LOG_INFO_ID( "RTSCommandInput.Ready", "[Skirmish] %# ready", pName );
                    break;
                }
                case RTSEvent::Kind::SupplyBlocked:
                {
                    SW_LOG_INFO_ID( "RTSCommandInput.NotEnoughSupplyBuild", "[Skirmish] not enough supply - build a Supply Depot (B)" );
                    (void)GameSound::postEvent( hashed_string( Internal::kSoundBlocked ) ); // 소리가 없는 구성이면 내지 않는다
                    break;
                }
                case RTSEvent::Kind::UnderAttack:
                {
                    SW_LOG_INFO_ID( "RTSCommandInput.WeUnderAttack", "[Skirmish] we are under attack at (%#, %#)", static_cast<int32>( event._position._x ), static_cast<int32>( event._position._z ) );
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
} // namespace sw
