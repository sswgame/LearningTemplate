#include "pch.h"

#include "App/App.h"

#include "App/AppHeadlessTasks.h"
#include "App/Dev/InteractiveAssertDialog.h"
#include "App/EditorModuleManager.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/AutomationRuntime.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Module/HostInterfaces.h"
#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"
#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/SplashSubsystem.h"
#include "Engine/Window/SplashWindow.h"

#include "ModuleManager/Host/HostDevLayer.h"
#include "ModuleManager/ModuleLoaderSubsystem.h"

namespace sw
{
    namespace
    {
        struct AppInternal
        {
            /** @brief 에디터 창의 최소 클라이언트 크기입니다. 메뉴바, 도크 다섯 개, 게임 뷰 툴바가 겹치지 않는 하한입니다. */
            static constexpr uint32 kEditorMinClientWidth  = 960;
            static constexpr uint32 kEditorMinClientHeight = 540;

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

            /** @brief 표면이 없거나 앱이 멈춘 동안 메시지를 보는 주기입니다(잠들어도 수명 이벤트를 곧 받는다). */
            static constexpr int64 kPausedTickNanoseconds = 10 * 1000 * 1000;

            /** @brief 감시 값 `Window.RenderPausedTicks` — 표면을 잃거나 앱이 멈춰 프레임을 돌지 않은 메인 루프 틱의 누적입니다. 주 창이 없으면(헤드리스) 값이 없습니다. */
            [[nodiscard]] static bool readWindowRenderPausedTicks( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                const IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                if ( pWindow == nullptr )
                    return false;
                outValue = static_cast<float64>( pWindow->getRenderPausedTickCount() );
                return true;
            }

            /** @brief 감시 값 `Window.Minimized` — 주 창이 최소화돼 있으면 1 입니다(플랫폼 통보 기준). 주 창이 없으면(헤드리스) 값이 없습니다. */
            [[nodiscard]] static bool readWindowMinimized( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
                const IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                if ( pWindow == nullptr )
                    return false;
                outValue = pWindow->isMinimized() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( engineFramePacingState, "Engine.FramePacingState",
                         "App loop pacing this frame: 0 Foreground, 1 Background (gv_backgroundFrameRate), 2 Minimized, 3 Automation, 4 Server, 5 FullSpeed (gv_framePacing), 6 EditorIdle",
                         &AppInternal::readFramePacingState );
    SW_AUTOMATION_WATCH( windowRenderPausedTicks, "Window.RenderPausedTicks", "Main loop ticks that ran no frame because the surface was lost or the app was suspended", &AppInternal::readWindowRenderPausedTicks );
    SW_AUTOMATION_WATCH( windowMinimized, "Window.Minimized", "1 while the main window is minimized (as the platform reported it)", &AppInternal::readWindowMinimized );
} // namespace sw

namespace sw
{
    App::App()
        : _launch{}
        , _pAutomation{ nullptr }
        , _pWindow{ nullptr }
        , _fixedTimestep{}
        , _pBackendSwitcher{}
        , _userSettingsHost{}
        , _uiPlatformBinding{}
        , _sceneViewCameraProvider{}
        , _configReloadedSubscription{}
        , _windowMessageSubscription{}
        , _pFramePacer{ nullptr }
        , _initializeStartMicro{ 0 }
        , _mainLoopStartMicro{ 0 }
        , _startupFrameCount{ 0 }
        , _bEnableEditor{ SW_FALSE }
        , _bQuitAfterInitialize{ SW_FALSE }
        , _bRenderPaused{ SW_FALSE }
        , _bShowWindowPending{ SW_FALSE }
        , _bActiveSceneSeen{ SW_FALSE }
        , _reserved{ 0 }
    {
        // 백엔드 교체기는 개발 구성에만 있다(`App/Dev/` — 배포 구성은 짓지 않는다). 렌더 스레드 훅이 호스트 서비스보다 먼저 묶으므로 여기서 만든다.
        if constexpr ( build::kShipping == false )
            _pBackendSwitcher = make_unique<RHIBackendSwitcher>();
    }

    App::~App() = default;

    EditorModuleManager* App::getModuleManager() const
    {
        return static_cast<EditorModuleManager*>( _launch.getModuleManager() ); // 만든 쪽이 형식을 안다(`loadModuleLibraries`)
    }

