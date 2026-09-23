#include "pch.h"

#include "Core/File/Windows/WindowsFileWatcher.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Concurrency/mutex.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Container/string.h"
    #include "Core/Container/vector.h"
    #include "Core/File/FileUtil.h"
    #include "Core/Log/Logger.h"
    #include "Core/String/StringUtil.h"

SW_LOG_CALLER( "WindowsFileWatcher" );
namespace sw
{
    WindowsFileWatcher::WindowsFileWatcher()
        : _hDirectory{ nullptr }
        , _hCompletionPort{ nullptr }
        , _workerThread{}
        , _bIsWatching{ false }
        , _bRecursive{ true }
    {
    }

    WindowsFileWatcher::~WindowsFileWatcher()
    {
        stopWatching();
    }

    bool WindowsFileWatcher::startWatching( string_view directoryPath, bool bRecursive )
    {
        if ( _bIsWatching )
            return false;

        const string dirNt( directoryPath );
        wstring      wDir = StringUtil::utf8ToUtf16( dirNt.c_str() );
        _hDirectory       = CreateFileW(
            wDir.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr );

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
        _bRecursive    = bRecursive;
        _bIsWatching   = true;

        _workerThread = std::thread( &WindowsFileWatcher::workerThreadMain, this );

        SW_LOG_INFO( "Started watching directory: %#", directoryPath );
        return true;
    }

    void WindowsFileWatcher::stopWatching()
    {
        if ( _bIsWatching == false )
            return;

        _bIsWatching = false;

        if ( _hDirectory && _hDirectory != INVALID_HANDLE_VALUE )
            CancelIoEx( _hDirectory, nullptr );

        if ( _hCompletionPort )
            PostQueuedCompletionStatus( _hCompletionPort, 0, 0, nullptr );

        if ( _workerThread.joinable() )
            _workerThread.join();

        if ( _hDirectory && _hDirectory != INVALID_HANDLE_VALUE )
        {
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
        constexpr DWORD bufferSize = 64 * 1024;
        vector<uint8>   buffer( bufferSize );
        OVERLAPPED      overlapped{};

        DWORD notifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME |
                             FILE_NOTIFY_CHANGE_DIR_NAME |
                             FILE_NOTIFY_CHANGE_ATTRIBUTES |
                             FILE_NOTIFY_CHANGE_SIZE |
                             FILE_NOTIFY_CHANGE_LAST_WRITE |
                             FILE_NOTIFY_CHANGE_CREATION;

        while ( _bIsWatching )
        {
            ZeroMemory( &overlapped, sizeof( OVERLAPPED ) );
            DWORD bytesReturned{ 0 };

            BOOL bResult = ReadDirectoryChangesW(
                _hDirectory,
                buffer.data(),
                bufferSize,
                _bRecursive ? TRUE : FALSE,
                notifyFilter,
                &bytesReturned,
                &overlapped,
                nullptr );

            if ( bResult == false )
                break;

            DWORD        bytesTransferred{ 0 };
            ULONG_PTR    completionKey{ 0 };
            LPOVERLAPPED pOverlapped{ nullptr };

            BOOL bWait = GetQueuedCompletionStatus(
                _hCompletionPort,
                &bytesTransferred,
                &completionKey,
                &pOverlapped,
                INFINITE );

            if ( _bIsWatching == false )
                break;

            if ( bWait && pOverlapped )
            {
                if ( bytesTransferred == 0 )
                {
                    // 버퍼가 넘치면 놓친 변경이 생기므로, 감시 디렉터리에 대한 Modified 이벤트를 만들어 다시 훑게 한다.
                    // 파일 이름이 비어 있으면 "전부 다시 확인하라" 는 약속이다.
                    pushChange( FileWatcherAction::Modified, _directoryPath, {} );
                    continue;
                }

                FILE_NOTIFY_INFORMATION* pNotify = reinterpret_cast<FILE_NOTIFY_INFORMATION*>( buffer.data() );

                while ( pNotify )
                {
                    wstring wFileName( pNotify->FileName, pNotify->FileNameLength / sizeof( WCHAR ) );
                    string  fileName = StringUtil::utf16ToUtf8( wFileName.c_str() );

                    FileWatcherAction action = FileWatcherAction::Modified;
                    switch ( pNotify->Action )
                    {
                        case FILE_ACTION_ADDED:
                        {
                            action = FileWatcherAction::Added;
                            break;
                        }
                        case FILE_ACTION_REMOVED:
                        {
                            action = FileWatcherAction::Removed;
                            break;
                        }
                        case FILE_ACTION_RENAMED_OLD_NAME:
                        {
                            action = FileWatcherAction::RenamedOldName;
                            break;
                        }
                        case FILE_ACTION_RENAMED_NEW_NAME:
                        {
                            action = FileWatcherAction::RenamedNewName;
                            break;
                        }
                        case FILE_ACTION_MODIFIED:
                        default:
                        {
                            action = FileWatcherAction::Modified;
                            break;
                        }
                    }

                    // 상한 · 넘침 표시 · 연속 중복 합치기는 IFileWatcher::pushChange 가 한다.
                    // 한 번 저장하면 LAST_WRITE 와 SIZE 가 잇달아 오는데 둘 다 Modified 로 합쳐지므로, 그 중복을 거르는 것도
                    // 거기서 한다(세 플랫폼이 같은 규칙을 쓴다).
                    pushChange( action, _directoryPath, fileName );

                    if ( pNotify->NextEntryOffset == 0 )
                        break;
                    pNotify = reinterpret_cast<FILE_NOTIFY_INFORMATION*>( reinterpret_cast<uint8*>( pNotify ) + pNotify->NextEntryOffset );
                }
            }
        }
    }
} // namespace sw

#endif // SW_PLATFORM_WINDOWS
