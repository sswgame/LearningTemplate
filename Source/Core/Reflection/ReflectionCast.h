#pragma once
/**
 * @file ReflectionCast.h
 * @brief Reflection-aware cast helpers (HasStaticType / castTo)
 */

#include "Core/CoreMinimal.h"
#include "Core/Reflection/TypeRegistry.h"

namespace sw
{

	/**
	 * @brief Codegen이 REFLECT 타입마다 특수화합니다 (T::StaticType 없이 FQN 조회).
	 */
	template <typename T>
	struct ReflectTypeTraits
	{
	};

	template <typename T, typename = void>
	struct HasStaticType : std::false_type
	{
	};

	template <typename T>
	struct HasStaticType<T, std::void_t<decltype( T::StaticType() )>> : std::true_type
	{
	};

	template <typename T>
	inline constexpr bool HasStaticType_v = HasStaticType<T>::value;

	template <typename T, typename = void>
	struct HasReflectStaticType : std::false_type
	{
	};

	template <typename T>
	struct HasReflectStaticType<T, std::void_t<decltype( ReflectTypeTraits<T>::StaticType() )>> : std::true_type
	{
	};

	template <typename T>
	inline constexpr bool HasReflectStaticType_v = HasReflectStaticType<T>::value;

	template <typename To, typename From>
	To* castTo( From* src )
	{
		if ( src == nullptr )
			return nullptr;

		const TypeInfo* toType = nullptr;
		if constexpr ( HasStaticType_v<To> )
		{
			toType = To::StaticType();
		}
		else if constexpr ( HasReflectStaticType_v<To> )
		{
			toType = ReflectTypeTraits<To>::StaticType();
		}
		else
		{
			return static_cast<To*>( src );
		}

		const TypeInfo* srcType = src->getTypeInfo();
		if ( srcType != nullptr && toType != nullptr && srcType->isA( toType->_fullyQualifiedName ) )
			return static_cast<To*>( src );
		return nullptr;
	}

} // namespace sw
