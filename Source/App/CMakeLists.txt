# ==============================================================================
# @file Source/App/CMakeLists.txt
# @brief 메인 애플리케이션(App)과 App · Server 가 같이 쓰는 모듈 호스트(AppHost) 빌드 스크립트
# ==============================================================================

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

# ------------------------------------------------------------------------------
# 0) AppHost — 모듈 호스트(ModuleHost · LiveReloadManager · ModuleCompiler · 매니페스트 해석)와 프레임 시간 정책.
#    플레이어 실행 파일(App)과 전용 서버(Server, Source/Server)가 같이 쓴다. 파일은 App 폴더에 그대로 둔다(include 경로 `App/Module/...`).
# ------------------------------------------------------------------------------
set(APP_HOST_SOURCES ${APP_SOURCES})
list(FILTER APP_HOST_SOURCES INCLUDE REGEX "/Source/App/(Module/[^/]+|FixedTimestep\\.(cpp|h))$")
list(REMOVE_ITEM APP_SOURCES ${APP_HOST_SOURCES})

# 핫 리로드는 Dev 전용이다. Shipping 은 모듈을 정적으로 링크하므로 갈아 끼울 것이 없다.
# 코드가 실행되지 않는 것만으로는 부족하고, 바이너리에서 아예 빠져야 한다.
# 빼는 것은 소스 검사 게이트에도 적힌다(`sw_excludeUnbuiltSources`).
if(SW_SHIPPING_BUILD)
	sw_excludeUnbuiltSources(APP_HOST_SOURCES "Module/LiveReloadManager\\.(cpp|h)$")
endif()

add_library(AppHost STATIC ${APP_HOST_SOURCES})
set_target_properties(AppHost PROPERTIES FOLDER "Source/App")
target_link_libraries(AppHost
    PUBLIC
        Engine
        RuntimeAPI
    PRIVATE
        sw_global_options
)
target_compile_definitions(AppHost PRIVATE "SW_LOG_TAG=\"Engine\"" SW_APP_INTERNAL)
sw_configurePch(AppHost "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

# ------------------------------------------------------------------------------
# 1) App — 플레이어 실행 파일(창 · 렌더러). 전용 서버 타깃은 짓지 않는다.
# ------------------------------------------------------------------------------
if(SW_TARGET_TYPE STREQUAL "Server")
	sw_declareUnbuiltSources(${APP_SOURCES})
	return()
endif()

add_executable(App ${APP_SOURCES})
sw_embedProcessManifest(App)
set_target_properties(App PROPERTIES FOLDER "Source/App")

target_include_directories(App PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(App
    PRIVATE
        AppHost
        Engine
        RuntimeAPI
        sw_global_options
)
target_compile_definitions(App PRIVATE "SW_LOG_TAG=\"Engine\"" SW_APP_INTERNAL)
sw_configurePch(App "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
sw_splitShippingDebugInfo(App)


# ------------------------------------------------------------------------------
# 2) 런타임 DLL 복사: DXC, Vulkan 검증 레이어(+mimalloc)
#    구성마다 실제로 쓰는 것만 배포한다 — 무조건 복사하면 Shipping 산출물에
#    셰이더 컴파일러와 검증 레이어가 따라 들어간다.
# ------------------------------------------------------------------------------

# DXC(dxcompiler/dxil)는 런타임 HLSL 컴파일과 DXIL 리플렉션에 쓰이는데, 배포 빌드는 둘 다
# 하지 않는다. 셰이더는 미리 쿠킹하고(ShaderCache), 바인딩 리플렉션도 쿠킹할 때 쿠킹해 둔
# 매니페스트를 읽는다(ShaderReflectionLibrary). 상용 엔진이 셰이더 컴파일러를 배포하지 않는
# 것과 같은 구조다.
if(NOT SW_SHIPPING_BUILD)
	sw_copyDxcDlls(App)
endif()

# Vulkan 검증 레이어는 VulkanRHIDevice 가 SW_DEBUG 에서만 켜므로 Debug 구성에만 배포한다.
# (함께 딸려 오는 mimalloc 도 검증 레이어의 의존성일 뿐이다.)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	sw_copyVulkanValidationRuntime(App)
endif()

# Tracy 클라이언트(개발 빌드 · Windows). Engine 이 지연 로드하므로 `-gv_tracy` 를 켜기 전에는 올라오지 않는다.
sw_copyTracyRuntime(App)

sw_emitRuntimeCopies(App)
