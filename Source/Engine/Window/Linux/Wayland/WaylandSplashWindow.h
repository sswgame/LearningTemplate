/**
 * @file WaylandSplashWindow.h
 * @brief Linux Wayland 기반 ISplashWindow 구현입니다(xdg-shell 최상위 창 + 공유 메모리 버퍼).
 * @details 게임 창보다 먼저 뜨므로 자기 컴포지터 연결을 따로 연다(`WaylandWindow` 와 연결을 나누지 않는다). 창 자리는 컴포지터가 정한다(Wayland 클라이언트는 자리를 정하지 못한다) —
 *          크기는 최소 · 최대를 같게 두어 고정한다. 이벤트 루프가 없으므로 그릴 때마다 막지 않고 연결을 처리한다(핑 응답 · 버퍼 반납). 글은 `WaylandTextPainter`(시스템 글꼴)로 그리고
 *          글꼴이 없으면 글 없이 그림과 진행 막대만 그린다. `SW_ENABLE_WAYLAND` 로 지은 리눅스 클라이언트에서만 실제로 돈다. 선택은 `ISplashWindow::createPlatformSplash` 가 한다.
 */
#pragma once
#include "Engine/Window/ISplashWindow.h"

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
        unique_ptr<State> _pState; ///< 라이브러리 객체(.cpp 에만 있다)
    };
} // namespace sw
