# ==============================================================================
# @file cmake/Modules/Platform/Web.cmake
# @brief 웹(Emscripten) 플랫폼 정의(앞문 — 매크로와 이름). 링크 옵션 · 패키징은 WebGPU 백엔드를 붙일 때 여기에 더한다.
# @note Emscripten 은 POSIX 층을 흉내 낸다(파일 · 시계 · pthread). 소켓 · 프로세스처럼 없는 것은 그 자리의 `#elif` 가 오류로 알린다.
# ==============================================================================

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
	return()
endif()

add_library(sw_platform_web INTERFACE)
target_compile_definitions(sw_platform_web INTERFACE SW_PLATFORM_WEB SW_PLATFORM_POSIX SW_PLATFORM_NAME="Web")  # 이름은 sw::build::kPlatformName
list(APPEND sw_flag_libraries sw_platform_web)
