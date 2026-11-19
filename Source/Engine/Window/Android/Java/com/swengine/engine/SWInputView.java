/*
 * SWInputView.java — 입력기(IME)의 InputConnection 호출을 엔진(C++)에 넘기는 얇은 뷰입니다(설계: docs/DesignBacklog.md "Android IME 조합", 감사 P-4).
 *
 * NativeActivity 의 입력 연결은 키 이벤트만 보내 한글 · 일본어 · 중국어 조합이 오지 않는다. 이 뷰가 내용 뷰 위(1 x 1)에 붙어 포커스를 받고
 * onCreateInputConnection 으로 BaseInputConnection 파생을 돌려준다 — 호출(조합 글 · 조합 끝 · 확정 · 주변 글 지우기)은 그대로 JNI 로 넘기고,
 * 옮기는 규칙은 C++ 한 곳(AndroidInputConnectionState)이다. 편집기 글은 엔진(UI 글 칸)이 가지므로 여기 편집기는 늘 비어 있다
 * (getTextBeforeCursor 는 빈 글 — 언리얼 GameTextInput 이 확정마다 상태를 비우는 것과 같다). Godot 의 GodotEditText 자리다.
 *
 * 스레드: attach · showSoftInput · detach 는 게임 스레드가 부르고 뷰 조작은 runOnUiThread 로 옮긴다. JNI 호출은 UI 스레드에서 이 객체의 잠금 안에서 하고,
 * detach 는 같은 잠금 안에서 핸들을 0 으로 만든다 — detach 가 돌아온 뒤에는 C++ 객체를 부르지 않는다(C++ 가 그 뒤에 객체를 지운다).
 * 묶기: py -3 -m Scripts android-apk 가 javac + d8 로 classes.dex 에 담는다(Scripts/package/android_apk.py). native 메서드는 C++ 가 RegisterNatives 로 잇는다.
 */
package com.swengine.engine;

import android.app.Activity;
import android.content.Context;
import android.graphics.Rect;
import android.text.InputType;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

public final class SWInputView extends View {
    private final Activity mActivity;
    /** C++ AndroidInputView 의 주소입니다. 0 이면 떼어졌다(잠금 this 안에서만 읽고 쓴다). */
    private long mNativeHandle;

    private SWInputView(Activity activity, long nativeHandle) {
        super(activity);
        mActivity = activity;
        mNativeHandle = nativeHandle;
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    /** 뷰를 만들고 UI 스레드에서 액티비티 내용 뷰 위에 붙입니다(게임 스레드가 부른다 — 붙기 전 뷰는 어느 스레드에서 만들어도 된다). */
    public static SWInputView attach(final Activity activity, long nativeHandle) {
        final SWInputView view = new SWInputView(activity, nativeHandle);
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.addContentView(view, new ViewGroup.LayoutParams(1, 1));
            }
        });
        return view;
    }

    /** 화면 키보드를 올리거나(포커스 + showSoftInput, 플래그 0 = 명시적 요청) 내립니다. */
    public void showSoftInput(final boolean bVisible) {
        mActivity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                InputMethodManager manager = (InputMethodManager) mActivity.getSystemService(Context.INPUT_METHOD_SERVICE);
                if (manager == null) {
                    return;
                }
                if (bVisible) {
                    requestFocus();
                    manager.showSoftInput(SWInputView.this, 0);
                } else {
                    manager.hideSoftInputFromWindow(getWindowToken(), 0);
                    clearFocus();
                }
            }
        });
    }

    /** C++ 를 더 부르지 않게 하고(돌아온 뒤 C++ 객체를 지워도 된다) UI 스레드에서 뷰를 뗍니다. */
    public void detach() {
        synchronized (this) {
            mNativeHandle = 0;
        }
        mActivity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                ViewParent parent = getParent();
                if (parent instanceof ViewGroup) {
                    ((ViewGroup) parent).removeView(SWInputView.this);
                }
            }
        });
    }

    @Override
    public boolean onCheckIsTextEditor() {
        return true;
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT;
        // 전체 화면 추출 편집기를 띄우지 않는다 — 글은 게임 UI 가 그린다.
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_ACTION_DONE;
        return new Connection(this);
    }

    @Override
    protected void onFocusChanged(boolean gainFocus, int direction, Rect previouslyFocusedRect) {
        super.onFocusChanged(gainFocus, direction, previouslyFocusedRect);
        if (!gainFocus) {
            dispatchCancelComposition();
        }
    }

    private synchronized void dispatchSetComposingText(String text) {
        if (mNativeHandle != 0) {
            nativeSetComposingText(mNativeHandle, text);
        }
    }

    private synchronized void dispatchFinishComposingText() {
        if (mNativeHandle != 0) {
            nativeFinishComposingText(mNativeHandle);
        }
    }

    private synchronized void dispatchCommitText(String text) {
        if (mNativeHandle != 0) {
            nativeCommitText(mNativeHandle, text);
        }
    }

    private synchronized void dispatchDeleteSurroundingText(int beforeLength, int afterLength) {
        if (mNativeHandle != 0) {
            nativeDeleteSurroundingText(mNativeHandle, beforeLength, afterLength);
        }
    }

    private synchronized void dispatchCancelComposition() {
        if (mNativeHandle != 0) {
            nativeCancelComposition(mNativeHandle);
        }
    }

    /** 편집기 글을 갖지 않는 연결입니다 — 받은 호출을 C++ 로 넘기고 바탕(Editable)은 건드리지 않는다. 키(sendKeyEvent)는 바탕이 NativeActivity 입력 큐로 보낸다. */
    private static final class Connection extends BaseInputConnection {
        private final SWInputView mView;

        Connection(SWInputView view) {
            super(view, false);
            mView = view;
        }

        @Override
        public boolean setComposingText(CharSequence text, int newCursorPosition) {
            mView.dispatchSetComposingText(text != null ? text.toString() : "");
            return true;
        }

        @Override
        public boolean finishComposingText() {
            mView.dispatchFinishComposingText();
            return true;
        }

        @Override
        public boolean commitText(CharSequence text, int newCursorPosition) {
            mView.dispatchCommitText(text != null ? text.toString() : "");
            return true;
        }

        @Override
        public boolean deleteSurroundingText(int beforeLength, int afterLength) {
            mView.dispatchDeleteSurroundingText(beforeLength, afterLength);
            return true;
        }
    }

    private static native void nativeSetComposingText(long nativeHandle, String text);

    private static native void nativeFinishComposingText(long nativeHandle);

    private static native void nativeCommitText(long nativeHandle, String text);

    private static native void nativeDeleteSurroundingText(long nativeHandle, int beforeLength, int afterLength);

    private static native void nativeCancelComposition(long nativeHandle);
}
