#include "pch.h"

#include "Editor/Common/Workspace/ReloadFileManager.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Resource/ResourceUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/File/Windows/WindowsFileWatcher.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/File/Linux/LinuxFileWatcher.h"
#elif defined( SW_PLATFORM_MACOS )
    #include "Core/File/Mac/MacFileWatcher.h"
#endif

namespace sw
{
    namespace
    {
        struct ReloadFileManagerInternal
        {
            static void considerFileVal( unordered_map<string, uint64>& mapPollMtime, vector<FileChangeEvent>& outListEvent, const vector<string>& listExtension, const string& filePath )
            {
                if ( FileUtil::fileExists( filePath ) == false )
                    return;

                const string filename         = FileUtil::getFileNamePart( filePath );
                bool         extensionAllowed = listExtension.empty();
                if ( extensionAllowed == false )
                {
                    for ( const string& allowed : listExtension )
                    {
                        if ( FileUtil::hasExtension( filename, allowed ) )
                        {
                            extensionAllowed = true;
                            break;
                        }
                    }
                }
                if ( extensionAllowed == false )
                    return;

                const uint64 mtime = FileUtil::getFileTimestamp( filePath );
                if ( mtime == 0 )
                    return;

                const string                                  normalized = FileUtil::normalizePath( filePath );
                const unordered_map<string, uint64>::iterator it         = mapPollMtime.find( normalized );
                if ( it == mapPollMtime.end() )
                {
                    mapPollMtime.emplace( normalized, mtime );
                    return;
                }

                if ( it->second != mtime )
                {
                    it->second = mtime;
                    FileChangeEvent changeEvent{};
                    changeEvent._action    = FileWatcherAction::Modified;
                    changeEvent._directory = FileUtil::normalizePath( FileUtil::getDirectoryPart( filePath ) );
                    changeEvent._filename  = filename;
                    outListEvent.push_back( std::move( changeEvent ) );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ReloadFileManager" );

    ReloadFileManager::ReloadFileManager() = default;

    ReloadFileManager::~ReloadFileManager()
    {
        shutdown();
    }

    bool ReloadFileManager::initialize()
    {
#if defined( SW_PLATFORM_WINDOWS )
        _fileWatcher = make_unique<WindowsFileWatcher>();
#elif defined( SW_PLATFORM_LINUX )
        _fileWatcher = make_unique<LinuxFileWatcher>();
#elif defined( SW_PLATFORM_MACOS )
        _fileWatcher = make_unique<MacFileWatcher>();
#endif

#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
        const string& rootPath = ResourceUtil::getRootFolderPath();
        if ( rootPath.empty() == false )
        {
            if ( _fileWatcher->startWatching( rootPath, true ) == false )
            {
                SW_LOG_ERROR( "Failed to start watching: %#", rootPath );
                _fileWatcher.reset();
                _bUseMtimePoll = true;
                SW_LOG_WARNING( "Falling back to mtime poll." );
            }
        }
        else
        {
            SW_LOG_WARNING( "ReloadFileManager: Root resource path is empty." );
            _bUseMtimePoll = true;
        }
#else
        _bUseMtimePoll = true;
        SW_LOG_INFO( "ReloadFileManager: Using mtime poll fallback (no native file watcher)." );
#endif
        return true;
    }

    void ReloadFileManager::shutdown()
    {
        _listWatch.clear();
        _mapPollMtime.clear();
        _bUseMtimePoll = false;
        if ( _fileWatcher )
        {
            _fileWatcher->stopWatching();
            _fileWatcher.reset();
        }
    }

    void ReloadFileManager::update()
    {
        vector<FileChangeEvent> listEvent;

        if ( _fileWatcher )
            _fileWatcher->pollEvents( listEvent );
        else if ( _bUseMtimePoll )
            pollMtimeFallback( listEvent );
        else
            return;

        if ( listEvent.empty() )
            return;

        dispatchEvents( listEvent );
    }

    FileWatchHandle ReloadFileManager::registerWatch( string_view pathPrefix, const vector<string>& listExtension, const FileWatchMatchDelegate& onMatch )
    {
        WatchEntry entry{};
        entry._handle = FileWatchHandle{ _nextWatchId++ };
        // Keep real FS path for mtime poll / native watchers; matching uses normalizePath.
        entry._pathPrefix    = FileUtil::normalizeSeparators( pathPrefix );
        entry._listExtension = listExtension;
        entry._onMatch       = onMatch;
        _listWatch.push_back( entry );

        SW_LOG_TRACE( "Registered watch %# (ext count %#)", entry._pathPrefix, static_cast<uint32>( listExtension.size() ) );
        return entry._handle;
    }

    void ReloadFileManager::unregisterWatch( FileWatchHandle handle )
    {
        if ( handle.isValid() == false )
            return;

        _listWatch.erase( std::remove_if( _listWatch.begin(), _listWatch.end(),
                                          [&]( const WatchEntry& entry )
        { return entry._handle == handle; } ),
                          _listWatch.end() );
    }

    bool ReloadFileManager::matchesWatch( const WatchEntry& entry, const FileChangeEvent& changeEvent ) const
    {
        const string fullPath = FileUtil::normalizePath( FileUtil::joinPath( changeEvent._directory, changeEvent._filename ) );
        const string prefix   = FileUtil::normalizePath( entry._pathPrefix );

        if ( FileUtil::startsWithPathComponent( fullPath, prefix ) == false )
            return false;

        return extensionAllowed( entry, changeEvent._filename );
    }

    void ReloadFileManager::dispatchEvents( const vector<FileChangeEvent>& listEvent )
    {
        for ( const FileChangeEvent& changeEvent : listEvent )
        {
            bool bAnyMatch{ false };
            for ( const WatchEntry& entry : _listWatch )
            {
                if ( matchesWatch( entry, changeEvent ) == false )
                    continue;

                bAnyMatch = true;
                if ( entry._onMatch.isBound() )
                    entry._onMatch( changeEvent );
            }

            if ( bAnyMatch )
            {
// 가드는 이 문자열을 **쓰는 로그가 컴파일되는가** 와 같아야 한다. 예전엔 "Shipping 아님" 이었는데,
// Release 는 Shipping 이 아니면서 Trace 는 컴파일하지 않는다 — 문자열만 만들고 아무도 안 쓰게 됐다.
#if SW_LOG_LEVEL_COMPILED( 3 )
                // 의도된 기본값이다. 지금 switch 가 모든 열거자를 덮어 "쓰이지 않는 초기화" 로
                // 보이지만, 열거자가 늘면 이 값이 로그에 남아야 한다.
                // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
                const utf8* pActionStr = "Unknown";
                switch ( changeEvent._action )
                {
                    case FileWatcherAction::Added:
                        pActionStr = "Added";
                        break;
                    case FileWatcherAction::Removed:
                        pActionStr = "Removed";
                        break;
                    case FileWatcherAction::Modified:
                        pActionStr = "Modified";
                        break;
                    case FileWatcherAction::RenamedOldName:
                        pActionStr = "RenamedOld";
                        break;
                    case FileWatcherAction::RenamedNewName:
                        pActionStr = "RenamedNew";
                        break;
                    default:
                        break;
                }
                SW_LOG_TRACE( "%# : %#/%#", pActionStr, changeEvent._directory.c_str(), changeEvent._filename.c_str() );
#endif
            }
        }
    }

    void ReloadFileManager::pollMtimeFallback( vector<FileChangeEvent>& outListEvent )
    {
        for ( const WatchEntry& entry : _listWatch )
        {
            if ( FileUtil::fileExists( entry._pathPrefix ) == false &&
                 FileUtil::directoryExists( entry._pathPrefix ) == false )
                continue;

            if ( FileUtil::fileExists( entry._pathPrefix ) )
                ReloadFileManagerInternal::considerFileVal( _mapPollMtime, outListEvent, entry._listExtension, entry._pathPrefix );
            else
            {
                vector<string> listFile;
                FileUtil::collectFiles( entry._pathPrefix, {}, listFile, true );
                for ( const string& filePath : listFile )
                    ReloadFileManagerInternal::considerFileVal( _mapPollMtime, outListEvent, entry._listExtension, filePath );
            }
        }
    }

    bool ReloadFileManager::extensionAllowed( const WatchEntry& entry, string_view filename ) const
    {
        if ( entry._listExtension.empty() )
            return true;

        // **접미사**로 본다. `hasExtension` 은 마지막 점 뒤만 보므로 `.prefab.xml` 같은 복합
        // 접미사가 영영 걸리지 않았다 — 프리팹 핫리로드가 등록은 되는데 이벤트를 못 받았다.
        for ( const string& allowed : entry._listExtension )
        {
            if ( StringUtil::endsWith( filename, allowed, true ) )
                return true;
        }
        return false;
    }
} // namespace sw
