#include "pch.h"

#include "App/Module/ModuleHost.h"

#include "App/Module/LiveReloadManager.h"
#include "App/Module/ModuleCompiler.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Renderer/RenderThread.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Window/IWindow.h"

#include "RuntimeAPI/ABI/ModuleAbi.h"
#include "RuntimeAPI/Service/ModuleService.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다(유니티 빌드에서 이름이 충돌하지 않도록 TU 이름을 붙입니다). */
        struct ModuleHostInternal
        {
            enum class Target : uint8
            {
                Editor,
                Game
            };

            /**
             * @brief 모듈이 호스트와 **같은 테이블 구조**로 빌드됐는지 대조합니다.
             * @details `GameAPI` · `EditorAPI` 는 함수 포인터를 순서대로 늘어놓은 구조체입니다. 모듈은 자기가 아는 자리에 채우고 호스트는
             *          자기가 아는 자리에서 읽으므로, 서로 다른 헤더로 빌드되면 **호스트가 엉뚱한 함수를 부릅니다.** 예전에 이것을 막는
             *          것은 아래의 `create != nullptr && destroy != nullptr` 뿐이었는데, 그 둘은 **맨 앞**에 있어서 가운데에 끼워 넣어도
             *          채워집니다. 가장 위험한 어긋남을 정확히 통과시킨 셈입니다. 핫 리로드는 모듈만 다시 굽는 기능이라 이런 어긋남이
             *          생기는 바로 그 상황입니다. RHI 경계가 `RHIModuleAbi.h` 로 하는 대조를 여기에 그대로 옮겼습니다.
             */
            static bool matchesModuleAbi( void* pLibraryModule, const utf8* pVersionSymbol, const utf8* pStampSymbol,
                                          const utf8* pModuleName )
            {
                const PFN_GetModuleAbiVersion pfnVersion =
                    reinterpret_cast<PFN_GetModuleAbiVersion>( FileUtil::getDynamicSymbol( pLibraryModule, pVersionSymbol ) );
                if ( pfnVersion == nullptr || pfnVersion() != kModuleAbiVersion )
                {
                    SW_LOG_ERROR( "%# 모듈 ABI 버전이 다릅니다 (기대 %#) — 엔진과 모듈을 함께 다시 빌드하세요.",
                                  pModuleName, kModuleAbiVersion );
                    return false;
                }

                const PFN_GetModuleAbiStamp pfnStamp =
                    reinterpret_cast<PFN_GetModuleAbiStamp>( FileUtil::getDynamicSymbol( pLibraryModule, pStampSymbol ) );
                if ( pfnStamp == nullptr || StringUtil::equals( pfnStamp(), kModuleAbiStamp ) == false )
                {
                    SW_LOG_ERROR( "%# 모듈 ABI 스탬프가 다릅니다 (기대 '%#') — 엔진과 모듈을 함께 다시 빌드하세요.",
                                  pModuleName, kModuleAbiStamp );
                    return false;
                }
                return true;
            }

            /** @brief 호스트가 제공하는 서비스 테이블을 만듭니다. 게임 모듈에는 gameAllowed=1 인 것만 노출합니다. */
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
                _pLiveReloadManager->setOnReloadFault( config::kTargetEditorModule,
                                                       SW_DELEGATE_METHOD( LiveReloadManager::OnReloadFaultDelegate, &ModuleHost::onEditorReloadFault, this ) );
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
                _pLiveReloadManager->setOnReloadFault( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnReloadFaultDelegate, &ModuleHost::onGameReloadFault, this ) );

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

        // 에디터 · 게임을 한 번의 비우기로 내린다. 예전에는 onBefore*Reload 를 그대로 불러서 drainRenderWorkers 가 두 번 돌았고,
        // 그래서 종료 경로에서 태스크 대기 제한 시간을 두 번까지 기다릴 수 있었다.
        suspendModules( ModuleScope::Both, true );

