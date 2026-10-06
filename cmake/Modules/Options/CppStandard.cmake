# ==============================================================================
# @file cmake/Modules/Options/CppStandard.cmake
# @brief C++ 표준 INTERFACE (최소 C++17, 그 이상은 캐시 옵션 SW_CPP_STANDARD — cmake/Config/BuildOptions.cmake)
# ==============================================================================

if(SW_CPP_STANDARD LESS 17)
	message(FATAL_ERROR "SW_CPP_STANDARD must be >= 17 (got ${SW_CPP_STANDARD})")
endif()

add_library(sw_cpp_standard INTERFACE)
target_compile_features(sw_cpp_standard INTERFACE cxx_std_${SW_CPP_STANDARD})

set(CMAKE_CXX_STANDARD ${SW_CPP_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND sw_flag_libraries sw_cpp_standard)
