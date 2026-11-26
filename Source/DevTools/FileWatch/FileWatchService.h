/**
 * @file FileWatchService.h
 * @brief 프로세스의 파일 감시 창구 하나입니다 — OS 감시자를 루트마다 하나씩 나눠 쓰고, 프레임에 한 번 쌓인 변경을 모아 접두사 · 확장자가 맞는 구독자에게 나눠 줍니다.
 * @details 감시자를 띄우는 열쇠(`FileWatcherKey`)는 이것만 만든다. 한 감시자는 OS 핸들 · 스레드 하나라, 이미 보는 트리 안의 폴더는 그 감시자를 같이 쓴다.
 *          네이티브 감시를 걸지 못하면 mtime 폴링으로 바꾸지 않는다(이유를 남기고 빈 구독).
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Path/PathKey.h"
#include "Core/Subsystem/Subsystem.h"

#include "DevTools/DevToolsAPI.h"
#include "DevTools/FileWatch/FileWatcher.h"

namespace sw
{
    /** @brief 감시가 보는 깊이입니다. */
    enum class FileWatchDepth : uint8
    {
        Folder, ///< 그 폴더 바로 아래 파일만
        Tree,   ///< 하위 폴더까지
    };

    /**
     * @class FileWatchService
     * @brief 파일 머리말 참고. 호스트 개발 계층(`HostDevLayer`)이 값으로 들고, 개발 도구가 프레임 끝에 `update` 를 한 번 부른다. 게임 스레드에서만 씁니다.
     */
    class SW_DEVTOOLS_API FileWatchService
    {
    public:
        static constexpr SubsystemModuleVisibility kModuleVisibility = SubsystemModuleVisibility::HostModules; ///< 에디터의 에셋 리로드가 찾는다(`HostDevTools` 가 호스트 범위에 드러낸다)

        /** @brief 루트 하나를 볼 감시자를 만드는 함수입니다(없으면 nullptr). 테스트가 OS 없는 감시자를 준다. */
        using CreateWatcherDelegate = Delegate<unique_ptr<FileWatcher>()>;

        /** @brief 이 플랫폼의 OS 감시자를 씁니다. */
        FileWatchService();
        explicit FileWatchService( const CreateWatcherDelegate& createWatcher );
        ~FileWatchService();
        FileWatchService( const FileWatchService& )            = delete;
        FileWatchService& operator=( const FileWatchService& ) = delete;

        /**
         * @brief @p folder 아래에서 확장자가 맞는 변경이 있을 때 @p onMatch 를 부르게 하고 그 구독을 돌려줍니다. 구독이 사라지면 감시가 빠지고, 루트의 마지막 감시가 빠지면 감시자를 내립니다.
         * @param folder 절대 경로입니다(사건의 경로와 그대로 맞춰 본다). 비었으면 경고하고 빈 구독이다(팩만 실린 실행 — 콘텐츠 폴더가 없다).
         * @param listExtension 점을 포함한 접미사(`.prefab.xml` 같은 복합 접미사도). 비면 모든 파일이다. 대소문자를 가리지 않는다.
         * @return OS 감시를 걸지 못하면 빈 구독입니다(이유를 로그에 남긴다).
         */
        [[nodiscard]] Subscription watch( const DiskPath& folder, FileWatchDepth depth, const vector<string>& listExtension, const FileChangeDelegate& onMatch );

        /**
         * @brief 프레임에 한 번 — 모든 감시자의 변경을 꺼내 한 묶음으로 나눠 줍니다. 같은 파일에 같은 동작이 이어지면(사이에 다른 파일이 끼어도) 한 번만 알립니다.
         * @details 콜백 안에서 `watch` 와 구독 해제를 해도 된다.
         */
        void update();

        /** @brief 지금 열린 OS 감시자(루트) 수입니다. */
        [[nodiscard]] size_t getRootCount() const { return _listRoot.size(); }

    private:
        /** @brief OS 감시자 하나와 그것이 보는 루트입니다. */
        struct Root
        {
            uint64                  _id{ 0 };
            string                  _path;
            ComparableDiskPath      _comparablePath;
            FileWatchDepth          _depth{ FileWatchDepth::Tree };
            unique_ptr<FileWatcher> _pWatcher;               ///< 감시를 잃었으면 비어 있다(다시 걸어도 같은 실패라 두지 않는다)
            uint64                  _lastPollTimestamp{ 0 }; ///< 직전 `update` 가 큐를 비운 시각(파일 시계, 초)
        };
        /** @brief 감시 하나입니다. */
        struct WatchEntry
        {
            uint64             _id{ 0 };     ///< 이 감시를 소유하는 구독의 번호
            uint64             _rootID{ 0 }; ///< 사건을 받을 루트 — 루트 둘이 겹쳐도 한 번만 받는다
            DiskPath           _folder;
            ComparableDiskPath _comparableFolder; ///< 등록할 때 한 번 접는다 — 사건마다 다시 만들지 않는다
            FileWatchDepth     _depth{ FileWatchDepth::Tree };
            vector<string>     _listExtension;
            FileChangeDelegate _onMatch;
        };

        /** @brief @p folder 를 이미 보는 루트의 번호입니다. 없으면 감시자를 띄워 새 루트를 만들고, 못 만들면 빈 값입니다. */
        [[nodiscard]] optional<uint64> acquireRoot( const DiskPath& folder, FileWatchDepth depth );
        /** @brief 루트 하나의 변경을 꺼내 @p outListEvent 에 담습니다(리스캔 신호는 실제 변경 파일로 펼친다). */
        void pollRoot( Root& root, vector<FileChangeEvent>& outListEvent );
        /** @brief 파일 이름이 빈 리스캔 신호를 실제 변경 파일 목록으로 펼칩니다 — @p sinceTimestamp(파일 시계, 초) 이후에 바뀐 파일만. */
        void        expandRescanEvents( const Root& root, vector<FileChangeEvent>& inoutListEvent, uint64 sinceTimestamp ) const;
        void        dispatch( uint64 rootID, const vector<FileChangeEvent>& listEvent );
        void        releaseWatch( uint64 id );
        static bool isExtensionAllowed( const WatchEntry& entry, string_view filename );

    private:
        CreateWatcherDelegate _createWatcher;
        vector<Root>          _listRoot;
        vector<WatchEntry>    _listWatch;
        IDAllocator<uint64>   _idAllocator;
        /** @brief 이 객체가 내준 구독입니다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다. */
        SubscriptionList _subscriptions{ SW_DELEGATE_METHOD( SubscriptionList::ReleaseFunction, &FileWatchService::releaseWatch, this ) };
    };
} // namespace sw
