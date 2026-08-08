# ==============================================================================
# @file ThirdParty/dxc/CMakeLists.txt
# @brief DirectX Shader Compiler (DXC) 연동 + 런타임 공유 라이브러리 큐잉
# @note
#   - ShaderCompiler는 dxcompiler를 dlopen/LoadLibrary로 로드한다.
#   - Windows만 import lib(Microsoft::DirectXShaderCompiler)를 링크한다.
#   - Linux/macOS에서는 헤더만 연결한다. (macOS는 vcpkg directx-dxc 미지원)
#   - Linux 프리빌드 libdxil.so 의 SONAME 이 libdxildll.so 라서, DXIL을
#     링크하면 ReflectionParser 등이 로드에 실패한다. DXIL은 링크하지 않는다.
# ==============================================================================

find_package(directx-dxc CONFIG QUIET)

add_library(dxc INTERFACE)

if(TARGET Microsoft::DirectXShaderCompiler)
	# include path는 전 플랫폼 공통
	target_include_directories(dxc SYSTEM INTERFACE
		$<TARGET_PROPERTY:Microsoft::DirectXShaderCompiler,INTERFACE_INCLUDE_DIRECTORIES>
	)
	if(WIN32)
		# Windows: dxcompiler.lib 링크. DXIL은 dxcompiler가 런타임에 로드.
		# INTERFACE의 Microsoft::DXIL 전파는 끊는다.
		target_link_libraries(dxc INTERFACE Microsoft::DirectXShaderCompiler)
		set_property(TARGET Microsoft::DirectXShaderCompiler
			PROPERTY INTERFACE_LINK_LIBRARIES "")
	endif()
elseif(TARGET directx-dxc::directx-dxc)
	target_include_directories(dxc SYSTEM INTERFACE
		$<TARGET_PROPERTY:directx-dxc::directx-dxc,INTERFACE_INCLUDE_DIRECTORIES>
	)
	if(WIN32)
		target_link_libraries(dxc INTERFACE directx-dxc::directx-dxc)
	endif()
else()
	# macOS 등: 패키지 없음 → include 경로만 (있으면) 연결
	add_library(dxc::dxc ALIAS dxc)
	sw_link_vcpkg_header_only_target(dxc)
	if(APPLE)
		message(STATUS "[dxc] directx-dxc is not available on macOS (headers-only / runtime optional)")
	endif()
endif()

if(TARGET sw_third_party_includes)
	target_link_libraries(sw_third_party_includes INTERFACE dxc)
endif()

# ------------------------------------------------------------------------------
# Linux: 파일명 libdxil.so / SONAME libdxildll.so 불일치 호환 심링크
# (링크하지 않아도, 배포본·수동 링크·도구가 SONAME을 요구할 수 있음)
# ------------------------------------------------------------------------------
function(sw_ensure_dxil_linux_soname_symlink LIB_DIR)
	if(NOT LIB_DIR OR NOT EXISTS "${LIB_DIR}/libdxil.so")
		return()
	endif()
	set(_link "${LIB_DIR}/libdxildll.so")
	if(EXISTS "${_link}" OR IS_SYMLINK "${_link}")
		return()
	endif()
	file(CREATE_LINK "libdxil.so" "${_link}" SYMBOLIC RESULT _link_result)
	if(_link_result)
		message(WARNING "[dxc] Failed to create ${_link} -> libdxil.so (${_link_result})")
	else()
		message(STATUS "[dxc] Created Linux DXIL SONAME link: ${_link} -> libdxil.so")
	endif()
endfunction()

if(UNIX AND NOT APPLE)
	set(_dxil_dirs "")
	if(TARGET Microsoft::DXIL)
		foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
			get_target_property(_loc Microsoft::DXIL IMPORTED_LOCATION_${_cfg})
			if(_loc)
				get_filename_component(_dir "${_loc}" DIRECTORY)
				list(APPEND _dxil_dirs "${_dir}")
			endif()
		endforeach()
		get_target_property(_loc Microsoft::DXIL IMPORTED_LOCATION)
		if(_loc)
			get_filename_component(_dir "${_loc}" DIRECTORY)
			list(APPEND _dxil_dirs "${_dir}")
		endif()
	endif()
	if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
		list(APPEND _dxil_dirs
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
		)
	endif()
	list(REMOVE_DUPLICATES _dxil_dirs)
	foreach(_dir IN LISTS _dxil_dirs)
		sw_ensure_dxil_linux_soname_symlink("${_dir}")
	endforeach()
