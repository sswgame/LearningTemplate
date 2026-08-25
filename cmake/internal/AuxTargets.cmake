# ==============================================================================
# @file cmake/internal/AuxTargets.cmake
# @brief 보조 커스텀 타겟 (changelog / docs) — Scripts/generate 위임
# @note LoadFlagModules 명시 include 목록에 넣지 않음 — 컴파일 플래그와 분리
# ==============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/Python.cmake")

# ------------------------------------------------------------------------------
# 1) Doxygen — SW_BUILD_DOCS일 때만 GenerateDocs
# ------------------------------------------------------------------------------
if(SW_BUILD_DOCS)
    if(NOT Python3_Interpreter_FOUND)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
    endif()
    find_program(DOXYGEN_EXECUTABLE doxygen)
    set(doxyfile "${CMAKE_SOURCE_DIR}/Doxyfile")
    if(DOXYGEN_EXECUTABLE AND EXISTS "${doxyfile}")
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

# ------------------------------------------------------------------------------
# 2) 스크립트 타겟 — CookPrefabs, Engine 레이어/GLOB 린트
#    CheckEngineLayers: Engine → Editor/GameFramework/Games 금지 include
#    CheckSourceGlob: GLOB 누락 힌트 (compile_commands.json 필요)
# ------------------------------------------------------------------------------
if(NOT Python3_Interpreter_FOUND)
    find_package(Python3 COMPONENTS Interpreter QUIET)
endif()
if(Python3_Interpreter_FOUND)
    add_custom_target(CookPrefabs
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Scripts/generate/CookPrefabs.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Cooking prefab XML (JSON fallback) to PFB2 binary..."
        VERBATIM
    )
    set_target_properties(CookPrefabs PROPERTIES FOLDER "Engine/Scripts")

    add_custom_target(CookScenes
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Scripts/generate/CookScenes.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Cooking scene XML to SCN1 binary..."
        VERBATIM
    )
    set_target_properties(CookScenes PROPERTIES FOLDER "Engine/Scripts")

    add_custom_target(CookAssets
        DEPENDS CookPrefabs CookScenes
        COMMENT "Cooking all scene and prefab assets to binary..."
    )
    set_target_properties(CookAssets PROPERTIES FOLDER "Engine/Scripts")

    add_custom_target(CheckEngineLayers
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Scripts/lint/CheckEngineLayers.py"
            --root "${CMAKE_SOURCE_DIR}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Checking Engine layer include rules..."
        VERBATIM
    )
    set_target_properties(CheckEngineLayers PROPERTIES FOLDER "Engine/Scripts")

    add_custom_target(CheckSourceGlob
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Scripts/lint/CheckSourceGlob.py"
            --root "${CMAKE_SOURCE_DIR}"
            --build "${CMAKE_BINARY_DIR}"
            --active-game "${SW_ACTIVE_GAME}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Checking source GLOB coverage vs compile_commands..."
        VERBATIM
    )
    set_target_properties(CheckSourceGlob PROPERTIES FOLDER "Engine/Scripts")
endif()
