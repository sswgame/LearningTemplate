# ==============================================================================
# @file cmake/Engine/ModuleTargets.cmake
# @brief 모듈 라이브러리 팩토리 — sw_add_module_library(기본값 한 자리)와 그것을 부르는 RHI 백엔드 · 키트 · 게임 모듈, 모듈 출력 폴더 · 서드파티 DLL 모으기
# ==============================================================================

# 모듈 · 핫 리로드 타깃의 런타임 출력 — 모듈 DLL(rhi · kit · game · editor)은 `Bin/Modules/`, 모두가 링크하는 GameFramework 는 `Bin/` 이다.
# 런타임은 `ModuleLibraryUtil::findModuleLibraryPath` 로 같은 순서(Modules → Bin)로 찾는다. SHARED/MODULE 은 플랫폼에 따라 LIBRARY 출력(Lib/)으로
# 갈 수 있어 둘 다 정한다. `$<0:>` 는 BuildLayout 과 같다.
# vcpkg 의 applocal 이 `Bin/Modules` 의 모듈 옆에 서드파티 DLL 을 복사하지 않게 하고, 같은 일을 모듈마다 스테이지 폴더에서 해 `Bin` 으로 옮긴다
# (`StageModuleRuntimeDLLs.cmake`). 서드파티 DLL 은 `Bin` 에 한 벌이다 — 모듈 로드(`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | DEFAULT_DIRS`)는 모듈 폴더 다음에
# 실행 파일 폴더를 본다. vcpkg 의 `add_library` 래퍼는 타깃을 만드는 순간 `VCPKG_APPLOCAL_DEPS` 를 읽으므로 부르는 쪽(`sw_add_module_library`)이 그 순간만 끈다.
function(sw_stage_module_runtime_dlls TARGET_NAME)
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
			-P "${CMAKE_SOURCE_DIR}/cmake/Engine/StageModuleRuntimeDLLs.cmake"
		VERBATIM
	)
endfunction()

