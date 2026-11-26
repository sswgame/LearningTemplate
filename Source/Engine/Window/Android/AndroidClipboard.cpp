/**
 * @file AndroidClipboard.cpp
 * @brief Android 클립보드입니다. JNI 로 액티비티의 `ClipboardManager` 를 부른다(NDK 에는 클립보드 API 가 없다).
 * @details 읽기: `getPrimaryClip().getItemAt( 0 ).coerceToText( activity ).toString()`, 쓰기: `setPrimaryClip( ClipData.newPlainText( "", text ) )`.
 *          글은 `AndroidJNIString`(UTF-16 단위, `GetStringChars` · `NewString`)으로 옮긴다. JNI 의 `NewStringUTF` · `GetStringUTFChars` 는 수정 UTF-8 이라
 *          보충 평면 글자(이모지)가 서로게이트 두 개로 깨진다. https://developer.android.com/reference/android/content/ClipboardManager
 *          Looper 제약: API 26 부터 `ClipboardManager` 는 만들 때 Handler 를 만들지 않고(액티비티의 메인 스레드 Handler 를 받는다) 호출은 바인더라
 *          게임 스레드(android_main)에서 불러도 된다. 이 앱의 최소 API 는 29 다(CMakePresets.json). Android 10 부터 읽기는 앱이 포커스를 가질 때만 글을 준다(그 밖은 null 이라 false).
 *          유니티 `GUIUtility.systemCopyBuffer` · 언리얼 `FAndroidApplicationMisc::ClipboardCopy`(자바 쪽 ClipboardManager)와 같은 자리다.
 */
#include "pch.h"

#include "Engine/Window/PlatformClipboard.h"

#if defined( SW_PLATFORM_ANDROID )
    #include "Engine/Window/Android/AndroidJNIScope.h"
    #include "Engine/Window/Android/AndroidJNIString.h"
    #include "Engine/Window/Android/AndroidWindow.h"

    #include <android_native_app_glue.h>
    #include <jni.h>

namespace sw
{
    namespace
    {
        struct AndroidClipboardInternal
        {
            /** @brief 액티비티의 `ClipboardManager` 입니다(`getSystemService( Context.CLIPBOARD_SERVICE )`). 없으면 nullptr 입니다. */
            static jobject findClipboardManager( const AndroidJNIScope& jni )
            {
                const jclass  contextC   = jni.findClass( "android/content/Context" );
                const jobject serviceKey = jni.getStaticObjectField( contextC, jni.getStaticField( contextC, "CLIPBOARD_SERVICE", "Ljava/lang/String;" ) );
                return jni.callObject( jni.getActivity(), jni.getMethod( contextC, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;" ), serviceKey );
            }
        };
    } // namespace

    bool PlatformClipboard::isAvailable()
    {
        const android_app* const pApp = AndroidWindow::getApplication();
        return pApp != nullptr && pApp->activity != nullptr;
    }

    bool PlatformClipboard::readText( string& outText )
    {
        outText.clear();
        // 호출마다 범위가 예외를 확인하고 빈 값을 낸다 — 예외 위에서 다음 JNI 를 부르지 않는다(CheckJNI).
        const AndroidJNIScope jni;
        const jobject         manager = AndroidClipboardInternal::findClipboardManager( jni );
        const jobject         clip    = jni.callObject( manager, jni.getMethod( jni.getObjectClass( manager ), "getPrimaryClip", "()Landroid/content/ClipData;" ) );
        if ( clip == nullptr )
            return false; // 클립보드가 비었거나 앱이 포커스를 갖지 않았다(Android 10+)
        const jclass         clipC     = jni.getObjectClass( clip );
        const optional<jint> itemCount = jni.callInt( clip, jni.getMethod( clipC, "getItemCount", "()I" ) );
        if ( itemCount.has_value() == false || *itemCount <= 0 )
            return false;
        const jobject item         = jni.callObject( clip, jni.getMethod( clipC, "getItemAt", "(I)Landroid/content/ClipData$Item;" ), 0 );
        const jobject textSequence = jni.callObject( item, jni.getMethod( jni.getObjectClass( item ), "coerceToText", "(Landroid/content/Context;)Ljava/lang/CharSequence;" ), jni.getActivity() );
        const jobject text         = jni.callObject( textSequence, jni.getMethod( jni.getObjectClass( textSequence ), "toString", "()Ljava/lang/String;" ) );
        if ( text == nullptr )
            return false;
        outText = AndroidJNIString::toUTF8( jni.getEnv(), static_cast<jstring>( text ) );
        return true;
    }

    bool PlatformClipboard::writeText( string_view text )
    {
        const AndroidJNIScope jni;
        const jobject         manager = AndroidClipboardInternal::findClipboardManager( jni );
        if ( manager == nullptr )
        {
            SW_LOG_WARNING_ID( "AndroidClipboard.NoManager", "Android: the activity's ClipboardManager is unavailable - the text was not copied" );
            return false;
        }
        const jstring   clipText = jni.newString( text );
        const jstring   label    = jni.newString( string_view{} );
        const jclass    clipC    = jni.findClass( "android/content/ClipData" );
        const jmethodID newPlain = jni.getStaticMethod( clipC, "newPlainText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;" );
        const jobject   clip     = clipText != nullptr && label != nullptr ? jni.callStaticObject( clipC, newPlain, label, clipText ) : nullptr;
        if ( clip == nullptr || jni.callVoid( manager, jni.getMethod( jni.getObjectClass( manager ), "setPrimaryClip", "(Landroid/content/ClipData;)V" ), clip ) == false )
        {
            SW_LOG_WARNING_ID( "AndroidClipboard.WriteFailed", "Android: ClipboardManager.setPrimaryClip failed - the text (%# bytes) was not copied", text.size() );
            return false;
        }
        return true;
    }
} // namespace sw

#endif
