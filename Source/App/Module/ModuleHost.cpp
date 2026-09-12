#include "pch.h"

#include "App/Module/ModuleHost.h"

#include "App/Module/LiveReloadManager.h"
#include "App/Module/ModuleCompiler.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Renderer/RenderThread.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Window/IWindow.h"

#include "RuntimeAPI/Service/ModuleService.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 로컬 헬퍼 모음 (유니티 빌드 이름 충돌을 피하려 TU 이름을 붙인다). */
        struct ModuleHostInternal
        {
            enum class Target : uint8
            {
                Editor,
                Game
            };

            /** @brief 호스트가 제공하는 서비스 표를 만듭니다. 게임 모듈에는 gameAllowed=1 만 노출됩니다. */
            template <Target TargetModule>
            static void buildModuleService( const ModuleHost* pHost, ModuleService& outService )
            {
                engine::fillModuleServices( outService, TargetModule == Target::Game );

#define SW_HOST_SERVICE( member, Tag, Type, getter, gameAllowed )                             \
    if constexpr ( TargetModule == Target::Editor || ( ( gameAllowed ) == 1 ) )               \
    {                                                                                         \
        outService.arrServices[internal::toRawServiceId( internal::ModuleServiceId::Type )] = \
            ( pHost != nullptr ) ? pHost->getter() : nullptr;                                 \
    }

#include "RuntimeAPI/Service/HostServiceList.xxx"
#undef SW_HOST_SERVICE
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ModuleHost" );

    ModuleHost::ModuleHost()
        : _moduleCompiler{ nullptr }
        , _editorApi{}
        , _gameApi{}
        , _editor{ nullptr }
        , _game{ nullptr }
        , _pLiveReloadManager{ nullptr }
        , _pRHI{ nullptr }
        , _pWindow{ nullptr }
        , _pRenderThread{ nullptr }
        , _listGameSavedState{}
        , _frameState{}
        , _bEnableEditor{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ModuleHost::~ModuleHost()
    {
        shutdown();
    }

    bool ModuleHost::initialize( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread, bool bEnableEditor, const vector<GameKitConfig>& listGameKitModule )
    {
        _pLiveReloadManager = pLiveReloadManager;
        _pRHI               = pRHI;
        _pWindow            = pWindow;
        _pRenderThread      = pRenderThread;
        _bEnableEditor      = bEnableEditor ? SW_TRUE : SW_FALSE;

#if !defined( SW_SHIPPING )
        _moduleCompiler = make_unique<ModuleCompiler>( _pLiveReloadManager );
        if ( _pLiveReloadManager != nullptr )
        {
            _pLiveReloadManager->setDrainWorkers(
                SW_DELEGATE_METHOD( LiveReloadManager::DrainWorkersDelegate, &ModuleHost::drainRenderWorkers, this ) );
        }
#endif

#if defined( SW_SHIPPING )
        (void)listGameKitModule;
        onAfterGameReload( nullptr );
#else
        if ( _bEnableEditor == SW_TRUE && _pLiveReloadManager != nullptr )
        {
            BLOCK( "에디터: 뷰포트 / EditorModule 등록" )
            {
                _pLiveReloadManager->setOnBeforeReload( config::kTargetEditorModule,
                                                        SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeEditorReload, this ) );
                _pLiveReloadManager->setOnAfterReload( config::kTargetEditorModule,
                                                       SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterEditorReload, this ) );
                if ( _pLiveReloadManager->registerModule( config::kTargetEditorModule ) == false )
                {
                    SW_LOG_ERROR( "Editor Module 로드에 실패했습니다." );
                    return false;
                }

                if ( _pLiveReloadManager->isGraphBroken() )
                {
                    SW_LOG_ERROR( "LiveReload graph broken during module registration — aborting initialize" );
                    return false;
                }
            }
        }

        if ( _pLiveReloadManager != nullptr )
        {
            BLOCK( "게임플레이 키트 및 SWGame 모듈 등록" )
            {
                const string   gameFrameworkModule = "GameFramework";
                vector<string> listGameModule{ gameFrameworkModule };

                for ( const GameKitConfig& kitConfig : listGameKitModule )
                {
                    vector<string> listDep = kitConfig._listDependencyModule;
                    if ( listDep.empty() )
                        listDep.push_back( gameFrameworkModule );

                    if ( _pLiveReloadManager->registerModule( kitConfig._name, listDep ) == false )
                    {
                        SW_LOG_ERROR( "Kit module register failed (%#)", kitConfig._name );
                        return false;
                    }
                    _pLiveReloadManager->setOnBeforeReload( kitConfig._name, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeGameplayDllReload, this ) );
                    _pLiveReloadManager->setOnAfterReload( kitConfig._name, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterGameplayDllReload, this ) );
                    listGameModule.push_back( kitConfig._name );
                }

                _pLiveReloadManager->setOnBeforeReload( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeGameReload, this ) );
                _pLiveReloadManager->setOnAfterReload( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterGameReload, this ) );

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
            }
        }
