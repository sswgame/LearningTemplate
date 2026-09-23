/**
 * @file MacFileWatcher.h
 * @brief FSEvents 워커로 디렉터리 변경을 모읍니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/IFileWatcher.h"

#if defined( SW_PLATFORM_MACOS )

namespace sw
{
    /**
     * @class MacFileWatcher
     * @brief macOS FSEvents 기반 파일 감시입니다.
     */
    class SW_API MacFileWatcher final : public IFileWatcher
    {
    public:
        /** @brief 스트림과 큐가 빈 상태로 만듭니다. */
        MacFileWatcher();
        /** @brief 감시를 멈추고 워커 스레드를 조인합니다. */
        ~MacFileWatcher() override;

        /** @brief FSEventStream 을 열고 런 루프 워커를 띄웁니다. */
        bool startWatching( string_view directoryPath, bool bRecursive = true ) override;
        /** @brief 런 루프를 멈추고 스트림을 해제합니다. */
        void stopWatching() override;
        /** @brief 워커가 돌고 있으면 true 입니다. */
        bool isWatching() const override { return _bIsWatching; }

    private:
        /** @brief CFRunLoop 에서 FSEvents 를 돌립니다. */
        void workerThreadMain();
        /** @brief FSEvents 콜백이 받은 경로를 큐에 넣습니다. */
        void        handlePaths( size_t numEvents, void* pEventPaths, const uint32* pFlags );
        static void streamCallback( const void* pStreamRef, void* pClientCallBackInfo, size_t numEvents, void* pEventPaths,
                                    const uint32* pEventFlags, const uint64* pEventIds );

    private:
        // 큐 · 뮤텍스 · 감시 경로 · 넘침 표시는 IFileWatcher 가 가진다. 세 플랫폼이 똑같이 가져야 하는 것들이다.
        void*        _pStream;  ///< FSEventStreamRef
        void*        _pRunLoop; ///< CFRunLoopRef
        std::thread  _workerThread;
        atomic<bool> _bIsWatching;
        bool         _bRecursive;
    };
} // namespace sw

#endif
