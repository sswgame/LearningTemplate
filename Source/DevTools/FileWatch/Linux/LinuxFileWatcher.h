/**
 * @file LinuxFileWatcher.h
 * @brief inotify 와 eventfd 워커로 디렉터리 변경을 모읍니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"
#include "DevTools/FileWatch/IFileWatcher.h"

namespace sw
{
    enum class MemoryTag : uint8;

    // ------------------------------------------------------------------------------
    // 1) LinuxFileWatcher — inotify 의 wd → 경로 맵, eventfd 로 워커를 깨운다
    // ------------------------------------------------------------------------------
    /**
     * @class LinuxFileWatcher
     * @brief Linux 전용 FileWatcher 입니다(inotify + eventfd).
     */
    class SW_DEVTOOLS_API LinuxFileWatcher final : public IFileWatcher
    {
    public:
        /** @brief fd 와 큐가 빈 상태로 만듭니다. */
        LinuxFileWatcher();
        /** @brief 감시를 멈추고 워커 스레드를 조인합니다. */
        ~LinuxFileWatcher() override;

        /** @brief inotify · eventfd 를 열고 워커를 띄웁니다. */
        [[nodiscard]] bool startWatching( string_view directoryPath, bool bRecursive = true ) override;
        /** @brief eventfd 로 워커를 깨운 뒤 모든 watch 를 뗍니다. */
        void stopWatching() override;
        /** @brief 워커가 돌고 있으면 true 입니다. */
        bool isWatching() const override { return _bIsWatching; }

    private:
        /** @brief inotify 이벤트를 읽어 큐에 넣습니다. */
        void workerThreadMain( MemoryTag memoryTag );
        /** @brief inotify fd 에 쌓인 이벤트를 다 읽어 처리합니다. 읽기가 실패하거나 루트를 잃으면 감시를 잃은 것으로 알리고 false 를 반환합니다. */
        [[nodiscard]] bool drainEvents( uint8* pBuffer, uint32 bufferSize );
        /** @brief inotify 이벤트 하나(감시 wd, 마스크, wd 기준 이름)를 변경으로 바꿔 큐에 넣습니다. 루트를 잃으면 알리고 false 를 반환합니다. */
        [[nodiscard]] bool handleWatchEvent( int32 watchDescriptor, uint32 mask, string_view name );
        /** @brief 디렉터리를(재귀면 하위 디렉터리까지) inotify 에 등록합니다. */
        bool addWatchRecursive( string_view directoryPath );
        /** @brief 디렉터리 하나만 inotify 에 등록하고 wd 맵에 넣습니다. */
        bool addWatchDirectory( string_view directoryPath );
        /** @brief wd 를 해제하고 맵에서 지웁니다. */
        void removeWatch( int32 watchDescriptor );
        /** @brief 절대 경로를 감시 루트 기준 상대 경로로 바꿔 큐에 넣습니다. */
        void pushRelativeChange( FileWatcherAction action, string_view absoluteDirectory, string_view name );

    private:
        // 큐 · 뮤텍스 · 감시 경로 · 넘침 표시는 IFileWatcher 가 가진다. 세 플랫폼이 똑같이 가져야 하는 것들이다.
        sw::thread         _workerThread;
        mutex              _watchMutex;
        map<int32, string> _mapWatchDescriptorToPath;
        int32              _inotifyFd;
        int32              _wakeFd;
        atomic<bool>       _bIsWatching;
        bool               _bRecursive;
    };
} // namespace sw
