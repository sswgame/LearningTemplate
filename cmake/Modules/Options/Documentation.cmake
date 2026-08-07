# SW_BUILD_DOCS is declared in cmake/UserConfig.cmake

if(SW_BUILD_DOCS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_program(DOXYGEN_EXECUTABLE doxygen)

    set(_doxyfile "${CMAKE_SOURCE_DIR}/Doxyfile")
    if(DOXYGEN_EXECUTABLE AND EXISTS "${_doxyfile}")
        set(GENERATE_DOCS_SCRIPT "${CMAKE_SOURCE_DIR}/Scripts/GenerateDocs.py")

        add_custom_target(GenerateDocs
            COMMAND ${Python3_EXECUTABLE} ${GENERATE_DOCS_SCRIPT}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Generating Doxygen Documentation..."
        )

        message(STATUS "[Docs] Doxygen Documentation Target Enabled.")
    else()
        message(STATUS "[Docs] Skipping docs target (doxygen or Doxyfile missing).")
    endif()
endif()
