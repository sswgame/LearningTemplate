/**
 * @file LinuxFileWatcher.cpp
 * @brief inotify 기반 Linux 파일 감시
 */
#include "pch.h"
#include "LinuxFileWatcher.h"

#if defined( SW_PLATFORM_LINUX )
	#include "Core/Utility/Log/Logger.h"
	#include "Core/Utility/File/FileUtil.h"

	#include <sys/inotify.h>
	#include <sys/eventfd.h>
	#include <sys/select.h>
	#include <unistd.h>
	#include <cerrno>
	#include <cstring>

namespace sw
{
	namespace
	{
		constexpr uint32 kInotifyEventBufferSize = 16 * 1024;
		constexpr uint32 kInotifyMask			 = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO
									   | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF;

		std::string makeRelativePath( const std::string& root, const std::string& absolutePath )
		{
			const std::string rootNorm = FileUtil::normalizeSeparators( root );
			const std::string absNorm  = FileUtil::normalizeSeparators( absolutePath );
			if ( absNorm.compare( 0, rootNorm.size(), rootNorm ) != 0 )
				return FileUtil::getFileNamePart( absNorm );

			if ( absNorm.size() == rootNorm.size() )
				return {};

			size_t offset = rootNorm.size();
			if ( absNorm[offset] == '/' )
				++offset;
			return absNorm.substr( offset );
		}
	} // namespace

	LinuxFileWatcher::LinuxFileWatcher() = default;

	LinuxFileWatcher::~LinuxFileWatcher()
	{
		stopWatching();
	}

	bool LinuxFileWatcher::startWatching( const std::string_view directoryPath, bool bRecursive )
	{
		if ( _bIsWatching )
			return false;

		if ( FileUtil::isDirectoryExist( directoryPath ) == false )
		{
			SW_LOG_ERROR( "[LinuxFileWatcher] Directory does not exist: %#", std::string{ directoryPath }.c_str() );
			return false;
		}

		_inotifyFd = inotify_init1( IN_NONBLOCK | IN_CLOEXEC );
		if ( _inotifyFd < 0 )
		{
			SW_LOG_ERROR( "[LinuxFileWatcher] inotify_init1 failed: %#", strerror( errno ) );
			return false;
		}

		_wakeFd = eventfd( 0, EFD_NONBLOCK | EFD_CLOEXEC );
		if ( _wakeFd < 0 )
		{
			SW_LOG_ERROR( "[LinuxFileWatcher] eventfd failed: %#", strerror( errno ) );
			close( _inotifyFd );
			_inotifyFd = -1;
			return false;
		}

		_directoryPath = FileUtil::normalizeSeparators( std::string{ directoryPath } );
		_bRecursive	   = bRecursive;

		const bool bAdded = _bRecursive ? addWatchRecursive( _directoryPath ) : addWatchDirectory( _directoryPath );
		if ( bAdded == false )
		{
			stopWatching();
			return false;
		}

		_bIsWatching  = true;
		_workerThread = std::thread( &LinuxFileWatcher::workerThreadMain, this );

		SW_LOG_INFO( "[LinuxFileWatcher] Started watching directory: %#", _directoryPath.c_str() );
		return true;
	}

	uint32 LinuxFileWatcher::pollEvents( std::vector<FileChangeEvent>& outEvents )
	{
		std::lock_guard<std::mutex> lock{ _eventMutex };
		const uint32				count = static_cast<uint32>( _eventQueue.size() );
		if ( count > 0 )
		{
			outEvents.insert( outEvents.end(), _eventQueue.begin(), _eventQueue.end() );
			_eventQueue.clear();
		}
		return count;
	}

	void LinuxFileWatcher::stopWatching()
	{
		if ( _bIsWatching == false && _inotifyFd < 0 && _wakeFd < 0 )
			return;

		_bIsWatching = false;

		if ( _wakeFd >= 0 )
		{
			const uint64 one = 1;
			(void)!write( _wakeFd, &one, sizeof( one ) );
		}

		if ( _workerThread.joinable() )
			_workerThread.join();

		{
			std::lock_guard<std::mutex> lock{ _watchMutex };
			for ( const auto& pair : _watchDescriptorToPath )
				inotify_rm_watch( _inotifyFd, pair.first );
			_watchDescriptorToPath.clear();
		}

		if ( _inotifyFd >= 0 )
		{
			close( _inotifyFd );
			_inotifyFd = -1;
		}
		if ( _wakeFd >= 0 )
		{
			close( _wakeFd );
			_wakeFd = -1;
		}
	}

	bool LinuxFileWatcher::addWatchDirectory( const std::string& directoryPath )
	{
		const std::string normalized = FileUtil::normalizeSeparators( directoryPath );
		const int		  wd		 = inotify_add_watch( _inotifyFd, normalized.c_str(), kInotifyMask );
		if ( wd < 0 )
		{
			SW_LOG_ERROR( "[LinuxFileWatcher] inotify_add_watch failed (%#): %#", normalized.c_str(), strerror( errno ) );
			return false;
		}

		std::lock_guard<std::mutex> lock{ _watchMutex };
		_watchDescriptorToPath[wd] = normalized;
		return true;
	}

