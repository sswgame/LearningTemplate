#include "pch.h"

#include "Engine/EngineLoop.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/DeadlockDetector.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Game/GameState.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Graphics/RenderPass/RenderFramePacket.h"
#include "Engine/Graphics/RenderPass/RenderThread.h"
#include "Engine/Graphics/Shader/LiveShaderManager.h"
#include "Engine/Graphics/Shader/ShaderCache.h"
#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Debug/DebugOverlayState.h"
#include "Engine/Utility/File/ReloadFileManager.h"
#include "Engine/Utility/Module/LiveReloadManager.h"
#include "Engine/Utility/Resource/AssetStreamingQueue.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Window/IWindow.h"

#include "RuntimeAPI/PluginAPI.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingHostDefaults.h"

namespace sw
{
	SW_EXTERN_GLOBAL_VARIABLE_BOOL( gv_useRenderThread );

	namespace
	{
		bool cliRequestsBackend( const CommandLineManager& cli )
		{
			bool bFlag{ false };
			if ( cli.getArgument( CommandLineArgument::DIRECTX_11, bFlag ) && bFlag )
				return true;
			if ( cli.getArgument( CommandLineArgument::DIRECTX_12, bFlag ) && bFlag )
				return true;
			if ( cli.getArgument( CommandLineArgument::VULKAN, bFlag ) && bFlag )
				return true;
			if ( cli.getArgument( CommandLineArgument::OPENGL, bFlag ) && bFlag )
				return true;
			return false;
		}

	} // namespace

	EngineLoop::EngineLoop()
		: _logger{ nullptr }
		, _deadlockDetector{ nullptr }
		, _memoryProfiler{ nullptr }
		, _commandLineManager{ nullptr }
		, _taskManager{ nullptr }
		, _globalVariableManager{ nullptr }
		, _typeRegistry{ nullptr }
		, _configManager{ nullptr }
		, _localizationManager{ nullptr }
		, _resourceManager{ nullptr }
		, _rhi{ nullptr }
		, _liveReloadManager{ nullptr }
		, _reloadFileManager{ nullptr }
		, _sceneManager{ nullptr }
		, _inputManager{ nullptr }
		, _mapDebugAction{ nullptr }
		, _audioSystem{ nullptr }
		, _eventDispatcher{ nullptr }
		, _frameRenderer{ nullptr }
		, _renderThread{ nullptr }
		, _engineData{ nullptr }
		, _assetStreamingQueue{ nullptr }
		, _commandStack{ nullptr }
		, _debugOverlayState{ nullptr }
		, _debugDrawQueue{ nullptr }
		, _rhiBackendRegistry{ nullptr }
		, _bShellActionsBound{ false }
	{
	}

	EngineLoop::~EngineLoop() = default;

