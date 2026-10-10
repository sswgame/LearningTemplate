# ==============================================================================
# @file Tools/ReflectionParser/CMakeLists.txt
# @brief C++ AST 파서 기반 자체 리플렉션 생성기(ReflectionParser) 빌드 스크립트
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) libclang — 못 찾으면 파서를 짓지 않는다(SW_REQUIRE_REFLECTION 이면 구성이 선다). 찾는 순서는 cmake/Environment/FindLibclang.cmake.
# ------------------------------------------------------------------------------
include("${CMAKE_SOURCE_DIR}/cmake/Environment/FindLibclang.cmake")
sw_findLibclang(swLibclangLib swLibclangIncludeDir swLibclangDll)
if(NOT swLibclangLib)
	return()
endif()

# ------------------------------------------------------------------------------
# 2) ReflectionParser 실행 파일 — Engine.dll 과 순환하지 않게 Core 만 링크한다
# ------------------------------------------------------------------------------
file(GLOB_RECURSE PARSER_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_executable(ReflectionParser ${PARSER_SOURCES})
sw_embedProcessManifest(ReflectionParser)

target_include_directories(ReflectionParser PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/Tools"
	"${CMAKE_SOURCE_DIR}/Source"
	"${CMAKE_SOURCE_DIR}/Source/Core"
)
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
sw_configurePch(ReflectionParser "${CMAKE_SOURCE_DIR}/Source/Core/pch.h")

set_property(TARGET ReflectionParser PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
sw_setUnityBuild(ReflectionParser OFF)

# 빌드 타임 전용 도구다. 런타임 산출물(Bin)과 섞이면 배포물에 파서 실행 파일과
# libclang.dll(수십 MB)이 그대로 따라 들어가므로 별도 디렉터리로 내보낸다(`$<0:>` 는 구성 이름 하위 폴더를 막는다 — BuildLayout 과 같다).
# 호출부는 모두 $<TARGET_FILE:ReflectionParser> 제너레이터 식이라 경로를 따라온다.
set_target_properties(ReflectionParser PROPERTIES
	FOLDER "Tools"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/BuildTools$<0:>"
)

# 어노테이션 매크로 표 — IDE 에서 보이게 소스로 단다.
target_sources(ReflectionParser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PredefinedReflectAnnotation.xxx")

# ------------------------------------------------------------------------------
# 3) libclang.dll — ReflectionParser 옆(BuildTools)으로 복사
# ------------------------------------------------------------------------------
if(swLibclangDll)
	add_custom_command(
		TARGET ReflectionParser POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${swLibclangDll}"
		"$<TARGET_FILE_DIR:ReflectionParser>"
		COMMENT "[ReflectionParser] Copying libclang.dll..."
		VERBATIM
	)
endif()