endif()

# ------------------------------------------------------------------------------
# 내부: DXC 런타임 공유 라이브러리 경로 탐색 (vcpkg → DIRECTX_DXC_TOOL → engine_config)
# ------------------------------------------------------------------------------
function(sw_find_dxc_runtime_dlls OUT_DXC OUT_DXIL)
	set(_dxc "")
	set(_dxil "")

	if(WIN32)
		set(_dxc_names "dxcompiler.dll")
		set(_dxil_names "dxil.dll")
	elseif(APPLE)
		# 공식 프리빌드 없음. 소스 빌드/수동 배치 시에만 복사됨.
		set(_dxc_names "libdxcompiler.dylib")
		set(_dxil_names "libdxil.dylib" "libdxildll.dylib")
	else()
		set(_dxc_names "libdxcompiler.so")
		set(_dxil_names "libdxildll.so" "libdxil.so")
	endif()

	if(COMMAND sw_get_vcpkg_paths)
		sw_get_vcpkg_paths(_inc_dirs _bin_dirs)
		set(_search_dirs ${_bin_dirs})
		if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
			list(APPEND _search_dirs
				"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
				"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
				"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
			)
		endif()

		foreach(_dir IN LISTS _search_dirs)
			foreach(_name IN LISTS _dxc_names)
				if(_dxc STREQUAL "" AND EXISTS "${_dir}/${_name}")
					set(_dxc "${_dir}/${_name}")
				endif()
			endforeach()
			foreach(_name IN LISTS _dxil_names)
				if(_dxil STREQUAL "" AND EXISTS "${_dir}/${_name}")
					set(_dxil "${_dir}/${_name}")
				endif()
			endforeach()
		endforeach()
	endif()

	if(DEFINED DIRECTX_DXC_TOOL AND EXISTS "${DIRECTX_DXC_TOOL}")
		get_filename_component(_tool_dir "${DIRECTX_DXC_TOOL}" DIRECTORY)
		foreach(_name IN LISTS _dxc_names)
			if(_dxc STREQUAL "" AND EXISTS "${_tool_dir}/${_name}")
				set(_dxc "${_tool_dir}/${_name}")
			endif()
		endforeach()
		foreach(_name IN LISTS _dxil_names)
			if(_dxil STREQUAL "" AND EXISTS "${_tool_dir}/${_name}")
				set(_dxil "${_tool_dir}/${_name}")
			endif()
		endforeach()
	endif()

	if(EXISTS "${CMAKE_SOURCE_DIR}/Config/engine_config.json")
		file(READ "${CMAKE_SOURCE_DIR}/Config/engine_config.json" _cfg)
		if(_dxc STREQUAL "")
			string(JSON _cfg_dxc ERROR_VARIABLE _e1 GET "${_cfg}" "dxc_dll_path")
			if(_cfg_dxc AND EXISTS "${_cfg_dxc}")
				set(_dxc "${_cfg_dxc}")
			endif()
		endif()
		if(_dxil STREQUAL "")
			string(JSON _cfg_dxil ERROR_VARIABLE _e2 GET "${_cfg}" "dxil_dll_path")
			if(_cfg_dxil AND EXISTS "${_cfg_dxil}")
				set(_dxil "${_cfg_dxil}")
			endif()
		endif()
	endif()

	set(${OUT_DXC} "${_dxc}" PARENT_SCOPE)
	set(${OUT_DXIL} "${_dxil}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# @function sw_copy_dxc_dlls
# @brief DXC 런타임 공유 라이브러리를 타겟 복사 큐에 등록 (실제 복사는 sw_emit_runtime_copies)
# ------------------------------------------------------------------------------
function(sw_copy_dxc_dlls TARGET_NAME)
	sw_find_dxc_runtime_dlls(_dxc_dll _dxil_dll)
	if(_dxc_dll)
		sw_queue_runtime_copy(${TARGET_NAME} "${_dxc_dll}")
	endif()
	if(_dxil_dll)
		sw_queue_runtime_copy(${TARGET_NAME} "${_dxil_dll}")
	endif()
endfunction()
