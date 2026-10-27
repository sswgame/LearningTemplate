#include "pch.h"

#include "Core/File/Linux/LinuxFileWatcher.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/ThreadCrashStack.h"

#if defined( SW_PLATFORM_LINUX )

namespace sw
{
    namespace
    {
        constexpr uint32 kInotifyEventBufferSize = 64 * 1024;
        // IN_ATTRIB 는 mtime 만 바뀐 변경(`touch`, 시각을 지키는 복사)을 알린다. Windows 의 FILE_NOTIFY_CHANGE_ATTRIBUTES 에 해당한다.
        constexpr uint32 kInotifyMask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF;
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "LinuxFileWatcher" );

    LinuxFileWatcher::LinuxFileWatcher()
        : _workerThread{}
        , _watchMutex{}
        , _mapWatchDescriptorToPath{}
        , _inotifyFd{ -1 }
        , _wakeFd{ -1 }
        , _bIsWatching{ false }
        , _bRecursive{ true }
    {
    }

    LinuxFileWatcher::~LinuxFileWatcher()
    {
        stopWatching();
    }

    bool LinuxFileWatcher::startWatching( string_view directoryPath, bool bRecursive )
    {
        if ( _bIsWatching.load( std::memory_order_relaxed ) == true )
        {
            SW_LOG_ERROR( "Already watching %# - stop it before watching %#", _directoryPath.c_str(), string{ directoryPath }.c_str() );
            return false;
        }
        stopWatching(); // 워커가 오류로 끝났으면 남은 fd · 스레드를 거둔다

        if ( FileUtil::isDirectory( directoryPath ) == false )
        {
            SW_LOG_ERROR( "Directory does not exist: %#", string{ directoryPath }.c_str() );
            return false;
        }

        _inotifyFd = inotify_init1( IN_NONBLOCK | IN_CLOEXEC );
        if ( _inotifyFd < 0 )
        {
            SW_LOG_ERROR( "inotify_init1 failed: %#", strerror( errno ) );
            return false;
        }

        _wakeFd = eventfd( 0, EFD_NONBLOCK | EFD_CLOEXEC );
        if ( _wakeFd < 0 )
        {
            SW_LOG_ERROR( "eventfd failed: %#", strerror( errno ) );
            close( _inotifyFd );
            _inotifyFd = -1;
            return false;
        }

        _directoryPath = FileUtil::normalizeSeparators( string{ directoryPath } );
        _bRecursive    = bRecursive;

        const bool bAdded = _bRecursive ? addWatchRecursive( _directoryPath ) : addWatchDirectory( _directoryPath );
        if ( bAdded == false )
        {
            // 하위 디렉터리의 실패는 addWatchRecursive 가 모아서 경고하고 넘어간다. 여기에 오는 것은 **루트조차** 걸지 못한
            // 경우뿐이라, 감시 자체가 성립하지 않는다.
            SW_LOG_ERROR( "inotify_add_watch failed for root (%#): %#", _directoryPath.c_str(), strerror( errno ) );
            stopWatching();
            return false;
        }

        _bIsWatching  = true;
        _workerThread = sw::thread( &LinuxFileWatcher::workerThreadMain, this, Memory::getCurrentMemoryTag() );

        SW_LOG_INFO( "Started watching directory: %#", _directoryPath.c_str() );
        return true;
    }

