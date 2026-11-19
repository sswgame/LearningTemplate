/**
 * @file AndroidJNIScope.h
 * @brief 엔진 스레드가 Java 를 부르는 한 범위입니다 — 스레드를 JVM 에 붙이고(스레드당 한 번, 스레드가 끝날 때 뗀다) 지역 참조 틀을 열며, 호출마다 예외를 확인합니다.
 * @details 예전에는 JNI 를 부르는 곳(창 · 클립보드 · 입력 뷰)마다 붙이기 · 떼기 · 예외 확인을 따로 적었다. 그래서 이미 붙은 스레드(Java 가 만든 스레드)를
 *          `DetachCurrentThread` 가 끊을 수 있었고, 예외가 걸린 채 다음 JNI 를 불러 CheckJNI(디버그 APK)가 앱을 죽일 수 있었다.
 *          이 범위의 호출 함수는 입력이 nullptr 이면 부르지 않고, 부른 뒤 예외가 있으면 지우고(logcat 에 스택) nullptr · 빈 값을 돌려준다 — 사슬로 이어 써도 예외 위에서 부르지 않는다.
 *          언리얼 `AndroidJavaEnv::GetJavaEnv`(스레드마다 한 번 붙이고 pthread 키 소멸자가 뗀다)와 `FJavaHelper` 자리다. 글 변환은 `AndroidJNIString`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include <jni.h>

namespace sw
{
    /** @class AndroidJNIScope @brief JNI 호출 한 범위입니다. 스택에만 둔다(스레드를 넘기지 않는다). */
    class AndroidJNIScope
    {
    public:
        /** @brief 지금 스레드의 JNIEnv 를 얻습니다(처음이면 붙인다). 앱(`AndroidWindow::getApplication`)이 없으면 `isValid` 가 false 다. */
        AndroidJNIScope();
        /** @brief 남은 예외를 지우고 지역 참조 틀을 닫습니다. 스레드는 떼지 않는다(스레드가 끝날 때 뗀다). */
        ~AndroidJNIScope();
        AndroidJNIScope( const AndroidJNIScope& )            = delete;
        AndroidJNIScope& operator=( const AndroidJNIScope& ) = delete;

        bool    isValid() const { return _pEnv != nullptr; }
        JNIEnv* getEnv() const { return _pEnv; }
        /** @brief 액티비티(`ANativeActivity::clazz`)입니다. */
        jobject getActivity() const { return _activity; }
        /** @brief 이 범위에서 예외가 한 번이라도 났으면 true 입니다(진단 로그용). */
        bool hadException() const { return _bHadException != 0; }

        /** @brief 예외가 있으면 지우고(logcat 에 스택) true 입니다. */
        bool takeException() const;

        /** @brief 시스템 클래스(`java/lang/String` 꼴)입니다. */
        jclass findClass( const utf8* pSlashName ) const;
        /** @brief 앱 dex 의 클래스(`com.swengine.engine.SWInputView` 꼴)를 액티비티 클래스 로더로 찾습니다 — 붙인 네이티브 스레드의 `FindClass` 는 시스템 로더라 찾지 못한다. */
        jclass    loadAppClass( const utf8* pDottedName ) const;
        jclass    getObjectClass( jobject object ) const;
        jmethodID getMethod( jclass classObject, const utf8* pName, const utf8* pSignature ) const;
        jmethodID getStaticMethod( jclass classObject, const utf8* pName, const utf8* pSignature ) const;
        jfieldID  getStaticField( jclass classObject, const utf8* pName, const utf8* pSignature ) const;
        jobject   getStaticObjectField( jclass classObject, jfieldID field ) const;
        /** @brief UTF-8 글로 Java 글을 만듭니다(`AndroidJNIString::toJava` + 예외 확인). */
        jstring newString( string_view text ) const;

        template <typename... TArgs>
        jobject callObject( jobject object, jmethodID method, TArgs... args ) const
        {
            if ( _pEnv == nullptr || object == nullptr || method == nullptr )
                return nullptr;
            const jobject result = _pEnv->CallObjectMethod( object, method, args... );
            return takeException() ? nullptr : result;
        }
        template <typename... TArgs>
        jobject callStaticObject( jclass classObject, jmethodID method, TArgs... args ) const
        {
            if ( _pEnv == nullptr || classObject == nullptr || method == nullptr )
                return nullptr;
            const jobject result = _pEnv->CallStaticObjectMethod( classObject, method, args... );
            return takeException() ? nullptr : result;
        }
        template <typename... TArgs>
        jobject newObject( jclass classObject, jmethodID constructor, TArgs... args ) const
        {
            if ( _pEnv == nullptr || classObject == nullptr || constructor == nullptr )
                return nullptr;
            const jobject result = _pEnv->NewObject( classObject, constructor, args... );
            return takeException() ? nullptr : result;
        }
        /** @brief int 를 돌려주는 메서드입니다. 못 불렀거나 예외면 빈 값이다. */
        template <typename... TArgs>
        optional<jint> callInt( jobject object, jmethodID method, TArgs... args ) const
        {
            if ( _pEnv == nullptr || object == nullptr || method == nullptr )
                return nullopt;
            const jint result = _pEnv->CallIntMethod( object, method, args... );
            if ( takeException() )
                return nullopt;
            return result;
        }
        /** @brief 돌려주는 것이 없는 메서드입니다. 불렀고 예외가 없으면 true 다. */
        template <typename... TArgs>
        bool callVoid( jobject object, jmethodID method, TArgs... args ) const
        {
            if ( _pEnv == nullptr || object == nullptr || method == nullptr )
                return false;
            _pEnv->CallVoidMethod( object, method, args... );
            return takeException() == false;
        }

    private:
        JNIEnv*                _pEnv;
        jobject                _activity;
        mutable uint8          _bHadException : 1;
        [[maybe_unused]] uint8 _reserved      : 7;
    };
} // namespace sw
