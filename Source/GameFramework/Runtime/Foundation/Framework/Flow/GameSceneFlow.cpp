#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameSceneFlow.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Config/GameTimeScale.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstanceClientHook.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/ScreenTransitionManager.h"
#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/SaveGate.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

namespace sw
{
    /**
     * @brief `"-gv_firstScene=<리소스 경로>"` 인자입니다. 팩 설정(gamesettings)의 시작 씬 대신 이 씬을 처음 엽니다.
     * @details 배포 전용 서버가 띄울 맵을 고르는 길이라 배포에도 있다(언리얼 `Server.exe <맵>` · 실행 인자 URL 의 맵 자리, `gv_netConnectAddress` 와 같은 일반 변수).
     *          에디터 새 창 플레이 · 멀티 프로세스 재생 · 시나리오(`<Launch>`)도 이것으로 시작 씬을 준다.
     * @note PowerShell 은 점이 든 인자를 쪼개므로 따옴표로 감싸야 한다.
     */
    SW_GLOBAL_VARIABLE( sw::string, gv_firstScene, "", "팩 설정의 시작 씬 대신 처음 열 씬의 리소스 경로 (비우면 사용 안 함)" );

    GameSceneFlow::GameSceneFlow( const GameSettings& settings, ScreenTransitionManager& screenTransition, ILoadingScreen* pLoadingScreen, SaveGate& saveGate )
        : _settings{ settings }
        , _screenTransition{ screenTransition }
        , _pLoadingScreen{ pLoadingScreen }
        , _saveGate{ saveGate }
        , _listPendingSceneLoad{}
        , _sceneLoadRequestedSubscription{}
        , _returnToTitleRegistration{}
        , _bResumingWorld{ SW_FALSE }
    {
    }

    GameSceneFlow::~GameSceneFlow() = default;

    span<const utf8* const> GameSceneFlow::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { ScreenTransitionManager::kSubsystemName }; // 로딩 화면(클라이언트 전용)은 생성자에서 받고 갱신 때만 부른다
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
        optional<float32> loadProgress;
        float32           progressValue = 0.0f;
        if ( pSceneManager != nullptr && pSceneManager->tryGetLoadProgress( progressValue ) )
            loadProgress = progressValue;
        // 페이드와 로딩 화면은 실제 시간으로 돌린다. 게임 시간으로 돌리면 정지 메뉴(게임 시간 0)가 뜬 동안 페이드가 반쯤 검은 채로 멈춘다.
        const float32 realDeltaTime = game::getSubsystem<GameTimeScale>().getUnscaledDeltaTime();
        _screenTransition.update( realDeltaTime );
        if ( _pLoadingScreen != nullptr )
            _pLoadingScreen->update( realDeltaTime, bLoading, loadProgress, _screenTransition.fade() );
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
            SW_LOG_INFO_ID( "GameSceneFlow.GameInstanceRecreatedOver", "The game instance was recreated over a live scene - keeping it instead of loading the first scene" );
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

    bool GameSceneFlow::hasTitleScene() const
    {
        return _settings._titleScene.isSet();
    }

    void GameSceneFlow::holdSessionCommand( Subscription registration )
    {
        _returnToTitleRegistration = std::move( registration );
    }

    void GameSceneFlow::unregisterSessionCommands()
    {
        _returnToTitleRegistration.reset(); // 핫 리로드의 새 인스턴스가 그 사이 교체했으면 그 등록은 지우지 않는다
    }

    void GameSceneFlow::onReturnToTitleCommand( [[maybe_unused]] const hashed_string& command, [[maybe_unused]] Widget& source )
    {
        if ( requestTitleScene() == false )
            SW_LOG_ERROR_ID( "GameSceneFlow.ReturnTitleTitleScene", "Return to title: the title scene '%#' was not requested", _settings._titleScene.c_str() );
    }

