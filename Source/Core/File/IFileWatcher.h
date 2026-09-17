/**
 * @file IFileWatcher.h
 * @brief 디렉터리 변경 감시 인터페이스. 구현은 Windows/Linux 전용 헤더.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
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
         * @details **여기서 한 번만 구현한다.** 예전에는 세 플랫폼이 이 함수를 글자까지 똑같이 각자
         *          들고 있었다 — 큐를 비우고, 넘쳤으면 합성 리스캔 하나를 덧붙이고, 표시를 내린다.
         *          같은 코드를 세 벌 두면 하나가 조용히 어긋난다(실제로 `pushChange` 쪽이 그랬다).
         */
        uint32 pollEvents( vector<FileChangeEvent>& outListEvent );

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
         * @brief 변경 하나를 큐에 넣습니다. 플랫폼 워커 스레드에서 부릅니다.
         * @details **상한·오버플로 표시·연속 중복 접기를 여기서 한 번만 한다.** 예전에는 세 구현이
         *          각자 적었고 **macOS 만 중복 접기가 빠져 있었다** — 같은 파일에 같은 동작이 잇달아
         *          들어와도 그대로 쌓여, 같은 부하에서 macOS 만 큐가 먼저 차고 리스캔이 잦아졌다.
         *          그 차이는 macOS 에서 돌려 보기 전에는 드러나지 않는다(이 저장소는 macOS 를 빌드하지
         *          않는다). 그래서 플랫폼 코드에서 이 판단을 걷어내고 여기로 모았다.
         * @param action 변경 종류
         * @param directory 이벤트의 디렉터리 (보통 감시 루트)
         * @param filename 디렉터리 기준 상대 경로. 비어 있으면 "전부 다시 봐라" 를 뜻한다
         */
        void pushChange( FileWatcherAction action, string_view directory, string_view filename );

        /**
         * @brief 폴링 전까지 쌓아 둘 변경 이벤트의 상한입니다.
         * @details `pollEvents` 를 부르는 쪽이 없거나 밀리는 동안(대량 임포트·브랜치 전환) 큐가 끝없이
         *          자라지 않게 한다. 상한에 걸리면 개별 이벤트 대신 **합성 리스캔 하나**로 접는다 —
         *          `_filename` 이 빈 `Modified` 가 그 약속이고, 받는 쪽은 그것을 "전부 다시 봐라" 로 읽는다.
         *          `pushChange` · `pollEvents` 가 여기 있으므로 값도 하나뿐이다.
         */
        static constexpr size_t _s_kMaxQueuedEvent = 4096;

        /** @brief 감시 중인 디렉터리. 합성 리스캔 이벤트가 이 값을 단다. */
        string _directoryPath;

        /** @brief 큐를 지킵니다 — 플랫폼 워커가 넣고 폴링하는 쪽이 뺍니다. */
        mutex _eventMutex;

        /** @brief 폴링 전까지 쌓이는 변경 목록. */
        vector<FileChangeEvent> _listEventQueue;

        /** @brief 상한에 걸려 개별 이벤트를 버렸다는 표시. 다음 폴링이 합성 리스캔으로 바꿔 전달한다. */
        bool _bEventQueueOverflowed{ false };
    };
} // namespace sw
