#include "pch.h"

#include "DevTools/FileWatch/FileWatchService.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/pair.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    FileWatchService::FileWatchService()
        : FileWatchService{ SW_DELEGATE_LAMBDA( CreateWatcherDelegate, []()
    { return FileWatcher::createNative( FileWatcherKey{} ); } ) }
    {
    }

    FileWatchService::FileWatchService( const CreateWatcherDelegate& createWatcher )
        : _createWatcher{ createWatcher }
        , _listRoot{}
        , _listWatch{}
        , _idAllocator{}
    {
    }

    FileWatchService::~FileWatchService() = default; // 감시자의 소멸자가 워커를 멈추고 합류한다

    Subscription FileWatchService::watch( const DiskPath& folder, FileWatchDepth depth, const vector<string>& listExtension, const FileChangeDelegate& onMatch )
    {
        const DiskPath normalizedFolder = folder.join( {} );
        if ( normalizedFolder.empty() )
        {
            SW_LOG_WARNING_ID( "FileWatchService.EmptyFolder", "File watch: the folder is empty (no content root folder) - hot reload is off" );
            return {};
        }
        const optional<uint64> rootID = acquireRoot( normalizedFolder, depth );
        if ( rootID.has_value() == false )
            return {};
        WatchEntry entry{};
        entry._id               = _idAllocator.allocate();
        entry._rootID           = *rootID;
        entry._folder           = normalizedFolder;
        entry._comparableFolder = FileUtil::makeComparableDiskPath( normalizedFolder.str() );
        entry._depth            = depth;
        entry._listExtension    = listExtension;
        entry._onMatch          = onMatch;
        const uint64 id         = entry._id;
        _listWatch.push_back( std::move( entry ) );
        SW_LOG_TRACE_ID( "FileWatchService.RegisteredWatch", "Registered watch %# (ext count %#)", normalizedFolder.str(), static_cast<uint32>( listExtension.size() ) );
        return _subscriptions.issue( id );
    }

    optional<uint64> FileWatchService::acquireRoot( const DiskPath& folder, FileWatchDepth depth )
    {
        const ComparableDiskPath comparableFolder = FileUtil::makeComparableDiskPath( folder.str() );
        for ( const Root& root : _listRoot )
        {
            const bool bSameFolder = root._comparablePath == comparableFolder && ( root._depth == FileWatchDepth::Tree || depth == FileWatchDepth::Folder );
            const bool bInsideTree = root._depth == FileWatchDepth::Tree && FileUtil::startsWithPathComponent( comparableFolder, root._comparablePath );
            if ( bSameFolder || bInsideTree )
                return root._id;
        }
        unique_ptr<FileWatcher> pWatcher = _createWatcher();
        if ( pWatcher == nullptr )
        {
            // 설계상 없는 것이다(웹 — 브라우저 파일 시스템에는 바깥 편집이 없다) — 경고가 아니라 정보 한 줄이다.
            SW_LOG_INFO_ID( "FileWatchService.NoNativeWatcher", "File watch: this platform has no OS file watcher - '%#' is not watched", folder.str() );
            return nullopt;
        }
        if ( pWatcher->startWatching( folder, depth == FileWatchDepth::Tree ) == false )
        {
            SW_LOG_ERROR_ID( "FileWatchService.FailedStartWatching", "File watch: failed to start watching %#", folder.str() );
            return nullopt;
        }
        Root root{};
        root._id                = _idAllocator.allocate();
        root._path              = folder.str();
        root._comparablePath    = comparableFolder;
        root._depth             = depth;
        root._pWatcher          = std::move( pWatcher );
        root._lastPollTimestamp = FileUtil::getCurrentFileTimestamp();
        const uint64 id         = root._id;
        _listRoot.push_back( std::move( root ) );
        return id;
    }

    void FileWatchService::update()
    {
        // 콜백이 감시를 걸고 뺄 수 있어(루트가 생기고 사라진다) 모두 꺼낸 뒤 나눠 준다.
        vector<pair<uint64, vector<FileChangeEvent>>> listBatch;
        for ( Root& root : _listRoot )
        {
            vector<FileChangeEvent> listEvent;
            pollRoot( root, listEvent );
            if ( listEvent.empty() == false )
                listBatch.emplace_back( root._id, std::move( listEvent ) );
        }
        if ( listBatch.empty() )
            return;
        // 감시 트리의 파일이 바뀌었다 — 바깥 도구(git, 바깥 편집기, 기다리지 않은 쿠커 자식)가 만들거나 지운 파일은 이 프로세스의 `FileUtil` 이 몰라 경로 캐시의
        // "없음" · "있음" 이 낡았을 수 있다. 받는 쪽이 새 경로를 묻기 전에 한 번 비운다.
        engine::getSubsystem<AssetPaths>().clearPathCache();
        for ( const auto& [rootID, listEvent] : listBatch )
        {
            dispatch( rootID, listEvent );
        }
    }

    void FileWatchService::pollRoot( Root& root, vector<FileChangeEvent>& outListEvent )
    {
        // 잃은 알림은 "직전 비우기 이후" 에 일어났다. 그 시각을 이번 비우기의 시각으로 바꾸기 전에 붙잡아 둔다.
        const uint64 sinceTimestamp = root._lastPollTimestamp;
        root._lastPollTimestamp     = FileUtil::getCurrentFileTimestamp();
        if ( root._pWatcher == nullptr )
            return;
        vector<FileChangeEvent>   listEvent;
        const FileWatchPollResult poll = root._pWatcher->pollEvents( listEvent );
        if ( poll._bWatchLost )
        {
            // 워커가 이유를 이미 오류로 남겼다. 다시 걸어도 같은 실패가 되풀이되므로 감시를 내리고, 마지막 리스캔은 아래에서 처리한다.
            SW_LOG_ERROR_ID( "FileWatchService.WatchingStopped", "File watch: watching of '%#' stopped - its changes are no longer reported", root._path );
            root._pWatcher->stopWatching();
            root._pWatcher.reset();
        }
        if ( listEvent.empty() )
            return;
        expandRescanEvents( root, listEvent, sinceTimestamp );
        // 파일마다 직전 동작과 같은 동작은 한 번이다 — 저장 한 번에 다른 파일의 사건이 끼어 둘로 온다. 동작이 바뀌면(지움 → 만듦) 차례 그대로 남긴다.
        unordered_map<string, FileWatcherAction> mapLastAction;
        for ( FileChangeEvent& changeEvent : listEvent )
        {
            const string key = changeEvent.makePath().str();
            const auto   it  = mapLastAction.find( key );
            if ( it != mapLastAction.end() && it->second == changeEvent._action )
                continue;
            mapLastAction.insert_or_assign( key, changeEvent._action );
            outListEvent.push_back( std::move( changeEvent ) );
        }
    }

    void FileWatchService::dispatch( uint64 rootID, const vector<FileChangeEvent>& listEvent )
    {
        for ( const FileChangeEvent& changeEvent : listEvent )
        {
            const AbsolutePath       fullPath       = changeEvent.makePath();
            const ComparableDiskPath comparablePath = FileUtil::makeComparableDiskPath( fullPath.str() );
            const ComparableDiskPath comparableDir  = FileUtil::makeComparableDiskPath( fullPath.getParent().str() );
            // **콜백이 감시를 등록 · 해제할 수 있다.** 인덱스로 돌면서 델리게이트를 부르기 전에 복사한다(`MulticastDelegate::broadcast` 와 같은 모양). 순회 도중 앞쪽 감시가
            // 빠지면 뒤의 하나가 당겨져 건너뛰어질 수 있는데, 부르는 도중 목록을 바꾼 쪽이 감수한다.
            for ( size_t watchIndex = 0; watchIndex < _listWatch.size(); ++watchIndex )
            {
                const WatchEntry& entry   = _listWatch[watchIndex];
                const bool        bInside = entry._depth == FileWatchDepth::Tree ? FileUtil::startsWithPathComponent( comparablePath, entry._comparableFolder )
                                                                                 : comparableDir == entry._comparableFolder;
                if ( entry._rootID != rootID || bInside == false || isExtensionAllowed( entry, changeEvent._filename ) == false )
                    continue;
                const FileChangeDelegate onMatch = entry._onMatch;
                if ( onMatch.isBound() )
                    onMatch( changeEvent );
            }
        }
    }

    void FileWatchService::releaseWatch( uint64 id )
    {
        VectorUtil::removeIf( _listWatch, [id]( const WatchEntry& entry )
        { return entry._id == id; } );
        // 감시가 하나도 남지 않은 루트는 감시자(핸들 · 스레드)를 내린다.
        VectorUtil::removeIf( _listRoot, [this]( const Root& root )
        {
            for ( const WatchEntry& entry : _listWatch )
            {
                if ( entry._rootID == root._id )
                    return false;
            }
            return true;
        } );
    }

    void FileWatchService::expandRescanEvents( const Root& root, vector<FileChangeEvent>& inoutListEvent, uint64 sinceTimestamp ) const
    {
        // 파일 이름이 빈 Modified 는 감시자의 **리스캔 신호**다. 큐 상한이나 버퍼 오버플로로 개별 알림을
        // 잃었다는 뜻이다(브랜치 전환 · 쿠킹 · 대량 임포트). OS 는 잃은 알림을 돌려주지 않으므로, 무엇이
        // 바뀌었는지는 상태를 대조해서만 알 수 있다. 다만 그 대조는 **여기서 한 번**, 감시 트리 안에서,
        // 직전 비우기 이후 mtime 인 파일만 stat 으로 확인하고 끝낸다. 평소에는 폴링이 없다.
        // 받는 쪽은 평소와 같은 파일 단위 이벤트만 받고, 리스캔이 있었는지 알 필요가 없다.
        const size_t rescanSignalCount = VectorUtil::removeIf( inoutListEvent, []( const FileChangeEvent& event )
        { return event._filename.empty(); } );
        if ( rescanSignalCount == 0 )
            return;

        // 살아남은 개별 이벤트와 같은 파일은 다시 내지 않는다. 리로드는 멱등이지만 두 번 할 이유는 없다.
        // 체크아웃 한 번에 수천 건이 되므로 중복 검사는 해시 집합으로 한다(목록 검색이면 O(N^2)).
        unordered_set<string> uniqueSetEmittedPath;
        for ( const FileChangeEvent& changeEvent : inoutListEvent )
        {
            uniqueSetEmittedPath.insert( FileUtil::makeComparableDiskPath( changeEvent.makePath().str() ).str() ); // 디스크 경로라 OS 규칙(리눅스는 대소문자를 가른다)
        }

        // 되찾은 개수를 쓰는 곳은 아래 로그뿐이다. SW_LOG_INFO 는 Shipping 에서 통째로 사라지므로
        // 그 구성에서는 쓰이지 않는 변수가 된다.
        [[maybe_unused]] uint32 found{ 0 };
        for ( const WatchEntry& entry : _listWatch )
        {
            if ( entry._rootID != root._id )
                continue;
            if ( FileUtil::isDirectory( entry._folder ) == false )
                continue;

            vector<DiskPath> listFile;
            if ( FileUtil::collectFiles( entry._folder, {}, listFile, entry._depth == FileWatchDepth::Tree ) == false )
                SW_LOG_WARNING_ID( "FileWatchService.WatchTreeCouldNot", "Watch tree '%#' could not be listed completely - changes in the part left out are not recovered", entry._folder.str() );
            for ( const DiskPath& filePath : listFile )
            {
                if ( isExtensionAllowed( entry, filePath.str() ) == false )
                    continue;
                uint64 mtime = 0;
                if ( FileUtil::tryGetFileTimestamp( filePath, mtime ) == false || mtime < sinceTimestamp )
                    continue; // 그 사이 지워졌거나 그 뒤로 바뀌지 않았다

                const string normalized = FileUtil::makeComparableDiskPath( filePath.str() ).str(); // 집합 키
                if ( uniqueSetEmittedPath.insert( normalized ).second == false )
                    continue;

                string relative{};
                if ( FileUtil::makeRelativePath( root._path, filePath.str(), relative ) == false || relative.empty() )
                    continue;

                FileChangeEvent changeEvent{};
                changeEvent._action    = FileWatcherAction::Modified;
                changeEvent._directory = root._path; // 감시자의 사건과 같은 기준
                changeEvent._filename  = relative;
                inoutListEvent.push_back( std::move( changeEvent ) );
                ++found;
            }
        }
        SW_LOG_INFO_ID( "FileWatchService.BurstFileChangesMade", "A burst of file changes made the watcher drop individual notifications - recovered %# by diffing the watch tree.", found );
    }

    bool FileWatchService::isExtensionAllowed( const WatchEntry& entry, string_view filename )
    {
        if ( entry._listExtension.empty() )
            return true;

        // **접미사**로 본다. `hasExtension` 은 마지막 점 뒤만 보므로 `.prefab.xml` 같은 복합
        // 접미사가 걸리지 않는다(프리팹 핫 리로드가 등록은 되는데 이벤트를 받지 못한다).
        for ( const string& allowed : entry._listExtension )
        {
            if ( StringUtil::endsWith( filename, allowed, true ) )
                return true;
        }
        return false;
    }
} // namespace sw
