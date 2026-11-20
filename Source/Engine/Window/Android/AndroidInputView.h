/**
 * @file AndroidInputView.h
 * @brief Java 입력 뷰(`SWInputView` — `Window/Android/Java`)의 C++ 짝입니다. 입력기의 `InputConnection` 호출을 받아 조합 · 확정 글과 지우기 키로 옮기고 게임 스레드로 넘깁니다.
 * @details 설계: DesignBacklog "Android IME 조합"(감사 P-4). 호출은 Java UI 스레드에서 JNI 로 오고(뷰의 잠금 안 — 하나씩), 옮기는 규칙은 `AndroidInputConnectionState` 다.
 *          나온 동작은 잠금 안 목록에 모으고 파이프에 한 바이트를 써서 게임 스레드의 ALooper 를 깨운다 — 창은 그 ALooper 를 비울 때 목록을 받아
 *          `CallbackInputWindow::queueTextInput` · 키 입력으로 낸다(창 스레드 계약 그대로). 언리얼 GameActivity 의 가상 키보드 입력 뷰 · Godot `GodotEditText` 자리다.
 *          Java 클래스는 앱의 dex 에 있어 액티비티의 클래스 로더로 찾는다(붙인 네이티브 스레드의 `FindClass` 는 시스템 로더라 찾지 못한다).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Window/AndroidInputConnectionState.h"

#include <android_native_app_glue.h>
#include <jni.h>

namespace sw
{
    /** @class AndroidInputView @brief Java 입력 뷰 하나와 그 파이프입니다. 창(`AndroidWindow`)이 갖는다. */
    class AndroidInputView final
    {
    public:
        /** @brief Java 클래스의 이름(점 구분 — `ClassLoader.loadClass`)입니다. 소스는 `Window/Android/Java/com/swengine/engine/SWInputView.java`. */
        static constexpr const utf8* kJavaClassName = "com.swengine.engine.SWInputView";

        AndroidInputView();
        /** @brief 붙어 있으면 뗀다(`detach`). */
        ~AndroidInputView();
        AndroidInputView( const AndroidInputView& )            = delete;
        AndroidInputView& operator=( const AndroidInputView& ) = delete;

        /**
         * @brief 파이프를 게임 스레드 ALooper(`pApp->looper`)에 걸고, Java 클래스를 찾아 네이티브 메서드를 잇고, 뷰를 내용 뷰 위에 붙입니다.
         * @param onReady 파이프가 깨면 ALooper 를 비우는 쪽이 부를 함수(`android_poll_source::process`)입니다 — 그 안에서 `takePendingActions` 를 부른다.
         * @return 하나라도 못 하면 오류 로그와 false 입니다(APK 에 dex 가 없으면 클래스를 못 찾는다 — `android-apk` 의 dex 단계).
         */
        [[nodiscard]] bool attach( android_app* pApp, void ( *onReady )( android_app*, android_poll_source* ) );
        /** @brief Java 뷰를 떼고(돌아온 뒤 JNI 가 이 객체를 부르지 않는다) 파이프를 닫습니다. 여러 번 불러도 됩니다. */
        void detach();
        /** @brief 화면 키보드를 올리거나 내립니다(Java 뷰가 UI 스레드에서 포커스 + `InputMethodManager.showSoftInput`). 어느 스레드에서 불러도 된다. */
        void setSoftInputVisible( bool bVisible );
        /** @brief 게임 스레드: 파이프를 비우고 모인 동작을 @p outListAction 뒤에 옮깁니다. */
        void takePendingActions( vector<AndroidTextAction>& outListAction );

        /** @name JNI 가 부르는 자리(Java UI 스레드, 뷰의 잠금 안 — 한 번에 하나) */
        ///@{
        void onSetComposingText( string_view text );
        void onFinishComposingText();
        void onCommitText( string_view text );
        void onDeleteSurroundingText( uint32 beforeLength, uint32 afterLength );
        void onCancelComposition();
        ///@}

    private:
        /** @brief 옮긴 동작을 목록에 더하고 게임 스레드를 깨웁니다. */
        void publish( vector<AndroidTextAction>& listAction );

    private:
        android_app*                                 _pApp;
        jobject                                      _view; ///< Java `SWInputView` 의 전역 참조(없으면 nullptr)
        int32                                        _readFd;
        int32                                        _writeFd;
        android_poll_source                          _pollSource;
        AndroidInputConnectionState                  _state; ///< JNI 쪽(UI 스레드)만 만진다
        mutex                                        _mutex;
        vector<AndroidTextAction> _listPendingAction SW_GUARDED_BY( _mutex );
    };
} // namespace sw
