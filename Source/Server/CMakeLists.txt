# ==============================================================================
# @file Source/Server/CMakeLists.txt
# @brief 전용 서버 실행 파일(Server) — 창 · GPU 없이 게임 모듈을 고정 틱으로 돌린다. Game · Server 타깃에서 짓는다(Client 타깃은 짓지 않는다).
# ==============================================================================

sw_globSourceTree(SERVER_SOURCES RECURSE "*.cpp" "*.c" "*.h" "*.hpp")

if(SW_TARGET_TYPE STREQUAL "Client")
	sw_declareExcludedSources(${SERVER_SOURCES})
	return()
endif()

# 개발 전용(`Tests/` · `Dev/`)은 배포가 짓지 않는다. 서버 개발 조종(`ModuleManager/Host/ServerDebugControl` — 구현은 DevTools 의 `Host/`)과 개발 계층은 `ServerApp` 이 `build::kShipping` 이 아닐 때만 만든다.
if(SW_SHIPPING_BUILD)
	sw_removeExcludedSources(SERVER_SOURCES ${SW_DEV_ONLY_SOURCE_PATTERNS})
endif()

sw_addExecutable(Server ${SERVER_SOURCES})
sw_applyTargetLayerGuard(Server server)
set_target_properties(Server PROPERTIES FOLDER "Source/Server")

target_link_libraries(Server
	PRIVATE
		ModuleManager
		Engine
		ModuleAPI
		sw_global_options
)
# 개발 도구(셰이더 · 설정 리로드) — 호스트 범위의 `DevToolsHost` 가 엔진 확장점에 붙는다. 배포 호스트는 링크하지 않는다.
if(NOT SW_SHIPPING_BUILD)
	target_link_libraries(Server PRIVATE DevTools)
endif()
target_compile_definitions(Server PRIVATE "SW_LOG_TAG=\"Server\"" "SW_REGISTRAR_MODULE=\"Server\"" SW_APP_INTERNAL)
sw_configurePCH(Server "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

# Windows 서비스 제어(SCM)는 advapi32. 콘솔 서브시스템이어야 콘솔 제어 처리기 · 표준 입력이 있다(WIN32 를 붙이지 않는다).
if(WIN32)
	target_link_libraries(Server PRIVATE advapi32.lib)
endif()

# Tracy 클라이언트(개발 빌드 · Windows). Engine 이 지연 로드하므로 `-gv_tracy` 를 켜기 전에는 올라오지 않는다.
sw_copyTracyRuntime(Server)
sw_emitRuntimeCopies(Server)
