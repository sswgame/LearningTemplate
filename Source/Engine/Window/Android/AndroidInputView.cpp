#include "pch.h"

#include "Engine/Window/Android/AndroidInputView.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Log/Logger.h"

#include "Engine/Window/Android/AndroidJNIScope.h"
#include "Engine/Window/Android/AndroidJNIString.h"

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

            static uint32 toCount( jint value ) { return value > 0 ? static_cast<uint32>( value ) : 0u; } // 음수는 명세 밖 — 지우지 않는다

            static void JNICALL setComposingText( JNIEnv* pEnv, jclass, jlong nativeHandle, jstring text ) { fromHandle( nativeHandle )->onSetComposingText( AndroidJNIString::toUTF8( pEnv, text ) ); }
            static void JNICALL finishComposingText( JNIEnv*, jclass, jlong nativeHandle ) { fromHandle( nativeHandle )->onFinishComposingText(); }
            static void JNICALL commitText( JNIEnv* pEnv, jclass, jlong nativeHandle, jstring text ) { fromHandle( nativeHandle )->onCommitText( AndroidJNIString::toUTF8( pEnv, text ) ); }
            static void JNICALL deleteSurroundingText( JNIEnv*, jclass, jlong nativeHandle, jint beforeLength, jint afterLength )
            {
                fromHandle( nativeHandle )->onDeleteSurroundingText( toCount( beforeLength ), toCount( afterLength ) );
            }
            static void JNICALL cancelComposition( JNIEnv*, jclass, jlong nativeHandle ) { fromHandle( nativeHandle )->onCancelComposition(); }
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

        const AndroidJNIScope jni;
        if ( jni.isValid() == false )
        {
            SW_LOG_ERROR_ID( "AndroidInputView.AttachFailed", "Android IME: could not attach the game thread to the JVM" );
            detach();
            return false;
        }
        const jclass viewC = jni.loadAppClass( kJavaClassName );
        if ( viewC == nullptr )
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
        const bool bRegistered = viewC != nullptr && jni.getEnv()->RegisterNatives( viewC, arrMethod, static_cast<jint>( sizeof( arrMethod ) / sizeof( arrMethod[0] ) ) ) == JNI_OK &&
                                 jni.takeException() == false;
        const jmethodID attachMethod = bRegistered ? jni.getStaticMethod( viewC, "attach", "(Landroid/app/Activity;J)Lcom/swengine/engine/SWInputView;" ) : nullptr;
        const jobject   view         = jni.callStaticObject( viewC, attachMethod, jni.getActivity(), static_cast<jlong>( reinterpret_cast<uintptr_t>( this ) ) );
        if ( view != nullptr )
            _view = jni.getEnv()->NewGlobalRef( view );
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
            const AndroidJNIScope jni;
            if ( jni.isValid() )
            {
                // Java 가 잠금 안에서 핸들을 0 으로 만든다 — 돌아오면 UI 스레드가 이 객체를 더 부르지 않는다.
                (void)jni.callVoid( _view, jni.getMethod( jni.getObjectClass( _view ), "detach", "()V" ) );
                jni.getEnv()->DeleteGlobalRef( _view );
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
        const AndroidJNIScope jni;
        (void)jni.callVoid( _view, jni.getMethod( jni.getObjectClass( _view ), "showSoftInput", "(Z)V" ), bVisible ? JNI_TRUE : JNI_FALSE );
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