    // 길이를 나누지 않는다: 호스트 범위 → 엔진 → 헤드리스 작업 → 자동화 → 설정 감시 순의 시동 열이라 순서가 곧 뜻이다.
    // `Engine::initialize` 와 같은 이유다.
    bool App::initialize( int32 argc, utf8* pArgv[] )
    {
        _initializeStartMicro = MonotonicClock::nowMicroseconds();
        // 리소스 루트 탐색은 Engine 가 로거를 시작한 **뒤에** 한다. 여기서 먼저 부르면 실패했을 때 로거가 없어 진단 메시지가
        // 사라지고, 반환값도 여기서는 쓸 곳이 없다.

        // 0. 호스트 범위를 먼저 세운다 — 엔진 범위의 부모다. 엔진의 `ModuleLoadStage` 기동 단계가 여기서 모듈 라이브러리 창구를 찾아 타입 공급자 모듈을 올리고
        //    (이 App 의 `loadModuleLibraries`), `ModuleUnloadStage` 종료 단계가 같은 창구로 모듈 DLL 을 내린다.
        if ( _launch.initializeHostScope( SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &App::loadModuleLibraries, this ), "App" ) == false )
            return false;
        // 시동 단계별 시간(측정만 한다 — 시동 요약 로그의 재료).
        StartupTimings timings{};
        timings._hostScopeEndMicro = MonotonicClock::nowMicroseconds();

        // 1. 코어 매니저는 모두 Engine 가 초기화한다(헤드리스 작업 처리 포함).
        const HostEngineStart engineStart = _launch.initializeEngine( argc, pArgv, EngineHostRole::Client );
        if ( engineStart == HostEngineStart::Failed )
            return false;
        timings._engineEndMicro = MonotonicClock::nowMicroseconds();
        // 헤드리스 모드(예: --cook-shaders, --cook-scenes)면 스플래시와 창 UI 를 건너뛴다.
        // App 이 맡는 헤드리스 작업(크래시 보고, 로컬라이제이션 도구, 원본 임포트)은 `AppHeadlessTasks` 의 표가 정한다.
        if ( engineStart == HostEngineStart::Headless )
            return AppHeadlessTasks::runAll( _launch.getEngine().getCommandLineManager() );

        // 무인 실행(-unattended)은 보는 사람이 없다 — 스플래시도 메인 창도 띄우지 않고 그리기 · 캡처만 한다(언리얼 -RenderOffScreen). 화면 표시를 보는 시나리오
        // (present="true")는 그 단계에서 자동화 세션이 메인 창을 띄운다(`AutomationSession::applyPresentPolicy`).
        // 스플래시는 엔진 기동이 에셋 직후 띄웠다(`SplashSubsystem`) — 여기서는 남은 단계의 진행을 적고 닫는다.
        SplashWindow           noSplash;
        SplashSubsystem* const pSplashHost = engine::findSubsystem<SplashSubsystem>();
        SplashWindow&          splash      = pSplashHost != nullptr ? pSplashHost->getSplash() : noSplash; // 렌더러 없는 구성은 스플래시 단계가 없다

        splash.updateStatus( "Loading Configuration & Display...", 0.40f );

        ConfigManager*            pConfigManager      = _launch.getEngine().getConfigManager();
        const CommandLineManager* pCommandLineManager = _launch.getEngine().getCommandLineManager();
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
        if ( startAutomation( *pCommandLineManager ) == false )
        {
            splash.dismiss();
            return false;
        }

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
        timings._windowEndMicro = MonotonicClock::nowMicroseconds();

        // 3. 호스트 서비스(게임 · 에디터 인스턴스, 교체기, 사용자 설정, 런타임 UI 훅, 개발 콘솔)을 의존 순서로 세운다. 내리는 것은 `shutdown` 의 역순 한 줄이다.
        splash.updateStatus( "Loading Modules & Compiling Shaders...", 0.75f );
        if ( _launch.startHostServices( kArrClientHostService, SW_DELEGATE_METHOD( HostServiceStartDelegate, &App::startHostService, this ),
                                        SW_DELEGATE_METHOD( HostServiceStopDelegate, &App::stopHostService, this ) ) == false )
        {
            splash.dismiss();
            return false;
        }
        timings._modulesEndMicro = MonotonicClock::nowMicroseconds();

        // 모듈이 선 뒤 한 번 하고 끝나는 작업(초상화 굽기)은 `AppHeadlessTasks` 의 표가 정한다. 실행했으면 창을 띄우지 않고 끝낸다.
        const AppHeadlessTasks::TaskResult afterServices = AppHeadlessTasks::runAfterHostServices( *pCommandLineManager, _launch.getEngine() );
        if ( afterServices != AppHeadlessTasks::TaskResult::NotRequested )
        {
            splash.dismiss();
            _bQuitAfterInitialize = SW_TRUE;
            return afterServices != AppHeadlessTasks::TaskResult::Failed;
        }

