#include "pch.h"

#include "Engine/Utility/Module/LiveReloadManager.h"

#include "Core/File/IFileWatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/PluginAPI.h"

#include <algorithm>
#include <chrono>

#if defined( SW_PLATFORM_WINDOWS )
	#include "Core/File/Windows/WindowsFileWatcher.h"
#elif defined( SW_PLATFORM_LINUX )
	#include "Core/File/Linux/LinuxFileWatcher.h"
#elif defined( SW_PLATFORM_MACOS )
	#include "Core/File/Mac/MacFileWatcher.h"
#endif

namespace sw
{
	SW_LOG_CALLER( "LiveReloadManager" );

	namespace
	{
		static LiveReloadManager* s_delayLoadManager{ nullptr };

		namespace
		{
			uint32 s_reloadCount{ 0 };

			void tryDeleteFile( string_view path )
			{
				FileUtil::removeFile( path );
			}

			void tryDeleteShadowArtifacts( string_view modulePath )
			{
				if ( modulePath.empty() )
					return;
				tryDeleteFile( modulePath );
				tryDeleteFile( FileUtil::getDebugSymbolPath( modulePath ) );
			}

			bool copyFileWithRetry( string_view source, string_view destination )
			{
				for ( int32 retryIndex = 0; retryIndex < 10; ++retryIndex )
				{
					if ( FileUtil::copyFile( source, destination ) )
						return true;
					std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
				}
				return false;
			}

			void copyDebugSymbolsIfPresent( string_view originalModulePath, string_view shadowModulePath )
			{
				const string originalDebugPath = FileUtil::getDebugSymbolPath( originalModulePath );
				const string shadowDebugPath   = FileUtil::getDebugSymbolPath( shadowModulePath );
				if ( FileUtil::fileExists( originalDebugPath ) == false )
					return;

				if ( FileUtil::copyFile( originalDebugPath, shadowDebugPath ) == false )
					SW_LOG_WARNING( "Failed to copy debug symbols: %#", shadowDebugPath.c_str() );
			}

			void cleanStaleShadowArtifacts( string_view directoryPath )
			{
				vector<string> listFiles;
				if ( FileUtil::collectFiles( directoryPath, "", listFiles, false ) == false )
					return;

				for ( const string& filePath : listFiles )
				{
					if ( filePath.find( "_temp_" ) != string::npos )
					{
						FileUtil::removeFile( filePath );
					}
				}
			}

		} // namespace
	} // namespace

	LiveReloadManager::LiveReloadManager()
		: _mapModule{}
		, _fileWatcher{
#if defined( SW_PLATFORM_WINDOWS )
			  make_unique<WindowsFileWatcher>()
#elif defined( SW_PLATFORM_LINUX )
			  make_unique<LinuxFileWatcher>()
#elif defined( SW_PLATFORM_MACOS )
			  make_unique<MacFileWatcher>()
#else
			  nullptr
#endif
		  }
		, _onBeforeCommitBatch{}
		, _drainWorkers{}
		, _bReloadGraphBroken{ false }
		, _bReloadingBatch{ false }
	{
		cleanStaleShadowArtifacts( FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ) );

