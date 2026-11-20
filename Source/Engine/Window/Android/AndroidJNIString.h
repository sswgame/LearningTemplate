/**
 * @file AndroidJNIString.h
 * @brief JNI 글(Java `String`)과 엔진 글(UTF-8)을 오가는 한 곳입니다. 모든 Android JNI 입구 · 출구가 이것만 쓴다.
 * @details JNI 의 `GetStringUTFChars` · `NewStringUTF` 는 수정 UTF-8(CESU-8 + NUL 은 2 바이트)이라 평면 밖 글자(이모지 · 일부 한자)를 3 바이트 두 개로 내고,
 *          4 바이트 UTF-8 을 받으면 깨뜨린다. 그래서 UTF-16 단위(`GetStringChars` · `NewString`)로 옮기고 변환은 `StringUtil::utf16UnitsToUTF8` · `utf8ToUTF16Units`
 *          (데스크톱 단위 테스트가 덮는다) 하나다. 언리얼 `FJavaHelper::FStringFromLocalRef` 도 UTF-16 으로 받는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include <jni.h>

namespace sw
{
    /** @struct AndroidJNIString @brief Java 글 ↔ UTF-8 변환입니다. */
    struct AndroidJNIString
    {
        /** @brief Java 글 @p text 를 UTF-8 로 옮깁니다. nullptr · 못 읽음이면 빈 글이다. 짝 없는 서로게이트는 U+FFFD 다. */
        static string toUTF8( JNIEnv* pEnv, jstring text );
        /** @brief UTF-8 @p text 로 Java 글(지역 참조)을 만듭니다. 못 만들면 nullptr 이다(예외 확인은 부른 쪽 — 다른 JNI 호출과 같다). */
        static jstring toJava( JNIEnv* pEnv, string_view text );
    };
} // namespace sw