	bool EngineLoop::initialize( int32 argc, utf8* pArgv[] )
	{
		initializeHashedStringPool();

		BLOCK( "Logger / DeadlockDetector / MemoryProfiler / CommandLine / GVM 초기화" )
		{
			_logger = make_unique<Logger>();
			_logger->initialize();

#if defined( SW_DEBUG )
			_deadlockDetector = make_unique<DeadlockDetector>();
			_deadlockDetector->initialize();

			_memoryProfiler = make_unique<MemoryProfiler>();
			_memoryProfiler->initialize();
#endif

			_commandLineManager = make_unique<CommandLineManager>();
			_commandLineManager->initialize();

			_globalVariableManager = make_unique<GlobalVariableManager>();
			_globalVariableManager->registerPendingVariables( "Engine", GlobalVariableRegistrar::getHead() );
			_globalVariableManager->registerPendingVariables( "App", GlobalVariableRegistrar::getHead() );
			_globalVariableManager->registerToCommandLine( _commandLineManager.get() );
			_commandLineManager->parse( argc, pArgv );
			_globalVariableManager->updateFromCommandLine( _commandLineManager.get() );
		}

		BLOCK( "Core Services 생성 및 바인딩" )
		{
			_taskManager		 = make_unique<TaskManager>();
			_typeRegistry		 = make_unique<TypeRegistry>();
			_configManager		 = make_unique<ConfigManager>();
			_localizationManager = make_unique<LocalizationManager>();
			_resourceManager	 = make_unique<ResourceManager>();
			_liveReloadManager	 = make_unique<LiveReloadManager>();
			_reloadFileManager	 = make_unique<ReloadFileManager>();
			_sceneManager		 = make_unique<SceneManager>();
			_inputManager		 = make_unique<InputManager>();
			_audioSystem		 = IAudioSystem::create();
			_eventDispatcher	 = make_unique<EventDispatcher>();
			_frameRenderer		 = make_unique<FrameRenderer>();
			_engineData			 = make_unique<EngineData>();
			_assetStreamingQueue = make_unique<AssetStreamingQueue>();
			_commandStack		 = make_unique<CommandStack>();
			_debugOverlayState	 = make_unique<DebugOverlayState>();
			_debugDrawQueue		 = make_unique<DebugDrawQueue>();
			_frameDoubleBuffer	 = make_unique<FrameDoubleBuffer>();
			_rhiBackendRegistry	 = make_unique<RHIBackendRegistry>();

			EngineServices services{};
			services._pCommandLineManager	 = _commandLineManager.get();
			services._pGlobalVariableManager = _globalVariableManager.get();
			services._pLocalizationManager	 = _localizationManager.get();
			services._pTaskManager			 = _taskManager.get();
			services._pTypeRegistry			 = _typeRegistry.get();
			services._pSceneManager			 = _sceneManager.get();
			services._pInputManager			 = _inputManager.get();
			services._pAudioSystem			 = _audioSystem.get();
			services._pEventDispatcher		 = _eventDispatcher.get();
			services._pResourceManager		 = _resourceManager.get();
			services._pMemoryProfiler		 = _memoryProfiler.get();
			services._pEngineData			 = _engineData.get();
			services._pAssetStreamingQueue	 = _assetStreamingQueue.get();
			services._pCommandStack			 = _commandStack.get();
			services._pDebugOverlayState	 = _debugOverlayState.get();
			services._pDebugDrawQueue		 = _debugDrawQueue.get();
			services._pFrameDoubleBuffer	 = _frameDoubleBuffer.get();
			services._pRHIBackendRegistry	 = _rhiBackendRegistry.get();

			engine::bindEngineServices( services );
			engine::registerModuleTypes( "Engine" );
			engine::registerModuleTypes( "GameFramework" );
		}

		BLOCK( "Task / Resource / Scene 초기화" )
		{
			if ( _resourceManager->initialize() == false )
				return false;
			if ( _taskManager->initialize() == false )
				return false;
			if ( _reloadFileManager->initialize() == false )
				return false;
			_resourceManager->attachReloadFileManager( *_reloadFileManager );
			if ( _sceneManager->initialize() == false )
				return false;
			if ( _inputManager->initialize() == false )
				return false;
			if ( _audioSystem->initialize() == false )
				return false;
		}

		BLOCK( "엔진 기본 설정 로드 및 RHI 백엔드 선정 & 초기화" )
		{
			const hashed_string kEngineConfigHash = hashed_string{ "EngineConfig" };
			const EngineConfig* pEngineConfig	  = _configManager->ensureConfig<EngineConfig>(
				kEngineConfigHash, config::kFileRuntimeEngineConfig, shipping_host::kEngineConfigJson );
			if ( pEngineConfig == nullptr )
				return false;

			const hashed_string kGameConfigHash = hashed_string{ "GameConfig" };
			const GameConfig*	pGameConfig		= _configManager->ensureConfig<GameConfig>(
				kGameConfigHash, config::kFileRuntimeGameConfig, shipping_host::kGameConfigJson );
			if ( pGameConfig != nullptr )
				GameConfig::setActive( *pGameConfig );

			if ( pEngineConfig->_engineData.empty() == false )
				_engineData->loadFromResource( pEngineConfig->_engineData );
			else
				_engineData->loadFromResource();

			if ( cliRequestsBackend( *_commandLineManager ) == false )
				gv_rhiBackend = pEngineConfig->_window._defaultRHI;

			if ( IWindow::getActiveWindow() == nullptr )
			{
				uint32 windowWidth	= pEngineConfig->_window._width;
				uint32 windowHeight = pEngineConfig->_window._height;
				_commandLineManager->getArgument( CommandLineArgument::WIDTH, windowWidth );
				_commandLineManager->getArgument( CommandLineArgument::HEIGHT, windowHeight );

				unique_ptr<IWindow> defaultWindow = IWindow::createPlatformWindow();
				if ( defaultWindow != nullptr && defaultWindow->initializeWindow( pEngineConfig->_window._title.c_str(), windowWidth, windowHeight ) )
				{
					IWindow::setActiveWindow( defaultWindow.release() );
				}
			}

			_rhi = make_unique<RHI>();
			_rhi->setPreferredVSync( pEngineConfig->_window._bVSync );
			if ( _rhi->initialize() == false )
				return false;

			if ( _frameRenderer->initialize( &_rhi->getDevice(), _taskManager.get() ) == false )
			{
				SW_LOG_ERROR( "Failed to initialize FrameRenderer!" );
				return false;
			}

			_renderThread = make_unique<RenderThread>();
			if ( gv_useRenderThread )
			{
				if ( _renderThread->start( &_rhi->getDevice(), _frameRenderer.get() ) == false )
				{
					SW_LOG_ERROR( "Failed to start RenderThread!" );
					return false;
				}
			}
			else
			{
				if ( _renderThread->bind( &_rhi->getDevice(), _frameRenderer.get() ) == false )
				{
					SW_LOG_ERROR( "Failed to bind RenderThread!" );
					return false;
				}
			}

			if ( _sceneManager != nullptr )
			{
				_sceneManager->setRhiDevice( &_rhi->getDevice() );
				_sceneManager->setFrameRenderer( _frameRenderer.get() );
			}
		}

		MemoryProfiler::captureMemoryLeakBaseline();

		return true;
	}

