/**
 * @file GameFrameworkExports.h
 * @brief GameFramework SHARED DLL용 SW_GF_API
 */
#pragma once
#include "Core/Common/Macros.h"

// ------------------------------------------------------------------------------
// 1) SW_GF_API — GameFramework SHARED 내보내기
//    Windows: dllexport/dllimport, 그 외: default visibility
//    STATIC(Shipping)에서는 빈 매크로
// ------------------------------------------------------------------------------

#if defined( SW_PLATFORM_WINDOWS )
	#if defined( SW_GF_EXPORTS )
		#define SW_GF_API __declspec( dllexport )
	#elif defined( SW_GF_IMPORTS )
		#define SW_GF_API __declspec( dllimport )
	#else
		#define SW_GF_API
	#endif
#else
	#define SW_GF_API __attribute__( ( visibility( "default" ) ) )
#endif

namespace sw
{
	SW_GF_API void registerGameFrameworkTypes();
} // namespace sw