#if !defined( SW_SHIPPING )
        // 콜백은 ModuleHost 의 메서드를 가리킨다. 이 객체가 사라지기 전에 떼어 낸다.
        //
        // **모듈마다 건 것까지 뗀다.** 예전에는 이 둘만 떼고 `setOnBeforeReload`/`setOnAfterReload` 로 모듈마다 건 델리게이트는
        // 그대로 두었다. 그것도 이 객체의 메서드를 가리킨다. `App` 은 ModuleHost 를 먼저 지우고 나중에 LiveReloadManager 를
        // 내리므로, 그 사이에 리로드가 한 번 돌면 이미 사라진 객체를 부른다. 지금은 그런 순서로 돌지 않지만, "뗀다" 고 적어 두고
        // 절반만 떼면 다음 사람은 모두 뗀 줄 안다. 이름을 여기에 다시 적지 않으려고 등록부 쪽에 창구를 두었다(키트 모듈은
        // 설정에서 오므로 이 자리에서는 이름을 알 수도 없다).
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

    void ModuleHost::updateEditorUi( float32 /*deltaTime*/ )
    {
        if ( hasEditor() == false )
            return;

        if ( _editorApi.updateUi != nullptr )
            _editorApi.updateUi( _editor );

        // 에디터가 이번 프레임 입력을 처리한 **뒤에** 확정한다. Step 버튼은 이 갱신에서 눌리고, 씬을 한 칸 틱한 다음
        // endEditorFrame 에서 소비된다. 이 질의를 프레임 앞으로 옮기면 Step 이 틱 없이 소비되어 아무 일도 일어나지 않는다.
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

        // 에디터 내부 상태 갱신과 입력 필터링은 에디터 모듈 안에서 처리한다
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
        if ( bindEditorApi( pLibraryModule ) == false )
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
        if ( _gameApi.create == nullptr && bindGameApi( nullptr ) == false )
            return;
#else
        void* pModuleHandle = pLibraryModule;
        if ( pModuleHandle == nullptr && _pLiveReloadManager != nullptr )
            pModuleHandle = _pLiveReloadManager->getModuleHandle( sw::config::kTargetGameModule );

        if ( bindGameApi( pModuleHandle ) == false )
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

        // 렌더 워커가 일을 끝내고 쉬게 됐으면 에디터의 "렌더 대기" 표시도 함께 버려야 한다. 그 표시는 렌더 스레드의
        // postPresent 만 풀 수 있는데, 그 스레드는 방금 일을 끝내고 쉬고 있다. 알려 주지 않으면 다음 updateUi 나 shutdown 이
        // waitForDrawSnapshotIdle 에서 영원히 돌아오지 않는다. 에디터 모듈 핫 리로드가 실제로 여기서 멈췄다.
        if ( _editor != nullptr && _editorApi.abandonPendingDraw != nullptr )
            _editorApi.abandonPendingDraw( _editor );

        // 모듈을 내리기 전에 비동기 태스크가 모두 끝나기를 기다린다. 제한 시간은 LiveReloadManager 의 폴백과 공유한다.
        if ( engine::areEngineServicesBound() )
        {
            if ( engine::getTaskManager().waitAll( LiveReloadManager::kModuleDrainTimeoutMs ) == false )
            {
                SW_LOG_ERROR( "Task fencing timeout (%# ms) before module reload — poisoning LiveReload graph.", LiveReloadManager::kModuleDrainTimeoutMs );
                poisonLiveReload( "task fencing timeout before unload" );
            }
        }
    }

    void ModuleHost::onEditorReloadFault( uint32 faultCode )
    {
        // 반쯤 만든 인스턴스를 부수는 코드도 결함을 낸 그 모듈이다. 부르지 않고 잊는다(새는 것은 재시작이 치운다).
        SW_LOG_ERROR( "Editor module faulted after the reload (code 0x%#) — the editor is off until restart",
                      Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ) );
        _editor    = nullptr;
        _editorApi = {};
    }

    void ModuleHost::onGameReloadFault( uint32 faultCode )
    {
        SW_LOG_ERROR( "Game module faulted after the reload (code 0x%#) — the game is off until restart",
                      Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ) );
        _game    = nullptr;
        _gameApi = {};
        _listGameSavedState.clear();
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

    bool ModuleHost::bindEditorApi( void* pLibraryModule )
    {
        _editorApi = {};
        if ( pLibraryModule == nullptr )
            return false;

        if ( ModuleHostInternal::matchesModuleAbi( pLibraryModule, "getEditorModuleAbiVersion", "getEditorModuleAbiStamp", "Editor" ) == false )
            return false;

        PFN_ExportEditorAPI pfnExport = reinterpret_cast<PFN_ExportEditorAPI>( FileUtil::getDynamicSymbol( pLibraryModule, "exportEditorApi" ) );
        if ( pfnExport == nullptr || pfnExport( &_editorApi ) == false )
        {
            SW_LOG_ERROR( "Failed to bind EditorAPI from module" );
            return false;
        }

        rebindEditorService();

        engine::registerModuleTypes( sw::config::kTargetEditorModule );
        return _editorApi.create != nullptr && _editorApi.destroy != nullptr;
    }

    bool ModuleHost::bindGameApi( void* pLibraryModule )
    {
        _gameApi = {};
#if defined( SW_SHIPPING )
        (void)pLibraryModule;
        if ( exportGameApi( &_gameApi ) == false )
        {
            SW_LOG_ERROR( "Failed to bind GameAPI (shipping)" );
            return false;
        }
#else
        if ( pLibraryModule == nullptr )
            return false;
        // Shipping 은 게임을 정적으로 링크하므로(위 분기) 테이블이 어긋날 수 없다. 대조는 동적 경로에서만 한다.
        if ( ModuleHostInternal::matchesModuleAbi( pLibraryModule, "getGameModuleAbiVersion", "getGameModuleAbiStamp", "Game" ) == false )
            return false;
        PFN_ExportGameAPI pfnExport = reinterpret_cast<PFN_ExportGameAPI>( FileUtil::getDynamicSymbol( pLibraryModule, "exportGameApi" ) );
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

        // 게임만 내릴 때는 에디터가 남아 Play 상태를 유지한다. 인스턴스가 없는 동안 사라진 게임을 계속 돌리려 하므로 먼저
        // 시뮬레이션을 멈춘다. 에디터도 함께 내릴 때는 아래에서 인스턴스 자체가 사라지므로 멈출 대상이 없다.
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
        // 정적 링크라 "로드된 모듈" 이라는 것이 없다. 부르는 쪽이 nullptr 을 처리한다.
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
    // 인스턴스 생성 · 파괴 — 리로드 경로와 RHI 핫스왑 경로가 같은 코드를 쓴다
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
            // Shipping 은 모듈을 내리지 않으므로 등록 해제 자체가 없다(Engine 에도 그 코드가 없다).
#if !defined( SW_SHIPPING )
            engine::unregisterModuleTypes( sw::config::kTargetEditorModule );
#endif
        }
    }

    void ModuleHost::destroyGameInstance( bool bReleaseApiTable )
    {
        if ( _game != nullptr && _gameApi.shutdown != nullptr )
            _gameApi.shutdown( _game );
        if ( _game != nullptr && _gameApi.destroy != nullptr )
            _gameApi.destroy( _game );

#if !defined( SW_SHIPPING )
        if ( bReleaseApiTable )
            engine::unregisterModuleTypes( sw::config::kTargetGameModule );
#endif

        if ( _gameApi.bindService != nullptr )
            _gameApi.bindService( nullptr );
        _game = nullptr;

        if ( bReleaseApiTable )
            _gameApi = {};
    }

    bool ModuleHost::createEditorInstance()
    {
        // 디바이스를 인자로 넘기는 곳이다. 없으면 만들지 않는다. `getDevice()` 가 널 참조라 묻는 것 자체가 죽는 길이고, 만들어
        // 봐야 초기화가 실패할 것이 정해져 있다.
        if ( _pRHI == nullptr || _pRHI->hasDevice() == false )
        {
            SW_LOG_ERROR( "RHI 디바이스가 없어 Editor 인스턴스를 만들지 않습니다." );
            return false;
        }

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
        // 위 `createEditorInstance` 와 같은 이유다. 디바이스 없이 부르면 널 참조다.
        if ( _pRHI == nullptr || _pRHI->hasDevice() == false )
        {
            SW_LOG_ERROR( "RHI 디바이스가 없어 Game 인스턴스를 만들지 않습니다." );
            return false;
        }

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
