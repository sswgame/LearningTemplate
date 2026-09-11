#include "pch.h"

#include "App/App.h"

#include "App/AppConfig.h"
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
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/RenderThread.h"
#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Module/LiveReloadManager.h"
#include "Engine/Object/Component/CameraComponent.h"
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
        , _forceReloadHandler{}
        , _bEnableEditor{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    App::~App() = default;

    bool App::initialize( int32 argc, utf8* pArgv[] )
    {
        // 리소스 루트 탐색은 EngineLoop 이 로거를 세운 **뒤에** 한다. 여기서 먼저 부르면 실패했을 때의
        // 진단이 로거가 없어 사라지고, 반환값도 여기서는 볼 것이 없었다.

        // 1. 코어 매니저들은 모두 EngineLoop가 초기화 (헤드리스 작업 처리 포함)
        if ( _engineLoop.initialize( argc, pArgv ) == false )
        {
            SW_LOG_ERROR( "EngineLoop initialization failed." );
            return false;
        }

        // 헤드리스 모드(예: --bake-shaders)인 경우 스플래시 창 및 윈도우 UI 생성을 건너뛰고 정상 완료
        if ( _engineLoop.isHeadless() )
            return true;

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

        const EngineConfig* pEngineConfig = pConfigManager->getConfig<EngineConfig>( hashed_string( "EngineConfig" ) );
        if ( pEngineConfig == nullptr )
        {
            splash.dismiss();
            return false;
        }

        _frameTimeline.configure( pEngineConfig->_maxFrameDeltaTime,
                                  pEngineConfig->_fixedDeltaTime,
                                  pEngineConfig->_maxFixedStepPerFrame );

        // 2. 윈도우 소유권 획득 (초기화 중에는 숨김 상태로 시작)
        splash.updateStatus( "Initializing Platform Window & Graphics...", 0.60f );
        if ( acquireMainWindow( *pEngineConfig, *pCommandLineManager ) == false )
        {
            splash.dismiss();
            return false;
        }

        // 3. ModuleHost (에디터/게임 라이프사이클) 바인딩
        splash.updateStatus( "Loading Modules & Compiling Shaders...", 0.75f );
        if ( startModules() == false )
        {
            splash.dismiss();
            return false;
        }

        warnUnclaimedGlobalOverrides();

        // 4. 윈도우 콜백 및 이벤트 라우팅 설정
        splash.updateStatus( "Finalizing Setup...", 0.95f );
        bindHostCallbacks();

        splash.updateStatus( "Ready", 1.0f );

        // 준비 완료 -> 스플래시 창을 닫고 메인 윈도우를 화면에 표시
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

        // EngineLoop::initialize 가 플랫폼 윈도우를 만들어 IWindow::setActiveWindow 로 넘겨뒀으면
        // (그쪽은 release() 로 소유권을 놓는다) 여기서 App 의 unique_ptr 이 입양한다.
        // 전역 포인터는 그 뒤로 관찰용으로만 남는다.
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

        const RHICapabilities capabilities = RHIAvailability::query( gv_rhiBackend );
        if ( bEnableEditor && capabilities._bEditorSupported == false )
        {
            SW_LOG_WARNING( "Editor requested but backend %# does not set _bEditorSupported — disabling editor.", RHI::getBackendTypeName( gv_rhiBackend ) );
            bEnableEditor = false;
        }
        _bEnableEditor = bEnableEditor ? SW_TRUE : SW_FALSE;

        return true;
    }

    bool App::startModules()
    {
        vector<GameKitConfig> listGameKitModule{};
#if !defined( SW_SHIPPING )
        const hashed_string kAppConfigHash = hashed_string{ "AppConfig" };
        const AppConfig*    pAppConfig     = _engineLoop.getConfigManager()->ensureConfig<AppConfig>(
            kAppConfigHash, config::kFileRuntimeAppConfig, nullptr );
        if ( pAppConfig != nullptr )
            listGameKitModule = pAppConfig->_listGameKitModule;
#endif

        _moduleHost = make_unique<ModuleHost>();
        if ( _moduleHost->initialize( _engineLoop.getLiveReloadManager(),
                                      _engineLoop.getRHI(),
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

        // 모르는 `-gv_*` 키는 파서가 버리지 않고 보류표에 남긴다 — 모듈이 선언하는 변수는 파싱
        // 시점에 아직 없기 때문이다. 모듈이 다 올라온 지금까지도 가져간 임자가 없으면 오타다.
        // 표는 비우지 않는다: 핫 리로드로 나중에 올라오는 모듈이 여전히 가져갈 수 있다.
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

        // 루프 안에서 매 프레임 다시 만들던 것들이다. 바인딩 대상이 프레임마다 바뀌지 않으므로
        // 여기서 한 번 묶고, 에디터 뷰 카메라는 에디터 모드에서만 묶는다 — 비어 있다는 사실이
        // "씬 카메라를 쓴다" 는 뜻이라 루프에서 모드 분기를 할 필요가 없다.
        _forceReloadHandler = SW_DELEGATE_METHOD( Delegate<void( const utf8* )>, &App::onForceReload, this );
        if ( _bEnableEditor == SW_TRUE )
            _viewCameraProvider = SW_DELEGATE_METHOD( ViewCameraProviderDelegate, &App::getEditorViewCamera, this );

        _backendSwap.initialize( &_engineLoop, _moduleHost.get(), _bEnableEditor == SW_TRUE );

        _engineLoop.setPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorRender, this ) );
        _engineLoop.setPostPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorPostPresent, this ) );
    }

    void App::shutdown()
    {
        // 전역 변수 훅은 그 변수를 소유한 GlobalVariableManager(EngineLoop 소유)가 사라지기
        // 전에 떼어 낸다. 헤드리스 부팅처럼 연결되지 않은 경우에는 아무것도 하지 않는다.
        _backendSwap.shutdown();

        // 헤드리스 부팅은 창도 ModuleHost 도 만들지 않는다 — 아래 경로가 그대로 no-op 이므로
        // 모드 분기를 따로 두지 않는다.
        // NOTE: ModuleHost를 EngineLoop보다 먼저 종료해야 합니다.
        //       에디터 shutdown이 Game View RT를 해제할 때 RenderThread와 RHI Device를 사용합니다.
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
            // App이 소유권을 가진 활성 윈도우를 파괴하기 전에 전역 포인터를 먼저 끊습니다(댕글링 방지).
            if ( IWindow::getActiveWindow() == _window.get() )
                IWindow::setActiveWindow( nullptr );
            _window->destroy();
            _window.reset();
        }
    }

    void App::run()
    {
        // 루프는 창이 있어야 돈다. 헤드리스 부팅(예: --bake-shaders)은 창을 만들지 않으므로
        // 여기서 끝난다 — 모드 플래그가 아니라 실제 선행 조건으로 적는다.
        if ( _window == nullptr )
            return;

        SW_LOG_INFO( "Entering App Main Loop (Thin Launcher)..." );

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
            // 에디터 Play/Pause 상태를 여기서 한 번 래치한다 — 아래 고정 스텝이 여러 번 돌아도
            // DLL 경계를 넘어 다시 묻지 않고, 모든 단계가 같은 답을 본다.
            _moduleHost->beginFrame();

            pollReloadHotkeys( frameTime._deltaTime );

            for ( uint32 stepIndex = 0; stepIndex < frameTime._fixedStepCount; ++stepIndex )
                _moduleHost->fixedUpdateGame( frameTime._fixedDeltaTime );

            _moduleHost->updateGame( frameTime._deltaTime );
            // 에디터가 없으면 즉시 반환한다. 이 호출이 게임 뷰포트 RT 와 씬 틱 여부를 확정한다.
            _moduleHost->updateEditorUI( frameTime._deltaTime );

            // 카메라 포인터를 미리 잡아두면 tick 내부의 씬 전환/핫리로드가 그 GameObject 를
            // 파괴한 뒤 역참조하게 된다. 조회 자체를 tick 안으로 넘긴다.
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
        _engineLoop.pollDebugHotkeys( _forceReloadHandler );

        const bool bReloadEditorRequested = _bEnableEditor == SW_TRUE && _engineLoop.wasDebugActionTriggered( ActionMapDefaults::kReloadEditorAction );
        if ( bReloadEditorRequested )
        {
            onForceReload( config::kTargetEditorModule );
            SW_LOG_INFO( "%#: force EditorModule reload", ActionMapDefaults::kReloadEditorAction );
        }
#endif
        // Shipping 에는 리로드할 모듈이 없다. 예전에는 셸 ActionMap 을 리소스에서 올려 매 프레임
        // 갱신했지만 그 입력 상태를 질의하는 코드가 하나도 없었다 — 배포 빌드에서 아무도 읽지
        // 않는 입력을 계속 돌리던 자리다.
    }

    void App::onResize( const uint32 width, const uint32 height )
    {
        RHI* pRHI = _engineLoop.getRHI();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
            return;

        // gv_useRenderThread(기본 true)일 때는 전용 RenderThread가 별도 스레드에서 beginFrame/
        // endFrame으로 스왑체인·렌더타겟을 계속 건드리고 있을 수 있다. 이 상태에서 메인(윈도우
        // 메시지) 스레드가 바로 resize()를 호출하면 같은 리소스에 대한 진짜 크로스스레드 레이스가
        // 된다 — waitIdle()로 큐를 비우고 GPU까지 완전히 쉬게 한 뒤 리사이즈한다.
        RenderThread* pRenderThread = _engineLoop.getRenderThread();
        if ( pRenderThread != nullptr )
            pRenderThread->waitIdle();

        pRHI->getDevice().resize( width, height );
    }

    bool App::onWindowMessage( const NativeWindowEvent& event )
    {
        // 이벤트를 ModuleHost(ImGui 등)로 먼저 보냄
        const bool bConsumedByEditor = ( _moduleHost != nullptr && _moduleHost->onWindowMessage( event ) );

        // 에디터가 가로채지 않은 경우에만 게임 InputManager로 전달
        if ( bConsumedByEditor == false && engine::areEngineServicesBound() )
            engine::getInputManager().processNativeEvent( event );

        // Win32 OS 레벨 포커스/활성화(DefWindowProc)가 정상 동작하도록 false 반환
        return false;
    }

    CameraComponent* App::getEditorViewCamera()
    {
        return _moduleHost != nullptr ? _moduleHost->getViewportCamera() : nullptr;
    }

    void App::onForceReload( const utf8* pModuleName )
    {
        LiveReloadManager* const pLiveReloadManager = _engineLoop.getLiveReloadManager();
        if ( pLiveReloadManager == nullptr )
            return;

        pLiveReloadManager->triggerReload( pModuleName );
    }

    void App::onEditorRender( IRHIDevice& renderDevice, const RenderFramePacket& /*framePacket*/ )
    {
        // 이 훅은 렌더 스레드가 부른다. ModuleHost 가 사라진 뒤에는 불리지 않는다 —
        // shutdown 이 ModuleHost 를 지우기 전에 drainRenderWorkers 로 큐를 비우고, 그 시점엔
        // 메인 루프가 이미 끝나 새 프레임이 들어오지 않는다. 훅 델리게이트 자체를 여기서 끊는
        // 것은 오히려 위험하다: RenderThread::setPresentHook 은 잠금 없는 대입이라 렌더 스레드가
        // 도는 중에 바꾸면 레이스가 된다(실제로 종료 시 크래시했다). 아래 검사는 그 불변식이
        // 깨졌을 때 죽는 대신 조용히 넘어가기 위한 것이다.
        if ( _moduleHost == nullptr )
            return;

        const EditorHandle pEditor = _moduleHost->getEditor();
        if ( pEditor == nullptr )
            return;

        const EditorAPI& editorAPI = _moduleHost->getEditorAPI();
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

        const EditorAPI& editorAPI = _moduleHost->getEditorAPI();
        if ( editorAPI.postPresent != nullptr )
            editorAPI.postPresent( pEditor, &renderDevice );
    }
} // namespace sw
