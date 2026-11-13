# ==============================================================================
# @file cmake/Engine/BuildLayout.cmake
# @brief 산출물이 어디에 놓이나 — 출력 경로, 전역 옵션 타겟, IPO, 런타임 복사 큐, `Bin` 의 옛 자리 산출물 지우기
# ==============================================================================

# 이 파일은 함수만 정의하지 않는다. include 되는 순간 출력 경로와 sw_global_options 가 정해진다.
# 그래서 타깃 규칙(ExcludedSources · TargetCompileRules · ModuleTargets · TestTargets …)보다 먼저 include 해야 한다.

# ------------------------------------------------------------------------------
# 플랫폼 · 구성 이름 — 매니페스트(_listExcludedPlatform · _listConfiguration) · 모듈 해석 · 배포 백엔드 확인이 같은 낱말을 쓴다(런타임 ModuleCatalog 와 같은 표).
# ------------------------------------------------------------------------------
if(WIN32)
	set(SW_PLATFORM_NAME "Windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(SW_PLATFORM_NAME "Linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
	set(SW_PLATFORM_NAME "Android")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
	set(SW_PLATFORM_NAME "Web")
else()
	message(FATAL_ERROR "[Platform] unknown CMAKE_SYSTEM_NAME '${CMAKE_SYSTEM_NAME}' (one of Windows, Linux, Android, Emscripten)")
endif()
if(SW_SHIPPING_BUILD)
	set(SW_CONFIGURATION_NAME "Shipping")
else()
	set(SW_CONFIGURATION_NAME "Dev")
endif()

# ------------------------------------------------------------------------------
# 출력 경로 — 산출물은 구성과 무관하게 `Bin/` · `Lib/` 에 놓인다(LiveReload · 테스트의 작업 폴더가 이 경로를 안다).
# ------------------------------------------------------------------------------
# `$<0:>`(빈 생성기 식)는 다중 구성 생성기가 구성 하위 폴더를 붙이지 않게 한다 — 구성마다 `*_DEBUG` · `*_RELEASE` 를 따로 적지 않는다.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin$<0:>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Lib$<0:>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Lib$<0:>")
# 링크 PDB 는 실행 파일 · DLL 옆에 두지 않는다. Shipping 은 배포 폴더(Bin) 밖 `Symbols/`(심볼 저장소에는 `py -3 -m Scripts symbols` 가 넣는다),
# Dev(Release 포함)는 `Bin/Symbols/` 다 — 디버거는 이미지에 박힌 PDB 절대 경로로 찾고, 핫 리로드는 `ModuleLibraryUtil::findBuiltDebugSymbolPath` 로
# 찾아 섀도 복사본 옆에 복사한다. 테스트 실행 파일은 TestBin 옆에 둔다(크래시 스택이 이름을 낸다).
# 모듈 DLL(GF_* · RHI_* · EditorModule · SWGame)은 `Bin/Modules/`(`sw_set_module_bin_output`), Engine · GameFramework · 서드파티 DLL 은 `Bin/` 이다.
if(SW_SHIPPING_BUILD)
	set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Symbols")
	set(CMAKE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Symbols")
elseif(WIN32)
	set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin/Symbols")
	set(CMAKE_PDB_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Bin/Symbols")
	set(CMAKE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Bin/Symbols")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/Content")
	install(DIRECTORY "${CMAKE_SOURCE_DIR}/Content" DESTINATION .)
endif()

# ------------------------------------------------------------------------------
# Dev / Shipping 레이아웃 및 글로벌 옵션 (Modern CMake INTERFACE)
# ------------------------------------------------------------------------------
add_library(sw_global_options INTERFACE)

# 컴파일 플래그 모듈(Architecture/Platform/Compiler/BuildType/Options)이 만든 INTERFACE 타겟을
# 여기서 한 번 흡수한다. SW_FLAG_LIBRARIES 는 비어 있을 수 있는 **리스트 변수**라 소비자마다 가드와 함께
# 다시 걸어야 하지만, 타겟 하나로 묶으면 sw_global_options 만 걸면 되고 새 플래그 모듈이 늘어도 소비자는 고칠 게 없다.
if(SW_FLAG_LIBRARIES)
	target_link_libraries(sw_global_options INTERFACE ${SW_FLAG_LIBRARIES})
endif()

# 빌드 구성 이름(sw::build::kConfigName, Core/Foundation/BuildInfo.h) — Shipping 이면 "Shipping", 아니면 실제 구성 이름.
# 생성기 식이라 다중 구성 생성기에서도 그 구성의 이름이 된다.
target_compile_definitions(sw_global_options INTERFACE "SW_BUILD_CONFIG_NAME=\"$<IF:$<BOOL:${SW_SHIPPING_BUILD}>,Shipping,$<CONFIG>>\"")

# 빌드 타깃 종류(SW_TARGET_TYPE) — 코드는 이름 글(`SW_TARGET_NAME`)에서 만든 상수 `sw::build::kTargetName` · `kWithClientCode` · `kWithServerCode` 만 읽는다
# (`Core/Foundation/BuildInfo.h`). 전처리 갈래용 매크로(예전 `SW_WITH_CLIENT_CODE` · `SW_WITH_SERVER_CODE`)는 두지 않는다 — `#if` 구역은 서버가 짓지 않는
# 헤더를 서버 게이트(`serverbuild`) 밖에 숨겨, 클라이언트 일은 폴더(`Client/` · `SW_EXCLUDED_ENGINE_FOLDERS`)와 모듈(`_listTarget`)로 가른다. 상수 갈래(`if constexpr`)는
# 두 갈래가 모든 구성에서 컴파일되므로 게이트를 비켜 가지 못한다. 옛 이름은 GCC · Clang 이 독으로 막는다(`TargetMacroCheck.h`).
target_compile_definitions(sw_global_options INTERFACE "SW_TARGET_NAME=\"${SW_TARGET_TYPE}\"")
message(STATUS "[BuildConfig] Target type: ${SW_TARGET_TYPE}")

# 이 타깃이 짓지 않는 Engine 폴더(`Source/Engine/` 기준) — 정본은 이 목록 하나다.
# Engine 은 그 폴더의 소스 · 리플렉션 헤더를 뺀다(Source/Engine/CMakeLists.txt). 헤더도 읽지 못한다 — 타깃 게이트 `serverbuild`(`sw_apply_target_layer_guard`)가 막는다.
# 전용 서버는 렌더러를 짓지 않는다. 엔진 루프 · 호스트는 `IEngineRenderDriver`(Engine/Graphics/View)만 보고, 뷰 요청 값(`FrameViewports`)도 그 폴더에 있다.
# 리눅스도 Windows 처럼 표시한 것(`SW_API` 등)만 공개한다. 켜 두지 않아 공개 매크로가 아무 일도 하지 않고 모든 모듈이 전부를 공개했다 - 확장 모듈의 링커가 자기 ImGui 사본 대신
# 에디터 모듈의 심볼을 잡아, 확장의 결속기가 에디터의 할당자를 덮어쓰고 확장이 내려간 뒤 없는 코드로 뛰었다. 정적 라이브러리(vcpkg)는 숨김으로 짓지 않아 링크에서 따로 가린다.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_compile_options(sw_global_options INTERFACE -fvisibility=hidden "$<$<COMPILE_LANGUAGE:CXX>:-fvisibility-inlines-hidden>")
	target_link_options(sw_global_options INTERFACE "LINKER:--exclude-libs,ALL")
endif()

set(SW_EXCLUDED_ENGINE_FOLDERS "")
if(SW_TARGET_TYPE STREQUAL "Server")
	# 전용 서버는 그리지도 화면을 띄우지도 소리를 내지도 않는다 — 렌더러는 `IEngineRenderDriver`, 런타임 UI 는 `IEngineUIDriver`, 오디오는 `IEngineAudioDriver` ·
	# `ISoundPlayer` · `IVoiceDecoder` 와 게임 코드의 창구(Engine/Presentation · GameFramework 의 Presentation)로만 이어진다. 클라이언트 전용 소스(`Client/`)도
	# 짓지 않는다(`SW_CLIENT_ONLY_SOURCE_PATTERNS`). 언리얼 데디케이티드 서버는 오디오 모듈을 싣고 장치만 만들지 않는다 — 여기는 코드를 싣지 않는다(기억).
	list(APPEND SW_EXCLUDED_ENGINE_FOLDERS "Audio" "Renderer" "UI")
	# 창 · 입력의 리눅스 데스크톱 구현(X11 · Wayland · evdev)도 짓지 않는다 — 리눅스 서버는 `Window/Headless` · `Input/Headless` 가 같은 함수를 정의한다.
	# Windows 서버는 Win32 구현을 그대로 짓는다(user32 는 모든 Windows Server 에 있다). 클라이언트는 거꾸로 `Headless` 를 짓지 않는다(Source/Engine/CMakeLists.txt).
	list(APPEND SW_EXCLUDED_ENGINE_FOLDERS "Window/Linux" "Input/Linux")
endif()

if(SW_SHIPPING_BUILD)
	target_compile_definitions(sw_global_options INTERFACE SW_SHIPPING)
	# 배포 클라이언트는 Info 로그를 컴파일하지 않는다(Core/Log/Logger.h). 전용 서버는 남긴다 — 준비 · 상태 · 종료 줄과 콘솔 명령의 답이 Info 이고,
	# 운영(journald · docker logs · 테스트 ServerBootTest)이 그 줄을 읽는다. 모든 타깃에 같게 걸어야 한다(헤더의 로그 매크로).
	if(SW_TARGET_TYPE STREQUAL "Server")
		target_compile_definitions(sw_global_options INTERFACE SW_LOG_COMPILED_VERBOSITY=SW_LOG_VERBOSITY_INFO)
	endif()
	message(STATUS "[BuildConfig] Shipping: Engine/SWGame STATIC, RHI backend linked into Engine, Editor off (SW_SHIPPING_BUILD=ON)")
else()
	message(STATUS "[BuildConfig] Dev: Engine SHARED, Editor/SWGame MODULE (type=${CMAKE_BUILD_TYPE})")
endif()

# Vulkan 검증 레이어를 켜는 구성의 정의는 여기 하나뿐이다. 세 곳이 이 정의만 읽는다.
#   레이어 복사(sw_deploy_runtime_dependencies) : 변수 SW_VULKAN_VALIDATION
#   켜기(VulkanRHIDevice 생성자) · 그것을 보는 테스트 : 매크로 SW_VULKAN_VALIDATION
# 세 곳이 조건을 따로 적으면 한쪽만 바뀐다. 그러면 레이어가 없는 구성에서 테스트가 돌아 꺼진 레이어를 실패로 본다.
# 출시 빌드는 빌드 형식이 Debug 여도 레이어를 싣지 않는다(배포물에 개발용 DLL 이 들어가지 않게).
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT SW_SHIPPING_BUILD)
	set(SW_VULKAN_VALIDATION ON)
	target_compile_definitions(sw_global_options INTERFACE SW_VULKAN_VALIDATION)
else()
	set(SW_VULKAN_VALIDATION OFF)
endif()

# 두 매크로 모두 헤더(Mutex.h)와 전역 연산자에 영향을 주므로 모든 타겟에 동일하게 적용해야 한다.
if(SW_ENABLE_DEADLOCK_DETECTION)
	target_compile_definitions(sw_global_options INTERFACE SW_ENABLE_DEADLOCK_DETECTION)
	message(STATUS "[BuildConfig] sw::Mutex deadlock detection enabled (slow)")
endif()

if(SW_ENABLE_STL_CONTAINER)
	target_compile_definitions(sw_global_options INTERFACE SW_ENABLE_STL_CONTAINER)
	message(STATUS "[BuildConfig] SW_ENABLE_STL_CONTAINER=ON -> Using std::allocator as the container allocator")
endif()


# ------------------------------------------------------------------------------
# IPO — 전역 CMAKE_INTERPROCEDURAL_OPTIMIZATION (Shipping 만)
# ------------------------------------------------------------------------------
set(SW_IPO_SUPPORTED FALSE)
include("${CMAKE_CURRENT_LIST_DIR}/../Config/JsonValue.cmake")

# `SW_ENABLE_LTO` 가 여기도 걸린다 — 이 전역 IPO 와 Shipping LTO 는 한 스위치다(빼면 OFF 를 줘도 LTO 로 간다).
# 배포 빌드(`SW_SHIPPING_BUILD`)만이다 — 개발 Release 에 걸면 엔진 .cpp 하나를 고쳐도 Engine.dll 링크가 코드 생성을 다시 해 75 초였다(Debug 4.7 초).
# 언리얼도 LTO 는 Shipping 만 한다(Development 는 끈다). 개발 Release 의 성능은 LTO 없는 값이다 — 배포 성능은 Ninja-Shipping 으로 잰다.
if(SW_ENABLE_LTO AND SW_SHIPPING_BUILD AND (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel"))
	sw_check_ipo_support(SW_IPO_SUPPORTED SW_IPO_ERROR)

	if(SW_IPO_SUPPORTED)
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
		message(STATUS "[BuildConfig] IPO enabled globally (opt-out for Tools/Tests/Editor)")

		# ThinLTO 링크 캐시 — 링크마다 최적화·코드 생성을 처음부터 다시 돌지 않고, 입력 비트코드와 옵션이 같은 모듈의 결과를 재사용한다(lld 가 모듈별 해시로 찾는다).
		# 실행 파일 · DLL 여덟 개가 같은 엔진 정적 라이브러리를 링크하므로 한 폴더를 같이 쓴다(lld 의 캐시 쓰기는 원자적이라 동시 링크가 안전하다).
		# 폴더는 빌드 폴더 아래라 `.gitignore`(/build/)가 가린다. 빌드 폴더를 지우면 같이 사라지지만 `ninja clean` 은 지우지 않는다 — 콜드 측정 전에는 `BuildCaches.json` 의 `cold_clear` 목록을 지운다.
		# 크기 상한 · 만료는 아래 정책이 정한다(lld 기본은 남은 디스크의 75 % · 만료 1 주일 · 정리 간격 1 시간 — 크기만 8 GB 로 조인다).
		# clang 은 LLD 가 필수라(`Clang.cmake`) 이 옵션을 알아듣는다. 다른 컴파일러(IPO 가 되더라도)에는 주지 않는다.
		if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			# 캐시 폴더 이름은 `BuildCaches.json` 이 정한다 — 콜드 측정 전에 지울 캐시 목록과 한 곳이다. 빌드 폴더 밖이면 구성을 세운다.
			set(SW_BUILD_CACHES_FILE "${CMAKE_CURRENT_LIST_DIR}/../Config/BuildCaches.json")
			set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SW_BUILD_CACHES_FILE}")
			sw_require_json_value(SW_LTO_CACHE_BASE "${SW_BUILD_CACHES_FILE}" cold_clear lto_link_cache base)
			sw_require_json_value(SW_LTO_CACHE_NAME "${SW_BUILD_CACHES_FILE}" cold_clear lto_link_cache path)

			if(NOT SW_LTO_CACHE_BASE STREQUAL "build")
				message(FATAL_ERROR "[BuildConfig] ${SW_BUILD_CACHES_FILE}: cold_clear.lto_link_cache.base must be 'build' (got '${SW_LTO_CACHE_BASE}')")
			endif()

			set(SW_LTO_CACHE_DIR "${CMAKE_BINARY_DIR}/${SW_LTO_CACHE_NAME}")
			set(SW_LTO_CACHE_POLICY "cache_size_bytes=8g:prune_after=168h")

			# 링크 풀(`BuildOptions.cmake` 5 — 코어 수 / 4, 2 ~ 4 개)이 동시에 여는 링크 수를 정한다. ThinLTO 백엔드는 링크마다 코어 수만큼 스레드를 띄우므로
			# 풀이 가득 차면 코어 수 × 풀 크기 스레드가 동시에 코드 생성을 해 풀이 막으려던 메모리 압박이 그대로 온다. 링크 하나의 스레드를 코어 수 / 풀 크기로 나눠
			# 합이 코어 수에 맞게 한다. 풀이 없는 생성기(Ninja 가 아님)는 상한을 두지 않는다(lld 기본).
			set(SW_LTO_JOBS_OPTION "")

			if(DEFINED cpu_count AND DEFINED link_jobs)
				math(EXPR SW_LTO_JOBS "${cpu_count} / ${link_jobs}")

				if(SW_LTO_JOBS LESS 2)
					set(SW_LTO_JOBS 2)
				endif()

				set(SW_LTO_JOBS_OPTION "${SW_LTO_JOBS}")
			endif()

			if(MSVC)
				target_link_options(sw_global_options INTERFACE "/lldltocache:${SW_LTO_CACHE_DIR}" "/lldltocachepolicy:${SW_LTO_CACHE_POLICY}")

				if(SW_LTO_JOBS_OPTION)
					target_link_options(sw_global_options INTERFACE "/opt:lldltojobs=${SW_LTO_JOBS_OPTION}")
				endif()
			else()
				target_link_options(sw_global_options INTERFACE "LINKER:--thinlto-cache-dir=${SW_LTO_CACHE_DIR}" "LINKER:--thinlto-cache-policy=${SW_LTO_CACHE_POLICY}")

				if(SW_LTO_JOBS_OPTION)
					target_link_options(sw_global_options INTERFACE "LINKER:--thinlto-jobs=${SW_LTO_JOBS_OPTION}")
				endif()
			endif()

			message(STATUS "[BuildConfig] ThinLTO link cache: ${SW_LTO_CACHE_DIR} (${SW_LTO_CACHE_POLICY}, backend jobs per link: ${SW_LTO_JOBS_OPTION})")
		endif()
	elseif(SW_IPO_ERROR)
		message(STATUS "[BuildConfig] IPO unsupported: ${SW_IPO_ERROR}")
	endif()
endif()


# ------------------------------------------------------------------------------
# 런타임 파일 복사 큐 — POST_BUILD는 sw_emit_runtime_copies가 한 번에 방출
# ------------------------------------------------------------------------------
function(sw_queue_runtime_copy TARGET_NAME SRC_FILE)
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "sw_queue_runtime_copy: target '${TARGET_NAME}' does not exist")
	endif()

	if(NOT SRC_FILE OR NOT EXISTS "${SRC_FILE}")
		return()
	endif()

	set_property(TARGET ${TARGET_NAME} APPEND PROPERTY SW_RUNTIME_COPY_FILES "${SRC_FILE}")
endfunction()

function(sw_emit_runtime_copies TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "sw_emit_runtime_copies: target '${TARGET_NAME}' does not exist")
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
# sw_keep_import_library_if_unchanged — SHARED 타깃의 임포트 라이브러리(`Lib/<타깃>.lib`)를 내용이 바뀔 때만 갱신한다(Windows).
#   내보내기가 같아도 링크는 `.lib` 를 다시 써서 수정 시각이 바뀌고, 그것을 링크하는 DLL · 실행 파일이 전부 다시 링크된다
#   (`Engine.cpp` 하나를 고쳐도 킷 · 게임 · 에디터 DLL 84 개). 링크가 임시 경로(`ImportLibraryStage/`)로 내보내게 하고 POST_BUILD 의
#   `copy_if_different` 가 옮긴다. 링크 규칙의 `RESTAT` 이 안 바뀐 출력을 보고 아래쪽 링크를 건너뛴다.
#   `/IMPLIB` 는 CMake 가 `/implib:` 를 쓴 뒤의 링크 옵션이라 뒤쪽 값이 이긴다. 내보내기가 없는 MODULE 은 `.lib` 가 없으므로 SHARED 만 부른다.
# ------------------------------------------------------------------------------
function(sw_keep_import_library_if_unchanged TARGET_NAME)
	if(NOT WIN32 OR NOT MSVC)
		return()
	endif()
	get_target_property(target_type ${TARGET_NAME} TYPE)
	if(NOT target_type STREQUAL "SHARED_LIBRARY")
		return()
	endif()
	set(stage_library "${CMAKE_BINARY_DIR}/ImportLibraryStage/${TARGET_NAME}.lib")
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/ImportLibraryStage")
	target_link_options(${TARGET_NAME} PRIVATE "/IMPLIB:${stage_library}")
	add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${stage_library}" "$<TARGET_LINKER_FILE:${TARGET_NAME}>"
		VERBATIM
	)
endfunction()

# 실행 파일 옆에 놓을 런타임 DLL — 구성마다 실제로 쓰는 것만.
#   DXC        : 모든 구성. 개발 빌드는 런타임 HLSL 컴파일 · DXIL 리플렉션에, 배포 빌드는 App 이 쿠커(`CookAssets` · `pack.py` 의 `--cook-all`)라 셰이더 쿠킹에 쓴다.
#                배포 패키지에는 들지 않는다 — 스테이징(`Scripts/package/pack.py` 의 LIST_COOK_ONLY_LIBRARY_STEM)이 뺀다(언리얼도 셰이더 컴파일러는 쿠커 쪽에만 둔다).
#   검증 레이어 : SW_VULKAN_VALIDATION 인 구성만(위의 정의 하나를 VulkanRHIDevice 도 매크로로 읽는다. 딸려 오는 mimalloc 도 그 의존성일 뿐이다)
#   Tracy      : 개발 빌드 · Windows · SW_ENABLE_TRACY (함수 안에서 거른다)
#   Tint       : WebGPU 셰이더 대상(SPIR-V → WGSL)의 쿠킹 도구 — DXC 와 같은 이유로 실행 파일 옆에 둔다(ThirdParty/dawn, 웹 · Android · 서버는 함수 안에서 거른다)
function(sw_deploy_runtime_dependencies TARGET_NAME)
	sw_copy_dxc_dlls(${TARGET_NAME})
	sw_copy_tint_tool(${TARGET_NAME})
	if(SW_VULKAN_VALIDATION)
		sw_copy_vulkan_validation_runtime(${TARGET_NAME})
	endif()
	sw_copy_tracy_runtime(${TARGET_NAME})
	sw_emit_runtime_copies(${TARGET_NAME})
endfunction()

# ------------------------------------------------------------------------------
# sw_remove_stale_binary_outputs — Dev `Bin` 의 옛 산출물을 configure 때 지운다(ninja 는 지어 놓은 파일을 치우지 않는다).
#   1) `Bin/Modules` 의 모듈 산출물(GF_* · RHI_* · EditorModule · SWGame*) 가운데 이 구성에서 꺼진 모듈의 것 — 켜진 모듈 것은 건드리지 않는다.
#      서드파티 DLL · PDB(모듈 이름 꼴이 아닌 것 — 서드파티는 `Bin` 에 한 벌, `sw_stage_module_runtime_dlls`)와 남은 섀도 복사본(`<모듈>_temp_p<pid>_…`).
#      살아 있는 App 이 쓰는 섀도 복사본은 잠겨 있어 지워지지 않는다 — 아래 경고로 남는다.
#   2) `Bin` 바로 아래의 옛 자리 산출물 — 모듈 DLL · PDB · 매니페스트(지금은 `Bin/Modules`), 테스트 실행 파일(지금은 `TestBin`),
#      `Bin/Symbols` 에 같은 이름이 생긴 PDB(옛 자리).
#   실행 중인 App · 테스트가 DLL 을 쥐고 있으면 지우지 못한다 — 경고만 하고 넘어간다(다음 configure 가 다시 지운다).
# ------------------------------------------------------------------------------
function(sw_remove_stale_binary_outputs ACTIVE_MODULES)
	set(bin_dir "${CMAKE_BINARY_DIR}/Bin")
	set(module_pattern "^(GF_.+|RHI_.+|EditorModule|SWGame.*)$")
	set(image_suffixes ".dll;.pdb;.so;.exp;.lib;.ilk;.debug")
	set(stale "")

	file(GLOB module_folder_files LIST_DIRECTORIES false "${bin_dir}/Modules/*")
	foreach(path IN LISTS module_folder_files)
		get_filename_component(stem "${path}" NAME_WE)
		get_filename_component(suffix "${path}" LAST_EXT)
		string(REGEX REPLACE "^lib" "" stem_no_prefix "${stem}")
		get_filename_component(file_name "${path}" NAME)
		if(file_name MATCHES "_temp_")
			list(APPEND stale "${path}")
		elseif(suffix IN_LIST image_suffixes AND stem_no_prefix MATCHES "${module_pattern}" AND NOT stem_no_prefix IN_LIST ACTIVE_MODULES)
			list(APPEND stale "${path}")
		elseif((suffix STREQUAL ".dll" OR suffix STREQUAL ".pdb") AND NOT stem_no_prefix MATCHES "${module_pattern}")
			list(APPEND stale "${path}")
		endif()
	endforeach()

	file(GLOB bin_files LIST_DIRECTORIES false "${bin_dir}/*")
	foreach(path IN LISTS bin_files)
		get_filename_component(file_name "${path}" NAME)
		get_filename_component(stem "${path}" NAME_WE)
		get_filename_component(suffix "${path}" LAST_EXT)
		string(REGEX REPLACE "^lib" "" stem_no_prefix "${stem}")
		if(stem_no_prefix MATCHES "${module_pattern}" AND (suffix IN_LIST image_suffixes OR file_name MATCHES "\\.module\\.json$"))
			list(APPEND stale "${path}")
		elseif(stem MATCHES "Test$" AND (suffix STREQUAL ".exe" OR suffix STREQUAL ".pdb" OR suffix STREQUAL ""))
			list(APPEND stale "${path}")
		elseif(suffix STREQUAL ".pdb" AND EXISTS "${bin_dir}/Symbols/${file_name}")
			list(APPEND stale "${path}")
		endif()
	endforeach()

	set(removed_count 0)
	foreach(path IN LISTS stale)
		execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -f "${path}" RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
		if(result EQUAL 0 AND NOT EXISTS "${path}")
			math(EXPR removed_count "${removed_count} + 1")
		else()
			message(WARNING "[Module] Could not remove a stale output (in use?): ${path}")
		endif()
	endforeach()
	if(removed_count GREATER 0)
		message(STATUS "[Module] Removed ${removed_count} stale output(s) from Bin (old layout or modules this configuration turns off)")
	endif()
endfunction()
