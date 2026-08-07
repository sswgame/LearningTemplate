#pragma once
/**
 * @file ReflectionCast.h
 * @brief Reflection-aware cast helpers (HasStaticType / castTo)
 */

#include "Core/CoreMinimal.h"
#include "Core/Reflection/TypeRegistry.h"

namespace sw
{

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

	template <typename To, typename From>
	To* castTo( From* src )
	{
		if ( src == nullptr )
			return nullptr;

		if constexpr ( HasStaticType_v<To> )
		{
			const TypeInfo* srcType = src->getTypeInfo();
			if ( srcType != nullptr && srcType->isA( To::StaticType()._fullyQualifiedName ) )
				return static_cast<To*>( src );
			return nullptr;
		}
		else
		{
			return static_cast<To*>( src );
		}
	}

}
