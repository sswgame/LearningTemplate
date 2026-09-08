#include "pch.h"

#include "App/App.h"

#include "App/AppConfig.h"
#include "App/Module/ModuleHost.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Time/CpuTimer.h"

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
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Module/LiveReloadManager.h"
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
        , _maxFrameDeltaTime{ 0.1f }
        , _bEnableEditor{ SW_FALSE }
        , _bHandlingRhiBackendChange{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    App::~App() = default;

    bool App::initialize( int32 argc, utf8* pArgv[] )
    {
        ResourceUtil::initialize();

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

        _maxFrameDeltaTime = pEngineConfig->_maxFrameDeltaTime;

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

    void App::bindHostCallbacks()
    {
        _window->setResizeCallback( SW_DELEGATE_METHOD( WindowResizeDelegate, &App::onResize, this ) );
        _window->setCustomMessageHandler( SW_DELEGATE_METHOD( WindowMessageHandlerDelegate, &App::onWindowMessage, this ) );

        GlobalVariableInfo* pRHIBackendVar = engine::getGlobalVariableManager().findVariable( "gv_rhiBackend" );
        if ( pRHIBackendVar != nullptr )
            pRHIBackendVar->_onValueChanged = SW_DELEGATE_METHOD( GlobalVariableChangedDelegate, &App::onRhiBackendChanged, this );

        _engineLoop.setPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorRender, this ) );
        _engineLoop.setPostPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorPostPresent, this ) );
    }

    void App::shutdown()
    {
        if ( _engineLoop.isHeadless() )
        {
            _engineLoop.shutdown();
            return;
        }

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
        if ( _engineLoop.isHeadless() )
            return;

        SW_LOG_INFO( "Entering App Main Loop (Thin Launcher)..." );

        CpuTimer frameTimer;
        frameTimer.resetTimer();
        frameTimer.startTimer();

        float32           accumulator     = 0.0f;
        constexpr float32 kFixedDeltaTime = 1.0f / 60.0f;

        while ( _window->processMessages() )
        {
            // 프로파일 실행(-gv_profileFrames=N)은 목표 프레임을 채우면 스스로 끝난다.
            if ( _engineLoop.wantsQuit() )
            {
                _window->requestClose();
                break;
            }

            frameTimer.updateTimer();
            const float32 deltaTime = MathUtil::min( frameTimer.getDeltaTime(), _maxFrameDeltaTime );
            accumulator += deltaTime;

            _engineLoop.beginFrame();

            pollReloadHotkeys( deltaTime );

            while ( accumulator >= kFixedDeltaTime )
            {
                _moduleHost->fixedUpdateGame( kFixedDeltaTime );
                accumulator -= kFixedDeltaTime;
            }

            _moduleHost->updateGame( deltaTime );

            if ( _bEnableEditor == SW_TRUE )
                _moduleHost->updateEditorUI( deltaTime );

            uint64 gameRenderTarget   = 0;
            uint32 gameViewportWidth  = 0;
            uint32 gameViewportHeight = 0;
            _moduleHost->getGameViewport( gameRenderTarget, gameViewportWidth, gameViewportHeight );
            // 카메라 포인터를 미리 잡아두면 tick 내부의 씬 전환/핫리로드가 그 GameObject 를
            // 파괴한 뒤 역참조하게 된다. 조회 자체를 tick 안으로 넘긴다.
            ViewCameraProviderDelegate viewCameraProvider{};
            if ( _bEnableEditor == SW_TRUE )
                viewCameraProvider = SW_DELEGATE_METHOD( ViewCameraProviderDelegate, &App::getEditorViewCamera, this );
            const bool bTickScene = _moduleHost->shouldTickScene();
            _engineLoop.tick( deltaTime, gameRenderTarget, gameViewportWidth, gameViewportHeight, viewCameraProvider, bTickScene );
            if ( _bEnableEditor == SW_TRUE )
                _moduleHost->endEditorFrame();

            applyBackendChangeIfPending();

            _engineLoop.endFrame();
        }
    }

    void App::pollReloadHotkeys( float32 deltaTime )
    {
        _engineLoop.updateShellActions( deltaTime );
        _engineLoop.pollDebugHotkeys( SW_DELEGATE_METHOD( Delegate<void( const utf8* )>, &App::onForceReload, this ) );

        if ( _bEnableEditor == SW_TRUE && _engineLoop.wasDebugActionTriggered( ActionMapDefaults::kReloadEditorAction ) )
        {
            onForceReload( config::kTargetEditorModule );
            SW_LOG_INFO( "%#: force EditorModule reload", ActionMapDefaults::kReloadEditorAction );
        }
    }

    void App::applyBackendChangeIfPending()
    {
        const RHI* pRHI = _engineLoop.getRHI();
        if ( pRHI == nullptr || pRHI->hasPendingBackendChange() == false )
            return;

        if ( applyPendingBackendChange() == false )
        {
            SW_LOG_ERROR( "Backend soft-recreate failed." );
            // 되돌림 대입은 onRhiBackendChanged 를 다시 부르지만, 커밋된 백엔드와 같은 값이면
            // RHI::schedulePendingBackendChange 가 no-op 이라 재시도 루프가 되지 않는다.
            gv_rhiBackend = pRHI->getCommittedBackend();
        }
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
        {
            engine::getInputManager().processNativeEvent( event );
        }

        // Win32 OS 레벨 포커스/활성화(DefWindowProc)가 정상 동작하도록 false 반환
        return false;
    }

    CameraComponent* App::getEditorViewCamera()
    {
        return _moduleHost != nullptr ? _moduleHost->getViewportCamera() : nullptr;
    }

    void App::onRhiBackendChanged( const GlobalVariableInfo* pInfo )
    {
        RHI* pRHI = _engineLoop.getRHI();
        if ( pInfo == nullptr || pRHI == nullptr )
            return;

        // 아래에서 gv_rhiBackend 로 되돌림 대입을 하면 이 콜백이 다시 불린다.
        // 되돌림 대상 자체가 사용 불가/에디터 미지원이면 무한 재귀가 되므로 재진입을 막는다.
        if ( _bHandlingRhiBackendChange == SW_TRUE )
            return;
        _bHandlingRhiBackendChange = SW_TRUE;

        const RHIBackend requestedBackend = static_cast<RHIBackend>( pInfo->getValueAsInt() );
        if ( _bEnableEditor == SW_TRUE && RHIAvailability::query( requestedBackend )._bEditorSupported == false )
        {
            SW_LOG_WARNING( "Backend %# is not editor-supported — reverting.", RHI::getBackendTypeName( requestedBackend ) );
            gv_rhiBackend = pRHI->getCommittedBackend();
        }
        else
        {
            pRHI->schedulePendingBackendChange( requestedBackend );
        }

        _bHandlingRhiBackendChange = SW_FALSE;
    }

    bool App::applyPendingBackendChange()
    {
        if ( _moduleHost == nullptr )
            return false;

        void* pEditorModule{ nullptr };
        void* pGameModule{ nullptr };
#if !defined( SW_SHIPPING )
        const LiveReloadManager* pLiveReloadManager = _engineLoop.getLiveReloadManager();
        if ( pLiveReloadManager != nullptr )
        {
            pEditorModule = pLiveReloadManager->getModuleHandle( sw::config::kTargetEditorModule );
            pGameModule   = pLiveReloadManager->getModuleHandle( sw::config::kTargetGameModule );
        }
#endif

        _moduleHost->drainRenderWorkers();
        _moduleHost->onBeforeRhiSwap();

        const bool bSwapOk = _engineLoop.applyPendingBackendChange();
        RHI*       pRHI    = _engineLoop.getRHI();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
        {
            SW_LOG_ERROR( "applyPendingBackendChange 실패 — RHI 디바이스가 없어 모듈을 재생성하지 않습니다." );
            return false;
        }

        const bool bReinitOk = _moduleHost->reinitializeAfterRhiSwap( pEditorModule, pGameModule );
        if ( bReinitOk == false )
            SW_LOG_ERROR( "reinitializeAfterRhiSwap 실패." );
        if ( bSwapOk == false )
            SW_LOG_ERROR( "applyPendingBackendChange 실패 — 이전 백엔드로 복구한 뒤 모듈을 재생성했습니다." );
        return bSwapOk && bReinitOk;
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
