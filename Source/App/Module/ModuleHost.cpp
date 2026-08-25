#include "pch.h"

#include "App/Module/ModuleHost.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Game/GameState.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RenderPass/RenderThread.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Utility/Module/LiveReloadManager.h"
#include "Engine/Utility/Task/TaskManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/NativeWindowEvent.h"

#include "RuntimeAPI/EditorService.h"
#include "RuntimeAPI/GameService.h"
#include "RuntimeAPI/PluginAPI.h"

#include "sw/config/ConfigConstants.h"

namespace
{
	sw::EditorUIContext* s_pEditorUIContext = nullptr;

	void* getEditorService( sw::EditorServiceId id )
	{
		switch ( id )
		{
			case sw::EditorServiceId::LocalizationManager:
				return &sw::engine::getLocalizationManager();
			case sw::EditorServiceId::TaskManager:
				return &sw::engine::getTaskManager();
			case sw::EditorServiceId::SceneManager:
				return &sw::engine::getSceneManager();
			case sw::EditorServiceId::GlobalVariableManager:
				return &sw::engine::getGlobalVariableManager();
			case sw::EditorServiceId::ResourceManager:
				return &sw::engine::getResourceManager();
			case sw::EditorServiceId::TypeRegistry:
				return &sw::engine::getTypeRegistry();
			case sw::EditorServiceId::MemoryProfiler:
				return sw::engine::getMemoryProfiler();
			case sw::EditorServiceId::UIContext:
				return s_pEditorUIContext;
			case sw::EditorServiceId::CommandStack:
				return &sw::engine::getCommandStack();
			case sw::EditorServiceId::DebugOverlayState:
				return &sw::engine::getDebugOverlayState();
			case sw::EditorServiceId::DebugDrawQueue:
				return &sw::engine::getDebugDrawQueue();
			case sw::EditorServiceId::EngineData:
				return const_cast<sw::EngineData*>( &sw::engine::getEngineData() );
		}
		return nullptr;
	}

	void* getGameService( sw::GameServiceId id )
	{
		switch ( id )
		{
			case sw::GameServiceId::LocalizationManager:
				return &sw::engine::getLocalizationManager();
			case sw::GameServiceId::EventDispatcher:
				return &sw::engine::getEventDispatcher();
			case sw::GameServiceId::GlobalVariableManager:
				return &sw::engine::getGlobalVariableManager();
			case sw::GameServiceId::AudioSystem:
				return &sw::engine::getAudioSystem();
			case sw::GameServiceId::TypeRegistry:
				return &sw::engine::getTypeRegistry();
			case sw::GameServiceId::InputManager:
				return &sw::engine::getInputManager();
			case sw::GameServiceId::SceneManager:
				return &sw::engine::getSceneManager();
			case sw::GameServiceId::ResourceManager:
				return &sw::engine::getResourceManager();
			case sw::GameServiceId::DebugDrawQueue:
				return &sw::engine::getDebugDrawQueue();
			case sw::GameServiceId::DebugOverlayState:
				return &sw::engine::getDebugOverlayState();
		}
		return nullptr;
	}
} // namespace

namespace sw
{
	ModuleHost::ModuleHost()
		: _editorApi{}
		, _gameApi{}
		, _editor{ nullptr }
		, _game{ nullptr }
		, _editorContext{}
		, _pLiveReloadManager{ nullptr }
		, _pRHI{ nullptr }
		, _pWindow{ nullptr }
		, _pRenderThread{ nullptr }
		, _bEnableEditor{ 0 }
		, _reserved{ 0 }
		, _pGameSavedStateBuffer{ nullptr }
		, _gameSavedStateCapacity{ 0 }
		, _gameSavedStateSize{ 0 }
	{
	}

	ModuleHost::~ModuleHost()
	{
		shutdown();

		if ( _pGameSavedStateBuffer != nullptr )
		{
			std::free( _pGameSavedStateBuffer );
			_pGameSavedStateBuffer	= nullptr;
			_gameSavedStateCapacity = 0;
			_gameSavedStateSize		= 0;
		}
	}

