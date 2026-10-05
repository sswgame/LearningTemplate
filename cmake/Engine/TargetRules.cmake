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

# ------------------------------------------------------------------------------
# 정적 라이브러리 통째 링크 — 리플렉션 등록기 보존
# ------------------------------------------------------------------------------
# 생성된 *.gen.cpp 의 등록기는 파일 스코프 static 객체(생성자가 전역 링크드 리스트에 자신을
# 매단다)라 외부에서 참조되는 심볼이 없다. Dev 는 Engine 이 DLL 이라 전부 로드되지만, Shipping 은
# 정적 라이브러리라 링커가 "아무도 참조 안 하는 오브젝트 파일" 을 통째로 버린다. 타입은
# (StaticType() 정의가 같은 파일에 있어) 살아남고 **열거형만 조용히 사라진다** — RHITypes.gen.cpp
# 가 빠지면 RHIBackend · RHIFormat 이 등록되지 않아 EngineConfig 역직렬화가 기본값으로 떨어지고,
# 렌더패스 포맷이 전부 미상이 되며, KeyCodeUtil::fromName 이 Unknown 만 내서 InputMap 바인딩이
# 하나도 안 붙고, SaveGame 의 리플렉션 왕복이 깨진다. 그래서 리플렉션을 담은 정적 라이브러리는
# 통째로 링크한다.
#
# 플래그는 링커마다 다르다. 주의: 한쪽(`/WHOLEARCHIVE`)만 `WIN32` 가드 안에 두면 리눅스 Shipping 은 등록된 열거형이
# 거의 없는 채로 테스트를 돈다 — 그리고 `CI-Debug`(Engine 이 SHARED)만 돌려서는 재현되지 않는다.
#
#   | 링커                  | 플래그                                     |
#   | --------------------- | ------------------------------------------ |
#   | link.exe · lld-link   | `/WHOLEARCHIVE:<lib>`                      |
#   | GNU ld · lld · gold   | `--whole-archive <lib> --no-whole-archive` |
#
# 링크 옵션은 오브젝트보다 앞에 놓이지만 문제없다 — 통째로 올라온 멤버가 필요로 하는 심볼은 뒤에
# 오는 라이브러리가 채우고, 같은 아카이브가 뒤에 한 번 더 나와도 이미 올라온 멤버는 다시 올리지
# 않는다. STATIC 이 아닌 타겟(SHARED/MODULE/OBJECT)이나 없는 타겟은 조용히 건너뛴다 — 호출부가
# 구성마다 다른 목록을 그대로 넘겨도 되게.
function(sw_linkWholeArchive TARGET_NAME)
	foreach(reflLib IN LISTS ARGN)
		if(NOT TARGET ${reflLib})
			continue()
		endif()

		get_target_property(reflLibType ${reflLib} TYPE)

		if(NOT reflLibType STREQUAL "STATIC_LIBRARY")
			continue()
		endif()

		if(MSVC)
			target_link_options(${TARGET_NAME} PRIVATE "LINKER:/WHOLEARCHIVE:$<TARGET_FILE:${reflLib}>")
		else()
			target_link_options(${TARGET_NAME} PRIVATE
				"LINKER:--whole-archive,$<TARGET_FILE:${reflLib}>,--no-whole-archive")
		endif()
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# 동적 모듈 레지스트리 — **만드는 자리가 등록하고, 쓰는 자리는 묻는다**
#
# 등록을 호출부에 맡기거나 소비자가 이름을 리터럴로 들면 새 모듈이 조용히 빠진다. 그래서 등록은
# **타겟을 만드는 함수 안에서만** 한다. 종류(`KIND`)를 같이 받아 두면 소비자가 필요한 것만 고를 수
# 있다 — App 은 전부, EngineTest 는 `rhi` 만.
#
# 이 저장소의 `Scripts/lint/gate/` 와 같은 규칙이다: **목록이 아니라 자리가 규칙이다.**
#
#   KIND: rhi | kit | game | gameframework | editor
# ------------------------------------------------------------------------------
function(sw_registerDynamicModule TARGET_NAME KIND)
	# 모든 동적 모듈은 매니페스트를 갖는다(없으면 여기서 구성이 선다) — App 이 그것으로 적재 순서를 정한다.
	sw_isModuleActive(${TARGET_NAME} swModuleActive)
	if(NOT swModuleActive)
		message(FATAL_ERROR "[Module] ${TARGET_NAME} is registered but its manifest says it is off — the creating function must skip it (sw_skipInactiveModule)")
	endif()
	set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES ${TARGET_NAME})
	set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES_${KIND} ${TARGET_NAME})
	sw_addModuleEngineStamp(${TARGET_NAME})
endfunction()

# ------------------------------------------------------------------------------
# 엔진 ABI 도장 — Core · Engine 헤더 내용의 지문 (핫 리로드가 올리기 전에 대조한다)
#
# 핫 리로드는 모듈만 갈아 끼우고 Engine 은 그대로다. Engine 헤더를 고친 빌드에서 `Engine.dll` 은 실행 중이라
# 다시 링크되지 못해도(잠김) 모듈은 **새 헤더로** 써질 수 있고, 그 모듈을 올리면 구조체 배치 · vtable 이 어긋나
# 조용히 망가진다. 그래서 Engine 과 모듈이 같은 지문을 박고 `LiveReloadManager` 가 섀도 복사본을 올리기 **전에**
# 파일 바이트에서 찾아 대조한다(`ModuleImagePatch::findEngineAbiStamp`). 헤더 내용이 같으면 스크립트가 파일을
# 다시 쓰지 않으므로(Ninja restat) 뒤따르는 재빌드가 없다.
# ------------------------------------------------------------------------------
set(SW_ENGINE_ABI_STAMP_HEADER "${CMAKE_BINARY_DIR}/generated/engineabi/EngineAbiStamp.gen.h")