function(sw_set_module_bin_output TARGET_NAME KIND)
	set(output_dir "${CMAKE_BINARY_DIR}/Bin/Modules")
	if(KIND STREQUAL "gameframework")
		set(output_dir "${CMAKE_BINARY_DIR}/Bin")
	endif()
	set_target_properties(${TARGET_NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${output_dir}$<0:>"
		LIBRARY_OUTPUT_DIRECTORY "${output_dir}$<0:>"
	)
endfunction()

# 키트 라이브러리의 컴파일 설정. 키트는 명령줄 정의가 서로 같아야 PCH 하나(`sw_kit_pch`)를 나눠 쓴다 — 타깃 고유 정의는 강제 포함 헤더로 준다.
# 자동 정의 `<타깃>_EXPORTS` 는 아무도 쓰지 않아 끈다.
function(sw_configure_kit_module_compile TARGET_NAME LIB_TYPE LOG_TAG MODULE_NAME DEFINITIONS)
	set_target_properties(${TARGET_NAME} PROPERTIES DEFINE_SYMBOL "")
	if(DEFINITIONS)
		target_compile_definitions(${TARGET_NAME} PRIVATE ${DEFINITIONS})
	endif()
	set(list_target_defs_option LOG_TAG "${LOG_TAG}" REGISTRAR_MODULE "${MODULE_NAME}")
	set(list_pch_definition "")
	if(LIB_TYPE STREQUAL "SHARED")
		# 키트 TU 는 `SW_GF_EXPORTS` 를 주지 않는다 — 주면 include 한 Base 선언까지 dllexport 로 풀린다(C-3).
		# 서버 키트(`GF_Server_<X>`)는 `SW_KIT_SERVER_EXPORTS`, 클라이언트 키트(`GF_Client_<X>`)는 `SW_KIT_CLIENT_EXPORTS`, 그 밖은 `SW_KIT_EXPORTS` 만 내보낸다 — 서버 · 클라이언트 키트가 include 한 런타임 키트 헤더(`SW_KIT_API`)는
		# dllimport 로 남아 런타임 키트 클래스를 다시 내보내지 않는다. `SW_KIT_IMPORTS` 는 모든 키트 TU 가 가진다(자기 내보내기 매크로가 먼저 읽힌다) —
		# 키트 PCH 하나를 나눠 쓰려면 명령줄 정의가 같아야 한다.
		if(TARGET_NAME MATCHES "^GF_Server_")
			list(APPEND list_target_defs_option KIT_SERVER_EXPORTS)
		elseif(TARGET_NAME MATCHES "^GF_Client_")
			list(APPEND list_target_defs_option KIT_CLIENT_EXPORTS)
		else()
			list(APPEND list_target_defs_option KIT_EXPORTS)
		endif()
		target_compile_definitions(${TARGET_NAME} PUBLIC SW_KIT_IMPORTS)
		set(list_pch_definition DEFINITIONS SW_KIT_IMPORTS)
	endif()
	sw_set_target_defs_header(${TARGET_NAME} ${list_target_defs_option})
	sw_add_shared_pch_owner(sw_kit_pch "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h"
		LINK GameFramework Engine sw_public_source_includes sw_global_options ${list_pch_definition})
endfunction()

# ------------------------------------------------------------------------------
# sw_add_module_library — 엔진 모듈 라이브러리 하나(RHI 백엔드 · 키트 · 게임 · GameFramework · 에디터)
#
# 모듈마다 다른 것만 받고 나머지는 여기서 정한다(언리얼 ModuleRules 의 기본값 자리). 꺼진 모듈 건너뛰기(`sw_skip_inactive_module`)는
# 부르는 쪽이 먼저 한다 — 함수는 부른 쪽을 return 시킬 수 없다.
#
#   KIND        rhi | kit | game | gameframework | editor | editorextension   동적 모듈 레지스트리 종류(`sw_register_dynamic_module`)
#   DEV_TYPE    SHARED | MODULE                              개발 빌드의 라이브러리 종류. 배포(SW_SHIPPING_BUILD)는 늘 STATIC 이다
#   EXPORTS     GF | MODULE | MODULE_SHARED | EDITOR          내보내기 매크로(`sw_configure_dll_exports`)
#   LOG_TAG     로그 태그(SW_LOG_TAG)
#   FOLDER      IDE 폴더
#   SOURCES     소스
#   LINK_PUBLIC · LINK_PRIVATE   링크(PRIVATE 끝에 sw_global_options 가 붙는다)
#   DEFINITIONS 더 붙일 PRIVATE 정의
#   REFLECTION_HEADERS  리플렉션 입력 헤더. 비우면 폴더를 재귀로 훑는다(`sw_add_reflection_step` 자동 훑기)
#   REFLECTION_EXCLUDE  자동 훑기에서 뺄 경로 정규식(하위 폴더의 다른 모듈 — 키트 · 게임의 `/Editor/`)
#   NO_REFLECTION       리플렉션 단계를 두지 않는다(RHI 백엔드)
#   NO_REGISTRY         동적 모듈 레지스트리에 넣지 않는다 — App 이 `Bin/Modules` 에서 올리지 않는 게임 팩(`sw_add_game_module`, 활성 게임이 아닌 것)
#   MODULE_NAME         매니페스트 · 산출물 · ABI 스탬프 · 리플렉션 타입의 모듈 이름(기본 타깃 이름) — 게임은 타깃 `SWGame_<게임>`, 모듈 `SWGame` 이다
#   CONTENT_OWNER       콘텐츠 모듈 검사에서 이 모듈 타입의 주인(기본 MODULE_NAME) — 게임은 이름이 모두 SWGame 이라 `game:<폴더>` 로 가른다
# ------------------------------------------------------------------------------
function(sw_add_module_library TARGET_NAME)
	cmake_parse_arguments(ARG "NO_REFLECTION;NO_REGISTRY" "KIND;DEV_TYPE;EXPORTS;LOG_TAG;FOLDER;REFLECTION_EXCLUDE;MODULE_NAME;CONTENT_OWNER"
		"SOURCES;LINK_PUBLIC;LINK_PRIVATE;DEFINITIONS;REFLECTION_HEADERS" ${ARGN})

	if(NOT ARG_DEV_TYPE MATCHES "^(SHARED|MODULE)$")
		message(FATAL_ERROR "sw_add_module_library(${TARGET_NAME}): DEV_TYPE 는 SHARED | MODULE 이다 (받은 값: ${ARG_DEV_TYPE})")
	endif()
	if(SW_SHIPPING_BUILD)
		set(lib_type STATIC)
	else()
		set(lib_type ${ARG_DEV_TYPE})
	endif()
	if(NOT ARG_MODULE_NAME)
		set(ARG_MODULE_NAME ${TARGET_NAME})
	endif()
	if(NOT ARG_CONTENT_OWNER)
		set(ARG_CONTENT_OWNER ${ARG_MODULE_NAME})
	endif()
	# 게임 팩끼리 PCH 하나(`sw_game_pch`)를 나눠 쓴다 — 명령줄 정의가 같아야 하므로 Dev · clang-cl · PCH 켬일 때만(키트의 `sw_kit_pch` 와 같은 장치).
	set(share_game_pch OFF)
	if(ARG_KIND STREQUAL "game" AND SW_ENABLE_PCH AND MSVC AND NOT lib_type STREQUAL "STATIC")
		set(share_game_pch ON)
	endif()

	# `Bin/Modules` 에 가는 모듈은 vcpkg applocal 을 끄고(이 함수 범위만) 서드파티 DLL 을 `Bin` 으로 모은다(`sw_stage_module_runtime_dlls`).
	set(stage_runtime_dlls OFF)
	if(VCPKG_APPLOCAL_DEPS AND NOT lib_type STREQUAL "STATIC" AND NOT ARG_KIND STREQUAL "gameframework")
		set(VCPKG_APPLOCAL_DEPS OFF)
		set(stage_runtime_dlls ON)
	endif()
	# 개발 전용 소스(단위 테스트 · 개발 명령)는 배포 구성이 짓지 않는다(GameFramework · 키트 · 게임 · 에디터 확장 모두).
	if(SW_SHIPPING_BUILD)
		sw_remove_excluded_sources(ARG_SOURCES ${SW_DEV_ONLY_SOURCE_PATTERNS})
	endif()
	add_library(${TARGET_NAME} ${lib_type} ${ARG_SOURCES})
	sw_require_deterministic_mark_for_rollback(${TARGET_NAME} ${ARG_SOURCES})
	sw_keep_import_library_if_unchanged(${TARGET_NAME})
	if(stage_runtime_dlls)
		sw_stage_module_runtime_dlls(${TARGET_NAME})
	endif()
	set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "${ARG_FOLDER}")
	if(NOT ARG_MODULE_NAME STREQUAL TARGET_NAME)
		set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME "${ARG_MODULE_NAME}")
	endif()
	# 모듈 폴더는 include 경로로 내보내지 않는다 — 키트 · 게임 헤더도 `Source/` 기준 전체 경로로 쓴다(`sw_verify_source_include_roots`).
	sw_apply_module_layer_guard(${TARGET_NAME} ${ARG_KIND} ${ARG_MODULE_NAME} "${ARG_CONTENT_OWNER}")
	if(ARG_KIND MATCHES "^(gameframework|kit|game)$")
		sw_require_template_definitions(${TARGET_NAME}) # 부분(GameFramework 층)보다 먼저 — 부분이 옵션을 복사한다
	endif()
	if(ARG_LINK_PUBLIC)
		target_link_libraries(${TARGET_NAME} PUBLIC ${ARG_LINK_PUBLIC})
	endif()
	target_link_libraries(${TARGET_NAME} PRIVATE ${ARG_LINK_PRIVATE} sw_global_options)
	set(list_identity_definition "SW_LOG_TAG=\"${ARG_LOG_TAG}\"" "SW_REGISTRAR_MODULE=\"${ARG_MODULE_NAME}\"")
	if(ARG_KIND STREQUAL "kit")
		sw_configure_kit_module_compile(${TARGET_NAME} ${lib_type} "${ARG_LOG_TAG}" ${ARG_MODULE_NAME} "${ARG_DEFINITIONS}")
	elseif(share_game_pch)
		# 자동 정의 `<타깃>_EXPORTS`(`SWGame_<게임>_EXPORTS`)는 게임마다 달라 PCH 를 나눌 수 없고 아무도 쓰지 않는다.
		set_target_properties(${TARGET_NAME} PROPERTIES DEFINE_SYMBOL "")
		# 키트를 링크하는 게임 모듈은 키트 타깃의 PUBLIC `SW_KIT_IMPORTS` 를 물려받는다. 게임 PCH 와 게임 TU 의 명령줄 정의가 같아야
		# 하므로 모든 게임이 이 정의를 직접 가지고 PCH 소유자도 같게 가진다(키트가 없는 게임에도 해롭지 않다. 키트 헤더만 읽는다).
		# 게임 모듈 이름은 모두 SWGame 이라 등록자 이름(`SW_REGISTRAR_MODULE`)도 PCH 소유자와 같다.
		target_compile_definitions(${TARGET_NAME} PRIVATE ${list_identity_definition} SW_KIT_IMPORTS ${ARG_DEFINITIONS})
		sw_configure_dll_exports(${TARGET_NAME} ${lib_type} ${ARG_EXPORTS})
		sw_add_shared_pch_owner(sw_game_pch "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h"
			LINK Engine ModuleAPI GameFramework sw_global_options
			DEFINITIONS ${list_identity_definition} SW_MODULE_EXPORTS SW_KIT_IMPORTS ${ARG_DEFINITIONS})
	else()
		target_compile_definitions(${TARGET_NAME} PRIVATE ${list_identity_definition} ${ARG_DEFINITIONS})
		sw_configure_pch(${TARGET_NAME} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
		sw_configure_dll_exports(${TARGET_NAME} ${lib_type} ${ARG_EXPORTS})
	endif()

	if(NOT lib_type STREQUAL "STATIC")
		sw_set_module_bin_output(${TARGET_NAME} ${ARG_KIND})
	endif()

	if(NOT ARG_NO_REGISTRY)
		sw_register_dynamic_module(${TARGET_NAME} ${ARG_KIND} MODULE_NAME ${ARG_MODULE_NAME})
	endif()

	sw_set_unity_build(${TARGET_NAME})

	if(NOT ARG_NO_REFLECTION)
		# 게임 모듈의 타입은 다른 모듈이 링크하지 않는다. 타입을 내보내는 매크로도 없다(`GameModuleExports.h` 는 C-ABI 진입점만 만든다).
		set(list_reflection_option "")
		if(ARG_KIND STREQUAL "game")
			set(list_reflection_option MODULE_LOCAL)
		endif()
		sw_add_reflection_step(${TARGET_NAME} ${list_reflection_option}
			MODULE ${ARG_MODULE_NAME}
			CONTENT_OWNER ${ARG_CONTENT_OWNER}
			HEADERS ${ARG_REFLECTION_HEADERS}
			INCLUDES "${CMAKE_SOURCE_DIR}/Source"
			EXCLUDE_REGEX "${ARG_REFLECTION_EXCLUDE}"
		)
	endif()

	if(ARG_KIND STREQUAL "kit")
		sw_use_shared_pch(${TARGET_NAME} sw_kit_pch)   # 소스(리플렉션 산출물 포함)가 다 붙은 뒤
	elseif(share_game_pch)
		sw_use_shared_pch(${TARGET_NAME} sw_game_pch)
	endif()
endfunction()

# ------------------------------------------------------------------------------
# sw_add_module_part: 모듈 하나의 소스 일부를 의존을 좁힌 OBJECT 라이브러리로 짓고, 오브젝트를 그 모듈 라이브러리 안에 그대로 넣는다.
#
# 모듈이 링크하는 라이브러리(에디터의 imgui 계열) 가운데 이 부분이 몰라야 하는 것을 컴파일로 막는다. 부분은 `LINK` 의 include · 정의만 보므로
# 그 헤더를 include 하면 "file not found" 로 멈춘다. 정적 라이브러리 · 별도 DLL 이 아닌 까닭: 확장 모듈이 따로 링크하면 정적 상태가
# 두 벌이 되고 export 가 빠질 수 있다(언리얼이 모듈을 쪼개지 않고 Private 폴더 의존만 가르는 것과 같은 자리).
# 정의 · 컴파일 옵션 · 자기 include 자리(내보내기 매크로 · 로그 태그 · 리플렉션 산출물 · FlagOps 강제 포함)는 모듈 타깃에서 그대로 받아
# 같은 값을 두 곳에 적지 않는다. 모듈의 sw_add_module_library 다음에 부른다(리플렉션 산출 단계에 의존을 건다).
#
#   OWNER    모듈 타깃(sw_add_module_library 로 만든 것)
#   SOURCES  부분의 소스(모듈 SOURCES 에서는 부르는 쪽이 뺀다)
#   LINK     부분이 쓰는 라이브러리(모듈의 LINK_PRIVATE 보다 좁게)
# ------------------------------------------------------------------------------
function(sw_add_module_part PART_NAME)
	cmake_parse_arguments(ARG "" "OWNER" "SOURCES;LINK" ${ARGN})
	if(NOT TARGET ${ARG_OWNER})
		message(FATAL_ERROR "sw_add_module_part(${PART_NAME}): OWNER '${ARG_OWNER}' is not a target")
	endif()
	if(NOT ARG_SOURCES)
		message(FATAL_ERROR "sw_add_module_part(${PART_NAME}): SOURCES is empty")
	endif()
	add_library(${PART_NAME} OBJECT ${ARG_SOURCES})
	get_target_property(owner_folder ${ARG_OWNER} FOLDER)
	# 부분의 오브젝트는 모듈 라이브러리에 들어가므로 이미지와 같은 코드 모델로 짓는다. CMake 는 SHARED · MODULE 자신에게만 PIC 를 붙이고
	# 거기 넣는 OBJECT 라이브러리에는 붙이지 않는다 — 빠지면 ELF 링크가 `R_X86_64_PC32 ... recompile with -fPIC` 로 멈춘다(COFF 인 Windows 는 무관).
	get_target_property(owner_type ${ARG_OWNER} TYPE)
	# 플랫폼이 전체를 PIC 로 짓게 했으면(Android — 앱이 공유 라이브러리) 정적 모듈의 부분도 따른다.
	if(owner_type MATCHES "^(SHARED|MODULE)_LIBRARY$" OR CMAKE_POSITION_INDEPENDENT_CODE)
		set(part_pic ON)
	else()
		set(part_pic OFF)
	endif()
	set_target_properties(${PART_NAME} PROPERTIES FOLDER "${owner_folder}" POSITION_INDEPENDENT_CODE ${part_pic})
	target_link_libraries(${PART_NAME} PRIVATE ${ARG_LINK} sw_global_options)
	# 생성식(`$<TARGET_PROPERTY:…>`)으로 받으면 모듈이 링크한 라이브러리의 사용 요구까지 따라와(imgui include 자리) 경계가 사라진다.
	# 구성 때 모듈 자신의 값만 읽는다.
	foreach(property COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES)
		get_target_property(list_owner_value ${ARG_OWNER} ${property})
		if(list_owner_value)
			set_property(TARGET ${PART_NAME} APPEND PROPERTY ${property} ${list_owner_value})
		endif()
	endforeach()
	if(TARGET ${ARG_OWNER}_ReflectionGen)
		add_dependencies(${PART_NAME} ${ARG_OWNER}_ReflectionGen)
	endif()
	sw_configure_pch(${PART_NAME} "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
	sw_set_unity_build(${PART_NAME})
	# OBJECT 라이브러리를 직접 링크하면 오브젝트가 모듈 라이브러리에 들어간다(CMake 3.12+). 확장 모듈은 모듈의 export 로 그대로 쓴다.
	target_link_libraries(${ARG_OWNER} PRIVATE ${PART_NAME})
endfunction()

# RHI 그래픽스 백엔드 MODULE — 표(CookContract.json rhi_backends)의 이름 하나를 받는다. Dev 만 짓는다(배포는 Source/Engine/CMakeLists.txt 가
# 백엔드 하나를 Engine 에 넣는다).
function(sw_add_rhi_backend_module BACKEND_NAME)
	set(module_name ${SW_RHI_BACKEND_${BACKEND_NAME}_MODULE})
	sw_get_rhi_backend_sources(${BACKEND_NAME} list_device_source)
	sw_skip_inactive_module(${module_name} skip)
	if(skip)
		# 장치 소스는 모듈 폴더 밖(Graphics/RHI/<폴더>)에 있다 — 그것도 이 구성이 짓지 않는다.
		sw_declare_excluded_sources(${list_device_source})
		return()
	endif()
	sw_add_module_library(${module_name}
		KIND rhi
		DEV_TYPE MODULE
		EXPORTS MODULE
		LOG_TAG "RHI"
		FOLDER "Source/Engine/Graphics/RHI"
		SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ModuleEntry.cpp" ${list_device_source}
		LINK_PRIVATE Engine ${SW_RHI_BACKEND_${BACKEND_NAME}_GRAPHICS_LIBS} sw_third_party_includes
		DEFINITIONS SW_ENGINE_INTERNAL
		NO_REFLECTION
	)
endfunction()

# GameFramework 장르 키트 — 폴더의 소스 전부. 리플렉션 헤더는 폴더를 재귀로 훑는다(키트 루트의 *.h 만 모으면 하위 폴더의 REFLECT() 가 조용히 빠진다).
function(sw_add_game_framework_kit KIT_NAME)
	# 꺼진 키트(프로젝트가 껐거나 이 플랫폼 · 구성에 없는 것)는 짓지 않는다 — 매니페스트(`<키트>.module.json`)가 정한다.
	sw_skip_inactive_module(${KIT_NAME} skip)
	if(skip)
		return()
	endif()
	sw_glob_source_tree(list_kit_source RECURSE "*.cpp" "*.c" "*.h" "*.hpp")
	# 키트의 에디터 확장은 Plugin/Editor/<이름>/ 의 별도 모듈이다(GF_EditorPlugin_<이름>) — 키트 DLL(배포본 포함)에 섞지 않는다. 게임의 Editor/ 도 같다.
	list(FILTER list_kit_source EXCLUDE REGEX "/Editor/")
	sw_add_module_library(${KIT_NAME}
		KIND kit
		DEV_TYPE SHARED
		EXPORTS GF
		LOG_TAG "${KIT_NAME}"
		FOLDER "Source/GameFramework/Kits"
		SOURCES ${list_kit_source}
		LINK_PUBLIC GameFramework Engine sw_public_source_includes
		REFLECTION_EXCLUDE "/Editor/"
	)
	# 키트의 에디터 확장(언리얼 플러그인의 <X>Editor 모듈)은 키트 폴더의 Editor/ 다. 런타임 · 서버로 나뉜 키트는 `Runtime/` 이 이 함수를 부르고
	# 확장은 그 옆(`<기능>/Editor/`)에 있다.
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Editor/CMakeLists.txt")
		add_subdirectory(Editor)
	else()
		get_filename_component(kit_folder_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
		if(kit_folder_name STREQUAL "Runtime" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Editor/CMakeLists.txt")
			add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Editor" "${CMAKE_CURRENT_BINARY_DIR}/Editor")
		endif()
	endif()
endfunction()

# 서버 · 클라이언트 키트(GF_Server_<X> · GF_Client_<X>)가 같은 기능의 런타임 키트(GF_<X>)를 링크합니다.
# 일반 import 다 — 모듈 호스트가 런타임 키트의 섀도 복사본에 세대 이름을 주고 이 키트 복사본의 import 이름을 그 이름으로 바꾼다
# (`ModuleShadowCopy::writeShadowCopy`). 그래서 의존은 매니페스트에 있어야 한다(없으면 로드가 거절된다).
function(sw_link_runtime_kit KIT_NAME RUNTIME_KIT_NAME)
	# 링크는 매니페스트 의존이어야 한다 — 키트 폴더를 의존 순서로 들어가므로(Kits/CMakeLists.txt) 그래야 런타임 키트 타깃이 먼저 선다.
	get_property(list_dependency GLOBAL PROPERTY SW_MODULE_${KIT_NAME}_DEPENDENCIES)
	if(NOT RUNTIME_KIT_NAME IN_LIST list_dependency)
		message(FATAL_ERROR "sw_link_runtime_kit: ${KIT_NAME}.module.json must list ${RUNTIME_KIT_NAME} in _listDependency")
	endif()
	if(NOT TARGET ${KIT_NAME} OR NOT TARGET ${RUNTIME_KIT_NAME})
		message(FATAL_ERROR "sw_link_runtime_kit: target '${KIT_NAME}' or '${RUNTIME_KIT_NAME}' does not exist")
	endif()
	target_link_libraries(${KIT_NAME} PUBLIC ${RUNTIME_KIT_NAME})
endfunction()

# ------------------------------------------------------------------------------
# sw_add_game_module — 게임 팩 모듈 하나. 게임 폴더의 CMakeLists 한 줄(`sw_add_game_module(SWGame)`), 인자는 모듈 이름(엔진과의 계약 `SWGame`)이다.
#
# 타깃은 `SWGame_<게임>` 이고 산출물 · 매니페스트 · ABI 스탬프는 모듈 이름 그대로(`SWGame`)다 — 핫 리로드 · 모듈 호스트는 모듈 이름으로 찾는다.
# 활성 게임(`SW_ACTIVE_GAME`): 동적 모듈 레지스트리에 들어가고(App 이 먼저 짓고 Shipping 이 정적으로 링크한다) `Bin/Modules/SWGame` 이다.
#   타깃 이름은 전역 속성 `SW_ACTIVE_GAME_TARGET` 에 둔다 — 링크 · 테스트 의존은 이 속성으로 묻는다(이름을 적지 않는다).
# 함께 짓는 게임(`SW_GAMES`, Dev 전용): 레지스트리 밖(`SW_GAME_PACK_TARGETS`)이고 `Bin/Games/<게임>/SWGame`, 링크 PDB 는 `Bin/Symbols/Games/<게임>/` 이다 —
#   게임마다 같은 파일 이름이라 폴더로 가른다. 그 게임의 매니페스트를 DLL 옆에 둔다(`-game=` 으로 고르면 모듈 카탈로그가 읽는다).
# 링크하는 키트는 그 게임 매니페스트의 의존 가운데 Kit 인 것이다(CMake 에 다시 적지 않는다) — 활성 게임은 모듈 표, 함께 짓는 게임은
#   `sw_resolve_module_manifests` 가 읽어 둔 `SW_GAME_PACK_<게임>_DEPENDENCIES`. 게임의 에디터 확장(Editor/)은 활성 게임만 짓는다.
# 엔진 · GameFramework · 키트는 모든 게임이 같은 타깃을 링크하므로 한 번만 컴파일되고, 게임끼리 PCH 하나(`sw_game_pch`)를 나눠 쓴다.
# ------------------------------------------------------------------------------
function(sw_add_game_module MODULE_NAME)
	get_filename_component(game_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
	set(target_name "${MODULE_NAME}_${game_name}")
	set(active_game OFF)
	if(game_name STREQUAL SW_ACTIVE_GAME)
		set(active_game ON)
	endif()

	if(active_game)
		sw_get_module_dependencies_of_kind(${MODULE_NAME} Kit list_kit)
		set(list_registry_option "")
	else()
		get_property(list_dependency GLOBAL PROPERTY SW_GAME_PACK_${game_name}_DEPENDENCIES)
		get_property(list_module_name GLOBAL PROPERTY SW_MODULE_NAMES)
		set(list_kit "")
		foreach(dependency IN LISTS list_dependency)
			if(NOT dependency IN_LIST list_module_name)
				message(FATAL_ERROR "[Games] ${game_name}: ${MODULE_NAME}.module.json depends on '${dependency}', which has no manifest (missing module)")
			endif()
			get_property(dependency_kind GLOBAL PROPERTY SW_MODULE_${dependency}_KIND)
			get_property(dependency_active GLOBAL PROPERTY SW_MODULE_${dependency}_ACTIVE)
			if(NOT dependency_active)
				get_property(dependency_reason GLOBAL PROPERTY SW_MODULE_${dependency}_REASON)
				message(FATAL_ERROR "[Games] ${game_name}: ${MODULE_NAME}.module.json depends on '${dependency}', which is ${dependency_reason}")
			endif()
			if(dependency_kind STREQUAL "Kit")
				list(APPEND list_kit ${dependency})
			endif()
		endforeach()
		set(list_registry_option NO_REGISTRY)
	endif()

	sw_glob_source_tree(list_game_source RECURSE "*.cpp" "*.c" "*.h" "*.hpp")
	# 게임의 에디터 확장(Editor/)은 따로 짓는 모듈이다(SWGameEditor).
	list(FILTER list_game_source EXCLUDE REGEX "/Editor/")
	# 게임의 클라이언트 전용 소스(Client/ — 그리는 벤치 · UI 시연)는 전용 서버가 짓지 않는다(cmake/Engine/ExcludedSources.cmake).
	# 게임의 서버 전용 소스(Server/)는 클라이언트 구성이 짓지 않는다.
	if(SW_TARGET_TYPE STREQUAL "Server")
		sw_remove_excluded_sources(list_game_source ${SW_CLIENT_ONLY_SOURCE_PATTERNS})
	elseif(SW_TARGET_TYPE STREQUAL "Client")
		sw_remove_excluded_sources(list_game_source ${SW_SERVER_ONLY_SOURCE_PATTERNS})
	endif()
	sw_add_module_library(${target_name}
		KIND game
		DEV_TYPE MODULE
		EXPORTS MODULE
		LOG_TAG "Game"
		FOLDER "Source/Games"
		SOURCES ${list_game_source}
		LINK_PRIVATE Engine ModuleAPI GameFramework ${list_kit}
		REFLECTION_EXCLUDE "/Editor/"
		MODULE_NAME ${MODULE_NAME}
		CONTENT_OWNER "game:${game_name}"
		${list_registry_option}
	)

	if(active_game)
		set_property(GLOBAL PROPERTY SW_ACTIVE_GAME_TARGET ${target_name})
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Editor/CMakeLists.txt")
			add_subdirectory(Editor)
		endif()
		return()
	endif()

	set(output_dir "${CMAKE_BINARY_DIR}/Bin/${SW_DIR_BIN_GAMES}/${game_name}")
	set(symbol_dir "${CMAKE_BINARY_DIR}/Bin/Symbols/${SW_DIR_BIN_GAMES}/${game_name}")
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_NAME}.module.json" "${output_dir}/${MODULE_NAME}.module.json" COPYONLY)
	set_target_properties(${target_name} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${output_dir}$<0:>"
		LIBRARY_OUTPUT_DIRECTORY "${output_dir}$<0:>"
		PDB_OUTPUT_DIRECTORY "${symbol_dir}"
		PDB_OUTPUT_DIRECTORY_DEBUG "${symbol_dir}"
		PDB_OUTPUT_DIRECTORY_RELEASE "${symbol_dir}"
	)
	sw_add_module_engine_stamp(${target_name} MODULE_NAME ${MODULE_NAME})
	set_property(GLOBAL APPEND PROPERTY SW_GAME_PACK_TARGETS ${target_name})
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Editor")
		sw_declare_excluded_directory("${CMAKE_CURRENT_SOURCE_DIR}/Editor")
	endif()
endfunction()

# ------------------------------------------------------------------------------
# 에디터 확장 모듈(GF_EditorPlugin_<이름> · SWGameEditor) — Dev 전용 SHARED. EditorModule 을 링크하고(언리얼 <X>Editor 모듈이 UnrealEd 를 링크하는 것과 같다),
# 매니페스트의 의존 가운데 키트 · GameFramework 를 링크한다(목록을 CMake 에 다시 적지 않는다 — 매니페스트가 원본). ImGui 결속 소스를 만들어 넣는다
# (손으로 빠뜨릴 수 없게 — `cmake/Engine/EditorExtensionUIBinder.cpp.in`). 다시 로드되는 DLL(EditorModule · 키트)도 일반 import 다(모듈 호스트가 이름을 바꾼다).
#
#   PANEL_VIEW_SOURCES  MVVM 패널 View 파일(이 폴더 기준). 따로 부분(`<TARGET>PanelView`)으로 짓고 에디터 패널 View 와 같은 모델 · 씬 헤더 껍데기를 받는다
#                       (Source/Editor/CMakeLists.txt 2-2 — 전역 속성 SW_EDITOR_PANEL_VIEW_STUB_ROOT).
# ------------------------------------------------------------------------------
function(sw_add_editor_extension TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "PANEL_VIEW_SOURCES" ${ARGN})
	sw_glob_source_tree(list_extension_source RECURSE "*.cpp" "*.h")
	set(list_panel_view_source "")
	foreach(view_source IN LISTS ARG_PANEL_VIEW_SOURCES)
		set(view_path "${CMAKE_CURRENT_SOURCE_DIR}/${view_source}")
		if(NOT view_path IN_LIST list_extension_source)
			message(FATAL_ERROR "sw_add_editor_extension(${TARGET_NAME}): PANEL_VIEW_SOURCES '${view_source}' is not a source in ${CMAKE_CURRENT_SOURCE_DIR}")
		endif()
		list(APPEND list_panel_view_source "${view_path}")
	endforeach()
	if(SW_SHIPPING_BUILD)
		sw_declare_excluded_sources(${list_extension_source})
		return()
	endif()
	sw_skip_inactive_module(${TARGET_NAME} skip)
	if(skip)
		return()
	endif()

	set(SW_EDITOR_EXTENSION_NAME ${TARGET_NAME})
	set(binder_source "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}UIBinder.cpp")
	configure_file("${CMAKE_SOURCE_DIR}/cmake/Engine/EditorExtensionUIBinder.cpp.in" "${binder_source}" @ONLY)

	set(list_linked_module "")
	get_property(list_dependency GLOBAL PROPERTY SW_MODULE_${TARGET_NAME}_DEPENDENCIES)
	foreach(dependency IN LISTS list_dependency)
		get_property(dependency_kind GLOBAL PROPERTY SW_MODULE_${dependency}_KIND)
		if(dependency_kind STREQUAL "Kit" OR dependency_kind STREQUAL "GameFramework")
			list(APPEND list_linked_module ${dependency})
		endif()
	endforeach()

	if(list_panel_view_source)
		list(REMOVE_ITEM list_extension_source ${list_panel_view_source}) # View 는 껍데기를 받는 부분이 짓는다(아래)
	endif()
	sw_add_module_library(${TARGET_NAME}
		KIND editorextension
		DEV_TYPE SHARED
		EXPORTS MODULE_SHARED
		LOG_TAG "${TARGET_NAME}"
		FOLDER "Source/EditorExtensions"
		SOURCES ${list_extension_source} "${binder_source}"
		LINK_PRIVATE EditorModule Engine ModuleAPI ${list_linked_module} imgui implot imgui_nodeeditor
	)
	sw_apply_editor_input_ban(${TARGET_NAME}) # 확장 패널도 에디터 코드다 — ImGui 입력은 입력 맵 · EditorInputGUI 로
	if(list_panel_view_source)
		get_property(panel_view_stub_root GLOBAL PROPERTY SW_EDITOR_PANEL_VIEW_STUB_ROOT)
		if(NOT panel_view_stub_root)
			message(FATAL_ERROR "sw_add_editor_extension(${TARGET_NAME}): SW_EDITOR_PANEL_VIEW_STUB_ROOT is not set - Source/Editor must be configured first")
		endif()
		sw_add_module_part(${TARGET_NAME}PanelView
			OWNER ${TARGET_NAME}
			SOURCES ${list_panel_view_source}
			LINK EditorModule Engine ModuleAPI ${list_linked_module} imgui implot imgui_nodeeditor
		)
		target_include_directories(${TARGET_NAME}PanelView BEFORE PRIVATE "${panel_view_stub_root}")
		sw_apply_editor_input_ban(${TARGET_NAME}PanelView)
	endif()
	add_dependencies(EditorAll ${TARGET_NAME})
endfunction()

# ------------------------------------------------------------------------------
# sw_apply_editor_input_ban — 에디터 코드 타깃에 `Source/Editor/Common/Input/EditorInputBan.h` 를 imgui.h 의 사용자 헤더로 붙이고 사용 중단을 오류로 올린다
#
# 그 헤더가 ImGui 입력 · 팝업 · 끌어 놓기 함수(imgui_internal.h 의 다른 겹 포함)를 [[deprecated]] 로 다시 선언한다(`docs/agents/EditorUI.md` 입력 절).
# 금지를 받지 않는 파일(ImGui 입력 어댑터 둘 · 노드 캔버스 지우기 입구 EditorNodeGraph.cpp · 입력 위젯을 감싸는 EditorWidgets.cpp)의 표시는 이 함수가 소스 속성으로만 준다
# (SW_EDITOR_INPUT_ADAPTER=1 · SW_EDITOR_NODE_GRAPH_ADAPTER=1 · SW_EDITOR_WIDGET_ADAPTER=1). 나머지 파일은 강제 포함한 EditorInputBanConfig.h 가 0 으로 정해 두므로 소스 안
# `#define` 은 다시 정의 오류(-Werror=macro-redefined)로 멈춘다 — 다른 파일이 매크로로 금지를 풀 수 없다. 소스 정의가 붙은 파일은 유니티 묶음에서 빠지고,
# 관문이 PCH 에 구워 둔 0 과 부딪히지 않게 PCH 도 쓰지 않는다. 옵션 SW_EDITOR_INPUT_BAN(Source/Editor/CMakeLists.txt, 기본 ON)이 꺼져 있으면 아무것도 하지 않는다.
# ------------------------------------------------------------------------------
function(sw_apply_editor_input_ban TARGET_NAME)
	if(NOT SW_EDITOR_INPUT_BAN)
		return()
	endif()
	target_compile_definitions(${TARGET_NAME} PRIVATE IMGUI_INCLUDE_IMGUI_USER_H
		"IMGUI_USER_H_FILENAME=\"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Input/EditorInputBan.h\"")
	if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
		target_compile_options(${TARGET_NAME} PRIVATE /we4996 /we4005)
	else()
		target_compile_options(${TARGET_NAME} PRIVATE -Werror=deprecated-declarations -Werror=macro-redefined)
	endif()
	set(guard_header "${CMAKE_SOURCE_DIR}/Source/Editor/Common/Input/EditorInputBanConfig.h")
	if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
		target_compile_options(${TARGET_NAME} PRIVATE "/FI${guard_header}")
	else()
		target_compile_options(${TARGET_NAME} PRIVATE "SHELL:-include ${guard_header}")
	endif()
	get_target_property(list_source ${TARGET_NAME} SOURCES)
	foreach(source IN LISTS list_source)
		set(adapter_mark "")
		if(source MATCHES "Common/Input/EditorInputGUI\\.cpp$|Common/Backend/ImGuiKeyForward\\.cpp$")
			set(adapter_mark "SW_EDITOR_INPUT_ADAPTER=1")
		elseif(source MATCHES "Common/Widgets/EditorNodeGraph\\.cpp$")
			set(adapter_mark "SW_EDITOR_NODE_GRAPH_ADAPTER=1")
		elseif(source MATCHES "Common/Widgets/EditorWidgets\.cpp$")
			set(adapter_mark "SW_EDITOR_WIDGET_ADAPTER=1")
		endif()
		if(adapter_mark)
			set_property(SOURCE ${source} TARGET_DIRECTORY ${TARGET_NAME} APPEND PROPERTY COMPILE_DEFINITIONS "${adapter_mark}")
			set_source_files_properties(${source} TARGET_DIRECTORY ${TARGET_NAME} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON SKIP_PRECOMPILE_HEADERS ON)
		endif()
	endforeach()
endfunction()
