#include "pch.h"

#include "ModuleHost/ModuleHost.h"

#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/UI/UISystem.h"

#include "ModuleHost/LiveReloadManager.h"
#include "ModuleHost/ModuleCompiler.h"
#include "ModuleHost/ModuleInstanceUtil.h"

#include "RuntimeAPI/Service/ModuleService.h"
#include "RuntimeAPI/Service/ServiceListColumns.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다(유니티 빌드에서 이름이 충돌하지 않도록 TU 이름을 붙입니다). */
        struct ModuleHostInternal
        {
            static constexpr ModuleAPISymbols kGameSymbols{ "getGameModuleAbiVersion", "getGameModuleAbiStamp", "exportGameAPI", "Game" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ModuleHost" );

    ModuleHost::ModuleHost()
        : _moduleCompiler{ nullptr }
        , _gameAPI{}
        , _game{ nullptr }
        , _pLiveReloadManager{ nullptr }
        , _pRHI{ nullptr }
        , _pWindow{ nullptr }
        , _pRenderThread{ nullptr }
        , _listGameSavedState{}
        , _frameState{}
    {
    }

    ModuleHost::~ModuleHost()
    {
        shutdown();
    }

    bool ModuleHost::loadModuleImages( LiveReloadManager* pLiveReloadManager, const ModuleCatalog& catalog, const ModuleResolution& resolution )
    {
        _pLiveReloadManager = pLiveReloadManager;
#if defined( SW_SHIPPING )
        // 게임 · 키트 · GameFramework 는 정적 링크라 올릴 이미지가 없다 — 그 타입은 리플렉션 단계가 이미 모았고, 무엇을 링크할지는 CMake 가 같은 매니페스트로 정했다.
        (void)catalog;
        (void)resolution;
        return true;
#else
        if ( _pLiveReloadManager == nullptr )
            return true;

        SW_MEMORY_SCOPE( Game );
        vector<string> listGameModule;

        // 적재 순서는 매니페스트의 의존이 정한다(의존이 먼저, 동점은 이름 순). 공용 모듈(GameFramework)은 그것을 링크하는 키트 · SWGame 보다 먼저
        // 오므로 제 이름으로 등록된다(`LiveReloadManager::loadSharedModule` 의 이유).
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
            _pLiveReloadManager->setOnBeforeReload( moduleName, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeGameplayDllReload, this ) );
            _pLiveReloadManager->setOnAfterReload( moduleName, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterGameplayDllReload, this ) );
            listGameModule.push_back( moduleName );
        }

        // 게임 모듈은 이미지만 올린다(타입 등록까지). 리로드 직후 콜백 — 인스턴스 생성 — 은 기동(`initializeGame`)이 걸고 부른다.
        _pLiveReloadManager->setOnBeforeReload( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeGameReload, this ) );
        _pLiveReloadManager->setOnReloadFault( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnReloadFaultDelegate, &ModuleHost::onGameReloadFault, this ) );
        _pLiveReloadManager->setOnValidateImage( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnValidateImageDelegate, &ModuleHost::isGameImageUsable, this ) );
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

        _pLiveReloadManager->setOnBeforeCommitBatch(
            SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeCommitBatchDelegate, &ModuleHost::onBeforeCommitBatch, this ) );
        return true;
