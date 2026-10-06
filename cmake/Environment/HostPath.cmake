# ==============================================================================
# @file cmake/Environment/HostPath.cmake
# @brief 이 configure 프로세스의 PATH 앞에 도구 폴더를 붙인다 — 자식(파이썬 스크립트 · vcpkg · git)이 같은 도구를 보게.
# ==============================================================================
include_guard(GLOBAL)

# DIRECTORY 를 PATH 맨 앞에(이미 있으면 그대로). 구분자는 호스트 것(Windows `;` · 그 밖 `:`) — project() 전에도 부르므로 CMAKE_HOST_WIN32 를 본다.
function(sw_prependEnvPath DIRECTORY)
	if(CMAKE_HOST_WIN32)
		set(separator ";")
	else()
		set(separator ":")
	endif()
	string(FIND "${separator}$ENV{PATH}${separator}" "${separator}${DIRECTORY}${separator}" existingIndex)
	if(existingIndex EQUAL -1)
		set(ENV{PATH} "${DIRECTORY}${separator}$ENV{PATH}")
	endif()
endfunction()

# CMake Tools 등 얇은 PATH 에서도 Git for Windows 를 쓰도록 기본 설치 경로를 앞에 붙인다(첫 번째로 있는 것 하나).
# 주의: ENV{ProgramFiles(x86)} 는 괄호 때문에 if(DEFINED ...) 파싱이 깨지므로 쓰지 않는다.
if(CMAKE_HOST_WIN32)
	foreach(gitDirectory
		"$ENV{ProgramFiles}/Git/cmd" "$ENV{ProgramFiles}/Git/bin"
		"C:/Program Files/Git/cmd" "C:/Program Files/Git/bin"
		"C:/Program Files (x86)/Git/cmd" "C:/Program Files (x86)/Git/bin"
		"$ENV{LOCALAPPDATA}/Programs/Git/cmd" "$ENV{LOCALAPPDATA}/Programs/Git/bin")
		if(EXISTS "${gitDirectory}/git.exe")
			sw_prependEnvPath("${gitDirectory}")
			break()
		endif()
	endforeach()
	unset(gitDirectory)
endif()
