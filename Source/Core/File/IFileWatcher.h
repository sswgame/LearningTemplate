/**
 * @file IFileWatcher.h
 * @brief 디렉터리 변경 감시 인터페이스. 구현은 Windows/Linux 전용 헤더.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) FileChangeEvent — 워커가 큐에 넣고 pollEvents 가 메인 스레드로 꺼냄
    // ------------------------------------------------------------------------------
    /** @brief 감시 큐에 올라가는 변경 종류입니다. */
    enum class FileWatcherAction : uint8
    {
        Added,
        Removed,
        Modified,
        RenamedOldName,
        RenamedNewName
    };

    /** @brief 한 번의 파일/폴더 변경입니다. */
    struct FileChangeEvent
    {
        FileWatcherAction _action;
        string            _directory;
        string            _filename;
    };

    SW_DECLARE_DELEGATE( void, FileChangeDelegate, const FileChangeEvent& );
    SW_DECLARE_MULTI_CAST_DELEGATE( void, FileChangeMulticastDelegate, const FileChangeEvent& );

    // ------------------------------------------------------------------------------
    // 2) IFileWatcher — startWatching → pollEvents(메인) → stopWatching
    // ------------------------------------------------------------------------------
    /**
     * @class IFileWatcher
     * @brief 디렉토리 및 파일 변경 이벤트를 모니터링하는 크로스플랫폼 인터페이스
     */
    class SW_API IFileWatcher
    {
    public:
        /** @brief 감시를 멈추고 워커를 합류시킵니다. */
        virtual ~IFileWatcher() = default;

        /** @brief 복사를 금지합니다. */
        IFileWatcher( const IFileWatcher& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        IFileWatcher& operator=( const IFileWatcher& ) = delete;

        /**
         * @brief 특정 디렉토리의 모니터링을 시작합니다.
         * @param directoryPath 감시할 디렉토리의 절대/상대 경로
         * @param bRecursive 하위 폴더도 포함하여 감시할지 여부
         * @return 모니터링 시작 성공 여부
         */
        virtual bool startWatching( string_view directoryPath, bool bRecursive = true ) = 0;

        /**
         * @brief 파일 변경 이벤트를 처리하기 위해 큐를 폴링합니다. (메인 스레드의 업데이트 루프에서 호출 권장)
         * @param outListEvent 발생한 파일 변경 이벤트 목록
         * @return 발생한 이벤트 개수
         */
        virtual uint32 pollEvents( vector<FileChangeEvent>& outListEvent ) = 0;

        /**
         * @brief 모니터링을 종료하고 리소스를 해제합니다.
         */
        virtual void stopWatching() = 0;

        /** @brief 워커가 돌고 있으면 true입니다. */
        virtual bool isWatching() const = 0;

    protected:
        /** @brief 인터페이스만 두며 감시는 시작하지 않습니다. */
        IFileWatcher() = default;

        /**
         * @brief 폴링 전까지 쌓아 둘 변경 이벤트의 상한입니다. **구현 셋이 같은 값을 써야 한다.**
         * @details `pollEvents` 를 부르는 쪽이 없거나 밀리는 동안(대량 임포트·브랜치 전환) 큐가 끝없이
         *          자라지 않게 한다. 상한에 걸리면 개별 이벤트 대신 **합성 리스캔 하나**로 접는다 —
         *          `_filename` 이 빈 `Modified` 가 그 약속이고, 받는 쪽은 그것을 "전부 다시 봐라" 로 읽는다.
         *          값이 플랫폼마다 다르면 같은 부하에서 한쪽만 이벤트를 잃는다 — 그 차이는 그 플랫폼에서
         *          돌려 보기 전에는 드러나지 않는다. 이 계층 밖에서는 쓰지 않으므로 `constant` 가 아니라 여기 둔다.
         */
        static constexpr size_t _s_kMaxQueuedEvent = 4096;
    };
} // namespace sw
