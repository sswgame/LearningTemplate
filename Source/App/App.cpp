/**
 * @file App.cpp
 * @brief 런타임 클라이언트 앱 구현
 */
#include "App.h"

#include "Core/Common/CoreServices.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/CommandLine/CommandLineManager.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Utility/Task/TaskManager.h"
#include "Core/Utility/Module/LiveReloadManager.h"
#include "Core/Utility/File/ReloadFileManager.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Object/ComponentManager.h"
#include "Core/Graphics/RHI/RHI.h"
#include "Core/Graphics/RHI/RHICapabilities.h"
#include "Core/Graphics/Shader/ShaderCache.h"
#include "Core/Window/IWindow.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Graphics/Material/Material.h"
#include "Core/Game/GameState.h"
#include "Core/Utility/Time/EngineTimer.h"
#include "Core/Window/NativeWindowEvent.h"

#if defined( SW_SHIPPING )
	#include "Runtime/GameAPI.h"
#endif

// GVM / 커맨드라인 샘플 (엔진 코어가 직접 소비하지 않음 — GlobalVariables 윈도우용)
SW_GLOBAL_VARIABLE_BOOL( gv_EnableVSync, true, "Enable Vertical Synchronization (VSync)" );
SW_GLOBAL_VARIABLE_INT( gv_MaxFPS, 60, "Maximum Framerate Limit" );
SW_GLOBAL_VARIABLE_FLOAT( gv_CameraFOV, 90.0f, "Main Camera Field of View in Degrees" );
SW_GLOBAL_VARIABLE_STRING( gv_PlayerName, "Player1", "Active Player Name" );

// EditorUIContext에 넘기는 에디터 상태 (App 멤버 대신 GVM)
SW_GLOBAL_VARIABLE_FLOAT( gv_EditorPlayerSpeed, 5.0f, "Editor inspector player speed slider" );

namespace sw
{
	namespace
	{
		constexpr const utf8* kEditorModuleName = "EditorModule";
#if !defined( SW_SHIPPING )
		constexpr const utf8* kGameModuleName = "SWGame";
#endif
	}

	App::App()	= default;
	App::~App() = default;

	bool App::initializeSubsystems( int argc, char* argv[] )
	{
		_logger = std::make_unique<Logger>();
		_logger->startup();

		_commandLineManager = std::make_unique<CommandLineManager>();
		_commandLineManager->initialize();

		_globalVariableManager = std::make_unique<GlobalVariableManager>();
		_globalVariableManager->initialize();
		_globalVariableManager->registerPendingVariables( "App", GlobalVariableRegistrar::getHead() );
		_globalVariableManager->registerToCommandLine( _commandLineManager.get() );
		_commandLineManager->parse( argc, argv );
		_globalVariableManager->updateFromCommandLine( _commandLineManager.get() );

		_taskManager	   = std::make_unique<TaskManager>();
		_typeRegistry	   = std::make_unique<TypeRegistry>();
		_componentManager  = std::make_unique<ComponentManager>();
		_liveReloadManager = std::make_unique<LiveReloadManager>();
		_reloadFileManager = std::make_unique<ReloadFileManager>();
		_sceneManager	   = std::make_unique<SceneManager>();

		CoreServices services{};
		services.commandLineManager	   = _commandLineManager.get();
		services.globalVariableManager = _globalVariableManager.get();
		services.taskManager		   = _taskManager.get();
		services.typeRegistry		   = _typeRegistry.get();
		services.componentManager	   = _componentManager.get();
		services.sceneManager		   = _sceneManager.get();
		bindCoreServices( services );

		registerCoreReflectionTypes();

		if ( _taskManager->initialize() == false )
			return false;
		if ( _liveReloadManager->initialize() == false )
			return false;

		if ( ResourceUtil::initialize() == false )
		{
			SW_LOG_ERROR( "Failed to initialize ResourceUtil!" );
			return false;
		}

		if ( _reloadFileManager->initialize() == false )
			return false;
		if ( _sceneManager->initialize() == false )
			return false;

		_window = IWindow::createPlatformWindow();
		if ( _window == nullptr || _window->create( L"Toy Engine Editor (Live Coding + Hot Reloading + Multi-Backend RHI)", 1280, 720 ) == false )
		{
			SW_LOG_ERROR( "Failed to create platform window!" );
			return false;
		}
		IWindow::setActiveWindow( _window.get() );

		_rhi = std::make_unique<RHI>();
		if ( _rhi->initialize() == false )
			return false;

		bool bEnableEditor = false;
		_commandLineManager->getArgument( CommandLineArgument::ENABLE_EDITOR, bEnableEditor );
		_bEnableEditor = bEnableEditor ? 1 : 0;
		return true;
	}

