/**
 * @file AndroidClipboard.cpp
 * @brief Android 클립보드입니다. JNI 로 액티비티의 `ClipboardManager` 를 부른다(NDK 에는 클립보드 API 가 없다).
 * @details 읽기: `getPrimaryClip().getItemAt( 0 ).coerceToText( activity ).toString()`, 쓰기: `setPrimaryClip( ClipData.newPlainText( "", text ) )`.
 *          글은 `String.getBytes( "UTF-8" )` · `new String( bytes, "UTF-8" )` 로 옮긴다 — JNI 의 `NewStringUTF` · `GetStringUTFChars` 는 수정 UTF-8 이라
 *          보충 평면 글자(이모지)가 서로게이트 두 개로 깨진다. https://developer.android.com/reference/android/content/ClipboardManager
 *          Looper 제약: API 26 부터 `ClipboardManager` 는 만들 때 Handler 를 만들지 않고(액티비티의 메인 스레드 Handler 를 받는다) 호출은 바인더라
 *          게임 스레드(android_main)에서 불러도 된다 — 이 앱의 최소 API 는 29 다(CMakePresets.json). Android 10 부터 읽기는 앱이 포커스를 가질 때만 글을 준다(그 밖은 null — false).
 *          유니티 `GUIUtility.systemCopyBuffer` · 언리얼 `FAndroidApplicationMisc::ClipboardCopy`(자바 쪽 ClipboardManager)와 같은 자리다.
 */
#include "pch.h"

#include "Engine/Window/PlatformClipboard.h"

#if defined( SW_PLATFORM_ANDROID )
    #include "Engine/Window/Android/AndroidWindow.h"

    #include <android_native_app_glue.h>
    #include <jni.h>

namespace sw
{
    namespace
    {
        /** @brief JNI 한 번의 호출 범위입니다 — 스레드를 JVM 에 붙이고 로컬 참조 틀을 열며, 끝나면 예외를 지우고 뗀다(AndroidWindow 의 JNI 호출과 같은 규칙). */
        class AndroidClipboardScope
        {
        public:
            AndroidClipboardScope()
                : _pApp{ AndroidWindow::getApplication() }
                , _pEnv{ nullptr }
            {
                if ( _pApp == nullptr || _pApp->activity == nullptr || _pApp->activity->vm->AttachCurrentThread( &_pEnv, nullptr ) != JNI_OK )
                {
                    _pEnv = nullptr;
                    return;
                }
                constexpr jint kLocalReferenceCapacity = 16;
                (void)_pEnv->PushLocalFrame( kLocalReferenceCapacity );
            }
            ~AndroidClipboardScope()
            {
                if ( _pEnv == nullptr )
                    return;
                if ( _pEnv->ExceptionCheck() )
                    _pEnv->ExceptionClear();
                (void)_pEnv->PopLocalFrame( nullptr );
                _pApp->activity->vm->DetachCurrentThread();
            }
            AndroidClipboardScope( const AndroidClipboardScope& )            = delete;
            AndroidClipboardScope& operator=( const AndroidClipboardScope& ) = delete;

            JNIEnv* getEnv() const { return _pEnv; }
            jobject getActivity() const { return _pApp->activity->clazz; }

            /** @brief 마지막 JNI 호출이 예외를 던졌으면 지우고 true 입니다. */
            bool takeException() const
            {
                if ( _pEnv->ExceptionCheck() == JNI_FALSE )
                    return false;
                _pEnv->ExceptionClear();
                return true;
            }

