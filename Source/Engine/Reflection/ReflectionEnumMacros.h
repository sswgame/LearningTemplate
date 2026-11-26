/**
 * @file ReflectionEnumMacros.h
 * @brief 열거형 표식(`ENUM` · `ENUMERATOR`)만 둔 잎 헤더입니다. 아무것도 include 하지 않아 엔진 pch(`EngineMinimal.h`)가 끈다.
 * @details 열거형 하나에 이름 · 표시 메타를 붙이려고 `ReflectionMacros.h`(이름 캐시 · REFLECT_BODY)를 끌면 헤더 포함 상한에 걸리는 헤더(고정 배치 · 온라인 클라이언트)가 있다.
 *          언리얼 `ObjectMacros.h` 의 `UENUM` · `UMETA` 가 어느 헤더에서나 보이는 것과 같은 자리다. 편집기 키 완성(clangd)은 `ReflectionMacros.h` 를 읽은 곳에서만 켜진다.
 */
#pragma once

#if defined( __REFLECT_PARSER__ )
    /** @brief 리플렉션 대상 열거형을 표시합니다(파서 모드의 표식 선언 — `ReflectionMacros.h` 의 REFLECT 와 같은 꼴). */
    #define ENUM( ... ) static_assert( true, "ENUM;" #__VA_ARGS__ );
    /** @brief 열거자 하나의 표시 메타입니다(언리얼 `UMETA`). 열거자 이름 뒤에 적습니다: `Red ENUMERATOR( DisplayName = "빨강", Hidden ),` */
    #define ENUMERATOR( ... ) __attribute__( ( annotate( "ENUMERATOR;" #__VA_ARGS__ ) ) )
#else
    #define ENUM( ... )
    #define ENUMERATOR( ... )
#endif
