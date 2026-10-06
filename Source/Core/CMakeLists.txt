# ==============================================================================
# @file Source/Core/CMakeLists.txt
# @brief 호스트 · 런타임의 기반(Common, Log, FileUtil, String 등)을 담는 STATIC 타깃
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 소스 — 폴더의 .cpp 전부. 플랫폼 폴더(Windows/ · Linux/ · Posix/)는 그 플랫폼에서만(`sw_filterPlatformSources` — 고르지 않은 것은
#    소스 검사 게이트에 "이 구성이 짓지 않는다" 로 적힌다). 목록을 손으로 들지 않는다 — 빠지면 ReflectionParser(Core 를 링크한다) 링크에서야 깨지던 자리다.
# ------------------------------------------------------------------------------
file(GLOB_RECURSE listCoreSource CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
sw_filterPlatformSources(listCoreSource)

# ------------------------------------------------------------------------------
# 2) Core_objects — 한 번만 컴파일한다
#    ReflectionParser: Core STATIC 을 링크한다
#    Engine: 같은 OBJECT 를 넣어 Dev 에서 Engine.dll 로 export 한다
#    SW_EXPORTS: App · Editor(SW_IMPORTS)가 dllimport 할 수 있게 한다
# ------------------------------------------------------------------------------
add_library(Core_objects OBJECT ${listCoreSource})
set_target_properties(Core_objects PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	FOLDER "Source"
)

target_include_directories(Core_objects
	PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(Core_objects PUBLIC sw_public_source_includes)
target_link_libraries(Core_objects PUBLIC sw_third_party_includes)

if(NOT SW_SHIPPING_BUILD)
	target_compile_definitions(Core_objects PRIVATE SW_EXPORTS)
endif()

if(WIN32)
	target_link_libraries(Core_objects PUBLIC comdlg32 ole32 shell32 dbghelp synchronization ws2_32)
endif()

target_compile_definitions(Core_objects PRIVATE "SW_LOG_TAG=\"Engine\"")
sw_configurePch(Core_objects "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
target_link_libraries(Core_objects PRIVATE sw_global_options)
# 벡터 거리 · 길이(`float3::getDistance` 등)는 파괴 · 파쇄가 쓰는 줄 밖 함수다 — 구성마다 같은 비트여야 한다(`sw_markDeterministicSources`).
# 여기서 축약을 끄는 비용은 이 함수들의 FMA 하나씩이다(인라인이 아니라 호출 비용이 이미 더 크다).
sw_markDeterministicSources(Core_objects Math/VectorMath.cpp)

# ------------------------------------------------------------------------------
# 3) Core STATIC — 파서가 링크하는 껍데기. Engine 은 Core_objects 를 직접 가져간다
# ------------------------------------------------------------------------------
add_library(Core STATIC $<TARGET_OBJECTS:Core_objects>)
set_target_properties(Core PROPERTIES
	FOLDER "Source"
)
target_include_directories(Core
	PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(Core PUBLIC sw_public_source_includes)
target_link_libraries(Core PUBLIC sw_third_party_includes)

if(WIN32)
	target_link_libraries(Core PUBLIC comdlg32 ole32 shell32 dbghelp synchronization ws2_32)
endif()

# Engine 이 $<TARGET_OBJECTS:Core_objects> 를 가져갈 수 있도록 의존성을 드러낸다
add_dependencies(Core Core_objects)
set_property(TARGET Core PROPERTY SW_CORE_OBJECTS Core_objects)

message(STATUS "[Core] STATIC - foundation (ReflectionParser links Core; Engine shares Core_objects)")
