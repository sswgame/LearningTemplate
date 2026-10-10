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
#   KIND        rhi | kit | game | gameframework | editor    동적 모듈 레지스트리 종류(`sw_registerDynamicModule`)
#   DEV_TYPE    SHARED | MODULE                              개발 빌드의 라이브러리 종류. 배포(SW_SHIPPING_BUILD)는 늘 STATIC 이다
#   EXPORTS     GF | MODULE                                  내보내기 매크로(`sw_configureDllExports`)
#   LOG_TAG     로그 태그(SW_LOG_TAG)
#   FOLDER      IDE 폴더
#   SOURCES     소스
#   LINK_PUBLIC · LINK_PRIVATE   링크(PRIVATE 끝에 sw_global_options 가 붙는다)
#   DEFINITIONS 더 붙일 PRIVATE 정의
#   DELAYLOAD   개발 빌드에서 지연 로드할 DLL(Windows 만 — `sw_addDelayloadHook`)
#   UNITY_BATCH 유니티 묶음 크기(주면 `sw_setUnityBuild`)
#   REFLECTION_HEADERS  리플렉션 입력 헤더. 비우면 폴더를 재귀로 훑는다(`sw_addReflectionStep` 자동 훑기)
#   NO_REFLECTION       리플렉션 단계를 두지 않는다(RHI 백엔드)
# ------------------------------------------------------------------------------
function(sw_addModuleLibrary TARGET_NAME)
	cmake_parse_arguments(ARG "NO_REFLECTION" "KIND;DEV_TYPE;EXPORTS;LOG_TAG;FOLDER;UNITY_BATCH"
		"SOURCES;LINK_PUBLIC;LINK_PRIVATE;DEFINITIONS;DELAYLOAD;REFLECTION_HEADERS" ${ARGN})

	if(NOT ARG_DEV_TYPE MATCHES "^(SHARED|MODULE)$")
		message(FATAL_ERROR "sw_addModuleLibrary(${TARGET_NAME}): DEV_TYPE 는 SHARED | MODULE 이다 (받은 값: ${ARG_DEV_TYPE})")
	endif()
	if(SW_SHIPPING_BUILD)
		set(libType STATIC)
	else()
		set(libType ${ARG_DEV_TYPE})
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
	target_include_directories(${TARGET_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	if(ARG_LINK_PUBLIC)
		target_link_libraries(${TARGET_NAME} PUBLIC ${ARG_LINK_PUBLIC})
	endif()
	target_link_libraries(${TARGET_NAME} PRIVATE ${ARG_LINK_PRIVATE} sw_global_options)
	target_compile_definitions(${TARGET_NAME} PRIVATE "SW_LOG_TAG=\"${ARG_LOG_TAG}\"" ${ARG_DEFINITIONS})
	sw_configurePch(${TARGET_NAME} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
	sw_configureDllExports(${TARGET_NAME} ${libType} ${ARG_EXPORTS})

	if(NOT libType STREQUAL "STATIC")
		sw_setModuleBinOutput(${TARGET_NAME} ${ARG_KIND})
		if(ARG_DELAYLOAD)
			sw_addDelayloadHook(${TARGET_NAME} DLLS ${ARG_DELAYLOAD})
		endif()
	endif()

	sw_registerDynamicModule(${TARGET_NAME} ${ARG_KIND})

	if(ARG_UNITY_BATCH)
		sw_setUnityBuild(${TARGET_NAME} BATCH_SIZE ${ARG_UNITY_BATCH})
	endif()

	if(NOT ARG_NO_REFLECTION)
		sw_addReflectionStep(${TARGET_NAME}
			HEADERS ${ARG_REFLECTION_HEADERS}
			INCLUDES "${CMAKE_SOURCE_DIR}/Source"
		)
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
	sw_addModuleLibrary(${KIT_NAME}
		KIND kit
		DEV_TYPE SHARED
		EXPORTS GF
		LOG_TAG "${KIT_NAME}"
		FOLDER "Source/GameFramework/Kits"
		SOURCES ${listKitSource}
		LINK_PUBLIC GameFramework Engine sw_public_source_includes
		DELAYLOAD GameFramework.dll
	)
endfunction()

# 서버 · 클라이언트 키트(GF_Server_<X> · GF_Client_<X>)가 같은 기능의 공유 키트(GF_<X>)를 링크합니다.
# 키트는 다시 올릴 수 있는 모듈이라(Dev) 공유 키트 DLL 도 GameFramework.dll 처럼 **지연 로드**해야 한다 — 바로 링크하면 OS 로더가
# Bin 의 원본 DLL 을 따로 올려, 모듈 호스트가 올린 그림자 사본과 이미지가 둘이 된다(LiveReloadManager 가 "bound to a stale ... image" 로 멈춘다).
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
	get_target_property(kitType ${KIT_NAME} TYPE)
	if(WIN32 AND kitType STREQUAL "SHARED_LIBRARY")
		sw_addDelayloadHook(${KIT_NAME} DLLS ${SHARED_KIT_NAME}.dll)
	endif()
endfunction()

# 게임 팩 모듈(SWGame) — 링크하는 키트는 게임 매니페스트(`SWGame.module.json`)의 의존 가운데 Kit 인 것이다(CMake 에 다시 적지 않는다).
function(sw_addGameModule TARGET_NAME)
	sw_getModuleDependenciesOfKind(${TARGET_NAME} Kit listKit)
	set(listDelayLoad GameFramework.dll)
	foreach(kit IN LISTS listKit)
		list(APPEND listDelayLoad "${kit}.dll")
	endforeach()

	file(GLOB_RECURSE listGameSource CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
	sw_addModuleLibrary(${TARGET_NAME}
		KIND game
		DEV_TYPE MODULE
		EXPORTS MODULE
		LOG_TAG "Game"
		FOLDER "Source/Games"
		SOURCES ${listGameSource}
		LINK_PRIVATE Engine RuntimeAPI GameFramework ${listKit}
		DELAYLOAD ${listDelayLoad}
		UNITY_BATCH 8
	)
endfunction()
