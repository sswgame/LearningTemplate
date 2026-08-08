/**
 * @file LiveReloadManager.cpp
 * @brief 모듈 공유 라이브러리 섀도 복사 로드 및 핫 리로드
 *
 * All platforms: `*_temp_N` unique shadow + two-handle swap (load new, then free old).
 * Windows also copies PDB debug symbols alongside the shadow module.
 */
#include "LiveReloadManager.h"

#include "Core/Common/CoreServices.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Task/TaskManager.h"

namespace sw
{
	namespace
	{
		uint32 s_reloadCount = 0;

		std::string joinDirFile( const std::string& dir, const std::string& file )
		{
			// Case-preserving: Linux/macOS paths are case-sensitive (libEditorModule.so).
			if ( dir.empty() )
				return FileUtil::normalizeSeparators( file );
			return FileUtil::normalizeSeparators( dir + "/" + file );
		}

		void tryDeleteFile( const std::string& path )
		{
			if ( path.empty() )
				return;
			std::error_code ec;
			std::filesystem::remove( path, ec );
		}

		void tryDeleteShadowArtifacts( const std::string& modulePath )
		{
			if ( modulePath.empty() )
				return;
			tryDeleteFile( modulePath );
			tryDeleteFile( FileUtil::getDebugSymbolPath( modulePath ) );
		}

		bool copyFileWithRetry( const std::string& source, const std::string& destination )
		{
			for ( int i = 0; i < 10; ++i )
			{
				if ( FileUtil::copyFile( source, destination ) )
					return true;
				std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
			}
			return false;
		}

		void copyDebugSymbolsIfPresent( const std::string& originalModulePath, const std::string& shadowModulePath )
		{
			const std::string originalDebugPath = FileUtil::getDebugSymbolPath( originalModulePath );
			const std::string shadowDebugPath	= FileUtil::getDebugSymbolPath( shadowModulePath );
			if ( std::filesystem::exists( originalDebugPath ) == false )
				return;

			std::error_code ec;
			std::filesystem::copy( originalDebugPath, shadowDebugPath, std::filesystem::copy_options::overwrite_existing, ec );
			if ( ec )
				SW_LOG_WARNING( "[LiveReloadManager] Failed to copy debug symbols: %#", ec.message().c_str() );
		}
	} // namespace

	LiveReloadManager::ModuleContext::ModuleContext() noexcept
		: _bPendingReload{ 0 }
		, _bMtimeDebouncing{ 0 }
		, _reserved{ 0 }
	{
	}

	LiveReloadManager::LiveReloadManager() = default;

	LiveReloadManager::~LiveReloadManager()
	{
		shutdown();
	}

	bool LiveReloadManager::registerModule( const std::string& moduleName )
	{
		ModuleContext& ctx = _modules[moduleName];
		ctx._moduleName	   = moduleName;

		const std::string execDir = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
		ctx._originalModulePath	  = joinDirFile( execDir, FileUtil::formatSharedLibraryName( moduleName ) );
		return loadShadowCopyModule( ctx );
	}

	void LiveReloadManager::triggerReload( const std::string& moduleName )
	{
		auto iter = _modules.find( moduleName );
		if ( iter == _modules.end() )
			return;

		SW_LOG_INFO( "[LiveReloadManager] Manual Live Reload triggered for %s...", moduleName.c_str() );
		iter->second._bPendingReload = true;
	}

