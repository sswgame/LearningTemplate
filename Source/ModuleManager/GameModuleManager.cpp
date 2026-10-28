#include "pch.h"

#include "ModuleManager/GameModuleManager.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/UISystem.h"

#include "ModuleManager/LiveReloadManager.h"
#include "ModuleManager/ModuleCompiler.h"
#include "ModuleManager/ModuleInstanceUtil.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다(유니티 빌드에서 이름이 충돌하지 않도록 TU 이름을 붙입니다). */
        struct GameModuleManagerInternal
        {
            static constexpr ModuleAPISymbols kGameSymbols{ "getGameModuleABIVersion", "getGameModuleABIStamp", "exportGameAPI", "Game" };

            /** @brief `StateBufferSink::allocate` — @p pContext 의 바이트 벡터를 @p size 로 맞춰 그 메모리를 돌려준다. */
            static void* allocateStateBuffer( void* pContext, uint32 size )
            {
                vector<uint8>* pBytes = static_cast<vector<uint8>*>( pContext );
                if ( pBytes == nullptr || size == 0 )
                    return nullptr;
                pBytes->resize( size );
                return pBytes->data();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameModuleManager" );

    GameModuleManager::GameModuleManager()
        : _moduleCompiler{ nullptr }
        , _gameAPI{}
        , _game{ nullptr }
        , _gameServices{ nullptr }
        , _pLiveReloadManager{ nullptr }
        , _pRHI{ nullptr }
        , _pWindow{ nullptr }
        , _pRenderThread{ nullptr }
        , _pSceneManager{ nullptr }
        , _pUISystem{ nullptr }
        , _listGameSavedState{}
        , _listGameScopeModule{}
        , _frameState{}
        , _listReloadSubscription{}
    {
    }

    GameModuleManager::~GameModuleManager()
    {
        shutdown();
    }

    bool GameModuleManager::loadModuleImages( LiveReloadManager* pLiveReloadManager, const ModuleCatalog& catalog, const ModuleResolution& resolution )
    {
        _pLiveReloadManager = pLiveReloadManager;
#if defined( SW_SHIPPING )
        // 게임, 키트, GameFramework 는 정적 링크라 로드할 이미지가 없다. 그 타입은 리플렉션 단계가 이미 모았고, 무엇을 링크할지는 CMake 가 같은 매니페스트로 정했다.
        (void)catalog;
        (void)resolution;
        return true;
#else
        if ( _pLiveReloadManager == nullptr )
            return true;

        SW_MEMORY_SCOPE( Game );
        vector<string> listGameModule;

        // 로드 순서는 매니페스트의 의존이 정한다(의존이 먼저, 동점은 이름 순). 공용 모듈(GameFramework)은 그것을 링크하는 키트와 SWGame 보다 먼저
        // 로드되므로 제 이름으로 등록된다(`LiveReloadManager::loadSharedModule` 의 이유).
        for ( const string& moduleName : resolution._listLoadOrder )
        {
            const ModuleManifest* pManifest = catalog.findManifest( moduleName );
            if ( pManifest == nullptr )
                continue;
            if ( pManifest->_kind == ModuleKind::GameFramework )
            {
                if ( _pLiveReloadManager->loadSharedModule( moduleName ) == false )
                {
                    SW_LOG_ERROR( "Shared module load failed (%#)", moduleName );
                    return false;
                }
                listGameModule.push_back( moduleName );
                continue;
            }
            if ( pManifest->_kind != ModuleKind::Kit )
                continue;

            vector<string> listDependency;
            for ( const ModuleDependency& dependency : pManifest->_listDependency )
            {
                listDependency.push_back( dependency._name );
            }
            if ( _pLiveReloadManager->registerModule( moduleName, listDependency ) == false )
            {
                SW_LOG_ERROR( "Kit module register failed (%#)", moduleName );
                return false;
            }
            holdReloadSubscription(
                _pLiveReloadManager->setOnBeforeReload( moduleName, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &GameModuleManager::onBeforeGameplayDLLReload, this ) ) );
            holdReloadSubscription(
                _pLiveReloadManager->setOnAfterReload( moduleName, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &GameModuleManager::onAfterGameplayDLLReload, this ) ) );
            listGameModule.push_back( moduleName );
        }

        // 게임 모듈은 이미지만 로드한다(타입 등록까지). 리로드 직후 콜백인 인스턴스 생성은 기동(`initializeGame`)이 등록하고 호출한다.
        holdReloadSubscription( _pLiveReloadManager->setOnBeforeReload(
            sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &GameModuleManager::onBeforeGameReload, this ) ) );
        holdReloadSubscription( _pLiveReloadManager->setOnReloadFault(
            sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnReloadFaultDelegate, &GameModuleManager::onGameReloadFault, this ) ) );
        holdReloadSubscription( _pLiveReloadManager->setOnValidateImage(
            sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnValidateImageDelegate, &GameModuleManager::isGameImageUsable, this ) ) );
        _listGameScopeModule = listGameModule; // SWGame 은 이름으로 따로 본다(onBeforeCommitBatch)
        if ( _pLiveReloadManager->registerModule( sw::config::kTargetGameModule, listGameModule ) == false )
        {
            SW_LOG_ERROR( "SWGame module register failed" );
            return false;
        }

        if ( _pLiveReloadManager->isGraphBroken() )
        {
            SW_LOG_ERROR( "LiveReload graph broken during module registration — aborting initialize" );
            return false;
        }

        holdReloadSubscription(
            _pLiveReloadManager->addOnBeforeCommitBatch( SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeCommitBatchDelegate, &GameModuleManager::onBeforeCommitBatch, this ) ) );
        return true;
