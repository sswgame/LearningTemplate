# ==============================================================================
# @file cmake/internal/Targets.cmake
# @brief 출력 디렉터리, 글로벌 옵션, DLL 익스포트 및 런타임 유틸리티
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 출력 경로 — Ninja 단일 설정 → 평탄한 Bin/Lib (LiveReload와 동일)
# ------------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${sw_output_directory}/Bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${sw_output_directory}/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${sw_output_directory}/Lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Lib")

if(EXISTS "${CMAKE_SOURCE_DIR}/Resource")
	install(DIRECTORY "${CMAKE_SOURCE_DIR}/Resource" DESTINATION .)
endif()

# ------------------------------------------------------------------------------
# 2) Dev / Shipping 레이아웃 및 글로벌 옵션 (Modern CMake INTERFACE)
# ------------------------------------------------------------------------------
add_library(sw_global_options INTERFACE)

if(SW_SHIPPING_BUILD)
	target_compile_definitions(sw_global_options INTERFACE SW_SHIPPING)
	set(SW_RHI_AS_MODULES OFF CACHE BOOL "RHI 백엔드(DX11/DX12/GL/Vulkan)를 MODULE 플러그인으로 빌드" FORCE)
	message(STATUS "[BuildConfig] Shipping: Engine/SWGame STATIC, Editor off (SW_SHIPPING_BUILD=ON)")
	message(STATUS "[BuildConfig] SW_RHI_AS_MODULES=OFF (CACHE FORCE)")
else()
	message(STATUS "[BuildConfig] Dev: Engine SHARED, Editor/SWGame MODULE (type=${CMAKE_BUILD_TYPE})")
endif()

# 두 매크로 모두 헤더(Mutex.h)와 전역 연산자에 영향을 주므로 모든 타겟에 동일하게 적용해야 한다.
if(SW_ENABLE_DEADLOCK_DETECTION)
	target_compile_definitions(sw_global_options INTERFACE SW_ENABLE_DEADLOCK_DETECTION)
	message(STATUS "[BuildConfig] sw::Mutex deadlock detection enabled (slow)")
endif()
if(SW_ENABLE_STL_CONTAINER)
	target_compile_definitions(sw_global_options INTERFACE SW_ENABLE_STL_CONTAINER)
	message(STATUS "[BuildConfig] SW_ENABLE_STL_CONTAINER=ON -> Using std::allocator for containers")
endif()

# ------------------------------------------------------------------------------
# 3) IPO — 전역 CMAKE_INTERPROCEDURAL_OPTIMIZATION (Release)
# ------------------------------------------------------------------------------
set(sw_ipo_supported FALSE)
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
	include(CheckIPOSupported)
	check_ipo_supported(RESULT sw_ipo_supported OUTPUT sw_ipo_error)
	if(sw_ipo_supported)
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
		message(STATUS "[BuildConfig] IPO enabled globally (opt-out for Tools/Tests/Editor)")
	elseif(sw_ipo_error)
		message(STATUS "[BuildConfig] IPO unsupported: ${sw_ipo_error}")
	endif()
endif()

