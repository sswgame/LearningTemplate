# ==============================================================================
# @file Source/DevTools/CMakeLists.txt
# @brief 개발 도구 공유 라이브러리(DevTools.dll) — 배포 게임이 쓰지 않는 개발 기능(리로드 · OS 파일 감시 · 콘솔 · HUD · 회선 흉내)을 엔진 밖에 둔다.
# ==============================================================================

# ------------------------------------------------------------------------------
# DevTools — 엔진 확장점(기동 확장 델리게이트 · 프레임 훅 `EngineFrameHooks`)에 붙는다(`DevToolsHost`). 엔진은 이 단위를 모른다.
# 링크하는 쪽: 개발 App · Server(호스트 개발 계층 `makeHostDevLayer` · 서버 개발 조종), EditorModule(설정 · 에셋 감시), GameFramework 의 개발 감시.
# 모듈 핫 리로드(`ModuleReload/` — LiveReloadManager · 리로드 구동기)와 호스트 개발 계층(`Host/`)은 모듈 관리(ModuleManager.dll) 위에 서서 그 창구를 구현한다.
# 의존은 DevTools → ModuleManager 한 방향이다 — 모듈 관리자는 `ILiveReloadManager` 만 안다(언리얼 HotReload 가 Core ModuleManager 위의 Developer 모듈인 자리).
# 늘 정의하되 `EXCLUDE_FROM_ALL` 이다 — 배포 호스트는 링크하지 않으므로 짓지 않는다(배포에서도 지어지는 에디터 테스트만 링크한다).
# 공유 라이브러리다 — 여러 이미지(App · EditorModule · GameFramework)가 링크하므로 정적이면 정적 등록이 이미지마다 돈다(언리얼 Developer 모듈도 DLL).
# include 경로는 `DevTools/...` 다.
# ------------------------------------------------------------------------------
file(GLOB_RECURSE DEVTOOLS_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")
sw_filterPlatformSources(DEVTOOLS_SOURCES) # OS 파일 감시기(`FileWatch/Windows` · `FileWatch/Linux`)
# 클라이언트 전용 부분(Client/ — 셰이더 라이브 리로드 · 디버그 HUD · 콘솔 오버레이, `IDevToolsClientPart`)은 전용 서버가 짓지 않는다(cmake/Engine/UnbuiltSources.cmake).
# 자동화 시나리오 실행기(`Client/Automation` — 언리얼 Automation 컨트롤러 · Gauntlet 이 Developer 모듈인 자리)도 여기다. 창 · RHI · 렌더러 사진을 모는 클라이언트 구동기라 서버는 짓지 않고,
# 엔진 창구 `IAutomationRuntime` 에 모듈 서브시스템으로 스스로 선다(`SW_MODULE_SUBSYSTEM` — 호스트 라이브러리 등록이 "Engine" 이름으로 걷는다).
# 설정 리로드 · 콘솔의 입력 · 시작 명령은 서버에도 선다.
if(SW_TARGET_TYPE STREQUAL "Server")
	sw_excludeUnbuiltSources(DEVTOOLS_SOURCES ${SW_CLIENT_ONLY_SOURCE_PATTERNS})
elseif(SW_TARGET_TYPE STREQUAL "Client")
	sw_excludeUnbuiltSources(DEVTOOLS_SOURCES ${SW_SERVER_ONLY_SOURCE_PATTERNS}) # 서버 전용 부분(Server/)은 클라이언트 구성이 짓지 않는다
endif()

add_library(DevTools SHARED ${DEVTOOLS_SOURCES})
sw_applyTargetLayerGate(DevTools devtools)
set_target_properties(DevTools PROPERTIES
	FOLDER "Source/DevTools"
	EXCLUDE_FROM_ALL ON)
target_link_libraries(DevTools
	PUBLIC
		Engine
	PRIVATE
		ModuleManager # 핫 리로드 · 호스트 개발 계층이 구현하는 창구와 매니페스트 해석(PRIVATE — 에디터는 DevTools 를 링크해도 모듈 관리를 보지 않는다)
		renderdoc # RenderDoc in-app API 헤더 하나(`RenderDoc/RenderDocCapture.cpp`)
		sw_global_options
)
target_compile_definitions(DevTools PRIVATE "SW_LOG_TAG=\"DevTools\"" "SW_REGISTRAR_MODULE=\"DevTools\"" SW_TOOL_INTERNAL) # 호스트 쪽 도구라 엔진 서비스 표를 직접 찾는다
sw_configureDLLExports(DevTools SHARED DEVTOOLS)
sw_configurePCH(DevTools "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

# Tracy — 엔진 프로파일러의 외부 출력(`Profiling/Tracy/TracyProfilerBackend`, 엔진은 `IProfilerBackend` 만 안다). 헤더는 Profiling/Tracy 에서만 include 한다.
# 클라이언트는 프로세스에 하나 — DevTools 만 링크하고, 엔진 · RHI 백엔드 · 모듈은 엔진 인터페이스만 본다. GPU 구간은 엔진이 이미 모으는 타임스탬프를 넘긴다.
# 임포트 타깃의 정의(TRACY_ENABLE · TRACY_IMPORTS …) · include 경로는 Tracy 구현 소스에만 붙인다(엔진의 Jolt 와 같은 방식). 배포 호스트는 DevTools 를 짓지 않는다.
# Windows 는 링크하지 않는다 — `-gv_tracy` 로 켤 때 TracyClient.dll 을 올려 C API 함수 표를 채운다(`TracyProfilerBackend::loadClient`). 리눅스는 링크한다.
if(SW_ENABLE_TRACY AND NOT SW_SHIPPING_BUILD AND TARGET Tracy::TracyClient)
	if(sw_platform_name STREQUAL "Windows")
		# 링크하지 않는다(위 — 지연 로드).
	elseif(sw_platform_name STREQUAL "Linux")
		target_link_libraries(DevTools PRIVATE $<LINK_ONLY:tracy>)
	else()
		message(FATAL_ERROR "[DevTools] Tracy link mode for platform '${sw_platform_name}' is not decided - name it here (SW_ENABLE_TRACY=OFF meanwhile)")
	endif()
	target_compile_definitions(DevTools PRIVATE SW_PROFILER_TRACY)
	file(GLOB swTracyBackendSources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Profiling/Tracy/*.cpp")
	# include 경로는 시스템 경로로 준다 — Tracy C API 의 `___tracy_*` 이름이 -Wreserved-identifier 경고를 낸다.
	get_target_property(swTracyIncludeDir Tracy::TracyClient INTERFACE_INCLUDE_DIRECTORIES)
	if(MSVC)
		set(swTracyIncludeOption "/external:I${swTracyIncludeDir}")
	else()
		set(swTracyIncludeOption "-isystem${swTracyIncludeDir}")
	endif()
	set_source_files_properties(${swTracyBackendSources} PROPERTIES
		COMPILE_DEFINITIONS "$<TARGET_PROPERTY:Tracy::TracyClient,INTERFACE_COMPILE_DEFINITIONS>"
		COMPILE_OPTIONS "${swTracyIncludeOption}"
		SKIP_PRECOMPILE_HEADERS ON
	)
	message(STATUS "[DevTools] Tracy profiler client enabled (loaded on demand on Windows)")
endif()
