/**
 * @file AndroidWindow.h
 * @brief Android NativeActivity 의 창입니다. 표면은 `ANativeWindow` 이고(`NativeSurfaceKind::AndroidWindow`), 입력 · 표면 · 수명은 `android_app` 명령으로 옵니다.
 * @details 메시지는 `processMessages` 가 ALooper 를 비우며 받는다(`android_native_app_glue` — NDK 소스). 표면이 사라지는 `APP_CMD_TERM_WINDOW` 는 돌아오기 전에
 *          스왑체인을 놓아야 하므로 수명 통보(`SurfaceLost`)를 그 자리에서 낸다. 회전 · 크기 변경은 `APP_CMD_WINDOW_RESIZED` · `CONFIG_CHANGED` 가 다시 읽는다.
 *          안전 여백(노치 · 시스템 바)은 NDK 에 API 가 없어 JNI 로 `View.getRootWindowInsets()` 를 읽는다.
 */
#pragma once
#include "Engine/Window/CallbackInputWindow.h"

#if defined( SW_PLATFORM_ANDROID )

struct android_app;
struct AInputEvent;

namespace sw
{
    /** @class AndroidWindow @brief 액티비티 하나의 창입니다(주 창). */
    class SW_API AndroidWindow final : public CallbackInputWindow
    {
    public:
        AndroidWindow();
        ~AndroidWindow() override;

        /** @brief `android_main` 이 받은 앱을 적습니다. 창보다 먼저 부른다(App/Android/AndroidMain.cpp). */
        static void setApplication( android_app* pApp );
        /** @brief 적힌 앱입니다. 없으면 nullptr 입니다. */
        static android_app* getApplication();
        /**
         * @brief 표면이 생길 때까지 메시지를 돕니다(액티비티가 막 떴을 때 — RHI 를 세우기 전에 창이 있어야 한다). 끝내라고 하면 false 입니다.
         * @details 앱 명령을 잠깐 받아 `APP_CMD_INIT_WINDOW` 를 기다린다 — 창 객체가 서기 전이라 이 정적 함수가 따로 돈다.
         */
        [[nodiscard]] static bool waitForInitialSurface();

        [[nodiscard]] bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;
        void               destroy() override;
        bool               processMessages() override;
        NativeSurfaceKind  getSurfaceKind() const override { return NativeSurfaceKind::AndroidWindow; }

    protected:
        void* getNativeHandle() const override;

    private:
        /** @brief 지금 표면 크기 · density · 안전 여백을 읽어 크기 콜백까지 맞춥니다(회전 · 크기 변경 · 표면 복귀). */
        void syncSurfaceMetrics();
        /** @brief JNI 로 창 안전 여백(왼쪽, 위, 오른쪽, 아래, 물리 픽셀)을 읽습니다. 못 읽으면 0 입니다. */
        float4 readSafeInsets() const;

        static void  onAppCommand( android_app* pApp, int32 command );
        static int32 onInputEvent( android_app* pApp, AInputEvent* pEvent );
    };
} // namespace sw

#endif