function(sw_defineEngineAbiStamp)
	if(TARGET SwEngineAbiStamp)
		return()
	endif()
	file(GLOB_RECURSE swEngineAbiHeaders CONFIGURE_DEPENDS
		"${CMAKE_SOURCE_DIR}/Source/Core/*.h"
		"${CMAKE_SOURCE_DIR}/Source/Core/*.hpp"
		"${CMAKE_SOURCE_DIR}/Source/Core/*.inl"
		"${CMAKE_SOURCE_DIR}/Source/Engine/*.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/*.hpp"
		"${CMAKE_SOURCE_DIR}/Source/Engine/*.inl"
		"${CMAKE_SOURCE_DIR}/Source/Core/*.xxx"
		"${CMAKE_SOURCE_DIR}/Source/Engine/*.xxx"
		"${CMAKE_SOURCE_DIR}/Source/RuntimeAPI/*.h"
		"${CMAKE_SOURCE_DIR}/Source/RuntimeAPI/*.xxx"
		# GameFramework 는 Kits 까지 잡히지만 상관없다 — 스크립트가 Kits 를 빼고 해시하고, 결과가 같으면 파일을 다시 쓰지 않는다.
		"${CMAKE_SOURCE_DIR}/Source/GameFramework/*.h"
		"${CMAKE_SOURCE_DIR}/Source/GameFramework/*.xxx"
	)
	add_custom_command(
		OUTPUT "${SW_ENGINE_ABI_STAMP_HEADER}"
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Scripts/generate/GenerateEngineAbiStamp.py"
			--root "${CMAKE_SOURCE_DIR}" --out "${SW_ENGINE_ABI_STAMP_HEADER}"
		DEPENDS ${swEngineAbiHeaders} "${CMAKE_SOURCE_DIR}/Scripts/generate/GenerateEngineAbiStamp.py"
		COMMENT "Fingerprinting Core/Engine headers for the hot reload ABI stamp"
		VERBATIM
	)
	add_custom_target(SwEngineAbiStamp DEPENDS "${SW_ENGINE_ABI_STAMP_HEADER}")
	set_target_properties(SwEngineAbiStamp PROPERTIES FOLDER "CMakePredefinedTargets")
endfunction()

