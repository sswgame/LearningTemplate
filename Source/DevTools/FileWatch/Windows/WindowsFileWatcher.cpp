#include "pch.h"

#include "DevTools/FileWatch/Windows/WindowsFileWatcher.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Process/ThreadCrashStack.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Container/string.h"
    #include "Core/Container/vector.h"
    #include "Core/File/FileUtil.h"
    #include "Core/Log/Logger.h"
    #include "Core/Memory/Memory.h"
    #include "Core/Container/StringUtil.h"

SW_LOG_CALLER( "WindowsFileWatcher" );
namespace sw
{
    namespace
    {
        /** @brief `FILE_ACTION_*` 를 변경 종류로 바꿉니다. `FILE_ACTION_MODIFIED` 와 문서에 없는 값은 Modified(그 파일을 다시 확인하라)입니다. */
        FileWatcherAction toWindowsFileWatcherAction( DWORD fileAction )
        {
            switch ( fileAction )
            {
                case FILE_ACTION_ADDED:
                    return FileWatcherAction::Added;
                case FILE_ACTION_REMOVED:
                    return FileWatcherAction::Removed;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    return FileWatcherAction::RenamedOldName;
                case FILE_ACTION_RENAMED_NEW_NAME:
                    return FileWatcherAction::RenamedNewName;
                default:
                    return FileWatcherAction::Modified;
            }
        }
    } // namespace

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
        {
            SW_LOG_ERROR( "Already watching %# - stop it before watching %#", _directoryPath, directoryPath );
            return false;
        }
        stopWatching(); // 워커가 오류로 끝났으면 남은 핸들 · 스레드를 거둔다

        const string dirNt( directoryPath );
        wstring      wDir = StringUtil::utf8ToUTF16( dirNt.c_str() );
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
            _hDirectory = nullptr; // stopWatching 은 nullptr 로 "연 핸들 없음" 을 본다
            SW_LOG_ERROR( "Failed to open directory for watching (error %#): %#", static_cast<uint32>( GetLastError() ), directoryPath );
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

        _workerThread = sw::thread( &WindowsFileWatcher::workerThreadMain, this, Memory::getCurrentMemoryTag() );

        SW_LOG_INFO( "Started watching directory: %#", directoryPath );
        return true;
    }

    void WindowsFileWatcher::stopWatching()
    {
        // 워커가 오류로 먼저 끝났으면(`reportWatchLost`) `_bIsWatching` 은 이미 false 지만 핸들과 스레드는 남아 있다 — 그것까지 본다.
        const bool bAlreadyStopped = _bIsWatching == false && _workerThread.joinable() == false && _hDirectory == nullptr && _hCompletionPort == nullptr;
        if ( bAlreadyStopped )
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

    void WindowsFileWatcher::workerThreadMain( MemoryTag memoryTag )
    {
        // 이 스레드의 할당은 감시를 시작한 쪽의 용도로 센다.
        const ScopedMemoryTag threadMemoryTag{ memoryTag };
        // 이 스레드에서 스택이 넘쳐도 크래시 리포트가 남게 한다(ThreadCrashStack::initializeCurrentThread 설명).
        ThreadCrashStack::initializeCurrentThread();
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
            {
                if ( _bIsWatching )
                    reportWatchLost( string( "ReadDirectoryChangesW failed, error " ) + to_string( static_cast<uint32>( GetLastError() ) ) );
                _bIsWatching = false;
                return;
            }

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

            // 멈추는 중이 아닌데 완료가 실패했다 — 감시 디렉터리가 지워졌거나 핸들이 무효다. 다시 걸어도 같은 실패가 되풀이된다.
            if ( bWait == false )
            {
                reportWatchLost( string( "directory change completion failed, error " ) + to_string( static_cast<uint32>( GetLastError() ) ) );
                _bIsWatching = false;
                return;
            }

            if ( pOverlapped == nullptr )
                continue;
            if ( bytesTransferred == 0 )
            {
                // 버퍼가 넘치면 놓친 변경이 생기므로, 감시 디렉터리에 대한 Modified 이벤트를 만들어 다시 훑게 한다.
                // 파일 이름이 비어 있으면 "전부 다시 확인하라" 는 약속이다.
                pushChange( FileWatcherAction::Modified, _directoryPath, {} );
                continue;
            }
            pushNotifyEntries( buffer.data() );
        }
    }

    void WindowsFileWatcher::pushNotifyEntries( const uint8* pBuffer )
    {
        const FILE_NOTIFY_INFORMATION* pNotify = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>( pBuffer );
        while ( pNotify != nullptr )
        {
            const wstring wFileName( pNotify->FileName, pNotify->FileNameLength / sizeof( WCHAR ) );
            const string  fileName = StringUtil::utf16ToUTF8( wFileName.c_str() );
            // 상한 · 넘침 표시 · 연속 중복 합치기는 IFileWatcher::pushChange 가 한다.
            // 한 번 저장하면 LAST_WRITE 와 SIZE 가 잇달아 오는데 둘 다 Modified 로 합쳐지므로, 그 중복을 거르는 것도
            // 거기서 한다(세 플랫폼이 같은 규칙을 쓴다).
            pushChange( toWindowsFileWatcherAction( pNotify->Action ), _directoryPath, fileName );
            if ( pNotify->NextEntryOffset == 0 )
                break;
            pNotify = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>( reinterpret_cast<const uint8*>( pNotify ) + pNotify->NextEntryOffset );
        }
    }
} // namespace sw

#endif // SW_PLATFORM_WINDOWS
