# ==============================================================================
# @file cmake/internal/Install.cmake
# @brief 엔진 내부: install() 헬퍼
# ==============================================================================

function(sw_install_target TARGET_NAME)
    install(
        TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION Bin
        LIBRARY DESTINATION Lib
        ARCHIVE DESTINATION Lib
    )
endfunction()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Resource")
    install(DIRECTORY Resource DESTINATION .)
endif()
