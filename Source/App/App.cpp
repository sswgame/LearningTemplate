/**
 * @file App.cpp
 * @brief App orchestrator — initialize / run / shutdown
 */
#include "App.h"

#include "Core/Common/CoreServices.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/CommandLine/CommandLineManager.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Utility/Task/TaskManager.h"
#include "Core/Utility/Memory/MemoryLeakDetector.h"
#include "Core/Utility/Module/LiveReloadManager.h"
#include "Core/Utility/File/ReloadFileManager.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Graphics/RHI/RHI.h"
#include "Core/Graphics/RHI/RHICapabilities.h"
#include "Core/Graphics/Shader/ShaderCache.h"
#include "Core/Window/IWindow.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Game/GameState.h"
#include "Core/Input/InputManager.h"
#include "Core/Graphics/RenderPass/FrameRenderer.h"
#include "Core/Utility/Time/EngineTimer.h"
#include "Core/Object/ComponentManager.h"
#include "Core/Game/GameState.h"
#include "AppState.h"

#if defined( SW_SHIPPING )
	#include "Runtime/GameAPI.h"
#endif

namespace sw
{
	SW_EXTERN_GLOBAL_VARIABLE_FLOAT( gv_EditorPlayerSpeed );
	SW_GLOBAL_VARIABLE_STRING( kEditorModuleName, "EditorModule", "에디터 모듈 네임" );
#if !defined( SW_SHIPPING )
	SW_GLOBAL_VARIABLE_STRING( kGameModuleName, "SWGame", "게임 모듈 네임" );
#endif

	App::App()
		: _bEnableEditor{ 0 }
		, _bAppRunning{ 0 }
		, _bPendingBackendChange{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	App::~App() = default;

	bool App::initialize( int32 argc, utf8* argv[] )
	{
		enableMemoryLeakChecks();
		if ( initializeSubsystems( argc, argv ) == false )
			return false;

		if ( _bEnableEditor )
		{
			BLOCK( "에디터: 셰이더 리플렉션 / 뷰포트 / EditorModule 등록" )
			{
				ShaderCompileDesc vsDesc{};
				vsDesc._filePath	 = "Shaders/BindlessTriangle.hlsl";
				vsDesc._entryPoint	 = "VSMain";
				vsDesc._stage		 = ShaderStage::Vertex;
				vsDesc._targetFormat = RHI::getShaderTargetFormat( gv_RHIBackend );

				const ShaderCompileResult vsCompileResult = ShaderCache::getOrCompile( vsDesc );
				_reflectionData							  = ShaderReflection::reflect( vsCompileResult._bytecode, vsDesc._targetFormat );

#if defined( SW_DEBUG )
				ShaderRecompiledDelegate shaderReloadDelegate = SW_DELEGATE_LAMBDA( ShaderRecompiledDelegate, []( const std::string& path, const ShaderCompileResult& result )
				{
					SW_LOG_INFO( "[LiveShaderNotify] Shader recompiled successfully live! Path: %# (Bytecode size: %# bytes)",
								 path.c_str(), static_cast<uint32>( result._bytecode.size() ) );
				} );
				_rhi->getLiveShaderManager().watchShader( vsDesc, shaderReloadDelegate );

				const std::string shaderWatchRoot = ResourceUtil::getRootFolderPath();
				if ( shaderWatchRoot.empty() == false && _reloadFileManager != nullptr )
				{
					FileWatchMatchDelegate fileWatchDelegate = SW_DELEGATE_LAMBDA( FileWatchMatchDelegate, [this]( const FileChangeEvent& ev )
					{
						const std::string fullPath = FileUtil::normalizePath( ev._directory + "/" + ev._filename );
						_rhi->getLiveShaderManager().notifyFileChanged( fullPath );
					} );
					_shaderWatchHandle = _reloadFileManager->registerWatch( shaderWatchRoot, { ".hlsl", ".hlsli" }, fileWatchDelegate );
				}
#endif

				_liveReloadManager->setOnBeforeReload( kEditorModuleName, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &App::onBeforeEditorReload, this ) );
				_liveReloadManager->setOnAfterReload( kEditorModuleName, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &App::onAfterEditorReload, this ) );
				if ( _liveReloadManager->registerModule( kEditorModuleName ) == false )
				{
					SW_LOG_ERROR( "Editor Module 로드에 실패했습니다." );
					return false;
				}

				createGameViewportTexture();
			}
		}

