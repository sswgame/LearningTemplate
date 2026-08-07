# Unity build flags (optional).

if(NOT sw_enable_unity_build)
    return()
endif()

add_library(sw_unity_build INTERFACE)

set(CMAKE_UNITY_BUILD ON)
set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
set(CMAKE_UNITY_BUILD_UNIQUE_ID_VAR "UNITY_BUILD_UNIQUE_ID")

list(APPEND sw_flag_libraries sw_unity_build)
