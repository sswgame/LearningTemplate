/**
 * @file X11SplashWindow.h
 * @brief Linux X11 기반 ISplashWindow 구현입니다.
 */
#pragma once
#include "Core/Container/string.h"

#include "Engine/Window/ISplashWindow.h"
#include "Engine/Window/Linux/LinuxSplashThread.h"

namespace sw
{
    /**
     * @class X11SplashWindow
     * @brief Linux X11 기반의 가벼운 스플래시 창입니다. 연결 · 창 · 이벤트 루프는 스플래시 스레드(`LinuxSplashThread`)가 갖는다.
     * @details 자기 X 연결을 스레드 안에서 열고 닫는다 — 주 창(`X11Window`)과 연결을 나누지 않는다. 다른 스레드가 다른 연결을 동시에 쓰므로
     *          Xlib 의 스레드 초기화(`XInitThreads`)를 첫 Xlib 호출 전에 한다(libX11 1.8 부터는 라이브러리가 스스로 한다 — 그 전 판을 위한 것이다).
     */
    class X11SplashWindow : public ISplashWindow
    {
    public:
        X11SplashWindow();
        virtual ~X11SplashWindow() override;

        [[nodiscard]] bool initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height ) override;
        void               updateStatus( const utf8* pStatus, float32 progress = -1.0f ) override;
        void               setProgress( float32 progress ) override;
        void               dismiss() override;

    private:
        /** @brief 스플래시 스레드 본문입니다 — 연결 · 창을 만들고, 끝내라고 할 때까지 이벤트를 비우며 상태가 오면 다시 그린다. */
        void runEventLoop( LinuxSplashThread& splashThread, const string& title );
        /** @brief 상태 · 진행률을 배경 픽스맵에 그려 창 배경으로 겁니다(스플래시 스레드). 픽스맵이 없으면 만든다. */
        void drawStatus( void* pX11Display, uint64 x11Window, uint64& inoutX11Pixmap, const LinuxSplashThread::PaintState& state ) const;

    private:
        LinuxSplashThread _splashThread;
    };
} // namespace sw
