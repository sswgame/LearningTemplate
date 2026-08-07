#pragma once
/**
 * @file ReflectionMacros.h
 * @brief REFLECT/PROPERTY/FUNCTION/ENUM annotation macros and IPropertyObserver
 */

#include "Core/CoreMinimal.h"

#if defined( __REFLECT_PARSER__ )
	#define REFLECT( ... )	__attribute__( ( annotate( "REFLECT;" #__VA_ARGS__ ) ) )
	#define PROPERTY( ... ) __attribute__( ( annotate( "PROPERTY;" #__VA_ARGS__ ) ) )
	#define FUNCTION( ... ) __attribute__( ( annotate( "FUNCTION;" #__VA_ARGS__ ) ) )
	#define ENUM( ... )		__attribute__( ( annotate( "ENUM;" #__VA_ARGS__ ) ) )
#else

	#define REFLECT( ... )
	#define PROPERTY( ... )
	#define FUNCTION( ... )
	#define ENUM( ... )
#endif

namespace sw
{

	class IPropertyObserver
	{
	public:
		virtual ~IPropertyObserver() = default;
		/**
		 * @brief 프로퍼티 변경 콜백을 호출합니다
		 */
		virtual void onPropertyChanged( const hashed_string& propertyName ) = 0;
	};

} // namespace sw
