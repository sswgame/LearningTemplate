/**
 * @file IFileWatcher.h
 * @brief 디렉터리 변경 감시 인터페이스입니다. 구현은 플랫폼별 헤더(Windows · Linux · Mac)에 있습니다.
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
    // 1) FileChangeEvent — 워커가 큐에 넣고, pollEvents 가 메인 스레드에서 꺼낸다
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

    /** @brief 파일 · 폴더의 변경 하나입니다. */
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
     * @brief 디렉터리와 파일의 변경을 감시하는 크로스 플랫폼 인터페이스입니다.
     */
    class SW_API IFileWatcher
    {
    public:
        /** @brief 가상 소멸자입니다. 감시를 멈추고 워커 스레드를 조인하는 일은 구현 클래스가 합니다. */
        virtual ~IFileWatcher() = default;

        /** @brief 복사를 금지합니다. */
        IFileWatcher( const IFileWatcher& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        IFileWatcher& operator=( const IFileWatcher& ) = delete;

        /**
         * @brief 디렉터리 감시를 시작합니다.
         * @param directoryPath 감시할 디렉터리의 절대 · 상대 경로
         * @param bRecursive 하위 폴더까지 감시할지 여부
         * @return 감시를 시작했으면 true
         */
        virtual bool startWatching( string_view directoryPath, bool bRecursive = true ) = 0;

        /**
         * @brief 큐에 쌓인 파일 변경 이벤트를 꺼냅니다(메인 스레드의 업데이트 루프에서 부르십시오).
         * @param outListEvent 꺼낸 파일 변경 이벤트 목록
         * @return 꺼낸 이벤트 수
         * @details **여기서 한 번만 구현합니다.** 예전에는 세 플랫폼이 이 함수를 글자 하나까지 똑같이 따로 들고 있었습니다.
         *          큐를 비우고, 넘쳤으면 합성 리스캔 하나를 덧붙이고, 넘침 표시를 내리는 일입니다. 같은 코드를 세 벌 두면
         *          하나가 조용히 어긋납니다(실제로 `pushChange` 쪽이 그랬습니다).
         */
        uint32 pollEvents( vector<FileChangeEvent>& outListEvent );

        /**
         * @brief 감시를 끝내고 자원을 해제합니다.
         */
        virtual void stopWatching() = 0;

        /** @brief 워커가 돌고 있으면 true 입니다. */
        virtual bool isWatching() const = 0;

    protected:
        /** @brief 인터페이스만 만들고 감시는 시작하지 않습니다. */
        IFileWatcher() = default;

        /**
         * @brief 변경 하나를 큐에 넣습니다. 플랫폼 워커 스레드에서 부릅니다.
         * @details **상한 · 넘침 표시 · 연속 중복 합치기를 여기서 한 번만 합니다.** 예전에는 세 구현이 각자 적었고, **macOS 만
         *          중복 합치기가 빠져 있었습니다.** 같은 파일에 같은 동작이 잇달아 들어와도 그대로 쌓여서, 같은 부하에서 macOS 만
         *          큐가 먼저 차고 리스캔이 잦았습니다. 그 차이는 macOS 에서 돌려 보기 전에는 드러나지 않습니다(이 저장소는 macOS
         *          를 빌드하지 않습니다). 그래서 플랫폼 코드에서 이 판단을 걷어 내 여기로 모았습니다.
         * @param action 변경 종류
         * @param directory 이벤트의 디렉터리(보통 감시 루트)
         * @param filename 디렉터리 기준 상대 경로. 비어 있으면 "전부 다시 확인하라" 는 뜻입니다
         */
        void pushChange( FileWatcherAction action, string_view directory, string_view filename );

        /**
         * @brief 폴링하기 전까지 쌓아 둘 변경 이벤트의 상한입니다.
         * @details `pollEvents` 를 부르는 쪽이 없거나 밀리는 동안(대량 임포트 · 브랜치 전환) 큐가 끝없이 커지지 않게 합니다.
         *          상한에 걸리면 개별 이벤트 대신 **합성 리스캔 하나**로 합칩니다. `_filename` 이 빈 `Modified` 가 그 약속이고,
         *          받는 쪽은 그것을 "전부 다시 확인하라" 로 읽습니다. `pushChange` · `pollEvents` 가 모두 여기 있으므로 값도
         *          하나뿐입니다.
         */
        static constexpr size_t _s_kMaxQueuedEvent = 4096;

        /** @brief 감시 중인 디렉터리입니다. 합성 리스캔 이벤트에 이 값을 넣습니다. */
        string _directoryPath;

        /** @brief 큐를 보호합니다. 플랫폼 워커가 넣고, 폴링하는 쪽이 꺼냅니다. */
        mutex _eventMutex;

        /** @brief 폴링하기 전까지 쌓이는 변경 목록입니다. */
        vector<FileChangeEvent> _listEventQueue;

        /** @brief 상한에 걸려 개별 이벤트를 버렸다는 표시입니다. 다음 폴링이 합성 리스캔으로 바꿔 전달합니다. */
        bool _bEventQueueOverflowed{ false };
    };
} // namespace sw
