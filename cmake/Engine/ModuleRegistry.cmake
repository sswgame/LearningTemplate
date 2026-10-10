# ==============================================================================
# @file cmake/Engine/ModuleRegistry.cmake
# @brief 동적 모듈 레지스트리 — 등록(ABI 도장 · 서버 전용 표식을 같이 단다) · 종류별 조회 · 빌드 순서 · 빠진 등록 대조
# ==============================================================================

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
# 서버 전용 모듈(매니페스트 `_listTarget` 이 Server 뿐)에 표식 글 `sw-server-only-module:<이름>` 을 박는다 — 클라이언트 산출물에 그 글이 없음을
# `BuildTargetImageTest` 가 바이트로 확인한다. 정적 초기화 객체가 글을 읽어(volatile) 링커가 Shipping 정적 링크 · 섹션 GC 에서 버리지 못한다
# (Shipping 은 모듈을 통째로 링크한다 — `sw_linkWholeArchive`). 서버 실행 파일 자신의 표식은 `Source/Server/ServerApp.cpp` 에 있다.
function(sw_addServerOnlyMarker TARGET_NAME)
	get_property(swTargets GLOBAL PROPERTY SW_MODULE_${TARGET_NAME}_TARGETS)
	if(NOT swTargets STREQUAL "Server")
		return()
	endif()
	set(swMarkerSource "${CMAKE_BINARY_DIR}/generated/moduletarget/${TARGET_NAME}ServerOnlyMarker.cpp")
	file(CONFIGURE OUTPUT "${swMarkerSource}" CONTENT
"// 생성 파일 - sw_addServerOnlyMarker (cmake/Engine/ModuleRegistry.cmake). 고치지 마십시오.
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
	target_sources(${TARGET_NAME} PRIVATE "${swMarkerSource}")
	set_source_files_properties("${swMarkerSource}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON SKIP_UNITY_BUILD_INCLUSION ON)
endfunction()

function(sw_registerDynamicModule TARGET_NAME KIND)
	# 모든 동적 모듈은 매니페스트를 갖는다(없으면 여기서 구성이 선다) — App 이 그것으로 적재 순서를 정한다.
	sw_isModuleActive(${TARGET_NAME} swModuleActive)
	if(NOT swModuleActive)
		message(FATAL_ERROR "[Module] ${TARGET_NAME} is registered but its manifest says it is off — the creating function must skip it (sw_skipInactiveModule)")
	endif()
	set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES ${TARGET_NAME})
	set_property(GLOBAL APPEND PROPERTY SW_DYNAMIC_MODULES_${KIND} ${TARGET_NAME})
	sw_addModuleEngineStamp(${TARGET_NAME})
	sw_addServerOnlyMarker(${TARGET_NAME})
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

# 레지스트리의 동적 모듈(KINDS 로 고른 것, 생략하면 전부)이 TARGET_NAME 보다 먼저 지어지게 한다 — 런타임에 올리는 것은 링크로 이어지지 않는다.
function(sw_addDynamicModuleDependencies TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "KINDS" ${ARGN})
	sw_getDynamicModules(listModule KINDS ${ARG_KINDS})
	if(listModule)
		add_dependencies(${TARGET_NAME} ${listModule})
	endif()
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
			"  (종류: rhi | kit | game | gameframework | editor — cmake/Engine/ModuleRegistry.cmake)")
	endif()
endfunction()
