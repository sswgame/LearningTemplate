# ==============================================================================
# @file cmake/Environment/FindLibclang.cmake
# @brief ReflectionParser 가 링크하는 libclang 찾기 — 환경 변수 → toolchain_config.json → 배포판 LLVM(/usr/lib/llvm-N)
# ==============================================================================

# ------------------------------------------------------------------------------
# sw_findLibclang — libclang 라이브러리 · 헤더 폴더 · (Windows) libclang.dll 을 찾아 OUT_LIB · OUT_INCLUDE_DIR · OUT_DLL 에 담는다.
#   못 찾으면 OUT_LIB 이 빈다. SW_REQUIRE_REFLECTION 이면 구성을 세우고, 아니면 경고만 남긴다(부르는 쪽이 파서를 짓지 않고 돌아간다).
#   캐시 항목 이름(LIBCLANG_LIB · LIBCLANG_INCLUDE_DIR · DETECTED_LIBCLANG_DLL)은 그대로 둔다 — 옛 빌드 폴더의 캐시가 이어진다.
# ------------------------------------------------------------------------------
function(sw_findLibclang OUT_LIB OUT_INCLUDE_DIR OUT_DLL)
	set(${OUT_LIB} "" PARENT_SCOPE)
	set(${OUT_INCLUDE_DIR} "" PARENT_SCOPE)
	set(${OUT_DLL} "" PARENT_SCOPE)

	set(DETECTED_LLVM_DIR "$ENV{LLVM_DIR}")

	if(NOT DETECTED_LLVM_DIR)
		set(DETECTED_LLVM_DIR "$ENV{LLVM_HOME}")
	endif()

	# toolchain_config.json 은 `DetectToolchain` 이 include 한 생성 파일(ToolchainVars.cmake)이
	# 이미 변수로 풀어 두었다. 여기서 다시 파싱하지 않는다.
	if(NOT DETECTED_LLVM_DIR)
		set(DETECTED_LLVM_DIR "${SW_TOOLCHAIN_LLVM_PATH}")
		set(DETECTED_LIBCLANG_DLL "${SW_TOOLCHAIN_LIBCLANG_DLL_PATH}")
	endif()

	# 환경 변수 · toolchain 에 없으면 배포판 패키지 경로를 찾는다(Ubuntu 의 /usr/lib/llvm-N). 버전 목록을 손으로 들고
	# 있으면 목록보다 새 LLVM 이 깔린 호스트에서 조용히 빠지므로, 설치된 것 중 가장 높은 메이저 버전을 고른다.
	if(NOT DETECTED_LLVM_DIR)
		file(GLOB llvmCandidates LIST_DIRECTORIES true "/usr/lib/llvm-*")
		list(SORT llvmCandidates COMPARE NATURAL ORDER DESCENDING)

		foreach(llvmCandidate IN LISTS llvmCandidates)
			if(EXISTS "${llvmCandidate}/include/clang-c/Index.h")
				set(DETECTED_LLVM_DIR "${llvmCandidate}")
				message(STATUS "[ReflectionParser] Using distro LLVM: ${DETECTED_LLVM_DIR}")
				break()
			endif()
		endforeach()
	endif()

	# llvm_path 가 없으면 검색을 포기한다.
	if(NOT DETECTED_LLVM_DIR)
		message(STATUS "[ReflectionParser] LLVM directory not detected, skipping.")
		return()
	endif()

	set(llvmInstallPrefix "${DETECTED_LLVM_DIR}")

	# 배포판 패키지에는 libclang-N.so 처럼 버전이 붙은 이름만 있을 수 있다. 여기도 목록 대신 glob 으로 모은다.
	file(GLOB libclangVersioned "${llvmInstallPrefix}/lib/libclang-*.so")
	set(libclangNames libclang clang)

	foreach(libclangPath IN LISTS libclangVersioned)
		get_filename_component(libclangName "${libclangPath}" NAME_WE)
		string(REGEX REPLACE "^lib" "" libclangName "${libclangName}")
		list(APPEND libclangNames "${libclangName}")
	endforeach()

	find_library(LIBCLANG_LIB
		NAMES ${libclangNames}
		PATHS "${llvmInstallPrefix}/lib" "${llvmInstallPrefix}/bin" "${llvmInstallPrefix}"
		NO_DEFAULT_PATH
	)

	find_path(LIBCLANG_INCLUDE_DIR
		NAMES clang-c/Index.h
		PATHS
		"${llvmInstallPrefix}/include"
		/usr/include
		NO_DEFAULT_PATH
	)

	if(NOT LIBCLANG_LIB OR NOT LIBCLANG_INCLUDE_DIR)
		if(SW_REQUIRE_REFLECTION)
			message(FATAL_ERROR
				"[ReflectionParser] '${llvmInstallPrefix}' 경로에서 libclang을 찾을 수 없습니다.\n"
				"  LLVM을 설치하거나 Config/Environment/toolchain_config.json 의 llvm_path 를 설정하세요.\n"
				"  도구만 빌드하려면 -DSW_REQUIRE_REFLECTION=OFF 를 사용하세요."
			)
		endif()

		message(WARNING
			"[ReflectionParser] '${llvmInstallPrefix}' 경로에서 libclang을 찾을 수 없습니다.\n"
			"  ReflectionParser 빌드가 건너뛰어집니다."
		)
		return()
	endif()

	# libclang.dll — 파서 옆(BuildTools)으로 복사할 원본. toolchain 이 알려 주지 않았으면 LLVM 폴더에서 찾는다.
	if(NOT DETECTED_LIBCLANG_DLL)
		find_file(
			DETECTED_LIBCLANG_DLL
			NAMES libclang.dll
			HINTS "${llvmInstallPrefix}/bin" "${llvmInstallPrefix}/lib"
		)
	endif()

	set(${OUT_LIB} "${LIBCLANG_LIB}" PARENT_SCOPE)
	set(${OUT_INCLUDE_DIR} "${LIBCLANG_INCLUDE_DIR}" PARENT_SCOPE)
	if(DETECTED_LIBCLANG_DLL AND EXISTS "${DETECTED_LIBCLANG_DLL}")
		set(${OUT_DLL} "${DETECTED_LIBCLANG_DLL}" PARENT_SCOPE)
	endif()
endfunction()
