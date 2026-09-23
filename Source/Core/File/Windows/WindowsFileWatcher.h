/**
 * @file WindowsFileWatcher.h
 * @brief ReadDirectoryChangesW 와 IOCP 워커로 디렉터리 변경을 모읍니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/IFileWatcher.h"

#if defined( SW_PLATFORM_WINDOWS )

// Windows.h 전체를 헤더에 넣지 않도록 HANDLE 만 전방 선언한다.
using HANDLE = void*;

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) WindowsFileWatcher — 워커가 IOCP 완료를 큐에 넣고, pollEvents 가 꺼낸다
    // ------------------------------------------------------------------------------
    /**
     * @class WindowsFileWatcher
     * @brief Windows 전용 FileWatcher 입니다(ReadDirectoryChangesW 사용).
     */
    class SW_API WindowsFileWatcher final : public IFileWatcher
    {
    public:
        /** @brief 핸들과 큐가 빈 상태로 만듭니다. */
        WindowsFileWatcher();
        /** @brief 감시를 멈추고 워커 스레드를 조인합니다. */
        virtual ~WindowsFileWatcher() override;

        /** @brief 디렉터리 핸들과 IOCP 를 열고 워커를 띄웁니다. */
        bool startWatching( string_view directoryPath, bool bRecursive = true ) override;
        /** @brief IOCP 로 워커를 깨워 멈춘 뒤 핸들을 닫습니다. */
        void stopWatching() override;
        /** @brief 워커가 돌고 있으면 true 입니다. */
        bool isWatching() const override { return _bIsWatching; }

    private:
        /** @brief ReadDirectoryChangesW 의 완료 결과를 이벤트 큐에 넣습니다. */
        void workerThreadMain();

        // 큐 · 뮤텍스 · 감시 경로 · 넘침 표시는 IFileWatcher 가 가진다. 세 플랫폼이 똑같이 가져야 하는 것들이다.
        HANDLE       _hDirectory;
        HANDLE       _hCompletionPort;
        std::thread  _workerThread;
        atomic<bool> _bIsWatching;
        bool         _bRecursive;
    };

} // namespace sw

#endif // SW_PLATFORM_WINDOWS