	void EngineLoop::shutdown()
	{
		BLOCK( "RHI / Window 정리" )
		{
			if ( _sceneManager != nullptr )
			{
				_sceneManager->setFrameRenderer( nullptr );
				_sceneManager->setRhiDevice( nullptr );
			}
			if ( _renderThread != nullptr )
			{
				_renderThread->waitIdle();
				_renderThread->stop();
			}
			if ( _frameRenderer != nullptr )
				_frameRenderer->shutdown();
			if ( _rhi != nullptr )
			{
				if ( _resourceManager != nullptr && _rhi->getDevice().getNativeDevice() != nullptr )
					_resourceManager->getMaterialManager().shutdownAllGpu( &_rhi->getDevice() );
				_rhi->getDevice().waitIdle();
				_rhi->shutdown();
			}
		}

		BLOCK( "매니저 종료 및 언바인드" )
		{
			if ( _sceneManager != nullptr )
				_sceneManager->shutdown();
			if ( _inputManager != nullptr )
				_inputManager->shutdown();
			if ( _audioSystem != nullptr )
				_audioSystem->shutdown();
			if ( _resourceManager != nullptr )
				_resourceManager->detachReloadFileManager();
			if ( _reloadFileManager != nullptr )
				_reloadFileManager->shutdown();
			if ( _taskManager != nullptr )
				_taskManager->shutdown();
			if ( _liveReloadManager != nullptr )
				_liveReloadManager->shutdown();
			if ( _globalVariableManager != nullptr )
				_globalVariableManager->shutdown();
			if ( _memoryProfiler != nullptr )
				_memoryProfiler->shutdown();
			if ( _deadlockDetector != nullptr )
				_deadlockDetector->shutdown();
			if ( _logger != nullptr )
				_logger->shutdown();

			engine::unbindEngineServices();

			_renderThread.reset();
			_frameRenderer.reset();
			_sceneManager.reset();
			_inputManager.reset();
			_audioSystem.reset();
			_eventDispatcher.reset();
			_reloadFileManager.reset();
			_liveReloadManager.reset();
			_engineData.reset();
			_assetStreamingQueue.reset();
			_commandStack.reset();
			_debugOverlayState.reset();
			_debugDrawQueue.reset();
			_frameDoubleBuffer.reset();
			_rhiBackendRegistry.reset();

			// [Note] ResourceManager는 가장 밑바탕이 되는 시스템입니다.
			// 다른 매니저들의 reset() 시 소멸자가 호출되며 들고 있던 리소스들을 해제하는데,
			// 이때 ResourceManager가 살아있어야 안전하게 해제됩니다.
			// 따라서 모든 매니저들의 소멸자가 불린 직후인 이곳에서 마지막으로 shutdown()을 호출합니다.
			if ( _resourceManager != nullptr )
				_resourceManager->shutdown();
			_resourceManager.reset();
			_typeRegistry.reset();
			_localizationManager.reset();
			_globalVariableManager.reset();
			_configManager.reset();
			_taskManager.reset();
			_rhi.reset();
			_commandLineManager.reset();
			_mapDebugAction.reset();
			_memoryProfiler.reset();
			_deadlockDetector.reset();

			shutdownHashedStringPools();

			_logger.reset();
		}

		ShaderCache::clearCache();
		MemoryProfiler::reportMemoryLeaks( "EngineLoop::shutdown" );
	}

