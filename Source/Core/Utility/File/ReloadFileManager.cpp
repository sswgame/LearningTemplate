/**
 * @file ReloadFileManager.cpp
 */
#include "pch.h"
#include "ReloadFileManager.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Utility/String/StringUtil.h"

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

		const std::string& rootPath = ResourceUtil::getRootFolderPath();
		if ( rootPath.empty() == false )
		{
			if ( _fileWatcher->startWatching( rootPath, true ) == false )
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
		_watches.clear();
		if ( _fileWatcher )
		{
			_fileWatcher->stopWatching();
			_fileWatcher.reset();
		}
	}

	std::string ReloadFileManager::extractExtension( const std::string& filename )
	{
		const auto pos = filename.find_last_of( '.' );
		if ( pos == std::string::npos )
			return {};
		return filename.substr( pos );
	}

	bool ReloadFileManager::matchesWatch( const WatchEntry& entry, const FileChangeEvent& ev ) const
	{
		const std::string fullPath = FileUtil::normalizePath( ev._directory + "/" + ev._filename );
		const std::string prefix   = FileUtil::normalizePath( entry._pathPrefix );

		if ( fullPath.size() < prefix.size() )
			return false;

		const bool bPrefixMatch = ( fullPath.compare( 0, prefix.size(), prefix ) == 0 );
		if ( bPrefixMatch == false )
			return false;

		if ( fullPath.size() > prefix.size() )
		{
			const utf8 next = fullPath[prefix.size()];
			if ( next != '/' && next != '\\' )
				return false;
		}

		if ( entry._extensions.empty() )
			return true;

		std::string ext = extractExtension( ev._filename );
		ext				= StringUtil::toLower( ext );

		for ( const std::string& rawAllowed : entry._extensions )
		{
			std::string allowed = StringUtil::toLower( rawAllowed );
			if ( allowed.empty() == false && allowed[0] != '.' )
				allowed.insert( allowed.begin(), '.' );
			if ( ext == allowed )
				return true;
		}
		return false;
	}

	FileWatchHandle ReloadFileManager::registerWatch( const std::string& pathPrefix, const std::vector<std::string>& extensions, const FileWatchMatchDelegate& onMatch )
	{
		WatchEntry entry{};
		entry._handle	   = FileWatchHandle{ _nextWatchId++ };
		entry._pathPrefix  = FileUtil::normalizePath( pathPrefix );
		entry._extensions  = extensions;
		entry._onMatch	   = onMatch;
		_watches.push_back( entry );

		SW_LOG_INFO( "[ReloadFileManager] Registered watch %# (ext count %#)", entry._pathPrefix, static_cast<uint32>( extensions.size() ) );
		return entry._handle;
	}

	void ReloadFileManager::unregisterWatch( FileWatchHandle handle )
	{
		if ( handle.isValid() == false )
			return;

		_watches.erase( std::remove_if( _watches.begin(), _watches.end(),
							 [&]( const WatchEntry& e )
		{
			return e._handle == handle;
		} ),
						_watches.end() );
	}

	void ReloadFileManager::update()
	{
		if ( !_fileWatcher )
			return;

		std::vector<FileChangeEvent> events;
		if ( _fileWatcher->pollEvents( events ) == 0 )
			return;

		for ( const FileChangeEvent& ev : events )
		{
			bool bAnyMatch = false;
			for ( const WatchEntry& entry : _watches )
			{
				if ( matchesWatch( entry, ev ) == false )
					continue;

				bAnyMatch = true;
				if ( entry._onMatch.isBound() )
					entry._onMatch( ev );
			}

			if ( bAnyMatch )
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
