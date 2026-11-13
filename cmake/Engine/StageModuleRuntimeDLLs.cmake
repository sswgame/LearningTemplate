# ==============================================================================
# @file cmake/Engine/StageModuleRuntimeDLLs.cmake
# @brief `cmake -P` 스크립트 — `Bin/Modules` 의 모듈 DLL 이 쓰는 서드파티 DLL 을 `Bin` 에 한 벌 둔다(`sw_stage_module_runtime_dlls` 가 POST_BUILD 로 부른다)
#
# vcpkg 의 applocal(`z-applocal`)은 의존 DLL 을 대상 바이너리 **옆에** 복사한다. 모듈 옆(`Bin/Modules`)에 두면 실행 파일 옆(`Bin`)과 두 벌이 되고,
# 의존성 기준선을 올릴 때 두 벌의 버전이 어긋날 수 있다. 그래서 모듈마다 따로 둔 스테이지 폴더에 모듈을 하드 링크로 두고 applocal 을 돌린 뒤,
# 복사된 DLL 만 `Bin` 으로 옮긴다. 스테이지가 모듈마다라 병렬 링크끼리 같은 폴더를 다투지 않는다.
#
# 인자: MODULE_FILE(모듈 DLL) · STAGE_DIR(이 모듈의 스테이지 폴더) · BIN_DIR(`Bin`) · VCPKG_EXECUTABLE · INSTALLED_BIN_DIR(vcpkg 설치 트리의 bin)
# ==============================================================================
cmake_minimum_required(VERSION 3.21)

foreach(argument MODULE_FILE STAGE_DIR BIN_DIR VCPKG_EXECUTABLE INSTALLED_BIN_DIR)
	if(NOT DEFINED ${argument} OR "${${argument}}" STREQUAL "")
		message(FATAL_ERROR "[StageModuleRuntimeDLLs] ${argument} is required")
	endif()
endforeach()

get_filename_component(module_name "${MODULE_FILE}" NAME)
file(REMOVE_RECURSE "${STAGE_DIR}")
file(MAKE_DIRECTORY "${STAGE_DIR}")
# 모듈을 복사하지 않고 하드 링크로 둔다(Debug 모듈은 수십 MB). 다른 볼륨이면 복사로 넘어간다.
file(CREATE_LINK "${MODULE_FILE}" "${STAGE_DIR}/${module_name}" COPY_ON_ERROR)

# 병렬 링크가 같은 `vcpkg.exe` 를 동시에 띄우면 백신 검사 · 다른 링크가 잡은 순간에 실행이 "resource busy or locked" 로 실패한다
# (CI Windows STL 잡). 잠깐 쉬고 다시 띄운다 — 스테이지 폴더는 매번 비우고 시작하므로 반복해도 결과가 같다.
set(attempt 0)
while(TRUE)
	math(EXPR attempt "${attempt} + 1")
	execute_process(
		COMMAND "${VCPKG_EXECUTABLE}" z-applocal "--target-binary=${STAGE_DIR}/${module_name}" "--installed-bin-dir=${INSTALLED_BIN_DIR}"
		RESULT_VARIABLE result
		OUTPUT_VARIABLE output
		ERROR_VARIABLE output
	)
	if(result EQUAL 0 OR attempt GREATER_EQUAL 5)
		break()
	endif()
	message(STATUS "[StageModuleRuntimeDLLs] z-applocal attempt ${attempt} failed for ${module_name} (${result}), retrying")
	execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2)
	file(REMOVE_RECURSE "${STAGE_DIR}")
	file(MAKE_DIRECTORY "${STAGE_DIR}")
	file(CREATE_LINK "${MODULE_FILE}" "${STAGE_DIR}/${module_name}" COPY_ON_ERROR)
endwhile()
if(NOT result EQUAL 0)
	message(FATAL_ERROR "[StageModuleRuntimeDLLs] vcpkg z-applocal failed for ${module_name} (${result}):\n${output}")
endif()

file(GLOB staged_files LIST_DIRECTORIES false "${STAGE_DIR}/*")
foreach(path IN LISTS staged_files)
	get_filename_component(file_name "${path}" NAME)
	if(file_name STREQUAL module_name)
		continue()
	endif()
	# 다른 모듈의 링크가 같은 DLL 을 같은 때 옮길 수 있다 — 복사가 실패해도 결과가 같은 바이트면 받아들인다.
	file(COPY_FILE "${path}" "${BIN_DIR}/${file_name}" ONLY_IF_DIFFERENT RESULT copy_result)
	if(NOT copy_result EQUAL 0)
		file(SHA256 "${path}" staged_hash)
		set(bin_hash "")
		if(EXISTS "${BIN_DIR}/${file_name}")
			file(SHA256 "${BIN_DIR}/${file_name}" bin_hash)
		endif()
		if(NOT staged_hash STREQUAL bin_hash)
			message(FATAL_ERROR "[StageModuleRuntimeDLLs] Cannot copy ${file_name} into ${BIN_DIR} (in use by a running process?): ${copy_result}")
		endif()
	endif()
endforeach()
file(REMOVE_RECURSE "${STAGE_DIR}")
