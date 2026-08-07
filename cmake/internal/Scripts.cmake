# ==============================================================================
# @file cmake/internal/Scripts.cmake
# @brief 엔진 내부: 보조 빌드 타겟 (changelog 등)
# ==============================================================================

# SW_AUTO_CHANGELOG is declared in cmake/UserConfig.cmake (default OFF).
if(SW_AUTO_CHANGELOG)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_target(AutoChangelog
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/GenerateChangelog.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Auto-Changelog via Git history..."
        VERBATIM
    )
    set_target_properties(AutoChangelog PROPERTIES FOLDER "Engine/Scripts")
endif()
