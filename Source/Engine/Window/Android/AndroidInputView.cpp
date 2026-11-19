#include "pch.h"

#include "Engine/Window/Android/AndroidInputView.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace sw
{
    namespace
    {
        struct AndroidInputViewInternal
        {
            static constexpr int32  kClosedFd      = -1;
            static constexpr int32  kPipeReadEnd   = 0;
            static constexpr int32  kPipeWriteEnd  = 1;
            static constexpr uint8  kWakeByte      = 1;
            static constexpr size_t kWakeReadBytes = 64; ///< 깨우기 바이트를 한 번에 읽는 양

            static AndroidInputView* fromHandle( jlong nativeHandle ) { return reinterpret_cast<AndroidInputView*>( static_cast<uintptr_t>( nativeHandle ) ); }

            /**
             * @brief Java 글(UTF-16)을 UTF-8 로 옮깁니다. `GetStringUTFChars` 는 수정 UTF-8 이라 보조 평면 글자(이모지)를 대리 쌍 두 개(CESU-8)로 내므로 쓰지 않는다.
             * @details 짝 없는 대리 값은 U+FFFD 로 바꾼다 — 입력기 글을 믿지 않는다.
             */
            static string toUTF8( JNIEnv* pEnv, jstring text )
            {
                string result;
                if ( text == nullptr )
                    return result;
                const jsize        length = pEnv->GetStringLength( text );
                const jchar* const pChar  = pEnv->GetStringChars( text, nullptr );
                if ( pChar == nullptr )
                    return result;
                constexpr uint32 kHighSurrogateBegin = 0xD800;
                constexpr uint32 kLowSurrogateBegin  = 0xDC00;
                constexpr uint32 kSurrogateEnd       = 0xE000;
                constexpr uint32 kUTF16LowBoundary   = 0x10000;
                constexpr uint32 kSurrogateBits      = 10;
                constexpr uint32 kReplacement        = 0xFFFD;
                for ( jsize index = 0; index < length; ++index )
                {
                    const uint32 unit = pChar[index];
                    if ( unit < kHighSurrogateBegin || unit >= kSurrogateEnd )
                    {
                        StringUtil::appendUTF8( result, unit );
                        continue;
                    }
                    const uint32 next = index + 1 < length ? static_cast<uint32>( pChar[index + 1] ) : 0u;
                    if ( unit < kLowSurrogateBegin && next >= kLowSurrogateBegin && next < kSurrogateEnd )
                    {
                        StringUtil::appendUTF8( result, kUTF16LowBoundary + ( ( unit - kHighSurrogateBegin ) << kSurrogateBits ) + ( next - kLowSurrogateBegin ) );
                        ++index;
                        continue;
                    }
                    StringUtil::appendUTF8( result, kReplacement );
                }
                pEnv->ReleaseStringChars( text, pChar );
                return result;
            }

            static uint32 toCount( jint value ) { return value > 0 ? static_cast<uint32>( value ) : 0u; } // 음수는 명세 밖 — 지우지 않는다

            static void JNICALL setComposingText( JNIEnv* pEnv, jclass, jlong nativeHandle, jstring text ) { fromHandle( nativeHandle )->onSetComposingText( toUTF8( pEnv, text ) ); }
            static void JNICALL finishComposingText( JNIEnv*, jclass, jlong nativeHandle ) { fromHandle( nativeHandle )->onFinishComposingText(); }
            static void JNICALL commitText( JNIEnv* pEnv, jclass, jlong nativeHandle, jstring text ) { fromHandle( nativeHandle )->onCommitText( toUTF8( pEnv, text ) ); }
            static void JNICALL deleteSurroundingText( JNIEnv*, jclass, jlong nativeHandle, jint beforeLength, jint afterLength )
            {
                fromHandle( nativeHandle )->onDeleteSurroundingText( toCount( beforeLength ), toCount( afterLength ) );
            }
            static void JNICALL cancelComposition( JNIEnv*, jclass, jlong nativeHandle ) { fromHandle( nativeHandle )->onCancelComposition(); }

            /** @brief 예외가 났으면 지우고 true 입니다. */
            static bool clearException( JNIEnv* pEnv )
            {
                if ( pEnv->ExceptionCheck() == JNI_FALSE )
                    return false;
                pEnv->ExceptionDescribe(); // logcat 에 Java 스택을 남긴다
                pEnv->ExceptionClear();
                return true;
            }

            /** @brief 액티비티의 클래스 로더로 앱 클래스 @p pDottedName 을 찾습니다. 못 찾으면 nullptr 입니다(지역 참조). */
            static jclass loadAppClass( JNIEnv* pEnv, jobject activity, const utf8* pDottedName )
            {
                const jclass    activityC      = pEnv->GetObjectClass( activity );
                const jmethodID getClassLoader = pEnv->GetMethodID( activityC, "getClassLoader", "()Ljava/lang/ClassLoader;" );
                const jobject   loader         = getClassLoader != nullptr ? pEnv->CallObjectMethod( activity, getClassLoader ) : nullptr;
                const jclass    loaderC        = loader != nullptr ? pEnv->FindClass( "java/lang/ClassLoader" ) : nullptr;
                const jmethodID loadClass      = loaderC != nullptr ? pEnv->GetMethodID( loaderC, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;" ) : nullptr;
                const jstring   name           = pEnv->NewStringUTF( pDottedName );
                const jclass    found          = loadClass != nullptr ? static_cast<jclass>( pEnv->CallObjectMethod( loader, loadClass, name ) ) : nullptr;
                return clearException( pEnv ) ? nullptr : found;
            }
        };
    } // namespace

    AndroidInputView::AndroidInputView()
        : _pApp{ nullptr }
        , _view{ nullptr }
        , _readFd{ AndroidInputViewInternal::kClosedFd }
        , _writeFd{ AndroidInputViewInternal::kClosedFd }
        , _pollSource{}
        , _state{}
        , _mutex{}
    {
    }

    AndroidInputView::~AndroidInputView()
    {
        detach();
    }

    bool AndroidInputView::attach( android_app* pApp, void ( *onReady )( android_app*, android_poll_source* ) )
    {
        if ( pApp == nullptr || pApp->activity == nullptr || pApp->looper == nullptr || _pApp != nullptr )
            return false;
        int32 arrFd[2] = { AndroidInputViewInternal::kClosedFd, AndroidInputViewInternal::kClosedFd };
        if ( pipe2( arrFd, O_CLOEXEC | O_NONBLOCK ) != 0 )
        {
            SW_LOG_ERROR_ID( "AndroidInputView.PipeFailed", "Android IME: pipe2 failed (errno %#) - the input view cannot wake the game thread", errno );
            return false;
        }
        _pApp               = pApp;
        _readFd             = arrFd[AndroidInputViewInternal::kPipeReadEnd];
        _writeFd            = arrFd[AndroidInputViewInternal::kPipeWriteEnd];
        _pollSource.id      = LOOPER_ID_USER;
        _pollSource.app     = pApp;
        _pollSource.process = onReady;
        if ( ALooper_addFd( pApp->looper, _readFd, LOOPER_ID_USER, ALOOPER_EVENT_INPUT, nullptr, &_pollSource ) != 1 )
        {
            SW_LOG_ERROR_ID( "AndroidInputView.LooperFailed", "Android IME: ALooper_addFd failed - the input view cannot wake the game thread" );
            detach();
            return false;
        }

        JavaVM* const pVM  = pApp->activity->vm;
        JNIEnv*       pEnv = nullptr;
        if ( pVM->AttachCurrentThread( &pEnv, nullptr ) != JNI_OK || pEnv == nullptr )
        {
            SW_LOG_ERROR_ID( "AndroidInputView.AttachFailed", "Android IME: could not attach the game thread to the JVM" );
            detach();
            return false;
        }
        const jobject activity = pApp->activity->clazz;
        const jclass  viewC    = AndroidInputViewInternal::loadAppClass( pEnv, activity, kJavaClassName );
        bool          bOk      = viewC != nullptr;
        if ( bOk == false )
            SW_LOG_ERROR_ID( "AndroidInputView.ClassMissing", "Android IME: class %# is not in the APK - package with py -3 -m Scripts android-apk (it compiles the Java views into classes.dex)",
                             kJavaClassName );
        const JNINativeMethod arrMethod[] = {
            {     "nativeSetComposingText", "(JLjava/lang/String;)V",      reinterpret_cast<void*>( &AndroidInputViewInternal::setComposingText )},
            {  "nativeFinishComposingText",                   "(J)V",   reinterpret_cast<void*>( &AndroidInputViewInternal::finishComposingText )},
            {           "nativeCommitText", "(JLjava/lang/String;)V",            reinterpret_cast<void*>( &AndroidInputViewInternal::commitText )},
            {"nativeDeleteSurroundingText",                 "(JII)V", reinterpret_cast<void*>( &AndroidInputViewInternal::deleteSurroundingText )},
            {    "nativeCancelComposition",                   "(J)V",     reinterpret_cast<void*>( &AndroidInputViewInternal::cancelComposition )},
        };
        // NativeActivity 는 .so 를 클래스 로더 밖에서 열어 이름(`Java_…`)으로 잇는 길이 없다 — 직접 잇는다.
        bOk = bOk && pEnv->RegisterNatives( viewC, arrMethod, static_cast<jint>( sizeof( arrMethod ) / sizeof( arrMethod[0] ) ) ) == JNI_OK &&
              AndroidInputViewInternal::clearException( pEnv ) == false;
        const jmethodID attachMethod = bOk ? pEnv->GetStaticMethodID( viewC, "attach", "(Landroid/app/Activity;J)Lcom/swengine/engine/SWInputView;" ) : nullptr;
        const jobject   view         = attachMethod != nullptr ? pEnv->CallStaticObjectMethod( viewC, attachMethod, activity, static_cast<jlong>( reinterpret_cast<uintptr_t>( this ) ) ) : nullptr;
        if ( AndroidInputViewInternal::clearException( pEnv ) == false && view != nullptr )
            _view = pEnv->NewGlobalRef( view );
        pVM->DetachCurrentThread();
        if ( _view == nullptr )
        {
            if ( viewC != nullptr )
                SW_LOG_ERROR_ID( "AndroidInputView.AttachViewFailed", "Android IME: registering natives or attaching %# failed (logcat has the Java exception)", kJavaClassName );
            detach();
            return false;
        }
        return true;
    }

    void AndroidInputView::detach()
    {
        if ( _pApp == nullptr )
            return;
        if ( _view != nullptr )
        {
            JavaVM* const pVM  = _pApp->activity->vm;
            JNIEnv*       pEnv = nullptr;
            if ( pVM->AttachCurrentThread( &pEnv, nullptr ) == JNI_OK && pEnv != nullptr )
            {
                // Java 가 잠금 안에서 핸들을 0 으로 만든다 — 돌아오면 UI 스레드가 이 객체를 더 부르지 않는다.
                const jmethodID detachMethod = pEnv->GetMethodID( pEnv->GetObjectClass( _view ), "detach", "()V" );
                if ( detachMethod != nullptr )
                    pEnv->CallVoidMethod( _view, detachMethod );
                (void)AndroidInputViewInternal::clearException( pEnv );
                pEnv->DeleteGlobalRef( _view );
                pVM->DetachCurrentThread();
            }
            _view = nullptr;
        }
        if ( _readFd != AndroidInputViewInternal::kClosedFd )
        {
            (void)ALooper_removeFd( _pApp->looper, _readFd );
            close( _readFd );
        }
        if ( _writeFd != AndroidInputViewInternal::kClosedFd )
            close( _writeFd );
        _readFd  = AndroidInputViewInternal::kClosedFd;
        _writeFd = AndroidInputViewInternal::kClosedFd;
        _pApp    = nullptr;
    }

    void AndroidInputView::setSoftInputVisible( bool bVisible )
    {
        if ( _pApp == nullptr || _view == nullptr )
            return;
        JavaVM* const pVM  = _pApp->activity->vm;
        JNIEnv*       pEnv = nullptr;
        if ( pVM->AttachCurrentThread( &pEnv, nullptr ) != JNI_OK || pEnv == nullptr )
            return;
        const jmethodID showMethod = pEnv->GetMethodID( pEnv->GetObjectClass( _view ), "showSoftInput", "(Z)V" );
        if ( showMethod != nullptr )
            pEnv->CallVoidMethod( _view, showMethod, bVisible ? JNI_TRUE : JNI_FALSE );
        (void)AndroidInputViewInternal::clearException( pEnv );
        pVM->DetachCurrentThread();
    }

    void AndroidInputView::takePendingActions( vector<AndroidTextAction>& outListAction )
    {
        uint8 arrWakeByte[AndroidInputViewInternal::kWakeReadBytes];
        while ( _readFd != AndroidInputViewInternal::kClosedFd && read( _readFd, arrWakeByte, sizeof( arrWakeByte ) ) > 0 )
        {
        } // 깨우기 바이트를 비운다 — 남기면 ALooper 가 쉬지 않고 깨운다
        ScopedLock<mutex> lock{ _mutex };
        for ( AndroidTextAction& action : _listPendingAction )
        {
            outListAction.push_back( std::move( action ) );
        }
        _listPendingAction.clear();
    }

    void AndroidInputView::publish( vector<AndroidTextAction>& listAction )
    {
        if ( listAction.empty() )
            return;
        {
            ScopedLock<mutex> lock{ _mutex };
            for ( AndroidTextAction& action : listAction )
            {
                _listPendingAction.push_back( std::move( action ) );
            }
        }
        const uint8 wake = AndroidInputViewInternal::kWakeByte;
        (void)write( _writeFd, &wake, sizeof( wake ) ); // 파이프가 차서 못 써도(EAGAIN) 이미 깨울 바이트가 있다
    }

    void AndroidInputView::onSetComposingText( string_view text )
    {
        vector<AndroidTextAction> listAction;
        _state.setComposingText( text, listAction );
        publish( listAction );
    }

    void AndroidInputView::onFinishComposingText()
    {
        vector<AndroidTextAction> listAction;
        _state.finishComposingText( listAction );
        publish( listAction );
    }

    void AndroidInputView::onCommitText( string_view text )
    {
        vector<AndroidTextAction> listAction;
        _state.commitText( text, listAction );
        publish( listAction );
    }

    void AndroidInputView::onDeleteSurroundingText( uint32 beforeLength, uint32 afterLength )
    {
        vector<AndroidTextAction> listAction;
        _state.deleteSurroundingText( beforeLength, afterLength, listAction );
        publish( listAction );
    }

    void AndroidInputView::onCancelComposition()
    {
        vector<AndroidTextAction> listAction;
        _state.cancelComposition( listAction );
        publish( listAction );
    }
} // namespace sw