#endif

        return true;
    }

    void ModuleHost::shutdown()
    {
        if ( _moduleCompiler != nullptr )
        {
            _moduleCompiler->shutdown();
            _moduleCompiler.reset();
        }

        // 에디터·게임을 한 번의 드레인으로 내린다. 예전엔 onBefore*Reload 를 그대로 불러서
        // drainRenderWorkers 가 두 번 돌았다 — 종료 경로에서 태스크 펜싱 타임아웃을 두 번 기다린다.
        suspendModules( ModuleScope::Both, true );

#if !defined( SW_SHIPPING )
        // 콜백은 ModuleHost 의 메서드를 가리킨다 — 이 객체가 사라지기 전에 떼어 낸다.
        if ( _pLiveReloadManager != nullptr )
        {
            _pLiveReloadManager->setDrainWorkers( {} );
            _pLiveReloadManager->setOnBeforeCommitBatch( {} );
        }
#endif
    }

    // ======================================================================
    // 프레임 단위 처리
    // ======================================================================

    bool ModuleHost::queryGameplayActive() const
    {
        if ( hasEditor() == false || _editorApi.isPlaying == nullptr )
            return true;
        return _editorApi.isPlaying( _editor );
    }

    bool ModuleHost::queryTickScene() const
    {
        if ( hasEditor() == false )
            return true;
        if ( _editorApi.isPaused != nullptr && _editorApi.isPaused( _editor ) )
        {
            if ( _editorApi.isPlaying == nullptr || _editorApi.isPlaying( _editor ) == false )
                return false;
        }
        return true;
    }

    void ModuleHost::sampleGameViewport()
    {
        _frameState._gameViewportTarget = 0;
        _frameState._gameViewportWidth  = 0;
        _frameState._gameViewportHeight = 0;
        if ( hasEditor() == false || _editorApi.getGameViewport == nullptr )
            return;

        _editorApi.getGameViewport( _editor,
                                    &_frameState._gameViewportTarget,
                                    &_frameState._gameViewportWidth,
                                    &_frameState._gameViewportHeight );
    }

    void ModuleHost::beginFrame()
    {
        _frameState                  = ModuleFrameState{};
        _frameState._bGameplayActive = queryGameplayActive() ? SW_TRUE : SW_FALSE;
    }

    void ModuleHost::updateGame( float32 deltaTime )
    {
        if ( _game != nullptr && _gameApi.update != nullptr && _frameState._bGameplayActive == SW_TRUE )
            _gameApi.update( _game, deltaTime );
    }

    void ModuleHost::fixedUpdateGame( float32 fixedDeltaTime )
    {
        if ( _game != nullptr && _gameApi.fixedUpdate != nullptr && _frameState._bGameplayActive == SW_TRUE )
            _gameApi.fixedUpdate( _game, fixedDeltaTime );
    }

    void ModuleHost::updateEditorUI( float32 /*deltaTime*/ )
    {
        if ( hasEditor() == false )
            return;

        if ( _editorApi.updateUI != nullptr )
            _editorApi.updateUI( _editor );

        // 에디터가 이번 프레임 입력을 처리한 **뒤에** 확정한다. Step 버튼은 이 갱신에서 눌리고,
        // 씬을 한 칸 틱한 다음 endEditorFrame 에서 소비된다 — 이 질의를 프레임 앞으로 옮기면
        // Step 이 틱 없이 소비되어 아무 일도 일어나지 않는다.
        _frameState._bTickScene = queryTickScene() ? SW_TRUE : SW_FALSE;
        sampleGameViewport();
    }

    void ModuleHost::endEditorFrame()
    {
        if ( hasEditor() == false || _editorApi.endFrame == nullptr )
            return;
        _editorApi.endFrame( _editor );
    }

    bool ModuleHost::onWindowMessage( const NativeWindowEvent& event )
    {
        if ( hasEditor() == false || _editorApi.processEvent == nullptr )
            return false;

        // 에디터 내부 상태 업데이트 및 입력 필터링은 Editor Module 내부에서 캡슐화 처리
        return _editorApi.processEvent( _editor, &event );
    }

    CameraComponent* ModuleHost::getViewportCamera() const
    {
        if ( hasEditor() == false || _editorApi.getViewportCamera == nullptr )
            return nullptr;
        return static_cast<CameraComponent*>( _editorApi.getViewportCamera( _editor ) );
    }

    // ======================================================================
    // LiveReload 콜백 — 에디터
    // ======================================================================

    void ModuleHost::onBeforeEditorReload()
    {
        suspendModules( ModuleScope::Editor, true );
    }

    void ModuleHost::onAfterEditorReload( void* pLibraryModule )
    {
        if ( bindEditorAPI( pLibraryModule ) == false )
        {
            poisonLiveReload( "EditorAPI bind failed after reload" );
            return;
        }

        if ( createEditorInstance() == false )
        {
            _editorApi = {};
            poisonLiveReload( "Editor create/initialize failed after reload" );
            return;
        }

        SW_LOG_INFO( "Editor initialized successfully via EditorAPI." );
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
#if defined( SW_SHIPPING )
        (void)pLibraryModule;
        if ( _gameApi.create == nullptr && bindGameAPI( nullptr ) == false )
            return;
#else
        void* pModuleHandle = pLibraryModule;
        if ( pModuleHandle == nullptr && _pLiveReloadManager != nullptr )
            pModuleHandle = _pLiveReloadManager->getModuleHandle( sw::config::kTargetGameModule );

        if ( bindGameAPI( pModuleHandle ) == false )
        {
            poisonLiveReload( "GameAPI bind failed after reload" );
            return;
        }
#endif

        if ( createGameInstance() == false )
        {
            _gameApi = {};
            poisonLiveReload( "Game create/initialize failed after reload" );
            return;
        }

        restoreGameState();

        SW_LOG_INFO( "SWGame initialized successfully via GameAPI." );
    }

    // ======================================================================
    // LiveReload 콜백 — GameFramework/Kit DLL 캐스케이드
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
                scene->getObjectManager()->rebindAllCachedTypeInfo();
        }
    }

    void ModuleHost::onBeforeCommitBatch( const vector<string>& listModuleName )
    {
#if !defined( SW_SHIPPING )
        for ( const string& name : listModuleName )
        {
            if ( name != sw::config::kTargetEditorModule )
            {
                onBeforeGameplayDllReload();
                return;
            }
        }
#else
        (void)listModuleName;
#endif
    }

    // ======================================================================
    // 보조 — drain / poison
    // ======================================================================

    void ModuleHost::drainRenderWorkers()
    {
        if ( _pRenderThread != nullptr )
            _pRenderThread->waitIdle();
        if ( _pRHI != nullptr )
            _pRHI->getDevice().waitIdle();

        // 렌더 워커를 재웠으면 에디터의 "렌더 대기" 표시도 같이 버려야 한다.
        // 그 표시는 렌더 스레드의 postPresent 만 풀 수 있는데, 방금 그 스레드를 재웠다.
        // 알려 주지 않으면 다음 updateUI 나 shutdown 이 waitForDrawSnapshotIdle 에서
        // 영원히 돌아오지 않는다 — 에디터 모듈 핫리로드가 실제로 여기서 멈췄다.
        if ( _editor != nullptr && _editorApi.abandonPendingDraw != nullptr )
            _editorApi.abandonPendingDraw( _editor );

        // 비동기 태스크 펜싱 (Module Unload 전 안전 보장). 타임아웃은 LiveReloadManager 폴백과 공유합니다.
        if ( engine::areEngineServicesBound() )
        {
            if ( engine::getTaskManager().waitAll( LiveReloadManager::kModuleDrainTimeoutMs ) == false )
            {
                SW_LOG_ERROR( "Task fencing timeout (%# ms) before module reload — poisoning LiveReload graph.", LiveReloadManager::kModuleDrainTimeoutMs );
                poisonLiveReload( "task fencing timeout before unload" );
            }
        }
    }

    void ModuleHost::poisonLiveReload( const utf8* pReason )
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

    bool ModuleHost::bindEditorAPI( void* pLibraryModule )
    {
        _editorApi = {};
        if ( pLibraryModule == nullptr )
            return false;

        PFN_ExportEditorAPI pfnExport = reinterpret_cast<PFN_ExportEditorAPI>( FileUtil::getDynamicSymbol( pLibraryModule, "exportEditorAPI" ) );
        if ( pfnExport == nullptr || pfnExport( &_editorApi ) == false )
        {
            SW_LOG_ERROR( "Failed to bind EditorAPI from module" );
            return false;
        }

        rebindEditorService();

        engine::registerModuleTypes( sw::config::kTargetEditorModule );
        return _editorApi.create != nullptr && _editorApi.destroy != nullptr;
    }

    bool ModuleHost::bindGameAPI( void* pLibraryModule )
    {
        _gameApi = {};
#if defined( SW_SHIPPING )
        (void)pLibraryModule;
        if ( exportGameAPI( &_gameApi ) == false )
        {
            SW_LOG_ERROR( "Failed to bind GameAPI (shipping)" );
            return false;
        }
#else
        if ( pLibraryModule == nullptr )
            return false;
        PFN_ExportGameAPI pfnExport = reinterpret_cast<PFN_ExportGameAPI>( FileUtil::getDynamicSymbol( pLibraryModule, "exportGameAPI" ) );
        if ( pfnExport == nullptr || pfnExport( &_gameApi ) == false )
        {
            SW_LOG_ERROR( "Failed to bind GameAPI from module" );
            return false;
        }
#endif

        rebindGameService();

        engine::registerModuleTypes( sw::config::kTargetGameModule );
        return _gameApi.create != nullptr && _gameApi.destroy != nullptr;
    }

    void ModuleHost::suspendModules( ModuleScope scope, bool bReleaseApiTable )
    {
        drainRenderWorkers();

        const bool bSuspendEditor = scope != ModuleScope::Game;
        const bool bSuspendGame   = scope != ModuleScope::Editor;

        // 게임만 내릴 때는 에디터가 남아 Play 상태를 유지한다 — 인스턴스가 없는 동안 죽은 게임을
        // 계속 돌리려 하므로 먼저 시뮬레이션을 멈춘다. 에디터도 같이 내릴 때는 아래에서 인스턴스
        // 자체가 사라지므로 멈출 대상이 없다.
        const bool bStopSimulation = bSuspendGame && bSuspendEditor == false && hasEditor() && _editorApi.stopSimulation != nullptr;
        if ( bStopSimulation )
        {
            SW_LOG_INFO( "Stopping editor simulation before game module reload." );
            _editorApi.stopSimulation( _editor );
        }

        if ( bSuspendGame )
            captureGameState();
        if ( bSuspendEditor )
            destroyEditorInstance( bReleaseApiTable );
        if ( bSuspendGame )
            destroyGameInstance( bReleaseApiTable );
    }

    bool ModuleHost::reinitializeAfterRhiSwap( void* pEditorModule, void* pGameModule )
    {
        if ( _pRHI == nullptr || _pRHI->hasDevice() == false )
            return false;

        bool bOk = true;
        if ( recreateEditorInstance( pEditorModule ) == false )
            bOk = false;
        if ( recreateGameInstance( pGameModule ) == false )
            bOk = false;
        return bOk;
    }

    void* ModuleHost::getLoadedModuleHandle( [[maybe_unused]] string_view moduleName ) const
    {
#if defined( SW_SHIPPING )
        // 정적 링크라 "로드된 모듈" 이라는 것이 없다 — 부르는 쪽이 nullptr 을 처리한다.
        return nullptr;
#else
        if ( _pLiveReloadManager == nullptr )
            return nullptr;
        return _pLiveReloadManager->getModuleHandle( moduleName );
#endif
    }

    void ModuleHost::captureGameState()
    {
        if ( _game == nullptr || _gameApi.serializeState == nullptr )
            return;

        uint32 size{ 0 };
        if ( _gameApi.serializeState( _game, nullptr, &size ) == false || size == 0 )
            return;

        vector<uint8> tempState( size );
        if ( _gameApi.serializeState( _game, tempState.data(), &size ) )
            _listGameSavedState = std::move( tempState );
    }

    void ModuleHost::restoreGameState()
    {
        if ( _game == nullptr || _gameApi.deserializeState == nullptr || _listGameSavedState.empty() )
            return;

        if ( _gameApi.deserializeState( _game, _listGameSavedState.data(), static_cast<uint32>( _listGameSavedState.size() ) ) )
        {
            SW_LOG_INFO( "Scene object state restored from %zu bytes.", _listGameSavedState.size() );
            _listGameSavedState.clear();
        }
        else
        {
            SW_LOG_ERROR( "Failed to restore game state — keeping saved state for subsequent reload." );
        }
    }

    bool ModuleHost::recreateEditorInstance( void* pEditorModule )
    {
        if ( _bEnableEditor == SW_FALSE )
            return true;

        // 테이블이 비어 있으면 모듈에서 다시 바인딩해야 한다 — 리로드 경로가 그 일을 한다.
        if ( _editorApi.create == nullptr || _editorApi.initialize == nullptr )
        {
            onAfterEditorReload( pEditorModule );
            return _editor != nullptr;
        }

        rebindEditorService();
        return createEditorInstance();
    }

    bool ModuleHost::recreateGameInstance( void* pGameModule )
    {
        if ( _gameApi.create == nullptr || _gameApi.initialize == nullptr )
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
    // 인스턴스 생성·파괴 — 리로드 경로와 RHI 핫스왑 경로가 같은 코드를 쓴다
    // ======================================================================

    void ModuleHost::rebindEditorService()
    {
        if ( _editorApi.bindService == nullptr )
            return;

        ModuleService editorService{};
        ModuleHostInternal::buildModuleService<ModuleHostInternal::Target::Editor>( this, editorService );
        _editorApi.bindService( &editorService );
    }

    void ModuleHost::rebindGameService()
    {
        if ( _gameApi.bindService == nullptr )
            return;

        ModuleService gameService{};
        ModuleHostInternal::buildModuleService<ModuleHostInternal::Target::Game>( this, gameService );
        _gameApi.bindService( &gameService );
    }

    void ModuleHost::destroyEditorInstance( bool bReleaseApiTable )
    {
        if ( _editor != nullptr && _editorApi.shutdown != nullptr )
            _editorApi.shutdown( _editor );
        if ( _editor != nullptr && _editorApi.destroy != nullptr )
            _editorApi.destroy( _editor );
        if ( _editorApi.bindService != nullptr )
            _editorApi.bindService( nullptr );
        _editor = nullptr;

        if ( bReleaseApiTable )
        {
            _editorApi = {};
            engine::unregisterModuleTypes( sw::config::kTargetEditorModule );
        }
    }

    void ModuleHost::destroyGameInstance( bool bReleaseApiTable )
    {
        if ( _game != nullptr && _gameApi.shutdown != nullptr )
            _gameApi.shutdown( _game );
        if ( _game != nullptr && _gameApi.destroy != nullptr )
            _gameApi.destroy( _game );

        if ( bReleaseApiTable )
            engine::unregisterModuleTypes( sw::config::kTargetGameModule );

        if ( _gameApi.bindService != nullptr )
            _gameApi.bindService( nullptr );
        _game = nullptr;

        if ( bReleaseApiTable )
            _gameApi = {};
    }

    bool ModuleHost::createEditorInstance()
    {
        _editor = _editorApi.create();
        if ( _editor == nullptr )
        {
            SW_LOG_ERROR( "Failed to create Editor instance" );
            return false;
        }

        if ( _editorApi.initialize( _editor, _pWindow, &_pRHI->getDevice() ) == false )
        {
            SW_LOG_ERROR( "Failed to initialize Editor instance" );
            if ( _editorApi.destroy != nullptr )
                _editorApi.destroy( _editor );
            _editor = nullptr;
            return false;
        }

        return true;
    }

    bool ModuleHost::createGameInstance()
    {
        _game = _gameApi.create();
        if ( _game == nullptr )
        {
            SW_LOG_ERROR( "Failed to create Game instance" );
            return false;
        }

        if ( _gameApi.initialize( _game, _pWindow, &_pRHI->getDevice() ) == false )
        {
            SW_LOG_ERROR( "Failed to initialize Game instance" );
            if ( _gameApi.destroy != nullptr )
                _gameApi.destroy( _game );
            _game = nullptr;
            return false;
        }

        return true;
    }
} // namespace sw