        // 4. 창 콜백과 이벤트 전달을 설정한다.
        splash.updateStatus( "Finalizing Setup...", 0.95f );
        bindHostCallbacks();

        // 보는 사람이 있으면(무인이 아니거나, 무인이어도 지켜보는 표시가 있으면) 창을 띄운다 — `MainWindowSubsystem::shouldRenderOffScreen`.
        // 띄우는 것은 첫 그림이 나온 뒤다(`onStartupFrameEnded`): 첫 프레임은 시작 씬을 받느라 길어(Debug 수 초) 그동안 빈 흰 창이 보였다. 그 사이는 스플래시가 진행을 보인다
        // (언리얼도 첫 맵이 설 때까지 스플래시를 둔다).
        if ( MainWindowSubsystem::shouldRenderOffScreen( *_launch.getEngine().getCommandLineManager() ) == false )
        {
            splash.updateStatus( "Loading First Scene...", 1.0f );
            _bShowWindowPending = SW_TRUE;
        }
        else
            splash.dismiss();

        logStartupPhases( timings );

        return true;
    }

    bool App::startAutomation( const CommandLineManager& commandLineManager )
    {
        _pAutomation = engine::findSubsystem<IAutomationRuntime>();
        if ( _pAutomation != nullptr )
            return _pAutomation->start( _launch.getEngine(), _launch.getHostScope(), commandLineManager );
        string     value;
        const bool bAsked = commandLineManager.getArgument( CommandLineArgument::SCENARIO, value ) || commandLineManager.getArgument( CommandLineArgument::SCENARIO_RECORD, value );
        if ( bAsked )
            SW_LOG_ERROR_ID( "App.ScenarioScenarioRecordNeed", "-scenario / -scenario-record need a development build (Debug or Release) - Shipping has no automation runner" );
        return bAsked == false;
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
            SW_LOG_ERROR_ID( "App.EngineScopeNoMainWindow", "The engine scope has no MainWindow subsystem!" );
            return false;
        }
        if ( pWindowHost->getWindow() == nullptr )
        {
            unique_ptr<IWindow> window = IWindow::createMainWindow( engine::getSubsystem<PackEngineSettings>()._windowTitle.c_str(), width, height, commandLineManager );
            if ( window == nullptr )
            {
                SW_LOG_ERROR_ID( "App.FailedCreatePlatformWindow", "Failed to create platform window!" );
                return false;
            }
            window->showWindow( false );
            if ( pWindowHost->attach( window ) == false )
            {
                SW_LOG_ERROR_ID( "App.MainWindowOwnerRefused", "The main window owner refused the platform window!" );
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

    bool App::loadModuleLibraries()
    {
        // 모듈 호스트, 감시자, 앱 설정은 엔진 기반의 일이다. 모듈 본체의 로드는 GameModuleManager 가 에디터와 게임 태그를 걸어 처리한다.
        SW_MEMORY_SCOPE( EngineMisc );
        // App 은 빌드가 담은 모든 대상을 로드한다(Game 은 클라이언트와 리슨 서버용 서버 모듈, Client 는 클라이언트). 전용 서버는 Server 실행 파일이 Server 대상으로 같은 해석을 한다.
        return _launch.loadModuleLibraries( ModuleCatalog::getBuildTargetMask(), make_unique<EditorModuleManager>() );
    }

    bool App::startHostService( HostServiceID id )
    {
        const bool bEnableEditor = _bEnableEditor == SW_TRUE;
        switch ( id )
        {
            case HostServiceID::Game:
            {
                const bool bStarted = getModuleManager()->initialize( _launch.getLiveReloadManager(), _launch.getEngine().getRHI(), _pWindow, _launch.getEngine().getRenderDriver(), bEnableEditor );
                if ( bStarted == false )
                {
                    SW_LOG_ERROR_ID( "App.GameModuleManagerInitializationFailed", "GameModuleManager initialization failed." );
                    return false;
                }
                // 에디터 렌더 훅은 모듈 매니저가 사는 동안만 걸린다(떼는 것은 stopHostService — 렌더 스레드가 사라진 매니저를 부르지 않는다).
                bindEditorRenderHooks( true );
                // 시나리오 체인이 단계마다 게임을 처음부터 다시 세운다(`AutomationSession::startStage` 가 `engine::findSubsystem<IGameRestarter>()` 로 찾는다).
                return _launch.getHostScope().bind<IGameRestarter>( _launch.getModuleManager() );
            }
            case HostServiceID::BackendSwitcher:
            {
                // 실행 중 백엔드 교체는 개발 기능이다(App/Dev — 언리얼도 RHI 변경은 재시작). 배포는 교체기가 없어 설 것이 없다.
                if constexpr ( build::kShipping )
                    return true;
                else
                {
                    _pBackendSwitcher->initialize( &_launch.getEngine(), getModuleManager(), bEnableEditor );
                    // 시나리오 단계 `SwapBackend` 와 감시 값 `RHI.*` 가 `engine::findSubsystem<RHIBackendSwitcher>()` 로 찾는다(엔진 범위 → 호스트 범위).
                    return _launch.getHostScope().bind( _pBackendSwitcher.get() );
                }
            }
            case HostServiceID::UserSettings:
            {
                _userSettingsHost.initialize( _launch.getEngine().getRenderDriver(), _pWindow );
                return true;
            }
            case HostServiceID::RuntimeUIHooks:
            {
                _uiPlatformBinding.install( _pWindow, bEnableEditor );
                return true;
            }
            case HostServiceID::DevConsole:
            {
                if ( _launch.getDevLayer() != nullptr )
                    _launch.getDevLayer()->startConsole( bEnableEditor );
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
        SW_LOG_ERROR_ID( "App.HostServiceNotClient", "Host service %# is not a client service", HostService::getName( id ) );
        return false;
    }

    void App::stopHostService( HostServiceID id )
    {
        switch ( id )
        {
            case HostServiceID::Game:
            {
                _launch.getHostScope().unbind<IGameRestarter>();
                // 에디터 shutdown 이 씬 뷰 · 게임 뷰 RT 를 놓을 때 렌더 스레드와 RHI 디바이스를 쓴다 — 호스트 서비스는 엔진 종료 앞에서 내려간다.
                getModuleManager()->shutdown();
                // 렌더 스레드가 받은 패킷을 끝낸 뒤 훅을 뗀다(`RenderThread::setPresentHook`) — 그 뒤에 매니저를 지운다.
                bindEditorRenderHooks( false );
                _launch.releaseModuleManager();
                return;
            }
            case HostServiceID::BackendSwitcher:
            {
                // 범위에서 먼저 풀어 자동화가 내려간 교체기를 찾지 않게 한다. 전역 변수 훅은 GlobalVariableManager(엔진 소유)가 사라지기 전에 뗀다.
                if constexpr ( build::kShipping == false )
                {
                    _launch.getHostScope().unbind<RHIBackendSwitcher>();
                    _pBackendSwitcher->shutdown();
                }
                return;
            }
            case HostServiceID::UserSettings:
            {
                _userSettingsHost.shutdown();
                return;
            }
            case HostServiceID::RuntimeUIHooks:
            {
                _uiPlatformBinding.remove();
                return;
            }
            case HostServiceID::DevConsole:
            {
                if ( _launch.getDevLayer() != nullptr )
                    _launch.getDevLayer()->stopConsole();
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

    void App::bindHostCallbacks()
    {
        // 주 창의 처리기 칸은 주인이 하나다 — 구독을 쥐고, 창(엔진 종료 때 파괴)이 먼저 사라지면 구독이 무효가 된다.
        _windowMessageSubscription = _pWindow->bindCustomMessageHandler( SW_DELEGATE_METHOD( WindowMessageHandlerDelegate, &App::onWindowMessage, this ) );

        // 연결 대상이 프레임마다 바뀌지 않으므로 루프 안이 아니라 여기서 한 번만 연결한다.
        // 에디터 뷰 카메라는 에디터 모드에서만 연결한다. 비어 있다는 사실이 곧 "씬 카메라를 쓴다"는 뜻이므로 루프에서 모드를
        // 나눌 필요가 없다.
        if ( _bEnableEditor == SW_TRUE )
        {
            // 모듈 매니저는 루프가 도는 동안 산다(호스트 서비스 Game 이 세우고 루프 뒤 `shutdown` 이 내린다) — App 이 중계하지 않고 바로 묶는다.
            _sceneViewCameraProvider = SW_DELEGATE_METHOD( ViewCameraProviderDelegate, &EditorModuleManager::getSceneViewCamera, static_cast<const EditorModuleManager*>( getModuleManager() ) );
            // 에디터 창은 패널 배치가 겹치는 크기 아래로 줄이지 않는다(언리얼 메인 프레임과 유니티 에디터 창도 최소 크기를 둔다).
            _pWindow->setMinimumClientSize( AppInternal::kEditorMinClientWidth, AppInternal::kEditorMinClientHeight );
            // 대화형 에디터 실행에서만 단언을 묻는다(Windows Debug). 자동 실행과 테스트는 기존처럼 멈춘다. 대화상자는 개발 도구다(배포는 DevTools 를 링크하지 않는다).
            if constexpr ( build::kShipping == false )
            {
                const CommandLineManager* pCommandLine = _launch.getEngine().getCommandLineManager();
                if ( pCommandLine != nullptr )
                    InteractiveAssertDialog::install( *pCommandLine );
            }
        }

        // 개발 도구의 프레임 시작 훅은 렌더 스레드에 바로 건다(엔진은 렌더 스레드를 빌려 줄 뿐 훅을 중계하지 않는다). 헤드리스가 아닌 클라이언트에는 늘 있다.
        // 에디터 렌더 훅은 게임 호스트 서비스와 함께 걸고 뗀다(`bindEditorRenderHooks`). 백엔드 교체기는 개발 구성에만 있다(`App/Dev/`).
        if constexpr ( build::kShipping == false )
        {
            if ( RenderThread* const pRenderThread = _launch.getEngine().getRenderThread() )
                _pBackendSwitcher->bindRenderThreadHook( *pRenderThread );
        }
    }

    void App::bindEditorRenderHooks( bool bBind )
    {
        RenderThread* const pRenderThread = _launch.getEngine().getRenderThread();
        if ( pRenderThread == nullptr ) // 헤드리스 호스트 — 그릴 렌더 스레드가 없다
            return;
        // 세터가 렌더 스레드를 비운 뒤 바꾼다 — 렌더 스레드가 훅을 부르는 중에 덮이거나, 뗀 뒤에 모듈 매니저를 부르는 일이 없다.
        pRenderThread->setPrepareHook( bBind ? SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorPrepareRender, this ) : PresentHookDelegate{} );
        pRenderThread->setPresentHook( bBind ? SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorRender, this ) : PresentHookDelegate{} );
        pRenderThread->setPostPresentHook( bBind ? SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorPostPresent, this ) : PresentHookDelegate{} );
    }

    void App::shutdown()
    {
        // 호스트 서비스를 선 순서의 역순으로 내린다(순서는 `HostService` 의 의존 선언 — 게임에 기대는 콘솔 · 교체기가 게임보다 먼저, 게임이 엔진보다 먼저).
        _launch.stopHostServices();

        // 창은 엔진(RHI) 뒤에 파괴한다 — 소유자(MainWindow 서브시스템)가 RHI 에 기대는 순서로 내린다(제목은 창 안의 고정 버퍼라 누수 보고에 남지 않는다).
        // 자동화 실행기는 엔진 범위와 함께 내려간다(스스로 프레임 · 호스트 범위에서 뗀다) — 빌린 포인터만 놓는다.
        _pAutomation = nullptr;
        _launch.shutdownEngine();

        // 주 창은 엔진 범위의 MainWindow 서브시스템이 소유한다 — 엔진 종료(RHI 가 내려간 뒤)가 파괴했다. 관찰 포인터만 비운다.
        _pWindow = nullptr;
    }

    void App::logMainLoopStart() const
    {
        // 시작 시간은 `initialize` 첫 줄부터 여기까지의 경과 시간이다. 표준 출력 로그는 에러가 아니면 버퍼에 머물러 있어 밖에서는 시각을 측정할 수 없다.
        [[maybe_unused]] const int64 startupMicro =
            MonotonicClock::nowMicroseconds() - _initializeStartMicro;
        SW_LOG_INFO_ID( "App.EnteringAppMainLoop", "Entering App Main Loop (Thin Launcher)... startup %# ms, %# file opens", startupMicro / constant::time::kMicrosecondsPerMillisecond,
                        PlatformFileUtil::getFileOpenCount() );
    }

    void App::logStartupPhases( [[maybe_unused]] const StartupTimings& timings ) const
    {
        // 시동 요약 한 줄 — 단계별 ms 와 오래 걸린 서브시스템(`Test/Load/Baselines.json` 의 `startup_ms` 와 별개의 진단 값이다).
        [[maybe_unused]] const int64     showEndMicro = MonotonicClock::nowMicroseconds();
        [[maybe_unused]] constexpr int64 kMs          = constant::time::kMicrosecondsPerMillisecond;
        SW_LOG_INFO_ID( "App.StartupPhasesHostScopeMs", "Startup phases: hostScope %# ms, engine %# ms, window %# ms, modules %# ms, finalize+show %# ms, total %# ms | subsystems: %#",
                        ( timings._hostScopeEndMicro - _initializeStartMicro ) / kMs, ( timings._engineEndMicro - timings._hostScopeEndMicro ) / kMs, ( timings._windowEndMicro - timings._engineEndMicro ) / kMs,
                        ( timings._modulesEndMicro - timings._windowEndMicro ) / kMs, ( showEndMicro - timings._modulesEndMicro ) / kMs, ( showEndMicro - _initializeStartMicro ) / kMs,
                        _launch.getEngine().describeSubsystemInitTimings().c_str() );
    }

    void App::onStartupFrameEnded()
    {
        // 메인 루프에 든 뒤 첫 프레임들이 끝난 시각 — 시작 씬을 받는 프레임이 길다(Debug 수 초).
        constexpr uint32 kStartupFramesLogged = 3;
        if ( _startupFrameCount < kStartupFramesLogged )
        {
            ++_startupFrameCount;
            SW_LOG_INFO_ID( "App.StartupFrameEndMs", "Startup frame %# ended %# ms after entering the main loop", _startupFrameCount,
                            ( MonotonicClock::nowMicroseconds() - _mainLoopStartMicro ) / constant::time::kMicrosecondsPerMillisecond );
        }
        if ( _bShowWindowPending == SW_FALSE )
            return;
        // 활성 씬이 선 프레임 다음 프레임이 끝나면 그 씬의 첫 그림이 나와 있다(렌더 스레드가 한 프레임 늦게 낸다). 그때 스플래시를 닫고 주 창을 띄운다.
        const bool bSceneActive = engine::getSubsystem<SceneManager>().getActiveScene() != nullptr;
        if ( bSceneActive && _bActiveSceneSeen == SW_TRUE )
        {
            _bShowWindowPending = SW_FALSE;
            if ( SplashSubsystem* const pSplashHost = engine::findSubsystem<SplashSubsystem>() ) // 선택: 렌더러 없는 구성은 스플래시가 없다
                pSplashHost->getSplash().dismiss();
            _pWindow->showWindow( true );
            SW_LOG_INFO_ID( "App.MainWindowShownMs", "Main window shown %# ms after entering the main loop (first scene drawn)",
                            ( MonotonicClock::nowMicroseconds() - _mainLoopStartMicro ) / constant::time::kMicrosecondsPerMillisecond );
        }
        _bActiveSceneSeen = bSceneActive ? SW_TRUE : SW_FALSE;
    }

    FramePacingInputs App::makeFramePacingInputs() const
    {
        const GameNetSession* pNetSession = engine::areEngineServicesBound() ? engine::findSubsystem<GameNetSession>() : nullptr;
        FramePacingInputs     inputs;
        inputs._bAutomationRun = _pAutomation != nullptr && _pAutomation->isBusy();
        inputs._bListenHost    = pNetSession != nullptr && pNetSession->getRole() == GameNetRole::Server;
        inputs._bMinimized     = _pWindow->isMinimized();
        inputs._bFocused       = _pWindow->hasFocus();
        inputs._bEditorQuiet   = getModuleManager()->getFrameState()._bEditorQuiet == SW_TRUE;
        return inputs;
    }

    bool App::runFrame( FramePacer& framePacer )
    {
        framePacer.markFrameStart();

        // 프로파일 실행(-gv_profileFrames=N, 개발 도구의 프레임 관찰자)과 자동화 시나리오(-scenario)는 스스로 끝난다(종료 코드는 `getExitCode`). 부모 프로세스는 표준 입력의 quit 으로 끝낸다(개발 도구 관찰자).
        if ( _launch.getEngine().isQuitRequested() )
        {
            _pWindow->requestClose();
            return false;
        }

        // 결정적 실행의 프레임 고정 시간(0 이면 벽시계) — 채우는 쪽(자동화 · 개발 스위치)이 없는 배포본은 늘 0 이다.
        const float32   fixedFrameDelta = engine::getSubsystem<DeterministicRun>().getPolicy()._fixedFrameDelta;
        const FrameTime frameTime       = _fixedTimestep.advance( engine::getSubsystem<GameTimeScale>().get(), fixedFrameDelta );

        // 사용자 설정의 화면 변경(창 방식, 해상도, VSync)은 OS 리사이즈와 같은 시점인 프레임 시작 전에 처리한다.
        _userSettingsHost.update( frameTime._deltaTime );

        _launch.getEngine().beginFrame( frameTime._deltaTime, frameTime._unscaledDeltaTime ); // 정지 메뉴와 페이드는 게임이 멈춰도 돈다.
        // 에디터 Play/Pause 상태를 여기서 한 번 고정한다. 아래 고정 스텝이 여러 번 돌아도 DLL 경계를 넘어 다시 묻지 않고,
        // 모든 단계가 같은 답을 본다.
        getModuleManager()->beginFrame();

        // 셸 액션(리로드 단축키 · 개발 콘솔 키)을 이번 프레임 것으로 갱신한 뒤 읽는다(콘솔은 씬 틱 뒤 개발 도구 관찰자). Shipping 에는 둘 다 없어 셸 InputMap 도 로드하지 않는다.
        if ( _launch.getDevLayer() != nullptr )
            _launch.getDevLayer()->pollShellActions( frameTime._deltaTime, _bEnableEditor == SW_TRUE );

        getModuleManager()->updateGameFrame( frameTime._fixedDeltaTime, frameTime._fixedStepCount, frameTime._deltaTime );

        {
            // 에디터가 없으면 바로 돌아온다. 이 호출이 게임 뷰와 씬 뷰 RT, 씬 틱 여부를 확정한다.
            SW_PROFILE_SCOPE( "GT.Editor.updateUI" );
            getModuleManager()->updateEditorUI( frameTime._deltaTime );
        }

        // 이번 프레임의 페이싱 상태를 정한다 — 에디터가 이번 프레임의 다시 그릴 까닭을 정한 뒤다. 실행 인자(`-gv_framePacing` · 시나리오 · 프로파일 · 리슨 포트)가
        // 고른 상태가 창 · 에디터 상태(최소화 · 유휴 · 포커스)보다 앞선다.
        framePacer.beginFrame( makeFramePacingInputs() );

        // 카메라 포인터를 미리 잡아 두면 tick 안의 씬 전환이나 핫 리로드가 그 GameObject 를 파괴한 뒤 역참조하게 된다. 그래서
        // 조회 자체를 tick 안으로 넘긴다.
        //
        // 모듈 교체는 **틱 직전**(게임 업데이트 뒤, 씬 틱 앞)에 한다. 여기서 DLL 이 바뀌고 인스턴스가 새로 만들어지기 때문이다.
        if ( _launch.getDevLayer() != nullptr )
            _launch.getDevLayer()->updateModuleReload();

        const ModuleFrameState& frameState = getModuleManager()->getFrameState();
        _launch.getEngine().update( frameTime._deltaTime, frameState._views, _sceneViewCameraProvider, frameState._bTickScene == SW_TRUE );
        getModuleManager()->endEditorFrame();

        if constexpr ( build::kShipping == false )
            _pBackendSwitcher->applyIfPending();

        _launch.getEngine().endFrame();
        onStartupFrameEnded();
        // 배경이면 상한 주기까지, 최소화면 창 메시지가 올 때까지 잔다. 다른 상태는 바로 다음 프레임이다.
        framePacer.endFrame( *_pWindow );
        return true;
    }

    bool App::beginMainLoop()
    {
        // 루프는 창이 있어야 돈다. 헤드리스 부팅(예: --cook-shaders)은 창을 만들지 않으므로 여기서 끝난다. 모드 플래그가 아니라
        // 실제 선행 조건으로 적는다.
        if ( _pWindow == nullptr || _bQuitAfterInitialize == SW_TRUE )
            return false;

        logMainLoopStart();
        _mainLoopStartMicro = MonotonicClock::nowMicroseconds();
        _fixedTimestep.start();

        // 페이서는 주 창 서비스가 든다 — 창(`_pWindow`)을 그 서비스에서 받았으므로 여기서 없으면 결함이다.
        MainWindowSubsystem* const pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        if ( pWindowHost == nullptr )
        {
            SW_LOG_ERROR_ID( "App.EngineScopeNoMainWindow2", "The engine scope has no MainWindow subsystem - the main loop cannot pace frames" );
            return false;
        }
        _pFramePacer = &pWindowHost->getFramePacer();
        return true;
    }

    bool App::tickMainLoop()
    {
        if ( _pWindow->processMessages() == false )
            return false;
        _pWindow->advanceLifecycleTick();
        // 표면이 없거나 앱이 멈췄으면 프레임을 돌지 않는다 — 프로세스는 살아 있고 게임 시간은 멈춘다(Android onPause · 언리얼 `FAppEventManager` 가 넣는 자리). 메시지만 처리하며 기다린다.
        if ( _pWindow->isRenderPaused() )
        {
            _bRenderPaused = SW_TRUE;
            MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + AppInternal::kPausedTickNanoseconds );
            return true;
        }
        // 멈춘 동안 흐른 실제 시간을 한 프레임의 델타로 받지 않는다.
        if ( _bRenderPaused == SW_TRUE )
        {
            _bRenderPaused = SW_FALSE;
            _fixedTimestep.start();
        }
        return runFrame( *_pFramePacer );
    }

    int32 App::finishMainLoop()
    {
        endMainLoop();
        const int32 exitCode = getExitCode();
        shutdown();
        return exitCode;
    }

    void App::endMainLoop()
    {
        _pFramePacer = nullptr;
        // 창이 닫혀 끝났으면 자동화 시나리오가 그 결과를 종료 코드로 정한다(시나리오가 이미 끝냈으면 아무 일도 없다).
        _launch.getEngine().onWindowClosed();
    }

    int32 App::getInitFailureExitCode() const
    {
        // 헤드리스 작업이 정한 코드(시나리오 행렬의 시나리오 결과)가 먼저다.
        if ( _launch.getEngine().isHeadless() && _launch.getEngine().didHeadlessTaskFail() && _launch.getEngine().getHeadlessFailureExitCode() != -1 )
            return _launch.getEngine().getHeadlessFailureExitCode();
        // 환경 탓(백엔드가 이 빌드에 없거나 드라이버가 기능을 주지 않음)은 테스트와 스크립트가 건너뜀으로 읽는 코드로 끝낸다. 그 밖은 초기화 실패다 —
        // 표에 없는 -1 로 끝내면 자동화가 크래시로 셌다(언리얼도 초기화 실패 코드를 따로 낸다). 까닭은 초기화가 오류 로그로 남겼다.
        return AppInternal::isRHIUnusableHere( _launch.getEngine().getRHIInitResult() ) ? kRHIUnusableHereExitCode : static_cast<int32>( ProcessExitCode::InitializationFailed );
    }

    int32 App::getExitCode() const
    {
        return static_cast<int32>( _launch.getEngine().getExitCode() ); // 프로세스 경계 — 엔진 안에서는 표의 열거형이다
    }

    void App::onConfigReloaded( const hashed_string& configTypeName )
    {
        if ( configTypeName != EngineConfig::StaticType()->_fullyQualifiedName )
            return;
        const ConfigManager* pConfigManager = _launch.getEngine().getConfigManager();
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
            inputManager.recordNativeEvent( event );
            if ( inputManager.shouldDropNativeEvent( event ) )
                return false;
        }

        // 이벤트를 GameModuleManager(ImGui 등)에 먼저 보낸다.
        EditorModuleManager* const pModuleManager    = getModuleManager();
        const bool                 bConsumedByEditor = ( pModuleManager != nullptr && pModuleManager->onWindowMessage( event ) );

        // 에디터가 가로채지 않았을 때만 게임 InputManager 로 전달한다.
        if ( bConsumedByEditor == false && engine::areEngineServicesBound() )
            engine::getSubsystem<InputManager>().processNativeEvent( event );

        // Win32 OS 수준의 포커스와 활성화(DefWindowProc)가 정상적으로 동작하도록 false 를 반환한다.
        return false;
    }

    // 아래 셋은 렌더 스레드가 부른다. 훅은 모듈 매니저가 사는 동안만 걸려 있다(`bindEditorRenderHooks` — 떼는 세터가 렌더 스레드를 먼저 비운다).
    void App::onEditorPrepareRender( IRHIDevice& renderDevice, const RenderFramePacket& /*framePacket*/ )
    {
        getModuleManager()->prepareRenderEditor( renderDevice );
    }

    void App::onEditorRender( IRHIDevice& renderDevice, const RenderFramePacket& /*framePacket*/ )
    {
        getModuleManager()->renderEditor( renderDevice );
    }

    void App::onEditorPostPresent( IRHIDevice& renderDevice, const RenderFramePacket& /*framePacket*/ )
    {
        getModuleManager()->postPresentEditor( renderDevice );
    }
} // namespace sw
