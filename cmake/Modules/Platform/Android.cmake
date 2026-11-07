# ==============================================================================
# @file cmake/Modules/Platform/Android.cmake
# @brief Android 플랫폼 정의(앞문 — 매크로와 이름). NDK 라이브러리 · 패키징은 Android 백엔드를 붙일 때 여기에 더한다.
# @note `SW_PLATFORM_LINUX` 를 켜지 않는다. 켜면 `__linux__` 검사를 통과해 X11 · ALSA · epoll 가지로 조용히 간다(언리얼 `PLATFORM_ANDROID` 도 Linux 와 따로다, 기억).
# ==============================================================================

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
	return()
endif()

add_library(sw_platform_android INTERFACE)
target_compile_definitions(sw_platform_android INTERFACE SW_PLATFORM_ANDROID SW_PLATFORM_POSIX SW_PLATFORM_NAME="Android")  # 이름은 sw::build::kPlatformName
list(APPEND sw_flag_libraries sw_platform_android)
