# ==============================================================================
# @file Source/Server/CMakeLists.txt
# @brief 전용 서버 실행 파일(Server) — 창 · GPU 없이 게임 모듈을 고정 틱으로 돌린다. Game · Server 타깃에서 짓는다(Client 타깃은 짓지 않는다).
# ==============================================================================

file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

if(SW_TARGET_TYPE STREQUAL "Client")
	sw_declareUnbuiltSources(${SERVER_SOURCES})
	return()
endif()

add_executable(Server ${SERVER_SOURCES})
sw_embedProcessManifest(Server)
set_target_properties(Server PROPERTIES FOLDER "Source/Server")

target_link_libraries(Server
    PRIVATE
        AppHost
        Engine
        RuntimeAPI
        sw_global_options
)
target_compile_definitions(Server PRIVATE "SW_LOG_TAG=\"Server\"" SW_APP_INTERNAL)
sw_configurePch(Server "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

# Windows 서비스 제어(SCM)는 advapi32. 콘솔 서브시스템이어야 콘솔 제어 처리기 · 표준 입력이 있다(WIN32 를 붙이지 않는다).
if(WIN32)
	target_link_libraries(Server PRIVATE advapi32.lib)
endif()

# Tracy 클라이언트(개발 빌드 · Windows). Engine 이 지연 로드하므로 `-gv_tracy` 를 켜기 전에는 올라오지 않는다.
sw_copyTracyRuntime(Server)
sw_emitRuntimeCopies(Server)