# 모듈이 자기가 빌드된 엔진 헤더의 지문을 박는다(Dev 의 공유 라이브러리만). 상수 하나가 두 가지 일을 한다.
#   - **도장** — 핫 리로드는 심볼이 아니라 파일 바이트에서 표식 문자열을 찾는다(모듈 코드가 돌기 전에).
#     내보내는 이유는 링커가 참조 없는 자료를 지우지 못하게 하려는 것이다.
#   - **결속 표식(리눅스)** — `LiveReloadManager::verifyModuleBindings` 는 "이 모듈이 의존을 어느 이미지에 묶었나" 를
#     묻는다. Windows 는 import 표에서 읽지만 리눅스는 그 정보를 밖에 내주지 않는다. 그래서 이름에 타겟 이름을 붙여
#     모듈마다 고유하게 두고, `dlsym( 모듈, "sw_moduleEngineAbiStamp_<의존>" )` 이 돌려주는 주소로 가린다 — dlsym 은
#     그 모듈의 검색 범위(자기 + 자기 의존)에서 찾으므로 그 주소가 곧 모듈이 실제로 묶인 의존 이미지의 것이다.
function(sw_addModuleEngineStamp TARGET_NAME)
	if(SW_SHIPPING_BUILD OR NOT TARGET ${TARGET_NAME})
		return()
	endif()

	get_target_property(swTargetType ${TARGET_NAME} TYPE)
	if(NOT swTargetType STREQUAL "SHARED_LIBRARY" AND NOT swTargetType STREQUAL "MODULE_LIBRARY")
		return()
	endif()

	sw_defineEngineAbiStamp()
	set(swStampSource "${CMAKE_BINARY_DIR}/generated/moduleidentity/${TARGET_NAME}EngineStamp.cpp")
	file(CONFIGURE OUTPUT "${swStampSource}" CONTENT
"// 생성 파일 - sw_addModuleEngineStamp (cmake/Engine/TargetRules.cmake). 고치지 마십시오.
// 이 모듈이 빌드된 Core · Engine 헤더의 지문입니다. 핫 리로드가 올리기 전에 돌고 있는 엔진의 것과 대조하고,
// 리눅스에서는 이 상수의 주소로 의존 모듈이 어느 이미지에 묶였는지 가립니다.
#include \"@SW_ENGINE_ABI_STAMP_HEADER@\"

#if defined( SW_PLATFORM_WINDOWS ) // 플랫폼은 CMake 의 SW_PLATFORM_* 로 묻는다 (Core/Common/TargetMacroCheck.h)
    #define SW_MODULE_ENGINE_STAMP_EXPORT __declspec( dllexport )
#else
    #define SW_MODULE_ENGINE_STAMP_EXPORT __attribute__( ( visibility( \"default\" ) ) )
#endif

extern \"C\" SW_MODULE_ENGINE_STAMP_EXPORT const char sw_moduleEngineAbiStamp_@TARGET_NAME@[];
extern \"C\" SW_MODULE_ENGINE_STAMP_EXPORT const char sw_moduleEngineAbiStamp_@TARGET_NAME@[] = SW_ENGINE_ABI_STAMP;
" @ONLY)

	target_sources(${TARGET_NAME} PRIVATE "${swStampSource}")
	set_source_files_properties("${swStampSource}" PROPERTIES
		SKIP_PRECOMPILE_HEADERS ON
		SKIP_UNITY_BUILD_INCLUSION ON
		OBJECT_DEPENDS "${SW_ENGINE_ABI_STAMP_HEADER}"
	)
	add_dependencies(${TARGET_NAME} SwEngineAbiStamp)
endfunction()

# 등록된 동적 모듈 중 **실제로 타겟이 있는 것**을 OUT_VAR 에 담습니다.
#
# `KINDS` 를 주면 그 종류만, 생략하면 전부. 타겟이 없는 이름은 거른다 — 배포 빌드는 RHI 를
# Engine 에 정적 링크하므로 등록만 되고 타겟은 없는 상태가 정상이다.
function(sw_getDynamicModules OUT_VAR)
	cmake_parse_arguments(ARG "" "" "KINDS" ${ARGN})

	if(ARG_KINDS)
		set(listRegistered "")
		foreach(kind IN LISTS ARG_KINDS)
			get_property(listOfKind GLOBAL PROPERTY SW_DYNAMIC_MODULES_${kind})
			list(APPEND listRegistered ${listOfKind})
		endforeach()
	else()
		get_property(listRegistered GLOBAL PROPERTY SW_DYNAMIC_MODULES)
	endif()

	set(listModule "")
	foreach(mod IN LISTS listRegistered)
		if(TARGET ${mod})
			list(APPEND listModule ${mod})
		endif()
	endforeach()

	if(listModule)
		list(REMOVE_DUPLICATES listModule)
	endif()

	set(${OUT_VAR} "${listModule}" PARENT_SCOPE)
endfunction()

# 등록을 **잊을 수 없게** 한다 — 구성 마지막에 한 번 대조합니다.
#
# 레지스트리는 규칙이지 강제가 아니다. `EditorModule` 은 실제로 아무 데도 등록되지 않은 채
# 오래 있었고, 아무 에러도 나지 않았다(소비하는 자리가 이름을 리터럴로 들고 있었으니까).
# 그래서 여기서 **런타임에 로드되는 타겟(MODULE)** 을 전부 훑어 레지스트리와 맞춰 본다.
# MODULE 은 정의상 "이름으로 찾아 올리는 플러그인" 이라 App 이 반드시 먼저 빌드해야 하는 것들이다.
#
# 빠진 것이 있으면 **구성이 선다.** 조용히 빠지는 것보다 낫다.
function(sw_verifyDynamicModuleRegistry)
	get_property(listRegistered GLOBAL PROPERTY SW_DYNAMIC_MODULES)

	# 루트부터 훑는다. `Source/` 에서 시작하면 안 된다 — `Source/Editor` · `Source/Engine` 등은
	# `Source/CMakeLists.txt` 가 아니라 **루트가** 직접 add_subdirectory 하므로 `Source/` 의
	# SUBDIRECTORIES 에 없다. (처음에 그렇게 짰다가 EditorModule 을 못 잡는 것을 확인했다.)
	set(listDirectory "${CMAKE_SOURCE_DIR}")
	set(listMissing "")

	while(listDirectory)
		list(POP_FRONT listDirectory currentDir)

		get_property(listSubDir DIRECTORY "${currentDir}" PROPERTY SUBDIRECTORIES)
		list(APPEND listDirectory ${listSubDir})

		get_property(listTarget DIRECTORY "${currentDir}" PROPERTY BUILDSYSTEM_TARGETS)
		foreach(targetName IN LISTS listTarget)
			get_target_property(targetType ${targetName} TYPE)
			if(NOT targetType STREQUAL "MODULE_LIBRARY")
				continue()
			endif()

			if(NOT targetName IN_LIST listRegistered)
				list(APPEND listMissing ${targetName})
			endif()
		endforeach()
	endwhile()

	if(listMissing)
		message(FATAL_ERROR
			"동적 모듈이 레지스트리에 없습니다: ${listMissing}
"
			"  타겟을 만드는 자리에서 sw_registerDynamicModule(<타겟> <종류>) 를 부르세요.
"
			"  (종류: rhi | kit | game | gameframework | editor — cmake/Engine/TargetRules.cmake)")
	endif()
endfunction()

# App의 런타임/플러그인/모듈 의존성을 구성합니다.
function(sw_configureAppDependencies TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		return()
	endif()

	# 1) 동적 모듈은 App 보다 먼저 빌드되어야 한다 — App 이 런타임에 로드하기 때문이다.
	#    이름을 적지 않는다: 레지스트리가 답한다(위 "동적 모듈 레지스트리").
	sw_getDynamicModules(listDynamicModule)
	foreach(mod IN LISTS listDynamicModule)
		add_dependencies(${TARGET_NAME} ${mod})
	endforeach()

	# 2) Shipping 은 게임을 정적으로 링크하고, App 이 서면 에셋을 쿠킹한다.
	#    (Dev 는 delay-load 라 링크하지 않는다 — 빌드 순서는 위 1) 이 이미 걸어 두었다.)
	#    쿠킹이 App 뒤인 이유: 씬 쿠킹이 App --cook-scenes 라서다. 반대로 걸면 깨끗한 트리에서 App 이
	#    없는 채로 쿠커가 돌아 죽는다(리눅스 CI). 기본 빌드(all)에 넣어 `cmake --build` 한 번이면 팩까지 선다.
	if(SW_SHIPPING_BUILD)
		if(TARGET SWGame)
			target_link_libraries(${TARGET_NAME} PRIVATE SWGame)
		endif()

		# 3) 리플렉션을 담은 정적 라이브러리(Engine · GameFramework · 킷 · 게임)는 통째로 링크한다. 생성된 *.gen.cpp 의 등록기는 파일 범위
		#    static 객체라(생성자가 전역 연결 리스트에 자신을 매단다) 밖에서 참조하는 심볼이 없고, 정적 링크에서 링커는 아무도 참조하지 않는
		#    오브젝트 파일을 버린다 — 그 타입 · 열거형은 등록되지 않고 씬의 컴포넌트는 MissingComponent 가 된다. 목록은 모듈 레지스트리이므로
		#    모든 모듈이 등록된 뒤인 여기서 읽는다. 플랫폼별 링커 플래그는 `sw_linkWholeArchive` 가 고른다.
		if(TARGET GameFramework AND NOT GameFramework IN_LIST listDynamicModule)
			message(FATAL_ERROR "[App] sw_configureAppDependencies ran before GameFramework registered itself — its reflection registrars would not be linked into ${TARGET_NAME}.")
		endif()
		set(listReflectionStaticLib Engine ${listDynamicModule})
		list(REMOVE_DUPLICATES listReflectionStaticLib)
		sw_linkWholeArchive(${TARGET_NAME} ${listReflectionStaticLib})

		if(TARGET CookAssets)
			add_dependencies(CookAssets ${TARGET_NAME})
			set_target_properties(CookAssets PROPERTIES EXCLUDE_FROM_ALL FALSE)
		endif()
	endif()
endfunction()

# RHI 그래픽스 백엔드 MODULE 타겟을 정의하고 공통 속성을 바인딩합니다.
function(sw_addRhiBackendModule BACKEND_NAME GRAPHICS_LIB)
	cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
	sw_skipInactiveModule(${BACKEND_NAME} swSkip)
	if(swSkip)
		return()
	endif()
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
	sw_registerDynamicModule(${BACKEND_NAME} rhi)
endfunction()

# GameFramework 장르 키트 라이브러리 타겟을 정의하고 빌드 모드에 맞게 구성합니다.
function(sw_addGameFrameworkKit KIT_NAME)
	# 꺼진 키트(프로젝트가 껐거나 이 플랫폼 · 구성에 없는 것)는 짓지 않는다 — 매니페스트(`<키트>.module.json`)가 정한다.
	sw_skipInactiveModule(${KIT_NAME} swSkip)
	if(swSkip)
		return()
	endif()
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

	sw_registerDynamicModule(${KIT_NAME} kit)
	set_target_properties(${KIT_NAME} PROPERTIES FOLDER "Source/GameFramework/Kits")

	# 헤더 목록을 넘기지 않는다. sw_addReflectionStep 이 REFLECT/ENUM 매크로를 가진 헤더를
	# **재귀로** 찾아낸다. 주의: 여기서 키트 루트의 *.h 만 모으면(소스는 GLOB_RECURSE) 하위 폴더의 .cpp 는
	# 컴파일되는데 그 안의 REFLECT() 타입만 조용히 등록되지 않는다.
	sw_addReflectionStep(${KIT_NAME}
		INCLUDES "${CMAKE_SOURCE_DIR}/Source"
	)
endfunction()

# 게임 팩 모듈(SWGame) 타겟을 정의하고 링크 및 리플렉션/딜레이로드를 구성합니다.
function(sw_addGameModule TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "HEADERS;EXCLUDE" ${ARGN})

	# 게임이 링크하는 키트는 게임 매니페스트(`SWGame.module.json`)의 의존 가운데 Kit 인 것이다 — 목록을 CMake 에 다시 적지 않는다.
	sw_isModuleActive(${TARGET_NAME} swGameActive)
	get_property(swGameDependencies GLOBAL PROPERTY SW_MODULE_${TARGET_NAME}_DEPENDENCIES)
	set(ARG_KITS "")
	foreach(swDependency IN LISTS swGameDependencies)
		get_property(swDependencyKind GLOBAL PROPERTY SW_MODULE_${swDependency}_KIND)
		if(swDependencyKind STREQUAL "Kit")
			list(APPEND ARG_KITS ${swDependency})
		endif()
	endforeach()

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
	sw_registerDynamicModule(${TARGET_NAME} game)

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

# ------------------------------------------------------------------------------
# ASan 테스트 보정 — 등록된 CTest 이름 하나에 적용한다.
#
# `sw_addTestExecutable` 안에만 두면 **손으로 add_test 한 테스트가 빠져** 그쪽만 평시 타임아웃으로
# ASan 에서 혼자 시간 초과가 난다. 그래서 한 곳으로 빼고 양쪽이 부른다 — 새 테스트를 손으로 등록해도
# 이 줄만 부르면 된다.
# ------------------------------------------------------------------------------
function(sw_applySanitizerTestProperties TEST_NAME)
	cmake_parse_arguments(ARG "" "" "ASAN_OPTIONS" ${ARGN})

	if(NOT SW_ENABLE_SANITIZER)
		return()
	endif()

	# ASan 은 실행을 한 자릿수 배로 늦춘다. 평시 기준을 그대로 쓰면 테스트가 전부 타임아웃으로
	# 떨어져 결함처럼 보인다.
	get_test_property(${TEST_NAME} TIMEOUT swCurrentTimeout)
	if(NOT swCurrentTimeout)
		set(swCurrentTimeout 30)
	endif()
	math(EXPR swCurrentTimeout "${swCurrentTimeout} * 10")

	# ODR 검사는 **아예 끈다(0).** 처음에는 1 로 두려 했다 — 크기가 다른 중복만 보고하니 진짜 ODR
	# 버그는 남는다는 계산이었다. 그런데 레벨 1 도 등록된 전역 전체를 훑는 비용은 그대로 치른다.
	# 모듈을 반복해 올리고 내리는 SmokeTest 가 비-ASan 6.9초 → ASan 1800초 초과(타임아웃)가 됐고,
	# 0 으로 바꾸자 5초로 끝났다. 260배는 오버헤드가 아니라 사용 불가다.
	#
	# 보고 자체도 이 구조에서는 영구 오탐이다: 플러그인 DLL 이 여럿이고(RHI_*, SWGame, GF_*,
	# EditorModule) 같은 SDK·CRT 헤더를 포함하니 헤더가 박는 전역이 DLL 마다 생긴다 — 나온 것이
	# `d3d11.h` 의 D3D11_DEFAULT 와 CRT 내부 _Avx2WmemEnabledWeakValue 다.
	# 테스트별 추가 옵션. 지금 쓰는 곳은 SmokeTest 하나다 — 아래 report_globals 주석 참고.
	set(swAsanOptions "detect_odr_violation=0")
	foreach(swAsanOption IN LISTS ARG_ASAN_OPTIONS)
		string(APPEND swAsanOptions ":${swAsanOption}")
	endforeach()

	# TSan: 경쟁이 하나라도 보고되면 종료 코드 66 으로 끝나 그 시험이 진다. 두 번째 스택까지 적어 교착 · 경쟁 원인을 좁힌다. 억제 목록은 비어 있는
	# 것이 정상이다(파일 머리말 — 스스로 동기화하는 서드파티는 계측해 짓는다). CI 와 손으로 돌린 ctest 가 같은 옵션을 쓴다.
	set(swTsanOptions "suppressions=${CMAKE_SOURCE_DIR}/cmake/Modules/Options/TsanSuppressions.txt:second_deadlock_stack=1:history_size=4")

	set_tests_properties(${TEST_NAME} PROPERTIES
		TIMEOUT ${swCurrentTimeout}
		ENVIRONMENT "ASAN_OPTIONS=${swAsanOptions};TSAN_OPTIONS=${swTsanOptions}"
	)
endfunction()

# ------------------------------------------------------------------------------
# sw_embedProcessManifest — Windows 실행 파일에 프로세스 설정 매니페스트를 박는다
#   (UTF-8 ANSI 코드 페이지 · 긴 경로; cmake/Modules/Platform/WindowsProcess.manifest 설명)
#   .manifest 를 소스로 주면 CMake 가 링크 단계에서 CMAKE_MT 로 합쳐 넣는다. 실행 파일마다 부른다 — 앱만 켜고 테스트를
#   빼면 테스트가 앱과 다른 코드 페이지에서 돌아 경로 인코딩 결함을 볼 수 없다.
# ------------------------------------------------------------------------------
function(sw_embedProcessManifest TARGET_NAME)
	if(NOT WIN32)
		return()
	endif()
	target_sources(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/cmake/Modules/Platform/WindowsProcess.manifest")
endfunction()

# ------------------------------------------------------------------------------
# sw_registerTestRun — 테스트 실행 파일 하나를 ctest 항목 하나로 등록한다
#
# 작업 폴더는 **구성과 무관하게 `Bin`** 이다. 테스트는 거기서 위로 올라가며 `Resource/` 를 찾고, 배포 구성은
# 실행 파일만 `TestBin` 으로 뺀다(`Bin` 에 테스트와 DXC 가 섞이지 않게). 주의: 실행 파일이 나가는 폴더를 작업 폴더로
# 쓰면 Shipping 에서 `TestBin` 에서 돌게 되고, `AppTest` 는 거기서 `App.exe` 를 못 찾아 진다.
# ------------------------------------------------------------------------------
function(sw_registerTestRun TEST_NAME TARGET_NAME)
	cmake_parse_arguments(ARG "RUN_SERIAL" "TIMEOUT" "ARGS;LABELS;ASAN_OPTIONS" ${ARGN})

	add_test(NAME ${TEST_NAME} COMMAND ${TARGET_NAME} ${ARG_ARGS})
	set_tests_properties(${TEST_NAME} PROPERTIES
		WORKING_DIRECTORY "${sw_output_directory}/Bin"
		LABELS "${ARG_LABELS}"
		TIMEOUT ${ARG_TIMEOUT}
	)
	if(ARG_RUN_SERIAL)
		set_tests_properties(${TEST_NAME} PROPERTIES RUN_SERIAL TRUE)
	endif()

	# ASan 의 ODR 검사를 완화한다. 이 엔진은 플러그인 DLL 이 여럿이고(RHI_*, SWGame, GF_*,
	# EditorModule) 그 DLL 들이 같은 SDK·CRT 헤더를 포함한다. 그러면 헤더가 박는 전역이
	# DLL 마다 생기고 ASan 은 그것을 ODR 위반으로 본다 — 실제로 나온 것이
	# `d3d11.h` 의 `D3D11_DEFAULT` 와 CRT 내부 `_Avx2WmemEnabledWeakValue` 다. 우리 코드가
	# 아니라 헤더 정의이고, 핫리로드로 DLL 사본이 오갈 때마다 다시 난다 — 영구 오탐이다.
	sw_applySanitizerTestProperties(${TEST_NAME} ASAN_OPTIONS ${ARG_ASAN_OPTIONS})
endfunction()

# ------------------------------------------------------------------------------
# sw_registerTestShards — 같은 실행을 SHARD_COUNT 개의 ctest 항목으로 가른다
#   1 이면 TEST_NAME 하나(`sw_registerTestRun` 그대로), 넘으면 `<TEST_NAME>_Shard<k>` 마다 `--test_shard=<k-1>/<n>` 을 ARGS 뒤에 붙인다.
#   나머지 인자(RUN_SERIAL · ARGS · LABELS · TIMEOUT · ASAN_OPTIONS)는 `sw_registerTestRun` 에 그대로 넘긴다.
# ------------------------------------------------------------------------------
function(sw_registerTestShards TEST_NAME TARGET_NAME SHARD_COUNT)
	cmake_parse_arguments(ARG "RUN_SERIAL" "TIMEOUT" "ARGS;LABELS;ASAN_OPTIONS" ${ARGN})
	set(runSerial "")
	if(ARG_RUN_SERIAL)
		set(runSerial RUN_SERIAL)
	endif()

	if(SHARD_COUNT LESS_EQUAL 1)
		sw_registerTestRun(${TEST_NAME} ${TARGET_NAME} ${runSerial}
			ARGS ${ARG_ARGS} LABELS "${ARG_LABELS}" TIMEOUT ${ARG_TIMEOUT} ASAN_OPTIONS ${ARG_ASAN_OPTIONS})
		return()
	endif()

	math(EXPR lastShard "${SHARD_COUNT} - 1")
	foreach(shardIndex RANGE 0 ${lastShard})
		math(EXPR shardNumber "${shardIndex} + 1")
		sw_registerTestRun(${TEST_NAME}_Shard${shardNumber} ${TARGET_NAME} ${runSerial}
			ARGS ${ARG_ARGS} --test_shard=${shardIndex}/${SHARD_COUNT} LABELS "${ARG_LABELS}" TIMEOUT ${ARG_TIMEOUT} ASAN_OPTIONS ${ARG_ASAN_OPTIONS})
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# sw_addTestExecutable — 테스트 실행 파일 타겟을 만들고 공통 PCH · 로그 태그 · ctest 등록을 한다
#
#   HOST_SPLIT    호스트 스위트(`SW_TEST_REQUIRES_HOST`)가 있는 실행 파일. ctest 항목을 둘로 가른다 —
#                 `<타깃>_NoGPU`(`--host_suites=exclude`, 라벨 `nogpu`, CI 가 도는 집합)와
#                 `<타깃>_HostOnly`(`--host_suites=only`, 라벨 `hostgpu`, 직렬). 어느 스위트가 호스트인지는
#                 **코드의 선언이 정한다** — 여기에 스위트 이름을 적지 않는다. 갈라진 두 항목 말고 전체 실행을
#                 하나 더 등록하지 말 것(라벨 없는 `ctest` 가 같은 시험을 두 번 돈다).
#   HOST_TIMEOUT  `_HostOnly` 의 제한 시간(기본: TIMEOUT).
#   SHARDS        ctest 항목을 이 수만큼 `<타깃>_Shard<k>` 로 갈라 병렬로 돌린다(`--test_shard=<k-1>/<n>`). 케이스는 **스위트 안에서 번갈아**
#                 나뉘므로 느린 스위트 하나가 끝을 정하는 실행 파일에 쓴다(ReflectionTest — 파서를 차례로 띄우는 스위트가 시간의 거의 전부).
#                 스위트 이름을 적지 않는다. HOST_SPLIT 과 함께 쓰면 `_NoGPU` 를 `<타깃>_NoGPU_Shard<k>` 로 가른다
#                 (`--host_suites=exclude --test_shard=…`, CI 가 병렬로 도는 쪽).
#   HOST_SHARDS   HOST_SPLIT 의 `_HostOnly` 를 이 수만큼 `<타깃>_HostOnly_Shard<k>` 로 가른다(기본 1 — 하나). 조각도 **직렬**이다
#                 (GPU 를 잡는다) — 합계 시간은 같고, 조각마다 제한 시간(HOST_TIMEOUT)을 따로 받는다. 조각마다 호스트 케이스가 하나는
#                 있어야 한다(`--host_suites=only` 가 아무것도 고르지 않으면 진다).
#   RUN_SERIAL    다른 테스트와 겹치면 안 되는 실행 파일(같은 파일 · 같은 장치를 쓰는 경우). **지금 쓰는 타겟은 없다.**
#                 쓸 때는 그 이유를 옆에 적는다.
# ------------------------------------------------------------------------------
# ------------------------------------------------------------------------------
# sw_splitShippingDebugInfo — 리눅스 Shipping: 디버그 정보를 `Symbols/<이름>.debug` 로 떼고 실행 파일에는 `.gnu_debuglink` 만 남긴다
#   (배포물에 정보가 실리지 않는다). Windows 는 PDB 가 원래 따로다(`CMAKE_PDB_OUTPUT_DIRECTORY`).
# ------------------------------------------------------------------------------
function(sw_splitShippingDebugInfo TARGET_NAME)
	if(NOT SW_SHIPPING_BUILD OR WIN32 OR SW_RELEASE_DEBUG_INFO STREQUAL "none")
		return()
	endif()
	if(NOT CMAKE_OBJCOPY)
		message(FATAL_ERROR "sw_splitShippingDebugInfo(${TARGET_NAME}): CMAKE_OBJCOPY is not set - install llvm-objcopy or binutils")
	endif()
	set(symbolsDir "${sw_output_directory}/Symbols")
	add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${symbolsDir}"
		COMMAND ${CMAKE_OBJCOPY} --only-keep-debug "$<TARGET_FILE:${TARGET_NAME}>" "${symbolsDir}/$<TARGET_FILE_NAME:${TARGET_NAME}>.debug"
		COMMAND ${CMAKE_OBJCOPY} --strip-debug "--add-gnu-debuglink=${symbolsDir}/$<TARGET_FILE_NAME:${TARGET_NAME}>.debug" "$<TARGET_FILE:${TARGET_NAME}>"
		COMMENT "Splitting debug info of ${TARGET_NAME} into Symbols/"
		VERBATIM)
endfunction()

function(sw_addTestExecutable TARGET_NAME)
	cmake_parse_arguments(ARG "RUN_SERIAL;HOST_SPLIT" "TIMEOUT;HOST_TIMEOUT;SHARDS;HOST_SHARDS" "SOURCES;LIBS;LABELS;DEFINITIONS;ASAN_OPTIONS" ${ARGN})
	if(ARG_HOST_SHARDS AND NOT ARG_HOST_SPLIT)
		message(FATAL_ERROR "sw_addTestExecutable(${TARGET_NAME}): HOST_SHARDS 는 HOST_SPLIT 의 `_HostOnly` 를 가른다 — HOST_SPLIT 없이 쓰지 않는다")
	endif()
	if(NOT ARG_SOURCES)
		file(GLOB_RECURSE ARG_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
	endif()

	add_executable(${TARGET_NAME} ${ARG_SOURCES})
	target_sources(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/Test/TestFramework/main.cpp" "${CMAKE_SOURCE_DIR}/Test/TestFramework/TestHostRuntime.cpp")
	sw_embedProcessManifest(${TARGET_NAME})
	set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Test")

	# 배포 빌드의 Bin 은 App.exe 와 Packs/ 만 담아야 한다. 테스트는 계속 빌드하되 옆 디렉터리로
	# 뺀다 — CI 가 Shipping 을 CoreTest 로 스모크할 수 있으면서 배포 산출물은 깨끗하다.
	# 작업 폴더는 그래도 `Bin` 이다(`sw_registerTestRun`).
	if(SW_SHIPPING_BUILD)
		set(testOutputDir "${sw_output_directory}/TestBin")
		# PDB 도 실행 파일 옆 — 시험은 배포물이 아니고, 크래시 핸들러의 스택(DbgHelp)이 실행 파일 폴더에서 PDB 를 찾는다.
		set_target_properties(${TARGET_NAME} PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${testOutputDir}"
			RUNTIME_OUTPUT_DIRECTORY_DEBUG "${testOutputDir}"
			RUNTIME_OUTPUT_DIRECTORY_RELEASE "${testOutputDir}"
			PDB_OUTPUT_DIRECTORY "${testOutputDir}"
			PDB_OUTPUT_DIRECTORY_RELEASE "${testOutputDir}"
		)
	endif()

	target_include_directories(${TARGET_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(${TARGET_NAME}
		PRIVATE
		TestFramework
		${ARG_LIBS}
		sw_global_options
	)

	# App 과 같은 이유로 테스트 실행 파일도 리플렉션 정적 라이브러리를 통째로 링크한다 —
	# 왜 그래야 하는지, 플랫폼마다 무슨 플래그인지는 `sw_linkWholeArchive` 머리말에 있다.
	# 플랫폼 가드는 여기 두지 않는다: `WIN32` 로 가드하면 리눅스 Shipping 만 조용히 깨진다.
	# Engine 은 모든 시험 실행 파일에 들어간다(TestFramework 가 끈다) — `LIBS` 에 적지 않은 실행 파일(CoreTest)도 Engine 의 등록기를
	# 통째로 받아야 기동 단계의 설정 역직렬화가 열거형을 찾는다. 같은 아카이브가 `LIBS` 에 또 나와도 된다.
	if(SW_SHIPPING_BUILD)
		sw_linkWholeArchive(${TARGET_NAME} Engine ${ARG_LIBS})
	endif()

	target_compile_definitions(${TARGET_NAME}
		PRIVATE
		"SW_LOG_TAG=\"Test\""
		${ARG_DEFINITIONS}
	)
	sw_configurePch(${TARGET_NAME} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

	if(NOT BUILD_TESTING)
		return()
	endif()

	set(timeout 30)
	if(ARG_TIMEOUT)
		set(timeout ${ARG_TIMEOUT})
	endif()

	set(labels "unit")
	if(ARG_LABELS)
		set(labels "${ARG_LABELS}")
	endif()

	set(runSerial "")
	if(ARG_RUN_SERIAL)
		set(runSerial RUN_SERIAL)
	endif()

	set(shardCount 1)
	if(ARG_SHARDS AND ARG_SHARDS GREATER 1)
		set(shardCount ${ARG_SHARDS})
	endif()

	if(NOT ARG_HOST_SPLIT)
		sw_registerTestShards(${TARGET_NAME} ${TARGET_NAME} ${shardCount} ${runSerial}
			LABELS "${labels}" TIMEOUT ${timeout} ASAN_OPTIONS ${ARG_ASAN_OPTIONS})
		return()
	endif()

	set(hostTimeout ${timeout})
	if(ARG_HOST_TIMEOUT)
		set(hostTimeout ${ARG_HOST_TIMEOUT})
	endif()

	sw_registerTestShards(${TARGET_NAME}_NoGPU ${TARGET_NAME} ${shardCount} ${runSerial}
		ARGS --host_suites=exclude LABELS "${labels};nogpu" TIMEOUT ${timeout} ASAN_OPTIONS ${ARG_ASAN_OPTIONS})
	# 직렬인 이유: 창을 띄우고 GPU 를 잡는다. 다른 GPU 테스트와 겹치면 서로를 느리게 만들고, 드라이버에 따라
	# 서로의 디바이스 생성을 막는다.
	set(hostShardCount 1)
	if(ARG_HOST_SHARDS AND ARG_HOST_SHARDS GREATER 1)
		set(hostShardCount ${ARG_HOST_SHARDS})
	endif()
	sw_registerTestShards(${TARGET_NAME}_HostOnly ${TARGET_NAME} ${hostShardCount} RUN_SERIAL
		ARGS --host_suites=only LABELS "${labels};hostgpu" TIMEOUT ${hostTimeout} ASAN_OPTIONS ${ARG_ASAN_OPTIONS})
endfunction()

# ------------------------------------------------------------------------------
# 이 구성이 일부러 짓지 않는 소스 — `Scripts/lint/gate/CheckSourceGlob.py` 가 빌드 트리에서 읽는 목록
#
# 그 게이트는 디스크의 소스와 compile_commands.json 을 맞춘다. 배포 구성은 에디터 · 핫 리로드 · 고르지 않은 RHI 백엔드를 **일부러**
# 짓지 않는데, 게이트는 그것을 CMake 와 따로 몰라 Shipping 트리에서 늘 졌다("compile_commands 에 없음" 95 개 — 그래서 Shipping 은
# `-L hostgpu` 만 돌려 왔다). 빼는 규칙은 여기 CMake 에 있으므로 빼는 자리가 직접 적고(`sw_declareUnbuiltSources` ·
# `sw_excludeUnbuiltSources`), 구성 끝에 `sw_writeUnbuiltSourceList` 가 빌드 트리에 목록을 쓴다 — 게이트는 그 목록만 읽는다.
# **다른 타겟으로 옮겨 짓는 것(키트 폴더 · RHI 모듈 · 지연 로드 훅을 부탁한 타겟)은 적지 않는다** — 지어지지 않으면 게이트가 잡아야 한다.
# ------------------------------------------------------------------------------
# 게이트는 빌드 트리 기준 같은 상대 경로(`generated/sw/config/UnbuiltSources.txt`)를 읽는다.
set(SW_UNBUILT_SOURCE_LIST "${CMAKE_BINARY_DIR}/generated/sw/config/UnbuiltSources.txt")

# 이 구성이 짓지 않는 소스를 적는다(절대 경로). 헤더도 받아 두지만 목록에는 `.c` · `.cpp` 만 쓴다.
function(sw_declareUnbuiltSources)
	set_property(GLOBAL APPEND PROPERTY SW_UNBUILT_SOURCES ${ARGN})
endfunction()

# 폴더(ARGN, 절대 경로) 아래의 .c · .cpp 를 전부 "이 구성이 짓지 않는 소스" 로 적는다 — 통째로 빼는 폴더(고르지 않은 게임 팩 ·
# 끈 GameFramework)에 쓴다.
function(sw_declareUnbuiltDirectory)
	foreach(directory IN LISTS ARGN)
		file(GLOB_RECURSE listSource CONFIGURE_DEPENDS "${directory}/*.c" "${directory}/*.cpp")
		sw_declareUnbuiltSources(${listSource})
	endforeach()
endfunction()

# 목록 변수 LIST_VAR 에서 정규식(ARGN)에 맞는 소스를 빼고, 뺀 것을 "이 구성이 짓지 않는 소스" 로 적는다.
# 빼기와 적기가 한 호출이라 한쪽만 하는 일이 없다. 다른 타겟으로 옮겨 짓는 것에는 쓰지 않는다(그때는 `list(FILTER)`).
function(sw_excludeUnbuiltSources LIST_VAR)
	set(listKept ${${LIST_VAR}})
	foreach(pattern IN LISTS ARGN)
		list(FILTER listKept EXCLUDE REGEX "${pattern}")
	endforeach()

	set(listRemoved ${${LIST_VAR}})
	if(listKept)
		list(REMOVE_ITEM listRemoved ${listKept})
	endif()
	sw_declareUnbuiltSources(${listRemoved})

	set(${LIST_VAR} "${listKept}" PARENT_SCOPE)
endfunction()

# 구성 끝(모든 소스 타겟을 만든 뒤)에 한 번 — 적어 둔 것을 빌드 트리에 쓴다. 내용이 같으면 파일을 다시 쓰지 않는다.
function(sw_writeUnbuiltSourceList)
	# 지연 로드 훅은 그것을 부탁한 타겟이 있어야 지어진다(`sw_addDelayloadHook`). 아무도 부탁하지 않은 구성(배포 · Windows 밖)은 짓지 않는다.
	get_property(bHookRequested GLOBAL PROPERTY SW_DELAYLOAD_HOOK_REQUESTED)
	if(NOT bHookRequested AND TARGET Engine)
		get_property(hookSource TARGET Engine PROPERTY SW_DELAYLOAD_HOOK_SOURCE)
		if(hookSource)
			sw_declareUnbuiltSources("${hookSource}")
		endif()
	endif()

	get_property(listSource GLOBAL PROPERTY SW_UNBUILT_SOURCES)
	list(FILTER listSource INCLUDE REGEX "\\.(c|cpp)$")
	set(listRelative "")
	foreach(source IN LISTS listSource)
		file(RELATIVE_PATH relativePath "${CMAKE_SOURCE_DIR}" "${source}")
		list(APPEND listRelative "${relativePath}")
	endforeach()
	if(listRelative)
		list(REMOVE_DUPLICATES listRelative)
		list(SORT listRelative)
	endif()

	string(JOIN "\n" content ${listRelative})
	file(CONFIGURE OUTPUT "${SW_UNBUILT_SOURCE_LIST}" CONTENT "${content}\n" @ONLY)
	list(LENGTH listRelative unbuiltCount)
	message(STATUS "[Build] Sources this configuration does not build: ${unbuiltCount} (${SW_UNBUILT_SOURCE_LIST})")
endfunction()

# ------------------------------------------------------------------------------
# Windows delay-load + 훅 소스 바인딩
# 지연 로드는 이 함수와 sw_addDelayloadSystemDlls 로만 정한다(CheckDelayLoadSites). lld 의 x64 지연 로드 썽크(__tailMerge_<dll>)는
# xmm0~3 을 [rsp] · [rsp+10h] · … 에 두고 __delayLoadHelper2 를 부르는데 [rsp..rsp+1Fh] 가 그 호출의 홈 공간이라, 첫 호출이 묶으면
# 그 호출의 첫 float 인자(xmm0)가 망가진다. 그래서 여기서 넣는 훅 TU 가 bindDelayLoadImports 를 내보내고, 모듈을 올리는 자리
# (LiveReloadManager · ModuleHost)와 엔진 기동이 그 코드가 돌기 전에 부른다(ModuleImageUtil::bindDelayLoadImports).
# ------------------------------------------------------------------------------
function(sw_addDelayloadHook TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "DLLS" ${ARGN})

	if(NOT WIN32)
		return()
	endif()

	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "sw_addDelayloadHook: target '${TARGET_NAME}' does not exist")
	endif()

	# 훅 소스가 지어진다는 표시 — 아무도 부탁하지 않으면 `sw_writeUnbuiltSourceList` 가 짓지 않는 소스로 적는다.
	set_property(GLOBAL PROPERTY SW_DELAYLOAD_HOOK_REQUESTED TRUE)

	set(swHookSrc "")

	if(TARGET Engine)
		get_property(swHookSrc TARGET Engine PROPERTY SW_DELAYLOAD_HOOK_SOURCE)
	endif()

	# 속성이 **있는데 그 파일이 없으면** 그것은 설정 실수다. 조용히 넘어가면 안 된다 — 파일을 옮기고 속성을
	# 놓치면 아래 폴백이 매번 대신 고쳐 주어 아무도 눈치채지 못한다. 속성을 두는 이유가 "훅 소스의 위치를
	# 한 곳에서 안다" 이므로, 그 한 곳이 틀리면 여기서 멈춘다.
	if(swHookSrc AND NOT EXISTS "${swHookSrc}")
		message(FATAL_ERROR "[sw_addDelayloadHook] SW_DELAYLOAD_HOOK_SOURCE points at a file that does not exist: ${swHookSrc}")
	endif()

	if(NOT swHookSrc)
		set(swHookSrc "${CMAKE_SOURCE_DIR}/Source/Engine/Module/DelayLoadNotifyHook.cpp")

		if(NOT EXISTS "${swHookSrc}")
			message(FATAL_ERROR "[sw_addDelayloadHook] DelayLoadNotifyHook.cpp not found: ${swHookSrc}")
		endif()
	endif()

	target_sources(${TARGET_NAME} PRIVATE "${swHookSrc}")
	target_link_libraries(${TARGET_NAME} PRIVATE delayimp)

	foreach(dll IN LISTS ARG_DLLS)
		target_link_options(${TARGET_NAME} PRIVATE "LINKER:/DELAYLOAD:${dll}")
	endforeach()
endfunction()

# ------------------------------------------------------------------------------
# Windows delay-load — 필요할 때만 올리는 시스템 DLL(훅 · 미리 묶기 없음)
# 미리 묶지 않으므로 첫 호출이 썽크를 지난다 — 이 DLL 에서 부르는 함수 가운데 **첫 인자가 float · double 인 것이 없어야** 한다(xmm0 이 망가진다).
# 지금 목록(D3DCompile · D3DReflect · MFStartup · MFCreate* · XAudio2Create · Tracy C API)은 첫 인자가 모두 포인터 · 정수다. 빠지면 기능이
# 꺼지는 선택 DLL(Windows N 의 Media Foundation 등)이라 기동에서 미리 올리지 않는다.
# ------------------------------------------------------------------------------
function(sw_addDelayloadSystemDlls TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "DLLS" ${ARGN})
	if(NOT WIN32)
		return()
	endif()

	target_link_libraries(${TARGET_NAME} PRIVATE delayimp)
	foreach(dll IN LISTS ARG_DLLS)
		target_link_options(${TARGET_NAME} PRIVATE "LINKER:/DELAYLOAD:${dll}")
	endforeach()
endfunction()
