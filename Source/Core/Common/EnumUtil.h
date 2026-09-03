/**
 * @file EnumUtil.h
 * @brief 비트플래그 enum 검사/조합 유틸 (전부 static, constexpr — 리플렉션/TypeRegistry 조회 없음)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) EnumUtil — 비트플래그 enum 검사/조합 (전부 static, constexpr)
    // ------------------------------------------------------------------------------
    /**
     * @struct EnumUtil
     * @brief 비트플래그로 쓰이는 enum class를 다루는 순수 컴파일 타임 유틸리티입니다.
     * @details TypeRegistry::hasFlag<E>()와 달리 리플렉션 레지스트리 조회가 전혀 없어 핫패스에서도
     *          안전하게 쓸 수 있습니다 — 언더라잉 정수 타입에 대한 비트 연산으로만 동작하며, 전부
     *          constexpr이라 컴파일 타임에 완전히 평가될 수 있습니다. ENUM(Flags) 매크로 여부와도
     *          무관하게 임의의 enum class에 바로 적용 가능합니다.
     */
    struct EnumUtil
    {
        /** @brief contains의 모든 비트가 flags에 설정되어 있으면 true. */
        template <typename E>
        static constexpr bool hasFlag( E flags, E contains ) noexcept
        {
            static_assert( std::is_enum_v<E>, "EnumUtil::hasFlag requires an enum type" );
            using Underlying = std::underlying_type_t<E>;
            return ( static_cast<Underlying>( flags ) & static_cast<Underlying>( contains ) ) == static_cast<Underlying>( contains );
        }

        /** @brief mask의 비트 중 하나라도 flags에 설정되어 있으면 true. */
        template <typename E>
        static constexpr bool hasAnyFlag( E flags, E mask ) noexcept
        {
            static_assert( std::is_enum_v<E>, "EnumUtil::hasAnyFlag requires an enum type" );
            using Underlying = std::underlying_type_t<E>;
            return ( static_cast<Underlying>( flags ) & static_cast<Underlying>( mask ) ) != Underlying{ 0 };
        }

        /** @brief flag 비트를 켠 값을 반환합니다 (flags | flag와 동일). */
        template <typename E>
        static constexpr E setFlag( E flags, E flag ) noexcept
        {
            static_assert( std::is_enum_v<E>, "EnumUtil::setFlag requires an enum type" );
            using Underlying = std::underlying_type_t<E>;
            return static_cast<E>( static_cast<Underlying>( flags ) | static_cast<Underlying>( flag ) );
        }

        /** @brief flag 비트를 끈 값을 반환합니다 (flags & ~flag와 동일). */
        template <typename E>
        static constexpr E clearFlag( E flags, E flag ) noexcept
        {
            static_assert( std::is_enum_v<E>, "EnumUtil::clearFlag requires an enum type" );
            using Underlying = std::underlying_type_t<E>;
            return static_cast<E>( static_cast<Underlying>( flags ) & static_cast<Underlying>( ~static_cast<Underlying>( flag ) ) );
        }
    };

    // ------------------------------------------------------------------------------
    // 2) IsBitFlagEnum — ENUM(Flags) opt-in 트레이트 (기본 false)
    // ------------------------------------------------------------------------------
    /**
     * @brief ENUM(Flags)로 선언된 타입만 true로 특수화됩니다 (ReflectionParser가 enum 하나당 한 줄만
     *        코드젠합니다). 아래 |, &, ^, ~, |=, &=, ^= 연산자가 이 트레이트로 게이트되어 있어,
     *        opt-in 하지 않은 일반 enum class에는 절대 적용되지 않습니다 — `Color::Red | Color::Green`
     *        같은 의미 없는 조합은 여전히 컴파일 에러입니다.
     */
    template <typename E>
    struct IsBitFlagEnum : std::false_type
    {
    };

    // ------------------------------------------------------------------------------
    // 3) 비트플래그 연산자 — sw::IsBitFlagEnum<E>로 opt-in 된 타입에만 적용
    // ------------------------------------------------------------------------------
    /**
     * @details namespace sw 안에 두는 이유: unqualified 연산자 조회는 (a) 호출부 스코프를 바깥으로
     *          훑는 일반 lookup과 (b) 인자 타입의 연관 네임스페이스를 보는 ADL을 함께 씁니다. 이미
     *          `Engine/Graphics/RHI/RHITypes.h`가 `sw::operator|(RHIBufferUsage, ...)`를 갖고 있어서,
     *          `sw::editor::PanelFlags`처럼 sw 하위에 중첩된 enum에 대한 (a) 훑기가 sw에서 그 이름을
     *          "발견"하는 순간 더 바깥(전역)까지 못 가고 멈춥니다(이름이 같으면 후보가 안 맞아도 멈추는
     *          C++ lookup 규칙) — 그래서 전역 스코프에만 선언하면 sw 하위 중첩 enum에서 못 찾습니다.
     *          반대로 sw 스코프에만 두면, 전역(네임스페이스 없음)에 선언된 enum(TestFlag 등)에서는
     *          일반 lookup도 ADL도 sw를 보지 않아 못 찾습니다. 그래서 실제 정의는 여기 한 번만 두고,
     *          전역 스코프에는 아래에서 using 선언으로 그대로 끌어옵니다 — 로직 중복 없이 두 경우 모두
     *          커버합니다. IsBitFlagEnum<E>로 opt-in 되지 않은 타입에는 SFINAE로 후보에서 제외되므로
     *          RHIBufferUsage 등 다른 operator|와 공존해도 충돌하지 않습니다.
     */
    template <typename E, typename = std::enable_if_t<IsBitFlagEnum<E>::value>>
    SW_INLINE constexpr E operator|( E lhs, E rhs ) noexcept
    {
        return EnumUtil::setFlag( lhs, rhs );
    }

    template <typename E, typename = std::enable_if_t<IsBitFlagEnum<E>::value>>
    SW_INLINE constexpr E operator&( E lhs, E rhs ) noexcept
    {
        using Underlying = std::underlying_type_t<E>;
        return static_cast<E>( static_cast<Underlying>( lhs ) & static_cast<Underlying>( rhs ) );
    }

    template <typename E, typename = std::enable_if_t<IsBitFlagEnum<E>::value>>
    SW_INLINE constexpr E operator^( E lhs, E rhs ) noexcept
    {
        using Underlying = std::underlying_type_t<E>;
        return static_cast<E>( static_cast<Underlying>( lhs ) ^ static_cast<Underlying>( rhs ) );
    }

    template <typename E, typename = std::enable_if_t<IsBitFlagEnum<E>::value>>
    SW_INLINE constexpr E operator~( E val ) noexcept
    {
        using Underlying = std::underlying_type_t<E>;
        return static_cast<E>( ~static_cast<Underlying>( val ) );
    }

    template <typename E, typename = std::enable_if_t<IsBitFlagEnum<E>::value>>
    SW_INLINE constexpr E& operator|=( E& lhs, E rhs ) noexcept
    {
        return lhs = lhs | rhs;
    }

    template <typename E, typename = std::enable_if_t<IsBitFlagEnum<E>::value>>
    SW_INLINE constexpr E& operator&=( E& lhs, E rhs ) noexcept
    {
        return lhs = lhs & rhs;
    }

    template <typename E, typename = std::enable_if_t<IsBitFlagEnum<E>::value>>
    SW_INLINE constexpr E& operator^=( E& lhs, E rhs ) noexcept
    {
        return lhs = lhs ^ rhs;
    }

} // namespace sw

// ------------------------------------------------------------------------------
// 4) 전역 스코프로 재노출 — 네임스페이스 없이 선언된 enum(예: 전역 TestFlag)용
// ------------------------------------------------------------------------------
/** @brief 위 sw:: 연산자를 그대로 끌어옵니다. 로직은 여기 없고 전부 sw:: 쪽에만 있습니다. */
using sw::operator|;
using sw::operator&;
using sw::operator^;
using sw::operator~;
using sw::operator|=;
using sw::operator&=;
using sw::operator^=;
