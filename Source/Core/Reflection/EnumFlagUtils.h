#pragma once
/**
 * @file EnumFlagUtils.h
 * @brief enum 플래그 연산 정의
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

namespace sw
{

	template <typename T>
	struct EnableEnumFlags : std::false_type
	{
	};

	template <typename T>
	SW_INLINE constexpr std::enable_if_t<EnableEnumFlags<T>::value, bool> hasFlag( T flags, T contains )
	{
		using Underlying = std::underlying_type_t<T>;
		return ( static_cast<Underlying>( flags ) & static_cast<Underlying>( contains ) ) == static_cast<Underlying>( contains );
	}
}

/** @brief SW_ENUM_FLAGS 매크로 정의입니다. */
#define SW_ENUM_FLAGS( EnumType )                                                                        \
	template <>                                                                                          \
	struct sw::EnableEnumFlags<EnumType> : std::true_type                                                \
	{                                                                                                    \
	};                                                                                                   \
	SW_INLINE constexpr EnumType operator|( EnumType lhs, EnumType rhs )                                 \
	{                                                                                                    \
		using Underlying = std::underlying_type_t<EnumType>;                                             \
		return static_cast<EnumType>( static_cast<Underlying>( lhs ) | static_cast<Underlying>( rhs ) ); \
	}                                                                                                    \
	SW_INLINE constexpr EnumType operator&( EnumType lhs, EnumType rhs )                                 \
	{                                                                                                    \
		using Underlying = std::underlying_type_t<EnumType>;                                             \
		return static_cast<EnumType>( static_cast<Underlying>( lhs ) & static_cast<Underlying>( rhs ) ); \
	}                                                                                                    \
	SW_INLINE constexpr EnumType operator^( EnumType lhs, EnumType rhs )                                 \
	{                                                                                                    \
		using Underlying = std::underlying_type_t<EnumType>;                                             \
		return static_cast<EnumType>( static_cast<Underlying>( lhs ) ^ static_cast<Underlying>( rhs ) ); \
	}                                                                                                    \
	SW_INLINE constexpr EnumType operator~( EnumType val )                                               \
	{                                                                                                    \
		using Underlying = std::underlying_type_t<EnumType>;                                             \
		return static_cast<EnumType>( ~static_cast<Underlying>( val ) );                                 \
	}                                                                                                    \
	SW_INLINE constexpr EnumType& operator|=( EnumType& lhs, EnumType rhs )                              \
	{                                                                                                    \
		return lhs = lhs | rhs;                                                                          \
	}                                                                                                    \
	SW_INLINE constexpr EnumType& operator&=( EnumType& lhs, EnumType rhs )                              \
	{                                                                                                    \
		return lhs = lhs & rhs;                                                                          \
	}                                                                                                    \
	SW_INLINE constexpr EnumType& operator^=( EnumType& lhs, EnumType rhs )                              \
	{                                                                                                    \
		return lhs = lhs ^ rhs;                                                                          \
	}
