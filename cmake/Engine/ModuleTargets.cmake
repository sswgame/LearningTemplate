# ==============================================================================
# @file cmake/Engine/ModuleTargets.cmake
# @brief 모듈 라이브러리 팩토리 — sw_addModuleLibrary(기본값 한 자리)와 그것을 부르는 RHI 백엔드 · 키트 · 게임 모듈, 모듈 출력 폴더 · 서드파티 DLL 모으기
# ==============================================================================

# 모듈 · 핫 리로드 타깃의 런타임 출력 — 모듈 DLL(rhi · kit · game · editor)은 `Bin/Modules/`, 모두가 링크하는 GameFramework 는 `Bin/` 이다.
# 런타임은 `ModuleImageUtil::findModuleLibraryPath` 로 같은 순서(Modules → Bin)로 찾는다. SHARED/MODULE 은 플랫폼에 따라 LIBRARY 출력(Lib/)으로
# 갈 수 있어 둘 다 정한다. `$<0:>` 는 BuildLayout 과 같다.
# vcpkg 의 applocal 이 `Bin/Modules` 의 모듈 옆에 서드파티 DLL 을 복사하지 않게 하고, 같은 일을 모듈마다 스테이지 폴더에서 해 `Bin` 으로 옮긴다
# (`StageModuleRuntimeDlls.cmake`). 서드파티 DLL 은 `Bin` 에 한 벌이다 — 모듈 로드(`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | DEFAULT_DIRS`)는 모듈 폴더 다음에
# 실행 파일 폴더를 본다. vcpkg 의 `add_library` 래퍼는 타깃을 만드는 순간 `VCPKG_APPLOCAL_DEPS` 를 읽으므로 부르는 쪽(`sw_addModuleLibrary`)이 그 순간만 끈다.
function(sw_stageModuleRuntimeDlls TARGET_NAME)
	if(NOT WIN32 OR NOT DEFINED Z_VCPKG_EXECUTABLE OR NOT DEFINED VCPKG_INSTALLED_DIR)
		return()
	endif()
	add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}"
			"-DMODULE_FILE=$<TARGET_FILE:${TARGET_NAME}>"
			"-DSTAGE_DIR=${CMAKE_BINARY_DIR}/ModuleRuntimeStage/${TARGET_NAME}"
			"-DBIN_DIR=${CMAKE_BINARY_DIR}/Bin"
			"-DVCPKG_EXECUTABLE=${Z_VCPKG_EXECUTABLE}"
			"-DINSTALLED_BIN_DIR=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}$<$<CONFIG:Debug>:/debug>/bin"
			-P "${CMAKE_SOURCE_DIR}/cmake/Engine/StageModuleRuntimeDlls.cmake"
		VERBATIM
	)
endfunction()

