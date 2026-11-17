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
        /** @brief 캔버스는 Pointer Lock API 로 포인터를 잠근다(https://developer.mozilla.org/docs/Web/API/Pointer_Lock_API). 가두기(Confined)는 브라우저에 없다. */
        [[nodiscard]] bool supportsPointerLock() const override { return true; }
        /**
         * @brief 포인터를 잠그거나 풉니다. 잠금은 사용자 입력 처리 안에서만 되므로 다음 입력 콜백까지 미뤄 청한다(`emscripten_request_pointerlock` 의 defer).
         * @details 브라우저는 Esc 로 언제든 잠금을 푼다 — 풀리면 `pointerlockchange` 가 알리고, 입력 관리자는 다음 클릭에서 다시 청한다(데스크톱 창과 같은 규칙).
         */
        [[nodiscard]] bool setPointerLock( WindowPointerLock lock ) override;
        /**
         * @brief 메인 스레드를 재우지 않고 다음 프레임 콜백을 마감 간격 뒤로 미룹니다(`emscripten_set_main_loop_timing` — 간격이 있으면 setTimeout, 없으면 requestAnimationFrame).
         * @details Emscripten 의 메인 스레드 잠은 바쁜 대기라 배경 상한이 브라우저 메인 스레드를 막았다(감사 P-5). setTimeout 간격은 프레임 시작부터 잰다(Emscripten 이 지난 시간을 뺀다).
         *          https://emscripten.org/docs/api_reference/emscripten.h.html#c.emscripten_set_main_loop_timing
         */
        void waitForNextFrame( int64 frameStartNanoseconds, int64 deadlineNanoseconds ) override;

    protected:
        void* getNativeHandle() const override { return const_cast<utf8*>( kCanvasSelector ); }
        void  applyTitle() override;
        /** @brief 숨은 글 칸(WebTextInput.js)에 포커스를 주거나 뺍니다 — 입력기 · 화면 키보드가 그 칸에 붙는다. */
        void applyTextInputWanted( bool bWanted ) override;
        /** @brief 숨은 글 칸을 글 커서 자리(CSS 픽셀)로 옮깁니다 — 입력기 후보 창이 그 옆에 뜬다. */
        void applyTextInputArea( const WindowTextInputArea& area ) override;

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
        static EM_BOOL onPointerLockChange( int eventType, const EmscriptenPointerlockChangeEvent* pEvent, void* pUserData );
        /** @brief 숨은 글 칸이 낸 조합(@p kind 0) · 확정(1) 글입니다(WebTextInput.js 가 부른다). */
        static void onTextInput( void* pUserData, int kind, const char* pText );

    private:
        float32                _devicePixelRatio;
        int32                  _mainLoopIntervalMilliseconds; ///< 지금 건 프레임 콜백 간격(0 이면 requestAnimationFrame — `emscripten_set_main_loop_arg` 의 fps 0 과 같다)
        uint8                  _bCallbacksBound : 1;
        uint8                  _bPointerLocked  : 1; ///< 브라우저가 캔버스에 포인터를 잠갔다(`pointerlockchange` 가 정한다 — 요청이 아니라 결과)
        [[maybe_unused]] uint8 _reserved        : 6;
    };
} // namespace sw

#endif