	bool App::createGameViewportTexture()
	{
		RHITextureDesc rtDesc{};
		rtDesc._width			  = 1280;
		rtDesc._height			  = 720;
		rtDesc._format			  = RHIFormat::R8G8B8A8_UNORM;
		rtDesc._bIsRenderTarget	  = true;
		rtDesc._bIsShaderResource = true;
		rtDesc._mipLevels		  = 1;
		rtDesc._clearColor[0]	  = 0.1f;
		rtDesc._clearColor[1]	  = 0.1f;
		rtDesc._clearColor[2]	  = 0.1f;
		rtDesc._clearColor[3]	  = 1.0f;

		_gameRenderTarget = _rhi->getDevice().createTexture2D( rtDesc );
		return _gameRenderTarget != 0;
	}

	bool App::applyPendingBackendChange()
	{
		const RHIBackend requested = _pendingRHIBackend;
		const RHIBackend previous  = _committedRHIBackend;
		if ( requested == previous )
			return true;

		if ( _rhi == nullptr )
			return false;

		void* editorModule = nullptr;
		void* gameModule   = nullptr;
#if !defined( SW_SHIPPING )
		if ( _liveReloadManager )
		{
			editorModule = _liveReloadManager->getModuleHandle( kEditorModuleName );
			gameModule	 = _liveReloadManager->getModuleHandle( kGameModuleName );
		}
#endif

		SW_LOG_INFO( "[Hot-Swap] Soft-recreating RHI: %# → %#",
					 RHI::getBackendTypeName( previous ),
					 RHI::getBackendTypeName( requested ) );

		_rhi->getDevice().waitIdle();

		onBeforeEditorReload();
		onBeforeGameReload();

		if ( _gameRenderTarget != 0 )
		{
			_rhi->getDevice().destroyTexture( _gameRenderTarget );
			_gameRenderTarget = 0;
			_gameTextureID	  = nullptr;
		}

		if ( Scene* scene = _sceneManager ? _sceneManager->getActiveScene() : nullptr )
		{
			if ( Material* material = scene->getMaterial() )
				material->shutdown( &_rhi->getDevice() );
		}

		_rhi->getDevice().waitIdle();

		gv_RHIBackend = requested;
		if ( _rhi->recreateDevice( requested ) == false )
		{
			SW_LOG_ERROR( "[Hot-Swap] recreateDevice(%#) failed — restoring %#",
						  RHI::getBackendTypeName( requested ),
						  RHI::getBackendTypeName( previous ) );
			gv_RHIBackend = previous;
			if ( _rhi->recreateDevice( previous ) == false )
				return false;
		}
		else
		{
			_committedRHIBackend = requested;
		}

		if ( Scene* scene = _sceneManager ? _sceneManager->getActiveScene() : nullptr )
		{
			if ( scene->initialize( &_rhi->getDevice() ) == false )
				SW_LOG_ERROR( "[Hot-Swap] Scene re-initialize failed after backend change." );
		}

		if ( _bEnableEditor )
		{
			if ( createGameViewportTexture() == false )
				SW_LOG_WARNING( "[Hot-Swap] Game viewport texture recreate failed." );
			onAfterEditorReload( editorModule );
		}

#if defined( SW_SHIPPING )
		onAfterGameReload( nullptr );
#else
		onAfterGameReload( gameModule );
#endif

		_editorCtx.rhiDevice	 = &_rhi->getDevice();
		_editorCtx.gameTextureID = _gameTextureID;

		SW_LOG_INFO( "[Hot-Swap] Active backend is now %#", RHI::getBackendTypeName( _committedRHIBackend ) );
		return true;
	}

