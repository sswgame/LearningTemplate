# ==============================================================================
# @file Source/App/CMakeLists.txt
# @brief 메인 애플리케이션(App) 빌드 및 패키징 스크립트
# ==============================================================================

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_executable(App ${APP_SOURCES})
set_target_properties(App PROPERTIES FOLDER "Source/App")

target_include_directories(App PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(App
    PRIVATE
        Engine
        RuntimeAPI
        sw_global_options
)
if(sw_flag_libraries)
    target_link_libraries(App PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(App PRIVATE "SW_LOG_TAG=\"Engine\"" SW_APP_INTERNAL)
sw_configurePch(App "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")

sw_addReflectionStep(App
    INCLUDES "${CMAKE_SOURCE_DIR}/Source"
    HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/AppConfig.h"
)

# ------------------------------------------------------------------------------
# 2) 런타임 DLL 복사 — DXC, Vulkan validation(+mimalloc)
#    구성별로 실제 쓰이는 것만 배포한다. 예전엔 무조건 복사해서 Shipping 산출물에
#    셰이더 컴파일러와 검증 레이어가 그대로 따라 들어갔다.
# ------------------------------------------------------------------------------

# DXC(dxcompiler/dxil)는 런타임 HLSL 컴파일과 DXIL 리플렉션에 쓰이는데, 배포 빌드는 둘 다
# 하지 않는다 — 셰이더는 미리 굽고(ShaderCache), 바인딩 리플렉션도 쿠킹 시점에 구운
# 매니페스트를 읽는다(ShaderReflectionLibrary). 상용 엔진이 셰이더 컴파일러를 배포하지 않는
# 것과 같은 구조다.
if(NOT SW_SHIPPING_BUILD)
	sw_copyDxcDlls(App)
endif()

# Vulkan 검증 레이어는 VulkanRHIDevice 가 SW_DEBUG 에서만 켠다 — Debug 구성에만 배포한다.
# (딸려오는 mimalloc 도 검증 레이어 의존성일 뿐이다.)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	sw_copyVulkanValidationRuntime(App)
endif()

sw_emitRuntimeCopies(App)

# ------------------------------------------------------------------------------
# 3) Shipping 정적 링크 시 리플렉션 등록기 보존
# ------------------------------------------------------------------------------
# 생성된 *.gen.cpp 의 등록기는 파일 스코프 static 객체이고(생성자가 전역 링크드 리스트에
# 자신을 매단다) 외부에서 참조되는 심볼이 없다. Dev 는 Engine 이 DLL 이라 전부 로드되지만,
# Shipping 은 정적 라이브러리라 링커가 "아무도 참조 안 하는 오브젝트 파일"을 통째로 버린다.
# 실제로 RHITypes.gen.cpp 가 빠져 RHIBackend 열거형이 등록되지 않았고, EngineConfig 의
# _window._defaultRHI 역직렬화가 실패해 엔진 설정이 통째로 기본값으로 떨어졌다
# (→ 리소스 우선순위 미설정 → 파이프라인 XML 조회 실패 → FrameRenderer 초기화 실패).
# 리플렉션을 담은 정적 라이브러리는 통째로 링크해 등록기를 살린다.
if(SW_SHIPPING_BUILD AND WIN32)
	set(swReflectionStaticLibs Engine GameFramework SWGame)
	get_property(swKitModules GLOBAL PROPERTY SW_DYNAMIC_MODULES)
	if(swKitModules)
		list(APPEND swReflectionStaticLibs ${swKitModules})
	endif()
	list(REMOVE_DUPLICATES swReflectionStaticLibs)

	foreach(reflLib IN LISTS swReflectionStaticLibs)
		if(TARGET ${reflLib})
			get_target_property(reflLibType ${reflLib} TYPE)
			if(reflLibType STREQUAL "STATIC_LIBRARY")
				target_link_options(App PRIVATE "LINKER:/WHOLEARCHIVE:$<TARGET_FILE:${reflLib}>")
			endif()
		endif()
	endforeach()
endif()
