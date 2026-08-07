# ==============================================================================
# @file ThirdParty/dxc/CMakeLists.txt
# @brief DirectX Shader Compiler (DXC) 연동 + 런타임 DLL 큐잉
# ==============================================================================

find_package(directx-dxc CONFIG QUIET)

if(TARGET Microsoft::DirectXShaderCompiler)
	add_library(dxc INTERFACE)
	target_link_libraries(dxc INTERFACE Microsoft::DirectXShaderCompiler)
elseif(TARGET directx-dxc::directx-dxc)
	add_library(dxc INTERFACE)
	target_link_libraries(dxc INTERFACE directx-dxc::directx-dxc)
else()
	add_library(dxc INTERFACE)
	add_library(dxc::dxc ALIAS dxc)
	sw_link_vcpkg_header_only_target(dxc)
endif()

if(TARGET sw_third_party_includes)
	target_link_libraries(sw_third_party_includes INTERFACE dxc)
endif()

# ------------------------------------------------------------------------------
# 내부: DXC 런타임 DLL 경로 탐색 (vcpkg → DIRECTX_DXC_TOOL → engine_config)
# ------------------------------------------------------------------------------
function(sw_find_dxc_runtime_dlls OUT_DXC OUT_DXIL)
	set(_dxc "")
	set(_dxil "")

	if(COMMAND sw_get_vcpkg_paths)
		sw_get_vcpkg_paths(_inc_dirs _bin_dirs)
		foreach(_dir IN LISTS _bin_dirs)
			if(_dxc STREQUAL "" AND EXISTS "${_dir}/dxcompiler.dll")
				set(_dxc "${_dir}/dxcompiler.dll")
			endif()
			if(_dxil STREQUAL "" AND EXISTS "${_dir}/dxil.dll")
				set(_dxil "${_dir}/dxil.dll")
			endif()
		endforeach()

		if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
			set(_tools "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc")
			if(_dxc STREQUAL "" AND EXISTS "${_tools}/dxcompiler.dll")
				set(_dxc "${_tools}/dxcompiler.dll")
			endif()
			if(_dxil STREQUAL "" AND EXISTS "${_tools}/dxil.dll")
				set(_dxil "${_tools}/dxil.dll")
			endif()
		endif()
	endif()

	if(_dxc STREQUAL "" AND DEFINED DIRECTX_DXC_TOOL AND EXISTS "${DIRECTX_DXC_TOOL}")
		get_filename_component(_tool_dir "${DIRECTX_DXC_TOOL}" DIRECTORY)
		if(EXISTS "${_tool_dir}/dxcompiler.dll")
			set(_dxc "${_tool_dir}/dxcompiler.dll")
		endif()
		if(_dxil STREQUAL "" AND EXISTS "${_tool_dir}/dxil.dll")
			set(_dxil "${_tool_dir}/dxil.dll")
		endif()
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
# @brief DXC 런타임 DLL을 타겟 복사 큐에 등록 (실제 복사는 sw_emit_runtime_copies)
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
