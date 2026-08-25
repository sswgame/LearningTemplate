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
	sw_linkVcpkgHeaderOnlyTarget(dxc)
	if(APPLE)
		message(STATUS "[dxc] directx-dxc is not available on macOS (headers-only / runtime optional)")
	endif()
endif()

if(TARGET sw_third_party_includes)
	target_link_libraries(sw_third_party_includes INTERFACE dxc)
endif()

# ------------------------------------------------------------------------------
# 2) sw_ensureDxilLinuxSonameSymlink — libdxil.so → libdxildll.so
#    Linux SONAME 불일치. LIB_DIR
# ------------------------------------------------------------------------------
function(sw_ensureDxilLinuxSonameSymlink LIB_DIR)
	if(NOT LIB_DIR OR NOT EXISTS "${LIB_DIR}/libdxil.so")
		return()
	endif()
	set(link "${LIB_DIR}/libdxildll.so")
	if(EXISTS "${link}" OR IS_SYMLINK "${link}")
		return()
	endif()
	file(CREATE_LINK "libdxil.so" "${link}" SYMBOLIC RESULT linkResult)
	if(linkResult)
		message(WARNING "[dxc] Failed to create ${link} -> libdxil.so (${linkResult})")
	else()
		message(STATUS "[dxc] Created Linux DXIL SONAME link: ${link} -> libdxil.so")
	endif()
endfunction()

if(UNIX AND NOT APPLE)
	set(dxilDirs "")
	if(TARGET Microsoft::DXIL)
		foreach(cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
			get_target_property(loc Microsoft::DXIL IMPORTED_LOCATION_${cfg})
			if(loc)
				get_filename_component(dir "${loc}" DIRECTORY)
				list(APPEND dxilDirs "${dir}")
			endif()
		endforeach()
		get_target_property(loc Microsoft::DXIL IMPORTED_LOCATION)
		if(loc)
			get_filename_component(dir "${loc}" DIRECTORY)
			list(APPEND dxilDirs "${dir}")
		endif()
	endif()
	if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
		list(APPEND dxilDirs
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
		)
	endif()
	list(REMOVE_DUPLICATES dxilDirs)
	foreach(dir IN LISTS dxilDirs)
		sw_ensureDxilLinuxSonameSymlink("${dir}")
	endforeach()
endif()

# ------------------------------------------------------------------------------
# 3) sw_findDxcRuntimeDlls — dxcompiler / dxil 런타임 경로
#    OUT_DXC, OUT_DXIL. macOS는 공식 프리빌드 없음
# ------------------------------------------------------------------------------
function(sw_findDxcRuntimeDlls OUT_DXC OUT_DXIL)
	set(dxc "")
	set(dxil "")

	if(WIN32)
		set(dxcNames "dxcompiler.dll")
		set(dxilNames "dxil.dll")
	elseif(APPLE)
		set(dxcNames "libdxcompiler.dylib")
		set(dxilNames "libdxil.dylib" "libdxildll.dylib")
	else()
		set(dxcNames "libdxcompiler.so")
		set(dxilNames "libdxildll.so" "libdxil.so")
	endif()

	if(COMMAND sw_getVcpkgPaths)
		sw_getVcpkgPaths(incDirs binDirs)
		set(searchDirs ${binDirs})
		if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
			list(APPEND searchDirs
				"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
				"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
				"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
			)
		endif()

		foreach(dir IN LISTS searchDirs)
			foreach(name IN LISTS dxcNames)
				if(dxc STREQUAL "" AND EXISTS "${dir}/${name}")
					set(dxc "${dir}/${name}")
				endif()
			endforeach()
			foreach(name IN LISTS dxilNames)
				if(dxil STREQUAL "" AND EXISTS "${dir}/${name}")
					set(dxil "${dir}/${name}")
				endif()
			endforeach()
		endforeach()
	endif()

	if(DEFINED DIRECTX_DXC_TOOL AND EXISTS "${DIRECTX_DXC_TOOL}")
		get_filename_component(toolDir "${DIRECTX_DXC_TOOL}" DIRECTORY)
		foreach(name IN LISTS dxcNames)
			if(dxc STREQUAL "" AND EXISTS "${toolDir}/${name}")
				set(dxc "${toolDir}/${name}")
			endif()
		endforeach()
		foreach(name IN LISTS dxilNames)
			if(dxil STREQUAL "" AND EXISTS "${toolDir}/${name}")
				set(dxil "${toolDir}/${name}")
			endif()
		endforeach()
	endif()

	set(swEngineCfg "${CMAKE_SOURCE_DIR}/Config/Environment/toolchain_config.json")

	if(EXISTS "${swEngineCfg}")

		file(READ "${swEngineCfg}" cfg)
		if(dxc STREQUAL "")
			string(JSON cfgDxc ERROR_VARIABLE e1 GET "${cfg}" "dxc_dll_path")
			if(cfgDxc AND EXISTS "${cfgDxc}")
				set(dxc "${cfgDxc}")
			endif()
		endif()
		if(dxil STREQUAL "")
			string(JSON cfgDxil ERROR_VARIABLE e2 GET "${cfg}" "dxil_dll_path")
			if(cfgDxil AND EXISTS "${cfgDxil}")
				set(dxil "${cfgDxil}")
			endif()
		endif()
	endif()

	set(${OUT_DXC} "${dxc}" PARENT_SCOPE)
	set(${OUT_DXIL} "${dxil}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# 4) sw_copyDxcDlls — 런타임 복사 큐 등록 (실제 복사는 sw_emitRuntimeCopies)
# ------------------------------------------------------------------------------
function(sw_copyDxcDlls TARGET_NAME)
	sw_findDxcRuntimeDlls(dxcDll dxilDll)
	if(dxcDll)
		sw_queueRuntimeCopy(${TARGET_NAME} "${dxcDll}")
	endif()
	if(dxilDll)
		sw_queueRuntimeCopy(${TARGET_NAME} "${dxilDll}")
	endif()
endfunction()
