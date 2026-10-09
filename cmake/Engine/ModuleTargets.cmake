# ==============================================================================
# @file cmake/Engine/ModuleTargets.cmake
# @brief 모듈 · 실행 파일 타깃을 어떻게 만드나 — 내보내기 · PCH · Bin 출력 · 통째 링크 · 동적 모듈 레지스트리 · ABI 도장 · 모듈 팩토리 · 지연 로드 · 결정성 TU · 심볼 분리
# ==============================================================================

# ------------------------------------------------------------------------------
# 내보내기 매크로 — 종류(KIND)마다 짝이 정해져 있다. STATIC(배포)에는 아무것도 붙이지 않는다.
#   ENGINE : SHARED 면 SW_EXPORTS(PRIVATE) · SW_IMPORTS(INTERFACE)        — Engine.dll
#   GF     : SHARED 면 SW_GF_EXPORTS(PRIVATE) · SW_GF_IMPORTS(INTERFACE)  — GameFramework.dll · 키트
#   MODULE : MODULE 이면 SW_MODULE_EXPORTS(PRIVATE)                       — C-ABI 진입점을 내보내는 플러그인(RHI · 게임 · 에디터)
# ------------------------------------------------------------------------------
function(sw_configureDllExports TARGET_NAME LIB_TYPE KIND)
	if(NOT KIND MATCHES "^(ENGINE|GF|MODULE)$")
		message(FATAL_ERROR "sw_configureDllExports(${TARGET_NAME}): KIND 는 ENGINE | GF | MODULE 이다 (받은 값: ${KIND})")
	endif()

	if(KIND STREQUAL "ENGINE" AND LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_EXPORTS INTERFACE SW_IMPORTS)
	elseif(KIND STREQUAL "GF" AND LIB_TYPE STREQUAL "SHARED")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_GF_EXPORTS INTERFACE SW_GF_IMPORTS)
	elseif(KIND STREQUAL "MODULE" AND LIB_TYPE STREQUAL "MODULE")
		target_compile_definitions(${TARGET_NAME} PRIVATE SW_MODULE_EXPORTS)
	endif()
endfunction()

# SW_ENABLE_PCH 가 켜져 있을 때만 PCH 를 건다.
function(sw_configurePch targetName headerPath)
	if(SW_ENABLE_PCH)
		target_precompile_headers(${targetName} PRIVATE "${headerPath}")
	endif()
endfunction()

# 모듈 · 핫 리로드 타깃의 런타임 출력 — 모듈 DLL(rhi · kit · game · editor)은 `Bin/Modules/`, 모두가 링크하는 GameFramework 는 `Bin/` 이다.
# 런타임은 `ModuleImageUtil::findModuleLibraryPath` 로 같은 순서(Modules → Bin)로 찾는다. SHARED/MODULE 은 플랫폼에 따라 LIBRARY 출력(Lib/)으로
# 갈 수 있어 둘 다 정한다. `$<0:>` 는 BuildLayout 과 같다.
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

		# 링크 옵션만 걸면 그 라이브러리의 링크 의존(키트가 PRIVATE 로 링크한 서드파티 — GF_SqlStore 의 sqlite3 · GF_Server_SqlStore 의
		# libpq)이 따라오지 않는다. 타깃으로도 링크해 전이 의존을 링크 줄에 올린다(같은 아카이브가 한 번 더 나와도 이미 올라온 멤버는 다시 올리지 않는다).
		target_link_libraries(${TARGET_NAME} PRIVATE ${reflLib})
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
"// 생성 파일 - sw_addServerOnlyMarker (cmake/Engine/TargetRules.cmake). 고치지 마십시오.
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
			"  (종류: rhi | kit | game | gameframework | editor — cmake/Engine/TargetRules.cmake)")
	endif()
endfunction()

