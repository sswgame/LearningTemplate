#include "pch.h"

#include "App/App.h"

#include "App/Module/LiveReloadManager.h"
#include "App/Module/ModuleCatalogLoader.h"
#include "App/Module/ModuleHost.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/String/string_splitter.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/DevTools/DevConsoleController.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/RenderThread.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Telemetry/CrashReportService.h"
#include "Engine/Telemetry/CrashReportUploader.h"
#include "Engine/Utility/GameTimeScale.h"
#include "Engine/Utility/Profiling/FrameProfiler.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/SplashWindow.h"

#include "RuntimeAPI/ABI/EditorAPI.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다(유니티 빌드에서 이름이 충돌하지 않도록 TU 이름을 붙입니다). */
        struct RHIBackendSwitcherInternal
        {
            /** @brief gv_rhiBackend 의 변수 정보를 찾습니다. 없으면 nullptr 입니다. */
            static GlobalVariableInfo* findBackendVariable()
            {
                return engine::getGlobalVariableManager().findVariable( "gv_rhiBackend" );
            }
        };

        /** @brief 이 TU 의 창 크기 상수입니다. */
        struct AppWindowInternal
        {
            /** @brief 에디터 창의 최소 클라이언트 크기입니다. 메뉴바 · 도크 다섯 칸 · 게임 뷰 툴바가 겹치지 않는 바닥입니다. */
            static constexpr uint32 kEditorMinClientWidth  = 960;
            static constexpr uint32 kEditorMinClientHeight = 540;
        };
    } // namespace