	bool App::bindEditorAPI( void* hLibraryModule )
	{
		_editorApi = {};
		if ( hLibraryModule == nullptr )
			return false;

		auto pfnFill = reinterpret_cast<PFN_FillEditorAPI>( FileUtil::getDynamicSymbol( hLibraryModule, "fillEditorAPI" ) );
		if ( pfnFill == nullptr || pfnFill( &_editorApi ) == false )
		{
			SW_LOG_ERROR( "[App] Failed to bind EditorAPI from module" );
			return false;
		}
		return _editorApi.create != nullptr && _editorApi.destroy != nullptr;
	}

	bool App::bindGameAPI( void* hLibraryModule )
	{
		_gameApi = {};
#if defined( SW_SHIPPING )
		(void)hLibraryModule;
		if ( fillGameAPI( &_gameApi ) == false )
		{
			SW_LOG_ERROR( "[App] Failed to bind GameAPI (shipping)" );
			return false;
		}
#else
		if ( hLibraryModule == nullptr )
			return false;
		auto pfnFill = reinterpret_cast<PFN_FillGameAPI>( FileUtil::getDynamicSymbol( hLibraryModule, "fillGameAPI" ) );
		if ( pfnFill == nullptr || pfnFill( &_gameApi ) == false )
		{
			SW_LOG_ERROR( "[App] Failed to bind GameAPI from module" );
			return false;
		}
#endif
		return _gameApi.create != nullptr && _gameApi.destroy != nullptr;
	}

	bool App::initialize( int argc, char* argv[] )
	{
		if ( initializeSubsystems( argc, argv ) == false )
			return false;

		GlobalVariableInfo* pRHIBackendVar = getGlobalVariableManager().findVariable( "gv_RHIBackend" );

		if ( _bEnableEditor )
		{
			ShaderCompileDesc vsDesc{};
			vsDesc._filePath	 = "Shaders/BindlessTriangle.hlsl";
			vsDesc._entryPoint	 = "VSMain";
			vsDesc._stage		 = ShaderStage::Vertex;
			vsDesc._targetFormat = RHI::getShaderTargetFormat( gv_RHIBackend );

			const ShaderCompileResult vsCompileResult = ShaderCache::getOrCompile( vsDesc );
			_reflectionData							  = ShaderReflection::reflect( vsCompileResult._bytecode, vsDesc._targetFormat );

#if defined( SW_DEBUG )
			auto shaderReloadDelegate = SW_DELEGATE_LAMBDA( ShaderRecompiledDelegate,
															[]( const std::string& path, const ShaderCompileResult& result )
			{
				SW_LOG_INFO( "[LiveShaderNotify] Shader recompiled successfully live! Path: %# (Bytecode size: %# bytes)",
							 path.c_str(), static_cast<uint32>( result._bytecode.size() ) );
			} );
			_rhi->getLiveShaderManager().watchShader( vsDesc, shaderReloadDelegate );

			const std::string shaderWatchRoot = FileUtil::normalizePath( ResourceUtil::getRootFolderPath() );
			if ( shaderWatchRoot.empty() == false && _reloadFileManager )
			{
				_shaderWatchHandle = _reloadFileManager->registerWatch(
					shaderWatchRoot,
					{ ".hlsl", ".hlsli" },
					SW_DELEGATE_LAMBDA( FileWatchMatchDelegate,
										[this]( const FileChangeEvent& ev )
					{
						const std::string fullPath = FileUtil::normalizePath( ev._directory + "/" + ev._filename );
						_rhi->getLiveShaderManager().notifyFileChanged( fullPath );
					} ) );
			}
#endif

			createGameViewportTexture();

			_liveReloadManager->setOnBeforeReload( kEditorModuleName, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &App::onBeforeEditorReload, this ) );
			_liveReloadManager->setOnAfterReload( kEditorModuleName, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &App::onAfterEditorReload, this ) );