#endif
    }

    bool GameModuleManager::initializeGame( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread )
    {
        _pLiveReloadManager = pLiveReloadManager;
        _pRHI               = pRHI;
        _pWindow            = pWindow;
        _pRenderThread      = pRenderThread;

        // 프레임 경로가 매 프레임 전역 조회를 하지 않도록 여기서 한 번 읽는다. 서비스 테이블이 비어 있으면 그 일을 건너뛴다.
        const bool bServicesBound = engine::areEngineServicesBound();
        _pSceneManager            = bServicesBound ? engine::findSubsystem<SceneManager>() : nullptr;
        _pUISystem                = bServicesBound ? engine::findSubsystem<UISystem>() : nullptr;

#if !defined( SW_SHIPPING )
        _moduleCompiler = make_unique<ModuleCompiler>( _pLiveReloadManager );
        if ( _pLiveReloadManager != nullptr )
        {
            // 매니저가 비우기를 부르는 것은 모듈을 내리기 직전(리로드)뿐이다 — 종료는 `shutdown` 이 이 콜백을 놓은 뒤 매니저를 내린다.
            holdReloadSubscription( _pLiveReloadManager->setDrainWorkers(
                SW_DELEGATE_LAMBDA( LiveReloadManager::DrainWorkersDelegate, [this]()
            { drainRenderWorkers( DrainReason::Reload ); } ) ) );
        }
#endif

#if defined( SW_SHIPPING )
        onAfterGameReload( nullptr );
#else
        if ( _pLiveReloadManager != nullptr )
        {
            BLOCK( "SWGame 인스턴스 생성" )
            {
                SW_MEMORY_SCOPE( Game );
                holdReloadSubscription( _pLiveReloadManager->setOnAfterReload(
                    sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &GameModuleManager::onAfterGameReload, this ) ) );
                if ( _pLiveReloadManager->runAfterReload( sw::config::kTargetGameModule ) == false )
                {
                    SW_LOG_ERROR( "SWGame instance could not be started" );
                    return false;
                }
            }
        }
