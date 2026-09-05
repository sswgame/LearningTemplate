# ==============================================================================
# @file cmake/Engine/ModuleBuildRules.cmake
# @brief 엔진 모듈, RHI 백엔드, 장르 키트 타겟 생성 헬퍼 및 링킹/익스포트 규칙
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
# SHARED Engine: SW_EXPORTS / SW_IMPORTS
function(sw_configureEngineDllExports TARGET_NAME LIB_TYPE)
	if(LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_EXPORTS)
		target_compile_definitions(${TARGET_NAME} INTERFACE SW_IMPORTS)
	endif()
endfunction()

# GameFramework·Kit SHARED: SW_GF_EXPORTS / SW_GF_IMPORTS
function(sw_configureGfExports TARGET_NAME LIB_TYPE)
	if(LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_GF_EXPORTS)
		target_compile_definitions(${TARGET_NAME} INTERFACE SW_GF_IMPORTS)
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

# App의 런타임/플러그인/모듈 의존성을 구성합니다.
function(sw_configureAppDependencies TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		return()
	endif()

	# 1) RHI 플러그인 빌드 순서 종속성 연결 (App이 런타임에 동적 로드)
	foreach(rhiMod IN ITEMS RHI_DX11 RHI_DX12 RHI_GL RHI_Vulkan)
		if(TARGET ${rhiMod})
			add_dependencies(${TARGET_NAME} ${rhiMod})
		endif()
	endforeach()

	# 2) Dev 에디터 모듈 빌드 순서 종속성 연결
	if(NOT SW_SHIPPING_BUILD AND TARGET EditorModule)
		add_dependencies(${TARGET_NAME} EditorModule)
	endif()

	# 3) Shipping / Dev 모듈 연결
	# Shipping: SWGame 정적 링크 및 CookAssets 자동 선행 실행
	# Dev: delay-load이므로 링크하지 않고 DLL이 App보다 먼저 빌드되도록 종속성만 연결
	if(SW_SHIPPING_BUILD)
		if(TARGET SWGame)
			target_link_libraries(${TARGET_NAME} PRIVATE SWGame)
		endif()

		if(TARGET CookAssets)
			add_dependencies(${TARGET_NAME} CookAssets)
		endif()
	else()
		if(TARGET SWGame)
			get_property(dynMods GLOBAL PROPERTY SW_DYNAMIC_MODULES)

			foreach(mod IN LISTS dynMods)
				if(TARGET ${mod})
					add_dependencies(${TARGET_NAME} ${mod})
				endif()
			endforeach()
		endif()
	endif()
endfunction()

# RHI 그래픽스 백엔드 MODULE 타겟을 정의하고 공통 속성을 바인딩합니다.
function(sw_addRhiBackendModule BACKEND_NAME GRAPHICS_LIB)
	cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
	add_library(${BACKEND_NAME} MODULE "${CMAKE_CURRENT_SOURCE_DIR}/ModuleEntry.cpp" ${ARG_SOURCES})

	target_include_directories(${BACKEND_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(${BACKEND_NAME}
		PRIVATE
		Engine
		${GRAPHICS_LIB}
		sw_third_party_includes
		sw_global_options
	)

	if(sw_flag_libraries)
		target_link_libraries(${BACKEND_NAME} PRIVATE ${sw_flag_libraries})
	endif()

	target_compile_definitions(${BACKEND_NAME}
		PRIVATE
		"SW_LOG_TAG=\"RHI\""
		SW_MODULE_EXPORTS
		SW_ENGINE_INTERNAL
	)
	sw_configurePch(${BACKEND_NAME} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
	sw_setModuleBinOutput(${BACKEND_NAME})
	set_target_properties(${BACKEND_NAME} PROPERTIES FOLDER "Source/Engine/Graphics/RHI/Modules")
endfunction()

# GameFramework 장르 키트 라이브러리 타겟을 정의하고 빌드 모드에 맞게 구성합니다.
function(sw_addGameFrameworkKit KIT_NAME)
	if(SW_SHIPPING_BUILD)
		set(kitType STATIC)
	else()
		set(kitType SHARED)
	endif()

	file(GLOB_RECURSE kitSources CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
	add_library(${KIT_NAME} ${kitType} ${kitSources})

	target_include_directories(${KIT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(${KIT_NAME}
		PUBLIC
		GameFramework
		Engine
		sw_public_source_includes
		PRIVATE
		sw_global_options
	)

	if(sw_flag_libraries)
		target_link_libraries(${KIT_NAME} PRIVATE ${sw_flag_libraries})
	endif()

	target_compile_definitions(${KIT_NAME} PRIVATE "SW_LOG_TAG=\"${KIT_NAME}\"")
	sw_configurePch(${KIT_NAME} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

	sw_configureGfExports(${KIT_NAME} ${kitType})

	if(kitType STREQUAL "SHARED")
		sw_setModuleBinOutput(${KIT_NAME})

		if(WIN32)
			sw_addDelayloadHook(${KIT_NAME} DLLS GameFramework.dll)
		endif()
	endif()

	set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES ${KIT_NAME})
	set_target_properties(${KIT_NAME} PROPERTIES FOLDER "Source/GameFramework/Kits")

	file(GLOB kitHeaders "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
	sw_addReflectionStep(${KIT_NAME}
		HEADERS ${kitHeaders}
		INCLUDES "${CMAKE_SOURCE_DIR}/Source"
	)
endfunction()

# 게임 팩 모듈(SWGame) 타겟을 정의하고 링크 및 리플렉션/딜레이로드를 구성합니다.
function(sw_addGameModule TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "KITS;HEADERS;EXCLUDE" ${ARGN})

	if(SW_SHIPPING_BUILD)
		set(gameLibType STATIC)
	else()
		set(gameLibType MODULE)
	endif()

	file(GLOB_RECURSE gameSources CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

	foreach(exPattern IN LISTS ARG_EXCLUDE)
		list(FILTER gameSources EXCLUDE REGEX "${exPattern}")
	endforeach()

	add_library(${TARGET_NAME} ${gameLibType} ${gameSources})
	set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Source/Games")

	target_include_directories(${TARGET_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(${TARGET_NAME}
		PRIVATE
		Engine
		RuntimeAPI
		GameFramework
		${ARG_KITS}
		sw_global_options
	)

	if(sw_flag_libraries)
		target_link_libraries(${TARGET_NAME} PRIVATE ${sw_flag_libraries})
	endif()

	target_compile_definitions(${TARGET_NAME}
		PRIVATE
		"SW_LOG_TAG=\"Game\""
		SW_GAME_INTERNAL
	)

	if(gameLibType STREQUAL "MODULE")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_MODULE_EXPORTS)
		sw_setModuleBinOutput(${TARGET_NAME})
	endif()

	sw_configurePch(${TARGET_NAME} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

	if(COMMAND sw_setUnityBuild)
		sw_setUnityBuild(${TARGET_NAME} BATCH_SIZE 8)
	endif()

	if(NOT SW_SHIPPING_BUILD AND WIN32)
		set(delayDlls GameFramework.dll)

		foreach(kit IN LISTS ARG_KITS)
			list(APPEND delayDlls "${kit}.dll")
		endforeach()

		sw_addDelayloadHook(${TARGET_NAME} DLLS ${delayDlls})
	endif()

	if(ARG_HEADERS)
		sw_addReflectionStep(${TARGET_NAME}
			HEADERS ${ARG_HEADERS}
			INCLUDES "${CMAKE_SOURCE_DIR}/Source"
		)
	else()
		sw_addReflectionStep(${TARGET_NAME}
			INCLUDES "${CMAKE_SOURCE_DIR}/Source"
		)
	endif()
endfunction()

# 테스트 실행 파일 타겟을 정의하고 공통 PCH, 로그 태그, CTest 등록을 수행합니다.
function(sw_addTestExecutable TARGET_NAME)
	cmake_parse_arguments(ARG "RUN_SERIAL" "TIMEOUT" "SOURCES;LIBS;LABELS;DEFINITIONS" ${ARGN})

	if(NOT ARG_SOURCES)
		file(GLOB_RECURSE ARG_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
	endif()

	add_executable(${TARGET_NAME} ${ARG_SOURCES})
	target_sources(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/Test/TestFramework/main.cpp")
	set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Test")

	# 배포 빌드의 Bin 은 App.exe 와 Packs/ 만 담아야 한다. 테스트는 계속 빌드하되 옆 디렉터리로
	# 뺀다 — CI 가 Shipping 을 CoreTest 로 스모크할 수 있으면서 배포 산출물은 깨끗하다.
	set(swTestOutputDir "${sw_output_directory}/Bin")
	if(SW_SHIPPING_BUILD)
		set(swTestOutputDir "${sw_output_directory}/TestBin")
		set_target_properties(${TARGET_NAME} PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${swTestOutputDir}"
			RUNTIME_OUTPUT_DIRECTORY_DEBUG "${swTestOutputDir}"
			RUNTIME_OUTPUT_DIRECTORY_RELEASE "${swTestOutputDir}"
		)
	endif()

	target_include_directories(${TARGET_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(${TARGET_NAME}
		PRIVATE
		TestFramework
		${ARG_LIBS}
		sw_global_options
	)

	if(sw_flag_libraries)
		target_link_libraries(${TARGET_NAME} PRIVATE ${sw_flag_libraries})
	endif()

	target_compile_definitions(${TARGET_NAME}
		PRIVATE
		"SW_LOG_TAG=\"Test\""
		${ARG_DEFINITIONS}
	)
	sw_configurePch(${TARGET_NAME} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

	if(BUILD_TESTING)
		add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
		set(timeout 30)

		if(ARG_TIMEOUT)
			set(timeout ${ARG_TIMEOUT})
		endif()

		set(labels "unit")

		if(ARG_LABELS)
			set(labels "${ARG_LABELS}")
		endif()

		set_tests_properties(${TARGET_NAME} PROPERTIES
			WORKING_DIRECTORY "${swTestOutputDir}"
			LABELS "${labels}"
			TIMEOUT ${timeout}
		)

		if(ARG_RUN_SERIAL)
			set_tests_properties(${TARGET_NAME} PROPERTIES RUN_SERIAL TRUE)
		endif()
	endif()
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

	if(TARGET ${ARG_NAME})
	# Target already exists as imported library from find_package
	elseif(TARGET ${ARG_CONFIG_TARGET})
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
