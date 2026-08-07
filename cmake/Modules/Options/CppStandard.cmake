# C++ standard INTERFACE module.

add_library(sw_cpp_standard INTERFACE)
target_compile_features(sw_cpp_standard INTERFACE cxx_std_${sw_cpp_standard})

set(CMAKE_CXX_STANDARD ${sw_cpp_standard})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND sw_flag_libraries sw_cpp_standard)
