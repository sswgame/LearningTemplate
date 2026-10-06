# ==============================================================================
# @file cmake/Engine/BuildLayout.cmake
# @brief 산출물이 어디에 놓이나 — 출력 경로, 전역 옵션 타겟, IPO, 런타임 복사 큐
# ==============================================================================

# 이 파일은 함수만 정의하지 않는다. include 되는 순간 출력 경로와 sw_global_options 가 정해진다.
# 그래서 타깃 규칙(UnbuiltSources · ModuleTargets · TestTargets)보다 먼저 include 해야 한다.

# ------------------------------------------------------------------------------
# 플랫폼 · 구성 이름 — 매니페스트(_listPlatform · _listConfiguration) · 모듈 해석 · 배포 백엔드 확인이 같은 낱말을 쓴다(런타임 ModuleCatalog 와 같은 표).
# ------------------------------------------------------------------------------
if(WIN32)
	set(sw_platform_name "Windows")
else()
	set(sw_platform_name "Linux")
endif()
if(SW_SHIPPING_BUILD)
	set(sw_configuration_name "Shipping")
else()
	set(sw_configuration_name "Dev")
endif()

# ------------------------------------------------------------------------------
# 출력 경로 — 산출물은 구성과 무관하게 `Bin/` · `Lib/` 에 놓인다(LiveReload · 시험의 작업 폴더가 이 경로를 안다).
# ------------------------------------------------------------------------------
# `$<0:>`(빈 생성기 식)는 다중 구성 생성기가 구성 하위 폴더를 붙이지 않게 한다 — 구성마다 `*_DEBUG` · `*_RELEASE` 를 따로 적지 않는다.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin$<0:>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Lib$<0:>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Lib$<0:>")
# Shipping 의 링크 PDB 는 배포 폴더(Bin) 밖 `Symbols/` 에 둔다 — 심볼 저장소에는 `py -3 -m Scripts symbols` 가 넣는다. 시험 실행 파일은
# TestBin 옆에 둔다(`sw_addTestExecutable` — 크래시 스택이 이름을 낸다). Dev(Release 포함)는 실행 파일 옆(디버거 · 핫 리로드가 그 자리에서 찾는다).
if(SW_SHIPPING_BUILD)
	set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Symbols")
	set(CMAKE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Symbols")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/Resource")
	install(DIRECTORY "${CMAKE_SOURCE_DIR}/Resource" DESTINATION .)
endif()

# ------------------------------------------------------------------------------
# Dev / Shipping 레이아웃 및 글로벌 옵션 (Modern CMake INTERFACE)
# ------------------------------------------------------------------------------
add_library(sw_global_options INTERFACE)

# 컴파일 플래그 모듈(Architecture/Platform/Compiler/BuildType/Options)이 만든 INTERFACE 타겟을
# 여기서 한 번 흡수한다. sw_flag_libraries 는 비어 있을 수 있는 **리스트 변수**라 소비자마다 가드와 함께
# 다시 걸어야 하지만, 타겟 하나로 묶으면 sw_global_options 만 걸면 되고 새 플래그 모듈이 늘어도 소비자는 고칠 게 없다.
if(sw_flag_libraries)
	target_link_libraries(sw_global_options INTERFACE ${sw_flag_libraries})
endif()

# 빌드 구성 이름(sw::build::kConfigName, Core/Common/BuildInfo.h) — Shipping 이면 "Shipping", 아니면 실제 구성 이름.
# 생성기 식이라 다중 구성 생성기에서도 그 구성의 이름이 된다.
target_compile_definitions(sw_global_options INTERFACE "SW_BUILD_CONFIG_NAME=\"$<IF:$<BOOL:${SW_SHIPPING_BUILD}>,Shipping,$<CONFIG>>\"")

# 빌드 타깃 종류(SW_TARGET_TYPE) — 코드는 이 둘과 `sw::build::kTargetName` 만 읽는다(`Core/Common/BuildInfo.h` · `TargetMacroCheck.h`).
#   SW_WITH_CLIENT_CODE : Game · Client — 창 · 렌더 · 입력 장치 · 오디오 장치 코드
#   SW_WITH_SERVER_CODE : Game · Server — 서버 코드
# `#if defined( SW_WITH_*_CODE )` 는 .cpp 본문에서만 쓴다. 리플렉션 선언 · 헤더의 클래스 모양을 가르지 않는다 — 나뉘는 코드는 모듈(`_listTarget`)로 나눈다.
target_compile_definitions(sw_global_options INTERFACE "SW_TARGET_NAME=\"${SW_TARGET_TYPE}\"")
if(NOT SW_TARGET_TYPE STREQUAL "Server")
	target_compile_definitions(sw_global_options INTERFACE SW_WITH_CLIENT_CODE)
endif()
if(NOT SW_TARGET_TYPE STREQUAL "Client")
	target_compile_definitions(sw_global_options INTERFACE SW_WITH_SERVER_CODE)
endif()
message(STATUS "[BuildConfig] Target type: ${SW_TARGET_TYPE}")

if(SW_SHIPPING_BUILD)
	target_compile_definitions(sw_global_options INTERFACE SW_SHIPPING)
	message(STATUS "[BuildConfig] Shipping: Engine/SWGame STATIC, RHI backend linked into Engine, Editor off (SW_SHIPPING_BUILD=ON)")
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
# IPO — 전역 CMAKE_INTERPROCEDURAL_OPTIMIZATION (Release)
# ------------------------------------------------------------------------------
set(sw_ipo_supported FALSE)

# `SW_ENABLE_LTO` 가 여기도 걸린다 — 이 전역 IPO 와 Shipping LTO 는 한 스위치다(빼면 OFF 를 줘도 Release 가 LTO 로 간다).
if(SW_ENABLE_LTO AND (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel"))
	sw_checkIpoSupport(sw_ipo_supported sw_ipo_error)

	if(sw_ipo_supported)
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
		message(STATUS "[BuildConfig] IPO enabled globally (opt-out for Tools/Tests/Editor)")
	elseif(sw_ipo_error)
		message(STATUS "[BuildConfig] IPO unsupported: ${sw_ipo_error}")
	endif()
endif()


# ------------------------------------------------------------------------------
# 런타임 파일 복사 큐 — POST_BUILD는 sw_emitRuntimeCopies가 한 번에 방출
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

# 실행 파일 옆에 놓을 런타임 DLL — 구성마다 실제로 쓰는 것만(무조건 복사하면 배포 산출물에 셰이더 컴파일러 · 검증 레이어가 따라 들어간다).
#   DXC        : 개발 빌드(런타임 HLSL 컴파일 · DXIL 리플렉션). ALWAYS_DXC 면 배포 구성에서도(시험 — TestBin 이라 배포물과 섞이지 않는다)
#   검증 레이어 : Debug 만(VulkanRHIDevice 가 SW_DEBUG 에서만 켠다 — 딸려 오는 mimalloc 도 그 의존성일 뿐이다)
#   Tracy      : 개발 빌드 · Windows · SW_ENABLE_TRACY (함수 안에서 거른다)
function(sw_deployRuntimeDependencies TARGET_NAME)
	cmake_parse_arguments(ARG "ALWAYS_DXC" "" "" ${ARGN})
	if(ARG_ALWAYS_DXC OR NOT SW_SHIPPING_BUILD)
		sw_copyDxcDlls(${TARGET_NAME})
	endif()
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		sw_copyVulkanValidationRuntime(${TARGET_NAME})
	endif()
	sw_copyTracyRuntime(${TARGET_NAME})
	sw_emitRuntimeCopies(${TARGET_NAME})
endfunction()
