/**
 * @file ReflectionCast.h
 * @brief Reflection-aware cast helpers (HasStaticType / castTo)
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{

	/**
	 * @brief Codegen이 REFLECT 타입마다 특수화합니다 (T::StaticType 없이 FQN 조회).
	 */
	template <typename T>
	/// @brief codegen이 REFLECT 타입마다 특수화 (T::StaticType 없이 FQN 조회)
	struct ReflectTypeTraits
	{
	};

	template <typename T, typename = void>
	/// @brief T::StaticType()이 없으면 false
	struct HasStaticType : std::false_type
	{
	};

	template <typename T>
	/// @brief T::StaticType()이 있으면 true
	struct HasStaticType<T, std::void_t<decltype( T::StaticType() )>> : std::true_type
	{
	};

	template <typename T>
	inline constexpr bool HasStaticType_v = HasStaticType<T>::value;

	template <typename T, typename = void>
	/// @brief ReflectTypeTraits<T>::StaticType()이 없으면 false
	struct HasReflectStaticType : std::false_type
	{
	};

	template <typename T>
	/// @brief ReflectTypeTraits<T>::StaticType()이 있으면 true
	struct HasReflectStaticType<T, std::void_t<decltype( ReflectTypeTraits<T>::StaticType() )>> : std::true_type
	{
	};

	template <typename T>
	inline constexpr bool HasReflectStaticType_v = HasReflectStaticType<T>::value;

	template <typename T, typename = void>
	/// @brief Has Own Reflect Body (checks if T declares its own REFLECT_BODY rather than slicing to a base class)
	struct HasOwnReflectBody : std::false_type
	{
	};

	template <typename T>
	struct HasOwnReflectBody<T, std::void_t<decltype( std::declval<const T>().swReflectSelf() )>>
		: std::is_same<std::remove_cv_t<std::remove_pointer_t<decltype( std::declval<const T>().swReflectSelf() )>>, T>
	{
	};

	template <typename T>
	inline constexpr bool HasOwnReflectBody_v = HasOwnReflectBody<T>::value;

	template <typename To, typename From>
	/** @brief TypeInfo 상속 체인을 보고 To*로 캐스트. 실패 시 nullptr. */
	To* castTo( From* pSrc )
	{
		if ( pSrc == nullptr )
			return nullptr;

		const TypeInfo* pToType = nullptr;
		if constexpr ( HasStaticType_v<To> )
			pToType = To::StaticType();
		else if constexpr ( HasReflectStaticType_v<To> )
			pToType = ReflectTypeTraits<To>::StaticType();
		else
			return static_cast<To*>( pSrc );

		const TypeInfo* pSrcType = pSrc->getTypeInfo();
		if ( pSrcType != nullptr && pToType != nullptr && pSrcType->isDerivedFrom( pToType->_fullyQualifiedName ) )
			return static_cast<To*>( pSrc );
		return nullptr;
	}

} // namespace sw