# App의 런타임/플러그인/모듈 의존성을 구성합니다.
function(sw_configureAppDependencies TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		return()
	endif()

	# 1) 동적 모듈은 App 보다 먼저 빌드되어야 한다 — App 이 런타임에 로드하기 때문이다. 이름을 적지 않는다: 레지스트리가 답한다.
	sw_addDynamicModuleDependencies(${TARGET_NAME})
	sw_getDynamicModules(listDynamicModule)

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

		# 쿠킹은 쿠커 실행 파일(App, 서버 타깃은 Server — SW_COOK_HOST_TARGET) 뒤에만 건다.
		if(TARGET CookAssets AND TARGET_NAME STREQUAL SW_COOK_HOST_TARGET)
			add_dependencies(CookAssets ${TARGET_NAME})
			set_target_properties(CookAssets PROPERTIES EXCLUDE_FROM_ALL FALSE)
		endif()
	endif()
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

	add_library(${TARGET_NAME} ${libType} ${ARG_SOURCES})
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
	set(symbolsDir "${CMAKE_BINARY_DIR}/Symbols")
	add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${symbolsDir}"
		COMMAND ${CMAKE_OBJCOPY} --only-keep-debug "$<TARGET_FILE:${TARGET_NAME}>" "${symbolsDir}/$<TARGET_FILE_NAME:${TARGET_NAME}>.debug"
		COMMAND ${CMAKE_OBJCOPY} --strip-debug "--add-gnu-debuglink=${symbolsDir}/$<TARGET_FILE_NAME:${TARGET_NAME}>.debug" "$<TARGET_FILE:${TARGET_NAME}>"
		COMMENT "Splitting debug info of ${TARGET_NAME} into Symbols/"
		VERBATIM)
endfunction()

# ------------------------------------------------------------------------------
# sw_markDeterministicSources — 같은 입력이면 구성 · 컴파일러 · 플랫폼을 넘어 같은 비트를 내야 하는 시뮬레이션 TU(파괴 · 파쇄 · 그 복제)에
#   부동소수점 축약(FMA 합치기)을 끈다. Release 의 /arch:AVX2 · -mavx2 는 `a * b + c` 를 FMA 한 번으로 합쳐 반올림이 Debug(/Od · AVX2 없음)와
#   달라진다 — 씨앗과 사건만 보내고 받는 쪽이 같은 계산을 하는 파괴 네트워킹은 Debug 서버 ↔ Shipping 클라이언트에서 다른 그림이 된다.
#   엔진 전체에는 걸지 않는다(렌더 · 애니메이션 수학은 FMA 가 이득이고 비트 결정성이 필요 없다). 언리얼도 결정성이 필요한 물리 경로만 /fp:precise 로 짓는다.
#   clang-cl 의 기본 /fp:precise 는 축약을 허락하므로(-ffp-contract=on) 축약만 따로 끈다.
#   유니티 묶음에서 뺀다 — 묶음 파일은 묶음의 옵션 하나로 컴파일되므로 파일별 옵션이 묶음 전체에 퍼지거나 빠진다.
#   PCH 는 그대로 쓴다(축약 옵션은 PCH 호환 검사 대상이 아니다).
# ------------------------------------------------------------------------------
function(sw_markDeterministicSources TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "sw_markDeterministicSources: no target ${TARGET_NAME}")
	endif()
	if(MSVC)
		set(contractOff "/clang:-ffp-contract=off")
	else()
		set(contractOff "-ffp-contract=off")
	endif()
	foreach(src IN LISTS ARGN)
		if(NOT IS_ABSOLUTE "${src}")
			set(src "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
		endif()
		# 없는 경로를 조용히 넘기면 파일을 옮겼을 때 결정성이 소리 없이 꺼진다.
		if(NOT EXISTS "${src}")
			message(FATAL_ERROR "sw_markDeterministicSources(${TARGET_NAME}): no such file -> ${src}")
		endif()
		set_source_files_properties("${src}" TARGET_DIRECTORY ${TARGET_NAME} PROPERTIES
			COMPILE_OPTIONS "${contractOff}"
			SKIP_UNITY_BUILD_INCLUSION ON)
	endforeach()
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

	# 훅 소스의 자리는 한 곳 — Engine 의 SW_DELAYLOAD_HOOK_SOURCE(Source/Engine/CMakeLists.txt)다. Engine 이 없는 구성에서 훅을 부탁하는
	# 타깃은 없다. 속성이 비거나 그 파일이 없으면 설정 실수다 — 손으로 적은 폴백 경로로 대신하지 않는다(그러면 옮긴 것을 아무도 모른다).
	set(swHookSrc "")
	if(TARGET Engine)
		get_property(swHookSrc TARGET Engine PROPERTY SW_DELAYLOAD_HOOK_SOURCE)
	endif()
	if(NOT swHookSrc)
		message(FATAL_ERROR "[sw_addDelayloadHook] Engine has no SW_DELAYLOAD_HOOK_SOURCE (${TARGET_NAME})")
	endif()
	if(NOT EXISTS "${swHookSrc}")
		message(FATAL_ERROR "[sw_addDelayloadHook] SW_DELAYLOAD_HOOK_SOURCE points at a file that does not exist: ${swHookSrc}")
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
