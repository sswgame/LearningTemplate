#include "pch.h"

#include "Engine/Window/Android/AndroidJNIScope.h"

#include "Engine/Window/Android/AndroidJNIString.h"
#include "Engine/Window/Android/AndroidWindow.h"

#include <android_native_app_glue.h>
#include <pthread.h>

namespace sw
{
    namespace
    {
        struct AndroidJNIScopeInternal
        {
            static constexpr jint kLocalReferenceCapacity = 16;

            static pthread_key_t  s_detachKey;
            static pthread_once_t s_detachKeyOnce;

            /** @brief 우리가 붙인 스레드가 끝날 때 뗀다(값은 그 JavaVM). 붙지 않은 채 끝나는 네이티브 스레드는 ART 가 멈춘다. */
            static void detachOnThreadExit( void* pVM ) { static_cast<JavaVM*>( pVM )->DetachCurrentThread(); }
            static void createDetachKey() { (void)pthread_key_create( &s_detachKey, &detachOnThreadExit ); }

            /** @brief 지금 스레드의 JNIEnv 입니다. 붙지 않았으면 붙이고 스레드 끝에서 떼도록 적는다 — Java 가 만든 스레드(UI)는 그대로 쓴다. */
            static JNIEnv* acquireEnv( JavaVM* pVM )
            {
                JNIEnv*    pEnv   = nullptr;
                const jint status = pVM->GetEnv( reinterpret_cast<void**>( &pEnv ), JNI_VERSION_1_6 );
                if ( status == JNI_OK )
                    return pEnv;
                if ( status != JNI_EDETACHED || pVM->AttachCurrentThread( &pEnv, nullptr ) != JNI_OK )
                    return nullptr;
                (void)pthread_once( &s_detachKeyOnce, &createDetachKey );
                (void)pthread_setspecific( s_detachKey, pVM );
                return pEnv;
            }
        };

        pthread_key_t  AndroidJNIScopeInternal::s_detachKey     = 0;
        pthread_once_t AndroidJNIScopeInternal::s_detachKeyOnce = PTHREAD_ONCE_INIT;
    } // namespace

    AndroidJNIScope::AndroidJNIScope()
        : _pEnv{ nullptr }
        , _activity{ nullptr }
        , _bHadException{ 0 }
        , _reserved{ 0 }
    {
        android_app* const pApp = AndroidWindow::getApplication();
        if ( pApp == nullptr || pApp->activity == nullptr )
            return;
        JNIEnv* const pEnv = AndroidJNIScopeInternal::acquireEnv( pApp->activity->vm );
        if ( pEnv == nullptr || pEnv->PushLocalFrame( AndroidJNIScopeInternal::kLocalReferenceCapacity ) != JNI_OK )
            return;
        _pEnv     = pEnv;
        _activity = pApp->activity->clazz;
    }

    AndroidJNIScope::~AndroidJNIScope()
    {
        if ( _pEnv == nullptr )
            return;
        (void)takeException();
        (void)_pEnv->PopLocalFrame( nullptr );
    }

    bool AndroidJNIScope::takeException() const
    {
        if ( _pEnv == nullptr || _pEnv->ExceptionCheck() == JNI_FALSE )
            return false;
        _pEnv->ExceptionDescribe(); // logcat 에 Java 스택을 남긴다
        _pEnv->ExceptionClear();
        _bHadException = 1;
        return true;
    }

    jclass AndroidJNIScope::findClass( const utf8* pSlashName ) const
    {
        if ( _pEnv == nullptr )
            return nullptr;
        const jclass found = _pEnv->FindClass( pSlashName );
        return takeException() ? nullptr : found;
    }

    jclass AndroidJNIScope::loadAppClass( const utf8* pDottedName ) const
    {
        const jmethodID getClassLoader = getMethod( getObjectClass( _activity ), "getClassLoader", "()Ljava/lang/ClassLoader;" );
        const jobject   loader         = callObject( _activity, getClassLoader );
        const jmethodID loadClass      = getMethod( findClass( "java/lang/ClassLoader" ), "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;" );
        const jstring   name           = loader != nullptr && loadClass != nullptr ? newString( pDottedName ) : nullptr;
        return name != nullptr ? static_cast<jclass>( callObject( loader, loadClass, name ) ) : nullptr;
    }

    jclass AndroidJNIScope::getObjectClass( jobject object ) const
    {
        return _pEnv != nullptr && object != nullptr ? _pEnv->GetObjectClass( object ) : nullptr;
    }

    jmethodID AndroidJNIScope::getMethod( jclass classObject, const utf8* pName, const utf8* pSignature ) const
    {
        if ( _pEnv == nullptr || classObject == nullptr )
            return nullptr;
        const jmethodID method = _pEnv->GetMethodID( classObject, pName, pSignature );
        return takeException() ? nullptr : method;
    }

    jmethodID AndroidJNIScope::getStaticMethod( jclass classObject, const utf8* pName, const utf8* pSignature ) const
    {
        if ( _pEnv == nullptr || classObject == nullptr )
            return nullptr;
        const jmethodID method = _pEnv->GetStaticMethodID( classObject, pName, pSignature );
        return takeException() ? nullptr : method;
    }

    jfieldID AndroidJNIScope::getStaticField( jclass classObject, const utf8* pName, const utf8* pSignature ) const
    {
        if ( _pEnv == nullptr || classObject == nullptr )
            return nullptr;
        const jfieldID field = _pEnv->GetStaticFieldID( classObject, pName, pSignature );
        return takeException() ? nullptr : field;
    }

    jobject AndroidJNIScope::getStaticObjectField( jclass classObject, jfieldID field ) const
    {
        if ( _pEnv == nullptr || classObject == nullptr || field == nullptr )
            return nullptr;
        const jobject value = _pEnv->GetStaticObjectField( classObject, field );
        return takeException() ? nullptr : value;
    }

    jstring AndroidJNIScope::newString( string_view text ) const
    {
        if ( _pEnv == nullptr )
            return nullptr;
        const jstring result = AndroidJNIString::toJava( _pEnv, text );
        return takeException() ? nullptr : result;
    }
} // namespace sw
