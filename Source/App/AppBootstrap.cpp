/**
 * @file AppBootstrap.cpp
 * @brief App subsystem / window / RHI bootstrap
 */
#include "App.h"
#include "AppInternal.h"
#include "AppModuleHeads.h"

SW_DEFINE_MODULE_REGISTRAR_HEAD( swAppGvmHead, ::sw::GlobalVariableRegistrar );
SW_DEFINE_MODULE_REGISTRAR_HEAD( swAppTypeHead, ::sw::TypeRegistrar );
SW_DEFINE_MODULE_REGISTRAR_HEAD( swAppEnumHead, ::sw::EnumRegistrar );

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

SW_GLOBAL_VARIABLE_FLOAT( gv_EditorPlayerSpeed, 5.0f, "Editor inspector player speed slider" );

namespace sw
{
	bool App::initializeSubsystems( int argc, char* argv[] )
	{
		BLOCK( "Logger / CommandLine / GVM 초기화" )
		{
			_logger = std::make_unique<Logger>();
			_logger->startup();

			_commandLineManager = std::make_unique<CommandLineManager>();
			_commandLineManager->initialize();

			_globalVariableManager = std::make_unique<GlobalVariableManager>();
			_globalVariableManager->initialize();
			// Core.dll list first (correct moduleName), then App-only list.
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

			CoreServices services{};
			services.commandLineManager	   = _commandLineManager.get();
			services.globalVariableManager = _globalVariableManager.get();
			services.taskManager		   = _taskManager.get();
			services.typeRegistry		   = _typeRegistry.get();
			services.componentManager	   = _componentManager.get();
			services.sceneManager		   = _sceneManager.get();
			bindCoreServices( services );

			registerCoreReflectionTypes();
		}

		BLOCK( "Task / LiveReload / Resource / Scene 초기화" )
		{
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
		}

		BLOCK( "플랫폼 윈도우 생성" )
		{
			_window = IWindow::createPlatformWindow();
			if ( _window == nullptr || _window->create( L"Toy Engine Editor (Live Coding + Hot Reloading + Multi-Backend RHI)", 1280, 720 ) == false )
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
} // namespace sw