    void LinuxFileWatcher::stopWatching()
    {
        const bool bAlreadyStopped = _bIsWatching == false && _inotifyFd < 0 && _wakeFd < 0;
        if ( bAlreadyStopped )
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
            ScopedLock<mutex> lock{ _watchMutex };
            for ( const pair<int32, string>& pair : _mapWatchDescriptorToPath )
            {
                inotify_rm_watch( _inotifyFd, pair.first );
            }
            _mapWatchDescriptorToPath.clear();
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

    void LinuxFileWatcher::workerThreadMain( MemoryTag memoryTag )
    {
        // 이 스레드의 할당은 감시를 시작한 쪽의 용도로 센다.
        const ScopedMemoryTag threadMemoryTag{ memoryTag };
        // 이 스레드에서 스택이 넘쳐도 크래시 리포트가 남게 한다(ThreadCrashStack::initializeCurrentThread 설명).
        ThreadCrashStack::initializeCurrentThread();
        alignas( inotify_event ) uint8 buffer[kInotifyEventBufferSize];

        while ( _bIsWatching )
        {
            fd_set readSet;
            FD_ZERO( &readSet );
            FD_SET( _inotifyFd, &readSet );
            FD_SET( _wakeFd, &readSet );
            const int32 maxFd = ( _inotifyFd > _wakeFd ) ? _inotifyFd : _wakeFd;

            const int32 ready = select( maxFd + 1, &readSet, nullptr, nullptr, nullptr );
            if ( ready < 0 )
            {
                if ( errno == EINTR )
                    continue;
                reportWatchLost( string( "select failed: " ) + strerror( errno ) );
                _bIsWatching = false;
                return;
            }

            if ( FD_ISSET( _wakeFd, &readSet ) )
            {
                uint64 value{ 0 };
                (void)!read( _wakeFd, &value, sizeof( value ) );
                if ( _bIsWatching == false )
                    break;
            }

            if ( FD_ISSET( _inotifyFd, &readSet ) && drainEvents( buffer, sizeof( buffer ) ) == false )
            {
                _bIsWatching = false;
                return;
            }
        }
    }

    bool LinuxFileWatcher::drainEvents( uint8* pBuffer, uint32 bufferSize )
    {
        while ( true )
        {
            const ssize_t bytesRead = read( _inotifyFd, pBuffer, bufferSize );
            if ( bytesRead < 0 )
            {
                if ( errno == EAGAIN || errno == EWOULDBLOCK )
                    return true;
                if ( errno == EINTR )
                    continue;
                reportWatchLost( string( "inotify read failed: " ) + strerror( errno ) );
                return false;
            }
            if ( bytesRead == 0 )
                return true;

            ssize_t offset{ 0 };
            while ( offset < bytesRead )
            {
                const inotify_event* pEvent = reinterpret_cast<const inotify_event*>( pBuffer + offset );
                offset += static_cast<ssize_t>( sizeof( inotify_event ) + pEvent->len );
                // 이름은 NUL 로 채워 정렬되므로 string_view 가 첫 NUL 에서 끊는다. 이름이 없는 이벤트(디렉터리 자신)는 빈 이름이다.
                const string_view name = pEvent->len > 0 ? string_view{ pEvent->name } : string_view{};
                if ( handleWatchEvent( pEvent->wd, pEvent->mask, name ) == false )
                    return false;
            }
        }
    }

    bool LinuxFileWatcher::handleWatchEvent( int32 watchDescriptor, uint32 mask, string_view name )
    {
        if ( mask & IN_Q_OVERFLOW )
        {
            SW_LOG_WARNING( "inotify queue overflow — emitting synthetic rescan event." );
            pushRelativeChange( FileWatcherAction::Modified, _directoryPath, {} );
            return true;
        }

        string watchedDir;
        {
            ScopedLock<mutex> lock{ _watchMutex };
            auto              it = _mapWatchDescriptorToPath.find( watchDescriptor );
            if ( it == _mapWatchDescriptorToPath.end() )
                return true;
            watchedDir = it->second;
        }

        if ( mask & ( IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED ) )
        {
            removeWatch( watchDescriptor );
            // 하위 폴더가 사라지는 것은 평범한 변경이지만, 루트가 사라지면 이 뒤로 아무 변경도 오지 않는다.
            if ( watchedDir == _directoryPath )
            {
                reportWatchLost( "the watched root directory was deleted or moved" );
                return false;
            }
            return true;
        }

        if ( name.empty() )
            return true;

        const bool bNewRecursiveDirectory = ( mask & IN_CREATE ) && ( mask & IN_ISDIR ) && _bRecursive;
        if ( bNewRecursiveDirectory )
        {
            const string childDir = FileUtil::normalizeSeparators( FileUtil::joinPath( watchedDir, name ) );
            if ( addWatchDirectory( childDir ) == false )
                SW_LOG_WARNING( "Failed to watch new directory: %#", childDir.c_str() );
            pushRelativeChange( FileWatcherAction::Added, watchedDir, name );
            return true;
        }

        if ( mask & IN_CREATE )
            pushRelativeChange( FileWatcherAction::Added, watchedDir, name );
        else if ( mask & IN_DELETE )
            pushRelativeChange( FileWatcherAction::Removed, watchedDir, name );
        else if ( mask & IN_MOVED_FROM )
            pushRelativeChange( FileWatcherAction::RenamedOldName, watchedDir, name );
        else if ( mask & IN_MOVED_TO )
            pushRelativeChange( FileWatcherAction::RenamedNewName, watchedDir, name );
        else if ( mask & ( IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB ) )
            pushRelativeChange( FileWatcherAction::Modified, watchedDir, name );
        return true;
    }

    bool LinuxFileWatcher::addWatchRecursive( string_view directoryPath )
    {
        if ( addWatchDirectory( directoryPath ) == false )
            return false;

        uint32 failedCount{ 0 };
        (void)FileUtil::forEachDirectoryEntry( directoryPath, true, [this, &failedCount]( const DirectoryEntry& entry )
        {
            if ( entry._bDirectory && addWatchDirectory( entry._path ) == false )
                ++failedCount;
            return true;
        } );

        // 실패는 보통 디렉터리마다 따로 나는 것이 아니라 한도(max_user_watches)를 넘긴 순간부터 전부 난다.
        // 디렉터리마다 로그를 찍으면 묻히므로 한 줄로 모으고, 흔한 원인을 함께 적는다.
        if ( failedCount > 0 )
        {
            SW_LOG_WARNING( "Failed to watch %# subdirectories - changes below them will not be detected. "
                            "This may be the inotify limit (/proc/sys/fs/inotify/max_user_watches).",
                            failedCount );
        }
        return true;
    }

    bool LinuxFileWatcher::addWatchDirectory( string_view directoryPath )
    {
        const string normalized = FileUtil::normalizeSeparators( directoryPath );
        const int32  wd         = inotify_add_watch( _inotifyFd, normalized.c_str(), kInotifyMask );
        if ( wd < 0 )
        {
            SW_LOG_TRACE( "inotify_add_watch failed (%#): %#", normalized.c_str(), strerror( errno ) );
            return false;
        }

        ScopedLock<mutex> lock{ _watchMutex };
        _mapWatchDescriptorToPath[wd] = normalized;
        return true;
    }

    void LinuxFileWatcher::removeWatch( int32 watchDescriptor )
    {
        ScopedLock<mutex> lock{ _watchMutex };
        auto              it = _mapWatchDescriptorToPath.find( watchDescriptor );
        if ( it == _mapWatchDescriptorToPath.end() )
            return;
        inotify_rm_watch( _inotifyFd, watchDescriptor );
        _mapWatchDescriptorToPath.erase( it );
    }

    void LinuxFileWatcher::pushRelativeChange( FileWatcherAction action, string_view absoluteDirectory, string_view name )
    {
        // inotify 는 watch 를 건 디렉터리를 기준으로 이름을 준다. 소비자는 `_directory` 와 `_filename` 을 이어 붙여 쓰므로
        // (감시 루트 기준 상대 경로) 여기서 루트 기준으로 되돌린다.
        const string absoluteFile = FileUtil::joinPath( absoluteDirectory, name );
        string       relative;
        const bool   bUnderRoot = FileUtil::tryMakePathUnderRoot( _directoryPath, absoluteFile, relative );

        // 상한 · 넘침 표시 · 연속 중복 합치기는 IFileWatcher::pushChange 가 한다.
        pushChange( action, _directoryPath, bUnderRoot ? string_view{ relative } : name );
    }
} // namespace sw

#endif // SW_PLATFORM_LINUX
