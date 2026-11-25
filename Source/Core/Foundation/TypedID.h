/**
 * @file TypedID.h
 * @brief 쓰임(태그)마다 다른 정수 번호 값 형식입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include <type_traits>

namespace sw
{
    /**
     * @class TypedID
     * @brief 쓰임(@p TTag)마다 다른 번호 값 형식입니다. 크기 · 복사 비용은 @p TValue 와 같다.
     * @details 번호가 모두 정수 별칭이면 재생 id 를 에미터 자리에, 글리프 번호를 면 자리에 넘겨도 컴파일된다(RHI 핸들 · bindless 번호에서 되풀이된 결함 — `RHIHandle` 이 같은 일을 막는다).
     *          번호끼리 · 정수와 번호 사이에 암묵 변환이 없다. 정수에서 만들 때는 명시로만, 값을 꺼낼 때는 `getValue` 로만 한다(로그 · 표의 자리 · 백엔드 경계).
     *          기본 생성은 없음(@p kNoneValue)이다.
     *          언리얼 `TStrongTypedef` · Godot `RID` 와 같은 자리다.
     */
    template <typename TTag, typename TValue, TValue kNoneValue = TValue{ 0 }>
    class TypedID
    {
        static_assert( std::is_integral_v<TValue>, "TypedID wraps an integer" );

    public:
        using ValueType = TValue;

        constexpr TypedID() noexcept = default;
        /** @brief 정수에서 만듭니다. 명시로만 부른다 — 번호를 내는 쪽 · 테스트가 쓴다. */
        template <typename TInt, std::enable_if_t<std::is_integral_v<TInt> && std::is_same_v<TInt, bool> == false, int> = 0>
        constexpr explicit TypedID( TInt value ) noexcept
            : _value( static_cast<TValue>( value ) )
        {
        }

        [[nodiscard]] constexpr TValue getValue() const noexcept { return _value; }
        [[nodiscard]] constexpr bool   isValid() const noexcept { return _value != kNoneValue; }
        /** @brief 해시 컨테이너 키로 쓸 때의 해시입니다(`DefaultHash` 가 부른다). */
        [[nodiscard]] constexpr uint64 computeHash() const noexcept { return static_cast<uint64>( _value ); }

        friend constexpr bool operator==( TypedID lhs, TypedID rhs ) noexcept { return lhs._value == rhs._value; }
        friend constexpr bool operator!=( TypedID lhs, TypedID rhs ) noexcept { return lhs._value != rhs._value; }
        friend constexpr bool operator<( TypedID lhs, TypedID rhs ) noexcept { return lhs._value < rhs._value; }

    private:
        TValue _value{ kNoneValue };
    };
} // namespace sw