		BLOCK( "메인 씬 생성 / FrameRenderer" )
		{
			_sceneManager->setRhiDevice( &_rhi->getDevice() );
			if ( _frameRenderer )
			{
				_frameRenderer->initialize( &_rhi->getDevice(), "Engine/RenderPass/ForwardPipeline.xml" );
				_sceneManager->setFrameRenderer( _frameRenderer.get() );
			}

			if ( Scene* activeScene = _sceneManager->createScene( "MainScene" ) )
			{
				activeScene->initialize( &_rhi->getDevice() );
				if ( _frameRenderer )
					activeScene->setFrameRenderer( _frameRenderer.get() );
			}
		}

		BLOCK( "Game Module 등록 / 초기화" )
		{
#if defined( SW_SHIPPING )
			if ( bindGameAPI( nullptr ) == false )
				return false;
			onAfterGameReload( nullptr );
#else
			_liveReloadManager->setOnBeforeReload( kGameModuleName, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &App::onBeforeGameReload, this ) );
			_liveReloadManager->setOnAfterReload( kGameModuleName, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &App::onAfterGameReload, this ) );

			if ( _liveReloadManager->registerModule( kGameModuleName ) == false )
			{
				SW_LOG_ERROR( "[App] SWGame module register failed" );
				return false;
			}
