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
#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RenderPass/RenderFramePacket.h"
#include "Engine/Graphics/RenderPass/RenderThread.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/Module/LiveReloadManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/NativeWindowEvent.h"

#include "RuntimeAPI/EditorAPI.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
	App::App()
		: _engineLoop{}
		, _moduleHost{ nullptr }
		, _window{ nullptr }
		, _maxFrameDeltaTime{ 0.1f }
		, _bEnableEditor{ false }
	{
	}

	App::~App() = default;

	bool App::initialize( int32 argc, utf8* pArgv[] )
	{
		// 1. 코어 매니저들은 모두 EngineLoop가 초기화
		if ( _engineLoop.initialize( argc, pArgv ) == false )
		{
			SW_LOG_ERROR( "EngineLoop initialization failed." );
			return false;
		}
		_engineLoop.pollDebugHotkeys( _bEnableEditor, SW_DELEGATE_METHOD( Delegate<void( const utf8* )>, &App::onForceReload, this ) );

		const ConfigManager*	  pConfigManager	  = _engineLoop.getConfigManager();
		const CommandLineManager* pCommandLineManager = _engineLoop.getCommandLineManager();
		if ( pConfigManager == nullptr || pCommandLineManager == nullptr )
			return false;

		const EngineConfig* pEngineConfig = pConfigManager->getConfig<EngineConfig>( hashed_string( "EngineConfig" ) );
		if ( pEngineConfig == nullptr )
			return false;

		_maxFrameDeltaTime = pEngineConfig->_maxFrameDeltaTime;

		uint32 width  = pEngineConfig->_window._width;
		uint32 height = pEngineConfig->_window._height;
		pCommandLineManager->getArgument( CommandLineArgument::WIDTH, width );
		pCommandLineManager->getArgument( CommandLineArgument::HEIGHT, height );

		// 2. 윈도우 소유권 획득
		_window.reset( IWindow::getActiveWindow() );
		if ( _window == nullptr )
		{
			_window = IWindow::createPlatformWindow();
			if ( _window == nullptr || _window->initializeWindow( pEngineConfig->_window._title.c_str(), width, height ) == false )
			{
				SW_LOG_ERROR( "Failed to create platform window!" );
				return false;
			}
			IWindow::setActiveWindow( _window.get() );
		}

		pCommandLineManager->getArgument( CommandLineArgument::ENABLE_EDITOR, _bEnableEditor );

		const RHICapabilities caps = RHIAvailability::query( gv_rhiBackend );
		if ( _bEnableEditor && caps._bEditorSupported == false )
		{
			SW_LOG_WARNING( "[App] Editor requested but backend %# does not set _bEditorSupported — disabling editor.", RHI::getBackendTypeName( gv_rhiBackend ) );
			_bEnableEditor = false;
		}

		// 3. ModuleHost (에디터/게임 라이프사이클) 바인딩
		vector<GameKitConfig> gameKitModuleList{};
#if !defined( SW_SHIPPING )
		const hashed_string kAppConfigHash = hashed_string{ "AppConfig" };
		const AppConfig*	pAppConfig	   = _engineLoop.getConfigManager()->ensureConfig<AppConfig>(
			kAppConfigHash, config::kFileRuntimeAppConfig, nullptr );
		if ( pAppConfig != nullptr )
			gameKitModuleList = pAppConfig->_gameKitModuleList;
#endif

		_moduleHost		   = make_unique<ModuleHost>();
		const bool bResult = _moduleHost->initialize( _engineLoop.getLiveReloadManager(),
													  _engineLoop.getRHI(),
													  _window.get(),
													  _engineLoop.getRenderThread(),
													  _bEnableEditor,
													  gameKitModuleList );
		if ( bResult == false )
		{
			SW_LOG_ERROR( "ModuleHost initialization failed." );
			return false;
		}

		if ( _bEnableEditor )
			_moduleHost->createGameViewportTexture( width, height );

		// 4. 윈도우 콜백 및 이벤트 라우팅 설정
		_window->setResizeCallback( SW_DELEGATE_METHOD( WindowResizeDelegate, &App::onResize, this ) );
		_window->setCustomMessageHandler( SW_DELEGATE_METHOD( WindowMessageHandlerDelegate, &App::onWindowMessage, this ) );

		GlobalVariableInfo* pRHIBackendVar = engine::getGlobalVariableManager().findVariable( "gv_rhiBackend" );
		if ( pRHIBackendVar != nullptr )
			pRHIBackendVar->_onValueChanged = SW_DELEGATE_METHOD( GlobalVariableChangedDelegate, &App::onRhiBackendChanged, this );

		_engineLoop.setPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorRender, this ) );
		_engineLoop.setPostPresentHook( SW_DELEGATE_METHOD( PresentHookDelegate, &App::onEditorPostPresent, this ) );

		return true;
	}

	void App::shutdown()
	{
		// NOTE: ModuleHost를 EngineLoop보다 먼저 종료해야 합니다.
		//       ModuleHost::destroyGameViewportTexture()가 내부에서 RenderThread와 RHI Device를 사용하기 때문입니다.
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
			_window->destroy();
			_window.reset();
		}
	}

	void App::run()
	{
		SW_LOG_INFO( "Entering App Main Loop (Thin Launcher)..." );

		CpuTimer frameTimer;
		frameTimer.resetTimer();
		frameTimer.startTimer();

		float32			  accumulator	  = 0.0f;
		constexpr float32 kFixedDeltaTime = 1.0f / 60.0f;

		while ( _window->processMessages() )
		{
			frameTimer.updateTimer();
			const float32 deltaTime = MathUtil::min( frameTimer.getDeltaTime(), _maxFrameDeltaTime );
			accumulator += deltaTime;

			_engineLoop.beginFrame();

			_engineLoop.updateShellActions( deltaTime );
			_moduleHost->processGameViewportResizeRequest();

			while ( accumulator >= kFixedDeltaTime )
			{
				_moduleHost->fixedUpdateGame( kFixedDeltaTime );
				accumulator -= kFixedDeltaTime;
			}

			_moduleHost->updateGame( deltaTime );

			if ( _bEnableEditor )
			{
				RenderThread* pRenderThread = _engineLoop.getRenderThread();
				if ( pRenderThread != nullptr )
					pRenderThread->waitIdle();

				const Scene* pHookScene = engine::areEngineServicesBound() ? engine::getSceneManager().getActiveScene() : nullptr;
				_moduleHost->setEditorGameMaterial( pHookScene != nullptr ? pHookScene->getMaterial() : nullptr );
				_moduleHost->updateEditorUI( deltaTime );
			}

			// 렌더 프레임 제출과 에디터 렌더 훅을 EngineLoop로 넘깁니다.
			const auto& [gameRenderTarget, gameViewportWidth, gameViewportHeight] = _moduleHost->getEditorViewportInfo();
			_engineLoop.tick( deltaTime, _bEnableEditor, gameRenderTarget, gameViewportWidth, gameViewportHeight );

			const RHI* pRHI = _engineLoop.getRHI();
			if ( pRHI != nullptr && pRHI->hasPendingBackendChange() )
			{
				if ( applyPendingBackendChange() == false )
				{
					SW_LOG_ERROR( "[Hot-Swap] Backend soft-recreate failed." );
					gv_rhiBackend = pRHI->getCommittedBackend();
				}
			}

			_engineLoop.endFrame();
		}
	}

	void App::onResize( const uint32 width, const uint32 height )
	{
		RHI* pRHI = _engineLoop.getRHI();
		if ( pRHI == nullptr || pRHI->hasDevice() == false )
			return;

		IRHISwapChain* pSwapChain = pRHI->getDevice().getSwapChain();
		if ( pSwapChain != nullptr )
			pSwapChain->resize( width, height );
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

	void App::onRhiBackendChanged( const GlobalVariableInfo* pInfo )
	{
		RHI* pRHI = _engineLoop.getRHI();
		if ( pInfo == nullptr || pRHI == nullptr )
			return;

		const RHIBackend requestedBackend = static_cast<RHIBackend>( pInfo->getValueAsInt() );

		if ( _bEnableEditor && RHIAvailability::query( requestedBackend )._bEditorSupported == false )
		{
			SW_LOG_WARNING( "[Hot-Swap] Backend %# is not editor-supported — reverting.", RHI::getBackendTypeName( requestedBackend ) );
			gv_rhiBackend = pRHI->getCommittedBackend();
			return;
		}

		pRHI->schedulePendingBackendChange( requestedBackend );
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
			pGameModule	  = pLiveReloadManager->getModuleHandle( sw::config::kTargetGameModule );
		}
#endif

		_moduleHost->drainRenderWorkers();
		_moduleHost->onBeforeRhiSwap();
		_moduleHost->destroyGameViewportTexture();

		const bool bSuccess = _engineLoop.applyPendingBackendChange();
		if ( bSuccess == false )
		{
			SW_LOG_ERROR( "[App] applyPendingBackendChange 실패 — reinitializeAfterRhiSwap을 건너뜁니다." );
			return false;
		}

		_moduleHost->reinitializeAfterRhiSwap( pEditorModule, pGameModule );
		return true;
	}

	void App::onForceReload( const utf8* pModuleName )
	{
		LiveReloadManager* const pLiveReloadManager = _engineLoop.getLiveReloadManager();
		if ( pLiveReloadManager == nullptr )
			return;

		pLiveReloadManager->triggerReload( pModuleName );
	}

	void App::onEditorRender( IRHIDevice& renderDevice, const RenderFramePacket& framePacket )
	{
		std::ignore = framePacket;

		const EditorHandle pEditor = _moduleHost->getEditor();
		if ( pEditor == nullptr )
			return;

		const EditorAPI& editorAPI = _moduleHost->getEditorAPI();
		if ( editorAPI.preRender != nullptr )
			editorAPI.preRender( pEditor, &renderDevice );

		if ( editorAPI.render != nullptr )
			editorAPI.render( pEditor, &renderDevice );
	}

	void App::onEditorPostPresent( IRHIDevice& renderDevice, const RenderFramePacket& framePacket )
	{
		std::ignore = framePacket;

		const EditorHandle pEditor = _moduleHost->getEditor();
		if ( pEditor == nullptr )
			return;

		const EditorAPI& editorAPI = _moduleHost->getEditorAPI();
		if ( editorAPI.postPresent != nullptr )
			editorAPI.postPresent( pEditor, &renderDevice );
	}
} // namespace sw
