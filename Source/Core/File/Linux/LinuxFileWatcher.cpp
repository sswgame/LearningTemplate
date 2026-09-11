#include "pch.h"

#include "Core/File/Linux/LinuxFileWatcher.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#if defined( SW_PLATFORM_LINUX )

namespace sw
{
    namespace
    {
        struct LinuxFileWatcherInternal
        {
            static constexpr uint32 kInotifyEventBufferSize = 64 * 1024;
            static constexpr uint32 kInotifyMask            = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF;

            static string makeRelativePath( string_view root, string_view absolutePath )
            {
                const string rootNorm = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( root ) );
                const string absNorm  = FileUtil::normalizeSeparators( absolutePath );
                if ( FileUtil::startsWithPathComponent( absNorm, rootNorm ) == false )
                    return FileUtil::getFileNamePart( absNorm );
                return FileUtil::suffixAfterPathComponent( absNorm, rootNorm );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "LinuxFileWatcher" );

    LinuxFileWatcher::LinuxFileWatcher()
        : _workerThread{}
        , _eventMutex{}
        , _watchMutex{}
        , _directoryPath{}
        , _listEventQueue{}
        , _mapWatchDescriptorToPath{}
        , _inotifyFd{ -1 }
        , _wakeFd{ -1 }
        , _bIsWatching{ false }
        , _bRecursive{ true }
        , _bEventQueueOverflowed{ false }
    {
    }

    LinuxFileWatcher::~LinuxFileWatcher()
    {
        stopWatching();
    }

    bool LinuxFileWatcher::startWatching( string_view directoryPath, bool bRecursive )
    {
        if ( _bIsWatching.load( std::memory_order_relaxed ) == true )
            return false;

        if ( FileUtil::directoryExists( directoryPath ) == false )
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
            // 하위 디렉터리 실패는 addWatchRecursive 가 모아서 경고하고 넘어간다. 여기 오는 건
            // **루트조차** 못 건 경우뿐이라 감시 자체가 성립하지 않는다.
            SW_LOG_ERROR( "inotify_add_watch failed for root (%#): %#", _directoryPath.c_str(), strerror( errno ) );
            stopWatching();
            return false;
        }

        _bIsWatching  = true;
        _workerThread = std::thread( &LinuxFileWatcher::workerThreadMain, this );

        SW_LOG_INFO( "Started watching directory: %#", _directoryPath.c_str() );
        return true;
    }