#endif
    }

    bool ModuleHost::initializeGame( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread )
    {
        _pLiveReloadManager = pLiveReloadManager;
        _pRHI               = pRHI;
        _pWindow            = pWindow;
        _pRenderThread      = pRenderThread;

#if !defined( SW_SHIPPING )
        _moduleCompiler = make_unique<ModuleCompiler>( _pLiveReloadManager );
        if ( _pLiveReloadManager != nullptr )
        {
            _pLiveReloadManager->setDrainWorkers(
                SW_DELEGATE_METHOD( LiveReloadManager::DrainWorkersDelegate, &ModuleHost::drainRenderWorkers, this ) );
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
                _pLiveReloadManager->setOnAfterReload( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterGameReload, this ) );
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

    bool ModuleHost::initializeDedicatedServer( LiveReloadManager* pLiveReloadManager )
    {
        return initializeGame( pLiveReloadManager, nullptr, nullptr, nullptr );
    }

    void ModuleHost::shutdown()
    {
        if ( _moduleCompiler != nullptr )
        {
            _moduleCompiler->shutdown();
            _moduleCompiler.reset();
        }

        // 호스트 모듈 · 게임을 한 번의 비우기로 내린다 — 따로 내리면 drainRenderWorkers 가 두 번 돌아 종료 경로에서
        // 태스크 대기 제한 시간을 두 번까지 기다릴 수 있다.
        suspendModules( ModuleScope::Both, true );

#if !defined( SW_SHIPPING )
        // 콜백은 이 객체의 메서드를 가리킨다. 이 객체가 사라지기 전에 떼어 낸다.
        //
        // **모듈마다 건 것까지 뗀다.** `setOnBeforeReload`/`setOnAfterReload` 로 모듈마다 건 델리게이트도 이 객체의 메서드를
        // 가리킨다. 실행 파일은 호스트를 먼저 지우고 나중에 LiveReloadManager 를 내리므로, 그 사이에 리로드가 한 번 돌면 이미
        // 사라진 객체를 부르게 된다. 이름을 여기에 다시 적지 않으려고 등록부 쪽에 창구를 두었다(키트 모듈은 설정에서 오므로 이
        // 자리에서는 이름을 알 수도 없다).
        if ( _pLiveReloadManager != nullptr )
        {
            _pLiveReloadManager->setDrainWorkers( {} );
            _pLiveReloadManager->setOnBeforeCommitBatch( {} );
            _pLiveReloadManager->clearReloadCallbacks();
        }
#endif
    }

    // ======================================================================
    // 프레임 단위 처리
    // ======================================================================

    void ModuleHost::beginFrame()
    {
        _frameState                  = ModuleFrameState{};
        _frameState._bGameplayActive = queryGameplayActive() ? SW_TRUE : SW_FALSE;
        // 호스트 모듈(에디터)이 없으면 월드는 처음부터 플레이 중이다(에디터가 있으면 Play · Stop 이 정한다) — 켜지 않으면 onBeginPlay 가 불리지 않는다.
        // 이미 켜져 있으면 아무 일도 없다.
        if ( controlsWorldPlay() == false && engine::getSceneManager().isWorldPlaying() == false )
            engine::getSceneManager().setWorldPlaying( true );
        // 게임 update 가 돌지 않는 동안(에디터 멈춤 · Simulate)은 게임이 연 화면(로딩 · HUD · 메뉴)을 그리지 않는다 — 그 화면을 닫을 게임 코드가
        // 돌지 않으므로, 그리면 첫 Play 전까지 로딩 화면이 게임 뷰를 덮는다. 오프스크린 미리보기(UI Preview)는 그대로다.
        if ( engine::areEngineServicesBound() )
            engine::getUISystem().setOnScreenSuppressed( _frameState._bGameplayActive == SW_FALSE );
    }

    void ModuleHost::updateGame( float32 deltaTime )
    {
        SW_MEMORY_SCOPE( Game );
        if ( _game != nullptr && _gameAPI.update != nullptr && _frameState._bGameplayActive == SW_TRUE )
            _gameAPI.update( _game, deltaTime );
    }

    void ModuleHost::fixedUpdateGame( float32 fixedDeltaTime )
    {
        SW_MEMORY_SCOPE( Game );
        if ( _game != nullptr && _gameAPI.fixedUpdate != nullptr && _frameState._bGameplayActive == SW_TRUE )
            _gameAPI.fixedUpdate( _game, fixedDeltaTime );
    }

    // ======================================================================
    // LiveReload 콜백 — 게임
    // ======================================================================

    void ModuleHost::onBeforeGameReload()
    {
        suspendModules( ModuleScope::Game, true );
    }

    void ModuleHost::onAfterGameReload( void* pLibraryModule )
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
    // LiveReload 콜백 — GameFramework · 키트 DLL 연쇄 교체
    // ======================================================================

    void ModuleHost::onBeforeGameplayDllReload()
    {
        onBeforeGameReload();
    }

    void ModuleHost::onAfterGameplayDllReload( void* /*hLibraryModule*/ )
    {
        if ( engine::areEngineServicesBound() == false )
            return;
        for ( const unique_ptr<Scene>& scene : engine::getSceneManager().getLoadedScenes() )
        {
            if ( scene != nullptr )
                scene->getObjectManager()->markTickStagesDirty(); // 기본값은 다시 찍지 않는다(ModuleTypeRegistry 의 같은 자리 참고)
        }
    }

    bool ModuleHost::onBeforeCommitBatch( const vector<string>& listModuleName )
    {
#if !defined( SW_SHIPPING )
        for ( const string& name : listModuleName )
        {
            if ( name != sw::config::kTargetEditorModule )
            {
                // 게임 상태는 새 이미지가 넘겨받을 유일한 것이다. 찍지 못했으면 내리지 않는다 — 옛 게임이 그대로 돈다.
                if ( suspendModulesInternal( ModuleScope::Game, true, true ) == false )
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

    bool ModuleHost::isGameImageUsable( void* pLibraryModule ) const
    {
        GameAPI api{};
        if ( ModuleInstanceUtil::exportAPIFromImage<GameAPI, PFN_ExportGameAPI>( pLibraryModule, ModuleHostInternal::kGameSymbols, api ) == false )
            return false;
        return api.create != nullptr && api.destroy != nullptr;
    }

    // ======================================================================
    // 보조 — 비우기 · 그래프 깨짐 표시
    // ======================================================================

    void ModuleHost::drainRenderWorkers()
    {
        if ( _pRenderThread != nullptr )
            _pRenderThread->waitIdle();
        // **디바이스가 없는 RHI 가 있다.** 백엔드 교체가 실패하면 RHI 객체는 남고 디바이스만 사라지는데, `getDevice()` 는 널
        // 참조를 반환하므로 그 상태로 물으면 죽는다. 종료 경로가 반드시 이곳을 지난다(`EngineLoop::shutdown` 도 같은 이유로
        // `hasDevice()` 를 먼저 확인한다).
        if ( _pRHI != nullptr && _pRHI->hasDevice() )
            _pRHI->getDevice().waitIdle();

        onRenderWorkersDrained();

        // 모듈을 내리기 전에 비동기 태스크가 모두 끝나기를 기다린다. 제한 시간은 LiveReloadManager 의 폴백과 공유한다.
        if ( engine::areEngineServicesBound() )
        {
            if ( engine::getTaskManager().waitAll( LiveReloadManager::kModuleDrainTimeoutMs ) == false )
            {
                SW_LOG_ERROR( "Task fencing timeout (%# ms) before module reload — marking the LiveReload graph broken.", LiveReloadManager::kModuleDrainTimeoutMs );
                markReloadGraphBroken( "task fencing timeout before unload" );
            }
        }
    }

    void ModuleHost::onGameReloadFault( uint32 faultCode )
    {
        // 스냅숏은 **버리지 않는다.** 게임 컴포넌트는 리로드 앞에서 이미 모든 씬에서 걷어 냈다(`destroyGameInstance`) — 여기서 비우면 그
        // 상태로 저장할 때 컴포넌트가 빠진 씬이 저장된다. 고쳐서 다시 빌드하면 다음 리로드가 이 스냅숏을 되돌린다(게임이 없으니 새로 찍지 않는다).
        // 그때까지 씬 저장은 막혀 있다(`destroyGameInstance` 가 막았다).
        SW_LOG_ERROR( "Game module faulted after the reload (code 0x%#) — the game is off; fix it and rebuild, the next reload restores the %# byte snapshot (scene saving is blocked until then)",
                      Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ), static_cast<uint64>( _listGameSavedState.size() ) );
        _game    = nullptr;
        _gameAPI = {};
    }

    void ModuleHost::markReloadGraphBroken( const utf8* pReason )
    {
#if !defined( SW_SHIPPING )
        if ( _pLiveReloadManager != nullptr )
            _pLiveReloadManager->markGraphBroken( pReason != nullptr ? pReason : "unspecified" );
#else
        (void)pReason;
#endif
    }

    // ======================================================================
    // API 바인딩
    // ======================================================================

    bool ModuleHost::bindGameAPI( void* pLibraryModule )
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
        if ( ModuleInstanceUtil::exportAPIFromImage<GameAPI, PFN_ExportGameAPI>( pLibraryModule, ModuleHostInternal::kGameSymbols, _gameAPI ) == false )
            return false;
#endif

        rebindGameService();

        engine::registerModuleTypes( sw::config::kTargetGameModule );
        return _gameAPI.create != nullptr && _gameAPI.destroy != nullptr;
    }

    void ModuleHost::suspendModules( ModuleScope scope, bool bReleaseAPITable )
    {
        // 종료 · RHI 교체 · 에디터 리로드에는 게임 상태를 넘겨받을 새 게임 이미지가 없다 — 찍기 실패가 내리기를 막지 않는다.
        (void)suspendModulesInternal( scope, bReleaseAPITable, false );
    }

    bool ModuleHost::suspendModulesInternal( ModuleScope scope, bool bReleaseAPITable, bool bKeepGameOnCaptureFailure )
    {
        drainRenderWorkers();

        const bool bSuspendHostModule = scope != ModuleScope::Game;
        const bool bSuspendGame       = scope != ModuleScope::Editor;

        // 무엇을 내리든 호스트 모듈(에디터 시뮬레이션)부터 멈춘다 — 이유는 `EditorModuleHost::onBeforeSuspendModules`.
        onBeforeSuspendModules();

        // 무엇이든 내리기 전에 찍는다. 찍지 못했으면(리로드 직전일 때만) 여기서 멈춘다 — 아직 아무것도 내리지 않았다.
        if ( bSuspendGame && captureGameState() == false && bKeepGameOnCaptureFailure )
            return false;
        if ( bSuspendHostModule )
        {
            suspendHostModule( bReleaseAPITable );
            // 멈춤 창구가 없던 호스트 모듈이라도 월드가 플레이 중으로 남지 않게 한다. 호스트 모듈 없이 다시 돌면 beginFrame 이 다시 켠다.
            if ( engine::areEngineServicesBound() && engine::getSceneManager().isWorldPlaying() )
                engine::getSceneManager().setWorldPlaying( false );
        }
        if ( bSuspendGame )
            destroyGameInstance( bReleaseAPITable );
        return true;
    }

#if !defined( SW_SHIPPING )
    void ModuleHost::attachGameInstance( const GameAPI& gameAPI, GameHandle game )
    {
        _gameAPI = gameAPI;
        _game    = game;
    }
#endif

    void* ModuleHost::getLoadedModuleHandle( [[maybe_unused]] string_view moduleName ) const
    {
#if defined( SW_SHIPPING )
        // 정적 링크라 "로드된 모듈" 이라는 것이 없다. 부르는 쪽이 nullptr 을 처리한다.
        return nullptr;
#else
        if ( _pLiveReloadManager == nullptr )
            return nullptr;
        return _pLiveReloadManager->getModuleHandle( moduleName );
#endif
    }

    bool ModuleHost::captureGameState()
    {
        if ( _game == nullptr || _gameAPI.serializeState == nullptr )
            return true;

        uint32 size{ 0 };
        if ( _gameAPI.serializeState( _game, nullptr, &size ) == false )
            return false;
        if ( size == 0 )
            return true;

        vector<uint8> tempState( size );
        if ( _gameAPI.serializeState( _game, tempState.data(), &size ) == false )
            return false;
        _listGameSavedState = std::move( tempState );
        return true;
    }

    void ModuleHost::restoreGameState()
    {
        if ( _game == nullptr )
            return;
        // 되돌릴 것이 없으면(상태 직렬화가 없는 게임 · 스냅숏이 비었다) 막을 이유도 없다.
        if ( _gameAPI.deserializeState == nullptr || _listGameSavedState.empty() )
        {
            if ( engine::areEngineServicesBound() )
                engine::getSceneManager().setSaveBlockReason( {} );
            return;
        }

        if ( _gameAPI.deserializeState( _game, _listGameSavedState.data(), static_cast<uint32>( _listGameSavedState.size() ) ) )
        {
            SW_LOG_INFO( "Scene object state restored from %zu bytes.", _listGameSavedState.size() );
            _listGameSavedState.clear();
            if ( engine::areEngineServicesBound() )
                engine::getSceneManager().setSaveBlockReason( {} );
        }
        else
        {
            SW_LOG_ERROR( "Failed to restore game state — keeping saved state for subsequent reload." );
        }
    }

    bool ModuleHost::recreateGameInstance( void* pGameModule )
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
    // 인스턴스 생성 · 파괴 — 리로드 경로와 RHI 핫스왑 경로가 같은 코드를 쓴다
    // ======================================================================

    void ModuleHost::fillModuleService( ModuleService& outService, bool bGameModule ) const
    {
        engine::fillModuleServices( outService, bGameModule );

#define SW_HOST_SERVICE( member, Tag, Type, getter, visibility )                                        \
    if ( bGameModule == false || ( SW_SERVICE_IS_GAME_VISIBLE( visibility ) == 1 ) )                    \
    {                                                                                                   \
        outService.arrServices[internal::toRawServiceID( internal::ModuleServiceID::Type )] = getter(); \
    }

#include "RuntimeAPI/Service/HostServiceList.xxx"
#undef SW_HOST_SERVICE
    }

    void ModuleHost::rebindGameService()
    {
        if ( _gameAPI.bindService == nullptr )
            return;

        ModuleService gameService{};
        fillModuleService( gameService, true );
        _gameAPI.bindService( &gameService );
    }

    void ModuleHost::destroyGameInstance( bool bReleaseAPITable )
    {
        ModuleInstanceUtil::destroyInstance( _gameAPI, _game, bReleaseAPITable, sw::config::kTargetGameModule );
#if !defined( SW_SHIPPING )
        // 게임 컴포넌트를 모든 씬에서 걷어 냈다. 되돌릴 때까지(`restoreGameState`) 씬을 저장하면 그것들이 빠진 채 저장된다 — 리로드가 실패 ·
        // 중단되면 되돌리는 쪽이 오지 않으므로 여기서 막는다.
        if ( bReleaseAPITable && engine::areEngineServicesBound() )
            engine::getSceneManager().setSaveBlockReason( "the game module's components were removed for a reload and have not been restored yet" );
#endif
    }

    bool ModuleHost::createGameInstance()
    {
        SW_MEMORY_SCOPE( Game );
        // RHI 를 받은 호스트(App)는 디바이스가 있어야 만든다. 전용 서버는 RHI 가 없고 게임을 디바이스 없이 만든다.
        return ModuleInstanceUtil::createInstance( _gameAPI, _game, _pWindow, _pRHI, _pRHI != nullptr, "Game" );
    }
} // namespace sw
