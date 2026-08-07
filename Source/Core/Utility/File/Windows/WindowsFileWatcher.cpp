#include "pch.h"
#include "WindowsFileWatcher.h"

#if defined( SW_PLATFORM_WINDOWS )
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/String/StringUtil.h"
#include "Core/Utility/File/FileUtil.h"
#include <Windows.h>

namespace sw
{
	WindowsFileWatcher::WindowsFileWatcher()
	{
	}

	WindowsFileWatcher::~WindowsFileWatcher()
	{
		stopWatching();
	}

	bool WindowsFileWatcher::startWatching( const std::string_view directoryPath, bool bRecursive )
	{
		if ( _bIsWatching )
			return false;

		std::wstring wDir = StringUtil::utf8ToUtf16( directoryPath );
		_hDirectory = CreateFileW(
			wDir.c_str(),
			FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
			nullptr
		);

		if ( _hDirectory == INVALID_HANDLE_VALUE )
		{
			SW_LOG_ERROR( "Failed to open directory for watching: %#", directoryPath );
			return false;
		}

		_hCompletionPort = CreateIoCompletionPort( _hDirectory, nullptr, 0, 1 );
		if ( _hCompletionPort == nullptr )
		{
			CloseHandle( _hDirectory );
			_hDirectory = nullptr;
			SW_LOG_ERROR( "Failed to create IO completion port for directory watching: %#", directoryPath );
			return false;
		}

		_directoryPath = directoryPath;
		_bRecursive = bRecursive;
		_bIsWatching = true;

		_workerThread = std::thread( &WindowsFileWatcher::workerThreadMain, this );

		SW_LOG_INFO( "Started watching directory: %#", directoryPath );
		return true;
	}

	uint32 WindowsFileWatcher::pollEvents( std::vector<FileChangeEvent>& outEvents )
	{
		std::lock_guard<std::mutex> lock{  _eventMutex  };
		uint32 count = static_cast<uint32>( _eventQueue.size() );
		if ( count > 0 )
		{
			outEvents.insert( outEvents.end(), _eventQueue.begin(), _eventQueue.end() );
			_eventQueue.clear();
		}
		return count;
	}

	void WindowsFileWatcher::stopWatching()
	{
		if ( !_bIsWatching )
			return;

		_bIsWatching = false;

		if ( _hCompletionPort )
		{
			PostQueuedCompletionStatus( _hCompletionPort, 0, 0, nullptr );
		}

		if ( _workerThread.joinable() )
		{
			_workerThread.join();
		}

		if ( _hDirectory && _hDirectory != INVALID_HANDLE_VALUE )
		{
			CancelIoEx( _hDirectory, nullptr );
			CloseHandle( _hDirectory );
			_hDirectory = nullptr;
		}

		if ( _hCompletionPort )
		{
			CloseHandle( _hCompletionPort );
			_hCompletionPort = nullptr;
		}
	}

	void WindowsFileWatcher::workerThreadMain()
	{
		constexpr DWORD bufferSize = 16 * 1024;
		std::vector<uint8> buffer( bufferSize );
		OVERLAPPED overlapped{};

		DWORD notifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME |
							 FILE_NOTIFY_CHANGE_DIR_NAME |
							 FILE_NOTIFY_CHANGE_ATTRIBUTES |
							 FILE_NOTIFY_CHANGE_SIZE |
							 FILE_NOTIFY_CHANGE_LAST_WRITE |
							 FILE_NOTIFY_CHANGE_CREATION;

		while ( _bIsWatching )
		{
			ZeroMemory( &overlapped, sizeof( OVERLAPPED ) );
			DWORD bytesReturned = 0;

			BOOL bResult = ReadDirectoryChangesW(
				_hDirectory,
				buffer.data(),
				bufferSize,
				_bRecursive ? TRUE : FALSE,
				notifyFilter,
				&bytesReturned,
				&overlapped,
				nullptr
			);

			if ( !bResult )
			{
				break;
			}

			DWORD bytesTransferred = 0;
			ULONG_PTR completionKey = 0;
			LPOVERLAPPED pOverlapped = nullptr;

			BOOL bWait = GetQueuedCompletionStatus(
				_hCompletionPort,
				&bytesTransferred,
				&completionKey,
				&pOverlapped,
				INFINITE
			);

			if ( !_bIsWatching )
			{
				break;
			}

			if ( bWait && pOverlapped )
			{
				if ( bytesTransferred == 0 )
				{
					// Buffer overflow or something else, need to issue again
					continue;
				}

				FILE_NOTIFY_INFORMATION* pNotify = reinterpret_cast<FILE_NOTIFY_INFORMATION*>( buffer.data() );

				std::lock_guard<std::mutex> lock{  _eventMutex  };

				while ( pNotify )
				{
					std::wstring wFileName( pNotify->FileName, pNotify->FileNameLength / sizeof( WCHAR ) );
					std::string fileName = StringUtil::utf16ToUtf8( wFileName );

					FileChangeEvent eventObj;
					eventObj._directory = _directoryPath;
					eventObj._filename = fileName;

					switch ( pNotify->Action )
					{
					case FILE_ACTION_ADDED:				eventObj._action = FileWatcherAction::Added; break;
					case FILE_ACTION_REMOVED:			eventObj._action = FileWatcherAction::Removed; break;
					case FILE_ACTION_MODIFIED:			eventObj._action = FileWatcherAction::Modified; break;
					case FILE_ACTION_RENAMED_OLD_NAME:	eventObj._action = FileWatcherAction::RenamedOldName; break;
					case FILE_ACTION_RENAMED_NEW_NAME:	eventObj._action = FileWatcherAction::RenamedNewName; break;
					default:							eventObj._action = FileWatcherAction::Modified; break;
					}

					_eventQueue.push_back( eventObj );

					if ( pNotify->NextEntryOffset == 0 )
					{
						break;
					}
					pNotify = reinterpret_cast<FILE_NOTIFY_INFORMATION*>( reinterpret_cast<uint8*>( pNotify ) + pNotify->NextEntryOffset );
				}
			}
		}
	}
}

#endif // SW_PLATFORM_WINDOWS
