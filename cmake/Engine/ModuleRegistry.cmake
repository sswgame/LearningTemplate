# ==============================================================================
# @file cmake/Engine/ModuleRegistry.cmake
# @brief 동적 모듈 레지스트리 — 등록(ABI 스탬프 · 서버 전용 표식을 같이 단다) · 종류별 조회 · 빌드 순서 · 빠진 등록 대조
# ==============================================================================

# ------------------------------------------------------------------------------
# 동적 모듈 레지스트리 — **만드는 자리가 등록하고, 쓰는 자리는 묻는다**
#
# 등록을 호출부에 맡기거나 소비자가 이름을 리터럴로 들면 새 모듈이 조용히 빠진다. 그래서 등록은
# **타겟을 만드는 함수 안에서만** 한다. 종류(`KIND`)를 같이 받아 두면 소비자가 필요한 것만 고를 수
# 있다 — App 은 전부, 다른 소비자는 `rhi` 같은 한 종류만.
#
# 이 저장소의 `Scripts/lint/` 와 같은 규칙이다: **목록이 아니라 자리가 규칙이다.**
#
#   KIND: rhi | kit | game | gameframework | editor
# ------------------------------------------------------------------------------
# 서버 전용 모듈(매니페스트 `_listTarget` 이 Server 뿐)에 표식 글 `sw-server-only-module:<이름>` 을 박는다 — 클라이언트 산출물에 그 글이 없음을
# `BuildTargetImageTest` 가 바이트로 확인한다. 정적 초기화 객체가 글을 읽어(volatile) 링커가 Shipping 정적 링크 · 섹션 GC 에서 버리지 못한다
# (Shipping 은 모듈을 통째로 링크한다 — `sw_link_whole_archive`). 서버 실행 파일 자신의 표식은 `Source/Server/ServerApp.cpp` 에 있다.
function(sw_add_server_only_marker TARGET_NAME MODULE_NAME)
	get_property(targets GLOBAL PROPERTY SW_MODULE_${MODULE_NAME}_TARGETS)
	if(NOT targets STREQUAL "Server")
		return()
	endif()
	# 파일 이름에 타깃 이름을 넣지 않는다 — 빌드 폴더 밖 소스의 오브젝트 경로는 "<타깃 폴더>/<해시>/<파일 이름>.obj" 라
	# 긴 타깃(`GF_Server_ServerDirectory`)과 긴 프리셋 이름이 CMAKE_OBJECT_PATH_MAX(250)를 넘겼다.
	set(marker_source "${CMAKE_BINARY_DIR}/generated/moduletarget/${TARGET_NAME}/ServerOnlyMarker.cpp")
	file(CONFIGURE OUTPUT "${marker_source}" CONTENT
"// 생성 파일 - sw_add_server_only_marker (cmake/Engine/ModuleRegistry.cmake). 고치지 마십시오.
// 서버 전용 모듈의 표식입니다. 클라이언트 산출물에 이 글이 있으면 서버 코드가 새어 든 것입니다(BuildTargetImageTest).
namespace
{
    struct ServerOnlyMarker_@TARGET_NAME@
    {
        ServerOnlyMarker_@TARGET_NAME@()
        {
            static const char kMarker[] = \"sw-server-only-module:@TARGET_NAME@\";
            const volatile char* pMarker = kMarker;
            (void)pMarker[0];
        }
    };
    const ServerOnlyMarker_@TARGET_NAME@ s_serverOnlyMarker_@TARGET_NAME@{};
}
" @ONLY)
	target_sources(${TARGET_NAME} PRIVATE "${marker_source}")
	set_source_files_properties("${marker_source}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON SKIP_UNITY_BUILD_INCLUSION ON)
endfunction()

# MODULE_NAME 은 매니페스트 · ABI 스탬프의 모듈 이름이다(기본 타깃 이름 — 게임은 타깃 `SWGame_<게임>`, 모듈 `SWGame`). 레지스트리에는 타깃 이름이 든다.
function(sw_register_dynamic_module TARGET_NAME KIND)
	cmake_parse_arguments(ARG "" "MODULE_NAME" "" ${ARGN})
	set(module_name ${TARGET_NAME})
	if(ARG_MODULE_NAME)
		set(module_name ${ARG_MODULE_NAME})
	endif()
	# 모든 동적 모듈은 매니페스트를 갖는다(없으면 여기서 구성이 선다) — App 이 그것으로 적재 순서를 정한다.
	sw_is_module_active(${module_name} module_active)
	if(NOT module_active)
		message(FATAL_ERROR "[Module] ${TARGET_NAME} is registered but its manifest says it is off — the creating function must skip it (sw_skip_inactive_module)")
	endif()
	set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES ${TARGET_NAME})
	set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES_${KIND} ${TARGET_NAME})
	sw_add_module_engine_stamp(${TARGET_NAME} MODULE_NAME ${module_name})
	sw_add_server_only_marker(${TARGET_NAME} ${module_name})