			if ( _liveReloadManager->registerModule( kEditorModuleName ) == false )
			{
				SW_LOG_ERROR( "Editor Module 로드에 실패했습니다." );
				return false;
			}
		}

		if ( Scene* activeScene = _sceneManager->createScene( "MainScene" ) )
			activeScene->initialize( &_rhi->getDevice() );

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

		// 에디터 OFF/Shipping: Play 토글 없이 바로 실행. 에디터 ON이면 UI Play로 제어.
		if ( _bEnableEditor == false )
			setGameState( GameState::Playing );

		_window->setResizeCallback( SW_DELEGATE_METHOD( WindowResizeDelegate, &App::onResize, this ) );
		_window->setCustomMessageHandler( SW_DELEGATE_METHOD( WindowMessageHandlerDelegate, &App::onWindowMessage, this ) );

		_bAppRunning			 = true;
		_bPendingBackendChange	 = false;
		_committedRHIBackend	 = gv_RHIBackend;
		_pendingRHIBackend		 = gv_RHIBackend;

		if ( pRHIBackendVar )
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
				SW_LOG_INFO( "[Hot-Swap] RHI Backend change queued: %# → %#",
							 RHI::getBackendTypeName( _committedRHIBackend ),
							 RHI::getBackendTypeName( requested ) );
				_pendingRHIBackend	   = requested;
				_bPendingBackendChange = true;
			} );
		}

		return true;
	}

	void App::run()
	{
		SW_LOG_INFO( "Entering App Main Loop..." );

		_editorCtx.playerSpeed	  = &gv_EditorPlayerSpeed;
		_editorCtx.clearColor	  = _clearColor;
		_editorCtx.reflectionData = &_reflectionData;
		_editorCtx.rhiDevice	  = &_rhi->getDevice();
		_editorCtx.gameTextureID  = _gameTextureID;

		CpuTimer frameTimer;
		frameTimer.resetTimer();
		frameTimer.startTimer();

		while ( _bAppRunning && _window->processMessages() )
		{
			frameTimer.updateTimer();
			const float32 deltaTime = frameTimer.getDeltaTime();

#if !defined( SW_SHIPPING )
			_liveReloadManager->update();
#if defined( SW_DEBUG )
			_rhi->getLiveShaderManager().update();
#endif
#endif
			_reloadFileManager->update();

			if ( _editor && _editorApi.preRender )
			{
				_editorCtx.gameTextureID = _gameTextureID;
				_editorApi.preRender( _editor, &_rhi->getDevice() );
			}

			if ( _game && _gameApi.update && getGameState() == GameState::Playing )
				_gameApi.update( _game, deltaTime );

			IRHIDevice& device		= _rhi->getDevice();
			Scene*		activeScene = _sceneManager->getActiveScene();

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

			if ( _editor && _editorApi.render )
			{
				_editorCtx.material		 = activeScene ? activeScene->getMaterial() : nullptr;
				_editorCtx.gameTextureID = _gameTextureID;
				_editorApi.render( _editor, &_editorCtx );
			}

			device.endFrame( true );

			if ( _editor && _editorApi.postPresent )
				_editorApi.postPresent( _editor, &_rhi->getDevice() );

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

		if ( _window && _window->shouldClose() )
			_bAppRunning = false;
	}

	void App::shutdown()
	{
		onBeforeGameReload();
		onBeforeEditorReload();

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
		if ( _sceneManager )
			_sceneManager->shutdown();
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
		unbindCoreServices();

		_sceneManager.reset();
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

	void App::onResize( uint32 w, uint32 h )
	{
		if ( _rhi )
			_rhi->getDevice().resize( w, h );
	}

	bool App::onWindowMessage( const NativeWindowEvent& event )
	{
#if defined( SW_PLATFORM_WINDOWS ) && !defined( SW_SHIPPING )
		if ( event.message == WM_KEYDOWN && _rhi )
		{
			switch ( event.wParam )
			{
				case VK_F5:
					_rhi->getLiveShaderManager().triggerReloadAll();
					SW_LOG_INFO( "[App] F5: force shader reload" );
					break;
				case VK_F6:
					if ( _liveReloadManager && _bEnableEditor )
					{
						_liveReloadManager->triggerReload( kEditorModuleName );
						SW_LOG_INFO( "[App] F6: force EditorModule reload" );
					}
					break;
				case VK_F7:
					if ( _liveReloadManager )
					{
						_liveReloadManager->triggerReload( kGameModuleName );
						SW_LOG_INFO( "[App] F7: force SWGame reload" );
					}
					break;
				default:
					break;
			}
		}
#endif

		if ( _editor && _editorApi.processEvent )
			return _editorApi.processEvent( _editor, &event );
		return false;
	}

	void App::onBeforeEditorReload()
	{
		if ( _editor && _editorApi.shutdown )
			_editorApi.shutdown( _editor );
		if ( _editor && _editorApi.destroy )
			_editorApi.destroy( _editor );
		_editor	   = nullptr;
		_editorApi = {};
	}

	void App::onAfterEditorReload( void* hLibraryModule )
	{
		if ( bindEditorAPI( hLibraryModule ) == false )
			return;

		_editor = _editorApi.create();
		if ( _editor == nullptr )
		{
			SW_LOG_ERROR( "[App] Failed to create Editor instance" );
			return;
		}

		if ( _editorApi.initialize( _editor, _window.get(), &_rhi->getDevice() ) == false )
		{
			SW_LOG_ERROR( "[App] Failed to initialize Editor instance" );
			return;
		}

		if ( _gameRenderTarget && _editorApi.registerTexture )
			_gameTextureID = _editorApi.registerTexture( _editor, static_cast<TextureHandle>( _gameRenderTarget ) );

		SW_LOG_INFO( "[App] Editor initialized successfully via EditorAPI." );
	}

	void App::onBeforeGameReload()
	{
		if ( _game == nullptr )
			return;

		if ( _gameApi.shutdown )
			_gameApi.shutdown( _game );
		if ( _gameApi.destroy )
			_gameApi.destroy( _game );
		_game	 = nullptr;
		_gameApi = {};
	}

	void App::onAfterGameReload( void* hLibraryModule )
	{
#if defined( SW_SHIPPING )
		(void)hLibraryModule;
		if ( _gameApi.create == nullptr && bindGameAPI( nullptr ) == false )
			return;
#else
		void* moduleHandle = hLibraryModule;
		if ( moduleHandle == nullptr && _liveReloadManager )
			moduleHandle = _liveReloadManager->getModuleHandle( kGameModuleName );

		if ( bindGameAPI( moduleHandle ) == false )
			return;
#endif

		_game = _gameApi.create();
		if ( _game == nullptr )
		{
			SW_LOG_ERROR( "[App] Failed to create Game instance" );
			return;
		}

		if ( _gameApi.initialize( _game, _window.get(), &_rhi->getDevice() ) == false )
		{
			SW_LOG_ERROR( "[App] Failed to initialize Game instance" );
			return;
		}

		SW_LOG_INFO( "[App] SWGame initialized successfully via GameAPI." );
	}
} // namespace sw
