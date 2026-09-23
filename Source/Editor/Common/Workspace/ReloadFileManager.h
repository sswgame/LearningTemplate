/**
 * @file ReloadFileManager.h
 * @brief 실행 중 애셋 파일(텍스처, 셰이더 등)의 변경을 감지해, 등록된 구독자에게만 전달합니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/IFileWatcher.h"

namespace sw
{
    using FileWatchMatchDelegate = FileChangeDelegate;

    /// @brief registerWatch 가 반환하는 감시 핸들입니다.
    struct FileWatchHandle
    {
        uint64 _id{ 0 };
        bool   isValid() const { return _id != 0; }
        bool   operator==( const FileWatchHandle& rhs ) const { return _id == rhs._id; }
        bool   operator!=( const FileWatchHandle& rhs ) const { return _id != rhs._id; }
    };

    /**
     * @class ReloadFileManager
     * @brief FileWatcher 로 리소스 변경을 폴링하고, 등록된 경로 접두사와 확장자가 맞을 때만 콜백을 부릅니다.
     * @note Windows 는 ReadDirectoryChangesW, Linux 는 inotify, 그 밖의 플랫폼은 mtime 폴링으로 대신합니다.
     */
    class ReloadFileManager
    {
    public:
        /** @brief 워치 목록을 비운 채 시작합니다. */
        ReloadFileManager();
        /** @brief 감시자와 등록된 감시를 정리합니다. */
        ~ReloadFileManager();

        /** @brief 초기화합니다. */
        bool initialize();
        /** @brief 종료합니다. */
        void shutdown();

        /**
         * @brief 프레임마다 불러 파일 변경을 폴링합니다.
         */
        void update();

        /**
         * @brief pathPrefix 아래에서 확장자가 맞는 변경이 있을 때만 onMatch 를 부르도록 등록합니다.
         * @param pathPrefix 감시 · 필터 경로(안에서 정규화합니다)
         * @param listExtension 예: { ".hlsl", ".hlsli" } (점 포함, 대소문자 무시)
         */
        FileWatchHandle registerWatch( string_view pathPrefix, const vector<string>& listExtension, const FileWatchMatchDelegate& onMatch );
        /** @brief 감시 등록을 해제합니다. */
        void unregisterWatch( FileWatchHandle handle );

        /**
         * @brief 파일 변경 이벤트를 등록된 감시 콜백으로 보냅니다.
         * @details **공개인 이유는 이것이 이 클래스의 절반이기 때문입니다.** 나머지 절반(폴링)은 OS 알림에 기대므로 검사가
         *          타이밍에 흔들립니다. 이벤트를 직접 넣을 수 있으면 "누구에게 가고 누구에게 안 가는가" 와 "콜백이 목록을
         *          바꿔도 견디는가" 를 파일 시스템 없이 결정적으로 볼 수 있습니다. 도구가 합성 이벤트를 넣는 데도 씁니다.
         * @note 콜백 안에서 `registerWatch`/`unregisterWatch` 를 불러도 됩니다.
         */
        void dispatchEvents( const vector<FileChangeEvent>& listEvent );

    private:
        /// @brief 경로 접두사 · 확장자 필터 · 콜백입니다.
        struct WatchEntry
        {
            FileWatchHandle _handle;
            string          _pathPrefix;
            /**
             * @brief `_pathPrefix` 를 `normalizePath` 한 것입니다. **등록할 때 한 번** 만듭니다.
             * @details `matchesWatch` 가 이벤트마다 이것을 다시 만들고 있었습니다. 접두사는 감시가 등록된 뒤로 바뀌지 않으므로,
             *          이벤트 E 개 · 감시 W 개면 E x W 번의 정규화가 모두 같은 값을 다시 구하는 일이었습니다.
             */
            string                 _normalizedPrefix;
            vector<string>         _listExtension;
            FileWatchMatchDelegate _onMatch;
        };

        /**
         * @brief 이벤트가 이 감시의 접두사 · 확장자와 맞으면 true 입니다.
         * @param normalizedFullPath 이벤트의 전체 경로를 정규화한 것. **부르는 쪽이 이벤트당 한 번** 만들어 넘깁니다(예전에는
         *        이 함수가 감시마다 같은 값을 다시 만들었습니다).
         */
        bool matchesWatch( const WatchEntry& entry, const FileChangeEvent& changeEvent,
                           string_view normalizedFullPath ) const;
        /**
         * @brief 파일 이름이 빈 리스캔 신호를 실제 변경 파일 목록으로 펼칩니다.
         * @param sinceTimestamp 이 시각(파일 시계, 초) 이후 mtime 인 파일만 냅니다(직전 비우기 시각).
         */
        void expandRescanEvents( vector<FileChangeEvent>& outListEvent, uint64 sinceTimestamp );
        /** @brief mtime 폴백으로 변경을 모읍니다. */
        void pollMtimeFallback( vector<FileChangeEvent>& outListEvent );
        /** @brief 파일 확장자가 감시 허용 목록에 있으면 true 입니다. */
        bool isExtensionAllowed( const WatchEntry& entry, string_view filename ) const;

        unique_ptr<IFileWatcher>      _fileWatcher;
        vector<WatchEntry>            _listWatch;
        uint64                        _nextWatchId{ 1 };
        unordered_map<string, uint64> _mapPollMtime;
        uint64                        _lastDrainTimestamp{ 0 }; ///< 직전 update() 가 큐를 비운 시각(파일 시계, 초)
        bool                          _bUseMtimePoll{ false };
    };
} // namespace sw