	void EngineLoop::beginFrame()
	{
		if ( _inputManager != nullptr )
			_inputManager->beginFrame();
	}

	void EngineLoop::tick( float32 deltaTime,
						   bool	   bEnableEditor,
						   uint64  gameRenderTarget,
						   uint32  vpWidth,
						   uint32  vpHeight )
	{
		BLOCK( "핫 리로드 / 씬 트랜지션 / 이벤트" )
		{
#if !defined( SW_SHIPPING )
			if ( _liveReloadManager != nullptr )
				_liveReloadManager->update();
	#if defined( SW_DEBUG )
			if ( _rhi != nullptr )
				_rhi->getLiveShaderManager().update();
	#endif
#endif
			if ( _reloadFileManager != nullptr )
				_reloadFileManager->update();
			engine::getAssetStreamingQueue().update();

			if ( _sceneManager != nullptr )
				_sceneManager->tickTransitions();
			if ( _eventDispatcher != nullptr )
				_eventDispatcher->processEvents();
			if ( _audioSystem != nullptr )
				_audioSystem->update( deltaTime );
		}

		BLOCK( "Scene update" )
		{
			if ( _sceneManager != nullptr )
				_sceneManager->tick( deltaTime );
		}

		Scene* pActiveScene = _sceneManager != nullptr ? _sceneManager->getActiveScene() : nullptr;

		BLOCK( "RenderFramePacket 제출" )
		{
			RenderFramePacket packet{};
			packet._bValid			 = 1;
			packet._bEnableEditor	 = bEnableEditor ? 1 : 0;
			packet._gameRenderTarget = bEnableEditor ? gameRenderTarget : 0;
			packet._pSceneMaterial	 = pActiveScene != nullptr ? pActiveScene->getMaterial() : nullptr;
			packet._viewportWidth	 = vpWidth;
			packet._viewportHeight	 = vpHeight;
			packet._cameraPos[0]	 = 0.0f;
			packet._cameraPos[1]	 = 1.2f;
			packet._cameraPos[2]	 = 3.2f;

			if ( pActiveScene != nullptr )
			{
				pActiveScene->ensureDefaultCameras();
				CameraComponent* pCam = pActiveScene->getActiveRenderCamera( bEnableEditor );
				if ( pCam != nullptr )
				{
					pCam->getCameraPosition( packet._cameraPos );
					const float32 aspect =
						( packet._viewportHeight > 0 )
							? ( static_cast<float32>( packet._viewportWidth ) / static_cast<float32>( packet._viewportHeight ) )
							: ( 16.0f / 9.0f );
					const float4x4 vp = pCam->getViewProjectionMatrix( aspect );
					Memory::copy( packet._viewProj, &vp._11, sizeof( packet._viewProj ) );
					packet._bHasViewProj = 1;
				}
				packet._gpuScene.buildFromScene( pActiveScene, packet._cameraPos, _taskManager.get() );
			}

			if ( _renderThread != nullptr )
			{
				_renderThread->submit( std::move( packet ) );
			}
		}
	}

	void EngineLoop::endFrame()
	{
		if ( _inputManager != nullptr )
			_inputManager->endFrame();
		engine::getDebugDrawQueue().clear();
		getThreadLocalFrameArena().reset();
		engine::getFrameDoubleBuffer().swapAndResetPrevious();
	}

