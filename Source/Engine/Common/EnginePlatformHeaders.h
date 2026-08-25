/**
 * @file EnginePlatformHeaders.h
 * @brief OS headers (PlatformOsHeaders) + graphics API system headers (Core/RHI).
 * @note Third Party(vulkan, glad, imgui 등)는 여기 넣지 않습니다. 사용처에서 직접 include 합니다.
 */
#pragma once
#include "Core/Common/PlatformOsHeaders.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include <d3d11.h>
	#include <d3d11shader.h>
	#include <d3d12.h>
	#include <d3d12shader.h>
	#include <d3dcompiler.h>
	#include <dxcapi.h>
	#include <dxgi1_4.h>
	#define SW_HAS_DXC_API 1

#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
	#if __has_include( <dxcapi.h> )
		#include <dxcapi.h>
		#define SW_HAS_DXC_API 1
	#endif
#endif
