# ==============================================================================
# @file cmake/Modules/Build/RuntimeDependencies.cmake
# @brief project() 이후: vcpkg include/bin 경로 조회 + 런타임 DLL 복사 헬퍼
# @note BuildLayout.cmake 의 sw_queue_runtime_copy 이후에 include 할 것
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) sw_get_vcpkg_paths — vcpkg 설치 트리의 include · bin/lib 폴더 후보
# vcpkg 를 쓰지 않는 구성(SW_USE_VCPKG=OFF)이면 둘 다 빈 목록이다 — 그래서 아래 함수들은 늘 정의돼 있다.
# ------------------------------------------------------------------------------
function(sw_get_vcpkg_paths OUT_INC_DIRS OUT_BIN_DIRS)
	set(list_include_dir "")
	set(list_bin_dir "")
	if(SW_USE_VCPKG AND VCPKG_TARGET_TRIPLET)
		foreach(installed_root "${VCPKG_INSTALLED_DIR}" "${SW_VCPKG_ROOT}/installed")
			if(installed_root AND NOT installed_root STREQUAL "/installed")
				list(APPEND list_include_dir "${installed_root}/${VCPKG_TARGET_TRIPLET}/include")
				list(APPEND list_bin_dir "${installed_root}/${VCPKG_TARGET_TRIPLET}/bin" "${installed_root}/${VCPKG_TARGET_TRIPLET}/lib")
			endif()
		endforeach()
	endif()
	set(${OUT_INC_DIRS} ${list_include_dir} PARENT_SCOPE)
	set(${OUT_BIN_DIRS} ${list_bin_dir} PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 2) sw_link_vcpkg_header_only_target — vcpkg include를 SYSTEM INTERFACE로
# ------------------------------------------------------------------------------
function(sw_link_vcpkg_header_only_target TARGET_NAME)
	sw_get_vcpkg_paths(inc_dirs bin_dirs)

	foreach(dir IN LISTS inc_dirs)
		if(EXISTS "${dir}")
			target_include_directories(${TARGET_NAME} SYSTEM INTERFACE "${dir}")
		endif()
	endforeach()

	target_include_directories(${TARGET_NAME} SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

# ------------------------------------------------------------------------------
# 3) 런타임 복사 큐 — 실제 POST_BUILD는 sw_emit_runtime_copies
# sw_copy_vcpkg_file: bin 디렉터리의 지정 파일
# sw_copy_vcpkg_shared_lib: 플랫폼별 .dll / .so
# ------------------------------------------------------------------------------
# vcpkg bin의 지정 파일을 런타임 복사 큐에 넣습니다.
function(sw_copy_vcpkg_file TARGET_NAME FILE_NAME)
	sw_get_vcpkg_paths(inc_dirs bin_dirs)
	set(found_file "")

	foreach(dir IN LISTS bin_dirs)
		if(EXISTS "${dir}/${FILE_NAME}")
			set(found_file "${dir}/${FILE_NAME}")
			break()
		endif()
	endforeach()

	if(found_file)
		sw_queue_runtime_copy(${TARGET_NAME} "${found_file}")
	endif()
endfunction()

# 플랫폼 접두사/접미사를 붙여 sw_copy_vcpkg_file에 위임합니다.
function(sw_copy_vcpkg_shared_lib TARGET_NAME LIB_BASE_NAME)
	set(lib_name "${CMAKE_SHARED_LIBRARY_PREFIX}${LIB_BASE_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")

	sw_copy_vcpkg_file(${TARGET_NAME} "${lib_name}")
endfunction()

# ------------------------------------------------------------------------------
# 4) sw_copy_vulkan_validation_runtime — Khronos validation + vcpkg mimalloc 의존성
# VkLayer_khronos_validation.dll 은 mimalloc.dll 에 링크되어 있음.
# layer만 복사하면 LoadLibrary(126) → vkCreateInstance LAYER_NOT_PRESENT(-6).
# ------------------------------------------------------------------------------
function(sw_copy_vulkan_validation_runtime TARGET_NAME)
	sw_copy_vcpkg_shared_lib(${TARGET_NAME} "VkLayer_khronos_validation")
	sw_copy_vcpkg_file(${TARGET_NAME} "VkLayer_khronos_validation.json")

	# vcpkg vulkan-validationlayers 가 mimalloc 을 쓰면 같이 배포 (없으면 no-op)
	sw_copy_vcpkg_shared_lib(${TARGET_NAME} "mimalloc")
	sw_copy_vcpkg_shared_lib(${TARGET_NAME} "mimalloc-redirect")
endfunction()

# ------------------------------------------------------------------------------
# 5) sw_copy_tracy_runtime — TracyClient.dll(개발 도구 DevTools 가 지연 로드한다)
# vcpkg 의 Debug DLL 은 debug/bin/Debug/ 에 있어 applocal 이 찾지 못한다. 임포트 타깃이 아는 구성별 위치를 그대로 옮긴다.
# Tracy 를 링크하지 않은 구성(Shipping · SW_ENABLE_TRACY=OFF · 리눅스 정적)은 아무것도 하지 않는다.
# ------------------------------------------------------------------------------
function(sw_copy_tracy_runtime TARGET_NAME)
	if(SW_SHIPPING_BUILD OR NOT SW_ENABLE_TRACY OR NOT WIN32 OR NOT TARGET Tracy::TracyClient)
		return()
	endif()

	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		get_target_property(tracy_dll Tracy::TracyClient IMPORTED_LOCATION_DEBUG)
	else()
		get_target_property(tracy_dll Tracy::TracyClient IMPORTED_LOCATION_RELEASE)
	endif()

	if(tracy_dll)
		sw_queue_runtime_copy(${TARGET_NAME} "${tracy_dll}")
	endif()
endfunction()
