# ==============================================================================
# @file cmake/Environment/FindLlvmBin.cmake
# @brief vcpkg 포트 툴체인이 쓰는 LLVM bin 폴더 — toolchain_config.json 의 llvm_path 하나만 본다
# ==============================================================================

# toolchain_config 의 파일 이름과 키(SW_DIR_CONFIG_ENV · SW_KEY_LLVM_PATH …)를 쓴다. vcpkg 포트 툴체인처럼 최상위
# CMakeLists 를 거치지 않는 곳에서 include 되므로 여기서 읽는다(파일 스코프 — 함수 안에서 읽으면 부를 때마다 다시 돈다).
include("${CMAKE_CURRENT_LIST_DIR}/../Config/LoadConfigConstants.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../Config/JsonValue.cmake")

# ------------------------------------------------------------------------------
# 1) sw_findLlvmBin — 저장소가 고른 LLVM 의 bin 폴더. 없으면 구성을 세운다.
#
# LLVM 을 **어디서 찾을지**(환경 변수 → PATH → search_paths → Tools/LLVM)는 `Scripts/setup/llvm.py` 한 곳이 정하고
# 그 답을 `toolchain_config.json` 의 `llvm_path` 에 적는다. 예전에는 이 함수가 같은 탐색을 CMake 로 한 번 더 했다(후보 이름 · Program Files 경로 ·
# search_paths 키를 따로 들고) — 두 탐색이 다른 LLVM 을 집으면 포트와 엔진이 다른 컴파일러로 지어졌다.
# 본 프로젝트 configure 는 이 함수를 부르지 않는다 — DetectToolchain 이 같은 값을 `SW_TOOLCHAIN_LLVM_PATH` 로 받아 두었다.
#
# 이 파일이 사는 곳은 `<repo>/cmake/Environment` 다 — 저장소 루트는 두 단계 위다. `project()` 앞(vcpkg 오버레이 생성)에서도 불린다.
# 포트 빌드는 이 함수를 부르지 않는다 — 생성한 포트 툴체인(`VcpkgTriplets.cmake`)이 여기서 푼 경로를 글로 싣는다.
# ------------------------------------------------------------------------------
function(sw_findLlvmBin OUT_VAR)
	get_filename_component(swRepoRoot "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
	set(swConfigFile "${swRepoRoot}/${SW_DIR_CONFIG_ENV}/${SW_FILE_TOOLCHAIN_CONFIG}")
	sw_requireJSONValue(swLlvmPath "${swConfigFile}" "${SW_KEY_LLVM_PATH}")

	if(swLlvmPath STREQUAL "" OR NOT IS_DIRECTORY "${swLlvmPath}/bin")
		message(FATAL_ERROR
			"[FindLlvmBin] ${swConfigFile}: ${SW_KEY_LLVM_PATH}='${swLlvmPath}' has no bin folder.\n"
			"  Run `py -3 Scripts/setup/environment.py` (or llvm.py --install) so it records a valid LLVM kit."
		)
	endif()

	set(${OUT_VAR} "${swLlvmPath}/bin" PARENT_SCOPE)
endfunction()