#if !defined( SW_SHIPPING )
    /**
     * @brief `-gv_reloadGameAtFrame=N`: N 번째 프레임에 게임 모듈(SWGame) 핫 리로드를 한 번 요청합니다(리로드 단축키와 같은 길).
     * @details 디렉터의 시뮬레이션이 리로드를 넘는지 손 없이 확인하는 스위치입니다 — 로그의 `[ReloadProbe]` 앞뒤 상태 줄을 견준다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_reloadGameAtFrame, 0, "이 프레임에 게임 모듈 핫 리로드를 요청한다 (0=사용 안 함)" );
#endif
    /**
     * @brief `-gv_fixedFrameDelta=<초>`: 0 보다 크면 프레임마다 벽시계 대신 그 시간을 흘립니다(자동화 시나리오 · 픽셀 비교 · 재현). 0 이면 실시간입니다.
     * @details 고정 스텝(물리 · fixedUpdate)도 이 시간으로 나뉩니다 — 1/60 이면 프레임마다 정확히 한 스텝입니다. 시나리오(`-scenario`)는 이 값을 시나리오의
     *          `fixedDelta` 로 둡니다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( float32, gv_fixedFrameDelta, 0.0f, "프레임마다 흘릴 고정 시간(초, 0=실시간) — 결정적 실행" );
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "App" );

#if !defined( SW_SHIPPING )
    /** @brief `-gv_devConsoleExec="timescale 0.5;gv_viewMode 2"`: 시작 씬이 열린 뒤 개발 콘솔로 돌릴 명령(`;` 로 나눈다). 자동화 · 재현용입니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_devConsoleExec, "", "시작 씬이 열린 뒤 개발 콘솔로 돌릴 명령 (; 로 나눔)" );
    /** @brief `-gv_devConsoleOpen=1`: 에디터 없이 띄울 때 게임 창의 개발 콘솔을 연 채로 시작합니다(화면 확인용). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_devConsoleOpen, 0, "게임 창 개발 콘솔을 연 채로 시작 (1=열기)" );
#endif

    App::App()
        : _engineLoop{}
        , _moduleHost{ nullptr }
        , _window{ nullptr }
        , _fixedTimestep{}
        , _backendSwap{}
        , _userSettingsHost{}
        , _viewCameraProvider{}
        , _initializeStartMicro{ 0 }
        , _bEnableEditor{ SW_FALSE }
        , _bDevConsoleExecPending{ SW_FALSE }
        , _bQuitAfterInitialize{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    App::~App() = default;

    bool App::initialize( int32 argc, utf8* pArgv[] )
    {
        _initializeStartMicro = MonotonicClock::nowMicroseconds();
        // 리소스 루트 탐색은 EngineLoop 가 로거를 세운 **뒤에** 한다. 여기서 먼저 부르면 실패했을 때 로거가 없어 진단이
        // 사라지고, 반환값도 여기서는 쓸 곳이 없다.

        // 1. 코어 매니저는 모두 EngineLoop 가 초기화한다(헤드리스 작업 처리 포함). 타입 공급자 모듈은 그 기동 단계(`ModuleTypes`)에서 이 App 이 올린다.
        _engineLoop.setModuleTypeLoader( SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &App::loadModuleImages, this ) );
        if ( _engineLoop.initialize( argc, pArgv ) == false )
        {
            SW_LOG_ERROR( "EngineLoop initialization failed." );
            return false;
        }

        // 헤드리스 모드(예: --cook-shaders, --cook-scenes)면 스플래시와 창 UI 를 건너뛴다.
        // 작업이 실패했으면 **초기화 실패로 반환한다.** 그래야 종료 코드가 0 이 아니고, 이것을 부르는 `CookAssets.py` 가
        // "쿠킹하지 못했다" 를 알아챌 수 있다.
        if ( _engineLoop.isHeadless() )
        {
            if ( _engineLoop.didHeadlessTaskFail() )
                return false;

            // 크래시 보고 프로세스(`-crash-reporter=<폴더>`) — 엔진은 명령줄까지만 섰다(서비스 · 게임 모듈 없음). 묶음만 보내고 끝낸다.
            // 이 저장소는 네트워크 창구를 싣지 않는다 — 게임이 IHttpClient 를 구현하면 여기서 HttpCrashReportUploader 를 쓴다.
            const CommandLineManager* pReporterCommandLine = _engineLoop.getCommandLineManager();
            string                    reporterFolder;
            if ( pReporterCommandLine != nullptr && pReporterCommandLine->getArgument( CommandLineArgument::CRASH_REPORTER, reporterFolder ) &&
                 reporterFolder.empty() == false )
            {
                const uint32 sentCount = CrashReportService::runReporter( reporterFolder, NullCrashReportUploader::get() );
                SW_LOG_INFO( "[CrashReporter] %# report(s) sent from '%#'", sentCount, reporterFolder.c_str() );
                return true;
            }

            // 원본 임포트(텍스처 · 모델)는 에디터 모듈의 일이다. 엔진은 헤드리스로 세우기만 했고, 모듈을 올리는 것은 App 이다.
            const CommandLineManager* pHeadlessCommandLine = _engineLoop.getCommandLineManager();
            bool                      bImportTextures      = false;
            bool                      bCheckTextures       = false;
            bool                      bImportModels        = false;
            bool                      bCheckModels         = false;
            bool                      bImportHeightfields  = false;
            bool                      bCheckHeightfields   = false;
            if ( pHeadlessCommandLine != nullptr )
            {
                pHeadlessCommandLine->getArgument( CommandLineArgument::IMPORT_TEXTURES, bImportTextures );
                pHeadlessCommandLine->getArgument( CommandLineArgument::CHECK_TEXTURES, bCheckTextures );
                pHeadlessCommandLine->getArgument( CommandLineArgument::IMPORT_MODELS, bImportModels );
                pHeadlessCommandLine->getArgument( CommandLineArgument::CHECK_MODELS, bCheckModels );
                pHeadlessCommandLine->getArgument( CommandLineArgument::IMPORT_HEIGHTFIELDS, bImportHeightfields );
                pHeadlessCommandLine->getArgument( CommandLineArgument::CHECK_HEIGHTFIELDS, bCheckHeightfields );
            }
            bool bSucceeded = true;
            if ( bImportTextures || bCheckTextures )
                bSucceeded = ModuleHost::importAssetsWithEditorModule( EditorImportKind::Texture, bCheckTextures ) && bSucceeded;
            if ( bImportModels || bCheckModels )
                bSucceeded = ModuleHost::importAssetsWithEditorModule( EditorImportKind::Model, bCheckModels ) && bSucceeded;
            if ( bImportHeightfields || bCheckHeightfields )
                bSucceeded = ModuleHost::importAssetsWithEditorModule( EditorImportKind::Heightfield, bCheckHeightfields ) && bSucceeded;
            return bSucceeded;
        }

        SplashWindow splash;
        splash.initialize( "SW Engine", "Initializing Engine Subsystems..." );
        splash.setProgress( 0.05f );

        splash.updateStatus( "Loading Configuration & Display...", 0.40f );

        ConfigManager*            pConfigManager      = _engineLoop.getConfigManager();
        const CommandLineManager* pCommandLineManager = _engineLoop.getCommandLineManager();
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

        _fixedTimestep.configure( pEngineConfig->_maxFrameDeltaTime,
                                  pEngineConfig->_fixedDeltaTime,
                                  pEngineConfig->_maxFixedStepPerFrame );
        // 에디터가 Config/ 를 감시해 다시 읽으면(ConfigManager::reloadConfigFile) 프레임 시간 정책도 따라간다.
        pConfigManager->onConfigReloaded().add( SW_DELEGATE_METHOD( Delegate<void( const hashed_string& )>, &App::onConfigReloaded, this ) );

        // 2. 창 소유권을 가져온다(초기화 중에는 숨긴 상태로 시작한다)
        splash.updateStatus( "Initializing Platform Window & Graphics...", 0.60f );
        if ( acquireMainWindow( *pEngineConfig, *pCommandLineManager ) == false )
        {
            splash.dismiss();
            return false;
        }

        // 3. ModuleHost(에디터 · 게임 수명 주기)를 연결한다
        splash.updateStatus( "Loading Modules & Compiling Shaders...", 0.75f );
        if ( startModules() == false )
        {
            splash.dismiss();
            return false;
        }

        warnUnknownGlobalOverrides();

        // 초상화 굽기(`--render-portraits=a.prefab.xml,b.prefab.xml`) — 게임 컴포넌트 타입이 올라온 지금 그리고 끝낸다(창은 띄우지 않는다).
        string portraitList;
        if ( pCommandLineManager->getArgument( CommandLineArgument::RENDER_PORTRAITS, portraitList ) && portraitList.empty() == false )
        {
            int32  portraitSize = 256;
            string portraitDir;
            pCommandLineManager->getArgument( CommandLineArgument::PORTRAIT_SIZE, portraitSize );
            pCommandLineManager->getArgument( CommandLineArgument::PORTRAIT_DIR, portraitDir );
            splash.dismiss();
            _bQuitAfterInitialize = SW_TRUE;
            return _engineLoop.renderPortraits( portraitList, portraitDir, static_cast<uint32>( portraitSize > 0 ? portraitSize : 256 ) );
        }

        // 4. 창 콜백과 이벤트 전달을 설정한다
        splash.updateStatus( "Finalizing Setup...", 0.95f );
        bindHostCallbacks();

        splash.updateStatus( "Ready", 1.0f );

        // 준비가 끝났다. 스플래시 창을 닫고 메인 창을 화면에 띄운다
        splash.dismiss();
        _window->showWindow( true );

        return true;
    }

    bool App::acquireMainWindow( const EngineConfig& engineConfig, const CommandLineManager& commandLineManager )
    {
        uint32 width  = engineConfig._window._width;
        uint32 height = engineConfig._window._height;
        commandLineManager.getArgument( CommandLineArgument::WIDTH, width );
        commandLineManager.getArgument( CommandLineArgument::HEIGHT, height );

        // EngineLoop::initialize 가 플랫폼 창을 만들어 IWindow::setActiveWindow 로 넘겨 두었으면(그쪽은 release() 로 소유권을
        // 놓는다) 여기서 App 의 unique_ptr 이 넘겨받는다. 전역 포인터는 그 뒤로 관찰용으로만 남는다.
        _window.reset( IWindow::getActiveWindow() );
        if ( _window == nullptr )
        {
            _window = IWindow::createPlatformWindow();
            if ( _window == nullptr || _window->initializeWindow( GameConfig::getActive()._windowTitle.c_str(), width, height ) == false )
            {
                SW_LOG_ERROR( "Failed to create platform window!" );
                return false;
            }
            _window->showWindow( false );
            IWindow::setActiveWindow( _window.get() );
        }

        bool bEnableEditor = false;
        commandLineManager.getArgument( CommandLineArgument::ENABLE_EDITOR, bEnableEditor );

        // 백엔드가 에디터를 지원하는지는 여기서 미리 묻지 않는다. 답을 아는 것은 에디터다. 지원하지 못하면
        // ImGuiEditor::initialize 가 렌더러 백엔드를 만들지 못해 실패하고, 에디터만 뜨지 않는다.
        _bEnableEditor = bEnableEditor ? SW_TRUE : SW_FALSE;

        return true;
    }

    LiveReloadManager* App::getLiveReloadManager() const
    {
#if defined( SW_SHIPPING )
        return nullptr;
#else
        return _liveReloadManager.get();
#endif
    }

    bool App::loadModuleImages()
    {
        // 모듈 호스트 · 감시자 · 앱 설정은 엔진 기반 몫이다. 모듈 본체의 로드는 ModuleHost 가 에디터 · 게임 태그를 건다.
        SW_MEMORY_SCOPE( EngineMisc );
#if !defined( SW_SHIPPING )
        // 무엇을 올릴지는 모듈 매니페스트가 정한다. App 은 빌드가 담은 모든 대상을 올린다(Game = 클라이언트 + 리슨 서버용 서버 모듈,
        // Client = 클라이언트). 전용 서버는 Server 실행 파일이 Server 대상으로 같은 해석을 한다.
        if ( ModuleCatalogLoader::loadAndResolve( ModuleCatalog::getBuildTargetMask(), _moduleCatalog, _moduleResolution ) == false )
            return false;

        // 모듈 감시자는 모듈을 올리는 ModuleHost 보다 먼저 있어야 한다. ModuleHost 가 이 포인터로 모듈을 올리고 리로드 콜백을 건다.
        _liveReloadManager = make_unique<LiveReloadManager>();
        // 모듈 DLL 을 내릴 수 있는 곳은 "씬은 사라졌고 서비스는 아직 있는" 좁은 구간뿐이다. 엔진이 내주는 훅에 건다.
        _engineLoop.setOnScenesReleased( SW_DELEGATE_LAMBDA( Delegate<void()>, [this]()
        {
            if ( _liveReloadManager != nullptr )
                _liveReloadManager->shutdown();
        } ) );
#endif

        _moduleHost = make_unique<ModuleHost>();
        return _moduleHost->loadModuleImages( getLiveReloadManager(), _moduleCatalog, _moduleResolution );
    }

    bool App::startModules()
    {
        if ( _moduleHost == nullptr )
            _moduleHost = make_unique<ModuleHost>();
        if ( _moduleHost->initialize( getLiveReloadManager(),
                                      _engineLoop.getRhi(),
                                      _window.get(),
                                      _engineLoop.getRenderThread(),
                                      _bEnableEditor == SW_TRUE ) == false )
        {
            SW_LOG_ERROR( "ModuleHost initialization failed." );
            return false;
        }

        return true;
    }

    void App::warnUnknownGlobalOverrides() const
    {
        const CommandLineManager* pCommandLineManager = _engineLoop.getCommandLineManager();
        if ( pCommandLineManager == nullptr )
            return;

        // 모르는 `-gv_*` 키는 파서가 버리지 않고 보류표에 남긴다. 모듈이 선언하는 변수는 파싱 시점에 아직 없기 때문이다.
        // 모듈이 모두 올라온 지금까지도 가져간 곳이 없으면 오타다. 표는 비우지 않는다. 핫 리로드로 나중에 올라오는 모듈이
        // 여전히 가져갈 수 있기 때문이다.
        const vector<string> listPendingName = pCommandLineManager->collectPendingGlobalNames();
        for ( const string& pendingName : listPendingName )
        {
            if ( engine::getGlobalVariableManager().findVariable( pendingName ) == nullptr )
                SW_LOG_WARNING( "%#: 그런 전역 변수가 없습니다. 무시됩니다", pendingName.c_str() );
        }
    }

    void App::bindHostCallbacks()
    {
        _window->setResizeCallback( SW_DELEGATE_METHOD( WindowResizeDelegate, &App::onResize, this ) );
        _window->setCustomMessageHandler( SW_DELEGATE_METHOD( WindowMessageHandlerDelegate, &App::onWindowMessage, this ) );

        // 연결 대상이 프레임마다 바뀌지 않으므로 루프 안이 아니라 여기서 한 번 묶고,
        // 에디터 뷰 카메라는 에디터 모드에서만 묶는다. 비어 있다는 사실이 곧 "씬 카메라를 쓴다" 는 뜻이라 루프에서 모드를
        // 나눌 필요가 없다.
        if ( _bEnableEditor == SW_TRUE )
        {
            _viewCameraProvider = SW_DELEGATE_METHOD( ViewCameraProviderDelegate, &App::getEditorViewCamera, this );
            // 에디터 창은 패널 배치가 겹치는 크기 밑으로 줄이지 않는다(언리얼 메인 프레임 · 유니티 에디터 창도 최소 크기를 둔다).
            _window->setMinimumClientSize( AppWindowInternal::kEditorMinClientWidth, AppWindowInternal::kEditorMinClientHeight );
        }

        _backendSwap.initialize( &_engineLoop, _moduleHost.get(), _bEnableEditor == SW_TRUE );
        _userSettingsHost.initialize( &_engineLoop, _window.get() );

        _engineLoop.setPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorRender, this ) );
        _engineLoop.setPostPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorPostPresent, this ) );
    }

    void App::shutdown()
    {
        // 전역 변수 훅은 그 변수를 소유한 GlobalVariableManager(EngineLoop 소유)가 사라지기 전에 떼어 낸다. 헤드리스 부팅처럼
        // 연결되지 않은 경우에는 아무것도 하지 않는다.
        _backendSwap.shutdown();
        _userSettingsHost.shutdown();

        // 헤드리스 부팅은 창도 ModuleHost 도 만들지 않는다. 아래 경로가 그대로 아무 일도 하지 않으므로 모드 분기를 따로 두지 않는다.
        // 주의: ModuleHost 를 EngineLoop 보다 먼저 종료해야 한다. 에디터 shutdown 이 Game View RT 를 해제할 때 RenderThread 와
        // RHI 디바이스를 쓴다.
        BLOCK( "Game / Editor 인스턴스 정리" )
        {
            if ( _moduleHost != nullptr )
            {
                _moduleHost->shutdown();
                _moduleHost.reset();
            }
        }

#if !defined( SW_SHIPPING )
        // 콘솔은 입력 관리자에 글자 콜백을 걸고 키보드 포커스를 쥔다 — 엔진(입력 관리자)보다 먼저 내린다. 콘솔 창은 게임 창이 소유한 팝업이라
        // 게임 창보다도 먼저다.
        _devConsoleController.reset();
#endif
        _engineLoop.shutdown();

        if ( _window != nullptr )
        {
            // App 이 소유한 활성 창을 파괴하기 전에 전역 포인터부터 끊는다(댕글링 방지).
            if ( IWindow::getActiveWindow() == _window.get() )
                IWindow::setActiveWindow( nullptr );
            _window->destroy();
            _window.reset();
        }
    }

    void App::run()
    {
        // 루프는 창이 있어야 돈다. 헤드리스 부팅(예: --cook-shaders)은 창을 만들지 않으므로 여기서 끝난다. 모드 플래그가 아니라
        // 실제 선행 조건으로 적는다.
        if ( _window == nullptr || _bQuitAfterInitialize == SW_TRUE )
            return;

        // 시작 시간: `initialize` 첫 줄부터 여기까지의 경과 시간이다. 표준 출력 로그는 에러가 아니면 버퍼에 머물러 있어 밖에서는 시각을 잴 수 없다.
        [[maybe_unused]] const int64 startupMicro =
            MonotonicClock::nowMicroseconds() - _initializeStartMicro;
        SW_LOG_INFO( "Entering App Main Loop (Thin Launcher)... startup %# ms", startupMicro / 1000 );

        _fixedTimestep.start();
        startDevConsole();

        while ( _window->processMessages() )
        {
#if !defined( SW_SHIPPING )
            runPendingDevConsoleExec();
#endif
            // 프로파일 실행(-gv_profileFrames=N) · 자동화 시나리오(-scenario)는 스스로 끝난다(종료 코드는 `getExitCode`).
            if ( _engineLoop.isQuitRequested() )
            {
                _window->requestClose();
                break;
            }

            const FrameTime frameTime = _fixedTimestep.advance( GameTimeScale::get(), gv_fixedFrameDelta );
            GameTimeScale::setUnscaledDeltaTime( frameTime._unscaledDeltaTime ); // 정지 메뉴 · 페이드는 게임이 멈춰도 돈다

            // 사용자 설정의 화면 변경(창 방식 · 해상도 · VSync)은 OS 리사이즈와 같은 자리 — 프레임을 시작하기 전 — 에서 한다.
            _userSettingsHost.tick( frameTime._deltaTime );

            _engineLoop.beginFrame( frameTime._deltaTime );
            // 에디터 Play/Pause 상태를 여기서 한 번 고정한다. 아래 고정 스텝이 여러 번 돌아도 DLL 경계를 넘어 다시 묻지 않고,
            // 모든 단계가 같은 답을 본다.
            _moduleHost->beginFrame();

            pollReloadHotkeys( frameTime._deltaTime );
            updateDevConsole();

            // **게임 모듈의 시간도 표에 올린다.** 이 셋이 계측 밖이면 "frame breakdown" 표에 게임 코드가 쓰는 시간이 한 줄도 없어
            // `GT.Frame` 만 보고 "프레임의 전부" 라고 읽게 된다.
            {
                SW_PROFILE_SCOPE( "GT.Game.fixedUpdate" );
                for ( uint32 stepIndex = 0; stepIndex < frameTime._fixedStepCount; ++stepIndex )
                    _moduleHost->fixedUpdateGame( frameTime._fixedDeltaTime );
            }

            {
                SW_PROFILE_SCOPE( "GT.Game.update" );
                _moduleHost->updateGame( frameTime._deltaTime );
            }

            {
                // 에디터가 없으면 바로 돌아온다. 이 호출이 게임 뷰포트 RT 와 씬 틱 여부를 확정한다.
                SW_PROFILE_SCOPE( "GT.Editor.updateUi" );
                _moduleHost->updateEditorUi( frameTime._deltaTime );
            }

            // 카메라 포인터를 미리 잡아 두면 tick 안의 씬 전환 · 핫 리로드가 그 GameObject 를 파괴한 뒤 역참조하게 된다. 그래서
            // 조회 자체를 tick 안으로 넘긴다.
            //
            // 모듈 교체는 **틱 직전**(게임 업데이트 뒤 · 씬 틱 앞)에 한다. 여기서 DLL 이 바뀌고 인스턴스가 새로 만들어지기 때문이다.
#if !defined( SW_SHIPPING )
            if ( _liveReloadManager != nullptr )
                _liveReloadManager->update();
#endif

            const ModuleFrameState& frameState = _moduleHost->getFrameState();
            _engineLoop.tick( frameTime._deltaTime,
                              frameState._gameViewportTarget,
                              frameState._gameViewportWidth,
                              frameState._gameViewportHeight,
                              _viewCameraProvider,
                              frameState._bTickScene == SW_TRUE );
            _moduleHost->endEditorFrame();

            _backendSwap.applyIfPending();

            _engineLoop.endFrame();
        }
        // 창이 닫혀 끝났으면 자동화 시나리오가 그 결과를 종료 코드로 정한다(시나리오가 이미 끝냈으면 아무 일도 없다).
        _engineLoop.onWindowClosed();
    }

    int32 App::getInitFailureExitCode() const
    {
        // 환경 탓(백엔드가 이 빌드에 없다 · 드라이버가 기능을 안 준다)은 시험 · 스크립트가 건너뜀으로 읽는 코드로 끝낸다. 그 밖은 결함일 수 있다.
        return RHIInitResultUtil::isUnusableHere( _engineLoop.getRhiInitResult() ) ? kRhiUnusableHereExitCode : -1;
    }

    int32 App::getExitCode() const
    {
        return _engineLoop.getExitCode();
    }

    void App::pollReloadHotkeys( [[maybe_unused]] float32 deltaTime )
    {
#if !defined( SW_SHIPPING )
        _engineLoop.updateShellActions( deltaTime );

        // 모듈을 다시 올리는 장치는 여기(App)에 있다. Engine 에는 "이 액션이 눌렸나" 만 묻는다.
        if ( _engineLoop.wasDebugActionTriggered( InputMapDefaults::kReloadGameAction ) )
        {
            onForceReload( config::kTargetGameModule );
            SW_LOG_INFO( "%#: force SWGame reload", InputMapDefaults::kReloadGameAction );
        }
        if ( gv_reloadGameAtFrame > 0 && engine::getFrameProfiler().getFrameCount() == static_cast<uint64>( gv_reloadGameAtFrame ) )
        {
            SW_LOG_INFO( "[ReloadProbe] frame %# - forcing SWGame reload", gv_reloadGameAtFrame );
            gv_reloadGameAtFrame = 0; // 한 번만. 프로파일러가 프레임 수를 되돌리면(워밍업 뒤) 같은 번호가 다시 온다
            onForceReload( config::kTargetGameModule );
        }
        if ( _bEnableEditor == SW_TRUE && _engineLoop.wasDebugActionTriggered( InputMapDefaults::kReloadEditorAction ) )
        {
            onForceReload( config::kTargetEditorModule );
            SW_LOG_INFO( "%#: force EditorModule reload", InputMapDefaults::kReloadEditorAction );
        }
#endif
        // Shipping 에는 리로드할 모듈이 없다. 그 입력 상태를 묻는 코드가 없으므로 셸 InputMap 도 올리지 않는다.
    }

    void App::onResize( const uint32 width, const uint32 height )
    {
        RHI* pRHI = _engineLoop.getRhi();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
            return;

        // gv_useRenderThread(기본 true)이면 전용 RenderThread 가 별도 스레드에서 beginFrame/endFrame 으로 스왑체인 · 렌더
        // 타깃을 계속 건드리고 있을 수 있다. 이 상태에서 메인(창 메시지) 스레드가 바로 resize() 를 부르면 같은 자원에 대한
        // 실제 스레드 간 레이스가 된다. waitIdle() 로 큐를 비우고 GPU 까지 완전히 쉬게 한 뒤 크기를 바꾼다.
        RenderThread* pRenderThread = _engineLoop.getRenderThread();
        if ( pRenderThread != nullptr )
            pRenderThread->waitIdle();

        pRHI->getDevice().resize( width, height );
    }

    void App::onConfigReloaded( const hashed_string& configTypeName )
    {
        if ( configTypeName != EngineConfig::StaticType()->_fullyQualifiedName )
            return;
        const ConfigManager* pConfigManager = _engineLoop.getConfigManager();
        const EngineConfig*  pEngineConfig  = pConfigManager != nullptr ? pConfigManager->getConfig<EngineConfig>() : nullptr;
        if ( pEngineConfig != nullptr )
            _fixedTimestep.configure( pEngineConfig->_maxFrameDeltaTime, pEngineConfig->_fixedDeltaTime, pEngineConfig->_maxFixedStepPerFrame );
    }

    void App::startDevConsole()
    {
#if !defined( SW_SHIPPING )
        _devConsoleController = make_unique<DevConsoleController>();
        // 에디터가 있으면 입력을 받지 않는다(Output Log 입력 줄이 같은 콘솔이다) — 시작 명령만 돌린다.
        if ( _bEnableEditor == SW_FALSE && engine::areEngineServicesBound() )
            (void)_devConsoleController->initialize( &engine::getInputManager(), _window.get() ); // 창을 만들지 못해도 입력 · 시작 명령은 돈다(경고는 그쪽이 남긴다)
        _bDevConsoleExecPending = gv_devConsoleExec.empty() ? SW_FALSE : SW_TRUE;
        if ( gv_devConsoleOpen != 0 && _bEnableEditor == SW_FALSE )
            _devConsoleController->setOpen( true );
#endif
    }

    void App::runPendingDevConsoleExec()
    {
#if !defined( SW_SHIPPING )
        // 시작 씬이 다 열린 뒤에 돌린다 — 씬을 보는 명령(teleport · select · debugdraw.demo)이 빈 씬에 닿지 않게.
        if ( _bDevConsoleExecPending == SW_FALSE || _devConsoleController == nullptr || engine::areEngineServicesBound() == false )
            return;
        const SceneManager& sceneManager = engine::getSceneManager();
        if ( sceneManager.isTransitioning() || sceneManager.getActiveScene() == nullptr )
            return;
        _bDevConsoleExecPending = SW_FALSE;
        const string_splitter commands( string_view{ gv_devConsoleExec.c_str(), gv_devConsoleExec.size() }, { ";" } );
        for ( const string_view command : commands.getSplitList() )
        {
            (void)_devConsoleController->getConsole().submit( command ); // 답 · 실패는 로그에 남는다
        }
#endif
    }

    void App::updateDevConsole()
    {
#if !defined( SW_SHIPPING )
        InputMap* pShellMap = _engineLoop.getShellInputMap();
        if ( _bEnableEditor == SW_FALSE && _devConsoleController != nullptr && pShellMap != nullptr )
            _devConsoleController->update( *pShellMap );
#endif
    }

    bool App::onWindowMessage( const NativeWindowEvent& event )
    {
        // 이벤트를 ModuleHost(ImGui 등)에 먼저 보낸다
        const bool bConsumedByEditor = ( _moduleHost != nullptr && _moduleHost->onWindowMessage( event ) );

        // 에디터가 가로채지 않았을 때만 게임 InputManager 로 전달한다
        if ( bConsumedByEditor == false && engine::areEngineServicesBound() )
            engine::getInputManager().processNativeEvent( event );

        // Win32 OS 수준의 포커스 · 활성화(DefWindowProc)가 정상적으로 동작하도록 false 를 반환한다
        return false;
    }

    CameraComponent* App::getEditorViewCamera()
    {
        return _moduleHost != nullptr ? _moduleHost->getViewportCamera() : nullptr;
    }

    void App::onForceReload( const utf8* pModuleName )
    {
        LiveReloadManager* const pLiveReloadManager = getLiveReloadManager();
        if ( pLiveReloadManager == nullptr )
            return;

        pLiveReloadManager->triggerReload( pModuleName );
    }

    void App::onEditorRender( IRHIDevice& renderDevice, const RenderFramePacket& /*framePacket*/ )
    {
        SW_MEMORY_SCOPE( Editor );
        // 이 훅은 렌더 스레드가 부른다. ModuleHost 가 사라진 뒤에는 불리지 않는다. shutdown 이 ModuleHost 를 지우기 전에
        // drainRenderWorkers 로 큐를 비우고, 그 시점에는 메인 루프가 이미 끝나 새 프레임이 들어오지 않는다. 훅 델리게이트 자체를
        // 여기서 끊는 것은 오히려 위험하다. RenderThread::setPresentHook 은 잠금 없는 대입이라, 렌더 스레드가 도는 중에 바꾸면
        // 레이스가 된다(실제로 종료할 때 크래시가 났다). 아래 검사는 그 불변 조건이 깨졌을 때 죽는 대신 조용히 넘어가기 위한 것이다.
        if ( _moduleHost == nullptr )
            return;

        const EditorHandle pEditor = _moduleHost->getEditor();
        if ( pEditor == nullptr )
            return;

        const EditorAPI& editorAPI = _moduleHost->getEditorApi();
        if ( editorAPI.preRender != nullptr )
            editorAPI.preRender( pEditor, &renderDevice );

        if ( editorAPI.render != nullptr )
            editorAPI.render( pEditor, &renderDevice );
    }

    void App::onEditorPostPresent( IRHIDevice& renderDevice, const RenderFramePacket& /*framePacket*/ )
    {
        SW_MEMORY_SCOPE( Editor );
        if ( _moduleHost == nullptr )
            return;

        const EditorHandle pEditor = _moduleHost->getEditor();
        if ( pEditor == nullptr )
            return;

        const EditorAPI& editorAPI = _moduleHost->getEditorApi();
        if ( editorAPI.postPresent != nullptr )
            editorAPI.postPresent( pEditor, &renderDevice );
    }

    // ------------------------------------------------------------------------------
    // RHIBackendSwitcher
    // ------------------------------------------------------------------------------
    RHIBackendSwitcher::RHIBackendSwitcher()
        : _pEngineLoop{ nullptr }
        , _pModuleHost{ nullptr }
        , _bEnableEditor{ SW_FALSE }
        , _bHandlingChange{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void RHIBackendSwitcher::initialize( EngineLoop* pEngineLoop, ModuleHost* pModuleHost, bool bEnableEditor )
    {
        _pEngineLoop   = pEngineLoop;
        _pModuleHost   = pModuleHost;
        _bEnableEditor = bEnableEditor ? SW_TRUE : SW_FALSE;

        GlobalVariableInfo* pBackendVariable = RHIBackendSwitcherInternal::findBackendVariable();
        if ( pBackendVariable != nullptr )
            pBackendVariable->_onValueChanged = SW_DELEGATE_METHOD( GlobalVariableChangedDelegate, &RHIBackendSwitcher::onBackendVariableChanged, this );
    }

    void RHIBackendSwitcher::shutdown()
    {
        // **훅부터, 조건 없이 뗀다.** 훅은 `_pEngineLoop` 와 상관없이 `initialize` 가 걸어 두므로, 루프를 받지 못한 채 초기화된
        // 경우(도구 · 부분 초기화)에도 떼지 않으면 사라진 `this` 를 가리키는 콜백이 전역 변수에 남는다.
        GlobalVariableInfo* pBackendVariable = RHIBackendSwitcherInternal::findBackendVariable();
        if ( pBackendVariable != nullptr )
            pBackendVariable->_onValueChanged = {};

        _pEngineLoop = nullptr;
        _pModuleHost = nullptr;
    }

    void RHIBackendSwitcher::applyIfPending()
    {
        if ( _pEngineLoop == nullptr )
            return;

        const RHI* pRHI = _pEngineLoop->getRhi();
        if ( pRHI == nullptr || pRHI->hasPendingBackendChange() == false )
            return;

        if ( applyPendingChange() == false )
        {
            SW_LOG_ERROR( "Backend soft-recreate failed." );
            // 값만 되돌린다. 변경 콜백(onBackendVariableChanged)은 GlobalVariableInfo 의 setValueAsInt/setValueFromString
            // (콘솔 · 에디터 패널) 경로에서만 불린다. 그래서 되돌림이 재시도 루프가 될 일은 없다. 심볼이 아니라 매니저가 든 주소로
            // 쓰므로 App 이 Engine.dll 의 변수를 import 할 필요가 없다.
            GlobalVariableInfo* pBackendVariable = RHIBackendSwitcherInternal::findBackendVariable();
            if ( pBackendVariable != nullptr )
                *static_cast<RHIBackend*>( pBackendVariable->_pData ) = pRHI->getCommittedBackend();
        }
    }

    void RHIBackendSwitcher::onBackendVariableChanged( const GlobalVariableInfo* pInfo )
    {
        RHI* pRHI = _pEngineLoop != nullptr ? _pEngineLoop->getRhi() : nullptr;
        if ( pInfo == nullptr || pRHI == nullptr )
            return;

        // 아래의 되돌림은 C++ 대입이라 이 콜백을 다시 부르지 않는다(콜백은 GlobalVariableInfo 의 set* 경로만 부른다). 재진입
        // 가드는 되돌림을 언젠가 set* 로 바꾸더라도 무한 재귀가 되지 않도록 남겨 둔다.
        if ( _bHandlingChange == SW_TRUE )
            return;
        _bHandlingChange = SW_TRUE;

        const RHIBackend requestedBackend = static_cast<RHIBackend>( pInfo->getValueAsInt() );
        pRHI->schedulePendingBackendChange( requestedBackend );

        _bHandlingChange = SW_FALSE;
    }

    bool RHIBackendSwitcher::applyPendingChange()
    {
        if ( _pEngineLoop == nullptr || _pModuleHost == nullptr )
            return false;

        // 모듈 핸들은 교체 **전에** 받아 둔다. 디바이스를 다시 만드는 동안 LiveReload 가 돌지는 않지만, 재생성 경로가 "테이블이
        // 비었으면 모듈에서 다시 바인딩" 을 하려면 핸들이 필요하다.
        void* pEditorModule{ nullptr };
        void* pGameModule{ nullptr };
#if !defined( SW_SHIPPING )
        // 모듈 수명은 ModuleHost 가 안다. 여기서 리로드 내부를 직접 뒤지지 않는다.
        pEditorModule = _pModuleHost->getLoadedModuleHandle( sw::config::kTargetEditorModule );
        pGameModule   = _pModuleHost->getLoadedModuleHandle( sw::config::kTargetGameModule );
#endif

        // API 테이블은 놓지 않는다. 모듈을 언로드하지 않고 같은 테이블로 다시 만든다.
        _pModuleHost->suspendModules( ModuleScope::Both, false );

        const bool bSwapOk = _pEngineLoop->applyPendingBackendChange();
        RHI*       pRHI    = _pEngineLoop->getRhi();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
        {
            SW_LOG_ERROR( "applyPendingBackendChange 실패 — RHI 디바이스가 없어 모듈을 재생성하지 않습니다." );
            return false;
        }

        const bool bReinitOk = _pModuleHost->reinitializeAfterRhiSwap( pEditorModule, pGameModule );
        if ( bReinitOk == false )
            SW_LOG_ERROR( "reinitializeAfterRhiSwap 실패." );
        if ( bSwapOk == false )
            SW_LOG_ERROR( "applyPendingBackendChange 실패 — 이전 백엔드로 복구한 뒤 모듈을 재생성했습니다." );
        return bSwapOk && bReinitOk;
    }
} // namespace sw
