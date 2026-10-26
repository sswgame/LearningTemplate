#include "pch.h"

#include "App/App.h"

#include "App/AppAssertDialog.h"
#include "App/AppHeadlessTasks.h"
#include "App/EditorModuleHost.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/AutomationSession.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/SceneStateHash.h"
#include "Engine/UI/Accessibility/UIAccessibilityBridgeFactory.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"
#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/PlatformClipboard.h"
#include "Engine/Window/SplashWindow.h"

#include "ModuleHost/ModuleCatalogLoader.h"
#include "ModuleHost/ModuleImageHostSubsystem.h"

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

            /** @brief 탐침 `Engine.FramePacingState` — 앱 루프가 이번 프레임에 고른 페이싱 상태 번호입니다. 주 창 서비스가 없으면(헤드리스) 값이 없습니다. */
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

    SW_AUTOMATION_PROBE( engineFramePacingState, "Engine.FramePacingState",
                         "App loop pacing this frame: 0 Foreground, 1 Background (gv_backgroundFrameRate), 2 Minimized, 3 Automation, 4 Server",
                         &AppInternal::readFramePacingState );
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "App" );

    App::App()
        : _host{}
        , _pModuleImageHost{ nullptr }
        , _pAutomationSession{ nullptr }
        , _engine{}
        , _moduleReload{}
        , _devConsole{}
        , _moduleHost{ nullptr }
        , _pWindow{ nullptr }
        , _fixedTimestep{}
        , _backendSwap{}
        , _userSettingsHost{}
        , _sceneViewCameraProvider{}
        , _initializeStartMicro{ 0 }
        , _bEnableEditor{ SW_FALSE }
        , _bQuitAfterInitialize{ SW_FALSE }
        , _bQuitCommandRegistered{ SW_FALSE }
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

        // 0. 호스트 범위를 먼저 세운다 — 엔진 범위의 부모다. 엔진의 `ModuleTypes` 기동 단계가 여기서 모듈 이미지 창구를 찾아 타입 공급자 모듈을 올리고
        //    (이 App 의 `loadModuleImages`), `ModuleImages` 종료 단계가 같은 창구로 모듈 DLL 을 내린다.
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

        // 헤드리스 모드(예: --cook-shaders, --cook-scenes)면 스플래시와 창 UI 를 건너뛴다.
        // 작업이 실패했으면 **초기화 실패로 반환한다.** 그래야 종료 코드가 0 이 아니고, 이 프로세스를 실행하는 빌드(CookAssets 타깃) · `MakePackage.py` 가
        // "쿠킹하지 못했다"는 사실을 알 수 있다.
        if ( _engine.isHeadless() )
        {
            if ( _engine.didHeadlessTaskFail() )
                return false;
            // App 이 맡는 헤드리스 작업(크래시 보고, 로컬라이제이션 도구, 원본 임포트)은 `AppHeadlessTasks` 의 표가 정한다.
            return AppHeadlessTasks::runAll( _engine.getCommandLineManager() );
        }

        SplashWindow splash;
        splash.initialize( "SW Engine", "Initializing Engine Subsystems..." );
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
        pConfigManager->onConfigReloaded().add( SW_DELEGATE_METHOD( Delegate<void( const hashed_string& )>, &App::onConfigReloaded, this ) );

        // 2. 창 소유권을 가져온다(초기화 중에는 숨긴 상태로 시작한다).
        splash.updateStatus( "Initializing Platform Window & Graphics...", 0.60f );
        if ( acquireMainWindow( *pEngineConfig, *pCommandLineManager ) == false )
        {
            splash.dismiss();
            return false;
        }
        [[maybe_unused]] const int64 windowEndMicro = MonotonicClock::nowMicroseconds();

        // 3. ModuleHost(에디터와 게임의 수명 주기)를 연결한다.
        splash.updateStatus( "Loading Modules & Compiling Shaders...", 0.75f );
        if ( startModules() == false )
        {
            splash.dismiss();
            return false;
        }
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
        {
            SW_MEMORY_SCOPE( EngineMisc );
            _pAutomationSession = &_host.addSubsystem( make_unique<AutomationSession>() );
        }
        _pModuleImageHost = ModuleImageHostSubsystem::initializeHostScope( _host, SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &App::loadModuleImages, this ), "App" );
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

    bool App::loadModuleImages()
    {
        // 모듈 호스트, 감시자, 앱 설정은 엔진 기반의 일이다. 모듈 본체의 로드는 ModuleHost 가 에디터와 게임 태그를 걸어 처리한다.
        SW_MEMORY_SCOPE( EngineMisc );
#if !defined( SW_SHIPPING )
        // 무엇을 로드할지는 모듈 매니페스트가 정한다. App 은 빌드가 담은 모든 대상을 로드한다(Game 은 클라이언트와 리슨 서버용 서버 모듈,
        // Client 는 클라이언트). 전용 서버는 Server 실행 파일이 Server 대상으로 같은 해석을 한다.
        if ( ModuleCatalogLoader::loadAndResolve( ModuleCatalog::getBuildTargetMask(), _moduleCatalog, _moduleResolution ) == false )
            return false;

        // 모듈 감시자는 모듈을 로드하는 ModuleHost 보다 먼저 있어야 한다. ModuleHost 가 이 포인터로 모듈을 로드하고 리로드 콜백을 등록한다.
        // 구동기는 자신을 호스트 범위에 건다(개발 명령 `module.reload` · 탐침 `Module.*` 가 찾는다).
        _moduleReload.initialize( _host, *_pModuleImageHost );
#endif

        _moduleHost = make_unique<EditorModuleHost>();
        return _moduleHost->loadModuleImages( _moduleReload.getLiveReloadManager(), _moduleCatalog, _moduleResolution );
    }

    bool App::startModules()
    {
        if ( _moduleHost == nullptr )
            _moduleHost = make_unique<EditorModuleHost>();
        if ( _moduleHost->initialize( _moduleReload.getLiveReloadManager(),
                                      _engine.getRHI(),
                                      _pWindow,
                                      _engine.getRenderThread(),
                                      _bEnableEditor == SW_TRUE ) == false )
        {
            SW_LOG_ERROR( "ModuleHost initialization failed." );
            return false;
        }

        return true;
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
            bScenario && _pAutomationSession->begin( scenarioPath, reportPath, bKeepGoing, pRenderThread != nullptr ? pRenderThread->getCompletedScenarioScreenshotCount() : 0u );
        if ( bStarted && pRenderThread != nullptr )
            pRenderThread->setScenarioCaptureEnabled( true );
        if ( _pAutomationSession->isBusy() )
            _engine.registerFrameObserver( _pAutomationSession );
    }

    void App::bindHostCallbacks()
    {
        _pWindow->setResizeCallback( SW_DELEGATE_METHOD( WindowResizeDelegate, &App::onResize, this ) );
        _pWindow->setCustomMessageHandler( SW_DELEGATE_METHOD( WindowMessageHandlerDelegate, &App::onWindowMessage, this ) );

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

        _backendSwap.initialize( &_engine, _moduleHost.get(), _bEnableEditor == SW_TRUE );
        // 시나리오 단계 `SwapBackend` 와 탐침 `RHI.*` 가 `engine::findSubsystem<RHIBackendSwitcher>()` 로 찾는다(엔진 범위 → 호스트 범위).
        const bool bSwitcherBound = _host.bind( &_backendSwap );
        SW_ASSERT( bSwitcherBound );
        (void)bSwitcherBound;
        _userSettingsHost.initialize( &_engine, _pWindow );
        installRuntimeUIHooks();

        _engine.setPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorRender, this ) );
        _engine.setPostPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorPostPresent, this ) );
    }

    void App::shutdown()
    {
        // 전역 변수 훅은 그 변수를 소유한 GlobalVariableManager(Engine 소유)가 사라지기 전에 해제한다. 헤드리스 부팅처럼
        // 연결되지 않은 경우에는 아무것도 하지 않는다. 교체기는 범위에서 먼저 풀어 자동화가 내려간 교체기를 찾지 않게 한다.
        _host.unbind<RHIBackendSwitcher>();
        _backendSwap.shutdown();
        _userSettingsHost.shutdown();
        removeRuntimeUIHooks();

        // 헤드리스 부팅은 창도 ModuleHost 도 만들지 않는다. 아래 경로가 그대로 아무 일도 하지 않으므로 모드 분기를 따로 두지 않는다.
        // 주의: ModuleHost 를 Engine 보다 먼저 종료해야 한다. 에디터 shutdown 이 씬 뷰와 게임 뷰 RT 를 해제할 때 RenderThread 와
        // RHI 디바이스를 쓰기 때문이다.
        BLOCK( "Game / Editor 인스턴스 정리" )
        {
            if ( _moduleHost != nullptr )
            {
                _moduleHost->shutdown();
                _moduleHost.reset();
            }
        }

        // 콘솔은 엔진(입력 관리자)과 게임 창보다 먼저 내린다(`DevConsoleHost::shutdown`).
        _devConsole.shutdown();
        // 창은 엔진(RHI) 뒤에 파괴한다 — 소유자(MainWindow 서브시스템)가 RHI 에 기대는 순서로 내린다(제목은 창 안의 고정 버퍼라 누수 보고에 남지 않는다).
        // 시나리오 실행기는 로그 리스너 · 입력을 빌려 쓴다 — 엔진 서비스보다 먼저 떼고 내린다. 호스트 범위는 엔진보다 오래 살아야 하므로(모듈 이미지 창구가
        // 엔진 종료 안에서 불린다) 범위째가 아니라 창구 뒤에 서는 구동기만 내린다.
        if ( _pAutomationSession != nullptr )
            _engine.unregisterFrameObserver( _pAutomationSession );
        if ( _pModuleImageHost != nullptr )
            _host.getCollection().shutdownDependentsOf( *_pModuleImageHost );
        _engine.shutdown();
        // 엔진 범위가 사라졌다 — 이제 호스트 범위를 내린다(창구 · 구동기 소멸).
        _host.destroyAll();
        _pModuleImageHost   = nullptr;
        _pAutomationSession = nullptr;

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
        _devConsole.initialize( _pWindow, _bEnableEditor == SW_TRUE );

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
            // `[frame=N]` 으로 적는다(결정 기록 5-12 `D7-3.6`). 세션이 서버가 아니게 되면 꼬리표를 뗀다.
            const GameNetSession* pNetSession = engine::areEngineServicesBound() ? engine::findSubsystem<GameNetSession>() : nullptr;
            const bool            bListenHost = pNetSession != nullptr && pNetSession->getRole() == GameNetRole::Server;
            if ( bListenHost )
                Logger::setClockNumber( LogClock::Frame, frameNumber );
            else if ( bFrameClockShown )
                Logger::clearClockNumber();
            bFrameClockShown = bListenHost;
            ++frameNumber;

            // 이번 프레임의 페이싱 상태를 정한다 — 실행 인자(시나리오 · 프로파일 · 리슨 포트)가 고른 상태가 창 상태(최소화 · 포커스)보다 앞선다.
            FramePacingInputs pacingInputs;
            pacingInputs._bAutomationRun = _pAutomationSession->isBusy() || _engine.isProfileRunActive();
            pacingInputs._bListenHost    = bListenHost;
            pacingInputs._bMinimized     = _pWindow->isMinimized();
            pacingInputs._bFocused       = _pWindow->hasFocus();
            framePacer.beginFrame( pacingInputs );

            _devConsole.runPendingExec();
            // 프로파일 실행(-gv_profileFrames=N)과 자동화 시나리오(-scenario)는 스스로 끝난다(종료 코드는 `getExitCode`). 부모 프로세스는 표준 입력의 quit 으로 끝낸다.
            if ( _engine.isQuitRequested() || _devConsole.pollStandardInputCommands() )
            {
                _pWindow->requestClose();
                break;
            }

            const FrameTime frameTime = _fixedTimestep.advance( GameTimeScale::get(), gv_fixedFrameDelta );

            // 사용자 설정의 화면 변경(창 방식, 해상도, VSync)은 OS 리사이즈와 같은 시점인 프레임 시작 전에 처리한다.
            _userSettingsHost.update( frameTime._deltaTime );

            _engine.beginFrame( frameTime._deltaTime, frameTime._unscaledDeltaTime ); // 정지 메뉴와 페이드는 게임이 멈춰도 돈다.
            // 에디터 Play/Pause 상태를 여기서 한 번 고정한다. 아래 고정 스텝이 여러 번 돌아도 DLL 경계를 넘어 다시 묻지 않고,
            // 모든 단계가 같은 답을 본다.
            _moduleHost->beginFrame();

            // 셸 액션(리로드 단축키 · 개발 콘솔 키)을 이번 프레임 것으로 갱신한 뒤 둘이 읽는다. Shipping 에는 둘 다 없어 셸 InputMap 도 로드하지 않는다.
#if !defined( SW_SHIPPING )
            _engine.updateShellActions( frameTime._deltaTime );
#endif
            pollReloadHotkeys();
            _devConsole.update( _engine.getShellInputMap() );

            // **게임 모듈의 시간도 테이블에 올린다.** 이 셋이 계측 밖이면 "frame breakdown" 테이블에 게임 코드가 쓰는 시간이 한 줄도 없어
            // `GT.Frame` 만 보고 "프레임의 전부"라고 읽게 된다.
            {
                SW_PROFILE_SCOPE( "GT.Game.fixedUpdate" );
                for ( uint32 stepIndex = 0; stepIndex < frameTime._fixedStepCount; ++stepIndex )
                {
                    _moduleHost->fixedUpdateGame( frameTime._fixedDeltaTime );
                }
            }

            {
                SW_PROFILE_SCOPE( "GT.Game.update" );
                _moduleHost->updateGame( frameTime._deltaTime );
            }

            {
                // 에디터가 없으면 바로 돌아온다. 이 호출이 게임 뷰와 씬 뷰 RT, 씬 틱 여부를 확정한다.
                SW_PROFILE_SCOPE( "GT.Editor.updateUI" );
                _moduleHost->updateEditorUI( frameTime._deltaTime );
            }

            // 카메라 포인터를 미리 잡아 두면 tick 안의 씬 전환이나 핫 리로드가 그 GameObject 를 파괴한 뒤 역참조하게 된다. 그래서
            // 조회 자체를 tick 안으로 넘긴다.
            //
            // 모듈 교체는 **틱 직전**(게임 업데이트 뒤, 씬 틱 앞)에 한다. 여기서 DLL 이 바뀌고 인스턴스가 새로 만들어지기 때문이다.
            _moduleReload.update();

            const ModuleFrameState& frameState = _moduleHost->getFrameState();
            _engine.update( frameTime._deltaTime, frameState._views, _sceneViewCameraProvider, frameState._bTickScene == SW_TRUE );
            _moduleHost->endEditorFrame();

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
            pUI->getClipboard().setAPI( api );
        }
        if ( _bEnableEditor == SW_TRUE )
            return; // 에디터는 창을 ImGui 가 쓴다 — 게임 UI 만 스크린 리더에 내보이지 않는다
        // 스크린 리더: 창의 접근성 요청(Windows `WM_GETOBJECT`)에 런타임 UI 로 답한다. 이 플랫폼에 연결이 없으면 nullptr 이고 UI 는 그대로 동작한다.
        if ( _pWindow != nullptr )
            pUI->setAccessibilityBridge( UIAccessibilityBridgeFactory::createForWindow( *pUI, *_pWindow ) );
        pUI->getGlobalCommands().registerCommand( hashed_string( UIGlobalCommandRegistry::kQuitGame ), SW_DELEGATE_METHOD( UICommandDelegate, &App::onQuitGameCommand, this ) );
        _bQuitCommandRegistered = SW_TRUE;
    }

    void App::removeRuntimeUIHooks()
    {
        UISystem* pUI = engine::findSubsystem<UISystem>();
        if ( pUI == nullptr )
            return;
        pUI->getClipboard().setAPI( UIClipboardAPI{} );
        pUI->setAccessibilityBridge( nullptr ); // 창이 사라지기 전에 창의 접근성 훅을 푼다
        if ( _bQuitCommandRegistered == SW_FALSE )
            return;
        pUI->getGlobalCommands().unregisterCommand( hashed_string( UIGlobalCommandRegistry::kQuitGame ), SW_DELEGATE_METHOD( UICommandDelegate, &App::onQuitGameCommand, this ) );
        _bQuitCommandRegistered = SW_FALSE;
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
            pRenderThread->waitIdle();

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
        // 무인 자동화는 사람의 마우스와 키보드 입력을 여기서 버린다. 에디터(ImGui)와 게임 입력 둘 다에 닿지 않는다. 창 이벤트는 그대로 지나간다.
        if ( engine::areEngineServicesBound() && engine::getSubsystem<InputManager>().shouldDropNativeEvent( event ) )
            return false;

        // 이벤트를 ModuleHost(ImGui 등)에 먼저 보낸다.
        const bool bConsumedByEditor = ( _moduleHost != nullptr && _moduleHost->onWindowMessage( event ) );

        // 에디터가 가로채지 않았을 때만 게임 InputManager 로 전달한다.
        if ( bConsumedByEditor == false && engine::areEngineServicesBound() )
            engine::getSubsystem<InputManager>().processNativeEvent( event );

        // Win32 OS 수준의 포커스와 활성화(DefWindowProc)가 정상적으로 동작하도록 false 를 반환한다.
        return false;
    }

    CameraComponent* App::getSceneViewCamera()
    {
        return _moduleHost != nullptr ? _moduleHost->getSceneViewCamera() : nullptr;
    }

    void App::onEditorRender( IRHIDevice& renderDevice, const RenderFramePacket& /*framePacket*/ )
    {
        // 이 훅은 렌더 스레드가 부른다. ModuleHost 가 사라진 뒤에는 불리지 않는다. shutdown 이 ModuleHost 를 지우기 전에
        // drainRenderWorkers 로 큐를 비우고, 그 시점에는 메인 루프가 이미 끝나 새 프레임이 들어오지 않는다. 훅 델리게이트 자체를
        // 여기서 끊는 것은 오히려 위험하다. RenderThread::setPresentHook 은 잠금 없는 대입이라, 렌더 스레드가 도는 중에 바꾸면
        // 레이스가 된다(실제로 종료할 때 크래시가 났다). 아래 검사는 그 불변 조건이 깨졌을 때 죽는 대신 조용히 넘어가기 위한 것이다.
        if ( _moduleHost == nullptr )
            return;
        _moduleHost->renderEditor( renderDevice );
    }

    void App::onEditorPostPresent( IRHIDevice& renderDevice, const RenderFramePacket& /*framePacket*/ )
    {
        if ( _moduleHost == nullptr )
            return;
        _moduleHost->postPresentEditor( renderDevice );
    }
} // namespace sw
