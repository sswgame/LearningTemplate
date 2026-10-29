#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameSceneFlow.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Config/GameTimeScale.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"

#include "GameFramework/Base/Foundation/Framework/Flow/LoadingScreenController.h"
#include "GameFramework/Base/Foundation/Framework/Flow/ScreenTransitionManager.h"
#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/GameData/GameSettings.h"

namespace sw
{
    SW_LOG_CALLER( "GameSceneFlow" );

    /**
     * @brief `"-gv_firstScene=<리소스 경로>"` 인자입니다. 팩 설정(gamesettings)의 시작 씬 대신 이 씬을 처음 엽니다.
     * @details 에디터 없이 다른 씬(환경 쇼케이스 등)을 띄워 볼 때 쓴다.
     * @note PowerShell 은 점이 든 인자를 쪼개므로 따옴표로 감싸야 한다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_firstScene, "", "팩 설정의 시작 씬 대신 처음 열 씬의 리소스 경로 (비우면 사용 안 함)" );

    GameSceneFlow::GameSceneFlow( const GameSettings& settings, ScreenTransitionManager& screenTransition, LoadingScreenController& loadingScreen )
        : _settings{ settings }
        , _screenTransition{ screenTransition }
        , _loadingScreen{ loadingScreen }
        , _listPendingSceneLoad{}
        , _sceneLoadRequestedSubscription{}
        , _returnToTitleRegistration{}
        , _bResumingWorld{ SW_FALSE }
    {
    }

    GameSceneFlow::~GameSceneFlow() = default;

    span<const utf8* const> GameSceneFlow::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { ScreenTransitionManager::kSubsystemName, LoadingScreenController::kSubsystemName };
        return kArrDependency;
    }

    SubsystemInitResult GameSceneFlow::initializeSubsystem()
    {
        SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
        // 인스턴스가 살아 있는 월드 위에 다시 섰다(게임 모듈 핫 리로드 · 백엔드 교체) — 첫 씬을 요청하지 않는다(`requestFirstScene`).
        _bResumingWorld = ( pSceneManager != nullptr && pSceneManager->findActiveObjectManager() != nullptr ) ? SW_TRUE : SW_FALSE;
        // 씬 로드를 누가 요청했든 받아들임 알림으로 로딩 화면을 띄운다(언리얼 로딩 화면 모듈이 PreLoadMap 을 구독하는 것과 같다). 첫 씬 요청(onInitialize) 앞에 건다.
        if ( pSceneManager != nullptr )
            _sceneLoadRequestedSubscription = pSceneManager->onSceneLoadRequested().add( SW_DELEGATE_METHOD( SceneLoadRequestedHandler, &GameSceneFlow::onSceneLoadRequested, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void GameSceneFlow::shutdownSubsystem()
    {
        _sceneLoadRequestedSubscription.reset();
        _listPendingSceneLoad.clear();
    }

    void GameSceneFlow::update()
    {
        // 맡긴 씬 로드가 남았거나 씬 매니저가 전환 중이면 로딩 중이다. 페이드를 먼저 진행하고, 로딩 화면이 닫히면 페이드 인을 건다.
        const SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        const bool          bLoading      = _listPendingSceneLoad.empty() == false || ( pSceneManager != nullptr && pSceneManager->isTransitioning() );
        // 로드 진행은 씬 매니저의 프리로드가 안다(쿠킹이 구운 목록이 없으면 모른다 — 로딩 화면은 진행 막대를 접는다).
        std::optional<float32> loadProgress;
        float32                progressValue = 0.0f;
        if ( pSceneManager != nullptr && pSceneManager->tryGetLoadProgress( progressValue ) )
            loadProgress = progressValue;
        // 페이드와 로딩 화면은 실제 시간으로 돌린다. 게임 시간으로 돌리면 정지 메뉴(게임 시간 0)가 뜬 동안 페이드가 반쯤 검은 채로 멈춘다.
        const float32 realDeltaTime = game::getSubsystem<GameTimeScale>().getUnscaledDeltaTime();
        _screenTransition.update( realDeltaTime );
        _loadingScreen.update( realDeltaTime, bLoading, loadProgress, _screenTransition.fade() );
        if ( _listPendingSceneLoad.empty() == false )
            publishFinishedSceneLoads();
    }

    const string& GameSceneFlow::getFirstScene() const
    {
        if ( gv_firstScene.empty() == false )
            return gv_firstScene;
        // 시작 맵은 필수 칸이다 — 읽기(`GameSettings::loadRoot`)가 빠진 것을 오류로 멈춘다.
        return _settings._titleScene.isSet() ? _settings._titleScene.get() : _settings._startMap.get();
    }

    const string& GameSceneFlow::getEntranceScene() const
    {
        return _settings._entranceScene.isSet() ? _settings._entranceScene.get() : _settings._startMap.get();
    }

    bool GameSceneFlow::requestFirstScene()
    {
        if ( _bResumingWorld == SW_TRUE )
        {
            SW_LOG_INFO( "The game instance was recreated over a live scene - keeping it instead of loading the first scene" );
            return false;
        }
        return requestLoadScene( getFirstScene(), "first" );
    }

    bool GameSceneFlow::requestEntranceScene()
    {
        return requestLoadScene( getEntranceScene(), "entrance" );
    }

    bool GameSceneFlow::requestTitleScene()
    {
        const DataPath& titleScene = _settings._titleScene;
        if ( titleScene.isSet() == false )
            return false;
        return requestLoadScene( titleScene.get(), "title" );
    }

    void GameSceneFlow::registerSessionCommands( UIGlobalCommandRegistry& commands )
    {
        // 타이틀 씬이 없는 게임은 "타이틀로" 를 걸지 않는다(창이 없는 호스트는 게임 인스턴스가 이 함수를 부르지 않는다 — UI 가 없다).
        if ( _settings._titleScene.isSet() == false )
            return;
        _returnToTitleRegistration = commands.registerCommand( hashed_string( UIGlobalCommandRegistry::kReturnToTitle ),
                                                               SW_DELEGATE_METHOD( UICommandDelegate, &GameSceneFlow::onReturnToTitleCommand, this ) );
    }

    void GameSceneFlow::unregisterSessionCommands()
    {
        _returnToTitleRegistration.reset(); // 핫 리로드의 새 인스턴스가 그 사이 교체했으면 그 등록은 지우지 않는다
    }

    void GameSceneFlow::onReturnToTitleCommand( [[maybe_unused]] const hashed_string& command, [[maybe_unused]] Widget& source )
    {
        if ( requestTitleScene() == false )
            SW_LOG_ERROR( "Return to title: the title scene '%#' was not requested", _settings._titleScene.c_str() );
    }

    bool GameSceneFlow::requestLoadScene( const string& scenePath, const utf8* pWhich )
    {
        if ( scenePath.empty() )
            return false;
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
        {
            SW_LOG_WARNING( "The %# scene '%#' is not opened - no SceneManager service", pWhich, scenePath.c_str() );
            return false;
        }
        // 로딩 화면 · 로드 이벤트는 받아들임 알림(`onSceneLoadRequested`)이 붙인다 — 시나리오 · 게임 코드가 연 씬도 같은 길이다.
        if ( pSceneManager->requestLoadFuture( scenePath, SceneLoadKind::Travel ).isValid() == false )
        {
            SW_LOG_ERROR( "The %# scene '%#' could not be requested", pWhich, scenePath.c_str() );
            return false;
        }
        return true;
    }

    void GameSceneFlow::onSceneLoadRequested( const string& scenePath, SceneLoadKind kind, const TaskFuture<Scene*>& future )
    {
        if ( kind != SceneLoadKind::Travel )
            return; // 도구가 연 씬(편집기 · 진단)은 게임 흐름이 아니다
        _listPendingSceneLoad.push_back( PendingSceneLoad{ scenePath, future } );
        if ( _loadingScreen.getSettings()._documentPath.isSet() )
            (void)_loadingScreen.beginLoading();

        SceneLoadRequestedEvent event{};
        event._levelName = scenePath;
        GameEventUtil::send( event );
    }

    void GameSceneFlow::publishFinishedSceneLoads()
    {
        // 끝난 것만 순서를 지키며 뺀다. 대기열에서 밀린 요청은 nullptr 로 끝나므로 실패로 알린다.
        size_t keptCount = 0;
        for ( size_t index = 0; index < _listPendingSceneLoad.size(); ++index )
        {
            PendingSceneLoad& pending = _listPendingSceneLoad[index];
            if ( pending._future.isReady() == false )
            {
                if ( keptCount != index )
                    _listPendingSceneLoad[keptCount] = std::move( pending );
                ++keptCount;
                continue;
            }
            SceneLoadCompletedEvent event{};
            event._levelName = pending._scenePath;
            event._bSuccess  = ( pending._future.get() != nullptr );
            GameEventUtil::send( event );
        }
        _listPendingSceneLoad.resize( keptCount );
    }
} // namespace sw
