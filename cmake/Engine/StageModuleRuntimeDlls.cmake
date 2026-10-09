# ==============================================================================
# @file cmake/Engine/StageModuleRuntimeDlls.cmake
# @brief `cmake -P` 스크립트 — `Bin/Modules` 의 모듈 DLL 이 쓰는 서드파티 DLL 을 `Bin` 에 한 벌 둔다(`sw_stageModuleRuntimeDlls` 가 POST_BUILD 로 부른다)
#
# vcpkg 의 applocal(`z-applocal`)은 의존 DLL 을 대상 바이너리 **옆에** 복사한다. 모듈 옆(`Bin/Modules`)에 두면 실행 파일 옆(`Bin`)과 두 벌이 되고,
# 의존성 기준선을 올릴 때 두 벌의 버전이 어긋날 수 있다. 그래서 모듈마다 따로 둔 스테이지 폴더에 모듈을 하드 링크로 두고 applocal 을 돌린 뒤,
# 복사된 DLL 만 `Bin` 으로 옮긴다. 스테이지가 모듈마다라 병렬 링크끼리 같은 폴더를 다투지 않는다.
#
# 인자: MODULE_FILE(모듈 DLL) · STAGE_DIR(이 모듈의 스테이지 폴더) · BIN_DIR(`Bin`) · VCPKG_EXECUTABLE · INSTALLED_BIN_DIR(vcpkg 설치 트리의 bin)
# ==============================================================================
cmake_minimum_required(VERSION 3.21)

foreach(swArgument MODULE_FILE STAGE_DIR BIN_DIR VCPKG_EXECUTABLE INSTALLED_BIN_DIR)
	if(NOT DEFINED ${swArgument} OR "${${swArgument}}" STREQUAL "")
		message(FATAL_ERROR "[StageModuleRuntimeDlls] ${swArgument} is required")
	endif()
endforeach()

get_filename_component(swModuleName "${MODULE_FILE}" NAME)
file(REMOVE_RECURSE "${STAGE_DIR}")
file(MAKE_DIRECTORY "${STAGE_DIR}")
# 모듈을 복사하지 않고 하드 링크로 둔다(Debug 모듈은 수십 MB). 다른 볼륨이면 복사로 넘어간다.
file(CREATE_LINK "${MODULE_FILE}" "${STAGE_DIR}/${swModuleName}" COPY_ON_ERROR)

execute_process(
	COMMAND "${VCPKG_EXECUTABLE}" z-applocal "--target-binary=${STAGE_DIR}/${swModuleName}" "--installed-bin-dir=${INSTALLED_BIN_DIR}"
	RESULT_VARIABLE swResult
	OUTPUT_VARIABLE swOutput
	ERROR_VARIABLE swOutput
)
if(NOT swResult EQUAL 0)
	message(FATAL_ERROR "[StageModuleRuntimeDlls] vcpkg z-applocal failed for ${swModuleName} (${swResult}):\n${swOutput}")
endif()

file(GLOB swStagedFiles LIST_DIRECTORIES false "${STAGE_DIR}/*")
foreach(swPath IN LISTS swStagedFiles)
	get_filename_component(swFileName "${swPath}" NAME)
	if(swFileName STREQUAL swModuleName)
		continue()
	endif()
	# 다른 모듈의 링크가 같은 DLL 을 같은 때 옮길 수 있다 — 복사가 실패해도 결과가 같은 바이트면 받아들인다.
	file(COPY_FILE "${swPath}" "${BIN_DIR}/${swFileName}" ONLY_IF_DIFFERENT RESULT swCopyResult)
	if(NOT swCopyResult EQUAL 0)
		file(SHA256 "${swPath}" swStagedHash)
		set(swBinHash "")
		if(EXISTS "${BIN_DIR}/${swFileName}")
			file(SHA256 "${BIN_DIR}/${swFileName}" swBinHash)
		endif()
		if(NOT swStagedHash STREQUAL swBinHash)
			message(FATAL_ERROR "[StageModuleRuntimeDlls] Cannot copy ${swFileName} into ${BIN_DIR} (in use by a running process?): ${swCopyResult}")
		endif()
	endif()
endforeach()
file(REMOVE_RECURSE "${STAGE_DIR}")
