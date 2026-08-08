# ==============================================================================
# @file cmake/internal/AuxTargets.cmake
# @brief 보조 커스텀 타겟 (changelog / docs) — Scripts/generate 위임
# @note flag-module GLOB(Options/)에 넣지 않음 — 컴파일 플래그와 분리
# ==============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/Python.cmake")

if(SW_AUTO_CHANGELOG)
    if(NOT Python3_Interpreter_FOUND)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
    endif()
    add_custom_target(AutoChangelog
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Scripts/generate/GenerateChangelog.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Generating Auto-Changelog via Git history..."
        VERBATIM
    )
    set_target_properties(AutoChangelog PROPERTIES FOLDER "Engine/Scripts")
endif()

if(SW_BUILD_DOCS)
    if(NOT Python3_Interpreter_FOUND)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
    endif()
    find_program(DOXYGEN_EXECUTABLE doxygen)
    set(_doxyfile "${CMAKE_SOURCE_DIR}/Doxyfile")
    if(DOXYGEN_EXECUTABLE AND EXISTS "${_doxyfile}")
        add_custom_target(GenerateDocs
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Scripts/generate/GenerateDocs.py"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Generating Doxygen Documentation..."
            VERBATIM
        )
        set_target_properties(GenerateDocs PROPERTIES FOLDER "Engine/Scripts")
        message(STATUS "[Docs] Doxygen Documentation Target Enabled.")
    else()
        message(STATUS "[Docs] Skipping docs target (doxygen or Doxyfile missing).")
    endif()
endif()
