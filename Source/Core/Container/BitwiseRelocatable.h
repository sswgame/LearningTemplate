/**
 * @file BitwiseRelocatable.h
 * @brief "옮긴 뒤 원본을 소멸시키는 일" 을 바이트 복사 한 번으로 할 수 있는 형식인지 판정합니다 — `vector` · `deque` 의 키우기 · 밀기가 씁니다.
 * @details 언리얼 `TArray` 는 모든 원소를 `memmove` 로 옮긴다(`TIsBitwiseRelocatable` 이 기본 참). 우리는 자기 주소를 든 형식(`small_vector` · 침입형 목록 노드 · 구독)이
 *          있어 기본을 "복사에도 소멸에도 사용자 코드가 없는 형식" 으로 두고, 소유 포인터뿐인 형식만 특수화로 올린다 — 올리지 않은 형식은 원소마다 이동 생성 · 소멸이다.
 *          특수화는 그 형식을 정의한 헤더에 둔다(번역 단위마다 판정이 갈리면 ODR 위반이다). 판정은 형식의 정의가 끝난 곳(함수 본문)에서만 본다.
 *          올릴 수 있는 조건: 객체의 어느 칸도 객체 자신의 주소를 담지 않고, 밖의 어느 것도 이 객체의 주소를 쥐지 않는다.
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"

#include <memory>
#include <type_traits>

namespace sw
{
    /** @brief 바이트 복사로 옮겨도 되는 형식이면 참입니다. 파일 머리말 참고. */
    template <typename T>
    struct is_bitwise_relocatable : std::bool_constant<std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>>
    {
    };

    /** @brief 해제자가 비어 있고 포인터가 맨 포인터면 포인터 한 칸이다. */
    template <typename T, typename Deleter>
    struct is_bitwise_relocatable<std::unique_ptr<T, Deleter>> : std::bool_constant<std::is_empty_v<Deleter> && std::is_pointer_v<typename std::unique_ptr<T, Deleter>::pointer>>
    {
    };

    /** @brief 포인터 두 칸(객체 · 제어 블록)이다. */
    template <typename T>
    struct is_bitwise_relocatable<std::shared_ptr<T>> : std::true_type
    {
    };

    /** @brief 포인터 두 칸(객체 · 제어 블록)이다. */
    template <typename T>
    struct is_bitwise_relocatable<std::weak_ptr<T>> : std::true_type
    {
    };

    template <typename T>
    inline constexpr bool is_bitwise_relocatable_v = is_bitwise_relocatable<T>::value;
} // namespace sw