    bool GameSceneFlow::requestLoadScene( const string& scenePath, const utf8* pWhich )
    {
        if ( scenePath.empty() )
            return false;
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
        {
            SW_LOG_WARNING_ID( "GameSceneFlow.SceneNotOpenedNo", "The %# scene '%#' is not opened - no SceneManager service", pWhich, scenePath.c_str() );
            return false;
        }
        // 로딩 화면 · 로드 이벤트는 받아들임 알림(`onSceneLoadRequested`)이 붙인다 — 시나리오 · 게임 코드가 연 씬도 같은 길이다.
        if ( pSceneManager->requestLoadFuture( scenePath, SceneLoadKind::Travel ).isValid() == false )
        {
            SW_LOG_ERROR_ID( "GameSceneFlow.SceneCouldNotRequested", "The %# scene '%#' could not be requested", pWhich, scenePath.c_str() );
            return false;
        }
        return true;
    }

    void GameSceneFlow::onSceneLoadRequested( const string& scenePath, SceneLoadKind kind, const TaskFuture<Scene*>& future )
    {
        if ( kind != SceneLoadKind::Travel )
            return; // 도구가 연 씬(편집기 · 진단)은 게임 흐름이 아니다
        // 로드가 끝날 때까지 세이브를 막는다 — 언리얼 게임도 로딩 화면 동안 세이브를 받지 않는다(맵이 반쯤 섰다).
        _listPendingSceneLoad.push_back( PendingSceneLoad{ scenePath, future, _saveGate.block( "scene.loading" ), SceneLoadedFunction{} } );
        if ( _pLoadingScreen != nullptr && _pLoadingScreen->hasLoadingScreenDocument() )
            (void)_pLoadingScreen->beginLoading();

        SceneLoadRequestedEvent event{};
        event._levelName = scenePath;
        GameEventUtil::send( event );
    }

    void GameSceneFlow::publishFinishedSceneLoads()
    {
        // 끝난 것만 순서를 지키며 먼저 뺀다 — 알림을 받는 쪽(세이브 적용 · 다시 열기)이 새 로드를 요청하면 목록이 자란다. 대기열에서 밀린 요청은 nullptr 로 끝나므로 실패로 알린다.
        vector<PendingSceneLoad> listFinished;
        size_t                   keptCount = 0;
        for ( size_t index = 0; index < _listPendingSceneLoad.size(); ++index )
        {
            PendingSceneLoad& pending = _listPendingSceneLoad[index];
            if ( pending._future.isReady() )
            {
                listFinished.push_back( std::move( pending ) );
                continue;
            }
            if ( keptCount != index )
                _listPendingSceneLoad[keptCount] = std::move( pending );
            ++keptCount;
        }
        _listPendingSceneLoad.resize( keptCount );

        for ( const PendingSceneLoad& finished : listFinished )
        {
            Scene* const            pScene = finished._future.get();
            SceneLoadCompletedEvent event{};
            event._levelName = finished._scenePath;
            event._bSuccess  = ( pScene != nullptr );
            GameEventUtil::send( event );
            if ( finished._onLoaded.isBound() )
                finished._onLoaded( pScene );
        }
    }

    bool GameSceneFlow::requestSceneThen( const string& scenePath, const SceneLoadedFunction& onLoaded )
    {
        if ( requestLoadScene( scenePath, "requested" ) == false )
            return false;
        // 받아들임 알림은 요청 안에서 동기로 돈다 — 방금 넣은 맨 뒤가 이 요청이다. 아니면 알림이 걸리지 않은 범위라 끝을 받을 수 없다(멈춘다).
        if ( _listPendingSceneLoad.empty() || _listPendingSceneLoad.back()._scenePath != scenePath )
        {
            SW_LOG_ERROR_ID( "GameSceneFlow.SceneLoadNotTracked", "The scene '%#' was requested but the scene flow did not receive the load - its completion cannot be delivered", scenePath.c_str() );
            return false;
        }
        _listPendingSceneLoad.back()._onLoaded = onLoaded;
        return true;
    }
} // namespace sw
