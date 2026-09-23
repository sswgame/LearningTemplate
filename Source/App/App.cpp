#include "pch.h"

#include "App/App.h"

#include "App/AppConfig.h"
#include "App/Module/LiveReloadManager.h"
#include "App/Module/ModuleHost.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/RenderThread.h"
#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Utility/Debug/FrameProfiler.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/SplashWindow.h"

#include "RuntimeAPI/ABI/EditorAPI.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    SW_LOG_CALLER( "App" );

    App::App()
        : _engineLoop{}
        , _moduleHost{ nullptr }
        , _window{ nullptr }
        , _frameTimeline{}
        , _backendSwap{}
        , _viewCameraProvider{}
        , _initializeStartMicro{ 0 }
        , _bEnableEditor{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    App::~App() = default;

    bool App::initialize( int32 argc, utf8* pArgv[] )
    {
        _initializeStartMicro = std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count();
        // 리소스 루트 탐색은 EngineLoop 가 로거를 세운 **뒤에** 한다. 여기서 먼저 부르면 실패했을 때 로거가 없어 진단이
        // 사라지고, 반환값도 여기서는 쓸 곳이 없었다.

        // 1. 코어 매니저는 모두 EngineLoop 가 초기화한다(헤드리스 작업 처리 포함)
        if ( _engineLoop.initialize( argc, pArgv ) == false )
        {
            SW_LOG_ERROR( "EngineLoop initialization failed." );
            return false;
        }

        // 헤드리스 모드(예: --bake-shaders, --cook-scenes)면 스플래시와 창 UI 를 건너뛴다.
        // 작업이 실패했으면 **초기화 실패로 반환한다.** 그래야 종료 코드가 0 이 아니고, 이것을 부르는 `CookAssets.py` 가
        // "굽지 못했다" 를 알아챌 수 있다.
        if ( _engineLoop.isHeadless() )
            return _engineLoop.didHeadlessTaskFail() == false;

        SplashWindow splash;
        splash.initialize( "SW Engine", "Initializing Engine Subsystems..." );
        splash.setProgress( 0.05f );

        splash.updateStatus( "Loading Configuration & Display...", 0.40f );

        const ConfigManager*      pConfigManager      = _engineLoop.getConfigManager();
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

        _frameTimeline.configure( pEngineConfig->_maxFrameDeltaTime,
                                  pEngineConfig->_fixedDeltaTime,
                                  pEngineConfig->_maxFixedStepPerFrame );

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

        warnUnclaimedGlobalOverrides();

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
            if ( _window == nullptr || _window->initializeWindow( engineConfig._window._title.c_str(), width, height ) == false )
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

    bool App::startModules()
    {
        vector<GameKitConfig> listGameKitModule{};
#if !defined( SW_SHIPPING )
        const AppConfig* pAppConfig = _engineLoop.getConfigManager()->ensureConfig<AppConfig>(
            config::kFileRuntimeAppConfig, nullptr );
        if ( pAppConfig != nullptr )
            listGameKitModule = pAppConfig->_listGameKitModule;
#endif

#if !defined( SW_SHIPPING )
        // 모듈 감시자는 ModuleHost 보다 먼저 있어야 한다. ModuleHost 가 이 포인터로 리로드 콜백을 건다.
        _liveReloadManager = make_unique<LiveReloadManager>();
        // 모듈 DLL 을 내릴 수 있는 곳은 "씬은 사라졌고 서비스는 아직 있는" 좁은 구간뿐이다. 엔진이 내주는 훅에 건다.
        _engineLoop.setOnScenesReleased( SW_DELEGATE_LAMBDA( Delegate<void()>, [this]()
        {
            if ( _liveReloadManager != nullptr )
                _liveReloadManager->shutdown();
        } ) );
#endif

        _moduleHost = make_unique<ModuleHost>();
        if ( _moduleHost->initialize( getLiveReloadManager(),
                                      _engineLoop.getRhi(),
                                      _window.get(),
                                      _engineLoop.getRenderThread(),
                                      _bEnableEditor == SW_TRUE,
                                      listGameKitModule ) == false )
        {
            SW_LOG_ERROR( "ModuleHost initialization failed." );
            return false;
        }

        return true;
    }

    void App::warnUnclaimedGlobalOverrides() const
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

        // 예전에는 루프 안에서 프레임마다 다시 만들던 것들이다. 연결 대상이 프레임마다 바뀌지 않으므로 여기서 한 번 묶고,
        // 에디터 뷰 카메라는 에디터 모드에서만 묶는다. 비어 있다는 사실이 곧 "씬 카메라를 쓴다" 는 뜻이라 루프에서 모드를
        // 나눌 필요가 없다.
        if ( _bEnableEditor == SW_TRUE )
            _viewCameraProvider = SW_DELEGATE_METHOD( ViewCameraProviderDelegate, &App::getEditorViewCamera, this );

        _backendSwap.initialize( &_engineLoop, _moduleHost.get(), _bEnableEditor == SW_TRUE );

        _engineLoop.setPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorRender, this ) );
        _engineLoop.setPostPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorPostPresent, this ) );
    }

    void App::shutdown()
    {
        // 전역 변수 훅은 그 변수를 소유한 GlobalVariableManager(EngineLoop 소유)가 사라지기 전에 떼어 낸다. 헤드리스 부팅처럼
        // 연결되지 않은 경우에는 아무것도 하지 않는다.
        _backendSwap.shutdown();

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
        // 루프는 창이 있어야 돈다. 헤드리스 부팅(예: --bake-shaders)은 창을 만들지 않으므로 여기서 끝난다. 모드 플래그가 아니라
        // 실제 선행 조건으로 적는다.
        if ( _window == nullptr )
            return;

        // 시작 시간: `initialize` 첫 줄부터 여기까지의 경과 시간이다. 표준 출력 로그는 에러가 아니면 버퍼에 머물러 있어 밖에서는 시각을 잴 수 없다.
        [[maybe_unused]] const int64 startupMicro =
            std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count() - _initializeStartMicro;
        SW_LOG_INFO( "Entering App Main Loop (Thin Launcher)... startup %# ms", startupMicro / 1000 );

        _frameTimeline.start();

        while ( _window->processMessages() )
        {
            // 프로파일 실행(-gv_profileFrames=N)은 목표 프레임을 채우면 스스로 끝난다.
            if ( _engineLoop.wantsQuit() )
            {
                _window->requestClose();
                break;
            }

            const FrameTime frameTime = _frameTimeline.advance();

            _engineLoop.beginFrame();
            // 에디터 Play/Pause 상태를 여기서 한 번 고정한다. 아래 고정 스텝이 여러 번 돌아도 DLL 경계를 넘어 다시 묻지 않고,
            // 모든 단계가 같은 답을 본다.
            _moduleHost->beginFrame();

            pollReloadHotkeys( frameTime._deltaTime );

            // **게임 모듈의 시간도 표에 올린다.** 예전에는 이 셋이 계측 밖이었다. 표 제목이 "frame breakdown" 인데 정작 게임 코드가
            // 쓰는 시간은 한 줄도 없었고, 그래서 `GT.Frame` 만 보고 "프레임의 전부" 라고 읽게 됐다(벤치의 큐브 2만 개 갱신이 통째로
            // 보이지 않았다).
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
            // 모듈 교체는 **틱 직전**에 한다. 예전에는 EngineLoop::tick 첫머리에서 돌았고, 그 순서(게임 업데이트 뒤 · 씬 틱 앞)를
            // 그대로 지킨다. 여기서 DLL 이 바뀌고 인스턴스가 새로 만들어지기 때문이다.
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
    }

    void App::pollReloadHotkeys( [[maybe_unused]] float32 deltaTime )
    {
#if !defined( SW_SHIPPING )
        _engineLoop.updateShellActions( deltaTime );

        // 모듈을 다시 올리는 장치는 여기(App)에 있다. Engine 에는 "이 액션이 눌렸나" 만 묻는다.
        if ( _engineLoop.wasDebugActionTriggered( ActionMapDefaults::kReloadGameAction ) )
        {
            onForceReload( config::kTargetGameModule );
            SW_LOG_INFO( "%#: force SWGame reload", ActionMapDefaults::kReloadGameAction );
        }
        if ( _bEnableEditor == SW_TRUE && _engineLoop.wasDebugActionTriggered( ActionMapDefaults::kReloadEditorAction ) )
        {
            onForceReload( config::kTargetEditorModule );
            SW_LOG_INFO( "%#: force EditorModule reload", ActionMapDefaults::kReloadEditorAction );
        }
#endif
        // Shipping 에는 리로드할 모듈이 없다. 예전에는 셸 ActionMap 을 리소스에서 올려 프레임마다 갱신했지만, 그 입력 상태를
        // 묻는 코드가 하나도 없었다. 배포 빌드에서 아무도 읽지 않는 입력을 계속 돌리고 있었던 것이다.
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
        if ( _moduleHost == nullptr )
            return;

        const EditorHandle pEditor = _moduleHost->getEditor();
        if ( pEditor == nullptr )
            return;

        const EditorAPI& editorAPI = _moduleHost->getEditorApi();
        if ( editorAPI.postPresent != nullptr )
            editorAPI.postPresent( pEditor, &renderDevice );
    }
} // namespace sw
