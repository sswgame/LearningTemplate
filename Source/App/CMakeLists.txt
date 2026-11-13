# ==============================================================================
# @file Source/App/CMakeLists.txt
# @brief 메인 애플리케이션(App) 빌드 스크립트. App · Server 가 같이 쓰는 모듈 호스트는 Source/ModuleManager 다.
# ==============================================================================

sw_glob_source_tree(APP_SOURCES RECURSE "*.cpp" "*.c" "*.h" "*.hpp")

# ------------------------------------------------------------------------------
# 1) App — 플레이어 실행 파일(창 · 렌더러). 전용 서버 타깃은 짓지 않는다.
# ------------------------------------------------------------------------------
if(SW_TARGET_TYPE STREQUAL "Server")
	sw_declare_excluded_sources(${APP_SOURCES})
	return()
endif()

set(swAppDir "${CMAKE_CURRENT_SOURCE_DIR}")
# 개발 전용 소스(`Tests/` · `Dev/`)는 다른 타깃과 같은 규칙으로 뺀다(`SW_DEV_ONLY_SOURCE_PATTERNS`). 에디터 모듈의 개발 경로는 `EditorModuleManager.cpp` 안의 `#if` 구역이다.
# 부르는 자리는 `build::kShipping` 으로 가르고 배포에서는 이유를 남기고 멈춘다.
if(SW_SHIPPING_BUILD)
	sw_remove_excluded_sources(APP_SOURCES ${SW_DEV_ONLY_SOURCE_PATTERNS})
endif()

sw_add_executable(App ${APP_SOURCES})
sw_apply_target_layer_guard(App app)
if(WIN32)
	# 실행 파일 아이콘(.rc). RC 컴파일러는 DetectToolchain 이 llvm-rc 로 정해 둔다. 리눅스에는 RC 가 없어 WIN32 안에서만 켠다.
	enable_language(RC)
	target_sources(App PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/App.rc")
	# GUI 서브시스템 — OS 가 콘솔 창을 만들지 않는다(언리얼 UnrealEditor.exe 자리). 진입점은 그대로 main 이다. 출력을 파이프로 받는 실행(ctest · 시나리오 · 스크립트)은
	# 물려받은 핸들로 그대로 쓴다. 로그는 로그 파일과 에디터 출력 로그 패널에 남는다.
	target_link_options(App PRIVATE "LINKER:/SUBSYSTEM:WINDOWS" "LINKER:/ENTRY:mainCRTStartup")
endif()
set_target_properties(App PROPERTIES FOLDER "Source/App")

target_link_libraries(App
	PRIVATE
		ModuleManager
		Engine
		ModuleAPI
		sw_global_options
)
# 개발 도구(셰이더 · 설정 리로드 · 핫 리로드) — 호스트가 개발 계층(`makeHostDevLayer`)을 만들어 부팅 열에 넘긴다. 배포 호스트는 링크하지 않는다.
# 개발 구성의 ModuleManager 는 공유 라이브러리라 그 안의 테스트 등록(`ModuleManager/Tests`)도 그 DLL 이 들고 온다(통째 링크가 필요 없다).
if(NOT SW_SHIPPING_BUILD)
	target_link_libraries(App PRIVATE DevTools)
endif()
target_compile_definitions(App PRIVATE "SW_LOG_TAG=\"Engine\"" "SW_REGISTRAR_MODULE=\"App\"" SW_APP_INTERNAL)

sw_configure_pch(App "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
sw_split_shipping_debug_info(App)


# ------------------------------------------------------------------------------
# 2) 런타임 DLL 복사: DXC, Vulkan 검증 레이어(+mimalloc)
#    구성마다 실제로 쓰는 것만 Bin 에 둔다.
# ------------------------------------------------------------------------------

# DXC(dxcompiler/dxil)는 런타임 HLSL 컴파일 · DXIL 리플렉션 · 셰이더 쿠킹에 쓰인다. 배포 App 은 런타임에 컴파일하지 않지만
# (셰이더는 미리 쿠킹 — ShaderCache, 바인딩 리플렉션도 쿠킹해 둔 매니페스트 — ShaderReflectionLibrary) 빌드의 쿠커라 Bin 에는 둔다.
# 배포 패키지에는 넣지 않는다(`Scripts/package/pack.py` 스테이징이 뺀다) — 상용 엔진이 셰이더 컴파일러를 배포하지 않는 것과 같다.
# Vulkan 검증 레이어는 VulkanRHIDevice 가 SW_DEBUG 에서만 켜므로 Debug 구성에만, Tracy 클라이언트는 개발 빌드 · Windows 에만
# (Engine 이 지연 로드하므로 `-gv_tracy` 를 켜기 전에는 올라오지 않는다) — 고르기는 sw_deploy_runtime_dependencies 가 한다.
sw_deploy_runtime_dependencies(App)
