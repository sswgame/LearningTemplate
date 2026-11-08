# ==============================================================================
# @file cmake/Engine/HostTools.cmake
# @brief 교차 빌드(웹 · Android)가 빌드 중에 돌리는 호스트 도구(ReflectionParser)를 호스트 빌드에서 가져온다
# @details 대상 툴체인으로 지은 도구는 이 기계에서 돌지 않는다 — 호스트 프리셋이 지은 실행 파일을 IMPORTED 타깃으로 세운다
#          (CMake 공식 교차 빌드 모양, 언리얼 UHT · 유니티 IL2CPP 도 호스트 도구다). 없으면 구성이 멈춘다(폴백 없음).
# ==============================================================================

set(SW_HOST_TOOLS_DIR "${CMAKE_SOURCE_DIR}/build/Ninja-Release/BuildTools" CACHE PATH "Host build's BuildTools folder (ReflectionParser) used by cross builds")

if(CMAKE_HOST_WIN32)
	set(swHostExeSuffix ".exe")
elseif(CMAKE_HOST_UNIX)
	set(swHostExeSuffix "")
else()
	message(FATAL_ERROR "[HostTools] unsupported host (Windows or Unix)")
endif()

set(swHostReflectionParser "${SW_HOST_TOOLS_DIR}/ReflectionParser${swHostExeSuffix}")
if(NOT EXISTS "${swHostReflectionParser}")
	message(FATAL_ERROR "[HostTools] cross build needs the host ReflectionParser at '${swHostReflectionParser}'.\n"
		"  Build it first: cmake --build --preset Ninja-Release --target ReflectionParser\n"
		"  (or set SW_HOST_TOOLS_DIR to another host build's BuildTools folder)")
endif()
add_executable(ReflectionParser IMPORTED GLOBAL)
set_target_properties(ReflectionParser PROPERTIES IMPORTED_LOCATION "${swHostReflectionParser}")
message(STATUS "[HostTools] ReflectionParser: ${swHostReflectionParser}")
unset(swHostReflectionParser)
unset(swHostExeSuffix)
