# ==============================================================================
# @file cmake/internal/ClangClWindowsTools.cmake
# @brief clang-cl + Ninja: project() 이후 정적 아카이브 도구(lib.exe) 재바인딩
# @note llvm-lib가 LLVM 키트에 없으면 enable_language가 CMAKE_CXX_COMPILER_AR /
#       아카이브 규칙을 *-NOTFOUND로 남길 수 있음. CMAKE_AR(MSVC lib.exe)로 다시 묶음.
# ==============================================================================

if(NOT WIN32)
	return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	return()
endif()

# ------------------------------------------------------------------------------
# 1) 아카이버 재바인딩 — llvm-lib 없으면 MSVC lib.exe
#    enable_language가 박아 둔 *-NOTFOUND 아카이브 규칙을 덮어씀
# ------------------------------------------------------------------------------
set(swArTool "")
if(CMAKE_AR AND NOT CMAKE_AR MATCHES "NOTFOUND" AND EXISTS "${CMAKE_AR}")
	set(swArTool "${CMAKE_AR}")
endif()

if(NOT swArTool)
	include("${CMAKE_CURRENT_LIST_DIR}/FindWindowsArchiveAndMt.cmake")
	sw_findWindowsArchiveAndMt(swFoundAr swFoundMt)
	if(swFoundAr)
		set(swArTool "${swFoundAr}")
	endif()
	if(swFoundMt AND (NOT CMAKE_MT OR CMAKE_MT MATCHES "NOTFOUND"))
		set(CMAKE_MT "${swFoundMt}" CACHE FILEPATH "매니페스트 도구" FORCE)
	endif()
endif()

if(NOT swArTool)
	message(WARNING "[ClangClWindowsTools] lib.exe not found — static libraries may fail to link")
	return()
endif()

set(CMAKE_AR "${swArTool}" CACHE FILEPATH "정적 라이브러리 아카이버" FORCE)
set(CMAKE_C_COMPILER_AR "${swArTool}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER_AR "${swArTool}" CACHE FILEPATH "" FORCE)

set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
set(CMAKE_C_ARCHIVE_APPEND "<CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_APPEND "<CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
set(CMAKE_C_ARCHIVE_FINISH "")
set(CMAKE_CXX_ARCHIVE_FINISH "")

message(STATUS "[ClangClWindowsTools] archive tool=${swArTool}")
