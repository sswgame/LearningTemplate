#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneSubLevelSubsystem.h"
#include "Engine/Serialization/Serializer/SerializePurpose.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstanceClientHook.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/SaveLock.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

namespace sw
{
    /**
     * @brief `"-gv_firstMinigame=<이름>"` 인자입니다. 첫 씬(`gv_firstScene` > 타이틀 > 시작 맵) 대신 이 미니게임(`<팩>/minigames/<이름>.minigame.xml`)으로 들어갑니다.
     * @details 언리얼 Lyra 의 URL 옵션 `?Experience=` 자리다. 전역 변수라 시나리오 `<Launch>` 가 그대로 쓰고 체인 단계마다 되돌려진다(따로 둔 실행 인자는 단계마다 바꿀 길이 없었다).
     *          `gv_firstScene` 처럼 배포에도 있다 — 배포본의 시나리오 · 전용 서버도 미니게임으로 바로 들어간다. 돌아올 자리가 없으므로 나가면 입구 씬으로 간다.
     */
    SW_GLOBAL_VARIABLE( sw::GlobalString, gv_firstMinigame, "", "첫 씬 대신 처음 들어갈 미니게임 이름 (비우면 사용 안 함)" );

    namespace
    {
        struct MinigameFlowInternal
        {
            /** @brief 활성 씬(오브젝트가 선 것)입니다. 없으면 nullptr 입니다. */
            static Scene* findActivePlayScene()
            {
                const SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                Scene* const              pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
                return ( pScene != nullptr && pScene->getObjectManager() != nullptr ) ? pScene : nullptr;
            }

            /** @brief `minigame.enter <이름>` 의 몸통입니다. 맡기기만 하고 켜졌는지는 로그 · 감시 값으로 본다(맵 로드를 기다린다). */
            static bool runEnterCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                MinigameFlow* const                      pFlow = game::findSubsystem<MinigameFlow>();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( pFlow == nullptr )
                    replyBuilder.append( "minigame.enter: no game is running" );
                else
                    replyBuilder.appendFormat( "minigame.enter: %# '%#'", pFlow->enter( listArgument[0] ) ? "entering" : "refused", listArgument[0].c_str() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `minigame.leave` 의 몸통입니다 — 미니게임을 끄고 돌아올 자리로 간다(돌아왔는지는 감시 값 `Minigame.Active` · `Minigame.RestoreCount`). */
            static bool runLeaveCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    outReply = "minigame.leave: no game is running";
                else
                    outReply = pFlow->leave() ? "minigame.leave: leaving" : "minigame.leave: refused";
                return true;
            }

            /** @brief 감시 값 `Minigame.Active` 입니다. 켜진 미니게임 이름이고, 없으면 `none` 입니다(맵을 기다리는 중도 `none`). */
            [[nodiscard]] static bool readActive( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                {
                    outValue.setText( "no game is running" );
                    return false;
                }
                const MinigameDefinition* const pActive = pFlow->isEntering() ? nullptr : pFlow->findActiveDefinition();
                outValue.setText( pActive != nullptr ? pActive->_name.c_str() : "none" );
                return true;
            }

            /** @brief 감시 값 `Minigame.LeakCount` 입니다 — 이 인스턴스에서 끈 미니게임 범위가 남긴 것의 합(0 이 아니면 `GameFeatureScope.Leaked*` 오류도 남았다). */
            [[nodiscard]] static bool readLeakCount( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getLeakCount() );
                return true;
            }

