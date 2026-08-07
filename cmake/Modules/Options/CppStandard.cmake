# C++ standard INTERFACE module (C++17 minimum; higher via sw_cpp_standard).

if(NOT DEFINED sw_cpp_standard)
	set(sw_cpp_standard 17)
endif()
if(sw_cpp_standard LESS 17)
	message(FATAL_ERROR "sw_cpp_standard must be >= 17 (got ${sw_cpp_standard})")
endif()

add_library(sw_cpp_standard INTERFACE)
target_compile_features(sw_cpp_standard INTERFACE cxx_std_${sw_cpp_standard})

set(CMAKE_CXX_STANDARD ${sw_cpp_standard})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND sw_flag_libraries sw_cpp_standard)
