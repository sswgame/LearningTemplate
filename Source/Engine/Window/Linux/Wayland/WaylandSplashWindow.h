/**
 * @file WaylandSplashWindow.h
 * @brief Linux Wayland 기반 ISplashWindow 구현입니다(xdg-shell 최상위 창 + 공유 메모리 버퍼).
 * @details 게임 창보다 먼저 뜨므로 자기 컴포지터 연결을 따로 연다(`WaylandWindow` 와 연결을 나누지 않는다). 창 자리는 컴포지터가 정한다(Wayland 클라이언트는 자리를 정하지 못한다) —
 *          크기는 최소 · 최대를 같게 두어 고정한다. 연결 · 창 · 이벤트 루프는 스플래시 스레드(`LinuxSplashThread`)가 갖는다 — 기동 스레드가 오래 막혀도 핑에 답하고
 *          버퍼 반납을 받는다(예전에는 `updateStatus` 안에서만 연결을 처리해 긴 단계마다 "응답 없음" 이 됐다, 감사 P-13). 글은 `WaylandTextPainter`(시스템 글꼴)로 그리고
 *          글꼴이 없으면 글 없이 그림과 진행 막대만 그린다. `SW_ENABLE_WAYLAND` 로 지은 리눅스 클라이언트에서만 실제로 돈다. 선택은 `ISplashWindow::createPlatformSplash` 가 한다.
 */
#pragma once
#include "Core/Container/string.h"

#include "Engine/Window/ISplashWindow.h"
#include "Engine/Window/Linux/LinuxSplashThread.h"

namespace sw
{
    /** @class WaylandSplashWindow @brief 파일 머리말 참고. */
    class WaylandSplashWindow : public ISplashWindow
    {
    public:
        WaylandSplashWindow();
        ~WaylandSplashWindow() override;

        [[nodiscard]] bool initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height ) override;
        void               updateStatus( const utf8* pStatus, float32 progress = -1.0f ) override;
        void               setProgress( float32 progress ) override;
        void               dismiss() override;

        /** @brief 라이브러리 객체 · 리스너가 쌓은 것입니다(.cpp 에만 정의가 있다). 리스너 묶음(.cpp 의 익명 구조체)이 이름을 쓰도록 공개 선언만 둔다. */
        struct State;

    private:
        /** @brief 스플래시 스레드 본문입니다 — 연결 · 창을 만들고, 끝내라고 할 때까지 연결 이벤트를 처리하며 상태가 오면 다시 그린다. */
        void runEventLoop( LinuxSplashThread& splashThread, const string& title );
        /** @brief 상태 · 진행률을 버퍼에 그려 붙입니다(스플래시 스레드). 버퍼가 모두 컴포지터에 있으면 false 입니다(반납 뒤 다시). */
        [[nodiscard]] bool drawStatus( const LinuxSplashThread::PaintState& paintState );
        /** @brief 버퍼 · 표면 · 전역 객체 · 연결을 만든 순서의 반대로 놓습니다(스플래시 스레드). */
        void releaseConnection();

    private:
        unique_ptr<State> _pState;       ///< 라이브러리 객체(.cpp 에만 있다). 스플래시 스레드만 만진다
        LinuxSplashThread _splashThread; ///< `_pState` 뒤에 둔다 — 먼저 부서지며 스레드를 끝낸다
    };
} // namespace sw
