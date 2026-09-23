/**
 * @file EnumUtil.h
 * @brief 비트플래그 enum 을 검사하고 조합하는 도우미입니다. 전부 static · constexpr 이고 리플렉션(TypeRegistry)을 조회하지 않습니다.
 *
 * @note 예전에는 `StdHeaders.h`(표준 헤더 48개, `<regex>` · `<random>` · `<iostream>` 포함)를 끌어왔지만, 이 파일이 쓰는 것은
 *       `<type_traits>` 의 넷(`is_enum_v` · `underlying_type_t` · `enable_if_t` · `false_type`)뿐입니다. 비트플래그 연산자를
 *       쓰려고 이 헤더를 include 한 쪽이 컴파일이 무거운 `<regex>` 까지 함께 끌어오고 있었습니다.
 */
#pragma once
#include "Core/Common/Macros.h"

#include <type_traits>

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) EnumUtil — 비트플래그 enum 검사/조합 (전부 static, constexpr)
    // ------------------------------------------------------------------------------
    /**
     * @struct EnumUtil
     * @brief 비트플래그로 쓰는 enum class 를 다루는 컴파일 타임 도우미입니다.
     * @details `TypeRegistry::hasFlag<E>()` 와 달리 리플렉션 레지스트리를 조회하지 않아 자주 도는 경로에서도 부담이 없습니다.
     *          기반 정수 타입의 비트 연산만 하고 전부 constexpr 이라 컴파일 타임에 계산될 수 있습니다. ENUM(Flags) 로
     *          선언했는지와 관계없이 아무 enum class 에나 쓸 수 있습니다.
     */
    struct EnumUtil
    {
        /** @brief `contains` 의 비트가 `flags` 에 모두 켜져 있으면 true 입니다. */
        template <typename E>
        static constexpr bool hasFlag( E flags, E contains ) noexcept
        {
            static_assert( std::is_enum_v<E>, "EnumUtil::hasFlag requires an enum type" );
            using Underlying = std::underlying_type_t<E>;
            return ( static_cast<Underlying>( flags ) & static_cast<Underlying>( contains ) ) == static_cast<Underlying>( contains );
        }

        /** @brief `mask` 의 비트 중 하나라도 `flags` 에 켜져 있으면 true 입니다. */
        template <typename E>
        static constexpr bool hasAnyFlag( E flags, E mask ) noexcept
        {
            static_assert( std::is_enum_v<E>, "EnumUtil::hasAnyFlag requires an enum type" );
            using Underlying = std::underlying_type_t<E>;
            return ( static_cast<Underlying>( flags ) & static_cast<Underlying>( mask ) ) != Underlying{ 0 };
        }

        /** @brief `flag` 비트를 켠 값을 반환합니다(`flags | flag` 와 같습니다). */
        template <typename E>
        static constexpr E setFlag( E flags, E flag ) noexcept
        {
            static_assert( std::is_enum_v<E>, "EnumUtil::setFlag requires an enum type" );
            using Underlying = std::underlying_type_t<E>;
            return static_cast<E>( static_cast<Underlying>( flags ) | static_cast<Underlying>( flag ) );
        }

        /** @brief `flag` 비트를 끈 값을 반환합니다(`flags & ~flag` 와 같습니다). */
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
     * @brief ENUM(Flags) 로 선언한 타입만 true 로 특수화됩니다(ReflectionParser 가 enum 하나마다 한 줄씩 생성합니다).
     * @details 아래 |, &, ^, ~, |=, &=, ^= 연산자는 이 트레이트가 true 인 타입에만 열려 있습니다. 그래서 opt-in 하지 않은
     *          enum class 에는 적용되지 않고, `Color::Red | Color::Green` 같은 뜻 없는 조합은 여전히 컴파일 오류입니다.
     */
    template <typename E>
    struct IsBitFlagEnum : std::false_type
    {
    };

    // ------------------------------------------------------------------------------
    // 3) 비트플래그 연산자 — sw::IsBitFlagEnum<E>로 opt-in 된 타입에만 적용
    // ------------------------------------------------------------------------------
    /**
     * @details 연산자를 `namespace sw` 안에 정의하고 전역에는 using 으로 다시 내놓는 이유는 이름 조회 규칙 때문입니다.
     *          한정하지 않은 연산자 호출은 (a) 호출한 자리에서 바깥쪽 스코프로 훑어 나가는 일반 조회와 (b) 인자 타입이 속한
     *          네임스페이스를 보는 ADL 을 함께 씁니다.
     *          - 전역에만 두면: `Engine/Graphics/RHI/RHITypes.h` 가 이미 `sw::operator|(RHIBufferUsage, ...)` 를 갖고 있어서,
     *            `sw::editor::PanelFlags` 처럼 sw 안쪽에 있는 enum 은 (a) 가 sw 에서 그 이름을 찾는 순간 멈춥니다. C++ 조회는
     *            같은 이름을 찾으면 후보가 맞지 않아도 더 바깥(전역)으로 가지 않으므로 전역 연산자를 끝내 보지 못합니다.
     *          - sw 안에만 두면: 네임스페이스 없이 전역에 선언한 enum(TestFlag 등)은 (a) 도 (b) 도 sw 를 보지 않아 찾지 못합니다.
     *          그래서 정의는 여기 한 번만 두고 전역에는 아래에서 using 선언으로 끌어와, 로직을 겹치지 않고 두 경우를 모두
     *          덮습니다. `IsBitFlagEnum<E>` 로 opt-in 하지 않은 타입은 SFINAE 로 후보에서 빠지므로 `RHIBufferUsage` 같은 다른
     *          `operator|` 와 함께 있어도 부딪히지 않습니다.
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
// 4) 전역 스코프로 다시 내놓기 — 네임스페이스 없이 선언한 enum(예: 전역 TestFlag)용
// ------------------------------------------------------------------------------
/** @brief 위 sw:: 연산자를 그대로 끌어옵니다. 로직은 여기 없고 전부 sw:: 쪽에만 있습니다. */
using sw::operator|;
using sw::operator&;
using sw::operator^;
using sw::operator~;
using sw::operator|=;
using sw::operator&=;
using sw::operator^=;
