/**
 * @file WebWindow.h
 * @brief 브라우저 캔버스 창입니다(Emscripten). 캔버스가 표면이고(`NativeSurfaceKind::WebCanvas` — 핸들은 CSS 선택자), 입력 · 크기 · 보임은 DOM 콜백으로 옵니다.
 * @details 캔버스 물리 크기 = CSS 크기 × devicePixelRatio 로 맞춘다(흐린 화면을 막는다). 안전 여백은 셸 HTML 이 CSS 변수(`--sw-safe-top` 등 — `env(safe-area-inset-*)`)로 낸다.
 *          탭이 숨으면 앱 멈춤, 보이면 돌아옴이다(`visibilitychange` — 브라우저는 숨은 탭의 프레임 콜백을 멈춘다).
 */
#pragma once
#include "Engine/Window/CallbackInputWindow.h"

#if defined( SW_PLATFORM_WEB )
    #include <emscripten/html5.h>

namespace sw
{
    /** @class WebWindow @brief 캔버스 하나의 창입니다. 한 페이지에 하나다(주 창). */
    class WebWindow final : public CallbackInputWindow
    {
    public:
        /** @brief 셸 HTML 이 두는 캔버스의 선택자입니다(Emscripten 기본 셸과 같다). */
        static constexpr const utf8* kCanvasSelector = "#canvas";

        WebWindow();
        ~WebWindow() override;

        [[nodiscard]] bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;
        void               destroy() override;
        NativeSurfaceKind  getSurfaceKind() const override { return NativeSurfaceKind::WebCanvas; }

    protected:
        void* getNativeHandle() const override { return const_cast<utf8*>( kCanvasSelector ); }
        void  applyTitle() override;

    private:
        /** @brief 캔버스의 CSS 크기 · 화면 배율 · 안전 여백을 다시 읽어 캔버스 물리 크기를 맞춥니다(창 크기 · 회전 · 배율 변경). */
        void syncCanvasSize();
        /** @brief DOM 콜백을 겁니다(마우스 · 바퀴 · 키 · 손가락 · 크기 · 보임 · 포커스). */
        void bindCallbacks();
        /** @brief DOM 콜백을 풉니다. */
        void unbindCallbacks();

        // DOM 콜백(userData 가 이 창이다). 반환값 EM_TRUE 는 브라우저 기본 동작(스크롤 · 확대 · 탭 이동)을 막는다.
        static EM_BOOL onMouse( int eventType, const EmscriptenMouseEvent* pEvent, void* pUserData );
        static EM_BOOL onWheel( int eventType, const EmscriptenWheelEvent* pEvent, void* pUserData );
        static EM_BOOL onKey( int eventType, const EmscriptenKeyboardEvent* pEvent, void* pUserData );
        static EM_BOOL onTouch( int eventType, const EmscriptenTouchEvent* pEvent, void* pUserData );
        static EM_BOOL onResize( int eventType, const EmscriptenUiEvent* pEvent, void* pUserData );
        static EM_BOOL onVisibility( int eventType, const EmscriptenVisibilityChangeEvent* pEvent, void* pUserData );
        static EM_BOOL onFocus( int eventType, const EmscriptenFocusEvent* pEvent, void* pUserData );

    private:
        float32                _devicePixelRatio;
        uint8                  _bCallbacksBound : 1;
        [[maybe_unused]] uint8 _reserved        : 7;
    };
} // namespace sw

#endif