#endif

			// App flow starts at Splash; GameState Playing is armed after Title.
			setAppState( AppState::Splash );
			_splashTimer = 0.0f;
			if ( _bEnableEditor == false )
				setGameState( GameState::Stopped );
		}

		BLOCK( "윈도우 콜백 / RHI Hot-Swap 훅 설정" )
		{
			_window->setResizeCallback( SW_DELEGATE_METHOD( WindowResizeDelegate, &App::onResize, this ) );
			_window->setCustomMessageHandler( SW_DELEGATE_METHOD( WindowMessageHandlerDelegate, &App::onWindowMessage, this ) );

			_bAppRunning		   = true;
			_bPendingBackendChange = false;
			_committedRHIBackend   = gv_RHIBackend;
			_pendingRHIBackend	   = gv_RHIBackend;

			GlobalVariableInfo* pRHIBackendVar = core::getGlobalVariableManager().findVariable( "gv_RHIBackend" );
			if ( pRHIBackendVar != nullptr )
			{
				pRHIBackendVar->_onValueChanged = SW_DELEGATE_LAMBDA( GlobalVariableChangedDelegate, [this]( GlobalVariableInfo* info )
				{
					const RHIBackend requested = static_cast<RHIBackend>( info->getValueAsInt() );
					if ( RHIAvailability::isAvailable( requested ) == false )
					{
						SW_LOG_WARNING( "[Hot-Swap] Backend %# unavailable — reverting.", static_cast<int32>( requested ) );
						gv_RHIBackend = _committedRHIBackend;
						return;
					}

					if ( _bEnableEditor && RHIAvailability::query( requested )._bEditorSupported == false )
					{
						SW_LOG_WARNING( "[Hot-Swap] Backend %# is not editor-supported — reverting.", RHI::getBackendTypeName( requested ) );
						gv_RHIBackend = _committedRHIBackend;
						return;
					}

					if ( requested == _committedRHIBackend )
						return;

					// Defer until after endFrame so we never tear down mid-ImGui/DX submit.
					SW_LOG_INFO( "[Hot-Swap] RHI Backend change queued: %# → %#", RHI::getBackendTypeName( _committedRHIBackend ), RHI::getBackendTypeName( requested ) );
					_pendingRHIBackend	   = requested;
					_bPendingBackendChange = true;
				} );
			}
		}

		return true;
	}

	void App::run()
	{
		SW_LOG_INFO( "Entering App Main Loop..." );

		_editorCtx.playerSpeed				 = &gv_EditorPlayerSpeed;
		_editorCtx.clearColor				 = _clearColor;
		_editorCtx.reflectionData			 = &_reflectionData;
		_editorCtx.rhiDevice				 = &_rhi->getDevice();
		_editorCtx.gameTextureID			 = _gameTextureID;
		_editorCtx.gameViewportWidth		 = _gameViewportWidth;
		_editorCtx.gameViewportHeight		 = _gameViewportHeight;
		_editorCtx.requestGameViewportWidth	 = &_requestedGameViewportWidth;
		_editorCtx.requestGameViewportHeight = &_requestedGameViewportHeight;

		CpuTimer frameTimer;
		frameTimer.resetTimer();
		frameTimer.startTimer();

		while ( _bAppRunning && _window->processMessages() )
		{
			frameTimer.updateTimer();
			const float32 deltaTime = frameTimer.getDeltaTime();

			BLOCK( "Input beginFrame" )
			{
				if ( _inputManager )
					_inputManager->beginFrame();
			}

			BLOCK( "AppState Splash / Title" )
			{
				const AppState state = getAppState();
				if ( state == AppState::Splash )
				{
					_splashTimer += deltaTime;
					constexpr float32 kSplashSeconds = 1.25f;
					if ( _splashTimer >= kSplashSeconds )
					{
						setAppState( AppState::Title );
						SW_LOG_INFO( "[App] Title — Enter/Space=New Game, C=Continue." );
					}
				}
				else if ( state == AppState::Title )
				{
					InputManager& input = core::getInputManager();
					if ( input.wasKeyPressed( Key::Enter ) || input.wasKeyPressed( Key::Space ) )
					{
						setGameStartMode( GameStartMode::NewGame );
						if ( _bEnableEditor )
							setAppState( AppState::Editor );
						else
							setAppState( AppState::Playing );
						setGameState( GameState::Playing );
						SW_LOG_INFO( "[App] New Game — entering Playing / Editor." );
					}
					else if ( input.wasKeyPressed( Key::C ) )
					{
						setGameStartMode( GameStartMode::Continue );
						if ( _bEnableEditor )
							setAppState( AppState::Editor );
						else
							setAppState( AppState::Playing );
						setGameState( GameState::Playing );
						SW_LOG_INFO( "[App] Continue — entering Playing / Editor." );
					}
				}
			}

			BLOCK( "Game View RT 리사이즈 요청 처리" )
			{
				processGameViewportResizeRequest();
			}

			BLOCK( "핫 리로드 / 파일 워치 업데이트" )
			{
#if !defined( SW_SHIPPING )
				_liveReloadManager->update();
	#if defined( SW_DEBUG )
				_rhi->getLiveShaderManager().update();
	#endif
#endif
				_reloadFileManager->update();
			}

			BLOCK( "Task dispatch / Scene async transitions" )
			{
				_taskManager->dispatch();
				_sceneManager->tickTransitions();
			}

			BLOCK( "에디터 PreRender / 게임 Update" )
			{
				if ( _editor && _editorApi.preRender )
				{
					_editorCtx.gameTextureID	  = _gameTextureID;
					_editorCtx.gameViewportWidth  = _gameViewportWidth;
					_editorCtx.gameViewportHeight = _gameViewportHeight;
					_editorApi.preRender( _editor, &_rhi->getDevice() );
				}

				const AppState appState = getAppState();
				const bool	   bGameplay =
					( appState == AppState::Playing || appState == AppState::Editor )
					&& getGameState() == GameState::Playing;
				if ( _game && _gameApi.update && bGameplay )
					_gameApi.update( _game, deltaTime );
			}

			IRHIDevice& device		= _rhi->getDevice();
			Scene*		activeScene = _sceneManager->getActiveScene();

			BLOCK( "씬 렌더 (Game View / 백버퍼)" )
			{
				const bool bRenderToGameView = _bEnableEditor && _editor && _gameRenderTarget != 0;
				if ( bRenderToGameView )
				{
					device.beginOffscreenPass( _gameRenderTarget, _clearColor );
					if ( activeScene )
						activeScene->render( &device );
					device.endOffscreenPass( _gameRenderTarget );

					device.beginFrame( _clearColor );
				}
				else
				{
					device.beginFrame( _clearColor );
					if ( activeScene )
						activeScene->render( &device );
				}
			}

			BLOCK( "에디터 UI 렌더 / Present" )
			{
				if ( _editor && _editorApi.render )
				{
					_editorCtx.material			  = activeScene ? activeScene->getMaterial() : nullptr;
					_editorCtx.gameTextureID	  = _gameTextureID;
					_editorCtx.gameViewportWidth  = _gameViewportWidth;
					_editorCtx.gameViewportHeight = _gameViewportHeight;
					_editorApi.render( _editor, &_editorCtx );
				}

				device.endFrame( true );

				if ( _editor && _editorApi.postPresent )
					_editorApi.postPresent( _editor, &_rhi->getDevice() );
			}

			BLOCK( "대기 중인 RHI Backend Hot-Swap" )
			{
				if ( _bPendingBackendChange )
				{
					_bPendingBackendChange = false;
					if ( applyPendingBackendChange() == false )
					{
						SW_LOG_ERROR( "[Hot-Swap] Backend soft-recreate failed." );
						gv_RHIBackend = _committedRHIBackend;
					}
				}
			}

			BLOCK( "Input endFrame" )
			{
				if ( _inputManager )
					_inputManager->endFrame();
			}
		}

		if ( _window && _window->shouldClose() )
			_bAppRunning = false;
	}

	void App::shutdown()
	{
		BLOCK( "Game / Editor 인스턴스 정리" )
		{
			onBeforeGameReload();
			onBeforeEditorReload();
		}

		BLOCK( "Game Viewport / RHI / Window 종료" )
		{
			if ( _gameRenderTarget && _rhi && _rhi->getDevice().getNativeDevice() )
			{
				_rhi->getDevice().destroyTexture( _gameRenderTarget );
				_gameRenderTarget = 0;
				_gameTextureID	  = nullptr;
			}

			if ( _rhi )
				_rhi->getDevice().waitIdle();

			if ( _rhi )
				_rhi->shutdown();
			if ( _window )
				_window->destroy();
		}

		BLOCK( "매니저 종료 및 CoreServices 언바인드" )
		{
			if ( _frameRenderer )
				_frameRenderer->shutdown();
			if ( _sceneManager )
				_sceneManager->shutdown();
			if ( _inputManager )
				_inputManager->shutdown();
			if ( _reloadFileManager )
			{
				if ( _shaderWatchHandle.isValid() )
					_reloadFileManager->unregisterWatch( _shaderWatchHandle );
				_shaderWatchHandle = {};
				_reloadFileManager->shutdown();
			}
			if ( _liveReloadManager )
				_liveReloadManager->shutdown();
			if ( _componentManager )
				_componentManager->shutdown();
			if ( _taskManager )
				_taskManager->shutdown();
			if ( _globalVariableManager )
				_globalVariableManager->shutdown();

			IWindow::setActiveWindow( nullptr );
			core::unbindCoreServices();

			_frameRenderer.reset();
			_sceneManager.reset();
			_inputManager.reset();
			_reloadFileManager.reset();
			_liveReloadManager.reset();
			_componentManager.reset();
			_typeRegistry.reset();
			_globalVariableManager.reset();
			_taskManager.reset();
			_rhi.reset();
			_window.reset();
			_commandLineManager.reset();
			_logger.reset();
		}

		reportMemoryLeaks( "App::shutdown" );
	}
} // namespace sw
