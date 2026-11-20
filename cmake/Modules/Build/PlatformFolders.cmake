# ==============================================================================
# @file cmake/Modules/Build/PlatformFolders.cmake
# @brief 플랫폼 폴더 규칙 하나 — `Windows/` · `Linux/` · `LinuxKernel/` · `Android/` · `Web/` · `Posix/` 폴더가 이 구성에서 지어지는가
# @details 거르기(sw_filter_platform_sources)와 구성 끝 훑기(sw_verify_built_sources)가 같은 판정을 쓴다.
#          `cmake -P` 로도 부를 수 있게 함수만 둔다(Scripts/setup/tests/test_cmake_build_rules.py).
# ==============================================================================
include_guard(GLOBAL)

# 저장소 기준 경로 RELATIVE_PATH 가 플랫폼 폴더 규칙으로 이 구성에서 지어지는가.
function(sw_is_platform_folder_built RELATIVE_PATH OUT_BUILT)
	set(is_built ON)
	if(RELATIVE_PATH MATCHES "(^|/)Windows/" AND NOT WIN32)
		set(is_built OFF)
	elseif(RELATIVE_PATH MATCHES "(^|/)Linux/" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
		set(is_built OFF)
	# Linux 커널 · bionic 이 함께 가진 것(epoll · eventfd · inotify) — 데스크톱 Linux 와 Android 가 짓는다(언리얼 Android 가 Unix · Linux 코드를 나눠 쓰는 것과 같다). Linux/ 는 데스크톱(X11 · Wayland · ALSA)만이다.
	elseif(RELATIVE_PATH MATCHES "(^|/)LinuxKernel/" AND NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Android)$")
		set(is_built OFF)
	elseif(RELATIVE_PATH MATCHES "(^|/)Android/" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
		set(is_built OFF)
	elseif(RELATIVE_PATH MATCHES "(^|/)Web/" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
		set(is_built OFF)
	elseif(RELATIVE_PATH MATCHES "(^|/)Posix/" AND WIN32)
		set(is_built OFF)
	endif()
	set(${OUT_BUILT} ${is_built} PARENT_SCOPE)
endfunction()
