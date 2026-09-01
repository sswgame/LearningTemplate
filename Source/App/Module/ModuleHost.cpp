#include "pch.h"

#include "App/Module/ModuleHost.h"

#include "App/Module/ModuleCompiler.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RenderPass/RenderThread.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Utility/Module/LiveReloadManager.h"
#include "Engine/Utility/Module/ModuleTypeRegistry.h"
#include "Engine/Window/IWindow.h"

#include "RuntimeAPI/Service/ModuleService.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
	namespace
	{
		ModuleHost* s_pCurrentModuleHost{ nullptr };

		enum class ModuleTarget : uint8
		{
			Editor,
			Game
		};

		template <ModuleTarget Target>
		void buildModuleService( ModuleService& outService )
		{
			engine::fillModuleServices( outService, Target == ModuleTarget::Game );

#define SW_HOST_SERVICE( member, Tag, Type, getter, gameAllowed )                             \
	if constexpr ( Target == ModuleTarget::Editor || ( ( gameAllowed ) == 1 ) )               \
	{                                                                                         \
		outService.arrServices[internal::toRawServiceId( internal::ModuleServiceId::Type )] = \
			( s_pCurrentModuleHost != nullptr ) ? s_pCurrentModuleHost->getter() : nullptr;   \
	}

#include "RuntimeAPI/Service/HostServiceList.xxx"
#undef SW_HOST_SERVICE
		}
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "ModuleHost" );

	ModuleHost::ModuleHost()
		: _moduleCompiler{ nullptr }
		, _editorApi{}
		, _gameApi{}
		, _editor{ nullptr }
		, _game{ nullptr }
		, _pLiveReloadManager{ nullptr }
		, _pRHI{ nullptr }
		, _pWindow{ nullptr }
		, _pRenderThread{ nullptr }
		, _listGameSavedState{}
		, _bEnableEditor{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	ModuleHost::~ModuleHost()
	{
		shutdown();
	}

	bool ModuleHost::initialize( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread, bool bEnableEditor, const vector<GameKitConfig>& listGameKitModule )
	{
		s_pCurrentModuleHost = this;
		_pLiveReloadManager	 = pLiveReloadManager;
		_pRHI				 = pRHI;
		_pWindow			 = pWindow;
		_pRenderThread		 = pRenderThread;
		_bEnableEditor		 = bEnableEditor ? SW_TRUE : SW_FALSE;

#if !defined( SW_SHIPPING )
		_moduleCompiler = make_unique<ModuleCompiler>( _pLiveReloadManager );
		if ( _pLiveReloadManager != nullptr )
		{
			_pLiveReloadManager->setDrainWorkers(
				SW_DELEGATE_METHOD( LiveReloadManager::DrainWorkersDelegate, &ModuleHost::drainRenderWorkers, this ) );
		}
#endif

#if defined( SW_SHIPPING )
		(void)listGameKitModule;
		onAfterGameReload( nullptr );
#else
		if ( _bEnableEditor == SW_TRUE && _pLiveReloadManager != nullptr )
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
					SW_LOG_ERROR( "LiveReload graph broken during module registration — aborting initialize" );
					return false;
				}
			}
		}

		if ( _pLiveReloadManager != nullptr )
		{
			BLOCK( "게임플레이 키트 및 SWGame 모듈 등록" )
			{
				const string   gameFrameWorkModule = "GameFramework";
				vector<string> listGameModule{ gameFrameWorkModule };

				for ( const GameKitConfig& kitConfig : listGameKitModule )
				{
					vector<string> listDep = kitConfig._listDependencyModule;
					if ( listDep.empty() )
						listDep.push_back( gameFrameWorkModule );

					if ( _pLiveReloadManager->registerModule( kitConfig._name, listDep ) == false )
					{
						SW_LOG_ERROR( "Kit module register failed (%#)", kitConfig._name );
						return false;
					}
					_pLiveReloadManager->setOnBeforeReload( kitConfig._name, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeGameplayDllReload, this ) );
					_pLiveReloadManager->setOnAfterReload( kitConfig._name, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterGameplayDllReload, this ) );
					listGameModule.push_back( kitConfig._name );
				}

				_pLiveReloadManager->setOnBeforeReload( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeGameReload, this ) );
				_pLiveReloadManager->setOnAfterReload( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterGameReload, this ) );

				if ( _pLiveReloadManager->registerModule( sw::config::kTargetGameModule, listGameModule ) == false )
				{
					SW_LOG_ERROR( "SWGame module register failed" );
					return false;
				}

				if ( _pLiveReloadManager->isGraphBroken() )
				{
					SW_LOG_ERROR( "LiveReload graph broken during module registration — aborting initialize" );
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
		if ( _moduleCompiler != nullptr )
		{
			_moduleCompiler->shutdown();
			_moduleCompiler.reset();
		}

		if ( s_pCurrentModuleHost == this )
			s_pCurrentModuleHost = nullptr;

		onBeforeEditorReload();
		onBeforeGameReload();

		if ( _pLiveReloadManager != nullptr )
		{
			_pLiveReloadManager->setDrainWorkers( {} );
			_pLiveReloadManager->setOnBeforeCommitBatch( {} );
		}
	}

	// ======================================================================
	// 프레임 단위 처리
	// ======================================================================

	void ModuleHost::updateEditorUI( float32 deltaTime )
	{
		std::ignore = deltaTime;
		if ( _bEnableEditor == SW_FALSE || _editor == nullptr || _editorApi.updateUI == nullptr )
			return;

		_editorApi.updateUI( _editor );
	}

	void ModuleHost::updateGame( float32 deltaTime )
	{
		const bool bGameplay = ( _bEnableEditor == SW_FALSE || _editor == nullptr || _editorApi.isPlaying == nullptr || _editorApi.isPlaying( _editor ) );
		if ( _game != nullptr && _gameApi.update != nullptr && bGameplay )
			_gameApi.update( _game, deltaTime );
	}

	void ModuleHost::fixedUpdateGame( float32 fixedDeltaTime )
	{
		const bool bGameplay = ( _bEnableEditor == SW_FALSE || _editor == nullptr || _editorApi.isPlaying == nullptr || _editorApi.isPlaying( _editor ) );
		if ( _game != nullptr && _gameApi.fixedUpdate != nullptr && bGameplay )
			_gameApi.fixedUpdate( _game, fixedDeltaTime );
	}

	bool ModuleHost::onWindowMessage( const NativeWindowEvent& event )
	{
		if ( _bEnableEditor == SW_FALSE || _editor == nullptr || _editorApi.processEvent == nullptr )
			return false;

		// 에디터 내부 상태 업데이트 및 입력 필터링은 Editor Module 내부에서 캡슐화 처리
		return _editorApi.processEvent( _editor, &event );
	}

	void ModuleHost::getGameViewport( uint64& renderTarget, uint32& width, uint32& height ) const
	{
		renderTarget = 0;
		width		 = 0;
		height		 = 0;
		if ( _bEnableEditor == SW_FALSE || _editor == nullptr || _editorApi.getGameViewport == nullptr )
			return;

		_editorApi.getGameViewport( _editor, &renderTarget, &width, &height );
	}

	CameraComponent* ModuleHost::getViewportCamera() const
	{
		if ( _bEnableEditor == SW_FALSE || _editor == nullptr || _editorApi.getViewportCamera == nullptr )
			return nullptr;
		return static_cast<CameraComponent*>( _editorApi.getViewportCamera( _editor ) );
	}

	bool ModuleHost::shouldTickScene() const
	{
		if ( _bEnableEditor == SW_FALSE || _editor == nullptr )
			return true;
		if ( _editorApi.isPaused != nullptr && _editorApi.isPaused( _editor ) )
		{
			if ( _editorApi.isPlaying == nullptr || _editorApi.isPlaying( _editor ) == false )
				return false;
		}
		return true;
	}

	void ModuleHost::endEditorFrame()
	{
		if ( _bEnableEditor == SW_FALSE || _editor == nullptr || _editorApi.endFrame == nullptr )
			return;
		_editorApi.endFrame( _editor );
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
			SW_LOG_ERROR( "Failed to create Editor instance" );
			_editorApi = {};
			poisonLiveReload( "Editor create failed after reload" );
			return;
		}

		if ( _editorApi.initialize( _editor, _pWindow, &_pRHI->getDevice() ) == false )
		{
			SW_LOG_ERROR( "Failed to initialize Editor instance" );
			if ( _editorApi.destroy != nullptr )
				_editorApi.destroy( _editor );
			_editor	   = nullptr;
			_editorApi = {};
			poisonLiveReload( "Editor initialize failed after reload" );
			return;
		}

		SW_LOG_INFO( "Editor initialized successfully via EditorAPI." );
	}

	// ======================================================================
	// LiveReload 콜백 — 게임
	// ======================================================================

	void ModuleHost::onBeforeGameReload()
	{
		drainRenderWorkers();

		if ( _bEnableEditor == SW_TRUE && _editor != nullptr && _editorApi.stopSimulation != nullptr )
		{
			SW_LOG_INFO( "Stopping editor simulation before game module reload." );
			_editorApi.stopSimulation( _editor );
		}

		if ( _game == nullptr )
			return;

		captureGameState();

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
			SW_LOG_ERROR( "Failed to create Game instance" );
			_gameApi = {};
			poisonLiveReload( "Game create failed after reload" );
			return;
		}

		if ( _gameApi.initialize( _game, _pWindow, &_pRHI->getDevice() ) == false )
		{
			SW_LOG_ERROR( "Failed to initialize Game instance" );
			if ( _gameApi.destroy != nullptr )
				_gameApi.destroy( _game );
			_game	 = nullptr;
			_gameApi = {};
			poisonLiveReload( "Game initialize failed after reload" );
			return;
		}

		restoreGameState();

		SW_LOG_INFO( "SWGame initialized successfully via GameAPI." );
	}

	// ======================================================================
	// LiveReload 콜백 — GameFramework/Kit DLL 캐스케이드
	// ======================================================================

	void ModuleHost::onBeforeGameplayDllReload()
	{
		onBeforeGameReload();
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

	void ModuleHost::onBeforeCommitBatch( const vector<string>& listModuleName )
	{
#if !defined( SW_SHIPPING )
		for ( const string& name : listModuleName )
		{
			if ( name != sw::config::kTargetEditorModule )
			{
				onBeforeGameplayDllReload();
				return;
			}
		}
#else
		(void)listModuleName;
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

		// 비동기 태스크 펜싱 (Module Unload 전 안전 보장). 타임아웃은 LiveReloadManager 폴백과 공유합니다.
		if ( engine::areEngineServicesBound() )
		{
			if ( engine::getTaskManager().waitAll( LiveReloadManager::kModuleDrainTimeoutMs ) == false )
			{
				SW_LOG_ERROR( "Task fencing timeout (%# ms) before module reload — poisoning LiveReload graph.", LiveReloadManager::kModuleDrainTimeoutMs );
				poisonLiveReload( "task fencing timeout before unload" );
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
		_editorApi = {};
		if ( pLibraryModule == nullptr )
			return false;

		PFN_ExportEditorAPI pfnExport = reinterpret_cast<PFN_ExportEditorAPI>( FileUtil::getDynamicSymbol( pLibraryModule, "exportEditorAPI" ) );
		if ( pfnExport == nullptr || pfnExport( &_editorApi ) == false )
		{
			SW_LOG_ERROR( "Failed to bind EditorAPI from module" );
			return false;
		}

		if ( _editorApi.bindService != nullptr )
		{
			ModuleService editorService{};
			buildModuleService<ModuleTarget::Editor>( editorService );
			_editorApi.bindService( &editorService );
		}

		engine::registerModuleTypes( sw::config::kTargetEditorModule );
		return _editorApi.create != nullptr && _editorApi.destroy != nullptr;
	}

	bool ModuleHost::bindGameAPI( void* pLibraryModule )
	{
		_gameApi = {};
#if defined( SW_SHIPPING )
		(void)pLibraryModule;
		if ( exportGameAPI( &_gameApi ) == false )
		{
			SW_LOG_ERROR( "Failed to bind GameAPI (shipping)" );
			return false;
		}
#else
		if ( pLibraryModule == nullptr )
			return false;
		PFN_ExportGameAPI pfnExport = reinterpret_cast<PFN_ExportGameAPI>( FileUtil::getDynamicSymbol( pLibraryModule, "exportGameAPI" ) );
		if ( pfnExport == nullptr || pfnExport( &_gameApi ) == false )
		{
			SW_LOG_ERROR( "Failed to bind GameAPI from module" );
			return false;
		}
#endif

		if ( _gameApi.bindService != nullptr )
		{
			ModuleService gameService{};
			buildModuleService<ModuleTarget::Game>( gameService );
			_gameApi.bindService( &gameService );
		}

		engine::registerModuleTypes( sw::config::kTargetGameModule );
		return _gameApi.create != nullptr && _gameApi.destroy != nullptr;
	}

	void ModuleHost::onBeforeRhiSwap()
	{
		drainRenderWorkers();
		captureGameState();

		if ( _editor != nullptr && _editorApi.shutdown != nullptr )
			_editorApi.shutdown( _editor );
		if ( _editor != nullptr && _editorApi.destroy != nullptr )
			_editorApi.destroy( _editor );
		if ( _editorApi.bindService != nullptr )
			_editorApi.bindService( nullptr );
		_editor = nullptr;

		if ( _game != nullptr && _gameApi.shutdown != nullptr )
			_gameApi.shutdown( _game );
		if ( _game != nullptr && _gameApi.destroy != nullptr )
			_gameApi.destroy( _game );
		if ( _gameApi.bindService != nullptr )
			_gameApi.bindService( nullptr );
		_game = nullptr;
	}

	bool ModuleHost::reinitializeAfterRhiSwap( void* pEditorModule, void* pGameModule )
	{
		if ( _pRHI == nullptr || _pRHI->hasDevice() == false )
			return false;

		bool bOk = true;
		if ( recreateEditorInstance( pEditorModule ) == false )
			bOk = false;
		if ( recreateGameInstance( pGameModule ) == false )
			bOk = false;
		return bOk;
	}

	void ModuleHost::captureGameState()
	{
		if ( _game == nullptr || _gameApi.serializeState == nullptr )
			return;

		uint32 size{ 0 };
		if ( _gameApi.serializeState( _game, nullptr, &size ) == false || size == 0 )
			return;

		vector<uint8> tempState( size );
		if ( _gameApi.serializeState( _game, tempState.data(), &size ) )
			_listGameSavedState = std::move( tempState );
	}

	void ModuleHost::restoreGameState()
	{
		if ( _game == nullptr || _gameApi.deserializeState == nullptr || _listGameSavedState.empty() )
			return;

		if ( _gameApi.deserializeState( _game, _listGameSavedState.data(), static_cast<uint32>( _listGameSavedState.size() ) ) )
		{
			SW_LOG_INFO( "Scene object state restored from %zu bytes.", _listGameSavedState.size() );
			_listGameSavedState.clear();
		}
		else
		{
			SW_LOG_ERROR( "Failed to restore game state — keeping saved state for subsequent reload." );
		}
	}

	bool ModuleHost::recreateEditorInstance( void* pEditorModule )
	{
		if ( _bEnableEditor == SW_FALSE )
			return true;

		if ( _editorApi.create == nullptr || _editorApi.initialize == nullptr )
		{
			onAfterEditorReload( pEditorModule );
			return _editor != nullptr;
		}

		if ( _editorApi.bindService != nullptr )
		{
			ModuleService editorService{};
			buildModuleService<ModuleTarget::Editor>( editorService );
			_editorApi.bindService( &editorService );
		}

		_editor = _editorApi.create();
		if ( _editor == nullptr )
		{
			SW_LOG_ERROR( "Failed to create Editor instance after RHI swap" );
			return false;
		}

		if ( _editorApi.initialize( _editor, _pWindow, &_pRHI->getDevice() ) == false )
		{
			SW_LOG_ERROR( "Failed to initialize Editor instance after RHI swap" );
			if ( _editorApi.destroy != nullptr )
				_editorApi.destroy( _editor );
			_editor = nullptr;
			return false;
		}

		return true;
	}

	bool ModuleHost::recreateGameInstance( void* pGameModule )
	{
		if ( _gameApi.create == nullptr || _gameApi.initialize == nullptr )
		{
#if defined( SW_SHIPPING )
			(void)pGameModule;
			onAfterGameReload( nullptr );
#else
			onAfterGameReload( pGameModule );
#endif
			return _game != nullptr;
		}

		if ( _gameApi.bindService != nullptr )
		{
			ModuleService gameService{};
			buildModuleService<ModuleTarget::Game>( gameService );
			_gameApi.bindService( &gameService );
		}

		_game = _gameApi.create();
		if ( _game == nullptr )
		{
			SW_LOG_ERROR( "Failed to create Game instance after RHI swap" );
			return false;
		}

		if ( _gameApi.initialize( _game, _pWindow, &_pRHI->getDevice() ) == false )
		{
			SW_LOG_ERROR( "Failed to initialize Game instance after RHI swap" );
			if ( _gameApi.destroy != nullptr )
				_gameApi.destroy( _game );
			_game = nullptr;
			return false;
		}

		restoreGameState();
		return true;
	}
} // namespace sw
