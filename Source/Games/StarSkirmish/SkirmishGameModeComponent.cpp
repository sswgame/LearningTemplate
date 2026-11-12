#include "pch.h"

#include "Games/StarSkirmish/SkirmishGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/XorShift32.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Control/InputActionCheckUtil.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "Games/StarSkirmish/SkirmishUnitComponent.h"

namespace sw
{
    namespace
    {
        struct SkirmishGameModeComponentInternal
        {
            static constexpr int32  kUnitLookCategory = 5; ///< 0번 팀, 1번 팀, 주인 없음, 광물, 가스.
            static constexpr uint32 kStateTag         = FourCCUtil::make( "RTMS" );
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

        /** @brief 선택한 유닛 중 @p bMoveOnly 면 앞 명령이 이동인 것만 셉니다. */
        [[nodiscard]] bool countSelected( const SkirmishGameModeComponent& gameMode, bool bMoveOnly, float64& outValue )
        {
            int32 count = 0;
            gameMode.getWorld().forEachUnit( [&gameMode, bMoveOnly, &count]( const RTSUnit& unit )
            {
                const RTSOrder* pOrder = unit.findOrder();
                if ( gameMode.isSelected( unit._id ) && ( bMoveOnly == false || ( pOrder != nullptr && pOrder->_type == RTSOrderType::Move ) ) )
                    ++count;
            } );
            outValue = static_cast<float64>( count );
            return true;
        }

        [[nodiscard]] bool readSelectedCount( const SkirmishGameModeComponent& gameMode, float64& outValue )
        {
            return countSelected( gameMode, false, outValue );
        }

        [[nodiscard]] bool readSelectedMovingCount( const SkirmishGameModeComponent& gameMode, float64& outValue )
        {
            return countSelected( gameMode, true, outValue );
        }

