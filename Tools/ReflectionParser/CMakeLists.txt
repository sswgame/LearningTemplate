# ==============================================================================
# @file Tools/ReflectionParser/CMakeLists.txt
# @brief C++ AST 파서 기반 자체 리플렉션 생성기(ReflectionParser) 빌드 스크립트
# ==============================================================================

# LLVM_DIR / LLVM_HOME 환경변수에서 LLVM(libclang) 경로 추출
set(DETECTED_LLVM_DIR "$ENV{LLVM_DIR}")
if(NOT DETECTED_LLVM_DIR)
    set(DETECTED_LLVM_DIR "$ENV{LLVM_HOME}")
endif()

# 환경변수가 없을 경우엔 Config/engine_config.json 설정 파일에서 파싱
if(NOT DETECTED_LLVM_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/Config/engine_config.json")
    file(READ "${CMAKE_SOURCE_DIR}/Config/engine_config.json" _config_json_content)
    string(JSON DETECTED_LLVM_DIR ERROR_VARIABLE _json_err GET "${_config_json_content}" "llvm_path")
    string(JSON DETECTED_LIBCLANG_DLL ERROR_VARIABLE _json_err2 GET "${_config_json_content}" "libclang_dll_path")
endif()

set(LLVM_INSTALL_PREFIX "${DETECTED_LLVM_DIR}" CACHE PATH "ReflectionParser용 LLVM 설치 경로")

# libclang 임포트 라이브러리 탐색
find_library(
    LIBCLANG_LIB
    NAMES libclang clang
    HINTS "${LLVM_INSTALL_PREFIX}/lib"
    DOC "libclang 임포트 라이브러리"
)

# clang-c 헤더 디렉토리 탐색
find_path(
    LIBCLANG_INCLUDE_DIR
    NAMES clang-c/Index.h
    HINTS "${LLVM_INSTALL_PREFIX}/include"
    DOC "clang-c 헤더 디렉토리"
)

# LLVM/Clang 경로 실패 시 경고 출력 후 빌드 스킵
if(NOT LIBCLANG_LIB OR NOT LIBCLANG_INCLUDE_DIR)
    message(WARNING
        "[ReflectionParser] '${LLVM_INSTALL_PREFIX}' 경로에서 libclang을 찾을 수 없습니다.\n"
        "  ReflectionParser 빌드가 건너뛰어집니다."
    )
    return()
endif()

# ReflectionParser 실행 파일 타겟 정의 및 Core, libclang 라이브러리 링킹
sw_add_executable(ReflectionParser
    NO_INSTALL
    LINK_LIBRARIES
        Core
        "${LIBCLANG_LIB}"
)
sw_set_log_tag(ReflectionParser Tool)
target_include_directories(ReflectionParser SYSTEM PRIVATE "${LIBCLANG_INCLUDE_DIR}")

# IDE 폴더 구조를 "Tools"로 분류
set_target_properties(ReflectionParser PROPERTIES FOLDER "Tools")

# Windows 환경 경고 무시 처리
if(WIN32)
    target_compile_definitions(ReflectionParser PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# 런타임에 필요한 libclang.dll 복사본 검색
if(NOT DETECTED_LIBCLANG_DLL AND LIBCLANG_INCLUDE_DIR)
    find_file(
        DETECTED_LIBCLANG_DLL
        NAMES libclang.dll
        HINTS "${LLVM_INSTALL_PREFIX}/bin" "${LLVM_INSTALL_PREFIX}/lib"
    )
endif()

# DLL 파일이 존재하면 빌드 출력 디렉토리(Bin)로 복사 커맨드 등록
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

# 생성된 Parser 경로를 CMake 캐시에 저장하여 다른 모듈(SW_ADD_REFLECTION_STEP 매크로)이 참조하도록 캐싱
set(SW_REFLECTION_PARSER_EXE
    $<TARGET_FILE:ReflectionParser>
    CACHE INTERNAL "ReflectionParser 실행 파일 경로"
)
