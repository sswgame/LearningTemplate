/**
 * @file PlatformOsHeaders.h
 * @brief OS 시스템 헤더만 포함합니다. DirectX/Vulkan/DXC는 넣지 않습니다.
 * @note 그래픽 API 헤더는 Core/Common/EnginePlatformHeaders.h에 있습니다.
 */
#pragma once

// ------------------------------------------------------------------------------
// 1) Windows — NOMINMAX / WIN32_LEAN_AND_MEAN 후 SDK 헤더
// ------------------------------------------------------------------------------
#if defined( SW_PLATFORM_WINDOWS ) || defined( _WIN32 ) || defined( _WIN64 )
	#if !defined( NOMINMAX )
		/** @brief Windows.h 의 min/max 매크로를 막습니다. */
		#define NOMINMAX
	#endif
	#if !defined( WIN32_LEAN_AND_MEAN )
		/** @brief 잘 안 쓰는 Windows API 를 빼 컴파일 시간을 줄입니다. */
		#define WIN32_LEAN_AND_MEAN
	#endif

	#include <Unknwn.h>
	#include <Windows.h>
	#include <commdlg.h>
	#include <intrin.h>
	#include <sdkddkver.h>
	#include <wrl/client.h>

// ------------------------------------------------------------------------------
// 2) POSIX — Linux / macOS 공통 + 플랫폼별
// ------------------------------------------------------------------------------
#elif defined( SW_PLATFORM_LINUX ) || defined( __linux__ ) || defined( SW_PLATFORM_MACOS ) || defined( __APPLE__ )
	#include <dirent.h>
	#include <dlfcn.h>
	#include <pthread.h>
	#include <sched.h>
	#include <sys/mman.h>
	#include <sys/select.h>
	#include <sys/stat.h>
	#include <sys/time.h>
	#include <sys/types.h>
	#include <unistd.h>

	#if defined( SW_PLATFORM_LINUX )
		#include <X11/Xlib.h>
		#include <X11/Xutil.h>
		#include <sys/eventfd.h>
		#include <sys/inotify.h>

		#if defined( None )
			#undef None
		#endif
		#if defined( Bool )
			#undef Bool
		#endif
		#if defined( Status )
			#undef Status
		#endif
		#if defined( Success )
			#undef Success
		#endif
		#if defined( Always )
			#undef Always
		#endif
		#if defined( Above )
			#undef Above
		#endif
		#if defined( Below )
			#undef Below
		#endif
		#if defined( Complex )
			#undef Complex
		#endif
		#if defined( True )
			#undef True
		#endif
		#if defined( False )
			#undef False
		#endif
	#elif defined( SW_PLATFORM_MACOS )
		#include <mach-o/dyld.h>
		#include <objc/message.h>
		#include <objc/runtime.h>
		#include <pwd.h>
	#endif
#else
	#error "NOT SUPPORTED PLATFORM"
#endif
