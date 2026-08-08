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
#elif defined( SW_PLATFORM_LINUX )
	#include "Core/Utility/File/Linux/LinuxFileWatcher.h"
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
#elif defined( SW_PLATFORM_LINUX )
		_fileWatcher = std::make_unique<LinuxFileWatcher>();
#endif

#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX )
		const std::string& rootPath = ResourceUtil::getRootFolderPath();
		if ( rootPath.empty() == false )
		{
			if ( _fileWatcher->startWatching( rootPath, true ) == false )
			{
				SW_LOG_ERROR( "ReloadFileManager failed to start watching: %#", rootPath );
				_fileWatcher.reset();
				_bUseMtimePoll = true;
				SW_LOG_WARNING( "ReloadFileManager: Falling back to mtime poll." );
			}
		}
		else
		{
			SW_LOG_WARNING( "ReloadFileManager: Root resource path is empty." );
			_bUseMtimePoll = true;
		}
#else
		// macOS: FSEvents watcher not implemented yet.
		_bUseMtimePoll = true;
		SW_LOG_INFO( "ReloadFileManager: Using mtime poll fallback (no native file watcher)." );
#endif
		return true;
	}

	void ReloadFileManager::shutdown()
	{
		_watches.clear();
		_pollMtimes.clear();
		_bUseMtimePoll = false;
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

	bool ReloadFileManager::extensionAllowed( const WatchEntry& entry, const std::string& filename ) const
	{
		if ( entry._extensions.empty() )
			return true;

		std::string ext = extractExtension( filename );
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

		return extensionAllowed( entry, ev._filename );
	}

	FileWatchHandle ReloadFileManager::registerWatch( const std::string& pathPrefix, const std::vector<std::string>& extensions, const FileWatchMatchDelegate& onMatch )
	{
		WatchEntry entry{};
		entry._handle	  = FileWatchHandle{ _nextWatchId++ };
		// Keep real FS path for mtime poll / native watchers; matching uses normalizePath.
		entry._pathPrefix = FileUtil::normalizeSeparators( pathPrefix );
		entry._extensions = extensions;
		entry._onMatch	  = onMatch;
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

	void ReloadFileManager::pollMtimeFallback( std::vector<FileChangeEvent>& outEvents )
	{
		namespace fs = std::filesystem;

		for ( const WatchEntry& entry : _watches )
		{
			std::error_code ec;
			const fs::path	prefixPath( entry._pathPrefix );
			if ( fs::exists( prefixPath, ec ) == false )
				continue;

			auto considerFile = [&]( const fs::path& filePath )
			{
				if ( fs::is_regular_file( filePath, ec ) == false )
					return;

				const std::string filename = filePath.filename().string();
				if ( extensionAllowed( entry, filename ) == false )
					return;

				const auto mtime = fs::last_write_time( filePath, ec );
				if ( ec )
					return;

				const std::string normalized = FileUtil::normalizePath( filePath.generic_string() );
				auto			  it		 = _pollMtimes.find( normalized );
				if ( it == _pollMtimes.end() )
				{
					_pollMtimes.emplace( normalized, mtime );
					return; // First sighting — baseline, no event
				}

				if ( it->second != mtime )
				{
					it->second = mtime;
					FileChangeEvent ev{};
					ev._action	  = FileWatcherAction::Modified;
					ev._directory = FileUtil::normalizePath( filePath.parent_path().generic_string() );
					ev._filename  = filename;
					outEvents.push_back( std::move( ev ) );
				}
			};

			if ( fs::is_regular_file( prefixPath, ec ) )
			{
				considerFile( prefixPath );
			}
			else if ( fs::is_directory( prefixPath, ec ) )
			{
				for ( fs::recursive_directory_iterator dirIt( prefixPath, fs::directory_options::skip_permission_denied, ec ), end;
					  dirIt != end;
					  dirIt.increment( ec ) )
				{
					if ( ec )
					{
						ec.clear();
						continue;
					}
					considerFile( dirIt->path() );
				}
			}
		}
	}

	void ReloadFileManager::dispatchEvents( const std::vector<FileChangeEvent>& events )
	{
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
					case FileWatcherAction::Added:
						actionStr = "Added";
						break;
					case FileWatcherAction::Removed:
						actionStr = "Removed";
						break;
					case FileWatcherAction::Modified:
						actionStr = "Modified";
						break;
					case FileWatcherAction::RenamedOldName:
						actionStr = "RenamedOld";
						break;
					case FileWatcherAction::RenamedNewName:
						actionStr = "RenamedNew";
						break;
				}
				SW_LOG_INFO( "[ReloadFileManager] %s : %s/%s", actionStr, ev._directory.c_str(), ev._filename.c_str() );
				_onFileChanged.broadcast( ev );
			}
		}
	}

	void ReloadFileManager::update()
	{
		std::vector<FileChangeEvent> events;

		if ( _fileWatcher )
		{
			_fileWatcher->pollEvents( events );
		}
		else if ( _bUseMtimePoll )
		{
			pollMtimeFallback( events );
		}
		else
		{
			return;
		}

		if ( events.empty() )
			return;

		dispatchEvents( events );
	}
} // namespace sw
