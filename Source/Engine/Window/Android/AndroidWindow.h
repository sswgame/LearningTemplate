/**
 * @file AndroidWindow.h
 * @brief Android NativeActivity 의 창입니다. 표면은 `ANativeWindow` 이고(`NativeSurfaceKind::AndroidWindow`), 입력 · 표면 · 수명은 `android_app` 명령으로 옵니다.
 * @details 메시지는 `processMessages` 가 ALooper 를 비우며 받는다(`android_native_app_glue` — NDK 소스). 표면이 사라지는 `APP_CMD_TERM_WINDOW` 는 돌아오기 전에
 *          스왑체인을 놓아야 하므로 수명 통보(`SurfaceLost`)를 그 자리에서 낸다. 회전 · 크기 변경은 `APP_CMD_WINDOW_RESIZED` · `CONFIG_CHANGED` 가 다시 읽는다.
 *          안전 여백(노치 · 시스템 바)은 NDK 에 API 가 없어 JNI 로 `View.getRootWindowInsets()` 를 읽는다.
 *          패드: 패드 · 조이스틱 입력 장치의 키 · 축 이벤트를 패드 칸(장치 번호마다 하나)으로 실어 보낸다. 연결 · 끊김은 NDK 에 알림이 없어(자바의
 *          `InputManager.InputDeviceListener` 뿐) 주기적으로 `InputDevice.getDeviceIds()` 를 JNI 로 읽어 칸과 대조한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"

#include "Engine/Window/CallbackInputWindow.h"
#include "Engine/Window/PlatformInputEvent.h"

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
        /**
         * @brief 액티비티를 연 인텐트의 명령줄(문자열 추가값 `kIntentArgumentsExtra`)입니다. 없으면 빈 글입니다(오류가 아니다 — 명령줄 없이 연 것이다).
         * @details `adb shell am start -n <패키지>/android.app.NativeActivity --es args "-scenario empty/noidlewait"` — 데스크톱 명령줄과 같은 글이고
         *          `AndroidMain` 이 따옴표 규칙으로 나눈다(`StringUtil::splitQuotedTokens`). 언리얼 Android 도 인텐트 추가값으로 명령줄을 받는다(기억).
         */
        [[nodiscard]] static string readIntentArguments();
        /** @brief 인텐트 추가값의 이름입니다. */
        static constexpr const utf8* kIntentArgumentsExtra = "args";

        [[nodiscard]] bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;
        void               destroy() override;
        bool               processMessages() override;
        /**
         * @brief 앱 명령 · 입력이 오거나 @p timeoutMilliseconds 가 지날 때까지 ALooper 에서 기다립니다(`ALooper_pollOnce`). 받은 입력은 모아 두고 다음 `processMessages` 가 넘긴다.
         * @details 콜백 창의 기본(바로 돌아옴)이면 멈춘 앱(표면 없음 · onPause)이 잠과 깨기를 되풀이하며 명령을 늦게 받았다(감사 P-5) — 이제 명령이 오면 곧 깬다.
         */
        void              waitForMessages( uint32 timeoutMilliseconds ) override;
        NativeSurfaceKind getSurfaceKind() const override { return NativeSurfaceKind::AndroidWindow; }
        /** @brief 묶지 않는다 — 손가락 입력에는 묶을 포인터가 없다(마우스 캡처 `View.requestPointerCapture` 는 아직 잇지 않았다). 풀기는 늘 된다. */
        [[nodiscard]] bool supportsPointerLock() const override { return false; }
        [[nodiscard]] bool setPointerLock( WindowPointerLock lock ) override { return lock == WindowPointerLock::Released; }

    protected:
        void* getNativeHandle() const override;
        /**
         * @brief 화면 키보드를 올리거나 내립니다(`ANativeActivity_showSoftInput` · `hideSoftInput` — 어느 스레드에서 불러도 된다고 NDK 헤더가 적는다).
         * @details NativeActivity 의 입력 연결은 키 이벤트만 보낸다 — 글자는 키의 유니코드(`queueKeyText`)로 받고, 입력기 조합(한글 · 일본어)은 오지 않는다.
         *          조합까지 받으려면 GameActivity + GameTextInput 으로 옮겨야 한다(유니티 · 언리얼 Android 는 자바 쪽 입력 연결을 둔다).
         */
        void applyTextInputWanted( bool bWanted ) override;
        /** @brief 받지 않는다 — NativeActivity 에는 글 커서 자리를 입력기에 알리는 API 가 없다(GameTextInput 의 `setImeInsets` 자리). */
        void applyTextInputArea( const WindowTextInputArea& area ) override { (void)area; }

    private:
        /** @brief 지금 표면 크기 · density · 안전 여백을 읽어 크기 콜백까지 맞춥니다(회전 · 크기 변경 · 표면 복귀). */
        void syncSurfaceMetrics();
        /** @brief JNI 로 창 안전 여백(왼쪽, 위, 오른쪽, 아래, 물리 픽셀)을 읽습니다. 못 읽으면 0 입니다. */
        float4 readSafeInsets() const;
        /** @brief 입력 장치 @p deviceId 의 패드 칸입니다. 처음 보는 장치면 빈 칸을 주고 연결을 알린다. 칸이 모두 찼으면 비어 있습니다. */
        [[nodiscard]] optional<uint32> findOrAssignGamepadSlot( int32 deviceId );
        /** @brief 지금 붙은 패드 장치를 JNI 로 읽어 칸과 대조합니다 — 사라진 장치는 끊김, 새 장치는 연결을 알린다(`processMessages` 가 주기적으로). */
        void syncGamepadDevices();
        /** @brief 패드 장치의 키 이벤트를 칸에 실어 보냅니다. 패드 장치가 아니면 false 입니다(키보드 길로 간다). */
        [[nodiscard]] bool queueGamepadKey( AInputEvent* pEvent );
        /** @brief 패드 · 조이스틱 장치의 축 이벤트를 칸에 실어 보냅니다. 그런 장치가 아니면 false 입니다(손가락 길로 간다). */
        [[nodiscard]] bool queueGamepadMotion( AInputEvent* pEvent );
        /** @brief 글을 원하는 동안 키 누름이 낸 글자(JNI `KeyEvent.getUnicodeChar`)를 확정 글로 모읍니다. NDK 에는 키의 유니코드를 읽는 함수가 없다. */
        void queueKeyText( AInputEvent* pEvent );

        static void  onAppCommand( android_app* pApp, int32 command );
        static int32 onInputEvent( android_app* pApp, AInputEvent* pEvent );

    private:
        int32  _arrGamepadDeviceID[kPlatformGamepadSlotCount]; ///< 칸마다 붙은 입력 장치 번호(-1 이면 빈 칸)
        uint32 _gamepadScanCountdown;                          ///< 다음 장치 대조까지 남은 메시지 처리 수
    };
} // namespace sw

#endif