endfunction()

# 등록된 동적 모듈 중 **실제로 타겟이 있는 것**을 OUT_VAR 에 담습니다.
#
# `KINDS` 를 주면 그 종류만, 생략하면 전부. 타겟이 없는 이름은 거른다 — 배포 빌드는 RHI 를
# Engine 에 정적 링크하므로 등록만 되고 타겟은 없는 상태가 정상이다.
function(sw_get_dynamic_modules OUT_VAR)
	cmake_parse_arguments(ARG "" "" "KINDS" ${ARGN})

	if(ARG_KINDS)
		set(list_registered "")
		foreach(kind IN LISTS ARG_KINDS)
			get_property(list_of_kind GLOBAL PROPERTY SW_DYNAMIC_MODULES_${kind})
			list(APPEND list_registered ${list_of_kind})
		endforeach()
	else()
		get_property(list_registered GLOBAL PROPERTY SW_DYNAMIC_MODULES)
	endif()

	set(list_module "")
	foreach(mod IN LISTS list_registered)
		if(TARGET ${mod})
			list(APPEND list_module ${mod})
		endif()
	endforeach()

	if(list_module)
		list(REMOVE_DUPLICATES list_module)
	endif()

	set(${OUT_VAR} "${list_module}" PARENT_SCOPE)
endfunction()

# 레지스트리의 동적 모듈(KINDS 로 고른 것, 생략하면 전부)이 TARGET_NAME 보다 먼저 지어지게 한다 — 런타임에 올리는 것은 링크로 이어지지 않는다.
function(sw_add_dynamic_module_dependencies TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "KINDS" ${ARGN})
	sw_get_dynamic_modules(list_module KINDS ${ARG_KINDS})
	if(list_module)
		add_dependencies(${TARGET_NAME} ${list_module})
	endif()
endfunction()

# 모듈 빌드 성공 스탬프 — 레지스트리의 동적 모듈이 **모두** 지어진 뒤에만 `Bin/Modules/${SW_FILE_MODULE_BUILD_STAMP}` 를 다시 쓴다.
#
# 핫 리로드(`LiveReloadManager`)가 바깥 빌드(터미널 · IDE 의 `cmake --build`)의 모듈 변경을 올리는 신호다. DLL 의 mtime 만 보면 연쇄 빌드가
# 모듈을 하나씩 다시 쓰는 도중의 반쯤 링크된 집합을 올린다. 이 타깃은 모든 모듈에 의존하므로 ninja 가 그것들을 다 링크한 뒤에만 돌고,
# 링크가 하나라도 실패하면 돌지 않는다. 언리얼 Live Coding · 유니티 도메인 리로드처럼 "빌드가 끝난 뒤"에 올리는 규칙을 바깥 빌드에도 준다.
# 스탬프는 모듈 산출물에 파일 단위로 의존한다(`DEPENDS` 에 타깃 이름) — 다시 링크된 모듈이 있을 때만 새로 쓰이고, 할 일 없는 빌드는 그대로 "no work" 다.
# 리로드 쪽은 스탬프가 바뀌면 모듈마다 mtime 을 견줘 바뀐 것만 올린다.
# 배포 빌드는 모듈을 정적으로 링크해 동적 모듈 타깃이 없으므로 만들지 않는다.
function(sw_add_module_build_stamp)
	sw_get_dynamic_modules(list_module)
	if(NOT list_module)
		return()
	endif()
	set(stamp_path "${CMAKE_BINARY_DIR}/Bin/Modules/${SW_FILE_MODULE_BUILD_STAMP}")
	add_custom_command(OUTPUT "${stamp_path}"
		COMMAND "${CMAKE_COMMAND}" "-DSTAMP_PATH=${stamp_path}" -P "${CMAKE_SOURCE_DIR}/cmake/Engine/WriteModuleBuildStamp.cmake"
		DEPENDS ${list_module}
		COMMENT "Module build stamp (live reload signal)"
		VERBATIM
	)
	add_custom_target(ModuleBuildStamp ALL DEPENDS "${stamp_path}")
	set_target_properties(ModuleBuildStamp PROPERTIES FOLDER "Engine/Modules")