#endif
        return true;
    }

    bool GameModuleManager::initializeDedicatedServer( LiveReloadManager* pLiveReloadManager )
    {
        return initializeGame( pLiveReloadManager, nullptr, nullptr, nullptr );
    }

    void GameModuleManager::shutdown()
    {
        if ( _moduleCompiler != nullptr )
        {
            _moduleCompiler->shutdown();
            _moduleCompiler.reset();
        }

        // 호스트 모듈과 게임을 한 번의 비우기로 언로드한다. 따로 언로드하면 drainRenderWorkers 가 두 번 돌아 종료 경로에서
        // 태스크 대기 제한 시간을 두 번까지 기다릴 수 있다.
        suspendModules( ModuleScope::Both, true, DrainReason::Shutdown );

        // 콜백은 이 객체의 메서드를 가리킨다. 실행 파일은 호스트를 먼저 지우고 나중에 리로드 매니저를 정리하므로, 이 객체가 건 것(모듈마다의 칸 ·
        // 배치 직전 · 비우기)을 지금 놓는다. 놓는 것은 이 객체의 구독뿐이다 — 다른 쪽(서버)이 건 배치 콜백은 그쪽 수명을 따른다.
        _listReloadSubscription.clear();
    }

    // ======================================================================
    // 프레임 단위 처리
    // ======================================================================

    void GameModuleManager::beginFrame()
    {
        _frameState                  = ModuleFrameState{};
        _frameState._bGameplayActive = queryGameplayActive() ? SW_TRUE : SW_FALSE;
        // 호스트 모듈(에디터)이 없으면 월드는 처음부터 플레이 중이다(에디터가 있으면 Play, Stop 이 정한다). 켜지 않으면 onBeginPlay 가 불리지 않는다.
        // 이미 켜져 있으면 아무 일도 없다.
        const bool bStartWorldPlay = _pSceneManager != nullptr && controlsWorldPlay() == false && _pSceneManager->isWorldPlaying() == false;
        if ( bStartWorldPlay )
            _pSceneManager->setWorldPlaying( true );
        // 게임 update 가 돌지 않는 동안(에디터 일시정지, Simulate)은 게임이 연 화면(로딩, HUD, 메뉴)을 그리지 않는다. 그 화면을 닫을 게임 코드가
        // 돌지 않으므로, 그리면 첫 Play 전까지 로딩 화면이 게임 뷰를 덮는다. 오프스크린 미리보기(UI Preview)는 그대로 그린다.
        if ( _pUISystem != nullptr )
            _pUISystem->setOnScreenSuppressed( _frameState._bGameplayActive == SW_FALSE );
    }

    void GameModuleManager::updateGame( float32 deltaTime )
    {
        SW_MEMORY_SCOPE( Game );
        const bool bCanUpdate = _game != nullptr && _gameAPI.update != nullptr && _frameState._bGameplayActive == SW_TRUE;
        if ( bCanUpdate )
            _gameAPI.update( _game, deltaTime );
    }

    void GameModuleManager::fixedUpdateGame( float32 fixedDeltaTime )
    {
        SW_MEMORY_SCOPE( Game );
        const bool bCanFixedUpdate = _game != nullptr && _gameAPI.fixedUpdate != nullptr && _frameState._bGameplayActive == SW_TRUE;
        if ( bCanFixedUpdate )
            _gameAPI.fixedUpdate( _game, fixedDeltaTime );
    }

    // ======================================================================
    // LiveReload 콜백. 게임.
    // ======================================================================

    void GameModuleManager::onBeforeGameReload()
    {
        suspendModules( ModuleScope::Game, true, DrainReason::Reload );
    }

    void GameModuleManager::onAfterGameReload( void* pLibraryModule )
    {
        SW_MEMORY_SCOPE( Game );
#if defined( SW_SHIPPING )
        (void)pLibraryModule;
        if ( _gameAPI.create == nullptr && bindGameAPI( nullptr ) == false )
            return;
#else
        void* pModuleHandle = pLibraryModule;
        if ( pModuleHandle == nullptr && _pLiveReloadManager != nullptr )
            pModuleHandle = _pLiveReloadManager->getModuleHandle( sw::config::kTargetGameModule );

        if ( bindGameAPI( pModuleHandle ) == false )
        {
            markReloadGraphBroken( "GameAPI bind failed after reload" );
            return;
        }
#endif

        if ( createGameInstance() == false )
        {
            _gameAPI = {};
            markReloadGraphBroken( "Game create/initialize failed after reload" );
            return;
        }

        restoreGameState();
    }

    // ======================================================================
    // LiveReload 콜백. GameFramework 와 키트 DLL 연쇄 교체.
    // ======================================================================

    void GameModuleManager::onBeforeGameplayDLLReload()
    {
        onBeforeGameReload();
    }

    void GameModuleManager::onAfterGameplayDLLReload( void* /*hLibraryModule*/ )
    {
        if ( engine::areEngineServicesBound() == false )
            return;
        for ( const unique_ptr<Scene>& scene : engine::getSubsystem<SceneManager>().getLoadedScenes() )
        {
            if ( scene != nullptr )
                scene->getObjectManager()->markTickStagesDirty(); // 기본값은 다시 저장하지 않는다(ModuleTypeRegistry 의 같은 위치 참고).
        }
    }

    bool GameModuleManager::onBeforeCommitBatch( const vector<string>& listModuleName )
    {
#if !defined( SW_SHIPPING )
        for ( const string& name : listModuleName )
        {
            const bool bGameScope = name == sw::config::kTargetGameModule || VectorUtil::contains( _listGameScopeModule, name );
            if ( bGameScope )
            {
                // 게임 상태는 새 이미지가 넘겨받을 유일한 것이다. 저장하지 못했으면 언로드하지 않는다. 옛 게임이 그대로 돈다.
                if ( suspendModulesInternal( ModuleScope::Game, true, true, DrainReason::Reload ) == false )
                {
                    SW_LOG_ERROR( "The game could not serialize its state for the reload — keeping the old game modules running" );
                    return false;
                }
                return true;
            }
        }
#else
        (void)listModuleName;
#endif
        return true;
    }

    bool GameModuleManager::isGameImageUsable( void* pLibraryModule ) const
    {
        GameAPI api{};
        if ( ModuleInstanceUtil::exportAPIFromImage<GameAPI, PFN_ExportGameAPI>( pLibraryModule, GameModuleManagerInternal::kGameSymbols, api ) == false )
            return false;
        return api.create != nullptr && api.destroy != nullptr;
    }

    // ======================================================================
    // 보조 함수. 비우기와 그래프 깨짐 표시.
    // ======================================================================

    void GameModuleManager::waitRenderThreadIdle()
    {
#if defined( SW_WITH_CLIENT_CODE )
        if ( _pRenderThread != nullptr )
            _pRenderThread->waitIdle( RHIWaitIdleReason::HotReload ); // 새 에디터 인스턴스(모듈 리로드 · RHI 교체 직후)의 첫 UI 프레임
#endif
    }

    void GameModuleManager::drainRenderWorkers( DrainReason reason )
    {
        // 종료의 비우기는 호스트 모듈(에디터)을 내리기 앞이라 에디터의 배경 일이 아직 돈다 — 기다리기 전에 그만 하라고 알린다(`Task fencing timeout` 의 원인이었다).
        onBeforeDrainWorkers( reason );
        if ( reason == DrainReason::Shutdown && _pSceneManager != nullptr )
            _pSceneManager->requestAsyncLoadStop(); // 도는 비동기 씬 로드가 인스턴스화까지 가지 않게 한다
        // 비우는 까닭(`DrainReason`)이 곧 장치 대기의 이유다.
        const RHIWaitIdleReason waitReason = reason == DrainReason::BackendSwap ? RHIWaitIdleReason::BackendSwap
                                                                                : ( reason == DrainReason::Shutdown ? RHIWaitIdleReason::Shutdown : RHIWaitIdleReason::HotReload );
#if defined( SW_WITH_CLIENT_CODE )
        if ( _pRenderThread != nullptr )
            _pRenderThread->waitIdle( waitReason );
#else
        SW_ASSERT( _pRenderThread == nullptr ); // Server 타깃은 렌더러를 짓지 않는다 — 렌더 스레드가 있을 수 없다
#endif
        // **디바이스가 없는 RHI 가 있다.** 백엔드 교체가 실패하면 RHI 객체는 남고 디바이스만 사라지는데, `getDevice()` 는 널
        // 참조를 반환하므로 그 상태로 물으면 죽는다. 종료 경로가 반드시 이곳을 지난다(`Engine::shutdown` 도 같은 이유로
        // `hasDevice()` 를 먼저 확인한다).
        if ( _pRHI != nullptr && _pRHI->hasDevice() )
            _pRHI->getDevice().waitIdle( waitReason );

        onRenderWorkersDrained();

        // 모듈을 언로드하기 전에 비동기 태스크가 모두 끝나기를 기다린다. 제한 시간은 LiveReloadManager 의 폴백과 공유한다.
        // **엔진 전체 대기가 맞다(모듈이 낸 태스크만 기다리지 않는다).** 입구를 한 곳으로 모아 `TaskHandleTracker` 에 올리는 구조는 만들 수 없다:
        // 모듈 코드는 태스크 본문(`emplaceTask` 에 넘기는 델리게이트)으로만 들어오지 않는다. 엔진이 낸 태스크(씬 틱 병렬 일 · `runParallel`)가 가상 함수로
        // 컴포넌트·델리게이트를 불러 모듈 이미지에 들어가고, 델리게이트는 어디서나 만들어져 어디로나 넘어간다. 낸 쪽 · 델리게이트 주소로 가르면 옛 이미지에
        // 들어가는 태스크를 놓쳐 언로드 뒤 해제된 코드를 실행한다. 그래서 영구 태스크는 두지 않는 것이 계약이다(엔진 소스에는 없다). 있으면 제한 시간 뒤
        // 아래 오류가 도는 태스크 이름과 함께 드러낸다.
        if ( engine::areEngineServicesBound() )
        {
            TaskManager& taskManager = engine::getSubsystem<TaskManager>();
            if ( taskManager.waitAll( LiveReloadManager::kModuleDrainTimeoutMs ) == false )
            {
                // 종료 앞이면 리로드가 없으니 그래프를 깨뜨릴 까닭이 없다 — 남은 태스크 수와 이름만 남겨 어떤 배경 일이 오래 도는지 좁힌다(스택은 `Scripts stacks <pid>`).
                if ( reason == DrainReason::Shutdown )
                {
                    SW_LOG_ERROR( "Task fencing timeout (%# ms) at shutdown — %# task(s) still running (first unfinished: '%#'); the task manager joins them next.", LiveReloadManager::kModuleDrainTimeoutMs,
                                  taskManager.getActiveTaskCount(), taskManager.describeUnfinishedTask() );
                }
                else
                {
                    SW_LOG_ERROR( "Task fencing timeout (%# ms) before module reload (%# task(s) still running, first unfinished: '%#') — marking the LiveReload graph broken.",
                                  LiveReloadManager::kModuleDrainTimeoutMs, taskManager.getActiveTaskCount(), taskManager.describeUnfinishedTask() );
                    markReloadGraphBroken( "task fencing timeout before unload" );
                }
            }
        }
    }

    void GameModuleManager::onGameReloadFault( uint32 faultCode )
    {
        // 스냅샷은 **버리지 않는다.** 게임 컴포넌트는 리로드 앞에서 이미 모든 씬에서 제거했다(`destroyGameInstance`). 여기서 비우면 그
        // 상태로 저장할 때 컴포넌트가 빠진 씬이 저장된다. 고쳐서 다시 빌드하면 다음 리로드가 이 스냅샷을 복원한다(게임이 없으니 새로 저장하지 않는다).
        // 그때까지 씬 저장은 막혀 있다(`destroyGameInstance` 가 막았다).
        SW_LOG_ERROR( "Game module faulted after the reload (code 0x%#) — the game is off; fix it and rebuild, the next reload restores the %# byte snapshot (scene saving is blocked until then)",
                      Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ), static_cast<uint64>( _listGameSavedState.size() ) );
        _game    = nullptr;
        _gameAPI = {};
    }

    void GameModuleManager::markReloadGraphBroken( const utf8* pReason )
    {
#if !defined( SW_SHIPPING )
        if ( _pLiveReloadManager != nullptr )
            _pLiveReloadManager->markGraphBroken( pReason != nullptr ? pReason : "unspecified" );
#else
        (void)pReason;
#endif
    }

    // ======================================================================
    // API 바인딩.
    // ======================================================================

    bool GameModuleManager::bindGameAPI( void* pLibraryModule )
    {
        _gameAPI = {};
#if defined( SW_SHIPPING )
        (void)pLibraryModule;
        if ( exportGameAPI( &_gameAPI ) == false )
        {
            SW_LOG_ERROR( "Failed to bind GameAPI (shipping)" );
            return false;
        }
#else
        // Shipping 은 게임을 정적으로 링크하므로(위 분기) 테이블이 어긋날 수 없다. 대조는 동적 경로에서만 한다.
        if ( ModuleInstanceUtil::exportAPIFromImage<GameAPI, PFN_ExportGameAPI>( pLibraryModule, GameModuleManagerInternal::kGameSymbols, _gameAPI ) == false )
            return false;
#endif

        rebindGameService();

        engine::registerModuleTypes( sw::config::kTargetGameModule );
        return _gameAPI.create != nullptr && _gameAPI.destroy != nullptr;
    }

    void GameModuleManager::suspendModules( ModuleScope scope, bool bReleaseAPITable, DrainReason reason )
    {
        // 종료, RHI 교체, 에디터 리로드에는 게임 상태를 넘겨받을 새 게임 이미지가 없다. 저장에 실패해도 언로드를 막지 않는다.
        (void)suspendModulesInternal( scope, bReleaseAPITable, false, reason );
    }

    bool GameModuleManager::suspendModulesInternal( ModuleScope scope, bool bReleaseAPITable, bool bKeepGameOnCaptureFailure, DrainReason reason )
    {
        drainRenderWorkers( reason );

        const bool bSuspendHostModule = scope != ModuleScope::Game;
        const bool bSuspendGame       = scope != ModuleScope::Editor;

        // 무엇을 언로드하든 호스트 모듈(에디터 시뮬레이션)부터 멈춘다. 이유는 `EditorModuleManager::onBeforeSuspendModules` 에 있다.
        onBeforeSuspendModules();

        // 무엇이든 언로드하기 전에 저장한다. 저장하지 못했으면(리로드 직전일 때만) 여기서 멈춘다. 아직 아무것도 언로드하지 않았다.
        const bool bAbortOnCaptureFailure = bSuspendGame && captureGameState() == false && bKeepGameOnCaptureFailure;
        if ( bAbortOnCaptureFailure )
            return false;
        if ( bSuspendHostModule )
        {
            suspendHostModule( bReleaseAPITable );
            // 일시정지 함수가 없던 호스트 모듈이라도 월드가 플레이 중으로 남지 않게 한다. 호스트 모듈 없이 다시 돌면 beginFrame 이 다시 켠다.
            if ( engine::areEngineServicesBound() && engine::getSubsystem<SceneManager>().isWorldPlaying() )
                engine::getSubsystem<SceneManager>().setWorldPlaying( false );
        }
        if ( bSuspendGame )
            destroyGameInstance( bReleaseAPITable );
        return true;
    }

