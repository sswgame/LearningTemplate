# ==============================================================================
# @file cmake/Config/IpoSupport.cmake
# @brief IPO(LTO) 를 이 툴체인이 실제로 할 수 있는지 판정한다
# ==============================================================================

# ------------------------------------------------------------------------------
# IPO(LTO) 지원 판정 — sw_checkIpoSupport(OUT_SUPPORTED OUT_ERROR)
#
# **CMake 의 `check_ipo_supported` 는 이 툴체인에서 구조적으로 거짓을 말한다.**
# 그것은 `try_compile(... PROJECT ...)` 로 작은 프로젝트를 따로 구성해서 재는데, 그 형식은
# `CMAKE_AR` 을 하위 프로젝트로 **넘기지 않는다**(CMake 4.4 의 CheckIPOSupported.cmake 는
# CMAKE_VERBOSE_MAKEFILE · CMAKE_INTERPROCEDURAL_OPTIMIZATION · 언어 플래그만 넘긴다).
# 그래서 하위 프로젝트는 아카이버를 스스로 찾아 MSVC lib.exe 를 집고, clang 이 `-flto` 로 낸
# 비트코드 .obj 를 못 읽어 `LNK1107` 로 죽는다. CMake 는 그 실패를 "이 컴파일러는 IPO 를
# 지원하지 않는다" 로 보고한다 — 컴파일러는 멀쩡한데.
#
# 그 오진 때문에 `SW_ENABLE_LTO=ON` 인 채로 **-flto 가 한 TU 에도 안 걸린** 상태가 오래 있었다
# (2026-09-14 에 Shipping 452 TU 중 0 개로 실측). 그래서 clang + llvm-lib 조합은 우리가 직접
# 판정한다. 그 조합이 아니면 예전대로 CMake 에게 묻는다.
# ------------------------------------------------------------------------------
function(sw_checkIpoSupport OUT_SUPPORTED OUT_ERROR)
	set(${OUT_ERROR} "" PARENT_SCOPE)

	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_AR MATCHES "llvm-lib|llvm-ar")
		set(${OUT_SUPPORTED} TRUE PARENT_SCOPE)
		return()
	endif()

	include(CheckIPOSupported)
	check_ipo_supported(RESULT swIpoOk OUTPUT swIpoErr)
	set(${OUT_SUPPORTED} ${swIpoOk} PARENT_SCOPE)
	set(${OUT_ERROR} "${swIpoErr}" PARENT_SCOPE)
endfunction()