            /** @brief 액티비티의 `ClipboardManager` 입니다(`getSystemService( Context.CLIPBOARD_SERVICE )`). 없으면 nullptr 입니다. */
            jobject findClipboardManager() const
            {
                const jclass    contextC   = _pEnv->FindClass( "android/content/Context" );
                const jfieldID  serviceF   = contextC != nullptr ? _pEnv->GetStaticFieldID( contextC, "CLIPBOARD_SERVICE", "Ljava/lang/String;" ) : nullptr;
                const jobject   serviceKey = serviceF != nullptr ? _pEnv->GetStaticObjectField( contextC, serviceF ) : nullptr;
                const jmethodID getService = contextC != nullptr ? _pEnv->GetMethodID( contextC, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;" ) : nullptr;
                const jobject   manager    = serviceKey != nullptr && getService != nullptr ? _pEnv->CallObjectMethod( getActivity(), getService, serviceKey ) : nullptr;
                return takeException() ? nullptr : manager;
            }

        private:
            android_app* _pApp;
            JNIEnv*      _pEnv;
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
        const AndroidClipboardScope scope;
        JNIEnv* const               pEnv    = scope.getEnv();
        const jobject               manager = pEnv != nullptr ? scope.findClipboardManager() : nullptr;
        if ( manager == nullptr )
            return false;
        // 예외가 걸린 채 다음 JNI 호출을 하면 CheckJNI(디버그 APK)가 앱을 죽인다 — 호출마다 예외를 보고 멈춘다.
        const jmethodID getClip = pEnv->GetMethodID( pEnv->GetObjectClass( manager ), "getPrimaryClip", "()Landroid/content/ClipData;" );
        const jobject   clip    = scope.takeException() == false ? pEnv->CallObjectMethod( manager, getClip ) : nullptr;
        if ( scope.takeException() || clip == nullptr )
            return false; // 클립보드가 비었거나 앱이 포커스를 갖지 않았다(Android 10+)
        const jclass    clipC     = pEnv->GetObjectClass( clip );
        const jmethodID getCount  = pEnv->GetMethodID( clipC, "getItemCount", "()I" );
        const jint      itemCount = scope.takeException() == false ? pEnv->CallIntMethod( clip, getCount ) : 0;
        if ( scope.takeException() || itemCount <= 0 )
            return false;
        const jmethodID getItem = pEnv->GetMethodID( clipC, "getItemAt", "(I)Landroid/content/ClipData$Item;" );
        const jobject   item    = scope.takeException() == false ? pEnv->CallObjectMethod( clip, getItem, 0 ) : nullptr;
        if ( scope.takeException() || item == nullptr )
            return false;
        const jmethodID coerceToText = pEnv->GetMethodID( pEnv->GetObjectClass( item ), "coerceToText", "(Landroid/content/Context;)Ljava/lang/CharSequence;" );
        const jobject   textSequence = scope.takeException() == false ? pEnv->CallObjectMethod( item, coerceToText, scope.getActivity() ) : nullptr;
        if ( scope.takeException() || textSequence == nullptr )
            return false;
        const jmethodID toString = pEnv->GetMethodID( pEnv->GetObjectClass( textSequence ), "toString", "()Ljava/lang/String;" );
        const jobject   text     = scope.takeException() == false ? pEnv->CallObjectMethod( textSequence, toString ) : nullptr;
        if ( scope.takeException() || text == nullptr )
            return false;
        const jstring   encodingName = pEnv->NewStringUTF( "UTF-8" );
        const jmethodID getBytes     = pEnv->GetMethodID( pEnv->GetObjectClass( text ), "getBytes", "(Ljava/lang/String;)[B" );
        const auto      bytes        = static_cast<jbyteArray>( scope.takeException() == false ? pEnv->CallObjectMethod( text, getBytes, encodingName ) : nullptr );
        if ( scope.takeException() || bytes == nullptr )
            return false;
        const jsize length = pEnv->GetArrayLength( bytes );
        if ( length > 0 )
        {
            outText.resize( static_cast<size_t>( length ) );
            pEnv->GetByteArrayRegion( bytes, 0, length, reinterpret_cast<jbyte*>( &outText[0] ) );
        }
        return true;
    }

    bool PlatformClipboard::writeText( string_view text )
    {
        const AndroidClipboardScope scope;
        JNIEnv* const               pEnv    = scope.getEnv();
        const jobject               manager = pEnv != nullptr ? scope.findClipboardManager() : nullptr;
        if ( manager == nullptr )
        {
            SW_LOG_WARNING_ID( "AndroidClipboard.NoManager", "Android: the activity's ClipboardManager is unavailable - the text was not copied" );
            return false;
        }
        const jsize      length = static_cast<jsize>( text.size() );
        const jbyteArray bytes  = pEnv->NewByteArray( length );
        if ( scope.takeException() || bytes == nullptr )
        {
            SW_LOG_WARNING_ID( "AndroidClipboard.WriteFailed", "Android: could not allocate %# bytes for the clipboard text", text.size() );
            return false;
        }
        pEnv->SetByteArrayRegion( bytes, 0, length, reinterpret_cast<const jbyte*>( text.data() ) );
        const jclass    stringC      = pEnv->FindClass( "java/lang/String" );
        const jmethodID construct    = stringC != nullptr ? pEnv->GetMethodID( stringC, "<init>", "([BLjava/lang/String;)V" ) : nullptr;
        const jstring   encodingName = pEnv->NewStringUTF( "UTF-8" );
        const jobject   clipText     = scope.takeException() == false && construct != nullptr ? pEnv->NewObject( stringC, construct, bytes, encodingName ) : nullptr;
        const jclass    clipC        = scope.takeException() == false && clipText != nullptr ? pEnv->FindClass( "android/content/ClipData" ) : nullptr;
        const jmethodID newPlain =
            clipC != nullptr ? pEnv->GetStaticMethodID( clipC, "newPlainText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;" ) : nullptr;
        const jstring   label   = pEnv->NewStringUTF( "" );
        const jobject   clip    = scope.takeException() == false && newPlain != nullptr ? pEnv->CallStaticObjectMethod( clipC, newPlain, label, clipText ) : nullptr;
        const jmethodID setClip = scope.takeException() == false && clip != nullptr ? pEnv->GetMethodID( pEnv->GetObjectClass( manager ), "setPrimaryClip", "(Landroid/content/ClipData;)V" ) : nullptr;
        const bool      bReady  = scope.takeException() == false && setClip != nullptr;
        if ( bReady )
            pEnv->CallVoidMethod( manager, setClip, clip );
        if ( bReady == false || scope.takeException() )
        {
            SW_LOG_WARNING_ID( "AndroidClipboard.WriteFailed", "Android: ClipboardManager.setPrimaryClip failed - the text was not copied" );
            return false;
        }
        return true;
    }
} // namespace sw

#endif
