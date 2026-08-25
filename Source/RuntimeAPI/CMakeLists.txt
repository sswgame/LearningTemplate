add_library(RuntimeAPI INTERFACE)
target_include_directories(RuntimeAPI INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(RuntimeAPI INTERFACE Engine)
