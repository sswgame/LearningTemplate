#pragma once

/**
 * @file PlatformHeaders.h
 * @brief 플랫폼(Windows, Linux, macOS) 및 주요 3D API에 의존적인 시스템 헤더들을 추상화하여 포함합니다.
 */

#if defined( SW_PLATFORM_WINDOWS )
	// Windows API 환경 설정: 최소화 및 매크로 충돌 방지
	#if !defined( NOMINMAX )
		/** @brief NOMINMAX 매크로 정의입니다. */
		#define NOMINMAX
	#endif
	#if !defined( WIN32_LEAN_AND_MEAN )
		/** @brief WIN32_LEAN_AND_MEAN 매크로 정의입니다. */
		#define WIN32_LEAN_AND_MEAN
	#endif

	// 기본 Windows SDK 헤더 모음
	#include <sdkddkver.h>
	#include <Windows.h>
	#include <Unknwn.h>
	#include <commdlg.h>
	#include <wrl/client.h>

	// DirectX 및 셰이더 컴파일러 관련 헤더
	#include <d3d11.h>
	#include <d3d12.h>
	#include <dxgi1_4.h>
	#include <d3dcompiler.h>
	#include <dxcapi.h>
	#include <d3d11shader.h>

#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
	// POSIX 표준 시스템 헤더 모음
	#include <unistd.h>
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <sys/select.h>
	#include <dlfcn.h>
	#include <dirent.h>

	// 플랫폼별 파일 시스템 이벤트 감지 헤더
	#if defined( SW_PLATFORM_LINUX )
		#include <sys/inotify.h>
		#include <sys/eventfd.h>
	#elif defined( SW_PLATFORM_MACOS )
		#include <mach-o/dyld.h>
		#include <pwd.h>
	#endif
#else
	#error "NOT SUPPORTED PLATFORM"
#endif

// ============================================================================
// [네트워크 소켓 API 헤더 추상화]
// ============================================================================
#if defined( SW_PLATFORM_WINDOWS )
	#include <WinSock2.h>
	#include <WS2tcpip.h>

	/** @brief 무효 소켓 및 에러 코드 추상화 매크로 (Windows) */
	#define SW_INVALID_SOCKET INVALID_SOCKET
	/** @brief SW_SOCKET_ERROR 매크로 정의입니다. */
	#define SW_SOCKET_ERROR SOCKET_ERROR
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
	#include <sys/types.h>
	#include <arpa/inet.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <netdb.h>

	/** @brief 무효 소켓 및 에러 코드 추상화 매크로 (POSIX) */
	#define SW_INVALID_SOCKET -1
	/** @brief SW_SOCKET_ERROR 매크로 정의입니다. */
	#define SW_SOCKET_ERROR	  -1
#endif