        /** @brief 감시 값 `Skirmish.AITask` 입니다. 플레이어 `key`(0 · 1)를 맡은 컴퓨터 상대의 행동 트리가 도는 잎(`GatherIdle` · `TrainWorkers` · `Defend` …)입니다. */
        [[nodiscard]] bool readAITask( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const SkirmishGameModeComponent* pGameMode = AutomationComponentWatch::findFirst<SkirmishGameModeComponent>( query._pManager );
            int32                            player    = -1;
            if ( pGameMode == nullptr || StringUtil::parseInt( query._key, player ) == false )
            {
                outValue.setText( pGameMode == nullptr ? string( "no skirmish game mode" ) : "key must be a player index, got '" + string( query._key ) + "'" );
                return false;
            }
            const RTSCommanderAI* pAI = pGameMode->getMatch().findAI( player );
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

    namespace
    {
        /** @brief 개발 명령입니다. 활성 씬의 디렉터를 찾습니다. */
        struct SkirmishGameModeComponentDevInternal
        {
            [[nodiscard]] static SkirmishGameModeComponent* findDirector( string& outReply )
            {
                const SceneManager* const  pScenes   = game::findSubsystem<SceneManager>();
                const Scene* const         pScene    = pScenes != nullptr ? pScenes->getActiveScene() : nullptr;
                GameObjectManager* const   pManager  = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                SkirmishGameModeComponent* pDirector = nullptr;
                if ( pManager != nullptr )
                {
                    for ( SkirmishGameModeComponent* pFound : pManager->getComponentRegistry().getAll<SkirmishGameModeComponent>() )
                    {
                        if ( pDirector == nullptr )
                            pDirector = pFound;
                    }
                }
                if ( pDirector == nullptr )
                    outReply = "skirmish: no skirmish director in the active scene";
                return pDirector;
            }

            static bool runOrder( const vector<string>& listArgument, string& outReply )
            {
                float32 x = 0.0f;
                float32 z = 0.0f;
                if ( listArgument.size() != 2 || StringUtil::parseFloat( listArgument[0], x ) == false || StringUtil::parseFloat( listArgument[1], z ) == false )
                    return false;
                SkirmishGameModeComponent* const pDirector = findDirector( outReply );
                if ( pDirector == nullptr )
                    return true;
                const int32 player = pDirector->getLocalPlayer();
                RTSCommand  command;
                command._kind        = RTSCommandKind::GroupMove;
                command._target      = float3{ x, 0.0f, z };
                command._bAttackMove = SW_TRUE;
                pDirector->getWorld().forEachUnit( [player, &command]( const RTSUnit& unit )
                {
                    const bool bArmy = unit._owner == player && unit.isMobile() && unit._pDef->_bWorker == SW_FALSE && unit._pDef->canAttack();
                    if ( bArmy && static_cast<int32>( command._listUnit.size() ) < RTSCommand::kMaxUnitCount )
                        command._listUnit.push_back( unit._id );
                } );
                const uint32                             unitCount = static_cast<uint32>( command._listUnit.size() );
                const int32                              applied   = pDirector->submitCommand( std::move( command ) );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "skirmish: player %# attack-moves %# units - %#", player, unitCount, applied < 0 ? "sent with the next lockstep input" : "applied" );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runLockstepStatus( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const SkirmishGameModeComponent* const pDirector = findDirector( outReply );
                if ( pDirector == nullptr )
                    return true;
                const SkirmishLockstepLink& lockstep = pDirector->getLockstep();
                if ( lockstep.isActive() == false )
                {
                    outReply = "skirmish: not a network match";
                    return true;
                }
                const LockstepSession&                   session = lockstep.getSession();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "skirmish lockstep: player %# tick %# stalls %# desynced %# applied %# %#", lockstep.getLocalPlayer(), session.getCurrentTick(), session.getStallCount(),
                                           session.isDesynced() ? 1 : 0, lockstep.getAppliedCommandCount( 0 ), lockstep.getAppliedCommandCount( 1 ) );
                outReply = string( replyBuilder.view() );
                SW_LOG_INFO_ID( "SkirmishGameMode.LockstepStatus", "[Skirmish] %#", outReply.c_str() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( skirmishSelectedCount, "Skirmish.SelectedCount", "Units in the human selection", SkirmishGameModeComponent, &readSelectedCount );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishSelectedMovingCount, "Skirmish.SelectedMovingCount", "Selected units whose current order is a move", SkirmishGameModeComponent, &readSelectedMovingCount );
    SW_DEV_COMMAND( SkirmishOrder, "skirmish.order", "skirmish.order <x> <z>", "Attack-move every army unit of this screen's player to the map point (through the lockstep input in a network match)",
                    &SkirmishGameModeComponentDevInternal::runOrder );
    SW_DEV_COMMAND( SkirmishLockstepStatus, "skirmish.lockstep", "skirmish.lockstep", "Show the network match lockstep tick, stalls, desync and applied command counts",
                    &SkirmishGameModeComponentDevInternal::runLockstepStatus );

    SW_AUTOMATION_QUERY_WATCH( skirmishAITask, "Skirmish.AITask", "Active behavior tree task of the AI commander playing player key (0 or 1)", &readAITask, false, true );
} // namespace sw

namespace sw
{
    SkirmishGameModeComponent::SkirmishGameModeComponent()
        : _unitDataPath{}
        , _unitModelPath{}
        , _commanderTreePath{}
        , _listCliffPrefab{}
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
        , _pCatalog{}
        , _pUnitModelCatalog{}
        , _pCommanderTree{}
        , _commanderDebugSource{ *this }
        , _match{}
        , _lockstep{}
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

    SkirmishGameModeComponent::~SkirmishGameModeComponent() = default;

    void SkirmishGameModeComponent::CommanderDebugSource::collectBehaviorTreeRunners( vector<BehaviorTreeDebugRunner>& outList ) const
    {
        for ( int32 player = 0; player < SkirmishMatch::kPlayerCount; ++player )
        {
            const RTSCommanderAI* pAI = _owner._match.findAI( player );
            if ( pAI == nullptr )
                continue;
            BehaviorTreeDebugRunner& entry = outList.emplace_back();
            entry._name                    = player == 0 ? "Player 0" : "Player 1"; // 판은 두 플레이어(kPlayerCount)
            entry._treePath                = _owner._commanderTreePath.path();
            entry._pRunner                 = &pAI->getRunner();
        }
    }

    void SkirmishGameModeComponent::onBeginPlay()
    {
        GameModeComponent::onBeginPlay(); // 패드 가상 커서는 입력 맵(`virtualCursor`)이 켠다
    }

    void SkirmishGameModeComponent::onEndPlay()
    {
        _unitModels.clear();
        _lockstep.close();
        GameModeComponent::onEndPlay();
    }

    void SkirmishGameModeComponent::onGameStarted()
    {
        // 처음 짓는 건물 · 유닛의 모델을 게임 스레드가 플레이 중에 디스크에서 읽지 않게 판을 열 때 워커로 미리 읽는다(느린 디스크의 히치).
        if ( _unitModels.getCount() == 0 )
        {
            for ( const string& path : _pUnitModelCatalog->makeAllModelPaths() )
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
        // 2 인 대전의 클라이언트(1 번, 북동)는 씬의 카메라 초점(0 번 본진)이 아니라 자기 본진에서 시작한다.
        const RTSPlayer* pLocalPlayer = _match.getWorld().findPlayer( getLocalPlayer() );
        if ( pRig != nullptr && _bHuman == SW_TRUE && getLocalPlayer() != 0 && pLocalPlayer != nullptr )
            pRig->setFocusPoint( float3{ pLocalPlayer->_startPosition._x, 0.0f, pLocalPlayer->_startPosition._z } );
        if ( _bHuman == SW_TRUE )
        {
            SW_LOG_INFO_ID( "SkirmishGameMode.YouBlueDragSelect", "[Skirmish] you are %# - drag to select, right click to move/attack/gather, A attack-move, S stop, H hold, Q/W/E train, "
                                                                  "B depot · N barracks · G refinery · Y academy · F factory · T starport · U bunker at the cursor, Ctrl+1..9 groups, arrows pan, "
                                                                  "wheel zoom, - = speed, P pause, F1 status",
                            getLocalPlayer() == 0 ? "blue" : "red" );
        }
        else
            SW_LOG_INFO_ID( "SkirmishGameMode.WatchingComputerVsComputer", "[Skirmish] watching computer vs computer - arrows/WASD pan, wheel zoom, - = speed, P pause, F1 status" );
    }

    void SkirmishGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        const InputManager* pInput = game::findSubsystem<InputManager>();
        if ( pInput != nullptr )
            updateInput( *pInput );
        if ( _lockstep.isActive() )
            (void)_lockstep.advance( deltaTime, _match.getWorld().getSettings()._fixedStep, _match ); // 두 프로세스가 같은 걸음으로 돈다 - 속도 · 일시정지는 쓰지 않는다
        else if ( _bPaused == SW_FALSE )
            _match.update( deltaTime * _timeScale );
        handleEvents();
        _selection.prune( _match.getWorld() );
        if ( areViewsSpawned() )
            collectUnitChanges();
    }

    void SkirmishGameModeComponent::onViewsDespawned()
    {
        _listUnitSlot.clear();
        _listPendingUnit.clear();
        _bDragging = SW_FALSE;
    }

    const shared_ptr<MaterialInstance>& SkirmishGameModeComponent::findUnitLook( const RTSUnit& unit, bool bSelected ) const
    {
        static const shared_ptr<MaterialInstance> kNoLook{};
        const int32                               lookIndex = SkirmishGameModeComponentInternal::computeLookCategory( unit ) * 2 + ( bSelected ? 1 : 0 );
        if ( lookIndex >= static_cast<int32>( _listUnitLook.size() ) )
            return kNoLook;
        // 뷰가 워커에서 부른다. 컨테이너 인덱스 대신 포인터로 읽는다.
        return _listUnitLook.data()[lookIndex];
    }

    bool SkirmishGameModeComponent::findDragBox( float3& outCenter, float3& outScale ) const
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
    void SkirmishGameModeComponent::serializeState( StateIO& io )
    {
        io.header( SkirmishGameModeComponentInternal::kStateTag, SkirmishGameModeComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        io.value( _match );
        io.valueAtomic( _selection );
        float32 timeScale = _timeScale;
        uint8   bPaused   = static_cast<uint8>( _bPaused );
        io.value( timeScale );
        io.value( bPaused );
        io.end();
        io.check( timeScale > 0.0f );
        if ( io.isReading() && io.isOk() )
        {
            _timeScale = timeScale;
            _bPaused   = bPaused != SW_FALSE ? SW_TRUE : SW_FALSE;
            _listEvent.clear();
        }
    }

    void SkirmishGameModeComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
        {
            SW_LOG_INFO_ID( "SkirmishGameMode.MatchStateRestored", "[Skirmish] match state restored - %#s in", static_cast<int32>( _match.getWorld().getTime() ) );
        }
        else
        {
            // 판이 반쯤 바뀌었을 수 있다. 새 판으로 되돌린다.
            SW_LOG_WARNING_ID( "SkirmishGameMode.SavedMatchStateDoes", "[Skirmish] the saved match state does not match this build - starting a new match" );
            (void)startGame();
        }
    }

    bool SkirmishGameModeComponent::startGame()
    {
        if ( acquireRequired( _unitDataPath, _pCatalog ) == false )
            return false;
        if ( acquireRequired( _unitModelPath, _pUnitModelCatalog ) == false )
            return false;
        if ( acquireRequired( _commanderTreePath, _pCommanderTree ) == false )
            return false;
        if ( hasValidNames() == false )
        {
            SW_LOG_ERROR_ID( "SkirmishGameMode.DirectorInputActionsUnit", "[Skirmish] the game mode's input actions or unit ids do not match the data - the match cannot start" );
            return false;
        }
        const bool bAIStylesSet = _rushAI.isSet() && _macroAI.isSet() && _opponentAI.isSet();
        if ( bAIStylesSet == false )
        {
            SW_LOG_ERROR_ID( "SkirmishGameMode.DirectorNeedsRushAIMacroAI", "[Skirmish] the game mode needs _rushAI, _macroAI and _opponentAI (worker, production and attack targets) - the match cannot start" );
            return false;
        }
        // 넷 세션이 열려 있으면 2 인 락스텝 대전이다(서버 0 번 · 클라이언트 1 번). 자동 플레이면 두 AI 가 양쪽 프로세스에서 같은 판 규칙으로 돈다.
        if ( _lockstep.open() == false )
            return false; // 까닭은 락스텝이 남겼다
        _bHuman                         = isAutoPlayOn() ? SW_FALSE : SW_TRUE;
        const uint8           humanMask = _bHuman == SW_FALSE ? uint8{ 0 } : _lockstep.isActive() ? uint8( SkirmishMatch::kHumanPlayer0 | SkirmishMatch::kHumanPlayer1 )
                                                                                                  : SkirmishMatch::kHumanPlayer0;
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
        matchSettings._pCommanderTree        = &_pCommanderTree->getTree();
        if ( _match.initialize( _pCatalog.get(), humanMask, matchSettings ) == false )
            return false;
        _selection.clear();
        _selection.setPlayer( getLocalPlayer() );
        _selection.setMaxCount( _maxSelectionCount );
        _listEvent.clear();
        return true;
    }

    // ------------------------------------------------------------------------------
    // 스폰(틱이 끝난 뒤, 게임 스레드)
    // ------------------------------------------------------------------------------
    void SkirmishGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
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

    void SkirmishGameModeComponent::spawnCliffs( GameObjectManager& manager )
    {
        // 절벽은 타일마다 바위 하나다(바위는 늘이면 모양이 망가져 줄을 상자 하나로 잇지 않는다). 고르기 · 요는 타일 자리의 해시다.
        const int32 size = SkirmishMatch::kMapSize;
        for ( int32 y = 0; y < size; ++y )
        {
            for ( int32 x = 0; x < size; ++x )
            {
                if ( _match.isCliff( x, y ) == false )
                    continue;
                const uint32    hash   = XorShift32::next( XorShift32::next( static_cast<uint32>( y * size + x ) + 1u ) );
                const string&   prefab = _listCliffPrefab[hash % static_cast<uint32>( _listCliffPrefab.size() )].path();
                GameObject*     pCliff = spawnPrefab( manager, prefab, "SkirmishCliff" );
                SceneComponent* pRoot  = pCliff != nullptr ? pCliff->getComponent<SceneComponent>() : nullptr;
                if ( pRoot == nullptr )
                    continue;
                pRoot->setLocalPosition( float3{ static_cast<float32>( x ) + 0.5f, 0.0f, static_cast<float32>( y ) + 0.5f } );
                pRoot->setLocalRotation( float3{ 0.0f, XorShift32::toUnit24( hash ) * MathUtil::kTwoPi, 0.0f } );
            }
        }
    }

    void SkirmishGameModeComponent::spawnUnit( GameObjectManager& manager, int32 slotIndex )
    {
        UnitSlot&      slot  = _listUnitSlot[static_cast<size_t>( slotIndex )];
        const RTSUnit* pUnit = _match.getWorld().findUnit( slot._shownID );
        if ( pUnit == nullptr )
            return;
        const SkirmishUnitModel* pModel = _pUnitModelCatalog->find( pUnit->_pDef->_id );
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

    void SkirmishGameModeComponent::prepareUnitLooks( Material* pMaterial )
    {
        using Internal = SkirmishGameModeComponentInternal;
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
    void SkirmishGameModeComponent::collectUnitChanges()
    {
        // 보일 유닛은 사람 쪽 화면이 안 보이는 적을 그리지 않게 정한다(자원은 항상 보인다). 슬롯의 유닛이 바뀌었거나 이번에 못 본 슬롯(죽음, 안개)만 쌓는다.
        ++_frameStamp;
        const RTSWorld& world       = _match.getWorld();
        const bool      bHuman      = _bHuman == SW_TRUE;
        const int32     localPlayer = getLocalPlayer();
        world.forEachUnit( [this, &world, bHuman, localPlayer]( const RTSUnit& unit )
        {
            const bool bHiddenFromHuman = bHuman && unit._owner != localPlayer && unit._owner != RTSWorld::kNoOwner && world.isVisibleTo( localPlayer, unit._id ) == false;
            if ( bHiddenFromHuman )
                return;
            if ( _pUnitModelCatalog->find( unit._pDef->_id ) == nullptr )
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

    void SkirmishGameModeComponent::updateInput( const InputManager& input )
    {
        // 카메라 이동과 확대는 리그(PostUpdate)가 읽는다. 여기서는 속도, 일시정지, 명령을 처리한다.
        // 키는 입력 맵(`data/skirmish.input.xml`)이 정한다. 일시정지 메뉴 같은 UI 가 먹은 입력은 보이지 않는다.
        const GameInputView inputMap( input.getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
        const bool          bFaster = inputMap.wasActionTriggered( _fasterAction );
        const bool          bTiming = bFaster || inputMap.wasActionTriggered( _slowerAction ) || inputMap.wasActionTriggered( _pauseAction );
        if ( bTiming && _lockstep.isActive() )
            SW_LOG_INFO_ID( "SkirmishGameMode.TimingInNetMatch", "[Skirmish] speed and pause are off in a network match - both players run the same steps" );
        else if ( bFaster || inputMap.wasActionTriggered( _slowerAction ) )
        {
            _timeScale = MathUtil::clamp( _timeScale * ( bFaster ? 2.0f : 0.5f ), 0.25f, 8.0f );
            SW_LOG_INFO_ID( "SkirmishGameMode.SpeedX", "[Skirmish] speed x%#", _timeScale );
        }
        if ( _lockstep.isActive() == false && inputMap.wasActionTriggered( _pauseAction ) )
        {
            _bPaused = _bPaused == SW_TRUE ? SW_FALSE : SW_TRUE;
            SW_LOG_INFO_ID( "SkirmishGameMode.PauseToggled", "[Skirmish] %#", _bPaused == SW_TRUE ? "paused" : "running" );
        }
        if ( inputMap.wasActionTriggered( _statusAction ) )
            _match.logStatus();
        if ( _bHuman == SW_TRUE && _match.isOver() == false )
            updateHumanCommands( input );
    }

    void SkirmishGameModeComponent::updateHumanCommands( const InputManager& input )
    {
        RTSWorld&           world = _match.getWorld();
        const GameInputView inputMap( input.getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
        float3              point{};
        const bool          bPointValid = findGroundPoint( input, point );
        const bool          bShift      = inputMap.isActionDown( _addToSelectionAction );
        const bool          bControl    = inputMap.isActionDown( _groupModifierAction );
        updateDrag( input, point, bPointValid );

        if ( bPointValid && inputMap.wasActionTriggered( _orderAction ) )
            issueOrder( point, bShift );

        // 명령 단축키는 선택한 것이 모두 내 것일 때만 동작한다.
        if ( _selection.isCommandable( world ) && _selection.getSelected().empty() == false )
        {
            if ( inputMap.wasActionTriggered( _attackMoveAction ) )
            {
                _bAttackMovePending = SW_TRUE;
                SW_LOG_INFO_ID( "SkirmishGameMode.AttackMoveLeftClick", "[Skirmish] attack-move - left click a target point" );
            }
            const bool bHold = inputMap.wasActionTriggered( _holdAction );
            if ( bHold || inputMap.wasActionTriggered( _stopAction ) )
            {
                RTSCommand command;
                command._kind     = bHold ? RTSCommandKind::Hold : RTSCommandKind::Stop;
                command._listUnit = _selection.getSelected();
                (void)submitCommand( std::move( command ) );
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
                SW_LOG_INFO_ID( "SkirmishGameMode.GroupUnits", "[Skirmish] group %# - %# units", group, static_cast<int32>( _selection.getSelected().size() ) );
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

    void SkirmishGameModeComponent::updateDrag( const InputManager& input, const float3& point, bool bPointValid )
    {
        RTSWorld&            world = _match.getWorld();
        const GameInputView  inputMap( input.getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
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
                [[maybe_unused]] const int32 count = submitCommand( std::move( command ) );
                SW_LOG_INFO_ID( "SkirmishGameMode.UnitsAttackMove", "[Skirmish] %# units attack-move to (%#, %#)", count, static_cast<int32>( point._x ), static_cast<int32>( point._z ) );
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
            SW_LOG_INFO_ID( "SkirmishGameMode.SelectedUnits", "[Skirmish] selected %# (%# units)", pPrimary->_pDef->_name.c_str(), static_cast<int32>( _selection.getSelected().size() ) );
            getSoundQueue().queueEvent( SkirmishGameModeComponentInternal::kSoundSelect );
        }
    }

    void SkirmishGameModeComponent::issueOrder( const float3& point, bool bQueue )
    {
        RTSWorld& world = _match.getWorld();
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
                (void)submitCommand( std::move( *pCommand ) );
        }
    }

    bool SkirmishGameModeComponent::hasValidNames() const
    {
        static constexpr const utf8* kOwner = "SkirmishGameModeComponent";
        bool                         bValid = true;
        // AI 와 건설 단축키가 쓰는 유닛 id 는 유닛 카탈로그에 있어야 한다.
        for ( const hashed_string* pUnitID : { &_workerUnitID, &_depotUnitID, &_supplyUnitID, &_productionUnitID, &_armyUnitID } )
        {
            if ( _pCatalog->find( *pUnitID ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "SkirmishGameMode.UnitIdNot", "%#: unit id '%#' is not in %#", kOwner, pUnitID->c_str(), _unitDataPath.c_str() );
            bValid = false;
        }
        // 모델 표와 유닛 카탈로그는 서로 같은 id 를 가져야 한다. 모델이 없는 유닛은 조용히 그려지지 않으므로 시작할 때 막는다.
        for ( const RTSUnitDef& def : _pCatalog->getAll() )
        {
            if ( _pUnitModelCatalog->find( def._id ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "SkirmishGameMode.UnitNoRow", "%#: unit '%#' of %# has no row in %#", kOwner, def._id.c_str(), _unitDataPath.c_str(), _unitModelPath.c_str() );
            bValid = false;
        }
        for ( const SkirmishUnitModel& model : _pUnitModelCatalog->getAll() )
        {
            if ( _pCatalog->find( model._id ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "SkirmishGameMode.ModelRowNot", "%#: model row '%#' of %# is not in %#", kOwner, model._id.c_str(), _unitModelPath.c_str(), _unitDataPath.c_str() );
            bValid = false;
        }
        if ( _listBuildAction.size() != _listBuildUnitID.size() )
        {
            SW_LOG_ERROR_ID( "SkirmishGameMode.BuildActionsButBuild", "%#: %# build actions but %# build units - they pair by index", kOwner, static_cast<uint32>( _listBuildAction.size() ),
                             static_cast<uint32>( _listBuildUnitID.size() ) );
            bValid = false;
        }
        for ( const hashed_string& unitID : _listBuildUnitID )
        {
            if ( _pCatalog->find( unitID ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "SkirmishGameMode.BuildUnitNot", "%#: build unit '%#' is not in %#", kOwner, unitID.c_str(), _unitDataPath.c_str() );
            bValid = false;
        }
        if ( _listCliffPrefab.empty() )
        {
            SW_LOG_ERROR_ID( "SkirmishGameMode.CliffPrefabsEmpty", "%#: _listCliffPrefab is empty - the scene must give at least one cliff prefab", kOwner );
            bValid = false;
        }
        // 관전 카메라 프리셋은 씬 데이터가 준다(코드에 기본 이름이 없다). 카탈로그에 있는지는 카메라가 시작할 때 본다(`CameraDirectorComponent::startWithPreset`).
        if ( _watchCameraPreset.empty() )
        {
            SW_LOG_ERROR_ID( "SkirmishGameMode.WatchCameraPresetEmptySceneMust", "%#: _watchCameraPreset is empty - the scene must name the camera preset for AI vs AI", kOwner );
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

    void SkirmishGameModeComponent::orderBuild( const hashed_string& buildingID, const float3& point )
    {
        RTSWorld&         world = _match.getWorld();
        const RTSUnitDef* pDef  = _pCatalog->find( buildingID );
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
            SW_LOG_INFO_ID( "SkirmishGameMode.SelectWorkerBuild", "[Skirmish] select a worker to build %#", pDef->_name.c_str() );
            return;
        }
        int2 cell{ static_cast<int32>( MathUtil::floor( point._x ) ) - pDef->_footprint / 2, static_cast<int32>( MathUtil::floor( point._z ) ) - pDef->_footprint / 2 };
        if ( pDef->_bExtractor != SW_FALSE && world.findBuildSite( pDef->_id, point, 0, 8, cell ) == false )
        {
            SW_LOG_INFO_ID( "SkirmishGameMode.NoFreeGeyserNear", "[Skirmish] no free geyser near the cursor for %#", pDef->_name.c_str() );
            return;
        }
        RTSCommand command;
        command._kind                      = RTSCommandKind::Build;
        command._listUnit                  = { workerID };
        command._defID                     = pDef->_id;
        command._cell                      = cell;
        [[maybe_unused]] const int32 count = submitCommand( std::move( command ) );
        SW_LOG_INFO_ID( "SkirmishGameMode.Build", "[Skirmish] build %# at (%#, %#): %#", pDef->_name.c_str(), cell._x, cell._y, count > 0 ? "ok" : count < 0 ? "sent"
                                                                                                                                                             : "refused" );
    }

    void SkirmishGameModeComponent::trainFromPrimary( int32 productIndex )
    {
        RTSWorld&      world    = _match.getWorld();
        const RTSUnit* pPrimary = world.findUnit( _selection.getPrimary() );
        if ( pPrimary == nullptr || pPrimary->isBuilding() == false )
            return;
        vector<const RTSUnitDef*> listProduct;
        _pCatalog->findProducts( pPrimary->_pDef->_id, listProduct );
        if ( productIndex >= static_cast<int32>( listProduct.size() ) )
            return;
        const RTSUnitDef& product = *listProduct[static_cast<size_t>( productIndex )];
        RTSCommand        command;
        command._kind                      = RTSCommandKind::Train;
        command._listUnit                  = { pPrimary->_id };
        command._defID                     = product._id;
        [[maybe_unused]] const int32 count = submitCommand( std::move( command ) );
        SW_LOG_INFO_ID( "SkirmishGameMode.Train", "[Skirmish] train %# at %#: %#", product._name.c_str(), pPrimary->_pDef->_name.c_str(), count > 0 ? "ok" : count < 0 ? "sent"
                                                                                                                                                                       : "refused" );
    }

    int32 SkirmishGameModeComponent::submitCommand( RTSCommand&& command )
    {
        if ( _lockstep.isActive() )
            return _lockstep.queueCommand( std::move( command ) ) ? -1 : 0;
        return _match.getWorld().applyCommand( getLocalPlayer(), command );
    }

    bool SkirmishGameModeComponent::findGroundPoint( const InputManager& input, float3& outPoint ) const
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
    void SkirmishGameModeComponent::handleEvents()
    {
        using Internal = SkirmishGameModeComponentInternal;
        _listEvent.clear(); // pollEvents 는 뒤에 붙인다.
        _match.pollEvents( _listEvent );
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
            const RTSUnitDef*            pDef  = _pCatalog->find( event._defID );
            [[maybe_unused]] const utf8* pName = pDef != nullptr ? pDef->_name.c_str() : "?";
            switch ( event._kind )
            {
                case RTSEvent::Kind::ConstructionComplete:
                {
                    SW_LOG_INFO_ID( "SkirmishGameMode.Complete", "[Skirmish] %# complete", pName );
                    getSoundQueue().queueEvent( Internal::kSoundBuilt );
                    break;
                }
                case RTSEvent::Kind::ProductionComplete:
                {
                    SW_LOG_INFO_ID( "SkirmishGameMode.Ready", "[Skirmish] %# ready", pName );
                    break;
                }
                case RTSEvent::Kind::SupplyBlocked:
                {
                    SW_LOG_INFO_ID( "SkirmishGameMode.NotEnoughSupplyBuild", "[Skirmish] not enough supply - build a Supply Depot (B)" );
                    getSoundQueue().queueEvent( Internal::kSoundBlocked );
                    break;
                }
                case RTSEvent::Kind::UnderAttack:
                {
                    SW_LOG_INFO_ID( "SkirmishGameMode.WeUnderAttack", "[Skirmish] we are under attack at (%#, %#)", static_cast<int32>( event._position._x ), static_cast<int32>( event._position._z ) );
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

    CameraDirectorComponent* SkirmishGameModeComponent::findCameraRig() const
    {
        return findOn<CameraDirectorComponent>( _cameraRig );
    }

    void SkirmishGameModeComponent::onRegister( GameObjectManager& manager )
    {
        GameModeComponent::onRegister( manager );
        manager.getComponentRegistry().add<SkirmishGameModeComponent>( this );
    }

    void SkirmishGameModeComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<SkirmishGameModeComponent>( this );
        GameModeComponent::onUnregister( manager );
    }
} // namespace sw