	bool EngineLoop::applyPendingBackendChange()
	{
		if ( _rhi == nullptr )
			return false;

		const RHIBackend requested = _rhi->consumePendingBackendChange();
		const RHIBackend previous  = _rhi->getCommittedBackend();
		if ( requested == previous )
			return true;

		SW_LOG_INFO( "[Hot-Swap] Soft-recreating RHI: %# → %#",
					 RHI::getBackendTypeName( previous ),
					 RHI::getBackendTypeName( requested ) );

		BLOCK( "기존 RHI / Scene 리소스 정리" )
		{
			if ( _sceneManager != nullptr )
			{
				_sceneManager->setFrameRenderer( nullptr );
				_sceneManager->setRhiDevice( nullptr );
			}
			if ( _renderThread != nullptr )
				_renderThread->stop();
			if ( _frameRenderer != nullptr )
				_frameRenderer->shutdown();

			engine::getResourceManager().getMaterialManager().shutdownAllGpu( &_rhi->getDevice() );

			_rhi->getDevice().waitIdle();
			ShaderCache::clearCache();
		}

		if ( _rhi->recreateDevice( requested ) == false )
		{
			SW_LOG_ERROR( "[Hot-Swap] recreateDevice failed; reverting gv_rhiBackend to %#",
						  RHI::getBackendTypeName( previous ) );
			gv_rhiBackend = previous;
			return false;
		}

		BLOCK( "Scene 재초기화" )
		{
			if ( engine::getResourceManager().getMaterialManager().reinitializeAll( &_rhi->getDevice() ) == false )
				SW_LOG_ERROR( "[Hot-Swap] MaterialCache reinitializeAll failed after backend change." );

			if ( _frameRenderer != nullptr )
				_frameRenderer->initialize( &_rhi->getDevice(), _taskManager.get() );

			if ( _renderThread != nullptr )
			{
				if ( gv_useRenderThread )
					_renderThread->start( &_rhi->getDevice(), _frameRenderer.get() );
				else
					_renderThread->bind( &_rhi->getDevice(), _frameRenderer.get() );
			}

			if ( _sceneManager != nullptr )
			{
				_sceneManager->setRhiDevice( &_rhi->getDevice() );
				_sceneManager->setFrameRenderer( _frameRenderer.get() );
			}

			Scene* pScene = _sceneManager != nullptr ? _sceneManager->getActiveScene() : nullptr;
			if ( pScene != nullptr )
				pScene->ensureDefaultCameras();
		}

		SW_LOG_INFO( "[Hot-Swap] Active backend is now %#", RHI::getBackendTypeName( _rhi->getCommittedBackend() ) );
		return true;
	}

	void EngineLoop::setPresentHook( PresentHookDelegate presentHook )
	{
		if ( _renderThread != nullptr )
			_renderThread->setPresentHook( std::move( presentHook ) );
	}

	void EngineLoop::setPostPresentHook( PresentHookDelegate postPresentHook )
	{
		if ( _renderThread != nullptr )
			_renderThread->setPostPresentHook( std::move( postPresentHook ) );
	}

	void EngineLoop::updateShellActions( float32 deltaTime )
	{
		if ( _inputManager == nullptr )
			return;

		if ( _bShellActionsBound == false )
		{
			_mapDebugAction			   = make_unique<ActionMap>();
			const string& inputMapPath = engine::getEngineData()._shellInputMap;
			if ( inputMapPath.empty() || _mapDebugAction->loadFromResource( inputMapPath ) == false )
				_mapDebugAction->bindEmergencyFallback();
			_bShellActionsBound = true;
		}

		// _shellActions는 위 lazy init에서 항상 생성되므로 nullptr일 수 없습니다.

		if ( _mapDebugAction->hasLayer( ActionMapDefaults::kTitleLayerName ) )
			_mapDebugAction->setLayerEnabled( ActionMapDefaults::kTitleLayerName, false );
		_mapDebugAction->setInputManager( _inputManager.get() );
		_mapDebugAction->update( deltaTime );
	}

	void EngineLoop::pollDebugHotkeys( [[maybe_unused]] bool bEnableEditor, [[maybe_unused]] const Delegate<void( const utf8* )>& forceReloadCallback )
	{
#if !defined( SW_SHIPPING )
		if ( _mapDebugAction == nullptr )
			return;

		if ( _mapDebugAction->wasActionTriggered( ActionMapDefaults::kReloadShadersAction ) && _rhi != nullptr )
		{
	#if defined( SW_DEBUG )
			_rhi->getLiveShaderManager().triggerReloadAll();
			SW_LOG_INFO( "[EngineLoop] %#: force shader reload", ActionMapDefaults::kReloadShadersAction );
	#endif
		}
		if ( _mapDebugAction->wasActionTriggered( ActionMapDefaults::kReloadEditorAction ) && bEnableEditor && forceReloadCallback.isBound() )
		{
			forceReloadCallback( config::kTargetEditorModule );
			SW_LOG_INFO( "[EngineLoop] %#: force EditorModule reload", ActionMapDefaults::kReloadEditorAction );
		}
		if ( _mapDebugAction->wasActionTriggered( ActionMapDefaults::kReloadGameAction ) && forceReloadCallback.isBound() )
		{
			forceReloadCallback( config::kTargetGameModule );
			SW_LOG_INFO( "[EngineLoop] %#: force SWGame reload", ActionMapDefaults::kReloadGameAction );
		}
#endif
	}
} // namespace sw