            /** @brief 감시 값 `Minigame.RestoreCount` 입니다 — 씬 전환 미니게임에서 나와 돌아올 자리를 복원한 수(나가기가 끝났는지 기다리는 값). */
            [[nodiscard]] static bool readRestoreCount( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getRestoreCount() );
                return true;
            }
        };
    } // namespace

    MinigameFlow::MinigameFlow( SubsystemScope& gameScope, const GamePackConfig& packConfig, GameInstance& owner )
        : _gameScope( gameScope )
        , _packConfig( packConfig )
        , _owner( owner )
        , _pActive{ nullptr }
        , _activeDefinition{}
        , _subLevelScenePath{}
        , _pSuspendedScene{ nullptr }
        , _pendingEnter{}
        , _sceneLoadRequested{}
        , _leakCount{ 0 }
        , _restoreCount{ 0 }
        , _bEntering{ SW_FALSE }
        , _bLeaving{ SW_FALSE }
        , _bRequestingScene{ SW_FALSE }
    {
    }

    MinigameFlow::~MinigameFlow() = default;

    SubsystemInitResult MinigameFlow::initializeSubsystem()
    {
        // 누가 요청했든 게임 이동을 듣는다 — 세이브 불러오기 · 타이틀로 · 시나리오의 씬 열기가 돌아올 자리를 낡게 만든다(씬 흐름 `GameSceneFlow` 와 같은 알림).
        SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager != nullptr )
            _sceneLoadRequested = pSceneManager->onSceneLoadRequested().add( SW_DELEGATE_METHOD( SceneLoadRequestedHandler, &MinigameFlow::onSceneLoadRequested, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void MinigameFlow::shutdownSubsystem()
    {
        _sceneLoadRequested.reset();
        _pendingEnter.clear();
        _pSuspendedScene.reset();
        _bLeaving = SW_FALSE;
        deactivateActive(); // 게임 범위 기능은 게임 인스턴스가 이 뒤에 끈다(미니게임의 해제 검사는 그것이 켜진 채로 찍었다)
    }

    bool MinigameFlow::enterFirstMinigame()
    {
        const string name = gv_firstMinigame.get();
        if ( name.empty() )
            return false;
        SW_LOG_INFO_ID( "MinigameFlow.FirstMinigame", "First minigame %# (gv_firstMinigame)", name.c_str() );
        (void)enter( name ); // 실패는 위에서 알렸다 — 다른 씬으로 대신하지 않는다
        return true;
    }

    string MinigameFlow::makeAssetPath( const GamePackConfig& packConfig, string_view name )
    {
        StringBuilder<constant::buffer::kMax256> relativeBuilder;
        relativeBuilder.appendFormat( "%#%#%#", kMinigameFolder, name, kMinigameSuffix );
        return packConfig.resolve( relativeBuilder.view() );
    }

    bool MinigameFlow::loadDefinition( string_view name, MinigameDefinition& outDefinition ) const
    {
        const string assetPath = makeAssetPath( _packConfig, name );
        if ( MinigameDefinition::loadFromAsset( AssetID{ assetPath }, outDefinition ) == false )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.NotLoaded", "Minigame '%#' (%#) could not be read (see the messages above)", name, assetPath.c_str() );
            return false;
        }
        if ( outDefinition._name != hashed_string( name ) )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.NameMismatch", "%#: name=\"%#\" differs from the file name '%#'", assetPath.c_str(), outDefinition._name.c_str(), name );
            return false;
        }
        // 등록부는 GameFramework 의 모듈 서브시스템이다 — 없으면 호스트가 모듈을 올리지 않은 것이라 멈춘다.
        return outDefinition.validate( game::getSubsystem<GameFeatureRegistry>(), assetPath );
    }

    bool MinigameFlow::rejectIfLoadPending( string_view name ) const
    {
        if ( isEntering() == false && isLeaving() == false )
            return false;
        SW_LOG_ERROR_ID( "MinigameFlow.Busy", "Minigame request '%#' is refused - %# is still waiting for its map", name,
                         isEntering() ? _activeDefinition._name.c_str() : "leaving the minigame" );
        return true;
    }

    bool MinigameFlow::requestScene( const string& scenePath, const Delegate<void( Scene* pScene )>& onLoaded )
    {
        // 받아들임 알림은 요청 안에서 동기로 온다 — 그동안만 이 흐름의 요청으로 표시해 장치 밖 이동으로 보지 않게 한다.
        _bRequestingScene   = SW_TRUE;
        const bool bRequest = game::getSubsystem<GameSceneFlow>().requestSceneThen( scenePath, onLoaded );
        _bRequestingScene   = SW_FALSE;
        return bRequest;
    }

    bool MinigameFlow::suspendActiveScene( string_view name )
    {
        Scene* const pScene = MinigameFlowInternal::findActivePlayScene();
        if ( pScene == nullptr )
            return true; // 첫 진입이 미니게임이다 — 돌아올 자리가 없어 나가면 입구 씬으로 간다
        if ( pScene->getSourcePath().empty() )
        {
            SW_LOG_WARNING_ID( "MinigameFlow.SceneNotSuspendable", "Minigame '%#': the active scene has no source path to reopen - leaving goes to the entrance scene", name );
            return true;
        }
        // 들어가기 직전에 저장을 한 번 맡긴다 — 미니게임 동안은 막히므로 바깥 진행의 마지막 좋은 저장이 여기 남는다. 정책이 없는 게임은 자동 저장이 없다.
        if ( AutosaveManager* const pAutosave = game::findSubsystem<AutosaveManager>() )
            (void)pAutosave->saveNow( AutosaveTrigger::AreaChanged, hashed_string( name ) ); // 맡기지 못했으면(쓰는 중 · 막힘) 마지막 저장이 그대로 남는다
        unique_ptr<SuspendedScene> pSuspended = make_unique<SuspendedScene>();
        pSuspended->_scenePath                = pScene->getSourcePath();
        if ( _owner.captureSnapshot( SerializePurpose::Snapshot, pSuspended->_snapshot ) == false )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.SceneNotCaptured", "Minigame '%#': the active scene could not be captured before switching scenes - nothing is entered", name );
            return false;
        }
        // 돌아올 자리가 있는 동안 저장을 막는다 — 세이브가 미니게임 맵을 바깥 씬으로 적지 않게(컷씬 막기와 같은 길, 자리가 사라지면 풀린다).
        pSuspended->_saveBlock = _owner.getSaveLock().block( hashed_string( kSaveBlockReason ) );
        _pSuspendedScene       = std::move( pSuspended );
        return true;
    }

    bool MinigameFlow::enter( string_view name )
    {
        if ( rejectIfLoadPending( name ) )
            return false;
        if ( _pActive != nullptr && _activeDefinition._name == hashed_string( name ) )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.AlreadyActive", "Minigame '%#' is already active - leave it first", name );
            return false;
        }
        MinigameDefinition definition;
        if ( loadDefinition( name, definition ) == false )
            return false;
        if ( definition._entry == MinigameEntry::SubLevel && MinigameFlowInternal::findActivePlayScene() == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.SubLevelNeedsScene", "Minigame '%#' (entry=\"SubLevel\") is added to the active scene - there is no active scene", name );
            return false;
        }
        // 진입은 원자적이다 — 읽기 · 검사가 끝난 뒤 앞 미니게임의 범위만 끄고(같은 서비스를 둘이 걸 수 없다), 새 것이 지면 앞 것을 다시 켠다. 앞 하위 레벨은 새 것이 선 뒤에 뺀다.
        // 맵의 컴포넌트가 씬 적용(`beginPlay`) 때 기능 서비스 · 카탈로그를 찾으므로 켜기는 맵 요청 앞이다.
        optional<PreviousMinigame> previous = takeActiveForReplace();
        // 씬을 바꾸면 활성 씬을 돌아올 자리로 찍는다. 이미 자리가 있으면(씬 전환 미니게임에서 다른 미니게임으로 교체) 처음 자리로 돌아간다.
        const bool bSuspendHere = definition._entry == MinigameEntry::SceneSwitch && _pSuspendedScene == nullptr;
        const auto rollback     = [this, bSuspendHere, &previous]()
        {
            if ( bSuspendHere )
                _pSuspendedScene.reset(); // 들어가지 못했다 — 여기서 찍은 자리와 저장 막기를 버린다
            restorePrevious( previous );
        };
        if ( bSuspendHere && suspendActiveScene( name ) == false )
        {
            rollback();
            return false;
        }
        _pActive = activate( definition );
        if ( _pActive == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.NotActivated", "Minigame '%#' could not be activated (see the messages above) - the previous state is kept", name );
            rollback();
            return false;
        }
        _activeDefinition    = std::move( definition );
        const string mapPath = _activeDefinition._map.get();
        if ( _activeDefinition._entry == MinigameEntry::SubLevel )
        {
            // 하위 레벨 이름은 미니게임 이름이다 — 나올 때 그 이름으로 뺀다. 자리는 원점이다(맵을 바탕 좌표로 놓는다 — 언리얼 레벨 인스턴스를 원점에 두는 꼴).
            Scene* const            pScene    = MinigameFlowInternal::findActivePlayScene();
            SceneSubLevelSubsystem& subLevels = pScene->getObjectManager()->getSubsystem<SceneSubLevelSubsystem>();
            if ( subLevels.addSubLevel( *pScene, name, mapPath, SubLevelPlacement{} ) == false )
            {
                SW_LOG_ERROR_ID( "MinigameFlow.SubLevelRefused", "Minigame '%#': the map '%#' could not be added to the active scene (see the messages above)", name, mapPath.c_str() );
                deactivateScope();
                rollback();
                return false;
            }
            _subLevelScenePath = pScene->getSourcePath();
        }
        else
        {
            if ( requestScene( mapPath, GameSceneFlow::SceneLoadedFunction::create<&MinigameFlow::onMapLoaded>( this ) ) == false )
            {
                SW_LOG_ERROR_ID( "MinigameFlow.MapRequestRefused", "Minigame '%#': the map '%#' could not be requested", name, mapPath.c_str() );
                deactivateScope();
                rollback();
                return false;
            }
            _bEntering = SW_TRUE;
        }
        // 새 것이 섰다 — 앞 하위 레벨을 뺀다(씬 전환이면 곧 씬과 함께 사라지지만 그동안 보이지 않게 지금 뺀다).
        if ( previous.has_value() )
            removeSubLevel( previous->_definition, previous->_subLevelScenePath );
        return true;
    }

    optional<MinigameFlow::PreviousMinigame> MinigameFlow::takeActiveForReplace()
    {
        if ( _pActive == nullptr )
            return nullopt;
        PreviousMinigame previous{ _activeDefinition, _subLevelScenePath };
        _subLevelScenePath.clear();
        deactivateScope();
        return previous;
    }

    void MinigameFlow::restorePrevious( optional<PreviousMinigame>& previous )
    {
        if ( previous.has_value() == false )
            return;
        // 같은 정의는 방금까지 켜져 있었다 — 다시 켜지 않으면(그 사이 기능 모듈이 내려갔다) 하위 레벨까지 거두고 켜진 미니게임 없이 둔다.
        _pActive = activate( previous->_definition );
        if ( _pActive == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.PreviousNotRestored", "Minigame '%#' could not be activated again after the failed entry - no minigame is active", previous->_definition._name.c_str() );
            removeSubLevel( previous->_definition, previous->_subLevelScenePath );
            previous = nullopt;
            return;
        }
        _activeDefinition  = std::move( previous->_definition );
        _subLevelScenePath = std::move( previous->_subLevelScenePath );
        previous           = nullopt;
    }

    bool MinigameFlow::leave()
    {
        if ( rejectIfLoadPending( "leave" ) )
            return false;
        if ( _pActive == nullptr && _pSuspendedScene == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.NothingToLeave", "There is no minigame to leave" );
            return false;
        }
        const bool bSubLevel = _pActive != nullptr && _activeDefinition._entry == MinigameEntry::SubLevel;
        deactivateActive();
        if ( _pSuspendedScene != nullptr )
        {
            const string scenePath = _pSuspendedScene->_scenePath;
            if ( requestScene( scenePath, GameSceneFlow::SceneLoadedFunction::create<&MinigameFlow::onReturnLoaded>( this ) ) == false )
            {
                SW_LOG_ERROR_ID( "MinigameFlow.ReturnRequestRefused", "The scene to return to '%#' could not be requested", scenePath.c_str() );
                return false;
            }
            _bLeaving = SW_TRUE;
            return true;
        }
        if ( bSubLevel )
            return true; // 하위 레벨을 뺐다 — 바탕 씬은 그대로 섰다
        // 돌아올 자리가 없다(첫 진입이 미니게임) — gamesettings 입구 씬으로 간다(타이틀의 "시작" 과 같은 씬).
        const string& entrance = game::getSubsystem<GameSceneFlow>().getEntranceScene();
        if ( entrance.empty() )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.NoEntranceScene", "There is no scene to return to - the minigame was entered first and GameSettings has no entrance scene or start map" );
            return false;
        }
        if ( requestScene( entrance, GameSceneFlow::SceneLoadedFunction::create<&MinigameFlow::onReturnLoaded>( this ) ) == false )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.EntranceRequestRefused", "The entrance scene '%#' could not be requested", entrance.c_str() );
            return false;
        }
        _bLeaving = SW_TRUE;
        return true;
    }

    void MinigameFlow::requestEnter( string_view name )
    {
        if ( _pendingEnter.empty() == false )
        {
            SW_LOG_WARNING_ID( "MinigameFlow.EnterAlreadyRequested", "Minigame '%#' is dropped - '%#' was requested first in this frame", name, _pendingEnter.c_str() );
            return;
        }
        _pendingEnter = string( name );
    }

    void MinigameFlow::update()
    {
        // 다시 읽힌 카탈로그를 먼저 건다 — 이 프레임의 진입 · 컴포넌트가 새 판을 찾는다.
        if ( _pActive != nullptr )
            _pActive->rebindReloadedCatalogs();
        if ( _pendingEnter.empty() )
            return;
        const string name = std::move( _pendingEnter );
        _pendingEnter.clear();
        (void)enter( name ); // 실패는 enter 가 알렸다
    }

    const MinigameDefinition* MinigameFlow::findActiveDefinition() const
    {
        return _pActive != nullptr ? &_activeDefinition : nullptr;
    }

    void MinigameFlow::deactivateActive()
    {
        _bEntering = SW_FALSE; // 기다리던 맵의 완료 알림이 와도 할 일이 없다
        if ( _pActive == nullptr )
            return;
        removeSubLevel( _activeDefinition, _subLevelScenePath );
        _subLevelScenePath.clear();
        deactivateScope();
    }

    void MinigameFlow::removeSubLevel( const MinigameDefinition& definition, const string& scenePath )
    {
        // 하위 레벨은 바탕 씬이 아직 활성일 때만 뺀다 — 씬이 바뀌었으면 씬 끝내기가 이미 버렸다.
        if ( definition._entry != MinigameEntry::SubLevel || scenePath.empty() )
            return;
        Scene* const pScene = MinigameFlowInternal::findActivePlayScene();
        if ( pScene == nullptr || pScene->getSourcePath() != scenePath )
            return;
        SceneSubLevelSubsystem& subLevels = pScene->getObjectManager()->getSubsystem<SceneSubLevelSubsystem>();
        SubLevelState           state{ SubLevelState::Queued };
        if ( subLevels.findSubLevelState( definition._name.view(), state ) )
            (void)subLevels.removeSubLevel( definition._name.view() ); // 방금 찾은 이름이다
    }

    void MinigameFlow::deactivateScope()
    {
        _bEntering = SW_FALSE;
        if ( _pActive == nullptr )
            return;
        const uint32 leakCount = _pActive->deactivate(); // 범위가 건 역순으로 거두고 남은 것을 알린다
        _leakCount += leakCount;
        SW_LOG_INFO_ID( "MinigameFlow.Deactivated", "Minigame '%#' is deactivated (%# left behind)", _activeDefinition._name.c_str(), leakCount );
        _pActive.reset();
    }

    void MinigameFlow::onSceneLoadRequested( const string& scenePath, SceneLoadKind kind, const TaskFuture<Scene*>& future )
    {
        (void)future;
        if ( kind != SceneLoadKind::Travel || _bRequestingScene == SW_TRUE )
            return; // 도구가 연 씬이거나 이 흐름의 요청이다
        if ( _pActive == nullptr && _pSuspendedScene == nullptr && isEntering() == false && isLeaving() == false )
            return;
        clearForOutsideTravel( scenePath );
    }

    void MinigameFlow::clearForOutsideTravel( const string& scenePath )
    {
        SW_LOG_INFO_ID( "MinigameFlow.ClearedByTravel", "Travel to '%#' was requested outside the minigame flow - the active minigame and the scene to return to are dropped", scenePath.c_str() );
        _bLeaving = SW_FALSE;
        _pSuspendedScene.reset(); // 저장 막기도 풀린다 — 새 씬(세이브 · 타이틀)이 바깥 진행이다
        deactivateActive();
    }

    void MinigameFlow::onMapLoaded( Scene* pScene )
    {
        if ( isEntering() == false || _pActive == nullptr )
            return;
        _bEntering = SW_FALSE;
        if ( pScene != nullptr )
            return;
        SW_LOG_ERROR_ID( "MinigameFlow.MapNotOpened", "Minigame '%#': the map '%#' did not open (failed or replaced by a later request) - the minigame is deactivated", _activeDefinition._name.c_str(),
                         _activeDefinition._map.c_str() );
        deactivateActive();
    }

    void MinigameFlow::onReturnLoaded( Scene* pScene )
    {
        if ( isLeaving() == false )
            return;
        _bLeaving                                       = SW_FALSE;
        const unique_ptr<SuspendedScene> pSuspendedHere = std::move( _pSuspendedScene ); // 끝나면 저장 막기가 풀린다
        if ( pScene == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.ReturnSceneNotOpened", "The scene to return to did not open (failed or replaced by a later request) - the captured scene is dropped" );
            return;
        }
        if ( pSuspendedHere == nullptr )
            return; // 돌아올 자리가 없던 미니게임이었다 — 입구 씬을 새로 열었다
        // 세이브 불러오기와 같은 길이다 — 찍은 씬을 연 뒤 같은 목적의 봉투를 읽는다(`GameInstance::onSaveSceneLoaded`).
        if ( _owner.restoreSnapshot( SerializePurpose::Snapshot, pSuspendedHere->_snapshot ) == false )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.SceneNotRestored", "The captured scene could not be restored (see the messages above) - it stays as it opened" );
            return;
        }
        ++_restoreCount;
        SW_LOG_INFO_ID( "MinigameFlow.Restored", "Returned to '%#' - the captured scene is restored", pSuspendedHere->_scenePath.c_str() );
    }

    unique_ptr<GameFeatureScope> MinigameFlow::activate( const MinigameDefinition& definition )
    {
        InputManager* const          pInput = game::findSubsystem<InputManager>(); // 입력 서비스가 없는 호스트(전용 서버)는 입력 레이어를 걸지 않는다
        unique_ptr<GameFeatureScope> scope  = make_unique<GameFeatureScope>( _gameScope, game::getSubsystem<GlobalVariableManager>(), _packConfig, pInput != nullptr ? &pInput->getInputMap() : nullptr,
                                                                             definition._name, GameFeatureScope::CatalogRule::Declared, definition._listCatalog );
        if ( scope->activateFeatures( definition._listFeature ) == false )
            return nullptr;
        // 적은 카탈로그를 아무 기능도 읽지 않았으면 데이터와 기능이 어긋났다 — 적지 않은 것을 읽는 쪽은 `GameFeatureScope::loadCatalog` 가 막았다.
        if ( const hashed_string* const pUnloaded = scope->findUnloadedCatalog() )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.CatalogNotLoaded", "Minigame '%#': catalog '%#' is declared but no feature in features=\"...\" loaded it", definition._name.c_str(), pUnloaded->c_str() );
            return nullptr;
        }
        if ( pushInputLayers( *scope, definition ) == false )
            return nullptr;
        // UI 는 클라이언트 전용 부분이 연다 — 전용 서버(창구가 없다)는 화면이 없다.
        IGameInstanceClientHook* const pClientHook = IGameInstanceClientHook::find();
        if ( pClientHook != nullptr && pClientHook->openMinigameUI( *scope, definition ) == false )
            return nullptr;
        scope->finishActivation();
        SW_LOG_INFO_ID( "MinigameFlow.Activated", "Minigame '%#' is active (%# features)", definition._name.c_str(), static_cast<uint32>( definition._listFeature.size() ) );
        return scope;
    }

    bool MinigameFlow::pushInputLayers( GameFeatureScope& scope, const MinigameDefinition& definition )
    {
        if ( definition._listInputContext.empty() )
            return true;
        InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.InputContextNeedsInput", "Minigame '%#' lists input contexts but the engine service InputManager is not bound to the game services", definition._name.c_str() );
            return false;
        }
        InputMap& inputMap = pInput->getInputMap();
        for ( const hashed_string& layer : definition._listInputContext )
        {
            if ( inputMap.hasLayer( layer ) == false )
            {
                SW_LOG_ERROR_ID( "MinigameFlow.InputContextMissing", "Minigame '%#': input context '%#' is not a layer of the game input map bundle", definition._name.c_str(), layer.c_str() );
                return false;
            }
            inputMap.pushLayer( layer );
            // 같은 입력 맵을 끌 때 찾는다 — 입력 서비스는 엔진 수명이라 미니게임보다 오래 산다.
            scope.addCleanup( Delegate<void()>::create( [pInput, layer]()
            { pInput->getInputMap().popLayer( layer ); } ) );
        }
        return true;
    }

    // 시나리오 · 개발용 입구 — 맵의 진입 자리는 `MinigameTriggerComponent` 다.
    SW_DEV_COMMAND( MinigameEnter, "minigame.enter", "minigame.enter <name>", "Enter a minigame (<pack>/minigames/<name>.minigame.xml)", &MinigameFlowInternal::runEnterCommand );
    SW_DEV_COMMAND( MinigameLeave, "minigame.leave", "minigame.leave",
                    "Leave the active minigame and return to the scene it was entered from (restored from the capture on a scene switch; the entrance scene without one)",
                    &MinigameFlowInternal::runLeaveCommand );
    SW_AUTOMATION_QUERY_WATCH( minigameActive, "Minigame.Active", "Name of the active minigame; none without one (also while it waits for its map)", &MinigameFlowInternal::readActive, false, false );
    SW_AUTOMATION_WATCH( minigameLeakCount, "Minigame.LeakCount", "Bindings left behind by deactivated minigame scopes in this game instance (leak check; 0 when clean)",
                         &MinigameFlowInternal::readLeakCount );
    SW_AUTOMATION_WATCH( minigameRestoreCount, "Minigame.RestoreCount", "Times the scene captured before a scene-switch minigame was restored on leaving", &MinigameFlowInternal::readRestoreCount );
} // namespace sw