    uint32 LinuxFileWatcher::pollEvents( vector<FileChangeEvent>& outListEvent )
    {
        std::scoped_lock<mutex> lock{ _eventMutex };
        const uint32            count = static_cast<uint32>( _listEventQueue.size() );
        if ( count > 0 )
        {
            outListEvent.insert( outListEvent.end(), _listEventQueue.begin(), _listEventQueue.end() );
            _listEventQueue.clear();
        }

        if ( _bEventQueueOverflowed )
        {
            // 버린 것이 있었다 — 개별 변경은 이미 잃었으므로 "전부 다시 훑어라" 하나로 알린다.
            FileChangeEvent rescanEvent{};
            rescanEvent._action    = FileWatcherAction::Modified;
            rescanEvent._directory = _directoryPath;
            outListEvent.push_back( std::move( rescanEvent ) );
            _bEventQueueOverflowed = false;
            return count + 1;
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
            std::scoped_lock<mutex> lock{ _watchMutex };
            for ( const pair<int32, string>& pair : _mapWatchDescriptorToPath )
                inotify_rm_watch( _inotifyFd, pair.first );
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

    void LinuxFileWatcher::workerThreadMain()
    {
        alignas( inotify_event ) uint8 buffer[LinuxFileWatcherInternal::kInotifyEventBufferSize];

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
                SW_LOG_ERROR( "select failed: %#", strerror( errno ) );
                break;
            }

            if ( FD_ISSET( _wakeFd, &readSet ) )
            {
                uint64 value{ 0 };
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
                    SW_LOG_ERROR( "read failed: %#", strerror( errno ) );
                    _bIsWatching = false;
                    return;
                }
                if ( bytesRead == 0 )
                    break;

                ssize_t offset{ 0 };
                while ( offset < bytesRead )
                {
                    const inotify_event* pEvent = reinterpret_cast<const inotify_event*>( buffer + offset );
                    offset += static_cast<ssize_t>( sizeof( inotify_event ) + pEvent->len );

                    if ( pEvent->mask & IN_Q_OVERFLOW )
                    {
                        SW_LOG_WARNING( "inotify queue overflow — emitting synthetic rescan event." );
                        pushEvent( FileWatcherAction::Modified, _directoryPath, {} );
                        continue;
                    }

                    string watchedDir;
                    {
                        std::scoped_lock<mutex> lock{ _watchMutex };
                        auto                    it = _mapWatchDescriptorToPath.find( pEvent->wd );
                        if ( it == _mapWatchDescriptorToPath.end() )
                            continue;
                        watchedDir = it->second;
                    }

                    if ( pEvent->mask & ( IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED ) )
                    {
                        removeWatch( pEvent->wd );
                        continue;
                    }

                    if ( pEvent->len == 0 || pEvent->name[0] == '\0' )
                        continue;

                    const string name( pEvent->name );
                    const bool   bIsDir = ( pEvent->mask & IN_ISDIR ) != 0;

                    if ( ( pEvent->mask & IN_CREATE ) && bIsDir && _bRecursive )
                    {
                        const string childDir = FileUtil::normalizeSeparators( FileUtil::joinPath( watchedDir, name ) );
                        if ( addWatchDirectory( childDir ) == false )
                            SW_LOG_WARNING( "Failed to watch new directory: %#", childDir.c_str() );
                        pushEvent( FileWatcherAction::Added, watchedDir, name );
                        continue;
                    }

                    if ( pEvent->mask & IN_CREATE )
                        pushEvent( FileWatcherAction::Added, watchedDir, name );
                    else if ( pEvent->mask & IN_DELETE )
                        pushEvent( FileWatcherAction::Removed, watchedDir, name );
                    else if ( pEvent->mask & IN_MOVED_FROM )
                        pushEvent( FileWatcherAction::RenamedOldName, watchedDir, name );
                    else if ( pEvent->mask & IN_MOVED_TO )
                        pushEvent( FileWatcherAction::RenamedNewName, watchedDir, name );
                    else if ( pEvent->mask & ( IN_MODIFY | IN_CLOSE_WRITE ) )
                        pushEvent( FileWatcherAction::Modified, watchedDir, name );
                }
            }
        }
    }

    bool LinuxFileWatcher::addWatchRecursive( string_view directoryPath )
    {
        if ( addWatchDirectory( directoryPath ) == false )
            return false;

        namespace fs = std::filesystem;
        std::error_code ec;
        uint32          failedCount{ 0 };
        for ( fs::recursive_directory_iterator it( directoryPath, fs::directory_options::skip_permission_denied, ec ), end;
              it != end && ec == std::error_code{}; it.increment( ec ) )
        {
            if ( it->is_directory( ec ) == false )
                continue;
            if ( addWatchDirectory( it->path().string() ) == false )
                ++failedCount;
        }

        // 실패는 대개 디렉터리마다 나는 게 아니라 한도(max_user_watches)를 넘긴 순간부터 전부 난다 —
        // 디렉터리별로 찍으면 로그가 묻히므로 한 줄로 모으고, 흔한 원인을 같이 적는다.
        if ( failedCount > 0 )
        {
            SW_LOG_WARNING( "%# 개 하위 디렉터리를 감시하지 못했습니다 — 그 아래 변경은 감지되지 않습니다. "
                            "inotify 한도일 수 있습니다(/proc/sys/fs/inotify/max_user_watches).",
                            failedCount );
        }
        return true;
    }

    bool LinuxFileWatcher::addWatchDirectory( string_view directoryPath )
    {
        const string normalized = FileUtil::normalizeSeparators( directoryPath );
        const int32  wd         = inotify_add_watch( _inotifyFd, normalized.c_str(), LinuxFileWatcherInternal::kInotifyMask );
        if ( wd < 0 )
        {
            SW_LOG_TRACE( "inotify_add_watch failed (%#): %#", normalized.c_str(), strerror( errno ) );
            return false;
        }

        std::scoped_lock<mutex> lock{ _watchMutex };
        _mapWatchDescriptorToPath[wd] = normalized;
        return true;
    }

    void LinuxFileWatcher::removeWatch( int32 watchDescriptor )
    {
        std::scoped_lock<mutex> lock{ _watchMutex };
        auto                    it = _mapWatchDescriptorToPath.find( watchDescriptor );
        if ( it == _mapWatchDescriptorToPath.end() )
            return;
        inotify_rm_watch( _inotifyFd, watchDescriptor );
        _mapWatchDescriptorToPath.erase( it );
    }

    void LinuxFileWatcher::pushEvent( FileWatcherAction action, string_view absoluteDirectory, string_view name )
    {
        const string absoluteFile = FileUtil::normalizeSeparators( FileUtil::joinPath( absoluteDirectory, name ) );
        const string relative     = LinuxFileWatcherInternal::makeRelativePath( _directoryPath, absoluteFile );

        FileChangeEvent eventObj{};
        eventObj._action    = action;
        eventObj._directory = _directoryPath;
        eventObj._filename  = relative.empty() ? name : relative;

        std::scoped_lock<mutex> lock{ _eventMutex };

        if ( _listEventQueue.size() >= _s_kMaxQueuedEvent )
        {
            _bEventQueueOverflowed = true;
            return;
        }

        // 한 번 저장하면 커널이 IN_MODIFY 와 IN_CLOSE_WRITE 를 잇달아 준다 — 둘 다 Modified 로 접히므로
        // 같은 파일에 같은 동작이 연달아 들어오면 하나로 합친다. 중복 리로드를 그만큼 줄인다.
        if ( _listEventQueue.empty() == false )
        {
            const FileChangeEvent& last = _listEventQueue.back();
            if ( last._action == eventObj._action && last._filename == eventObj._filename )
                return;
        }

        _listEventQueue.push_back( std::move( eventObj ) );
    }
} // namespace sw

#endif // SW_PLATFORM_LINUX
