#include "pch.h"
#include "ReloadFileManager.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Resource/ResourceUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
#include "Core/Utility/File/Windows/WindowsFileWatcher.h"
#endif

namespace sw
{
	ReloadFileManager::ReloadFileManager() = default;

	ReloadFileManager::~ReloadFileManager()
	{
		shutdown();
	}

	bool ReloadFileManager::initialize()
	{
#if defined( SW_PLATFORM_WINDOWS )
		_fileWatcher = std::make_unique<WindowsFileWatcher>();

		std::string rootPath = ResourceUtil::getRootFolderPath();
		if ( !rootPath.empty() )
		{
			if ( !_fileWatcher->startWatching( rootPath, true ) )
			{
				SW_LOG_ERROR( "ReloadFileManager failed to start watching: %#", rootPath );
				return false;
			}
		}
		else
		{
			SW_LOG_WARNING( "ReloadFileManager: Root resource path is empty." );
		}
#endif
		return true;
	}

	void ReloadFileManager::shutdown()
	{
		if ( _fileWatcher )
		{
			_fileWatcher->stopWatching();
			_fileWatcher.reset();
		}
	}

	void ReloadFileManager::update()
	{
		if ( !_fileWatcher )
			return;

		std::vector<FileChangeEvent> events;
		if ( _fileWatcher->pollEvents( events ) > 0 )
		{
			for ( const auto& ev : events )
			{
				const utf8* actionStr = "Unknown";
				switch ( ev._action )
				{
				case FileWatcherAction::Added: actionStr = "Added"; break;
				case FileWatcherAction::Removed: actionStr = "Removed"; break;
				case FileWatcherAction::Modified: actionStr = "Modified"; break;
				case FileWatcherAction::RenamedOldName: actionStr = "RenamedOld"; break;
				case FileWatcherAction::RenamedNewName: actionStr = "RenamedNew"; break;
				}
				SW_LOG_INFO( "[ReloadFileManager] %s : %s/%s", actionStr, ev._directory.c_str(), ev._filename.c_str() );

				_onFileChanged.broadcast( ev );
			}
		}
	}
}
