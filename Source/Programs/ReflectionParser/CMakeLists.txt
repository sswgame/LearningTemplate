# ==============================================================================
# @file Source/Programs/ReflectionParser/CMakeLists.txt
# @brief C++ AST 파서 기반 자체 리플렉션 생성기(ReflectionParser) 빌드 스크립트
# ==============================================================================

# ------------------------------------------------------------------------------
# 0) ReflectionSchema — 파서와 엔진 런타임이 함께 지키는 리플렉션 스키마 헤더(헤더만, 구현 없음)
# 파서는 Engine.dll 을 링크하지 않는다(순환). 예전에는 그러면서 Engine 헤더를 그냥 include 해 무엇에 기대는지 대상 그래프에 없었다 —
# 이 목록이 파서가 읽어도 되는 Engine 헤더의 전부다(그 헤더들이 include 하는 Engine 헤더까지). 구현이 필요한 헤더를 여기 넣지 않는다(파서가 링크하지 못한다).
# ------------------------------------------------------------------------------
add_library(ReflectionSchema INTERFACE)
target_sources(ReflectionSchema INTERFACE
	FILE_SET HEADERS
	BASE_DIRS "${CMAKE_SOURCE_DIR}/Source"
	FILES
		"${CMAKE_SOURCE_DIR}/Source/Engine/EngineMinimal.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Foundation/EngineDefines.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Foundation/Foundation.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Reflection/ReflectUnits.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Reflection/ReflectionAnnotationKeys.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Reflection/ReflectionConstants.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Reflection/ReflectionContainers.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Reflection/ReflectionEnumNames.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Reflection/ReflectionIntellisense.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Reflection/ReflectionMacros.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Reflection/ReflectionTypes.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Reflection/TextAddress.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Reflection/TypeCastHeader.h"
		"${CMAKE_SOURCE_DIR}/Source/Engine/Reflection/TypeLookupCache.h"
)
target_link_libraries(ReflectionSchema INTERFACE sw_public_source_includes)

# ------------------------------------------------------------------------------
# 1) libclang — 못 찾으면 파서를 짓지 않는다(SW_REQUIRE_REFLECTION 이면 구성이 선다). 찾는 순서는 cmake/Environment/FindLibclang.cmake.
# ------------------------------------------------------------------------------
include("${CMAKE_SOURCE_DIR}/cmake/Environment/FindLibclang.cmake")
sw_findLibclang(swLibclangLib swLibclangIncludeDir swLibclangDLL)
if(NOT swLibclangLib)
	return()
endif()

# ------------------------------------------------------------------------------
# 2) ReflectionParser 실행 파일 — Engine.dll 과 순환하지 않게 Core 만 링크한다
# ------------------------------------------------------------------------------
file(GLOB_RECURSE PARSER_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

sw_addExecutable(ReflectionParser ${PARSER_SOURCES})

# 헤더는 같은 폴더면 짧은 이름, 아니면 `Source/` 기준 전체 경로다 — 이 폴더를 include 경로에 두지 않는다(`sw_verifySourceIncludeRoots`).
target_link_libraries(ReflectionParser PRIVATE ReflectionSchema)
sw_addPCHEntryDirectory(ReflectionParser PRIVATE core "Core/pch.h")
# 외부 헤더는 SYSTEM 으로 — 우리 경고 규칙(-Werror=covered-switch-default 등)이 그 안에 걸리지 않는다.
target_include_directories(ReflectionParser SYSTEM PRIVATE "${swLibclangIncludeDir}" "${CMAKE_SOURCE_DIR}/ThirdParty/nlohmann-json")
target_include_directories(ReflectionParser PRIVATE "${CMAKE_BINARY_DIR}/generated")

target_link_libraries(ReflectionParser
	PRIVATE
	Core
	"${swLibclangLib}"
	sw_global_options
)

target_compile_definitions(ReflectionParser PRIVATE "SW_LOG_TAG=\"Tool\"" SW_TOOL_INTERNAL)
if(WIN32)
	target_compile_definitions(ReflectionParser PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
sw_configurePCH(ReflectionParser "${CMAKE_SOURCE_DIR}/Source/Core/pch.h")

set_property(TARGET ReflectionParser PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
sw_setUnityBuild(ReflectionParser OFF)

# 빌드 타임 전용 도구다. 런타임 산출물(Bin)과 섞이면 배포물에 파서 실행 파일과
# libclang.dll(수십 MB)이 그대로 따라 들어가므로 별도 디렉터리로 내보낸다(`$<0:>` 는 구성 이름 하위 폴더를 막는다 — BuildLayout 과 같다).
# 호출부는 모두 $<TARGET_FILE:ReflectionParser> 제너레이터 식이라 경로를 따라온다.
set_target_properties(ReflectionParser PROPERTIES
	FOLDER "Source/Programs"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/BuildTools$<0:>"
)

# 어노테이션 매크로 표 — IDE 에서 보이게 소스로 단다.
target_sources(ReflectionParser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PredefinedReflectAnnotation.xxx")

# ------------------------------------------------------------------------------
# 3) libclang.dll — ReflectionParser 옆(BuildTools)으로 복사
# ------------------------------------------------------------------------------
if(swLibclangDLL)
	add_custom_command(
		TARGET ReflectionParser POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${swLibclangDLL}"
		"$<TARGET_FILE_DIR:ReflectionParser>"
		COMMENT "[ReflectionParser] Copying libclang.dll..."
		VERBATIM
	)
endif()
