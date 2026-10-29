#include "pch.h"

#include "App/App.h"

#include "App/AppAssertDialog.h"
#include "App/AppHeadlessTasks.h"
#include "App/EditorModuleManager.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/MonotonicClock.h"

#ifndef SW_AUTOMATION_ENABLED
    #error "SW_AUTOMATION_ENABLED comes from CMake (cmake/Engine/BuildLayout.cmake, option SW_ENABLE_AUTOMATION)"
#endif
#if SW_AUTOMATION_ENABLED
    #include "Engine/Automation/Runner/AutomationSession.h"
    #include "Engine/Automation/Scenario/ScenarioPlayback.h"
#endif
#if !defined( SW_SHIPPING )
    #include "DevTools/AssetReload/ContentFolderWatchService.h"
    #include "DevTools/DevToolsHost.h"
#endif
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Module/HostServices.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/SceneStateHash.h"
#include "Engine/UI/Accessibility/IUIAccessibilityBridge.h"
#include "Engine/UI/Accessibility/UIAccessibilityService.h"
#include "Engine/UI/Input/UIClipboardService.h"
#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"
#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/PlatformClipboard.h"
#include "Engine/Window/SplashWindow.h"

#include "ModuleManager/ModuleCatalogLoader.h"
#include "ModuleManager/ModuleLoaderSubsystem.h"
#include "ModuleManager/ModuleStateStore.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        struct AppInternal
        {
            /** @brief 에디터 창의 최소 클라이언트 크기입니다. 메뉴바, 도크 다섯 개, 게임 뷰 툴바가 겹치지 않는 하한입니다. */
            static constexpr uint32 kEditorMinClientWidth  = 960;
            static constexpr uint32 kEditorMinClientHeight = 540;
#if !defined( SW_SHIPPING )
            /** @brief 에디터 모듈(EditorModule) 핫 리로드 단축키의 액션 이름(`default.input.xml` 의 Debug 레이어). 에디터를 아는 App 만 쓴다(배포본엔 리로드가 없다). */
            static constexpr const utf8* kReloadEditorAction = "ReloadEditor";
#endif

            /** @brief 이 기계 · 빌드가 그 백엔드를 못 돌린다는(환경) 결과면 true 입니다. 결함일 수 있는 `Failed` 는 false 입니다. */
            static constexpr bool isRHIUnusableHere( RHIInitResult result )
            {
                return result == RHIInitResult::BackendNotBuilt || result == RHIInitResult::DriverUnsupported;
            }

            /** @brief 엔진 설정에서 프레임 시간 정책 값을 꺼냅니다(값의 출처는 `Config/Engine/EngineConfig.json` 하나다). */
            static FixedTimestepParams makeFixedTimestepParams( const EngineConfig& config )
            {
                FixedTimestepParams params;
                params._maxFrameDeltaTime    = config._maxFrameDeltaTime;
                params._fixedDeltaTime       = config._fixedDeltaTime;
                params._maxFixedStepPerFrame = config._maxFixedStepPerFrame;
                return params;
            }

            /** @brief 감시 값 `Engine.FramePacingState` — 앱 루프가 이번 프레임에 고른 페이싱 상태 번호입니다. 주 창 서비스가 없으면(헤드리스) 값이 없습니다. */
            [[nodiscard]] static bool readFramePacingState( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                if ( pWindowHost == nullptr )
                    return false;
                outValue = static_cast<float64>( static_cast<uint32>( pWindowHost->getFramePacer().getState() ) );
                return true;
            }
        };
    } // namespace

    /**
     * @brief `-gv_fixedFrameDelta=<초>`: 0 보다 크면 프레임마다 실제 시계 대신 그 시간을 흘립니다(자동화 시나리오, 픽셀 비교, 재현). 0 이면 실시간입니다.
     * @details 고정 스텝(물리, fixedUpdate)도 이 시간으로 나뉩니다. 1/60 이면 프레임마다 정확히 한 스텝입니다. 시나리오(`-scenario`)는 이 값을
     *          시나리오의 `fixedDelta` 로 설정합니다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( float32, gv_fixedFrameDelta, 0.0f, "프레임마다 흘릴 고정 시간(초, 0=실시간) — 결정적 실행" );

    SW_AUTOMATION_WATCH( engineFramePacingState, "Engine.FramePacingState",
                         "App loop pacing this frame: 0 Foreground, 1 Background (gv_backgroundFrameRate), 2 Minimized, 3 Automation, 4 Server, 5 FullSpeed (gv_framePacing), 6 EditorIdle",
                         &AppInternal::readFramePacingState );
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "App" );

    App::App()
        : _host{}
        , _pModuleImageHost{ nullptr }
        , _pAutomationSession{ nullptr }
        , _pDevTools{ nullptr }
        , _devToolsFrameSubscription{}
        , _engine{}
        , _moduleReload{}
        , _gameModuleManager{ nullptr }
        , _pWindow{ nullptr }
        , _fixedTimestep{}
        , _backendSwap{}
        , _userSettingsHost{}
        , _sceneViewCameraProvider{}
        , _configReloadedSubscription{}
        , _windowResizeSubscription{}
        , _windowMessageSubscription{}
        , _initializeStartMicro{ 0 }
        , _hostServices{}
        , _bEnableEditor{ SW_FALSE }
        , _bQuitAfterInitialize{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    App::~App() = default;

    // 길이를 나누지 않는다: 호스트 범위 → 엔진 → 헤드리스 작업 → 자동화 → 설정 감시 순의 시동 열이라 순서가 곧 뜻이다.
    // `Engine::initialize` 와 같은 이유다.
    bool App::initialize( int32 argc, utf8* pArgv[] )
    {
        _initializeStartMicro = MonotonicClock::nowMicroseconds();
        // 리소스 루트 탐색은 Engine 가 로거를 시작한 **뒤에** 한다. 여기서 먼저 부르면 실패했을 때 로거가 없어 진단 메시지가
        // 사라지고, 반환값도 여기서는 쓸 곳이 없다.

        // 0. 호스트 범위를 먼저 세운다 — 엔진 범위의 부모다. 엔진의 `ModuleLoadStage` 기동 단계가 여기서 모듈 라이브러리 창구를 찾아 타입 공급자 모듈을 올리고
        //    (이 App 의 `loadModuleLibrarys`), `ModuleUnloadStage` 종료 단계가 같은 창구로 모듈 DLL 을 내린다.
        if ( initializeHostScope() == false )
            return false;
        // 시동 단계별 시간(측정만 한다 — 시동 요약 로그의 재료). 각 값은 그 단계가 끝난 시각의 `_initializeStartMicro` 기준 경과이다.
        [[maybe_unused]] const int64 hostScopeEndMicro = MonotonicClock::nowMicroseconds();

        // 1. 코어 매니저는 모두 Engine 가 초기화한다(헤드리스 작업 처리 포함).
        if ( _engine.initialize( argc, pArgv, EngineHostRole::Client, &_host ) == false )
        {
            SW_LOG_ERROR( "Engine initialization failed." );
            return false;
        }
        [[maybe_unused]] const int64 engineEndMicro = MonotonicClock::nowMicroseconds();
#if !defined( SW_SHIPPING )
        // 엔진이 섰다 — 개발 도구의 프레임 훅을 붙인다(씬 틱 뒤 HUD · 콘솔, 프레임 끝에 셰이더 재컴파일 · 단축키 · 설정 리로드).
        _pDevTools->setEngine( &_engine );
        _devToolsFrameSubscription = _engine.registerFrameHooks( _pDevTools->makeFrameHooks() );
#endif

        // 헤드리스 모드(예: --cook-shaders, --cook-scenes)면 스플래시와 창 UI 를 건너뛴다.
        // 작업이 실패했으면 **초기화 실패로 반환한다.** 그래야 종료 코드가 0 이 아니고, 이 프로세스를 실행하는 빌드(CookAssets 타깃) · `Pack.py` 가
        // "쿠킹하지 못했다"는 사실을 알 수 있다.
        if ( _engine.isHeadless() )
        {
            if ( _engine.didHeadlessTaskFail() )
                return false;
            // App 이 맡는 헤드리스 작업(크래시 보고, 로컬라이제이션 도구, 원본 임포트)은 `AppHeadlessTasks` 의 표가 정한다.
            return AppHeadlessTasks::runAll( _engine.getCommandLineManager() );
        }

        SplashWindow splash;
        (void)splash.initialize( "SW Engine", "Initializing Engine Subsystems..." ); // 스플래시 없이 기동하는 것도 정상 경로다(SplashWindow::initialize 설명)
        splash.setProgress( 0.05f );

        splash.updateStatus( "Loading Configuration & Display...", 0.40f );

        ConfigManager*            pConfigManager      = _engine.getConfigManager();
        const CommandLineManager* pCommandLineManager = _engine.getCommandLineManager();
        if ( pConfigManager == nullptr || pCommandLineManager == nullptr )
        {
            splash.dismiss();
            return false;
        }

        const EngineConfig* pEngineConfig = pConfigManager->getConfig<EngineConfig>();
        if ( pEngineConfig == nullptr )
        {
            splash.dismiss();
            return false;
        }

        // 자동화 시나리오는 엔진 기동이 끝난 지금, 모듈을 올리기 전에 시작한다(첫 단계의 시작 조건이 모듈 · 씬이 설 때까지 기다린다).
        startAutomationSession( *pCommandLineManager );

        _fixedTimestep.configure( AppInternal::makeFixedTimestepParams( *pEngineConfig ) );
        // 에디터가 Config/ 를 감시해 다시 읽으면(ConfigManager::reloadConfigFile) 프레임 시간 정책도 따라간다.
        _configReloadedSubscription = pConfigManager->onConfigReloaded().add( SW_DELEGATE_METHOD( Delegate<void( const hashed_string& )>, &App::onConfigReloaded, this ) );

        // 2. 창 소유권을 가져온다(초기화 중에는 숨긴 상태로 시작한다).
        splash.updateStatus( "Initializing Platform Window & Graphics...", 0.60f );
        if ( acquireMainWindow( *pEngineConfig, *pCommandLineManager ) == false )
        {
            splash.dismiss();
            return false;
        }
        [[maybe_unused]] const int64 windowEndMicro = MonotonicClock::nowMicroseconds();

        // 3. 호스트 서비스(게임 · 에디터 인스턴스, 교체기, 사용자 설정, 런타임 UI 훅, 개발 콘솔)을 의존 순서로 세운다. 내리는 것은 `shutdown` 의 역순 한 줄이다.
        splash.updateStatus( "Loading Modules & Compiling Shaders...", 0.75f );
        if ( startHostServices( _hostServices, kArrClientHostService, SW_DELEGATE_METHOD( HostServiceStartDelegate, &App::startHostService, this ),
                                SW_DELEGATE_METHOD( HostServiceStopDelegate, &App::stopHostService, this ) ) == false )
        {
            SW_LOG_ERROR( "Host services failed to start: %#", _hostServices.getError().c_str() );
            splash.dismiss();
            return false;
        }
        SW_LOG_INFO( "Host services up: %#", describeHostServiceOrder( _hostServices ).c_str() );
        [[maybe_unused]] const int64 modulesEndMicro = MonotonicClock::nowMicroseconds();

        warnUnknownGlobalOverrides();

        // 초상화 베이크(`--render-portraits=a.prefab.xml,b.prefab.xml`)는 게임 컴포넌트 타입이 로드된 지금 그리고 끝낸다(창은 띄우지 않는다).
        string portraitList;
        if ( pCommandLineManager->getArgument( CommandLineArgument::RENDER_PORTRAITS, portraitList ) && portraitList.empty() == false )
        {
            int32  portraitSize = 256;
            string portraitDir;
            pCommandLineManager->getArgument( CommandLineArgument::PORTRAIT_SIZE, portraitSize );
            pCommandLineManager->getArgument( CommandLineArgument::PORTRAIT_DIR, portraitDir );
            splash.dismiss();
            _bQuitAfterInitialize = SW_TRUE;
            return _engine.renderPortraits( portraitList, portraitDir, static_cast<uint32>( portraitSize > 0 ? portraitSize : 256 ) );
        }

        // 4. 창 콜백과 이벤트 전달을 설정한다.
        splash.updateStatus( "Finalizing Setup...", 0.95f );
        bindHostCallbacks();

        splash.updateStatus( "Ready", 1.0f );

        // 준비가 끝났다. 스플래시 창을 닫고 메인 창을 화면에 띄운다.
        splash.dismiss();
        _pWindow->showWindow( true );

        // 시동 요약 한 줄 — 단계별 ms 와 오래 걸린 서브시스템(`Test/Load/Baselines.json` 의 `startup_ms` 와 별개의 진단 값이다).
        {
            [[maybe_unused]] const int64     showEndMicro = MonotonicClock::nowMicroseconds();
            [[maybe_unused]] constexpr int64 kMs          = constant::kMicrosecondsPerMillisecond;
            SW_LOG_INFO( "Startup phases: hostScope %# ms, engine %# ms, window %# ms, modules %# ms, finalize+show %# ms, total %# ms | subsystems: %#",
                         ( hostScopeEndMicro - _initializeStartMicro ) / kMs, ( engineEndMicro - hostScopeEndMicro ) / kMs, ( windowEndMicro - engineEndMicro ) / kMs,
                         ( modulesEndMicro - windowEndMicro ) / kMs, ( showEndMicro - modulesEndMicro ) / kMs, ( showEndMicro - _initializeStartMicro ) / kMs,
                         _engine.describeSubsystemInitTimings().c_str() );
        }

        return true;
    }

    bool App::initializeHostScope()
    {
        // 자동화 구동기는 App 만 든다 — 범위를 세우기(Server 와 같은 한 곳) 전에 더한다. 엔진이 선 뒤 `begin` 으로 시작한다.
#if SW_AUTOMATION_ENABLED
        {
            SW_MEMORY_SCOPE( EngineMisc );
            _pAutomationSession = &_host.addSubsystem( make_unique<AutomationSession>() );
        }
#endif
        // 모듈이 리로드 · 인스턴스 재생성 사이에 데이터를 맡기는 칸(편집기의 되돌리기 기록 · 선택 · 복사 버퍼). 모듈은 인터페이스 형으로 찾는다.
#if !defined( SW_SHIPPING )
        // 개발 도구 — 엔진 기동 확장(델리게이트)으로 걸어, 엔진이 기동 단계 범위를 채울 때 자기 서브시스템을 더한다(엔진은 무엇인지 모른다).
        {
            SW_MEMORY_SCOPE( EngineMisc );
            _pDevTools = &_host.addSubsystem( make_unique<DevToolsHost>() );
        }
        _engine.setStartupExtension( _pDevTools->makeStartupExtension() );
        // 콘텐츠 폴더 감시 — 기능 · 게임의 데이터 다시 읽기가 범위에서 찾는다(배포에는 없다).
        const bool bFolderWatchBound = _host.bind<IContentFolderWatch>( &_pDevTools->getContentFolderWatch() );
        SW_ASSERT( bFolderWatchBound );
        (void)bFolderWatchBound;
#endif
        const bool bStateStoreBound = _host.bind<IModuleStateStore>( &_moduleStateStore );
        SW_ASSERT( bStateStoreBound );
        (void)bStateStoreBound;
        _pModuleImageHost = ModuleLoaderSubsystem::initializeHostScope( _host, SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &App::loadModuleLibrarys, this ), "App" );
        return _pModuleImageHost != nullptr;
    }

    bool App::acquireMainWindow( const EngineConfig& engineConfig, const CommandLineManager& commandLineManager )
    {
        uint32 width  = engineConfig._window._width;
        uint32 height = engineConfig._window._height;
        commandLineManager.getArgument( CommandLineArgument::WIDTH, width );
        commandLineManager.getArgument( CommandLineArgument::HEIGHT, height );

        // 주 창은 엔진 범위의 서브시스템(MainWindow)이 소유한다. 엔진 기동(RHI)이 기본 창을 만들어 넘겨 두었으면 그것을 쓰고, 없으면(헤드리스 작업 등) 여기서 만들어 넘긴다.
        // App 은 관찰 포인터만 든다 — 창은 엔진 종료 때 그 서브시스템이 파괴한다.
        MainWindowSubsystem* const pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        if ( pWindowHost == nullptr )
        {
            SW_LOG_ERROR( "The engine scope has no MainWindow subsystem!" );
            return false;
        }
        if ( pWindowHost->getWindow() == nullptr )
        {
            unique_ptr<IWindow> window   = IWindow::createPlatformWindow();
            const bool          bCreated = window != nullptr && window->applyCommandLinePosition( commandLineManager ) &&
                                  window->initializeWindow( engine::getSubsystem<PackEngineSettings>()._windowTitle.c_str(), width, height );
            if ( bCreated == false )
            {
                SW_LOG_ERROR( "Failed to create platform window!" );
                return false;
            }
            window->showWindow( false );
            if ( pWindowHost->adopt( window ) == false )
            {
                SW_LOG_ERROR( "The main window owner refused the platform window!" );
                return false;
            }
        }
        _pWindow = pWindowHost->getWindow();

        bool bEnableEditor = false;
        commandLineManager.getArgument( CommandLineArgument::ENABLE_EDITOR, bEnableEditor );

        // 백엔드가 에디터를 지원하는지는 여기서 미리 묻지 않는다. 답을 아는 것은 에디터다. 지원하지 못하면
        // ImGuiEditor::initialize 가 렌더러 백엔드를 만들지 못해 실패하고, 에디터만 뜨지 않는다.
        _bEnableEditor = bEnableEditor ? SW_TRUE : SW_FALSE;

        return true;
    }

    bool App::loadModuleLibrarys()
    {
        // 모듈 호스트, 감시자, 앱 설정은 엔진 기반의 일이다. 모듈 본체의 로드는 GameModuleManager 가 에디터와 게임 태그를 걸어 처리한다.
        SW_MEMORY_SCOPE( EngineMisc );
#if !defined( SW_SHIPPING )
        // 무엇을 로드할지는 모듈 매니페스트가 정한다. App 은 빌드가 담은 모든 대상을 로드한다(Game 은 클라이언트와 리슨 서버용 서버 모듈,
        // Client 는 클라이언트). 전용 서버는 Server 실행 파일이 Server 대상으로 같은 해석을 한다.
        if ( ModuleCatalogLoader::loadAndResolve( ModuleCatalog::getBuildTargetMask(), _moduleCatalog, _moduleResolution ) == false )
            return false;

        // 모듈 감시자는 모듈을 로드하는 GameModuleManager 보다 먼저 있어야 한다. GameModuleManager 가 이 포인터로 모듈을 로드하고 리로드 콜백을 등록한다.
        // 구동기는 자신을 호스트 범위에 건다(개발 명령 `module.reload` · 감시 값 `Module.*` 가 찾는다).
        _moduleReload.initialize( _host, *_pModuleImageHost );
#endif

        _gameModuleManager = make_unique<EditorModuleManager>();
        return _gameModuleManager->loadModuleLibrarys( _moduleReload.getLiveReloadManager(), _moduleCatalog, _moduleResolution );
    }

    bool App::startHostService( HostServiceID id )
    {
        const bool bEnableEditor = _bEnableEditor == SW_TRUE;
        switch ( id )
        {
            case HostServiceID::Game:
            {
                if ( _gameModuleManager == nullptr )
                    _gameModuleManager = make_unique<EditorModuleManager>();
                const bool bStarted =
                    _gameModuleManager->initialize( _moduleReload.getLiveReloadManager(), _engine.getRHI(), _pWindow, _engine.getRenderThread(), bEnableEditor );
                if ( bStarted == false )
                {
                    SW_LOG_ERROR( "GameModuleManager initialization failed." );
                    return false;
                }
                // 시나리오 체인이 단계마다 게임을 처음부터 다시 세운다(`AutomationSession::startStage` 가 `engine::findSubsystem<IGameRestarter>()` 로 찾는다).
                return _host.bind<IGameRestarter>( _gameModuleManager.get() );
            }
            case HostServiceID::BackendSwitcher:
            {
                _backendSwap.initialize( &_engine, _gameModuleManager.get(), bEnableEditor );
                // 시나리오 단계 `SwapBackend` 와 감시 값 `RHI.*` 가 `engine::findSubsystem<RHIBackendSwitcher>()` 로 찾는다(엔진 범위 → 호스트 범위).
                return _host.bind( &_backendSwap );
            }
            case HostServiceID::UserSettings:
            {
                _userSettingsHost.initialize( &_engine, _pWindow );
                return true;
            }
            case HostServiceID::RuntimeUIHooks:
            {
                installRuntimeUIHooks();
                return true;
            }
            case HostServiceID::DevConsole:
            {
#if !defined( SW_SHIPPING )
                _pDevTools->startConsole( bEnableEditor );
#endif
                return true;
            }
            case HostServiceID::Observability:
            case HostServiceID::CrashReports:
            case HostServiceID::ServiceContext:
            case HostServiceID::StandardInput:
            case HostServiceID::DebugChannel:
            {
                break;
            }
        }
        SW_LOG_ERROR( "Host service %# is not a client service", HostService::getName( id ) );
        return false;
    }

    void App::stopHostService( HostServiceID id )
    {
        switch ( id )
        {
            case HostServiceID::Game:
            {
                _host.unbind<IGameRestarter>();
                // 에디터 shutdown 이 씬 뷰 · 게임 뷰 RT 를 놓을 때 렌더 스레드와 RHI 디바이스를 쓴다 — 호스트 서비스는 엔진 종료 앞에서 내려간다.
                _gameModuleManager->shutdown();
                _gameModuleManager.reset();
                return;
            }
            case HostServiceID::BackendSwitcher:
            {
                // 범위에서 먼저 풀어 자동화가 내려간 교체기를 찾지 않게 한다. 전역 변수 훅은 GlobalVariableManager(엔진 소유)가 사라지기 전에 뗀다.
                _host.unbind<RHIBackendSwitcher>();
                _backendSwap.shutdown();
                return;
            }
            case HostServiceID::UserSettings:
            {
                _userSettingsHost.shutdown();
                return;
            }
            case HostServiceID::RuntimeUIHooks:
            {
                removeRuntimeUIHooks();
                return;
            }
            case HostServiceID::DevConsole:
            {
#if !defined( SW_SHIPPING )
                _pDevTools->stopConsole();
#endif
                return;
            }
            case HostServiceID::Observability:
            case HostServiceID::CrashReports:
            case HostServiceID::ServiceContext:
            case HostServiceID::StandardInput:
            case HostServiceID::DebugChannel:
            {
                return;
            }
        }
    }

    void App::warnUnknownGlobalOverrides() const
    {
        const CommandLineManager* pCommandLineManager = _engine.getCommandLineManager();
        if ( pCommandLineManager == nullptr )
            return;

        // 모르는 `-gv_*` 키는 파서가 버리지 않고 보류 테이블에 남긴다. 모듈이 선언하는 변수는 파싱 시점에 아직 없기 때문이다.
        // 모듈이 모두 로드된 지금까지도 가져간 곳이 없으면 오타다. 테이블은 비우지 않는다. 핫 리로드로 나중에 로드되는 모듈이
        // 여전히 가져갈 수 있기 때문이다.
        const vector<string> listPendingName = pCommandLineManager->collectPendingGlobalNames();
        for ( const string& pendingName : listPendingName )
        {
            if ( engine::getSubsystem<GlobalVariableManager>().findVariable( pendingName ) == nullptr )
                SW_LOG_WARNING( "%#: no such global variable. Ignored", pendingName.c_str() );
        }
    }

#if SW_AUTOMATION_ENABLED
    void App::startAutomationSession( const CommandLineManager& commandLineManager )
    {
        string recordPath;
        string recordProbes;
        if ( commandLineManager.getArgument( CommandLineArgument::SCENARIO_RECORD, recordPath ) && recordPath.empty() == false )
        {
            (void)commandLineManager.getArgument( CommandLineArgument::SCENARIO_RECORD_PROBES, recordProbes ); // 없으면 검사점 없이 입력만 녹화한다
            (void)_pAutomationSession->beginRecording( recordPath, recordProbes );                             // 거절하면 이유를 로그로 남기고 녹화 없이 돈다
        }
        string scenarioPath;
        string reportPath;
        (void)commandLineManager.getArgument( CommandLineArgument::SCENARIO_REPORT, reportPath ); // 없으면 보고 파일을 쓰지 않는다
        RenderThread* pRenderThread = _engine.getRenderThread();
        const bool    bKeepGoing    = commandLineManager.isArgumentProvided( CommandLineArgument::SCENARIO_KEEP_GOING );
        const bool    bScenario     = commandLineManager.getArgument( CommandLineArgument::SCENARIO, scenarioPath ) && scenarioPath.empty() == false;
        const bool    bStarted =
            bScenario && _pAutomationSession->begin( scenarioPath, reportPath, bKeepGoing );
        if ( bStarted && pRenderThread != nullptr )
            pRenderThread->setScenarioCaptureEnabled( true );
        // 늘 붙인다 — 게임 · 콘솔의 재생(`ScenarioPlayback::play`)은 기동 뒤 아무 때나 시작한다. 쉬는 세션의 프레임 훅은 아무것도 하지 않는다.
        _automationFrameSubscription = _engine.registerFrameHooks( _pAutomationSession->makeFrameHooks() );
        // 재생기는 호스트 범위에 인터페이스로 건다 — `ScenarioPlayback` 이 범위에서 찾는다(정적 칸을 두지 않는다).
        const bool bPlayerBound = _host.bind<IScenarioPlayer>( _pAutomationSession );
        SW_ASSERT( bPlayerBound );
        (void)bPlayerBound;
    }

    bool App::isAutomationBusy() const
    {
        // 재생(데모 · 벤치마크 경로)은 사람이 보는 화면이라 보통 프레임 속도로 돈다 — 무인 실행만 센다.
        return _pAutomationSession != nullptr && _pAutomationSession->isBusy() && _pAutomationSession->isPlaying() == false;
    }
#else
    void App::startAutomationSession( const CommandLineManager& commandLineManager )
    {
        // 자동화 런타임을 뺀 구성(`SW_ENABLE_AUTOMATION=OFF`)은 시나리오 인자를 조용히 무시하지 않는다 — 폴백 없이 이유를 남기고 시나리오 없이 돈다.
        string     value;
        const bool bAsked = commandLineManager.getArgument( CommandLineArgument::SCENARIO, value ) || commandLineManager.getArgument( CommandLineArgument::SCENARIO_RECORD, value );
        if ( bAsked )
            SW_LOG_ERROR( "-scenario / -scenario-record need a build with SW_ENABLE_AUTOMATION=ON (this build has no automation runtime). Ignored" );
    }

    bool App::isAutomationBusy() const
    {
        return false;
    }
#endif

    void App::bindHostCallbacks()
    {
        // 주 창의 처리기 칸은 주인이 하나다 — 구독을 쥐고, 창(엔진 종료 때 파괴)이 먼저 사라지면 구독이 무효가 된다.
        _windowResizeSubscription  = _pWindow->bindResizeCallback( SW_DELEGATE_METHOD( WindowResizeDelegate, &App::onResize, this ) );
        _windowMessageSubscription = _pWindow->bindCustomMessageHandler( SW_DELEGATE_METHOD( WindowMessageHandlerDelegate, &App::onWindowMessage, this ) );

        // 연결 대상이 프레임마다 바뀌지 않으므로 루프 안이 아니라 여기서 한 번만 연결한다.
        // 에디터 뷰 카메라는 에디터 모드에서만 연결한다. 비어 있다는 사실이 곧 "씬 카메라를 쓴다"는 뜻이므로 루프에서 모드를
        // 나눌 필요가 없다.
        if ( _bEnableEditor == SW_TRUE )
        {
            _sceneViewCameraProvider = SW_DELEGATE_METHOD( ViewCameraProviderDelegate, &App::getSceneViewCamera, this );
            // 에디터 창은 패널 배치가 겹치는 크기 아래로 줄이지 않는다(언리얼 메인 프레임과 유니티 에디터 창도 최소 크기를 둔다).
            _pWindow->setMinimumClientSize( AppInternal::kEditorMinClientWidth, AppInternal::kEditorMinClientHeight );
            // 대화형 에디터 실행에서만 단언을 묻는다(Windows Debug). 자동 실행과 테스트는 기존처럼 멈춘다.
            const CommandLineManager* pCommandLine = _engine.getCommandLineManager();
            if ( pCommandLine != nullptr )
                AppAssertDialog::installForInteractiveRun( *pCommandLine );
        }

        _engine.setPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorRender, this ) );
        _engine.setPostPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorPostPresent, this ) );
#if !defined( SW_SHIPPING )
        _engine.setBeginFrameHook( SW_DELEGATE_METHOD( BeginFrameHookDelegate, &RHIBackendSwitcher::onBeginFrameOnRenderThread, &_backendSwap ) ); // 개발 명령 `rhi.removeDevice <ms>`
#endif
    }

    void App::shutdown()
    {
        // 호스트 서비스를 선 순서의 역순으로 내린다(순서는 `HostService` 의 의존 선언 — 게임에 기대는 콘솔 · 교체기가 게임보다 먼저, 게임이 엔진보다 먼저).
        // 헤드리스 부팅 · 기동 실패면 선 것만 내린다.
        _hostServices.destroyAll();
        // 헤드리스 부팅은 이미지만 올리고 호스트 서비스(게임)을 세우지 않는다 — 이미지를 쥔 모듈 호스트는 엔진(모듈 라이브러리 종료 단계)보다 먼저 놓는다.
        _gameModuleManager.reset();

        // 창은 엔진(RHI) 뒤에 파괴한다 — 소유자(MainWindow 서브시스템)가 RHI 에 기대는 순서로 내린다(제목은 창 안의 고정 버퍼라 누수 보고에 남지 않는다).
        // 시나리오 실행기는 로그 리스너 · 입력을 빌려 쓴다 — 엔진 서비스보다 먼저 떼고 내린다. 호스트 범위는 엔진보다 오래 살아야 하므로(모듈 라이브러리 창구가
        // 엔진 종료 안에서 불린다) 범위째가 아니라 창구 뒤에 서는 구동기만 내린다.
#if SW_AUTOMATION_ENABLED
        _host.unbind<IScenarioPlayer>(); // 재생기(세션)를 내리기 전에 푼다
#endif
        if ( _pAutomationSession != nullptr )
            _pAutomationSession->onDetachedFromEngine();
        _automationFrameSubscription.reset();
#if !defined( SW_SHIPPING )
        _devToolsFrameSubscription.reset(); // 개발 도구는 엔진 서비스를 빌려 쓴다 — 엔진보다 먼저 뗀다
        if ( _pDevTools != nullptr )
            _pDevTools->setEngine( nullptr );
#endif
        if ( _pModuleImageHost != nullptr )
            _host.getCollection().shutdownDependentsOf( *_pModuleImageHost );
        // 엔진이 엔진 범위를 내린 뒤 코어 범위(종료 누수 보고)보다 먼저 호스트 범위를 비운다(창구 · 구동기 소멸, `Engine::shutdown`).
        _engine.shutdown( _host );
        _pModuleImageHost   = nullptr;
        _pAutomationSession = nullptr;
        _pDevTools          = nullptr;

        // 주 창은 엔진 범위의 MainWindow 서브시스템이 소유한다 — 엔진 종료(RHI 가 내려간 뒤)가 파괴했다. 관찰 포인터만 비운다.
        _pWindow = nullptr;
    }

    void App::run()
    {
        // 루프는 창이 있어야 돈다. 헤드리스 부팅(예: --cook-shaders)은 창을 만들지 않으므로 여기서 끝난다. 모드 플래그가 아니라
        // 실제 선행 조건으로 적는다.
        if ( _pWindow == nullptr || _bQuitAfterInitialize == SW_TRUE )
            return;

        // 시작 시간은 `initialize` 첫 줄부터 여기까지의 경과 시간이다. 표준 출력 로그는 에러가 아니면 버퍼에 머물러 있어 밖에서는 시각을 측정할 수 없다.
        [[maybe_unused]] const int64 startupMicro =
            MonotonicClock::nowMicroseconds() - _initializeStartMicro;
        SW_LOG_INFO( "Entering App Main Loop (Thin Launcher)... startup %# ms, %# file opens", startupMicro / constant::kMicrosecondsPerMillisecond,
                     PlatformFileUtil::getFileOpenCount() );

        _fixedTimestep.start();

        // 페이서는 주 창 서비스가 든다 — 창(`_pWindow`)을 그 서비스에서 받았으므로 여기서 없으면 결함이다.
        MainWindowSubsystem* const pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        if ( pWindowHost == nullptr )
        {
            SW_LOG_ERROR( "The engine scope has no MainWindow subsystem - the main loop cannot pace frames" );
            return;
        }
        FramePacer& framePacer       = pWindowHost->getFramePacer();
        uint64      frameNumber      = 0;
        bool        bFrameClockShown = false;
        while ( _pWindow->processMessages() )
        {
            // 리슨 호스트(이 App 의 넷 세션이 서버)이면 줄마다 프레임 번호를 붙인다. 고정 틱이 없어 전용 서버의 `[tick=N]` 과 구분해
            // `[frame=N]` 으로 적는다. 세션이 서버가 아니게 되면 꼬리표를 뗀다.
            const GameNetSession* pNetSession = engine::areEngineServicesBound() ? engine::findSubsystem<GameNetSession>() : nullptr;
            const bool            bListenHost = pNetSession != nullptr && pNetSession->getRole() == GameNetRole::Server;
            if ( bListenHost )
                Logger::setClockNumber( LogClock::Frame, frameNumber );
            else if ( bFrameClockShown )
                Logger::clearClockNumber();
            bFrameClockShown = bListenHost;
            ++frameNumber;

            framePacer.markFrameStart();

            // 프로파일 실행(-gv_profileFrames=N)과 자동화 시나리오(-scenario)는 스스로 끝난다(종료 코드는 `getExitCode`). 부모 프로세스는 표준 입력의 quit 으로 끝낸다(개발 도구 관찰자).
            if ( _engine.isQuitRequested() )
            {
                _pWindow->requestClose();
                break;
            }

            const FrameTime frameTime = _fixedTimestep.advance( engine::getSubsystem<GameTimeScale>().get(), gv_fixedFrameDelta );

            // 사용자 설정의 화면 변경(창 방식, 해상도, VSync)은 OS 리사이즈와 같은 시점인 프레임 시작 전에 처리한다.
            _userSettingsHost.update( frameTime._deltaTime );

            _engine.beginFrame( frameTime._deltaTime, frameTime._unscaledDeltaTime ); // 정지 메뉴와 페이드는 게임이 멈춰도 돈다.
            // 에디터 Play/Pause 상태를 여기서 한 번 고정한다. 아래 고정 스텝이 여러 번 돌아도 DLL 경계를 넘어 다시 묻지 않고,
            // 모든 단계가 같은 답을 본다.
            _gameModuleManager->beginFrame();

            // 셸 액션(리로드 단축키 · 개발 콘솔 키)을 이번 프레임 것으로 갱신한 뒤 읽는다(콘솔은 씬 틱 뒤 개발 도구 관찰자). Shipping 에는 둘 다 없어 셸 InputMap 도 로드하지 않는다.
#if !defined( SW_SHIPPING )
            _engine.updateDebugKeyActions( frameTime._deltaTime );
#endif
            pollReloadHotkeys();

            // **게임 모듈의 시간도 테이블에 올린다.** 이 셋이 계측 밖이면 "frame breakdown" 테이블에 게임 코드가 쓰는 시간이 한 줄도 없어
            // `GT.Frame` 만 보고 "프레임의 전부"라고 읽게 된다.
            {
                SW_PROFILE_SCOPE( "GT.Game.fixedUpdate" );
                for ( uint32 stepIndex = 0; stepIndex < frameTime._fixedStepCount; ++stepIndex )
                {
                    _gameModuleManager->fixedUpdateGame( frameTime._fixedDeltaTime );
                }
            }

            {
                SW_PROFILE_SCOPE( "GT.Game.update" );
                _gameModuleManager->updateGame( frameTime._deltaTime );
            }

            {
                // 에디터가 없으면 바로 돌아온다. 이 호출이 게임 뷰와 씬 뷰 RT, 씬 틱 여부를 확정한다.
                SW_PROFILE_SCOPE( "GT.Editor.updateUI" );
                _gameModuleManager->updateEditorUI( frameTime._deltaTime );
            }

            // 이번 프레임의 페이싱 상태를 정한다 — 에디터가 이번 프레임의 다시 그릴 까닭을 정한 뒤다. 실행 인자(`-gv_framePacing` · 시나리오 · 프로파일 · 리슨 포트)가
            // 고른 상태가 창 · 에디터 상태(최소화 · 유휴 · 포커스)보다 앞선다.
            {
                FramePacingInputs pacingInputs;
                pacingInputs._bAutomationRun = isAutomationBusy() || _engine.isProfileRunActive();
                pacingInputs._bListenHost    = bListenHost;
                pacingInputs._bMinimized     = _pWindow->isMinimized();
                pacingInputs._bFocused       = _pWindow->hasFocus();
                pacingInputs._bEditorQuiet   = _gameModuleManager->getFrameState()._bEditorQuiet == SW_TRUE;
                framePacer.beginFrame( pacingInputs );
            }

            // 카메라 포인터를 미리 잡아 두면 tick 안의 씬 전환이나 핫 리로드가 그 GameObject 를 파괴한 뒤 역참조하게 된다. 그래서
            // 조회 자체를 tick 안으로 넘긴다.
            //
            // 모듈 교체는 **틱 직전**(게임 업데이트 뒤, 씬 틱 앞)에 한다. 여기서 DLL 이 바뀌고 인스턴스가 새로 만들어지기 때문이다.
            _moduleReload.update();

            const ModuleFrameState& frameState = _gameModuleManager->getFrameState();
            _engine.update( frameTime._deltaTime, frameState._views, _sceneViewCameraProvider, frameState._bTickScene == SW_TRUE );
            _gameModuleManager->endEditorFrame();

            // 클라이언트는 씬이 이번 프레임을 마친 뒤 서버가 알린 틱 번호로 해시를 남긴다 — 서버 이력과 같은 틱끼리 비교한다(같은 틱은 한 번만 남는다).
            // 클라이언트 쪽 상태 해시 이력(서버 디버깅 S7)은 `-gv_stateHashHistory=N` 일 때만 켜진다(서비스가 설 때 용량을 정한다).
            StateHashHistory* pStateHashHistory = engine::areEngineServicesBound() ? engine::findSubsystem<StateHashHistory>() : nullptr;
            uint64            serverTick        = 0;
            const bool        bRecordClientHash = pStateHashHistory != nullptr && pStateHashHistory->isRecording() && pNetSession != nullptr && pNetSession->tryGetServerTick( serverTick );
            if ( bRecordClientHash )
                pStateHashHistory->recordActiveScene( serverTick );

            _backendSwap.applyIfPending();

            _engine.endFrame();
            // 배경이면 상한 주기까지, 최소화면 창 메시지가 올 때까지 잔다. 다른 상태는 바로 다음 프레임이다.
            framePacer.endFrame( *_pWindow );
        }
        // 창이 닫혀 끝났으면 자동화 시나리오가 그 결과를 종료 코드로 정한다(시나리오가 이미 끝냈으면 아무 일도 없다).
        _engine.onWindowClosed();
    }

    void App::installRuntimeUIHooks()
    {
        UISystem* pUI = engine::findSubsystem<UISystem>();
        if ( pUI == nullptr )
            return;
        if ( PlatformClipboard::isAvailable() )
        {
            UIClipboardAPI api{};
            api._pRead  = &PlatformClipboard::readText;
            api._pWrite = &PlatformClipboard::writeText;
            engine::getSubsystem<UIClipboardService>().setAPI( api );
        }
        if ( _bEnableEditor == SW_TRUE )
            return; // 에디터는 창을 ImGui 가 쓴다 — 게임 UI 만 스크린 리더에 내보이지 않는다
        // 스크린 리더: 창의 접근성 요청(Windows `WM_GETOBJECT`)에 런타임 UI 로 답한다. 이 플랫폼에 연결이 없으면 nullptr 이고 UI 는 그대로 동작한다.
        if ( _pWindow != nullptr )
            engine::getSubsystem<UIAccessibilityService>().setBridge( UIAccessibilityBridgeFactory::createForWindow( *pUI, *_pWindow ) );
        _quitCommandRegistration = engine::getSubsystem<UIGlobalCommandRegistry>().registerCommand( hashed_string( UIGlobalCommandRegistry::kQuitGame ), SW_DELEGATE_METHOD( UICommandDelegate, &App::onQuitGameCommand, this ) );
    }

    void App::removeRuntimeUIHooks()
    {
        UISystem* pUI = engine::findSubsystem<UISystem>();
        if ( pUI == nullptr )
            return;
        engine::getSubsystem<UIClipboardService>().setAPI( UIClipboardAPI{} );
        engine::getSubsystem<UIAccessibilityService>().setBridge( nullptr ); // 창이 사라지기 전에 창의 접근성 훅을 푼다
        _quitCommandRegistration.reset();
    }

    void App::onQuitGameCommand( [[maybe_unused]] const hashed_string& command, [[maybe_unused]] Widget& source )
    {
        SW_LOG_INFO( "Quit requested from the pause menu" );
        _engine.requestQuit( 0 );
    }

    void App::pollReloadHotkeys()
    {
#if !defined( SW_SHIPPING )
        // 모듈을 다시 로드하는 장치는 호스트(App)에 있다. Engine 에는 "이 액션이 눌렸나"만 묻는다.
        if ( _engine.wasDebugActionTriggered( InputMapDefaults::kReloadGameAction ) )
        {
            _moduleReload.requestReload( config::kTargetGameModule );
            SW_LOG_INFO( "%#: force SWGame reload", InputMapDefaults::kReloadGameAction );
        }
        if ( _bEnableEditor == SW_TRUE && _engine.wasDebugActionTriggered( AppInternal::kReloadEditorAction ) )
        {
            _moduleReload.requestReload( config::kTargetEditorModule );
            SW_LOG_INFO( "%#: force EditorModule reload", AppInternal::kReloadEditorAction );
        }
#endif
        // Shipping 에는 리로드할 모듈이 없다. 그 입력 상태를 묻는 코드가 없으므로 셸 InputMap 도 로드하지 않는다.
    }

    int32 App::getInitFailureExitCode() const
    {
        // 헤드리스 작업이 정한 코드(시나리오 행렬의 시나리오 결과)가 먼저다.
        if ( _engine.isHeadless() && _engine.didHeadlessTaskFail() && _engine.getHeadlessFailureExitCode() != -1 )
            return _engine.getHeadlessFailureExitCode();
        // 환경 탓(백엔드가 이 빌드에 없거나 드라이버가 기능을 주지 않음)은 테스트와 스크립트가 건너뜀으로 읽는 코드로 끝낸다. 그 밖은 결함일 수 있다.
        return AppInternal::isRHIUnusableHere( _engine.getRHIInitResult() ) ? kRHIUnusableHereExitCode : -1;
    }

    int32 App::getExitCode() const
    {
        return _engine.getExitCode();
    }

    void App::onResize( const uint32 width, const uint32 height )
    {
        RHI* pRHI = _engine.getRHI();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
            return;

        // gv_useRenderThread(기본값 true)이면 전용 RenderThread 가 별도 스레드에서 beginFrame/endFrame 으로 스왑체인과 렌더
        // 타깃을 계속 건드리고 있을 수 있다. 이 상태에서 메인(창 메시지) 스레드가 바로 resize() 를 부르면 같은 자원에 대한
        // 실제 스레드 간 레이스가 된다. waitIdle() 로 큐를 비우고 GPU 까지 완전히 쉬게 한 뒤 크기를 바꾼다.
        RenderThread* pRenderThread = _engine.getRenderThread();
        if ( pRenderThread != nullptr )
            pRenderThread->waitIdle( RHIWaitIdleReason::Resize );

        pRHI->getDevice().resize( width, height );
    }

    void App::onConfigReloaded( const hashed_string& configTypeName )
    {
        if ( configTypeName != EngineConfig::StaticType()->_fullyQualifiedName )
            return;
        const ConfigManager* pConfigManager = _engine.getConfigManager();
        const EngineConfig*  pEngineConfig  = pConfigManager != nullptr ? pConfigManager->getConfig<EngineConfig>() : nullptr;
        if ( pEngineConfig != nullptr )
            _fixedTimestep.configure( AppInternal::makeFixedTimestepParams( *pEngineConfig ) );
    }

    bool App::onWindowMessage( const NativeWindowEvent& event )
    {
        // 사람 입력은 버리기 전에 센다(데모 재생의 "손대면 멈춤"). 무인 자동화는 사람의 마우스와 키보드 입력을 여기서 버린다. 에디터(ImGui)와 게임 입력 둘 다에 닿지 않는다.
        if ( engine::areEngineServicesBound() )
        {
            InputManager& inputManager = engine::getSubsystem<InputManager>();
            inputManager.noteNativeEvent( event );
            if ( inputManager.shouldDropNativeEvent( event ) )
                return false;
        }

        // 이벤트를 GameModuleManager(ImGui 등)에 먼저 보낸다.
        const bool bConsumedByEditor = ( _gameModuleManager != nullptr && _gameModuleManager->onWindowMessage( event ) );

        // 에디터가 가로채지 않았을 때만 게임 InputManager 로 전달한다.
        if ( bConsumedByEditor == false && engine::areEngineServicesBound() )
            engine::getSubsystem<InputManager>().processNativeEvent( event );

        // Win32 OS 수준의 포커스와 활성화(DefWindowProc)가 정상적으로 동작하도록 false 를 반환한다.
        return false;
    }

    CameraComponent* App::getSceneViewCamera()
    {
        return _gameModuleManager != nullptr ? _gameModuleManager->getSceneViewCamera() : nullptr;
    }

    void App::onEditorRender( IRHIDevice& renderDevice, const RenderFramePacket& /*framePacket*/ )
    {
        // 이 훅은 렌더 스레드가 부른다. GameModuleManager 가 사라진 뒤에는 불리지 않는다. shutdown 이 GameModuleManager 를 지우기 전에
        // drainRenderWorkers 로 큐를 비우고, 그 시점에는 메인 루프가 이미 끝나 새 프레임이 들어오지 않는다. 훅 델리게이트 자체를
        // 여기서 끊는 것은 오히려 위험하다. RenderThread::setPresentHook 은 잠금 없는 대입이라, 렌더 스레드가 도는 중에 바꾸면
        // 레이스가 된다(실제로 종료할 때 크래시가 났다). 아래 검사는 그 불변 조건이 깨졌을 때 죽는 대신 조용히 넘어가기 위한 것이다.
        if ( _gameModuleManager == nullptr )
            return;
        _gameModuleManager->renderEditor( renderDevice );
    }

    void App::onEditorPostPresent( IRHIDevice& renderDevice, const RenderFramePacket& /*framePacket*/ )
    {
        if ( _gameModuleManager == nullptr )
            return;
        _gameModuleManager->postPresentEditor( renderDevice );
    }
} // namespace sw
