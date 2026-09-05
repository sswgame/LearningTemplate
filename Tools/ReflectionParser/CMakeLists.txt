# ==============================================================================
# @file Tools/ReflectionParser/CMakeLists.txt
# @brief C++ AST 파서 기반 자체 리플렉션 생성기(ReflectionParser) 빌드 스크립트
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) LLVM/libclang 경로 — env → toolchain_config.json
# ------------------------------------------------------------------------------
set(DETECTED_LLVM_DIR "$ENV{LLVM_DIR}")
if(NOT DETECTED_LLVM_DIR)
    set(DETECTED_LLVM_DIR "$ENV{LLVM_HOME}")
endif()

set(swEngineCfg "${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_TOOLCHAIN_CONFIG}")

if(NOT DETECTED_LLVM_DIR AND EXISTS "${swEngineCfg}")
    file(READ "${swEngineCfg}" configJsonContent)
    string(JSON DETECTED_LLVM_DIR ERROR_VARIABLE jsonErr GET "${configJsonContent}" "${SW_KEY_LLVM_PATH}")
    if(jsonErr)
        set(DETECTED_LLVM_DIR "")
    endif()
    string(JSON DETECTED_LIBCLANG_DLL ERROR_VARIABLE jsonErr2 GET "${configJsonContent}" "libclang_dll_path")
    if(jsonErr2)
        set(DETECTED_LIBCLANG_DLL "")
    endif()
endif()

# env/toolchain 에 없으면 distro 패키지 경로를 찾습니다 (CI Ubuntu llvm-14 등).
if(NOT DETECTED_LLVM_DIR)
    foreach(_swLlvmMajor 20 19 18 17 16 15 14)
        set(_swLlvmCandidate "/usr/lib/llvm-${_swLlvmMajor}")
        if(EXISTS "${_swLlvmCandidate}/include/clang-c/Index.h")
            set(DETECTED_LLVM_DIR "${_swLlvmCandidate}")
            message(STATUS "[ReflectionParser] Using distro LLVM: ${DETECTED_LLVM_DIR}")
            break()
        endif()
        unset(_swLlvmCandidate)
    endforeach()
    unset(_swLlvmMajor)
endif()

# llvm_path 가 없으면 검색을 포기합니다.
if(NOT DETECTED_LLVM_DIR)
    message(STATUS "[ReflectionParser] LLVM directory not detected, skipping.")
    return()
endif()

set(LLVM_INSTALL_PREFIX "${DETECTED_LLVM_DIR}")
set(CLANG_SEARCH_PATHS
    "${LLVM_INSTALL_PREFIX}/lib"
    "${LLVM_INSTALL_PREFIX}/bin"
    "${LLVM_INSTALL_PREFIX}"
)

find_library(LIBCLANG_LIB
    NAMES libclang clang clang-20 clang-19 clang-18 clang-17 clang-16 clang-15 clang-14
    PATHS ${CLANG_SEARCH_PATHS}
    NO_DEFAULT_PATH
)

find_path(LIBCLANG_INCLUDE_DIR
    NAMES clang-c/Index.h
    PATHS
        "${LLVM_INSTALL_PREFIX}/include"
        /usr/include
    NO_DEFAULT_PATH
)

if(NOT LIBCLANG_LIB OR NOT LIBCLANG_INCLUDE_DIR)
    if(SW_REQUIRE_REFLECTION)
        message(FATAL_ERROR
            "[ReflectionParser] '${LLVM_INSTALL_PREFIX}' 경로에서 libclang을 찾을 수 없습니다.\n"
            "  LLVM을 설치하거나 Config/Environment/toolchain_config.json 의 llvm_path 를 설정하세요.\n"
            "  도구만 빌드하려면 -DSW_REQUIRE_REFLECTION=OFF 를 사용하세요."
        )

    endif()
    message(WARNING
        "[ReflectionParser] '${LLVM_INSTALL_PREFIX}' 경로에서 libclang을 찾을 수 없습니다.\n"
        "  ReflectionParser 빌드가 건너뛰어집니다."
    )
    return()
endif()

file(GLOB_RECURSE PARSER_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_executable(ReflectionParser ${PARSER_SOURCES})

target_include_directories(ReflectionParser PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/Tools"
    "${CMAKE_SOURCE_DIR}/Source"
    "${CMAKE_SOURCE_DIR}/Source/Core"
    "${CMAKE_SOURCE_DIR}/ThirdParty/nlohmann-json"
)
target_include_directories(ReflectionParser SYSTEM PRIVATE "${LIBCLANG_INCLUDE_DIR}")
target_include_directories(ReflectionParser PRIVATE "${CMAKE_BINARY_DIR}/generated")

target_link_libraries(ReflectionParser
    PRIVATE
        Core
        "${LIBCLANG_LIB}"
        sw_global_options
)
if(sw_flag_libraries)
    target_link_libraries(ReflectionParser PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(ReflectionParser PRIVATE "SW_LOG_TAG=\"Tool\"")
sw_configurePch(ReflectionParser "${CMAKE_SOURCE_DIR}/Source/Core/pch.h")

set_property(TARGET ReflectionParser PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
if(COMMAND sw_setUnityBuild)
	sw_setUnityBuild(ReflectionParser OFF)
endif()

set_target_properties(ReflectionParser PROPERTIES FOLDER "Tools")

# 빌드 타임 전용 도구다 — 런타임 산출물(Bin)과 섞이면 배포물에 파서 실행 파일과
# libclang.dll(수십 MB)이 그대로 따라 들어간다. 별도 디렉터리로 내보낸다.
# 호출부는 전부 $<TARGET_FILE:ReflectionParser> 제너레이터 식이라 경로를 따라온다.
set(swReflectionParserOutDir "${CMAKE_BINARY_DIR}/BuildTools")
set_target_properties(ReflectionParser PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${swReflectionParserOutDir}"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${swReflectionParserOutDir}"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${swReflectionParserOutDir}"
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${swReflectionParserOutDir}"
	RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${swReflectionParserOutDir}"
)

if(WIN32)
    target_compile_definitions(ReflectionParser PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ------------------------------------------------------------------------------
# 3) libclang.dll — ReflectionParser 옆(BuildTools)으로 복사
# ------------------------------------------------------------------------------
if(NOT DETECTED_LIBCLANG_DLL AND LIBCLANG_INCLUDE_DIR)
    find_file(
        DETECTED_LIBCLANG_DLL
        NAMES libclang.dll
        HINTS "${LLVM_INSTALL_PREFIX}/bin" "${LLVM_INSTALL_PREFIX}/lib"
    )
endif()

if(DETECTED_LIBCLANG_DLL AND EXISTS "${DETECTED_LIBCLANG_DLL}")
    add_custom_command(
        TARGET ReflectionParser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DETECTED_LIBCLANG_DLL}"
            "$<TARGET_FILE_DIR:ReflectionParser>"
        COMMENT "[ReflectionParser] Copying libclang.dll..."
        VERBATIM
    )
endif()

target_sources(ReflectionParser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PredefinedReflectAnnotation.xxx")

# ------------------------------------------------------------------------------
# 4) 어노테이션 매크로 테이블 — IDE 폴더는 Tools
# ------------------------------------------------------------------------------
set_target_properties(ReflectionParser PROPERTIES FOLDER "Tools")

target_compile_definitions(ReflectionParser PRIVATE SW_TOOL_INTERNAL)
