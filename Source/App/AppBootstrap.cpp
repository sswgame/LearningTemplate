/**
 * @file AppBootstrap.cpp
 * @brief App subsystem / window / RHI bootstrap
 */
#include "App.h"
#include "AppModuleHeads.h"

SW_DEFINE_MODULE_REGISTRAR_HEAD( swAppGvmHead, ::sw::GlobalVariableRegistrar );
SW_DEFINE_MODULE_REGISTRAR_HEAD( swAppTypeHead, ::sw::TypeRegistrar );
SW_DEFINE_MODULE_REGISTRAR_HEAD( swAppEnumHead, ::sw::EnumRegistrar );
SW_DEFINE_MODULE_REGISTRAR_HEAD( swAppComponentFactoryHead, ::sw::ComponentFactoryRegistrar );

#include "Core/Common/CoreServices.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/CommandLine/CommandLineManager.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Utility/Task/TaskManager.h"
#include "Core/Utility/Module/LiveReloadManager.h"
#include "Core/Utility/File/ReloadFileManager.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Object/ComponentManager.h"
#include "Core/Graphics/RHI/RHI.h"
#include "Core/Graphics/RHI/RHICapabilities.h"
#include "Core/Window/IWindow.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Input/InputManager.h"
#include "Core/Graphics/RenderPass/FrameRenderer.h"
#include "Runtime/RuntimeHandles.h"

namespace sw
{
	SW_GLOBAL_VARIABLE_FLOAT( gv_EditorPlayerSpeed, 5.0f, "Editor inspector player speed slider" );
	SW_EXTERN_GLOBAL_VARIABLE_STRING( kEditorModuleName );
#if !defined( SW_SHIPPING )
	SW_EXTERN_GLOBAL_VARIABLE_STRING( kGameModuleName );
#endif

	bool App::initializeSubsystems( int argc, char* argv[] )
	{
		BLOCK( "Logger / CommandLine / GVM 초기화" )
		{
			_logger = std::make_unique<Logger>();
			_logger->initialize();

			_commandLineManager = std::make_unique<CommandLineManager>();
			_commandLineManager->initialize();

			// Core.dll list first (correct moduleName), then App-only list.
			_globalVariableManager = std::make_unique<GlobalVariableManager>();
			_globalVariableManager->registerPendingVariables( "Core", GlobalVariableRegistrar::getHead() );
			_globalVariableManager->registerPendingVariables( "App", swAppGvmHead() );
			_globalVariableManager->registerToCommandLine( _commandLineManager.get() );
			_commandLineManager->parse( argc, argv );
			_globalVariableManager->updateFromCommandLine( _commandLineManager.get() );
		}

		BLOCK( "Core Services 생성 및 바인딩" )
		{
			_taskManager	   = std::make_unique<TaskManager>();
			_typeRegistry	   = std::make_unique<TypeRegistry>();
			_componentManager  = std::make_unique<ComponentManager>();
			_liveReloadManager = std::make_unique<LiveReloadManager>();
			_reloadFileManager = std::make_unique<ReloadFileManager>();
			_sceneManager	   = std::make_unique<SceneManager>();
			_inputManager	   = std::make_unique<InputManager>();
			_frameRenderer	   = std::make_unique<FrameRenderer>();

			CoreServices services{};
			services.commandLineManager	   = _commandLineManager.get();
			services.globalVariableManager = _globalVariableManager.get();
			services.taskManager		   = _taskManager.get();
			services.typeRegistry		   = _typeRegistry.get();
			services.componentManager	   = _componentManager.get();
			services.sceneManager		   = _sceneManager.get();
			services.inputManager		   = _inputManager.get();
			core::bindCoreServices( services );

			registerCoreReflectionTypes();
			core::getComponentManager().registerPendingFactories( "App", swAppComponentFactoryHead() );
		}

		BLOCK( "Task / Resource / Scene 초기화" )
		{
			if ( ResourceUtil::initialize() == false )
			{
				SW_LOG_ERROR( "Failed to initialize ResourceUtil!" );
				return false;
			}

			if ( _taskManager->initialize() == false )
				return false;

			if ( _reloadFileManager->initialize() == false )
				return false;

			if ( _sceneManager->initialize() == false )
				return false;

			if ( _inputManager->initialize() == false )
				return false;
		}

		BLOCK( "플랫폼 윈도우 생성" )
		{
			uint32 width{};
			uint32 height{};
			_commandLineManager->getArgument( CommandLineArgument::WIDTH, width );
			_commandLineManager->getArgument( CommandLineArgument::HEIGHT, height );
			_window = IWindow::createPlatformWindow();
			if ( _window == nullptr || _window->create( "SWEngine", width, height ) == false )
			{
				SW_LOG_ERROR( "Failed to create platform window!" );
				return false;
			}
			IWindow::setActiveWindow( _window.get() );
		}

		BLOCK( "RHI 초기화" )
		{
			_rhi = std::make_unique<RHI>();
			if ( _rhi->initialize() == false )
				return false;

			bool bEnableEditor = false;
			_commandLineManager->getArgument( CommandLineArgument::ENABLE_EDITOR, bEnableEditor );
			_bEnableEditor = bEnableEditor ? 1 : 0;

			const RHICapabilities caps = RHIAvailability::query( gv_RHIBackend );
			if ( _bEnableEditor && caps._bEditorSupported == false )
			{
				SW_LOG_WARNING( "[App] Editor requested but backend %# does not set _bEditorSupported — disabling editor.",
								RHI::getBackendTypeName( gv_RHIBackend ) );
				_bEnableEditor = 0;
			}
		}
		return true;
	}