# ------------------------------------------------------------------------------
# 4) DLL export 매크로 — Engine / GameFramework가 공유
# ------------------------------------------------------------------------------
# SHARED Engine: SW_EXPORTS / SW_IMPORTS, 옵션으로 WINDOWS_EXPORT_ALL_SYMBOLS
function(sw_configureEngineDllExports TARGET_NAME LIB_TYPE)
	if(LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_EXPORTS)
		target_compile_definitions(${TARGET_NAME} INTERFACE SW_IMPORTS)
		if(WIN32 AND SW_WINDOWS_EXPORT_ALL_SYMBOLS)
			set_target_properties(${TARGET_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
			message(STATUS "[Exports] ${TARGET_NAME}: WINDOWS_EXPORT_ALL_SYMBOLS=ON")
		endif()
	endif()
endfunction()

# GameFramework·Kit SHARED export.
function(sw_configureGfExports TARGET_NAME LIB_TYPE)
	if(LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_GF_EXPORTS)
		target_compile_definitions(${TARGET_NAME} INTERFACE SW_GF_IMPORTS)
		if(WIN32 AND SW_WINDOWS_EXPORT_ALL_SYMBOLS)
			set_target_properties(${TARGET_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
		endif()
	endif()
endfunction()

# MODULE/핫리로드 타겟의 런타임 출력을 Bin/으로 고정합니다.
function(sw_setModuleBinOutput TARGET_NAME)
	set_target_properties(${TARGET_NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${sw_output_directory}/Bin"
		LIBRARY_OUTPUT_DIRECTORY "${sw_output_directory}/Bin"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Bin"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Bin"
		LIBRARY_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Bin"
		LIBRARY_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Bin"
	)
endfunction()

# ------------------------------------------------------------------------------
# 5) Windows delay-load + 훅 소스 바인딩
# ------------------------------------------------------------------------------
function(sw_addDelayloadHook TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "DLLS" ${ARGN})
	if(NOT WIN32)
		return()
	endif()
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "sw_addDelayloadHook: target '${TARGET_NAME}' does not exist")
	endif()
	set(swHookSrc "")
	if(TARGET Engine)
		get_property(swHookSrc TARGET Engine PROPERTY SW_DELAYLOAD_HOOK_SOURCE)
	endif()
	if(NOT swHookSrc OR NOT EXISTS "${swHookSrc}")
		set(swHookSrc "${CMAKE_SOURCE_DIR}/Source/Engine/Utility/Module/DelayLoadNotifyHook.cpp")
		if(NOT EXISTS "${swHookSrc}")
			message(WARNING "[sw_addDelayloadHook] DelayLoadNotifyHook.cpp not found: ${swHookSrc}")
		endif()
	endif()
	target_sources(${TARGET_NAME} PRIVATE "${swHookSrc}")
	target_link_libraries(${TARGET_NAME} PRIVATE delayimp)
	foreach(dll IN LISTS ARG_DLLS)
		target_link_options(${TARGET_NAME} PRIVATE "LINKER:/DELAYLOAD:${dll}")
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# 6) 런타임 파일 복사 큐 — POST_BUILD는 sw_emitRuntimeCopies가 한 번에 방출
# ------------------------------------------------------------------------------
function(sw_queueRuntimeCopy TARGET_NAME SRC_FILE)
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "sw_queueRuntimeCopy: target '${TARGET_NAME}' does not exist")
	endif()
	if(NOT SRC_FILE OR NOT EXISTS "${SRC_FILE}")
		return()
	endif()
	set_property(TARGET ${TARGET_NAME} APPEND PROPERTY SW_RUNTIME_COPY_FILES "${SRC_FILE}")
endfunction()

function(sw_emitRuntimeCopies TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "sw_emitRuntimeCopies: target '${TARGET_NAME}' does not exist")
	endif()
	get_property(already TARGET ${TARGET_NAME} PROPERTY SW_RUNTIME_COPIES_EMITTED)
	if(already)
		return()
	endif()
	get_property(files TARGET ${TARGET_NAME} PROPERTY SW_RUNTIME_COPY_FILES)
	if(NOT files)
		return()
	endif()
	list(REMOVE_DUPLICATES files)
	set(names "")
	set(commands "")
	foreach(src IN LISTS files)
		get_filename_component(name "${src}" NAME)
		list(APPEND names "${name}")
		list(APPEND commands
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${src}"
				"$<TARGET_FILE_DIR:${TARGET_NAME}>/${name}"
		)
	endforeach()
	list(JOIN names ", " summary)
	add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
		${commands}
		COMMENT "[${TARGET_NAME}] Runtime deps: ${summary}"
		VERBATIM
	)
	set_property(TARGET ${TARGET_NAME} PROPERTY SW_RUNTIME_COPIES_EMITTED TRUE)
endfunction()

# ------------------------------------------------------------------------------
# 7) ThirdParty 래퍼 — SYSTEM include / vcpkg CONFIG / STATIC 폴백
# ------------------------------------------------------------------------------
function(sw_thirdPartySystemIncludes TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "INTERFACE;PUBLIC;PRIVATE" ${ARGN})
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "[ThirdParty] Target not found: ${TARGET_NAME}")
	endif()
	if(ARG_INTERFACE)
		target_include_directories(${TARGET_NAME} SYSTEM INTERFACE ${ARG_INTERFACE})
	endif()
	if(ARG_PUBLIC)
		target_include_directories(${TARGET_NAME} SYSTEM PUBLIC ${ARG_PUBLIC})
	endif()
	if(ARG_PRIVATE)
		target_include_directories(${TARGET_NAME} SYSTEM PRIVATE ${ARG_PRIVATE})
	endif()
endfunction()

function(sw_addVcpkgConfigLib)
	cmake_parse_arguments(ARG "HEADER_ONLY;ATTACH_GLOBAL" "NAME;PACKAGE;CONFIG_TARGET" "" ${ARGN})
	if(NOT ARG_NAME)
		message(FATAL_ERROR "sw_addVcpkgConfigLib: NAME required")
	endif()
	if(NOT ARG_PACKAGE)
		set(ARG_PACKAGE ${ARG_NAME})
	endif()
	if(NOT ARG_CONFIG_TARGET)
		set(ARG_CONFIG_TARGET "${ARG_NAME}::${ARG_NAME}")
	endif()
	find_package(${ARG_PACKAGE} CONFIG QUIET)
	if(TARGET ${ARG_CONFIG_TARGET})
		add_library(${ARG_NAME} INTERFACE)
		target_link_libraries(${ARG_NAME} INTERFACE ${ARG_CONFIG_TARGET})
	else()
		add_library(${ARG_NAME} INTERFACE)
		if(NOT TARGET ${ARG_CONFIG_TARGET})
			add_library(${ARG_CONFIG_TARGET} ALIAS ${ARG_NAME})
		endif()
		if(ARG_HEADER_ONLY AND COMMAND sw_linkVcpkgHeaderOnlyTarget)
			sw_linkVcpkgHeaderOnlyTarget(${ARG_NAME})
		endif()
	endif()
	if(ARG_ATTACH_GLOBAL AND TARGET sw_third_party_includes)
		target_link_libraries(sw_third_party_includes INTERFACE ${ARG_NAME})
	endif()
endfunction()

function(sw_addVcpkgStaticLib)
	cmake_parse_arguments(ARG "" "NAME;PACKAGE;CONFIG_TARGET;HEADER;LIB_BASENAME;WARN" "" ${ARGN})
	if(NOT ARG_NAME OR NOT ARG_HEADER OR NOT ARG_LIB_BASENAME)
		message(FATAL_ERROR "sw_addVcpkgStaticLib: NAME, HEADER, LIB_BASENAME required")
	endif()
	if(NOT ARG_PACKAGE)
		set(ARG_PACKAGE ${ARG_NAME})
	endif()
	if(NOT ARG_CONFIG_TARGET)
		set(ARG_CONFIG_TARGET "${ARG_NAME}::${ARG_NAME}")
	endif()
	find_package(${ARG_PACKAGE} CONFIG QUIET)
	if(TARGET ${ARG_CONFIG_TARGET})
		add_library(${ARG_NAME} INTERFACE)
		target_link_libraries(${ARG_NAME} INTERFACE ${ARG_CONFIG_TARGET})
		return()
	endif()
	if(TARGET ${ARG_NAME})
		return()
	endif()

	set(root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
	if(NOT EXISTS "${root}/include/${ARG_HEADER}")
		if(ARG_WARN)
			message(WARNING "${ARG_WARN}")
		else()
			message(WARNING "[${ARG_NAME}] vcpkg 설치 트리에서 헤더 '${ARG_HEADER}'를 찾을 수 없습니다 (${root}/include).")
		endif()
		add_library(${ARG_NAME} INTERFACE)
		return()
	endif()

	add_library(${ARG_CONFIG_TARGET} STATIC IMPORTED GLOBAL)
	if(WIN32)
		set(dbg "${root}/debug/lib/${ARG_LIB_BASENAME}d.lib")
		if(NOT EXISTS "${dbg}")
			set(dbg "${root}/debug/lib/${ARG_LIB_BASENAME}.lib")
		endif()
		set(rel "${root}/lib/${ARG_LIB_BASENAME}.lib")
	else()
		set(dbg "${root}/debug/lib/lib${ARG_LIB_BASENAME}d.a")
		if(NOT EXISTS "${dbg}")
			set(dbg "${root}/debug/lib/lib${ARG_LIB_BASENAME}.a")
		endif()
		set(rel "${root}/lib/lib${ARG_LIB_BASENAME}.a")
	endif()
	set_target_properties(${ARG_CONFIG_TARGET} PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${root}/include"
		IMPORTED_LOCATION_DEBUG "${dbg}"
		IMPORTED_LOCATION_RELEASE "${rel}"
		IMPORTED_LOCATION_RELWITHDEBINFO "${rel}"
		IMPORTED_LOCATION_MINSIZEREL "${rel}"
		IMPORTED_LOCATION "${rel}"
	)
	add_library(${ARG_NAME} INTERFACE)
	target_link_libraries(${ARG_NAME} INTERFACE ${ARG_CONFIG_TARGET})
	message(STATUS "[${ARG_NAME}] Using vcpkg installed tree (manual import)")
endfunction()