endfunction()

# 등록을 **잊을 수 없게** 한다 — 구성 마지막에 한 번 대조합니다.
#
# 레지스트리는 규칙이지 강제가 아니다. `EditorModule` 은 실제로 아무 데도 등록되지 않은 채
# 오래 있었고, 아무 에러도 나지 않았다(소비하는 자리가 이름을 리터럴로 들고 있었으니까).
# 그래서 여기서 **런타임에 로드되는 타겟(MODULE)** 을 전부 훑어 레지스트리와 맞춰 본다.
# MODULE 은 정의상 "이름으로 찾아 올리는 플러그인" 이라 App 이 반드시 먼저 빌드해야 하는 것들이다.
#
# 빠진 것이 있으면 **구성이 선다.** 조용히 빠지는 것보다 낫다.
function(sw_verify_dynamic_module_registry)
	get_property(list_registered GLOBAL PROPERTY SW_DYNAMIC_MODULES)
	# 함께 짓는 게임 팩(`sw_add_game_module`, 활성 게임이 아닌 것)은 App 이 `Bin/Modules` 에서 올리지 않으므로 레지스트리 밖이다.
	get_property(list_game_pack_target GLOBAL PROPERTY SW_GAME_PACK_TARGETS)
	list(APPEND list_registered ${list_game_pack_target})

	# 루트부터 훑는다. `Source/` 에서 시작하면 안 된다 — `Source/Editor` · `Source/Engine` 등은
	# `Source/CMakeLists.txt` 가 아니라 **루트가** 직접 add_subdirectory 하므로 `Source/` 의
	# SUBDIRECTORIES 에 없다. (처음에 그렇게 짰다가 EditorModule 을 못 잡는 것을 확인했다.)
	set(list_directory "${CMAKE_SOURCE_DIR}")
	set(list_missing "")

	while(list_directory)
		list(POP_FRONT list_directory current_dir)

		get_property(list_sub_dir DIRECTORY "${current_dir}" PROPERTY SUBDIRECTORIES)
		list(APPEND list_directory ${list_sub_dir})

		get_property(list_target DIRECTORY "${current_dir}" PROPERTY BUILDSYSTEM_TARGETS)
		foreach(target_name IN LISTS list_target)
			get_target_property(target_type ${target_name} TYPE)
			if(NOT target_type STREQUAL "MODULE_LIBRARY")
				continue()
			endif()

			if(NOT target_name IN_LIST list_registered)
				list(APPEND list_missing ${target_name})
			endif()
		endforeach()
	endwhile()

	if(list_missing)
		message(FATAL_ERROR
			"동적 모듈이 레지스트리에 없습니다: ${list_missing}
"
			"  타겟을 만드는 자리에서 sw_register_dynamic_module(<타겟> <종류>) 를 부르세요.
"
			"  (종류: rhi | kit | game | gameframework | editor — cmake/Engine/ModuleRegistry.cmake)")
	endif()
endfunction()
