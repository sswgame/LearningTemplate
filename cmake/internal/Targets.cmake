# ==============================================================================
# @file cmake/internal/Targets.cmake
# @brief 타겟 생성·설치·출력·Dev/Shipping·ThirdParty 래퍼 헬퍼 (단일 진입점)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 출력 경로 — Ninja 단일 설정 → 평탄한 Bin/Lib (LiveReload와 동일)
# ------------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${sw_output_directory}/Bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${sw_output_directory}/Lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Lib")

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

# 런타임이 아닌 타겟(도구, 에디터 등)에서 IPO를 끕니다.
function(sw_disableTargetIpo TARGET_NAME)
	if(sw_ipo_supported AND TARGET ${TARGET_NAME})
		set_property(TARGET ${TARGET_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
	endif()
endfunction()

# ------------------------------------------------------------------------------
# 4) 설치 · 하위 프로젝트 탐색
# ------------------------------------------------------------------------------
# 타겟을 Bin/Lib 레이아웃으로 설치합니다.
function(sw_installTarget TARGET_NAME)
	install(TARGETS ${TARGET_NAME}
		RUNTIME DESTINATION Bin
		LIBRARY DESTINATION Lib
		ARCHIVE DESTINATION Lib
	)
endfunction()

# BASE_DIR 아래 CMakeLists.txt가 있는 하위 디렉터리를 add_subdirectory 합니다.
function(sw_discoverProjects BASE_DIR)
	if(NOT EXISTS "${BASE_DIR}")
		return()
	endif()
	file(GLOB children RELATIVE "${BASE_DIR}" "${BASE_DIR}/*")
	foreach(child IN LISTS children)
		set(childPath "${BASE_DIR}/${child}")
		if(IS_DIRECTORY "${childPath}" AND EXISTS "${childPath}/CMakeLists.txt")
			add_subdirectory("${childPath}")
		endif()
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# 5) DLL export 매크로 — Engine / MODULE / GameFramework가 공유
# ------------------------------------------------------------------------------
# SHARED Engine: SW_EXPORTS / SW_IMPORTS, 옵션으로 WINDOWS_EXPORT_ALL_SYMBOLS
function(sw_configureEngineDllExports TARGET_NAME LIB_TYPE)
	if(LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_EXPORTS)
		target_compile_definitions(${TARGET_NAME} INTERFACE SW_IMPORTS)
		if(WIN32 AND SW_WINDOWS_EXPORT_ALL_SYMBOLS)
			set_target_properties(${TARGET_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
			message(STATUS "[Exports] ${TARGET_NAME}: WINDOWS_EXPORT_ALL_SYMBOLS=ON (set -DSW_WINDOWS_EXPORT_ALL_SYMBOLS=OFF when SW_API surface is complete)")
		endif()
	endif()
endfunction()

# MODULE 타겟에 SW_MODULE_EXPORTS를 정의합니다.
function(sw_configureModuleDllExports TARGET_NAME LIB_TYPE)
	if(LIB_TYPE STREQUAL "MODULE")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_MODULE_EXPORTS)
	endif()
endfunction()

# GameFramework·Kit SHARED export. sw_addGfKit / GameFramework CMakeLists 양쪽에서 호출.
function(sw_configureGfExports TARGET_NAME LIB_TYPE)
	if(LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_GF_EXPORTS)
		target_compile_definitions(${TARGET_NAME} INTERFACE SW_GF_IMPORTS)
		if(WIN32 AND SW_WINDOWS_EXPORT_ALL_SYMBOLS)
			set_target_properties(${TARGET_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
		endif()
	endif()
endfunction()

# ------------------------------------------------------------------------------
# 6) 로그 태그 · 소스 GLOB
#    Ninja는 CONFIGURE_DEPENDS를 지원하므로 기본 ON — 파일 추가/삭제를 자동 감지
#    재구성 비용이 문제가 되면 -DSW_GLOB_CONFIGURE_DEPENDS=OFF (수동 재구성 필요)
# ------------------------------------------------------------------------------
# 타겟에 SW_LOG_TAG 컴파일 정의를 설정합니다.
function(sw_setLogTag TARGET_NAME TAG)
	target_compile_definitions(${TARGET_NAME} PRIVATE "SW_LOG_TAG=\"${TAG}\"")
endfunction()

# 디렉터리에서 소스/헤더를 GLOB하여 출력 변수에 담습니다.
function(sw_collectSources DIR_PATH OUT_SOURCES)
	set(globMode "")
	if(SW_GLOB_CONFIGURE_DEPENDS)
		set(globMode CONFIGURE_DEPENDS)
	endif()
	file(GLOB_RECURSE sourcesList ${globMode} "${DIR_PATH}/*.cpp" "${DIR_PATH}/*.c")
	file(GLOB_RECURSE headersList ${globMode} "${DIR_PATH}/*.h" "${DIR_PATH}/*.hpp" "${DIR_PATH}/*.inl")
	list(APPEND sourcesList ${headersList})
	set(${OUT_SOURCES} ${sourcesList} PARENT_SCOPE)
endfunction()

# 현재 소스 디렉터리를 수집하고 SOURCES/EXCLUDE 인자를 적용합니다.
macro(sw_prepareTargetSources OUT_SOURCES ARG_SOURCES ARG_EXCLUDE)
	sw_collectSources("${CMAKE_CURRENT_SOURCE_DIR}" ${OUT_SOURCES})
	if(${ARG_SOURCES})
		list(APPEND ${OUT_SOURCES} ${${ARG_SOURCES}})
	endif()
	if(${ARG_EXCLUDE})
		foreach(exPattern IN LISTS ${ARG_EXCLUDE})
			list(FILTER ${OUT_SOURCES} EXCLUDE REGEX "${exPattern}")
		endforeach()
	endif()
endmacro()

# ------------------------------------------------------------------------------
# 7) 공통 타겟 속성 — include / 링크 / IDE 폴더 / 설치
#    PUBLIC include = 로컬 소스 dir. PRIVATE = sw_public_source_includes (자기 TU만)
#    소비자 전파는 Engine·Core·Kits가 sw_public_source_includes를 PUBLIC 링크
# ------------------------------------------------------------------------------
macro(sw_setupTargetProperties TARGET_NAME ARG_INCLUDE_DIRECTORIES ARG_LINK_LIBRARIES SKIP_INSTALL)
	get_target_property(_target_type ${TARGET_NAME} TYPE)
	if(_target_type STREQUAL "INTERFACE_LIBRARY")
		set(_scope INTERFACE)
		set(_link_scope INTERFACE)
	else()
		set(_scope PUBLIC)
		set(_link_scope PRIVATE)
	endif()

	target_include_directories(${TARGET_NAME} ${_scope} "${CMAKE_CURRENT_SOURCE_DIR}")
	if(TARGET sw_public_source_includes)
		target_link_libraries(${TARGET_NAME} ${_link_scope} sw_public_source_includes)
	endif()
	if(${ARG_INCLUDE_DIRECTORIES})
		target_include_directories(${TARGET_NAME} ${_scope} ${${ARG_INCLUDE_DIRECTORIES}})
	endif()
	set(linkLibs "")
	if(${ARG_LINK_LIBRARIES})
		list(APPEND linkLibs ${${ARG_LINK_LIBRARIES}})
	endif()
	if(sw_flag_libraries)
		list(APPEND linkLibs ${sw_flag_libraries})
	endif()
	list(APPEND linkLibs sw_global_options)
	if(linkLibs)
		target_link_libraries(${TARGET_NAME} ${_link_scope} ${linkLibs})
	endif()
	file(RELATIVE_PATH relPath "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
	get_filename_component(folderPath "${relPath}" DIRECTORY)
	if(folderPath)
		set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "${folderPath}")
	endif()
	if(NOT SKIP_INSTALL)
		sw_installTarget(${TARGET_NAME})
	endif()
endmacro()

# ------------------------------------------------------------------------------
# 8) 라이브러리 · 실행 파일 생성 래퍼
#    - TYPE: STATIC | SHARED | MODULE | INTERFACE
#    - NO_INSTALL: 설치(Bin/Lib) 대상에서 제외
#    - LINK_LIBRARIES: 종속 라이브러리 목록
#    - EXCLUDE: 소스 GLOB 필터링 정규식
#    - SOURCES: 명시적 추가 소스 파일 목록
#    - INCLUDE_DIRECTORIES: 추가 헤더 경로
#    - LOG_TAG: SW_LOG_TAG 매크로 정의 ("Engine", "App", "Core" 등)
#    - PCH: 프리컴파일 헤더 활성화
#    - BIN_OUTPUT: LiveReload용 바이너리 출력 디렉터리를 Bin/으로 강제 고정
# ------------------------------------------------------------------------------

# 라이브러리 타겟을 생성하고 공통 속성(include/링크/PCH/출력/설치)을 설정합니다.
function(sw_addLibrary TARGET_NAME)
	cmake_parse_arguments(ARG
		"NO_INSTALL;PCH;BIN_OUTPUT"
		"TYPE;LOG_TAG"
		"LINK_LIBRARIES;EXCLUDE;SOURCES;INCLUDE_DIRECTORIES"
		${ARGN}
	)
	set(LIB_TYPE STATIC)
	if(ARG_TYPE)
		set(LIB_TYPE ${ARG_TYPE})
	endif()
	sw_prepareTargetSources(TARGET_SOURCES ARG_SOURCES ARG_EXCLUDE)
	add_library(${TARGET_NAME} ${LIB_TYPE} ${TARGET_SOURCES})
	sw_setupTargetProperties(${TARGET_NAME} ARG_INCLUDE_DIRECTORIES ARG_LINK_LIBRARIES ${ARG_NO_INSTALL})

	if(LIB_TYPE STREQUAL "MODULE")
		sw_configureModuleDllExports(${TARGET_NAME} MODULE)
		sw_setModuleBinOutput(${TARGET_NAME})
	elseif(ARG_BIN_OUTPUT)
		sw_setModuleBinOutput(${TARGET_NAME})
	endif()
	if(ARG_LOG_TAG)
		sw_setLogTag(${TARGET_NAME} ${ARG_LOG_TAG})
	endif()
	if(ARG_PCH)
		sw_configurePch(${TARGET_NAME})
	endif()
endfunction()

# 실행 파일(exe) 타겟을 생성하고 공통 속성을 설정합니다.
function(sw_addExecutable TARGET_NAME)
	cmake_parse_arguments(ARG
		"NO_INSTALL;PCH"
		"LOG_TAG"
		"LINK_LIBRARIES;EXCLUDE;SOURCES;INCLUDE_DIRECTORIES"
		${ARGN}
	)
	sw_prepareTargetSources(TARGET_SOURCES ARG_SOURCES ARG_EXCLUDE)
	add_executable(${TARGET_NAME} ${TARGET_SOURCES})
	sw_setupTargetProperties(${TARGET_NAME} ARG_INCLUDE_DIRECTORIES ARG_LINK_LIBRARIES ${ARG_NO_INSTALL})
	if(ARG_LOG_TAG)
		sw_setLogTag(${TARGET_NAME} ${ARG_LOG_TAG})
	endif()
	if(ARG_PCH)
		sw_configurePch(${TARGET_NAME})
	endif()
endfunction()

# ------------------------------------------------------------------------------
# 9) sw_configurePch — PCH_FILE 명시 또는 타겟명 폴백
#    Core / Core_objects / ReflectionParser → Core/pch.h, 나머지 → Engine/pch.h
# ------------------------------------------------------------------------------
function(sw_configurePch TARGET_NAME)
	cmake_parse_arguments(ARG "" "PCH_FILE" "" ${ARGN})
	if(NOT SW_ENABLE_PCH)
		return()
	endif()
	if(ARG_PCH_FILE)
		if(EXISTS "${ARG_PCH_FILE}")
			target_precompile_headers(${TARGET_NAME} PRIVATE "${ARG_PCH_FILE}")
		else()
			message(WARNING "[sw_configurePch] PCH_FILE not found: ${ARG_PCH_FILE} (target: ${TARGET_NAME})")
		endif()
		return()
	endif()
	if(TARGET_NAME STREQUAL "Core" OR TARGET_NAME STREQUAL "Core_objects" OR TARGET_NAME STREQUAL "ReflectionParser")
		set(swPch "${CMAKE_SOURCE_DIR}/Source/Core/pch.h")
	elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
		set(swPch "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
	else()
		set(swPch "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
	endif()
	if(EXISTS "${swPch}")
		target_precompile_headers(${TARGET_NAME} PRIVATE "${swPch}")
	endif()
endfunction()

# ------------------------------------------------------------------------------
# 10) sw_addDelayloadHook — Windows delay-load + 훅 소스
#     훅 경로: Engine 타겟 SW_DELAYLOAD_HOOK_SOURCE, 없으면 기본 경로 폴백
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
# 11) sw_addGfKit — Dev SHARED / Shipping STATIC + delay-load + PCH
#     export는 sw_configureGfExports로 중앙화 (GameFramework CMakeLists와 공유)
# ------------------------------------------------------------------------------
function(sw_addGfKit TARGET_NAME)
	cmake_parse_arguments(ARG "" "LOG_TAG" "LINK_LIBRARIES;DELAYLOAD_DLLS" ${ARGN})
	if(SW_SHIPPING_BUILD)
		set(kitType STATIC)
	else()
		set(kitType SHARED)
	endif()
	if(NOT ARG_LOG_TAG)
		set(ARG_LOG_TAG ${TARGET_NAME})
	endif()
	sw_addLibrary(${TARGET_NAME}
		TYPE ${kitType}
		LINK_LIBRARIES ${ARG_LINK_LIBRARIES}
		LOG_TAG ${ARG_LOG_TAG}
		PCH
		BIN_OUTPUT
	)
	sw_configureGfExports(${TARGET_NAME} ${kitType})
	if(kitType STREQUAL "SHARED" AND WIN32 AND ARG_DELAYLOAD_DLLS)
		sw_addDelayloadHook(${TARGET_NAME} DLLS ${ARG_DELAYLOAD_DLLS})
	endif()
	if(TARGET sw_public_source_includes)
		target_link_libraries(${TARGET_NAME} PUBLIC sw_public_source_includes)
	endif()
	set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES ${TARGET_NAME})
endfunction()

# ------------------------------------------------------------------------------
# 12) 런타임 파일 복사 큐 — POST_BUILD는 sw_emitRuntimeCopies가 한 번에 방출
# ------------------------------------------------------------------------------
# POST_BUILD에서 복사할 런타임 파일을 타겟 속성에 큐잉합니다.
function(sw_queueRuntimeCopy TARGET_NAME SRC_FILE)
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "sw_queueRuntimeCopy: target '${TARGET_NAME}' does not exist")
	endif()
	if(NOT SRC_FILE OR NOT EXISTS "${SRC_FILE}")
		return()
	endif()
	set_property(TARGET ${TARGET_NAME} APPEND PROPERTY SW_RUNTIME_COPY_FILES "${SRC_FILE}")
endfunction()

# 큐잉된 런타임 파일 복사를 POST_BUILD 커스텀 커맨드로 방출합니다.
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
# 13) ThirdParty 래퍼 — SYSTEM include / vcpkg CONFIG / STATIC 폴백
# ------------------------------------------------------------------------------
# ThirdParty include를 SYSTEM으로 타겟에 연결합니다.
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

# find_package CONFIG 타겟을 INTERFACE 별칭으로 감쌉니다.
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

# find_package 실패 시 vcpkg installed 트리에서 STATIC IMPORTED를 만듭니다.
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
	# find_package가 비네임스페이스 타겟만 만든 경우 — 그대로 사용
	if(TARGET ${ARG_NAME})
		return()
	endif()

	set(root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
	if(NOT EXISTS "${root}/include/${ARG_HEADER}")
		# 조용히 무시하면 런타임에야 깨지므로 항상 경고
		if(ARG_WARN)
			message(WARNING "${ARG_WARN}")
		else()
			message(WARNING "[${ARG_NAME}] vcpkg 설치 트리에서 헤더 '${ARG_HEADER}'를 찾을 수 없습니다 "
				"(${root}/include). 빈 INTERFACE 타겟으로 대체합니다. "
				"해당 타겟을 링크하는 소비자는 런타임 오류가 발생할 수 있습니다.")
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
