# ==============================================================================
# @file Source/Runtime/CMakeLists.txt
# @brief App ↔ Editor/Game 모듈 공유 Runtime API (헤더 전용 INTERFACE)
# ==============================================================================

add_library(RuntimeAPI INTERFACE)
target_include_directories(RuntimeAPI INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/Source"
)
target_link_libraries(RuntimeAPI INTERFACE Core)

file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
get_filename_component(folder_path "${rel_path}" DIRECTORY)
if(folder_path)
    set_target_properties(RuntimeAPI PROPERTIES FOLDER "${folder_path}")
endif()
