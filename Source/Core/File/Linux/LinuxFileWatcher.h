/**
 * @file LinuxFileWatcher.h
 * @brief inotify + eventfd 워커로 디렉터리 변경을 모읍니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/IFileWatcher.h"

#if defined( SW_PLATFORM_LINUX )

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) LinuxFileWatcher — inotify wd→경로 맵, eventfd 로 워커를 깨움
    // ------------------------------------------------------------------------------
    /**
     * @class LinuxFileWatcher
     * @brief Linux 플랫폼 특화 FileWatcher (inotify + eventfd)
     */
    class SW_API LinuxFileWatcher final : public IFileWatcher
    {
    public:
        /** @brief fd 와 큐를 비운 상태로 둡니다. */
        LinuxFileWatcher();
        /** @brief 감시를 멈추고 워커를 합류시킵니다. */
        ~LinuxFileWatcher() override;

        /** @brief inotify/eventfd 를 열고 워커를 띄웁니다. */
        bool startWatching( string_view directoryPath, bool bRecursive = true ) override;
        /** @brief 워커 큐에서 이벤트를 꺼내 outListEvent 에 담습니다. */
        uint32 pollEvents( vector<FileChangeEvent>& outListEvent ) override;
        /** @brief eventfd 로 워커를 깨운 뒤 watch 를 모두 뗍니다. */
        void stopWatching() override;
        /** @brief 워커가 돌고 있으면 true입니다. */
        bool isWatching() const override { return _bIsWatching; }

    private:
        /** @brief inotify 이벤트를 읽어 큐에 넣습니다. */
        void workerThreadMain();
        /** @brief 디렉터리와(재귀면) 하위를 inotify 에 등록합니다. */
        bool addWatchRecursive( string_view directoryPath );
        /** @brief 한 디렉터리만 inotify 에 등록하고 wd 맵에 넣습니다. */
        bool addWatchDirectory( string_view directoryPath );
        /** @brief wd 를 해제하고 맵에서 지웁니다. */
        void removeWatch( int32 watchDescriptor );
        /** @brief 뮤텍스 아래에서 변경 이벤트를 큐에 넣습니다. */
        void pushEvent( FileWatcherAction action, string_view absoluteDirectory, string_view name );

    private:
        std::thread             _workerThread;
        mutex                   _eventMutex;
        mutex                   _watchMutex;
        string                  _directoryPath;
        vector<FileChangeEvent> _listEventQueue;
        map<int32, string>      _mapWatchDescriptorToPath;
        int32                   _inotifyFd;
        int32                   _wakeFd;
        atomic<bool>            _bIsWatching;
        bool                    _bRecursive;
    };
} // namespace sw

#endif // SW_PLATFORM_LINUX