function(sw_setModuleBinOutput TARGET_NAME KIND)
	set(outputDir "${CMAKE_BINARY_DIR}/Bin/Modules")
	if(KIND STREQUAL "gameframework")
		set(outputDir "${CMAKE_BINARY_DIR}/Bin")
	endif()
	set_target_properties(${TARGET_NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${outputDir}$<0:>"
		LIBRARY_OUTPUT_DIRECTORY "${outputDir}$<0:>"
	)
endfunction()

# ------------------------------------------------------------------------------
# sw_addModuleLibrary — 엔진 모듈 라이브러리 하나(RHI 백엔드 · 키트 · 게임 · GameFramework · 에디터)
#
# 모듈마다 다른 것만 받고 나머지는 여기서 정한다(언리얼 ModuleRules 의 기본값 자리). 꺼진 모듈 건너뛰기(`sw_skipInactiveModule`)는
# 부르는 쪽이 먼저 한다 — 함수는 부른 쪽을 return 시킬 수 없다.
#
#   KIND        rhi | kit | game | gameframework | editor | editorextension   동적 모듈 레지스트리 종류(`sw_registerDynamicModule`)
#   DEV_TYPE    SHARED | MODULE                              개발 빌드의 라이브러리 종류. 배포(SW_SHIPPING_BUILD)는 늘 STATIC 이다
#   EXPORTS     GF | MODULE | MODULE_SHARED | EDITOR          내보내기 매크로(`sw_configureDllExports`)
#   LOG_TAG     로그 태그(SW_LOG_TAG)
#   FOLDER      IDE 폴더
#   SOURCES     소스
#   LINK_PUBLIC · LINK_PRIVATE   링크(PRIVATE 끝에 sw_global_options 가 붙는다)
#   DEFINITIONS 더 붙일 PRIVATE 정의
#   UNITY_BATCH 유니티 묶음 크기(주면 `sw_setUnityBuild`)
#   REFLECTION_HEADERS  리플렉션 입력 헤더. 비우면 폴더를 재귀로 훑는다(`sw_addReflectionStep` 자동 훑기)
#   REFLECTION_EXCLUDE  자동 훑기에서 뺄 경로 정규식(하위 폴더의 다른 모듈 — 키트 · 게임의 `/Editor/`)
#   NO_REFLECTION       리플렉션 단계를 두지 않는다(RHI 백엔드)
#   NO_REGISTRY         동적 모듈 레지스트리에 넣지 않는다 — App 이 `Bin/Modules` 에서 올리지 않는 게임 팩(`sw_addGameModule`, 활성 게임이 아닌 것)
#   MODULE_NAME         매니페스트 · 산출물 · ABI 스탬프의 모듈 이름(기본 타깃 이름) — 게임은 타깃 `SWGame_<게임>`, 모듈 `SWGame` 이다
# ------------------------------------------------------------------------------
function(sw_addModuleLibrary TARGET_NAME)
	cmake_parse_arguments(ARG "NO_REFLECTION;NO_REGISTRY" "KIND;DEV_TYPE;EXPORTS;LOG_TAG;FOLDER;UNITY_BATCH;REFLECTION_EXCLUDE;MODULE_NAME"
		"SOURCES;LINK_PUBLIC;LINK_PRIVATE;DEFINITIONS;REFLECTION_HEADERS" ${ARGN})

	if(NOT ARG_DEV_TYPE MATCHES "^(SHARED|MODULE)$")
		message(FATAL_ERROR "sw_addModuleLibrary(${TARGET_NAME}): DEV_TYPE 는 SHARED | MODULE 이다 (받은 값: ${ARG_DEV_TYPE})")
	endif()
	if(SW_SHIPPING_BUILD)
		set(libType STATIC)
	else()
		set(libType ${ARG_DEV_TYPE})
	endif()
	if(NOT ARG_MODULE_NAME)
		set(ARG_MODULE_NAME ${TARGET_NAME})
	endif()
	# 게임 팩끼리 PCH 하나(`sw_game_pch`)를 나눠 쓴다 — 명령줄 정의가 같아야 하므로 Dev · clang-cl · PCH 켬일 때만(키트의 `sw_kit_pch` 와 같은 장치).
	set(bShareGamePch OFF)
	if(ARG_KIND STREQUAL "game" AND SW_ENABLE_PCH AND MSVC AND NOT libType STREQUAL "STATIC")
		set(bShareGamePch ON)
	endif()

	# `Bin/Modules` 에 가는 모듈은 vcpkg applocal 을 끄고(이 함수 범위만) 서드파티 DLL 을 `Bin` 으로 모은다(`sw_stageModuleRuntimeDlls`).
	set(bStageRuntimeDlls OFF)
	if(VCPKG_APPLOCAL_DEPS AND NOT libType STREQUAL "STATIC" AND NOT ARG_KIND STREQUAL "gameframework")
		set(VCPKG_APPLOCAL_DEPS OFF)
		set(bStageRuntimeDlls ON)
	endif()
	add_library(${TARGET_NAME} ${libType} ${ARG_SOURCES})
	if(bStageRuntimeDlls)
		sw_stageModuleRuntimeDlls(${TARGET_NAME})
	endif()
	set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "${ARG_FOLDER}")
	if(NOT ARG_MODULE_NAME STREQUAL TARGET_NAME)
		set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME "${ARG_MODULE_NAME}")
	endif()
	target_include_directories(${TARGET_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	if(ARG_LINK_PUBLIC)
		target_link_libraries(${TARGET_NAME} PUBLIC ${ARG_LINK_PUBLIC})
	endif()
	target_link_libraries(${TARGET_NAME} PRIVATE ${ARG_LINK_PRIVATE} sw_global_options)
	if(ARG_KIND STREQUAL "kit")
		# 키트는 명령줄 정의가 서로 같아야 PCH 하나(`sw_kit_pch`, 함수 끝의 `sw_useSharedPch`)를 나눠 쓴다 — 타깃 고유 정의는 강제 포함 헤더로 준다.
		# 자동 정의 `<타깃>_EXPORTS` 는 아무도 쓰지 않아 끈다.
		set_target_properties(${TARGET_NAME} PROPERTIES DEFINE_SYMBOL "")
		if(ARG_DEFINITIONS)
			target_compile_definitions(${TARGET_NAME} PRIVATE ${ARG_DEFINITIONS})
		endif()
		set(listTargetDefsOption LOG_TAG "${ARG_LOG_TAG}")
		if(libType STREQUAL "SHARED")
			# 키트 TU 는 `SW_KIT_EXPORTS` 만 정의한다 — `SW_GF_EXPORTS` 를 주면 include 한 Base 선언까지 dllexport 로 풀린다(C-3).
			list(APPEND listTargetDefsOption KIT_EXPORTS)
			target_compile_definitions(${TARGET_NAME} INTERFACE SW_KIT_IMPORTS)
		endif()
		sw_setTargetDefsHeader(${TARGET_NAME} ${listTargetDefsOption})
		sw_addSharedPchOwner(sw_kit_pch "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h"
			LINK GameFramework Engine sw_public_source_includes sw_global_options)
	elseif(bShareGamePch)
		# 자동 정의 `<타깃>_EXPORTS`(`SWGame_<게임>_EXPORTS`)는 게임마다 달라 PCH 를 나눌 수 없고 아무도 쓰지 않는다.
		set_target_properties(${TARGET_NAME} PROPERTIES DEFINE_SYMBOL "")
		target_compile_definitions(${TARGET_NAME} PRIVATE "SW_LOG_TAG=\"${ARG_LOG_TAG}\"" ${ARG_DEFINITIONS})
		sw_configureDllExports(${TARGET_NAME} ${libType} ${ARG_EXPORTS})
		sw_addSharedPchOwner(sw_game_pch "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h"
			LINK Engine RuntimeAPI GameFramework sw_global_options
			DEFINITIONS "SW_LOG_TAG=\"${ARG_LOG_TAG}\"" SW_MODULE_EXPORTS ${ARG_DEFINITIONS})
	else()
		target_compile_definitions(${TARGET_NAME} PRIVATE "SW_LOG_TAG=\"${ARG_LOG_TAG}\"" ${ARG_DEFINITIONS})
		sw_configurePch(${TARGET_NAME} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
		sw_configureDllExports(${TARGET_NAME} ${libType} ${ARG_EXPORTS})
	endif()

	if(NOT libType STREQUAL "STATIC")
		sw_setModuleBinOutput(${TARGET_NAME} ${ARG_KIND})
	endif()

	if(NOT ARG_NO_REGISTRY)
		sw_registerDynamicModule(${TARGET_NAME} ${ARG_KIND} MODULE_NAME ${ARG_MODULE_NAME})
	endif()

	if(ARG_UNITY_BATCH)
		sw_setUnityBuild(${TARGET_NAME} BATCH_SIZE ${ARG_UNITY_BATCH})
	endif()

	if(NOT ARG_NO_REFLECTION)
		sw_addReflectionStep(${TARGET_NAME}
			HEADERS ${ARG_REFLECTION_HEADERS}
			INCLUDES "${CMAKE_SOURCE_DIR}/Source"
			EXCLUDE_REGEX "${ARG_REFLECTION_EXCLUDE}"
		)
	endif()

	if(ARG_KIND STREQUAL "kit")
		sw_useSharedPch(${TARGET_NAME} sw_kit_pch)   # 소스(리플렉션 산출물 포함)가 다 붙은 뒤
	elseif(bShareGamePch)
		sw_useSharedPch(${TARGET_NAME} sw_game_pch)
	endif()
endfunction()

# RHI 그래픽스 백엔드 MODULE — 표(CookContract.json rhi_backends)의 이름 하나를 받는다. Dev 만 짓는다(배포는 Source/Engine/CMakeLists.txt 가
# 백엔드 하나를 Engine 에 넣는다).
function(sw_addRhiBackendModule BACKEND_NAME)
	set(moduleName ${SW_RHI_BACKEND_${BACKEND_NAME}_MODULE})
	sw_getRhiBackendSources(${BACKEND_NAME} listDeviceSource)
	sw_skipInactiveModule(${moduleName} swSkip)
	if(swSkip)
		# 장치 소스는 모듈 폴더 밖(Graphics/RHI/<폴더>)에 있다 — 그것도 이 구성이 짓지 않는다.
		sw_declareUnbuiltSources(${listDeviceSource})
		return()
	endif()
	sw_addModuleLibrary(${moduleName}
		KIND rhi
		DEV_TYPE MODULE
		EXPORTS MODULE
		LOG_TAG "RHI"
		FOLDER "Source/Engine/Graphics/RHI/Modules"
		SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ModuleEntry.cpp" ${listDeviceSource}
		LINK_PRIVATE Engine ${SW_RHI_BACKEND_${BACKEND_NAME}_GRAPHICS_LIBS} sw_third_party_includes
		DEFINITIONS SW_ENGINE_INTERNAL
		NO_REFLECTION
	)
endfunction()

# GameFramework 장르 키트 — 폴더의 소스 전부. 리플렉션 헤더는 폴더를 재귀로 훑는다(키트 루트의 *.h 만 모으면 하위 폴더의 REFLECT() 가 조용히 빠진다).
function(sw_addGameFrameworkKit KIT_NAME)
	# 꺼진 키트(프로젝트가 껐거나 이 플랫폼 · 구성에 없는 것)는 짓지 않는다 — 매니페스트(`<키트>.module.json`)가 정한다.
	sw_skipInactiveModule(${KIT_NAME} swSkip)
	if(swSkip)
		return()
	endif()
	file(GLOB_RECURSE listKitSource CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
	# 키트의 에디터 확장은 PlugIn/Editor/<이름>/ 의 별도 모듈이다(GF_EditorPlugIn_<이름>) — 키트 DLL(배포본 포함)에 섞지 않는다. 게임의 Editor/ 도 같다.
	list(FILTER listKitSource EXCLUDE REGEX "/Editor/")
	sw_addModuleLibrary(${KIT_NAME}
		KIND kit
		DEV_TYPE SHARED
		EXPORTS GF
		LOG_TAG "${KIT_NAME}"
		FOLDER "Source/GameFramework/Kits"
		SOURCES ${listKitSource}
		LINK_PUBLIC GameFramework Engine sw_public_source_includes
		REFLECTION_EXCLUDE "/Editor/"
	)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Editor/CMakeLists.txt")
		add_subdirectory(Editor)
	endif()
endfunction()

# 서버 · 클라이언트 키트(GF_Server_<X> · GF_Client_<X>)가 같은 기능의 공유 키트(GF_<X>)를 링크합니다.
# 일반 import 다 — 모듈 호스트가 공유 키트의 섀도 복사본에 세대 이름을 주고 이 키트 복사본의 import 이름을 그 이름으로 바꾼다
# (`ModuleShadowCopy::writeShadowCopy`). 그래서 의존은 매니페스트에 있어야 한다(없으면 로드가 거절된다).
function(sw_linkSharedKit KIT_NAME SHARED_KIT_NAME)
	# 링크는 매니페스트 의존이어야 한다 — 키트 폴더를 의존 순서로 들어가므로(Kits/CMakeLists.txt) 그래야 공유 키트 타깃이 먼저 선다.
	get_property(listDependency GLOBAL PROPERTY SW_MODULE_${KIT_NAME}_DEPENDENCIES)
	if(NOT SHARED_KIT_NAME IN_LIST listDependency)
		message(FATAL_ERROR "sw_linkSharedKit: ${KIT_NAME}.module.json must list ${SHARED_KIT_NAME} in _listDependency")
	endif()
	if(NOT TARGET ${KIT_NAME} OR NOT TARGET ${SHARED_KIT_NAME})
		message(FATAL_ERROR "sw_linkSharedKit: target '${KIT_NAME}' or '${SHARED_KIT_NAME}' does not exist")
	endif()
	target_link_libraries(${KIT_NAME} PUBLIC ${SHARED_KIT_NAME})
endfunction()

# ------------------------------------------------------------------------------
# sw_addGameModule — 게임 팩 모듈 하나. 게임 폴더의 CMakeLists 한 줄(`sw_addGameModule(SWGame)`), 인자는 모듈 이름(엔진과의 계약 `SWGame`)이다.
#
# 타깃은 `SWGame_<게임>` 이고 산출물 · 매니페스트 · ABI 스탬프는 모듈 이름 그대로(`SWGame`)다 — 핫 리로드 · 모듈 호스트는 모듈 이름으로 찾는다.
# 활성 게임(`SW_ACTIVE_GAME`): 동적 모듈 레지스트리에 들어가고(App 이 먼저 짓고 Shipping 이 정적으로 링크한다) `Bin/Modules/SWGame` 이다.
#   타깃 이름은 전역 속성 `SW_ACTIVE_GAME_TARGET` 에 둔다 — 링크 · 테스트 의존은 이 속성으로 묻는다(이름을 적지 않는다).
# 함께 짓는 게임(`SW_GAMES`, Dev 전용): 레지스트리 밖(`SW_GAME_PACK_TARGETS`)이고 `Bin/Games/<게임>/SWGame`, 링크 PDB 는 `Bin/Symbols/Games/<게임>/` 이다 —
#   게임마다 같은 파일 이름이라 폴더로 가른다. 그 게임의 매니페스트를 DLL 옆에 둔다(`-game=` 으로 고르면 모듈 카탈로그가 읽는다).
# 링크하는 키트는 그 게임 매니페스트의 의존 가운데 Kit 인 것이다(CMake 에 다시 적지 않는다) — 활성 게임은 모듈 표, 함께 짓는 게임은
#   `sw_resolveModuleManifests` 가 읽어 둔 `SW_GAME_PACK_<게임>_DEPENDENCIES`. 게임의 에디터 확장(Editor/)은 활성 게임만 짓는다.
# 엔진 · GameFramework · 키트는 모든 게임이 같은 타깃을 링크하므로 한 번만 컴파일되고, 게임끼리 PCH 하나(`sw_game_pch`)를 나눠 쓴다.
# ------------------------------------------------------------------------------
function(sw_addGameModule MODULE_NAME)
	get_filename_component(gameName "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
	set(targetName "${MODULE_NAME}_${gameName}")
	set(bActiveGame OFF)
	if(gameName STREQUAL SW_ACTIVE_GAME)
		set(bActiveGame ON)
	endif()

	if(bActiveGame)
		sw_getModuleDependenciesOfKind(${MODULE_NAME} Kit listKit)
		set(listRegistryOption "")
	else()
		get_property(listDependency GLOBAL PROPERTY SW_GAME_PACK_${gameName}_DEPENDENCIES)
		get_property(listModuleName GLOBAL PROPERTY SW_MODULE_NAMES)
		set(listKit "")
		foreach(dependency IN LISTS listDependency)
			if(NOT dependency IN_LIST listModuleName)
				message(FATAL_ERROR "[Games] ${gameName}: ${MODULE_NAME}.module.json depends on '${dependency}', which has no manifest (missing module)")
			endif()
			get_property(dependencyKind GLOBAL PROPERTY SW_MODULE_${dependency}_KIND)
			get_property(dependencyActive GLOBAL PROPERTY SW_MODULE_${dependency}_ACTIVE)
			if(NOT dependencyActive)
				get_property(dependencyReason GLOBAL PROPERTY SW_MODULE_${dependency}_REASON)
				message(FATAL_ERROR "[Games] ${gameName}: ${MODULE_NAME}.module.json depends on '${dependency}', which is ${dependencyReason}")
			endif()
			if(dependencyKind STREQUAL "Kit")
				list(APPEND listKit ${dependency})
			endif()
		endforeach()
		set(listRegistryOption NO_REGISTRY)
	endif()

	file(GLOB_RECURSE listGameSource CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
	# 게임의 에디터 확장(Editor/)은 따로 짓는 모듈이다(SWGameEditor).
	list(FILTER listGameSource EXCLUDE REGEX "/Editor/")
	sw_addModuleLibrary(${targetName}
		KIND game
		DEV_TYPE MODULE
		EXPORTS MODULE
		LOG_TAG "Game"
		FOLDER "Source/Games"
		SOURCES ${listGameSource}
		LINK_PRIVATE Engine RuntimeAPI GameFramework ${listKit}
		UNITY_BATCH 8
		REFLECTION_EXCLUDE "/Editor/"
		MODULE_NAME ${MODULE_NAME}
		${listRegistryOption}
	)

	if(bActiveGame)
		set_property(GLOBAL PROPERTY SW_ACTIVE_GAME_TARGET ${targetName})
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Editor/CMakeLists.txt")
			add_subdirectory(Editor)
		endif()
		return()
	endif()

	set(outputDir "${CMAKE_BINARY_DIR}/Bin/${SW_DIR_BIN_GAMES}/${gameName}")
	set(symbolDir "${CMAKE_BINARY_DIR}/Bin/Symbols/${SW_DIR_BIN_GAMES}/${gameName}")
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_NAME}.module.json" "${outputDir}/${MODULE_NAME}.module.json" COPYONLY)
	set_target_properties(${targetName} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${outputDir}$<0:>"
		LIBRARY_OUTPUT_DIRECTORY "${outputDir}$<0:>"
		PDB_OUTPUT_DIRECTORY "${symbolDir}"
		PDB_OUTPUT_DIRECTORY_DEBUG "${symbolDir}"
		PDB_OUTPUT_DIRECTORY_RELEASE "${symbolDir}"
	)
	sw_addModuleEngineStamp(${targetName} MODULE_NAME ${MODULE_NAME})
	set_property(GLOBAL APPEND PROPERTY SW_GAME_PACK_TARGETS ${targetName})
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Editor")
		sw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}/Editor")
	endif()
endfunction()

# ------------------------------------------------------------------------------
# 에디터 확장 모듈(GF_EditorPlugIn_<이름> · SWGameEditor) — Dev 전용 SHARED. EditorModule 을 링크하고(언리얼 <X>Editor 모듈이 UnrealEd 를 링크하는 것과 같다),
# 매니페스트의 의존 가운데 키트 · GameFramework 를 링크한다(목록을 CMake 에 다시 적지 않는다 — 매니페스트가 원본). ImGui 결속 소스를 만들어 넣는다
# (손으로 빠뜨릴 수 없게 — `cmake/Engine/EditorExtensionUIBinder.cpp.in`). 다시 로드되는 DLL(EditorModule · 키트)도 일반 import 다(모듈 호스트가 이름을 바꾼다).
# ------------------------------------------------------------------------------
function(sw_addEditorExtension TARGET_NAME)
	file(GLOB_RECURSE listExtensionSource CONFIGURE_DEPENDS "*.cpp" "*.h")
	if(SW_SHIPPING_BUILD)
		sw_declareUnbuiltSources(${listExtensionSource})
		return()
	endif()
	sw_skipInactiveModule(${TARGET_NAME} swSkip)
	if(swSkip)
		return()
	endif()

	set(SW_EDITOR_EXTENSION_NAME ${TARGET_NAME})
	set(binderSource "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}UIBinder.cpp")
	configure_file("${CMAKE_SOURCE_DIR}/cmake/Engine/EditorExtensionUIBinder.cpp.in" "${binderSource}" @ONLY)

	set(listLinkedModule "")
	get_property(listDependency GLOBAL PROPERTY SW_MODULE_${TARGET_NAME}_DEPENDENCIES)
	foreach(dependency IN LISTS listDependency)
		get_property(dependencyKind GLOBAL PROPERTY SW_MODULE_${dependency}_KIND)
		if(dependencyKind STREQUAL "Kit" OR dependencyKind STREQUAL "GameFramework")
			list(APPEND listLinkedModule ${dependency})
		endif()
	endforeach()

	sw_addModuleLibrary(${TARGET_NAME}
		KIND editorextension
		DEV_TYPE SHARED
		EXPORTS MODULE_SHARED
		LOG_TAG "${TARGET_NAME}"
		FOLDER "Source/EditorExtensions"
		SOURCES ${listExtensionSource} "${binderSource}"
		LINK_PRIVATE EditorModule Engine RuntimeAPI ${listLinkedModule} imgui implot imgui_nodeeditor
	)
	add_dependencies(EditorAll ${TARGET_NAME})
endfunction()