	bool LinuxFileWatcher::addWatchRecursive( const std::string& directoryPath )
	{
		if ( addWatchDirectory( directoryPath ) == false )
			return false;

		namespace fs = std::filesystem;
		std::error_code ec;
		for ( fs::recursive_directory_iterator it( directoryPath, fs::directory_options::skip_permission_denied, ec ), end;
			  it != end && !ec; it.increment( ec ) )
		{
			if ( it->is_directory( ec ) == false )
				continue;
			if ( addWatchDirectory( it->path().string() ) == false )
			{
				SW_LOG_WARNING( "[LinuxFileWatcher] Failed to watch subdirectory: %#", it->path().string().c_str() );
			}
		}
		return true;
	}

	void LinuxFileWatcher::removeWatch( int watchDescriptor )
	{
		std::lock_guard<std::mutex> lock{ _watchMutex };
		auto						it = _watchDescriptorToPath.find( watchDescriptor );
		if ( it == _watchDescriptorToPath.end() )
			return;
		inotify_rm_watch( _inotifyFd, watchDescriptor );
		_watchDescriptorToPath.erase( it );
	}

	void LinuxFileWatcher::pushEvent( FileWatcherAction action, const std::string& absoluteDirectory, const std::string& name )
	{
		const std::string absoluteFile = FileUtil::normalizeSeparators( absoluteDirectory + "/" + name );
		const std::string relative	   = makeRelativePath( _directoryPath, absoluteFile );

		FileChangeEvent eventObj{};
		eventObj._action	= action;
		eventObj._directory = _directoryPath;
		eventObj._filename	= relative.empty() ? name : relative;

		std::lock_guard<std::mutex> lock{ _eventMutex };
		_eventQueue.push_back( std::move( eventObj ) );
	}

	void LinuxFileWatcher::workerThreadMain()
	{
		alignas( inotify_event ) uint8 buffer[kInotifyEventBufferSize];

		while ( _bIsWatching )
		{
			fd_set readSet;
			FD_ZERO( &readSet );
			FD_SET( _inotifyFd, &readSet );
			FD_SET( _wakeFd, &readSet );
			const int maxFd = ( _inotifyFd > _wakeFd ) ? _inotifyFd : _wakeFd;

			const int ready = select( maxFd + 1, &readSet, nullptr, nullptr, nullptr );
			if ( ready < 0 )
			{
				if ( errno == EINTR )
					continue;
				SW_LOG_ERROR( "[LinuxFileWatcher] select failed: %#", strerror( errno ) );
				break;
			}

			if ( FD_ISSET( _wakeFd, &readSet ) )
			{
				uint64 value = 0;
				(void)!read( _wakeFd, &value, sizeof( value ) );
				if ( _bIsWatching == false )
					break;
			}

			if ( FD_ISSET( _inotifyFd, &readSet ) == false )
				continue;

			while ( true )
			{
				const ssize_t bytesRead = read( _inotifyFd, buffer, sizeof( buffer ) );
				if ( bytesRead < 0 )
				{
					if ( errno == EAGAIN || errno == EWOULDBLOCK )
						break;
					if ( errno == EINTR )
						continue;
					SW_LOG_ERROR( "[LinuxFileWatcher] read failed: %#", strerror( errno ) );
					_bIsWatching = false;
					return;
				}
				if ( bytesRead == 0 )
					break;

				ssize_t offset = 0;
				while ( offset < bytesRead )
				{
					const auto* event = reinterpret_cast<const inotify_event*>( buffer + offset );
					offset += static_cast<ssize_t>( sizeof( inotify_event ) + event->len );

					if ( event->mask & IN_Q_OVERFLOW )
					{
						SW_LOG_WARNING( "[LinuxFileWatcher] inotify queue overflow — some events may be lost." );
						continue;
					}

					std::string watchedDir;
					{
						std::lock_guard<std::mutex> lock{ _watchMutex };
						auto						it = _watchDescriptorToPath.find( event->wd );
						if ( it == _watchDescriptorToPath.end() )
							continue;
						watchedDir = it->second;
					}

					if ( event->mask & ( IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED ) )
					{
						removeWatch( event->wd );
						continue;
					}

					if ( event->len == 0 || event->name[0] == '\0' )
						continue;

					const std::string name( event->name );
					const bool		  bIsDir = ( event->mask & IN_ISDIR ) != 0;

					if ( ( event->mask & IN_CREATE ) && bIsDir && _bRecursive )
					{
						const std::string childDir = FileUtil::normalizeSeparators( watchedDir + "/" + name );
						if ( addWatchDirectory( childDir ) == false )
						{
							SW_LOG_WARNING( "[LinuxFileWatcher] Failed to watch new directory: %#", childDir.c_str() );
						}
						pushEvent( FileWatcherAction::Added, watchedDir, name );
						continue;
					}

					if ( event->mask & IN_CREATE )
						pushEvent( FileWatcherAction::Added, watchedDir, name );
					else if ( event->mask & IN_DELETE )
						pushEvent( FileWatcherAction::Removed, watchedDir, name );
					else if ( event->mask & IN_MOVED_FROM )
						pushEvent( FileWatcherAction::RenamedOldName, watchedDir, name );
					else if ( event->mask & IN_MOVED_TO )
						pushEvent( FileWatcherAction::RenamedNewName, watchedDir, name );
					else if ( event->mask & ( IN_MODIFY | IN_CLOSE_WRITE ) )
						pushEvent( FileWatcherAction::Modified, watchedDir, name );
				}
			}
		}
	}
} // namespace sw

#endif // SW_PLATFORM_LINUX