	bool App::createGameViewportTexture( uint32 width, uint32 height )
	{
		if ( _rhi == nullptr || width == 0 || height == 0 )
			return false;

		RHITextureDesc rtDesc{};
		rtDesc._width			  = width;
		rtDesc._height			  = height;
		rtDesc._format			  = RHIFormat::R8G8B8A8_UNORM;
		rtDesc._bIsRenderTarget	  = true;
		rtDesc._bIsShaderResource = true;
		rtDesc._mipLevels		  = 1;
		rtDesc._clearColor[0]	  = 0.1f;
		rtDesc._clearColor[1]	  = 0.1f;
		rtDesc._clearColor[2]	  = 0.1f;
		rtDesc._clearColor[3]	  = 1.0f;

		_gameRenderTarget	= _rhi->getDevice().createTexture2D( rtDesc );
		_gameViewportWidth	= width;
		_gameViewportHeight = height;
		return _gameRenderTarget != 0;
	}

	bool App::recreateGameViewportTexture( uint32 width, uint32 height )
	{
		if ( _rhi == nullptr || width == 0 || height == 0 )
			return false;

		if ( _gameTextureID != nullptr && _editor && _editorApi.unregisterTexture )
		{
			_editorApi.unregisterTexture( _editor, _gameTextureID );
			_gameTextureID			 = nullptr;
			_editorCtx.gameTextureID = nullptr;
		}

		if ( _gameRenderTarget != 0 )
		{
			_rhi->getDevice().destroyTexture( _gameRenderTarget );
			_gameRenderTarget = 0;
			_gameTextureID	  = nullptr;
		}

		if ( createGameViewportTexture( width, height ) == false )
		{
			SW_LOG_WARNING( "[App] Game View RT recreate failed (%# x %#)", width, height );
			_editorCtx.gameTextureID = nullptr;
			return false;
		}

		if ( _editor && _editorApi.registerTexture )
			_gameTextureID = _editorApi.registerTexture( _editor, static_cast<TextureHandle>( _gameRenderTarget ) );

		_editorCtx.gameTextureID	  = _gameTextureID;
		_editorCtx.gameViewportWidth  = _gameViewportWidth;
		_editorCtx.gameViewportHeight = _gameViewportHeight;
		return true;
	}

	void App::processGameViewportResizeRequest()
	{
		if ( _bEnableEditor == 0 || _rhi == nullptr )
			return;

		const uint32 reqW = _requestedGameViewportWidth;
		const uint32 reqH = _requestedGameViewportHeight;
		if ( reqW == 0 || reqH == 0 )
		{
			_gameViewportResizeStableFrames = 0;
			return;
		}

		if ( reqW == _gameViewportWidth && reqH == _gameViewportHeight )
		{
			_requestedGameViewportWidth		= 0;
			_requestedGameViewportHeight	= 0;
			_gameViewportResizeStableFrames = 0;
			return;
		}

		if ( reqW != _gameViewportResizeLastW || reqH != _gameViewportResizeLastH )
		{
			_gameViewportResizeLastW		= reqW;
			_gameViewportResizeLastH		= reqH;
			_gameViewportResizeStableFrames = 0;
			return;
		}

		++_gameViewportResizeStableFrames;
		// Debounce: require a few stable frames so dock/layout thrash does not spam recreate.
		constexpr uint32 kStableFramesBeforeRecreate = 4;
		if ( _gameViewportResizeStableFrames < kStableFramesBeforeRecreate )
			return;

		if ( recreateGameViewportTexture( reqW, reqH ) )
		{
			_requestedGameViewportWidth		= 0;
			_requestedGameViewportHeight	= 0;
			_gameViewportResizeStableFrames = 0;
		}
	}
} // namespace sw
