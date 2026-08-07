/**
 * @file LiveReloadManager.cpp
 * @brief 모듈 DLL 섀도 복사 로드 및 핫 리로드
 */
#include "LiveReloadManager.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"

#include <chrono>
#include <filesystem>
#include <thread>

namespace sw
{
	namespace
	{
		uint32 s_reloadCount = 0;

		std::string joinDirFile( const std::string& dir, const std::string& file )
		{
			if ( dir.empty() )
				return FileUtil::normalizePath( file );
			return FileUtil::normalizePath( dir + "/" + file );
		}

		void tryDeleteFile( const std::string& path )
		{
			if ( path.empty() )
				return;
			std::error_code ec;
			std::filesystem::remove( path, ec );
		}

		void tryDeleteShadowArtifacts( const std::string& dllPath )
		{
			if ( dllPath.empty() )
				return;
			tryDeleteFile( dllPath );
			tryDeleteFile( FileUtil::getDebugSymbolPath( dllPath ) );
		}
	} // namespace

	LiveReloadManager::ModuleContext::ModuleContext() noexcept
		: _bPendingReload{ 0 }
		, _reserved{ 0 }
	{
	}

	LiveReloadManager::LiveReloadManager() = default;

	LiveReloadManager::~LiveReloadManager()
	{
		shutdown();
	}

	bool LiveReloadManager::initialize()
	{
		return true;
	}

	bool LiveReloadManager::registerModule( const std::string& moduleName )
	{
		ModuleContext& ctx = _modules[moduleName];
		ctx._moduleName	   = moduleName;

		const std::string execDir = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
		ctx._originalDllPath	  = joinDirFile( execDir, FileUtil::formatSharedLibraryName( moduleName ) );
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
		if ( FileUtil::isFileExist( ctx._originalDllPath ) == false )
		{
			SW_LOG_ERROR( "[LiveReloadManager] Original module DLL not found: %#", ctx._originalDllPath.c_str() );
			return false;
		}

		const uint64 sourceMtime = FileUtil::getFileTimestamp( ctx._originalDllPath );

		std::string newTempDllPath;
		std::string newTempPdbPath;
		void*		newHandle = nullptr;

		BLOCK( "DLL 섀도 카피" )
		{
			++s_reloadCount;
			const std::string tempName = ctx._moduleName + "_temp_" + std::to_string( s_reloadCount );
			const std::string execDir  = FileUtil::getDirectoryPart( ctx._originalDllPath );
			newTempDllPath			   = joinDirFile( execDir, FileUtil::formatSharedLibraryName( tempName ) );

			bool bCopySuccess = false;
			for ( int i = 0; i < 10; ++i )
			{
				if ( FileUtil::copyFile( ctx._originalDllPath, newTempDllPath ) )
				{
					bCopySuccess = true;
					break;
				}
				std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
			}

			if ( bCopySuccess == false )
			{
				SW_LOG_ERROR( "[LiveReloadManager] Failed to create shadow copy (locked): %#", newTempDllPath.c_str() );
				return false;
			}
		}

		BLOCK( "디버그 심볼 복사" )
		{
			const std::string originalDebugPath = FileUtil::getDebugSymbolPath( ctx._originalDllPath );
			newTempPdbPath						= FileUtil::getDebugSymbolPath( newTempDllPath );
			if ( std::filesystem::exists( originalDebugPath ) )
			{
				std::error_code ec;
				std::filesystem::copy( originalDebugPath, newTempPdbPath,
									   std::filesystem::copy_options::overwrite_existing, ec );
				if ( ec )
				{
					SW_LOG_WARNING( "[LiveReloadManager] Failed to copy debug symbols: %#", ec.message().c_str() );
					// PDB failure is non-fatal for loading the DLL.
					newTempPdbPath.clear();
				}
			}
			else
			{
				newTempPdbPath.clear();
			}
		}

		BLOCK( "동적 라이브러리 로드" )
		{
			newHandle = FileUtil::loadDynamicLibrary( newTempDllPath );
			if ( newHandle == nullptr )
			{
				SW_LOG_ERROR( "[LiveReloadManager] Failed to load dynamic library: %#", newTempDllPath.c_str() );
				tryDeleteShadowArtifacts( newTempDllPath );
				return false;
			}
		}

		// Two-handle swap: only free the old module after the new one is loaded successfully.
		BLOCK( "Swap handles / cleanup previous shadow" )
		{
			const std::string previousTempDll = ctx._tempDllPath;
			void*			  previousHandle  = ctx._hLibraryModule;

			if ( ctx._onBeforeReload.isBound() && previousHandle != nullptr )
				ctx._onBeforeReload();

			if ( previousHandle != nullptr )
			{
				FileUtil::freeDynamicLibrary( previousHandle );
				previousHandle = nullptr;
			}

			ctx._hLibraryModule		= newHandle;
			ctx._tempDllPath		= newTempDllPath;
			ctx._loadedSourceMtime	= sourceMtime;

			tryDeleteShadowArtifacts( previousTempDll );

			if ( ctx._onAfterReload.isBound() )
				ctx._onAfterReload( ctx._hLibraryModule );
		}

		SW_LOG_INFO( "[LiveReloadManager] Module loaded (shadow: %#)", ctx._tempDllPath.c_str() );
		return true;
	}

	void LiveReloadManager::update()
	{
		for ( auto& pair : _modules )
		{
			ModuleContext& ctx = pair.second;

			// Auto-reload when the source DLL is newer than the shadow we loaded.
			if ( ctx._originalDllPath.empty() == false && FileUtil::isFileExist( ctx._originalDllPath ) )
			{
				const uint64 sourceMtime = FileUtil::getFileTimestamp( ctx._originalDllPath );
				if ( sourceMtime != 0 && sourceMtime > ctx._loadedSourceMtime )
				{
					SW_LOG_INFO( "[LiveReloadManager] Source DLL newer — queuing reload for %#", ctx._moduleName.c_str() );
					ctx._bPendingReload = true;
				}
			}

			if ( ctx._bPendingReload == false )
				continue;

			ctx._bPendingReload = false;

			BLOCK( "Shadow Copy Reload" )
			{
				const uint64 attemptedMtime = FileUtil::getFileTimestamp( ctx._originalDllPath );
				if ( loadShadowCopyModule( ctx ) == false )
				{
					SW_LOG_ERROR( "[LiveReloadManager] Reload failed for %# — keeping previous module handle if any.",
								  ctx._moduleName.c_str() );
					// Prevent per-frame retry spam for the same source timestamp; manual trigger still works.
					if ( attemptedMtime != 0 )
						ctx._loadedSourceMtime = attemptedMtime;
				}
			}
		}
	}

	void LiveReloadManager::shutdown()
	{
		// App이 이미 onBefore*Reload로 인스턴스를 정리한다. 여기서는 DLL unload + temp cleanup.
		for ( auto& pair : _modules )
		{
			ModuleContext& ctx = pair.second;
			if ( ctx._hLibraryModule != nullptr )
			{
				FileUtil::freeDynamicLibrary( ctx._hLibraryModule );
				ctx._hLibraryModule = nullptr;
			}
			tryDeleteShadowArtifacts( ctx._tempDllPath );
			ctx._tempDllPath.clear();
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
