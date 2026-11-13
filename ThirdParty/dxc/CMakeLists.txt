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

# ------------------------------------------------------------------------------
# 1) 헤더 INTERFACE — 전 플랫폼 include, Windows만 import lib
#    macOS 등 패키지 없으면 include 경로만
# ------------------------------------------------------------------------------
find_package(directx-dxc CONFIG QUIET)

add_library(dxc INTERFACE)

if(TARGET Microsoft::DirectXShaderCompiler)
	target_include_directories(dxc SYSTEM INTERFACE
		$<TARGET_PROPERTY:Microsoft::DirectXShaderCompiler,INTERFACE_INCLUDE_DIRECTORIES>
	)
elseif(TARGET directx-dxc::directx-dxc)
	target_include_directories(dxc SYSTEM INTERFACE
		$<TARGET_PROPERTY:directx-dxc::directx-dxc,INTERFACE_INCLUDE_DIRECTORIES>
	)
else()
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
# 2) sw_ensure_dxil_linux_soname_symlink — libdxil.so → libdxildll.so
#    Linux SONAME 불일치. LIB_DIR
# ------------------------------------------------------------------------------
function(sw_ensure_dxil_linux_soname_symlink LIB_DIR)
	if(NOT LIB_DIR OR NOT EXISTS "${LIB_DIR}/libdxil.so")
		return()
	endif()
	set(link "${LIB_DIR}/libdxildll.so")
	if(EXISTS "${link}" OR IS_SYMLINK "${link}")
		return()
	endif()
	file(CREATE_LINK "libdxil.so" "${link}" SYMBOLIC RESULT link_result)
	if(link_result)
		message(WARNING "[dxc] Failed to create ${link} -> libdxil.so (${link_result})")
	else()
		message(STATUS "[dxc] Created Linux DXIL SONAME link: ${link} -> libdxil.so")
	endif()
endfunction()

if(UNIX AND NOT APPLE)
	set(dxil_dirs "")
	if(TARGET Microsoft::DXIL)
		foreach(cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
			get_target_property(loc Microsoft::DXIL IMPORTED_LOCATION_${cfg})
			if(loc)
				get_filename_component(dir "${loc}" DIRECTORY)
				list(APPEND dxil_dirs "${dir}")
			endif()
		endforeach()
		get_target_property(loc Microsoft::DXIL IMPORTED_LOCATION)
		if(loc)
			get_filename_component(dir "${loc}" DIRECTORY)
			list(APPEND dxil_dirs "${dir}")
		endif()
	endif()
	if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
		list(APPEND dxil_dirs
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
		)
	endif()
	list(REMOVE_DUPLICATES dxil_dirs)
	foreach(dir IN LISTS dxil_dirs)
		sw_ensure_dxil_linux_soname_symlink("${dir}")
	endforeach()
endif()

# ------------------------------------------------------------------------------
# 3) sw_find_dxc_runtime_dlls — dxcompiler / dxil 런타임 경로
#    OUT_DXC, OUT_DXIL. macOS는 공식 프리빌드 없음
# ------------------------------------------------------------------------------
function(sw_find_dxc_runtime_dlls OUT_DXC OUT_DXIL)
	set(dxc "")
	set(dxil "")

	if(WIN32)
		set(dxc_names "dxcompiler.dll")
		set(dxil_names "dxil.dll")
	elseif(APPLE)
		set(dxc_names "libdxcompiler.dylib")
		set(dxil_names "libdxil.dylib" "libdxildll.dylib")
	else()
		set(dxc_names "libdxcompiler.so")
		set(dxil_names "libdxildll.so" "libdxil.so")
	endif()

	sw_get_vcpkg_paths(inc_dirs bin_dirs)
	set(search_dirs ${bin_dirs})
	if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
		list(APPEND search_dirs
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
		)
	endif()

	foreach(dir IN LISTS search_dirs)
		foreach(name IN LISTS dxc_names)
			if(dxc STREQUAL "" AND EXISTS "${dir}/${name}")
				set(dxc "${dir}/${name}")
			endif()
		endforeach()
		foreach(name IN LISTS dxil_names)
			if(dxil STREQUAL "" AND EXISTS "${dir}/${name}")
				set(dxil "${dir}/${name}")
			endif()
		endforeach()
	endforeach()

	if(DEFINED DIRECTX_DXC_TOOL AND EXISTS "${DIRECTX_DXC_TOOL}")
		get_filename_component(tool_dir "${DIRECTX_DXC_TOOL}" DIRECTORY)
		foreach(name IN LISTS dxc_names)
			if(dxc STREQUAL "" AND EXISTS "${tool_dir}/${name}")
				set(dxc "${tool_dir}/${name}")
			endif()
		endforeach()
		foreach(name IN LISTS dxil_names)
			if(dxil STREQUAL "" AND EXISTS "${tool_dir}/${name}")
				set(dxil "${tool_dir}/${name}")
			endif()
		endforeach()
	endif()

	# DXC 는 vcpkg 의 directx-dxc 하나만 쓴다 — 예전에는 여기서 못 찾으면 Windows SDK 를 재귀로 훑은 경로(toolchain_config.json)로 넘어갔는데,
	# 그 탐색이 amd64 호스트에서 `Redist/D3D/arm64/` 를 먼저 집었다(폴백이 판 · 아키텍처를 고르지 못한다). vcpkg 가 이 플랫폼(vcpkg.json 의
	# "windows | linux")에 까는데 없으면 구성을 세운다. 다른 플랫폼(웹 · Android · macOS)은 실행 파일 옆에 DXC 를 두지 않는다(쿠킹은 호스트 빌드가 한다).
	if(CMAKE_SYSTEM_NAME MATCHES "^(Windows|Linux)$" AND (dxc STREQUAL "" OR dxil STREQUAL ""))
		message(FATAL_ERROR "[dxc] ${dxc_names} / ${dxil_names} not found in the vcpkg install (${search_dirs}) - "
			"install the vcpkg port directx-dxc (vcpkg.json, platform \"windows | linux\") by re-running the configure preset with vcpkg manifest install, "
			"or check VCPKG_INSTALLED_DIR / VCPKG_TARGET_TRIPLET")
	endif()

	set(${OUT_DXC} "${dxc}" PARENT_SCOPE)
	set(${OUT_DXIL} "${dxil}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 4) sw_copy_dxc_dlls — 런타임 복사 큐 등록 (실제 복사는 sw_emit_runtime_copies)
# ------------------------------------------------------------------------------
function(sw_copy_dxc_dlls TARGET_NAME)
	sw_find_dxc_runtime_dlls(dxc_dll dxil_dll)
	if(dxc_dll)
		sw_queue_runtime_copy(${TARGET_NAME} "${dxc_dll}")
	endif()
	if(dxil_dll)
		sw_queue_runtime_copy(${TARGET_NAME} "${dxil_dll}")
	endif()
endfunction()
