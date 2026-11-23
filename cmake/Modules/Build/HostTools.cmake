# ==============================================================================
# @file cmake/Modules/Build/HostTools.cmake
# @brief 교차 빌드(웹 · Android)와 배포 빌드가 빌드 중에 쓰는 호스트 쪽 것(쿠킹하는 App · Server, 교차 빌드는 ReflectionParser · 쿠커 도구 판 기록도)을 호스트 개발 빌드에서 가져온다
# @details 대상 툴체인으로 지은 도구는 이 기계에서 돌지 않고, 배포 구성에는 쿠커(DevTools)가 없다 — 호스트 프리셋 빌드의 실행 파일을 쓴다
#          (CMake 공식 교차 빌드 모양, 언리얼 UHT · 쿡 커맨드렛 · 유니티 IL2CPP 도 호스트 · 에디터 도구다). 반사 단계 앞에 호스트 빌드의 그 타깃을 다시 지어
#          (`SwHostReflectionParser`) 파서 소스 · 틀이 바뀌어도 낡은 파서로 돌지 않는다(언리얼 UBT 가 UHT 를 먼저 짓는 것과 같다).
#          호스트 빌드가 구성되지 않았으면 구성이 멈춘다(폴백 없음).
# ==============================================================================

# 전용 서버 팩은 서버 구성의 쿠커(Server)가 굽는다 — 서버가 모르는 형식은 그 실행 파일이 안다(`ClientOnlyTypes.json`). 그래서 서버 배포의 호스트는 서버 개발 빌드다.
if(SW_TARGET_TYPE STREQUAL "Server")
	set(default_host_preset "Ninja-Release-Server")
elseif(CMAKE_HOST_WIN32)
	set(default_host_preset "Ninja-Release")
else()
	set(default_host_preset "Linux-Release")
endif()
set(SW_HOST_BUILD_DIR "${CMAKE_SOURCE_DIR}/build/${default_host_preset}" CACHE PATH "Host development build folder that cooks shipping packs (and whose ReflectionParser and vcpkg record cross builds use)")

if(CMAKE_HOST_WIN32)
	set(host_exe_suffix ".exe")
elseif(CMAKE_HOST_UNIX)
	set(host_exe_suffix "")
else()
	message(FATAL_ERROR "[HostTools] unsupported host (Windows or Unix)")
endif()

if(NOT EXISTS "${SW_HOST_BUILD_DIR}/CMakeCache.txt")
	message(FATAL_ERROR "[HostTools] cross and shipping builds need a configured host development build at '${SW_HOST_BUILD_DIR}'.\n"
		"  Configure it first: cmake --preset ${default_host_preset}   (or set SW_HOST_BUILD_DIR to another host build folder)")
endif()
unset(default_host_preset)

# 배포 빌드(교차 아님)의 ReflectionParser · 쿠커 도구 판은 이 빌드의 것이다 — 쿠커 실행 파일만 호스트 빌드에서 빌린다(AssetAndToolTargets.cmake).
if(NOT CMAKE_CROSSCOMPILING)
	unset(host_exe_suffix)
	return()
endif()

set(host_reflection_parser "${SW_HOST_BUILD_DIR}/BuildTools/ReflectionParser${host_exe_suffix}")
add_custom_target(SwHostReflectionParser
	COMMAND "${CMAKE_COMMAND}" --build "${SW_HOST_BUILD_DIR}" --target ReflectionParser
	BYPRODUCTS "${host_reflection_parser}"
	COMMENT "[HostTools] Building the host ReflectionParser (${SW_HOST_BUILD_DIR})"
	USES_TERMINAL
	VERBATIM)
add_executable(ReflectionParser IMPORTED GLOBAL)
set_target_properties(ReflectionParser PROPERTIES IMPORTED_LOCATION "${host_reflection_parser}")

# 쿠커 도구 판(셰이더 컴파일러)은 쿠킹하는 호스트의 vcpkg 기록이 답이다 — 대상(arm64-android · wasm32)에는 dxc 가 없다.
file(STRINGS "${SW_HOST_BUILD_DIR}/CMakeCache.txt" host_installed_line REGEX "^VCPKG_INSTALLED_DIR:")
string(REGEX REPLACE "^VCPKG_INSTALLED_DIR:[A-Z]+=" "" host_installed_dir "${host_installed_line}")
if(NOT host_installed_dir OR NOT EXISTS "${host_installed_dir}/vcpkg/status")
	message(FATAL_ERROR "[HostTools] host build '${SW_HOST_BUILD_DIR}' has no vcpkg install record (VCPKG_INSTALLED_DIR='${host_installed_dir}')")
endif()
set(SW_HOST_VCPKG_STATUS "${host_installed_dir}/vcpkg/status" CACHE INTERNAL "Host vcpkg install record (cook tool versions)")

message(STATUS "[HostTools] ReflectionParser: ${host_reflection_parser} (rebuilt by SwHostReflectionParser)")
unset(host_reflection_parser)
unset(host_exe_suffix)
unset(host_installed_line)
unset(host_installed_dir)