	bool ModuleHost::initialize( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread, bool bEnableEditor, const vector<GameKitConfig>& gameKitModuleList )
	{
		_pLiveReloadManager = pLiveReloadManager;
		_pRHI				= pRHI;
		_pWindow			= pWindow;
		_pRenderThread		= pRenderThread;
		_bEnableEditor		= bEnableEditor;

		if ( _pRHI != nullptr )
			_editorContext._pRHIDevice = &_pRHI->getDevice();

#if defined( SW_SHIPPING )
		(void)gameKitModuleList;
		onAfterGameReload( nullptr );
#else
		if ( _bEnableEditor && _pLiveReloadManager != nullptr )
		{
			BLOCK( "에디터: 뷰포트 / EditorModule 등록" )
			{
				_pLiveReloadManager->setOnBeforeReload( config::kTargetEditorModule,
														SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeEditorReload, this ) );
				_pLiveReloadManager->setOnAfterReload( config::kTargetEditorModule,
													   SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterEditorReload, this ) );
				if ( _pLiveReloadManager->registerModule( config::kTargetEditorModule ) == false )
				{
					SW_LOG_ERROR( "Editor Module 로드에 실패했습니다." );
					return false;
				}

				if ( _pLiveReloadManager->isGraphBroken() )
				{
					SW_LOG_ERROR( "[ModuleHost] LiveReload graph broken during module registration — aborting initialize" );
					return false;
				}
			}
		}

		if ( _pLiveReloadManager != nullptr )
		{
			BLOCK( "게임플레이 키트 및 SWGame 모듈 등록" )
			{
				const string   gameFrameWorkModule = "GameFramework";
				vector<string> gameModuleList{ gameFrameWorkModule };

				for ( const GameKitConfig& kitConfig : gameKitModuleList )
				{
					vector<string> listDeps = kitConfig._dependencyModuleList;
					if ( listDeps.empty() )
						listDeps.push_back( gameFrameWorkModule );

					if ( _pLiveReloadManager->registerModule( kitConfig._name, listDeps ) == false )
					{
						SW_LOG_ERROR( "[ModuleHost] Kit module register failed (%#)", kitConfig._name );
						return false;
					}
					_pLiveReloadManager->setOnBeforeReload( kitConfig._name, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeGameplayDllReload, this ) );
					_pLiveReloadManager->setOnAfterReload( kitConfig._name, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterGameplayDllReload, this ) );
					gameModuleList.push_back( kitConfig._name );
				}

				_pLiveReloadManager->setOnBeforeReload( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeGameReload, this ) );
				_pLiveReloadManager->setOnAfterReload( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterGameReload, this ) );

				if ( _pLiveReloadManager->registerModule( sw::config::kTargetGameModule, gameModuleList ) == false )
				{
					SW_LOG_ERROR( "[ModuleHost] SWGame module register failed" );
					return false;
				}

				if ( _pLiveReloadManager->isGraphBroken() )
				{
					SW_LOG_ERROR( "[ModuleHost] LiveReload graph broken during module registration — aborting initialize" );
					return false;
				}

				_pLiveReloadManager->setOnBeforeCommitBatch(
					SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeCommitBatchDelegate, &ModuleHost::onBeforeCommitBatch, this ) );
			}
		}