	bool LiveReloadManager::loadShadowCopyModule( ModuleContext& ctx )
	{
		if ( FileUtil::isFileExist( ctx._originalModulePath ) == false )
		{
			SW_LOG_ERROR( "[LiveReloadManager] Original module not found: %#", ctx._originalModulePath.c_str() );
			return false;
		}

		const uint64 sourceMtime = FileUtil::getFileTimestamp( ctx._originalModulePath );

		std::string newTempModulePath;
		void*		newHandle = nullptr;

		BLOCK( "모듈 섀도 카피" )
		{
			++s_reloadCount;
			const std::string tempName = ctx._moduleName + "_temp_" + std::to_string( s_reloadCount );
			const std::string execDir  = FileUtil::getDirectoryPart( ctx._originalModulePath );
			newTempModulePath		   = joinDirFile( execDir, FileUtil::formatSharedLibraryName( tempName ) );

			if ( copyFileWithRetry( ctx._originalModulePath, newTempModulePath ) == false )
			{
				SW_LOG_ERROR( "[LiveReloadManager] Failed to create shadow copy (locked): %#", newTempModulePath.c_str() );
				return false;
			}
		}

		BLOCK( "디버그 심볼 복사" )
		{
			copyDebugSymbolsIfPresent( ctx._originalModulePath, newTempModulePath );
		}

		BLOCK( "동적 라이브러리 로드" )
		{
			newHandle = FileUtil::loadDynamicLibrary( newTempModulePath );
			if ( newHandle == nullptr )
			{
				SW_LOG_ERROR( "[LiveReloadManager] Failed to load dynamic library: %#", newTempModulePath.c_str() );
				tryDeleteShadowArtifacts( newTempModulePath );
				return false;
			}
		}

		// Two-handle swap: only free the old module after the new one is loaded successfully.
		BLOCK( "Swap handles / cleanup previous shadow" )
		{
			const std::string previousTempModule = ctx._tempModulePath;
			void*			  previousHandle	 = ctx._hLibraryModule;

			if ( ctx._onBeforeReload.isBound() && previousHandle != nullptr )
				ctx._onBeforeReload();

			if ( previousHandle != nullptr )
			{
				// Drop any TaskManager nodes that still capture callables from the old MODULE.
				core::getTaskManager().waitAll();
				core::getTaskManager().clear();

				FileUtil::freeDynamicLibrary( previousHandle );
				previousHandle = nullptr;
			}

			ctx._hLibraryModule	   = newHandle;
			ctx._tempModulePath	   = newTempModulePath;
			ctx._loadedSourceMtime = sourceMtime;

			tryDeleteShadowArtifacts( previousTempModule );

			if ( ctx._onAfterReload.isBound() )
				ctx._onAfterReload( ctx._hLibraryModule );
		}

		SW_LOG_INFO( "[LiveReloadManager] Module loaded (shadow: %#)", ctx._tempModulePath.c_str() );
		return true;
	}

	void LiveReloadManager::update()
	{
		const auto now = std::chrono::steady_clock::now();

		for ( auto& pair : _modules )
		{
			ModuleContext& ctx = pair.second;

			if ( ctx._originalModulePath.empty() == false && FileUtil::isFileExist( ctx._originalModulePath ) )
			{
				const uint64 sourceMtime = FileUtil::getFileTimestamp( ctx._originalModulePath );
				if ( sourceMtime != 0 && sourceMtime > ctx._loadedSourceMtime )
				{
					if ( ctx._bMtimeDebouncing == false || sourceMtime != ctx._debounceMtime )
					{
						ctx._bMtimeDebouncing = true;
						ctx._debounceMtime	  = sourceMtime;
						ctx._debounceSince	  = now;
					}
					else if ( std::chrono::duration_cast<std::chrono::milliseconds>( now - ctx._debounceSince ).count() >= kMtimeDebounceMs )
					{
						SW_LOG_INFO( "[LiveReloadManager] Source module newer (debounced) — queuing reload for %#",
									 ctx._moduleName.c_str() );
						ctx._bPendingReload	  = true;
						ctx._bMtimeDebouncing = false;
					}
				}
				else
				{
					ctx._bMtimeDebouncing = false;
				}
			}

			if ( ctx._bPendingReload == false )
				continue;

			ctx._bPendingReload	  = false;
			ctx._bMtimeDebouncing = false;

			BLOCK( "Shadow Copy Reload" )
			{
				const uint64 attemptedMtime = FileUtil::getFileTimestamp( ctx._originalModulePath );
				if ( loadShadowCopyModule( ctx ) == false )
				{
					SW_LOG_ERROR( "[LiveReloadManager] Reload failed for %# — keeping previous module handle if any.",
								  ctx._moduleName.c_str() );
					if ( attemptedMtime != 0 )
						ctx._loadedSourceMtime = attemptedMtime;
				}
			}
		}
	}

	void LiveReloadManager::shutdown()
	{
		for ( auto& pair : _modules )
		{
			ModuleContext& ctx = pair.second;
			if ( ctx._hLibraryModule != nullptr )
			{
				FileUtil::freeDynamicLibrary( ctx._hLibraryModule );
				ctx._hLibraryModule = nullptr;
			}
			tryDeleteShadowArtifacts( ctx._tempModulePath );
			ctx._tempModulePath.clear();
		}
		_modules.clear();
	}

	void* LiveReloadManager::getModuleHandle( const std::string& moduleName ) const
	{
		auto it = _modules.find( moduleName );
		return it != _modules.end() ? it->second._hLibraryModule : nullptr;
	}

	void LiveReloadManager::setOnBeforeReload( const std::string& moduleName, OnBeforeReloadDelegate delegate )
	{
		_modules[moduleName]._onBeforeReload = delegate;
	}

	void LiveReloadManager::setOnAfterReload( const std::string& moduleName, OnAfterReloadDelegate delegate )
	{
		_modules[moduleName]._onAfterReload = delegate;
	}
} // namespace sw