#if !defined( SW_SHIPPING )
    void GameModuleManager::attachGameInstance( const GameAPI& gameAPI, GameHandle game )
    {
        _gameAPI = gameAPI;
        _game    = game;
    }
#endif

    void* GameModuleManager::getLoadedModuleHandle( [[maybe_unused]] string_view moduleName ) const
    {
#if defined( SW_SHIPPING )
        // 정적 링크라 "로드된 모듈"이라는 것이 없다. 부르는 쪽이 nullptr 을 처리한다.
        return nullptr;
#else
        if ( _pLiveReloadManager == nullptr )
            return nullptr;
        return _pLiveReloadManager->getModuleHandle( moduleName );
#endif
    }

    bool GameModuleManager::captureGameState()
    {
        if ( _game == nullptr || _gameAPI.serializeState == nullptr )
            return true;

        // 모듈이 직렬화를 한 번 돌리고 크기가 정해진 뒤 싱크로 버퍼를 받는다. 실패하면 이전 저장본을 그대로 둔다.
        vector<uint8>         bytes;
        const StateBufferSink sink{ &bytes, &GameModuleManagerInternal::allocateStateBuffer };
        if ( _gameAPI.serializeState( _game, &sink ) == false )
            return false;
        if ( bytes.empty() )
            return true;
        _listGameSavedState = std::move( bytes );
        return true;
    }

    void GameModuleManager::restoreGameState()
    {
        if ( _game == nullptr )
            return;
        // 복원할 것이 없으면(상태 직렬화가 없는 게임, 스냅샷이 비었다) 막을 이유도 없다.
        if ( _gameAPI.deserializeState == nullptr || _listGameSavedState.empty() )
        {
            if ( engine::areEngineServicesBound() )
                engine::getSubsystem<SceneManager>().setSaveBlockReason( {} );
            return;
        }

        if ( _gameAPI.deserializeState( _game, _listGameSavedState.data(), static_cast<uint32>( _listGameSavedState.size() ) ) )
        {
            SW_LOG_INFO( "Scene object state restored from %zu bytes.", _listGameSavedState.size() );
            _listGameSavedState.clear();
            if ( engine::areEngineServicesBound() )
                engine::getSubsystem<SceneManager>().setSaveBlockReason( {} );
        }
        else
        {
            SW_LOG_ERROR( "Failed to restore game state — keeping saved state for subsequent reload." );
        }
    }

    bool GameModuleManager::recreateGameInstance( void* pGameModule )
    {
        if ( _gameAPI.create == nullptr || _gameAPI.initialize == nullptr )
        {
#if defined( SW_SHIPPING )
            (void)pGameModule;
            onAfterGameReload( nullptr );
#else
            onAfterGameReload( pGameModule );
#endif
            return _game != nullptr;
        }

        rebindGameService();
        if ( createGameInstance() == false )
            return false;

        restoreGameState();
        return true;
    }

    // ======================================================================
    // 인스턴스 생성과 파괴. 리로드 경로와 RHI 핫스왑 경로가 같은 코드를 쓴다.
    // ======================================================================

    void GameModuleManager::fillModuleServiceScope( SubsystemScope& outScope, bool bGameModule ) const
    {
        engine::bindModuleServices( outScope, bGameModule );
        // 호스트 전용 서비스 — 에디터 모듈만 찾는다(게임 모듈의 범위에는 걸지 않는다).
        if ( bGameModule == false )
        {
            (void)outScope.bind( getModuleCompiler() ); // 엔진 행과 타입이 달라 겹치지 않는다(없으면 걸지 않는다)
            // 설정 테이블은 서비스 목록 밖이다(게임 모듈이 볼 이유가 없다) — 에디터의 설정 감시 · 프로젝트 설정 창이 범위에서 찾게 건다.
            (void)outScope.bind( engine::findSubsystem<ConfigManager>() );
        }
    }

    void GameModuleManager::rebindGameService()
    {
        if ( _gameAPI.bindService == nullptr )
            return;

        // 새 범위를 채워 넘긴 뒤에 앞 범위를 놓는다 — 모듈이 앞 범위를 들고 있는 동안 사라지지 않게.
        unique_ptr<SubsystemScope> gameServices = make_unique<SubsystemScope>();
        fillModuleServiceScope( *gameServices, true );
        _gameAPI.bindService( gameServices.get() );
        _gameServices = std::move( gameServices );
    }

    void GameModuleManager::destroyGameInstance( bool bReleaseAPITable )
    {
        ModuleInstanceUtil::destroyInstance( _gameAPI, _game, bReleaseAPITable, sw::config::kTargetGameModule );
#if !defined( SW_SHIPPING )
        // 게임 컴포넌트를 모든 씬에서 제거했다. 복원할 때까지(`restoreGameState`) 씬을 저장하면 그것들이 빠진 채 저장된다. 리로드가 실패하거나
        // 중단되면 복원하는 쪽이 오지 않으므로 여기서 막는다.
        if ( bReleaseAPITable && engine::areEngineServicesBound() )
            engine::getSubsystem<SceneManager>().setSaveBlockReason( "the game module's components were removed for a reload and have not been restored yet" );
#endif
    }

    bool GameModuleManager::createGameInstance()
    {
        SW_MEMORY_SCOPE( Game );
        // RHI 를 받은 호스트(App)는 디바이스가 있어야 만든다. 전용 서버는 RHI 가 없고 게임을 디바이스 없이 만든다.
        return ModuleInstanceUtil::createInstance( _gameAPI, _game, _pWindow, _pRHI, _pRHI != nullptr, "Game" );
    }
} // namespace sw