#endif

		return true;
	}

	void ModuleHost::shutdown()
	{
		onBeforeEditorReload();
		onBeforeGameReload();
		destroyGameViewportTexture();
	}

	// ======================================================================
	// 프레임 단위 처리
	// ======================================================================

	void ModuleHost::updateGame( float32 deltaTime )
	{
		const bool bGameplay =
			getGameState() == GameState::Playing;
		if ( _game != nullptr && _gameApi.update != nullptr && bGameplay )
			_gameApi.update( _game, deltaTime );
	}

	void ModuleHost::fixedUpdateGame( float32 fixedDeltaTime )
	{
		const bool bGameplay =
			getGameState() == GameState::Playing;
		if ( _game != nullptr && _gameApi.fixedUpdate != nullptr && bGameplay )
			_gameApi.fixedUpdate( _game, fixedDeltaTime );
	}

	bool ModuleHost::onWindowMessage( const NativeWindowEvent& event )
	{
		if ( _bEnableEditor == false || _editor == nullptr || _editorApi.processEvent == nullptr )
			return false;

		// 에디터 내부 상태 업데이트 및 입력 필터링은 Editor Module 내부에서 캡슐화 처리
		return _editorApi.processEvent( _editor, &event, &_editorContext );
	}

	void ModuleHost::processGameViewportResizeRequest()
	{
		if ( _editorContext._requestGameViewportWidth != 0 && _editorContext._requestGameViewportHeight != 0 )
		{
			const uint32 wantW						  = _editorContext._requestGameViewportWidth;
			const uint32 wantH						  = _editorContext._requestGameViewportHeight;
			_editorContext._requestGameViewportWidth  = 0;
			_editorContext._requestGameViewportHeight = 0;
			if ( wantW != _editorContext._gameViewportWidth || wantH != _editorContext._gameViewportHeight )
			{
				recreateGameViewportTexture( wantW, wantH );
			}
		}
	}

	// ======================================================================
	// LiveReload 콜백 — 에디터
	// ======================================================================

	void ModuleHost::onBeforeEditorReload()
	{
		drainRenderWorkers();

		if ( _editor != nullptr && _editorApi.shutdown != nullptr )
			_editorApi.shutdown( _editor );
		if ( _editor != nullptr && _editorApi.destroy != nullptr )
			_editorApi.destroy( _editor );
		if ( _editorApi.bindService != nullptr )
			_editorApi.bindService( nullptr );
		_editor	   = nullptr;
		_editorApi = {};
		engine::unregisterModuleTypes( sw::config::kTargetEditorModule );
	}

	void ModuleHost::onAfterEditorReload( void* pLibraryModule )
	{
		if ( bindEditorAPI( pLibraryModule ) == false )
		{
			poisonLiveReload( "EditorAPI bind failed after reload" );
			return;
		}

		_editor = _editorApi.create();
		if ( _editor == nullptr )
		{
			SW_LOG_ERROR( "[ModuleHost] Failed to create Editor instance" );
			_editorApi = {};
			poisonLiveReload( "Editor create failed after reload" );
			return;
		}

		if ( _editorApi.initialize( _editor, _pWindow, &_pRHI->getDevice() ) == false )
		{
			SW_LOG_ERROR( "[ModuleHost] Failed to initialize Editor instance" );
			if ( _editorApi.destroy != nullptr )
				_editorApi.destroy( _editor );
			_editor	   = nullptr;
			_editorApi = {};
			poisonLiveReload( "Editor initialize failed after reload" );
			return;
		}

		if ( _editorContext._gameRenderTarget != 0 && _editorApi.registerTexture != nullptr )
			_editorContext._pGameTextureID = _editorApi.registerTexture( _editor, static_cast<TextureHandle>( _editorContext._gameRenderTarget ) );

		SW_LOG_INFO( "[ModuleHost] Editor initialized successfully via EditorAPI." );
	}

	// ======================================================================
	// LiveReload 콜백 — 게임
	// ======================================================================

	void ModuleHost::onBeforeGameReload()
	{
		drainRenderWorkers();

		const GameState state = getGameState();
		if ( state == GameState::Playing || state == GameState::Paused )
		{
			SW_LOG_INFO( "[ModuleHost] Forcing GameState::Stopped before game module reload." );
			setGameState( GameState::Stopped );
		}

		if ( _game == nullptr )
			return;

		// 상태 보존 직렬화 시도
		if ( _gameApi.serializeState != nullptr )
		{
			uint32 size{ 0 };
			if ( _gameApi.serializeState( _game, nullptr, &size ) && size > 0 )
			{
				if ( size > _gameSavedStateCapacity )
				{
					if ( _pGameSavedStateBuffer != nullptr )
						std::free( _pGameSavedStateBuffer );
					_gameSavedStateCapacity = size;
					_pGameSavedStateBuffer	= static_cast<uint8*>( std::malloc( size ) );
				}
				_gameSavedStateSize = size;
				if ( _gameApi.serializeState( _game, _pGameSavedStateBuffer, &size ) == false )
					_gameSavedStateSize = 0;
			}
		}

		if ( _gameApi.shutdown != nullptr )
			_gameApi.shutdown( _game );
		if ( _gameApi.destroy != nullptr )
			_gameApi.destroy( _game );

		engine::unregisterModuleTypes( sw::config::kTargetGameModule );

		if ( _gameApi.bindService != nullptr )
			_gameApi.bindService( nullptr );

		_game	 = nullptr;
		_gameApi = {};
	}

	void ModuleHost::onAfterGameReload( void* pLibraryModule )
	{
#if defined( SW_SHIPPING )
		(void)pLibraryModule;
		if ( _gameApi.create == nullptr && bindGameAPI( nullptr ) == false )
			return;
#else
		void* pModuleHandle = pLibraryModule;
		if ( pModuleHandle == nullptr && _pLiveReloadManager != nullptr )
			pModuleHandle = _pLiveReloadManager->getModuleHandle( sw::config::kTargetGameModule );

		if ( bindGameAPI( pModuleHandle ) == false )
		{
			poisonLiveReload( "GameAPI bind failed after reload" );
			return;
		}
#endif

		_game = _gameApi.create();
		if ( _game == nullptr )
		{
			SW_LOG_ERROR( "[ModuleHost] Failed to create Game instance" );
			_gameApi = {};
			poisonLiveReload( "Game create failed after reload" );
			return;
		}

		if ( _gameApi.initialize( _game, _pWindow, &_pRHI->getDevice() ) == false )
		{
			SW_LOG_ERROR( "[ModuleHost] Failed to initialize Game instance" );
			if ( _gameApi.destroy != nullptr )
				_gameApi.destroy( _game );
			_game	 = nullptr;
			_gameApi = {};
			poisonLiveReload( "Game initialize failed after reload" );
			return;
		}

		// 상태 복원 역직렬화
		if ( _gameApi.deserializeState != nullptr && _gameSavedStateSize > 0 )
		{
			if ( _gameApi.deserializeState( _game, _pGameSavedStateBuffer, static_cast<uint32>( _gameSavedStateSize ) ) )
				SW_LOG_INFO( "[ModuleHost] Game state successfully restored from %zu bytes.", _gameSavedStateSize );
			else
				SW_LOG_ERROR( "[ModuleHost] Failed to restore game state." );

			_gameSavedStateSize = 0;
		}

		SW_LOG_INFO( "[ModuleHost] SWGame initialized successfully via GameAPI." );
	}

	// ======================================================================
	// LiveReload 콜백 — GameFramework/Kit DLL 캐스케이드
	// ======================================================================

	void ModuleHost::onBeforeGameplayDllReload()
	{
		onBeforeGameReload();

		if ( engine::areEngineServicesBound() == false )
			return;
		for ( const unique_ptr<Scene>& scene : engine::getSceneManager().getLoadedScenes() )
		{
			if ( scene != nullptr )
				scene->getObjectManager()->clearAllCachedTypeInfo();
		}
	}

	void ModuleHost::onAfterGameplayDllReload( void* /*hLibraryModule*/ )
	{
		if ( engine::areEngineServicesBound() == false )
			return;
		for ( const unique_ptr<Scene>& scene : engine::getSceneManager().getLoadedScenes() )
		{
			if ( scene != nullptr )
				scene->getObjectManager()->rebindAllCachedTypeInfo();
		}
	}

	void ModuleHost::onBeforeCommitBatch( const vector<string>& moduleNames )
	{
#if !defined( SW_SHIPPING )
		for ( const string& name : moduleNames )
		{
			if ( name != sw::config::kTargetEditorModule )
			{
				onBeforeGameplayDllReload();
				return;
			}
		}
#else
		(void)moduleNames;
#endif
	}

	// ======================================================================
	// 보조 — drain / poison
	// ======================================================================

	void ModuleHost::drainRenderWorkers()
	{
		if ( _pRenderThread != nullptr )
			_pRenderThread->waitIdle();
		if ( _pRHI != nullptr )
			_pRHI->getDevice().waitIdle();

		// 비동기 태스크 펜싱 (Module Unload 전 안전 보장)
		if ( engine::areEngineServicesBound() )
		{
			if ( engine::getTaskManager().waitAll( 5000 ) == false )
			{
				SW_LOG_WARNING( "[ModuleHost] Task fencing timeout (5s) before module reload." );
			}
		}
	}

	void ModuleHost::poisonLiveReload( const utf8* pReason )
	{
#if !defined( SW_SHIPPING )
		if ( _pLiveReloadManager != nullptr )
			_pLiveReloadManager->markGraphBroken( pReason != nullptr ? pReason : "unspecified" );
#else
		(void)pReason;
#endif
	}

	// ======================================================================
	// API 바인딩
	// ======================================================================

	bool ModuleHost::bindEditorAPI( void* pLibraryModule )
	{
		_editorApi			   = {};
		_editorApi._abiVersion = kEditorAPIAbiVersion;
		_editorApi._structSize = static_cast<uint32>( sizeof( EditorAPI ) );
		if ( pLibraryModule == nullptr )
			return false;

		PFN_FillEditorAPI pfnFill = reinterpret_cast<PFN_FillEditorAPI>( FileUtil::getDynamicSymbol( pLibraryModule, "fillEditorAPI" ) );
		if ( pfnFill == nullptr || pfnFill( &_editorApi ) == false )
		{
			SW_LOG_ERROR( "[ModuleHost] Failed to bind EditorAPI from module" );
			return false;
		}
		if ( _editorApi._abiVersion != kEditorAPIAbiVersion )
		{
			SW_LOG_ERROR( "[ModuleHost] EditorAPI ABI mismatch (got %# expected %#)", _editorApi._abiVersion, kEditorAPIAbiVersion );
			_editorApi = {};
			return false;
		}

		if ( _editorApi.bindService != nullptr )
		{
			s_pEditorUIContext = &_editorContext;
			EditorService es{};
			es.getService = getEditorService;
			_editorApi.bindService( &es );
		}

		engine::registerModuleTypes( sw::config::kTargetEditorModule );
		return _editorApi.create != nullptr && _editorApi.destroy != nullptr;
	}

	bool ModuleHost::bindGameAPI( void* pLibraryModule )
	{
		_gameApi			 = {};
		_gameApi._abiVersion = kGameAPIAbiVersion;
		_gameApi._structSize = static_cast<uint32>( sizeof( GameAPI ) );
#if defined( SW_SHIPPING )
		(void)pLibraryModule;
		if ( fillGameAPI( &_gameApi ) == false )
		{
			SW_LOG_ERROR( "[ModuleHost] Failed to bind GameAPI (shipping)" );
			return false;
		}
#else
		if ( pLibraryModule == nullptr )
			return false;
		PFN_FillGameAPI pfnFill = reinterpret_cast<PFN_FillGameAPI>( FileUtil::getDynamicSymbol( pLibraryModule, "fillGameAPI" ) );
		if ( pfnFill == nullptr || pfnFill( &_gameApi ) == false )
		{
			SW_LOG_ERROR( "[ModuleHost] Failed to bind GameAPI from module" );
			return false;
		}
#endif
		if ( _gameApi._abiVersion != kGameAPIAbiVersion )
		{
			SW_LOG_ERROR( "[ModuleHost] GameAPI ABI mismatch (got %# expected %#)", _gameApi._abiVersion, kGameAPIAbiVersion );
			_gameApi = {};
			return false;
		}

		if ( _gameApi.bindService != nullptr )
		{
			GameService gs{};
			gs.getService			   = getGameService;
			gs._pLocalizationManager   = &sw::engine::getLocalizationManager();
			gs._pEventDispatcher	   = &sw::engine::getEventDispatcher();
			gs._pGlobalVariableManager = &sw::engine::getGlobalVariableManager();
			gs._pAudioSystem		   = &sw::engine::getAudioSystem();
			gs._pTypeRegistry		   = &sw::engine::getTypeRegistry();
			gs._pInputManager		   = &sw::engine::getInputManager();
			gs._pSceneManager		   = &sw::engine::getSceneManager();
			gs._pResourceManager	   = &sw::engine::getResourceManager();
			gs._pDebugDrawQueue		   = &sw::engine::getDebugDrawQueue();
			gs._pDebugOverlayState	   = &sw::engine::getDebugOverlayState();
			_gameApi.bindService( &gs );
		}

		engine::registerModuleTypes( sw::config::kTargetGameModule );
		return _gameApi.create != nullptr && _gameApi.destroy != nullptr;
	}

	// ======================================================================
	// Game View RT
	// ======================================================================

	bool ModuleHost::createGameViewportTexture( uint32 width, uint32 height )
	{
		if ( _pRHI == nullptr || width == 0 || height == 0 )
			return false;

		RHITextureDesc rtDesc{};
		rtDesc._width			  = width;
		rtDesc._height			  = height;
		rtDesc._format			  = RHIFormat::R8G8B8A8_UNORM;
		rtDesc._bIsRenderTarget	  = true;
		rtDesc._bIsShaderResource = true;
		rtDesc._mipLevels		  = 1;
		rtDesc._arrClearColor[0]  = _editorContext._arrClearColor[0];
		rtDesc._arrClearColor[1]  = _editorContext._arrClearColor[1];
		rtDesc._arrClearColor[2]  = _editorContext._arrClearColor[2];
		rtDesc._arrClearColor[3]  = _editorContext._arrClearColor[3];

		_editorContext._gameRenderTarget   = _pRHI->getDevice().getResource()->createTexture2D( rtDesc );
		_editorContext._gameViewportWidth  = width;
		_editorContext._gameViewportHeight = height;
		return _editorContext._gameRenderTarget != 0;
	}

	void ModuleHost::destroyGameViewportTexture()
	{
		drainRenderWorkers();

		if ( _editorContext._pGameTextureID != nullptr && _editor != nullptr && _editorApi.unregisterTexture != nullptr )
		{
			_editorApi.unregisterTexture( _editor, _editorContext._pGameTextureID );
			_editorContext._pGameTextureID = nullptr;
		}

		if ( _editorContext._gameRenderTarget != 0 && _pRHI != nullptr && _pRHI->getDevice().getNativeDevice() != nullptr )
		{
			_pRHI->getDevice().getResource()->destroyTexture( _editorContext._gameRenderTarget );
			_editorContext._gameRenderTarget = 0;
			_editorContext._pGameTextureID	 = nullptr;
		}
	}

	bool ModuleHost::recreateGameViewportTexture( uint32 width, uint32 height )
	{
		if ( _pRHI == nullptr || width == 0 || height == 0 )
			return false;

		destroyGameViewportTexture();

		if ( createGameViewportTexture( width, height ) == false )
		{
			SW_LOG_WARNING( "[ModuleHost] Game View RT recreate failed (%# x %#)", width, height );
			_editorContext._pGameTextureID = nullptr;
			return false;
		}

		if ( _editor != nullptr && _editorApi.registerTexture != nullptr )
			_editorContext._pGameTextureID = _editorApi.registerTexture( _editor, static_cast<TextureHandle>( _editorContext._gameRenderTarget ) );

		return true;
	}

	// ======================================================================
	// notifyModulesReady
	// ======================================================================

	void ModuleHost::notifyModulesReady()
	{
		// Splash 해제 로직이 필요하면 여기에 구현합니다.
	}

	// ======================================================================
	// RHI 핫스왑 후 재초기화
	// ======================================================================
	// ... (Game View RT)

	bool ModuleHost::reinitializeAfterRhiSwap( void* pEditorModule, void* pGameModule )
	{
		if ( _bEnableEditor )
		{
			// 핫스왑 전 destroyGameViewportTexture()가 먼저 호출되므로 크기는 이전 값이 보존됩니다.
			// 단, 뷰포트가 한 번도 생성되지 않았다면 0이 될 수 있으므로 기본값으로 폴백합니다.
			const uint32 vpW = _editorContext._gameViewportWidth > 0 ? _editorContext._gameViewportWidth : 1280;
			const uint32 vpH = _editorContext._gameViewportHeight > 0 ? _editorContext._gameViewportHeight : 720;
			if ( createGameViewportTexture( vpW, vpH ) == false )
				SW_LOG_WARNING( "[Hot-Swap] Game viewport texture recreate failed (%# x %#).", vpW, vpH );
			onAfterEditorReload( pEditorModule );
		}

#if defined( SW_SHIPPING )
		(void)pGameModule;
		onAfterGameReload( nullptr );
#else
		onAfterGameReload( pGameModule );
#endif

		_editorContext._pRHIDevice = &_pRHI->getDevice();
		return true;
	}
} // namespace sw