		if ( LiveReloadManager::getDelayLoadManager() == nullptr )
			LiveReloadManager::setDelayLoadManager( this );
	}

	LiveReloadManager::~LiveReloadManager()
	{
		shutdown();
	}

	void LiveReloadManager::shutdown()
	{
		_drainWorkers		 = {};
		_onBeforeCommitBatch = {};

		for ( auto& [name, ctx] : _mapModule )
		{
			ctx._onBeforeReload = {};
			ctx._onAfterReload	= {};
		}

		cleanStaleShadowArtifacts( FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ) );
		if ( LiveReloadManager::getDelayLoadManager() == this )
			LiveReloadManager::setDelayLoadManager( nullptr );

		if ( _fileWatcher != nullptr )
		{
			_fileWatcher->stopWatching();
			_fileWatcher.reset();
		}

		if ( _bReloadGraphBroken )
		{
			for ( auto& [moduleName, moduleContext] : _mapModule )
			{
				unloadModule( moduleContext );
			}
			_mapModule.clear();
			return;
		}

		vector<string> listNames;
		listNames.reserve( _mapModule.size() );
		for ( const auto& [name, ctx] : _mapModule )
		{
			listNames.push_back( name );
		}

		vector<string> listOrder;
		if ( topoSortSubgraph( listNames, listOrder ) == false )
		{
			SW_LOG_WARNING( "Shutdown topo sort failed (cycle?) — unloading in name order" );
			listOrder = listNames;
			std::sort( listOrder.begin(), listOrder.end() );
		}

		// Dependents first (reverse of dependency-first reload order).
		for ( auto it = listOrder.rbegin(); it != listOrder.rend(); ++it )
		{
			auto found = _mapModule.find( *it );
			if ( found != _mapModule.end() )
				unloadModule( found->second );
		}
		_mapModule.clear();
	}

	bool LiveReloadManager::registerModule( string_view moduleName, const vector<string>& listDependsOn )
	{
		ModuleContext& moduleContext = _mapModule[string( moduleName )];
		moduleContext._moduleName	 = moduleName;
		moduleContext._listDependsOn = listDependsOn;

		const string execDir = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );

		if ( _fileWatcher && _fileWatcher->isWatching() == false )
		{
			_fileWatcher->startWatching( execDir, false ); // no recursive for DLLs
			SW_LOG_INFO( "Started FileWatcher on directory: %#", execDir.c_str() );
		}

		moduleContext._originalModulePath = FileUtil::joinPath( execDir, FileUtil::formatSharedLibraryName( moduleName ) );
		return loadShadowCopyModule( moduleContext );
	}

	void LiveReloadManager::triggerReload( string_view moduleName )
	{
		if ( _bReloadGraphBroken )
		{
			SW_LOG_ERROR( "Reload graph is broken — restart the process (ignored %#)", moduleName );
			return;
		}

		auto iter = _mapModule.find( string( moduleName ) );
		if ( iter == _mapModule.end() )
			return;

		ModuleContext& ctx = iter->second;
		SW_LOG_INFO( "Manual Live Reload queued for %# (debounce %#ms)...",
					 moduleName, kMtimeDebounceMs );
		ctx._debounceMtime	  = FileUtil::getFileTimestamp( ctx._originalModulePath );
		ctx._debounceSince	  = std::chrono::steady_clock::now();
		ctx._bMtimeDebouncing = true;
		ctx._bForceReload	  = true;
	}

	void LiveReloadManager::update()
	{
		vector<FileChangeEvent> listEvents;

		BLOCK( "Poll File Events" )
		{
			if ( _fileWatcher != nullptr )
				_fileWatcher->pollEvents( listEvents );
		}

		const auto now = std::chrono::steady_clock::now();

		for ( const FileChangeEvent& ev : listEvents )
		{
			if ( ev._action == FileWatcherAction::Modified )
			{
				string fullPath = FileUtil::joinPath( ev._directory, ev._filename );

				for ( auto& [moduleName, moduleContext] : _mapModule )
				{
					if ( moduleContext._originalModulePath != fullPath )
						continue;

					const uint64 sourceMtime = FileUtil::getFileTimestamp( moduleContext._originalModulePath );
					if ( sourceMtime <= moduleContext._loadedSourceMtime )
						continue;

					moduleContext._debounceMtime	= sourceMtime;
					moduleContext._debounceSince	= now;
					moduleContext._bMtimeDebouncing = true;
				}
			}
		}

		for ( auto& [name, ctx] : _mapModule )
		{
			if ( ctx._bMtimeDebouncing == false )
				continue;

			const int64 elapsedMs =
				std::chrono::duration_cast<std::chrono::milliseconds>( now - ctx._debounceSince ).count();
			if ( elapsedMs < kMtimeDebounceMs )
				continue;

			const uint64 sourceMtime = FileUtil::getFileTimestamp( ctx._originalModulePath );
			if ( sourceMtime != ctx._debounceMtime )
			{
				ctx._debounceMtime = sourceMtime;
				ctx._debounceSince = now;
				continue;
			}

			ctx._bMtimeDebouncing = false;
			const bool bForce	  = ctx._bForceReload.exchange( false );
			if ( bForce || sourceMtime > ctx._loadedSourceMtime )
			{
				SW_LOG_TRACE( "FileWatcher settled — queuing reload for %#", ctx._moduleName );
				ctx._bPendingReload = true;
			}
		}

		if ( _bReloadGraphBroken )
			return;

		vector<string> listPendingRoots;
		BLOCK( "Collect Pending Reloads" )
		{
			for ( auto& [name, ctx] : _mapModule )
			{
				if ( ctx._bPendingReload )
				{
					ctx._bPendingReload = false;
					listPendingRoots.push_back( ctx._moduleName );
				}
			}
		}

		BLOCK( "Reload Cascade" )
		{
			vector<string> listSubgraph;
			for ( const string& root : listPendingRoots )
			{
				collectDependentClosure( root, listSubgraph );
			}
			if ( listSubgraph.empty() == false )
				reloadCascade( listSubgraph );
		}
	}

	void LiveReloadManager::setOnBeforeReload( string_view moduleName, OnBeforeReloadDelegate delegate )
	{
		_mapModule[string( moduleName )]._onBeforeReload = delegate;
	}

	void LiveReloadManager::setOnAfterReload( string_view moduleName, OnAfterReloadDelegate delegate )
	{
		_mapModule[string( moduleName )]._onAfterReload = delegate;
	}

	void LiveReloadManager::setOnBeforeCommitBatch( OnBeforeCommitBatchDelegate delegate )
	{
		_onBeforeCommitBatch = delegate;
	}

	void LiveReloadManager::setDrainWorkers( DrainWorkersDelegate delegate )
	{
		_drainWorkers = delegate;
	}

	void LiveReloadManager::markGraphBroken( string_view reason )
	{
		_bReloadGraphBroken = true;
		(void)reason;
		SW_LOG_ERROR( "Reload graph broken (%#) — restart the process; further live reloads are disabled",
					  reason );
	}

	void* LiveReloadManager::getModuleHandle( string_view moduleName ) const
	{
		auto it = _mapModule.find( string( moduleName ) );
		return it != _mapModule.end() ? it->second._pLibraryModule : nullptr;
	}

	void LiveReloadManager::addEventSubscription( string_view moduleName, const EventDispatcher::EventSubscription& token )
	{
		auto iter = _mapModule.find( string( moduleName ) );
		if ( iter != _mapModule.end() )
			iter->second._listEventSubscription.push_back( token );
	}

	bool LiveReloadManager::loadShadowCopyModule( ModuleContext& ctx )
	{
		PreparedShadow prepared;
		if ( prepareShadowCopy( ctx, prepared ) == false )
			return false;
		if ( commitShadowCopy( ctx, prepared ) )
			return true;
		abortShadowCopy( prepared );
		return false;
	}

	bool LiveReloadManager::prepareShadowCopy( ModuleContext& ctx, PreparedShadow& out )
	{
		out = {};
		BLOCK( "Check Original Module" )
		{
			if ( FileUtil::fileExists( ctx._originalModulePath ) == false )
			{
				SW_LOG_ERROR( "Original module not found: %#", ctx._originalModulePath.c_str() );
				return false;
			}
		}

		out._sourceMtime = FileUtil::getFileTimestamp( ctx._originalModulePath );

		++s_reloadCount;
		const uint64 timestamp = static_cast<uint64>( std::chrono::steady_clock::now().time_since_epoch().count() );
		const string tempName  = ctx._moduleName + "_temp_" + to_string( s_reloadCount ) + "_" + to_string( timestamp );
		const string execDir   = FileUtil::getDirectoryPart( ctx._originalModulePath );
		out._tempPath		   = FileUtil::joinPath( execDir, FileUtil::formatSharedLibraryName( tempName ) );

		BLOCK( "Create Shadow Copy" )
		{
			if ( copyFileWithRetry( ctx._originalModulePath, out._tempPath ) == false )
			{
				SW_LOG_ERROR( "Failed to create shadow copy (locked): %#", out._tempPath.c_str() );
				out._tempPath.clear();
				return false;
			}

			copyDebugSymbolsIfPresent( ctx._originalModulePath, out._tempPath );
		}

		BLOCK( "Load Dynamic Library" )
		{
			out._pHandle = FileUtil::loadDynamicLibrary( out._tempPath );
			if ( out._pHandle == nullptr )
			{
				SW_LOG_ERROR( "Failed to load dynamic library (keeping old): %#", out._tempPath.c_str() );
				tryDeleteShadowArtifacts( out._tempPath );
				out._tempPath.clear();
				return false;
			}

			out._pTypeHead	  = TypeRegistrar::getHead();
			out._pEnumHead	  = EnumRegistrar::getHead();
			out._pFactoryHead = sw::ComponentFactoryRegistrar::getHead();

			TypeRegistrar::getHead()				 = nullptr;
			EnumRegistrar::getHead()				 = nullptr;
			sw::ComponentFactoryRegistrar::getHead() = nullptr;
		}

		return true;
	}

	bool LiveReloadManager::commitShadowCopy( ModuleContext& ctx, PreparedShadow& prepared )
	{
		if ( prepared._pHandle == nullptr )
			return false;

		const string previousTempModule = ctx._tempModulePath;
		void*		 previousHandle		= ctx._pLibraryModule;

		BLOCK( "Swap Module Handles" )
		{
			// onBeforeReload 가 모듈 리소스를 만지기 전에 워커가 옛 이미지에서 빠져나와 있어야 한다.
			if ( previousHandle != nullptr && drainTasksBeforeUnload() == false )
				return false;

			if ( ctx._onBeforeReload.isBound() && previousHandle != nullptr )
				ctx._onBeforeReload();

			if ( previousHandle != nullptr )
			{
				for ( const EventDispatcher::EventSubscription& token : ctx._listEventSubscription )
				{
					engine::getEventDispatcher().unsubscribe( token );
				}
				ctx._listEventSubscription.clear();

				// onBefore 이후 남은 작업. 이미 모듈을 내렸으면 스왑을 계속하고, 타임아웃은 poison만 한다.
				drainTasksBeforeUnload();
				engine::unregisterModuleTypes( ctx._moduleName );
			}

			ctx._pLibraryModule	   = prepared._pHandle;
			ctx._tempModulePath	   = prepared._tempPath;
			ctx._loadedSourceMtime = prepared._sourceMtime;
			prepared._pHandle	   = nullptr;
			prepared._tempPath.clear();

			engine::registerModuleTypes(
				ctx._moduleName,
				prepared._pTypeHead,
				prepared._pEnumHead,
				prepared._pFactoryHead );
			prepared._pTypeHead	   = nullptr;
			prepared._pEnumHead	   = nullptr;
			prepared._pFactoryHead = nullptr;

			if ( ctx._onAfterReload.isBound() )
				ctx._onAfterReload( ctx._pLibraryModule );

			if ( previousHandle != nullptr && _bReloadGraphBroken == false )
			{
				FileUtil::unloadDynamicLibrary( previousHandle );
				tryDeleteShadowArtifacts( previousTempModule );
			}
		}

		if ( _bReloadGraphBroken )
		{
			SW_LOG_ERROR( "Module %# committed but onAfter poisoned the graph",
						  ctx._moduleName );
			return false;
		}

		SW_LOG_INFO( "Module loaded (shadow: %#)", ctx._tempModulePath.c_str() );
		return true;
	}

	void LiveReloadManager::abortShadowCopy( PreparedShadow& prepared )
	{
		prepared._pTypeHead	   = nullptr;
		prepared._pEnumHead	   = nullptr;
		prepared._pFactoryHead = nullptr;

		if ( prepared._pHandle != nullptr )
		{
			FileUtil::unloadDynamicLibrary( prepared._pHandle );
			prepared._pHandle = nullptr;
		}
		tryDeleteShadowArtifacts( prepared._tempPath );
		prepared._tempPath.clear();
		prepared._sourceMtime = 0;
	}

	void LiveReloadManager::unloadModule( ModuleContext& ctx )
	{
		if ( ctx._onBeforeReload.isBound() && ctx._pLibraryModule != nullptr )
			ctx._onBeforeReload();

		if ( ctx._pLibraryModule != nullptr )
		{
			SW_LOG_INFO( "Unloading module %# (handle=%#)", ctx._moduleName.c_str(), ctx._pLibraryModule );
			for ( const EventDispatcher::EventSubscription& token : ctx._listEventSubscription )
			{
				engine::getEventDispatcher().unsubscribe( token );
			}
			ctx._listEventSubscription.clear();

			drainTasksBeforeUnload();

			engine::unregisterModuleTypes( ctx._moduleName );

			FileUtil::unloadDynamicLibrary( ctx._pLibraryModule );
			ctx._pLibraryModule = nullptr;
		}

		tryDeleteShadowArtifacts( ctx._tempModulePath );
		ctx._tempModulePath.clear();
	}

	bool LiveReloadManager::drainTasksBeforeUnload()
	{
		constexpr uint32 kDrainTimeoutMs = 5000;

		// onBeforeReload must stop module-originated work. Drain in-flight tasks so
		// callbacks cannot enter the old image. Do not clear() — that drops unrelated
		// GpuScene / scene-load work and skips onTaskFinished bookkeeping.
		if ( engine::areEngineServicesBound() )
			engine::getSceneManager().cancelPendingAsyncLoads();

		// 렌더 스레드는 TaskManager 밖에서 돈다. present 훅이 모듈 코드를 실행 중일 수 있으므로 먼저 배수한다.
		if ( _drainWorkers.isBound() )
		{
			_drainWorkers();
			if ( _bReloadGraphBroken )
				return false;
			return true;
		}

		if ( engine::areEngineServicesBound() && engine::getTaskManager().waitAll( kDrainTimeoutMs ) == false )
		{
			markGraphBroken( "task drain timeout before unload" );
			return false;
		}
		return _bReloadGraphBroken == false;
	}

	void LiveReloadManager::collectDependentClosure( string_view root, vector<string>& outUnique ) const
	{
		unordered_set<string> uniqueVisited;
		for ( const string& existing : outUnique )
		{
			uniqueVisited.insert( existing );
		}

		vector<string> listStack;
		listStack.push_back( string( root ) );
		while ( listStack.empty() == false )
		{
			const string cur = listStack.back();
			listStack.pop_back();
			if ( uniqueVisited.insert( cur ).second == false )
				continue;
			if ( _mapModule.find( cur ) == _mapModule.end() )
				continue;
			outUnique.push_back( cur );
			for ( const auto& [modName, ctx] : _mapModule )
			{
				for ( const string& dep : ctx._listDependsOn )
				{
					if ( dep == cur )
						listStack.push_back( modName );
				}
			}
		}
	}

	bool LiveReloadManager::topoSortSubgraph( const vector<string>& listNames, vector<string>& outOrdered ) const
	{
		outOrdered.clear();
		unordered_set<string> uniqueSubgraph;
		uniqueSubgraph.reserve( listNames.size() );
		for ( const string& name : listNames )
		{
			if ( _mapModule.find( name ) != _mapModule.end() )
				uniqueSubgraph.insert( name );
		}
		if ( uniqueSubgraph.empty() )
			return true;

		unordered_map<string, uint32> indegree;
		for ( const string& name : uniqueSubgraph )
		{
			indegree[name] = 0;
		}

		for ( const string& name : uniqueSubgraph )
		{
			const auto found = _mapModule.find( name );
			for ( const string& dep : found->second._listDependsOn )
			{
				if ( uniqueSubgraph.find( dep ) != uniqueSubgraph.end() )
					indegree[name] += 1;
			}
		}

		vector<string> listReady;
		for ( const string& name : uniqueSubgraph )
		{
			if ( indegree[name] == 0 )
				listReady.push_back( name );
		}
		std::sort( listReady.begin(), listReady.end() );

		vector<string> listOrder;
		listOrder.reserve( uniqueSubgraph.size() );
		size_t cursor{ 0 };
		while ( cursor < listReady.size() )
		{
			const size_t waveBegin = cursor;
			const size_t waveEnd   = listReady.size();
			for ( size_t waveIndex = waveBegin; waveIndex < waveEnd; ++waveIndex )
			{
				const string n = listReady[waveIndex];
				listOrder.push_back( n );
				for ( const string& name : uniqueSubgraph )
				{
					const auto found = _mapModule.find( name );
					for ( const string& dep : found->second._listDependsOn )
					{
						if ( dep != n )
							continue;

						uint32& deg = indegree[name];
						if ( deg == 0 )
							continue;
						deg -= 1;
						if ( deg == 0 )
							listReady.push_back( name );
					}
				}
			}
			cursor = waveEnd;
			if ( listReady.size() > waveEnd )
				std::sort( listReady.begin() + static_cast<std::ptrdiff_t>( waveEnd ), listReady.end() );
		}

		if ( listOrder.size() != uniqueSubgraph.size() )
		{
			string cycleModules;
			for ( const string& name : uniqueSubgraph )
			{
				if ( std::find( listOrder.begin(), listOrder.end(), name ) == listOrder.end() )
				{
					if ( cycleModules.empty() == false )
						cycleModules += ", ";
					cycleModules += name;
				}
			}
			SW_LOG_ERROR( "Dependency cycle in module graph (sorted %# / %#). Cyclic modules: %s",
						  static_cast<uint32>( listOrder.size() ), static_cast<uint32>( uniqueSubgraph.size() ), cycleModules.c_str() );
			return false;
		}

		outOrdered = std::move( listOrder );
		return true;
	}

	void LiveReloadManager::reloadCascade( const vector<string>& subgraphNames )
	{
		vector<string> listOrder;
		if ( topoSortSubgraph( subgraphNames, listOrder ) == false )
		{
			markGraphBroken( "dependency cycle" );
			return;
		}
		if ( listOrder.empty() )
			return;

		SW_LOG_TRACE( "Cascade reload count=%#", static_cast<uint32>( listOrder.size() ) );

		struct PreparedEntry
		{
			ModuleContext* _pCtx{ nullptr };
			PreparedShadow _shadow;
		};
		vector<PreparedEntry> listPrepared;
		listPrepared.reserve( listOrder.size() );

		bool bPrepareOk{ true };
		for ( const string& name : listOrder )
		{
			auto found = _mapModule.find( name );
			if ( found == _mapModule.end() )
				continue;
			PreparedEntry entry;
			entry._pCtx = &found->second;
			if ( prepareShadowCopy( *entry._pCtx, entry._shadow ) == false )
			{
				SW_LOG_ERROR( "Cascade prepare failed for %# — aborting, previous modules kept",
							  name );
				bPrepareOk = false;
				break;
			}
			listPrepared.push_back( std::move( entry ) );
		}

		if ( bPrepareOk == false )
		{
			for ( PreparedEntry& entry : listPrepared )
			{
				abortShadowCopy( entry._shadow );
			}
			return;
		}

		if ( _onBeforeCommitBatch.isBound() )
			_onBeforeCommitBatch( listOrder );

		_bReloadingBatch = true;

		size_t committed{ 0 };
		for ( size_t moduleIndex = 0; moduleIndex < listPrepared.size(); ++moduleIndex )
		{
			PreparedEntry& entry = listPrepared[moduleIndex];
			if ( _bReloadGraphBroken )
			{
				SW_LOG_ERROR( "Cascade abort before commit of %# — graph already broken",
							  entry._pCtx->_moduleName );
				abortShadowCopy( entry._shadow );
				for ( size_t otherModuleIndex = moduleIndex + 1; otherModuleIndex < listPrepared.size(); ++otherModuleIndex )
				{
					abortShadowCopy( listPrepared[otherModuleIndex]._shadow );
				}
				if ( committed > 0 )
					markGraphBroken( "partial cascade commit" );
				_bReloadingBatch = false;
				return;
			}

			if ( commitShadowCopy( *entry._pCtx, entry._shadow ) )
			{
				++committed;
				continue;
			}

			SW_LOG_ERROR( "Cascade commit failed for %# — aborting remaining commits",
						  entry._pCtx->_moduleName );
			abortShadowCopy( entry._shadow );
			for ( size_t otherModuleIndex = moduleIndex + 1; otherModuleIndex < listPrepared.size(); ++otherModuleIndex )
			{
				abortShadowCopy( listPrepared[otherModuleIndex]._shadow );
			}

			if ( committed > 0 || _bReloadGraphBroken == false )
				markGraphBroken( "partial cascade commit" );
			_bReloadingBatch = false;
			return;
		}

		_bReloadingBatch = false;
	}

	LiveReloadManager::ModuleContext::ModuleContext() noexcept
		: _onBeforeReload{}
		, _onAfterReload{}
		, _moduleName{}
		, _originalModulePath{}
		, _tempModulePath{}
		, _listDependsOn{}
		, _listEventSubscription{}
		, _pLibraryModule{ nullptr }
		, _loadedSourceMtime{ 0 }
		, _debounceMtime{ 0 }
		, _debounceSince{}
		, _bPendingReload{ false }
		, _bMtimeDebouncing{ false }
		, _bForceReload{ false }
	{
	}

	LiveReloadManager::ModuleContext::ModuleContext( ModuleContext&& other ) noexcept
		: _onBeforeReload{ std::move( other._onBeforeReload ) }
		, _onAfterReload{ std::move( other._onAfterReload ) }
		, _moduleName{ std::move( other._moduleName ) }
		, _originalModulePath{ std::move( other._originalModulePath ) }
		, _tempModulePath{ std::move( other._tempModulePath ) }
		, _listDependsOn{ std::move( other._listDependsOn ) }
		, _listEventSubscription{ std::move( other._listEventSubscription ) }
		, _pLibraryModule{ other._pLibraryModule }
		, _loadedSourceMtime{ other._loadedSourceMtime }
		, _debounceMtime{ other._debounceMtime }
		, _debounceSince{ other._debounceSince }
		, _bPendingReload{ other._bPendingReload.load() }
		, _bMtimeDebouncing{ other._bMtimeDebouncing.load() }
		, _bForceReload{ other._bForceReload.load() }
	{
		other._pLibraryModule = nullptr;
		other._bPendingReload.store( false );
		other._bMtimeDebouncing.store( false );
		other._bForceReload.store( false );
	}

	LiveReloadManager::ModuleContext& LiveReloadManager::ModuleContext::operator=( ModuleContext&& other ) noexcept
	{
		if ( this != &other )
		{
			_onBeforeReload		   = std::move( other._onBeforeReload );
			_onAfterReload		   = std::move( other._onAfterReload );
			_moduleName			   = std::move( other._moduleName );
			_originalModulePath	   = std::move( other._originalModulePath );
			_tempModulePath		   = std::move( other._tempModulePath );
			_listDependsOn		   = std::move( other._listDependsOn );
			_listEventSubscription = std::move( other._listEventSubscription );
			_pLibraryModule		   = other._pLibraryModule;
			_loadedSourceMtime	   = other._loadedSourceMtime;
			_debounceMtime		   = other._debounceMtime;
			_debounceSince		   = other._debounceSince;
			_bPendingReload.store( other._bPendingReload.load() );
			_bMtimeDebouncing.store( other._bMtimeDebouncing.load() );
			_bForceReload.store( other._bForceReload.load() );

			other._pLibraryModule = nullptr;
			other._bPendingReload.store( false );
			other._bMtimeDebouncing.store( false );
			other._bForceReload.store( false );
		}
		return *this;
	}

	void LiveReloadManager::setDelayLoadManager( LiveReloadManager* pManager )
	{
		s_delayLoadManager = pManager;
	}

	LiveReloadManager* LiveReloadManager::getDelayLoadManager()
	{
		return s_delayLoadManager;
	}
} // namespace sw
