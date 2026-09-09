# ==============================================================================
# @file cmake/Engine/TargetRules.cmake
# @brief 타겟을 어떻게 만드나 — DLL export, RHI 백엔드·장르 키트·게임 모듈·테스트 팩토리, delay-load
# ==============================================================================

# ------------------------------------------------------------------------------
# DLL export 매크로 — Engine / GameFramework가 공유
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

	# 헤더 목록을 넘기지 않는다. sw_addReflectionStep 이 REFLECT/ENUM 매크로를 가진 헤더를
	# **재귀로** 찾아낸다. 예전에는 여기서 GLOB 으로 키트 루트의 *.h 만 모았는데, 소스는
	# GLOB_RECURSE 였다 — 키트 안에 하위 폴더를 만들면 .cpp 는 컴파일되고 그 안의 REFLECT()
	# 타입만 조용히 등록되지 않았다.
	sw_addReflectionStep(${KIT_NAME}
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

	# App 과 같은 이유로 테스트 실행 파일도 리플렉션 정적 라이브러리를 통째로 링크한다.
	# *.gen.cpp 의 등록기는 파일 스코프 static 이고 외부에서 참조되는 심볼이 없어서, Shipping
	# 처럼 Engine 이 정적 라이브러리인 빌드에서는 링커가 그 오브젝트를 통째로 버린다. 그러면
	# 타입은 (StaticType() 정의가 같은 파일에 있어) 살아남는데 **열거형만 조용히 사라진다** —
	# KeyCodes::fromName 이 전부 Unknown 을 내서 InputMap XML 의 바인딩이 하나도 안 붙고,
	# SaveGame 의 리플렉션 왕복도 깨진다. Source/App/CMakeLists.txt 의 같은 블록 참고.
	if(SW_SHIPPING_BUILD AND WIN32)
		foreach(reflLib IN LISTS ARG_LIBS)
			if(TARGET ${reflLib})
				get_target_property(reflLibType ${reflLib} TYPE)
				if(reflLibType STREQUAL "STATIC_LIBRARY")
					target_link_options(${TARGET_NAME} PRIVATE "LINKER:/WHOLEARCHIVE:$<TARGET_FILE:${reflLib}>")
				endif()
			endif()
		endforeach()
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

		# ASan 은 실행을 한 자릿수 배로 늦춘다. 평시 기준 타임아웃을 그대로 쓰면 **테스트가 전부
		# 타임아웃으로 실패한다** — 실제로 Windows ASan 에서 5개 테스트가 모두 그렇게 떨어졌고,
		# 결함처럼 보였다. 넉넉히 곱해 둔다(느린 것은 여기서 잡을 문제가 아니다).
		if(SW_ENABLE_SANITIZER)
			math(EXPR timeout "${timeout} * 10")
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

		# ASan 의 ODR 검사를 완화한다. 이 엔진은 플러그인 DLL 이 여럿이고(RHI_*, SWGame, GF_*,
		# EditorModule) 그 DLL 들이 같은 SDK·CRT 헤더를 포함한다. 그러면 헤더가 박는 전역이
		# DLL 마다 생기고 ASan 은 그것을 ODR 위반으로 본다 — 실제로 나온 것이
		# `d3d11.h` 의 `D3D11_DEFAULT` 와 CRT 내부 `_Avx2WmemEnabledWeakValue` 다. 우리 코드가
		# 아니라 헤더 정의이고, 핫리로드로 DLL 사본이 오갈 때마다 다시 난다 — 영구 오탐이다.
		#
		# 끄지(0) 않고 1 로 둔다: **크기가 다른** 중복만 보고하므로 진짜 ODR 버그(같은 이름, 다른
		# 정의)는 계속 잡히고, 위의 동일 크기 중복만 조용해진다.
		if(SW_ENABLE_SANITIZER)
			set_tests_properties(${TARGET_NAME} PROPERTIES
				ENVIRONMENT "ASAN_OPTIONS=detect_odr_violation=1"
			)
		endif()
	endif()
endfunction()

# ------------------------------------------------------------------------------
# Windows delay-load + 훅 소스 바인딩
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
		set(swHookSrc "${CMAKE_SOURCE_DIR}/Source/Engine/Module/DelayLoadNotifyHook.cpp")

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
