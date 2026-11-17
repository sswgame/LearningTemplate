/**
 * @file LinuxSplashThread.h
 * @brief 리눅스 스플래시(X11 · Wayland)가 창과 이벤트 루프를 맡기는 전용 스레드입니다. 기동 스레드는 상태만 적고 깨우며, 창 시스템 객체는 모두 이 스레드 안에서만 산다.
 * @details 예전에는 기동 스레드가 `updateStatus` 안에서만 연결을 처리해, 단계 하나가 길면 Wayland 컴포지터의 핑(`xdg_wm_base.ping`)에 답하지 못해 창이 "응답 없음" 이 됐다
 *          (Win32 스플래시와 같은 결함 — 감사 P-13). 언리얼 `FLinuxSplash` 도 스플래시를 자기 스레드에서 돌린다(기억).
 *          깨우기는 `eventfd` 하나다 — 스레드는 창 시스템 연결의 fd 와 이것을 함께 `poll` 하고, 기동 스레드는 그리기 · 끝내기 요청을 적은 뒤 eventfd 에 쓴다.
 *          창 시스템 객체를 스레드 사이에 나누지 않으므로(연결을 스레드 안에서 열고 닫는다) Xlib · libwayland 의 스레드 규칙은 "연결 하나를 한 스레드가 쓴다" 하나로 지켜진다.
 */
#pragma once
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include <condition_variable>
#include <functional>

namespace sw
{
    /** @brief `LinuxSplashThread::waitForEvents` 가 깬 까닭(비트)입니다. */
    enum class LinuxSplashWakeReason : uint8
    {
        None               = 0,
        ConnectionReadable = 1u << 0, ///< 창 시스템 연결에 읽을 것이 있다
        ConnectionLost     = 1u << 1, ///< 연결이 끊겼다(POLLHUP · POLLERR) — 스레드는 끝낸다
        Redraw             = 1u << 2, ///< 기동 스레드가 새 상태를 적었다(`PaintState`)
        Quit               = 1u << 3, ///< 기동 스레드가 끝내라고 했다(`stop`)
    };

    /** @class LinuxSplashThread @brief 파일 머리말 참고. */
    class LinuxSplashThread
    {
    public:
        /** @brief 스레드가 그릴 상태입니다(기동 스레드가 적은 마지막 값의 사본). */
        struct PaintState
        {
            string  _status;
            float32 _progress{ 0.0f };
        };
        /** @brief 스레드 본문입니다. 창을 만들고 `markReady` · `markFailed` 중 하나를 부른 뒤 `waitForEvents` 로 돈다. 돌아오기 전에 창 시스템 객체를 모두 놓는다. */
        using RunFunction = std::function<void( LinuxSplashThread& )>;

        LinuxSplashThread();
        /** @brief 아직 돌면 `stop` 합니다. */
        ~LinuxSplashThread();
        LinuxSplashThread( const LinuxSplashThread& )            = delete;
        LinuxSplashThread& operator=( const LinuxSplashThread& ) = delete;

        // ---- 기동 스레드 ----
        /**
         * @brief @p initialState 로 스레드를 띄우고 창이 섰는지(`markReady`) · 못 섰는지(`markFailed`) 기다립니다.
         * @return 창이 섰으면 true 입니다. eventfd 를 만들지 못하면 오류를 남기고 false 입니다(스레드를 띄우지 않는다).
         */
        [[nodiscard]] bool start( RunFunction run, const PaintState& initialState );
        /** @brief 새 상태를 적고 스레드를 깨웁니다. 스레드가 없으면 아무것도 하지 않는다. */
        void requestRedraw( const PaintState& state );
        /** @brief 끝내라고 하고 깨운 뒤 합류합니다. 두 번 불러도 된다. */
        void stop();

        // ---- 스플래시 스레드 ----
        /** @brief 창을 만들었다. `start` 가 true 로 돌아온다. */
        void markReady();
        /** @brief 창을 만들지 못했다. `start` 가 false 로 돌아온다 — 본문은 곧 돌아와야 한다. */
        void markFailed();
        /**
         * @brief @p connectionFD 가 읽을 것이 생기거나 기동 스레드가 깨울 때까지 기다립니다. 깨운 까닭(`LinuxSplashWakeReason` 비트)을 돌려준다.
         * @param outState `Redraw` 비트면 그릴 상태의 사본입니다.
         */
        [[nodiscard]] uint8 waitForEvents( int32 connectionFD, PaintState& outState );

    private:
        void run( RunFunction function );

    private:
        mutex                       _mutex;
        std::condition_variable_any _readyCondition;
        PaintState _paintState      SW_GUARDED_BY( _mutex );
        uint8 _requestMask          SW_GUARDED_BY( _mutex ){ 0 }; ///< 기동 스레드가 청한 것(`LinuxSplashWakeReason::Redraw` · `Quit`)
        uint8 _startMask            SW_GUARDED_BY( _mutex ){ 0 }; ///< 1 = 창이 섰다, 2 = 못 섰다
        int32                       _wakeFD;                      ///< eventfd(기동 스레드가 쓰고 스플래시 스레드가 읽는다). 없으면 -1
        thread                      _thread;                      ///< 마지막에 둔다 — 다른 칸이 다 선 뒤에 시작하고, 먼저 합류한다
    };
} // namespace sw
