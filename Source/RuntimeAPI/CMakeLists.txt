# ==============================================================================
# @file Source/RuntimeAPI/CMakeLists.txt
# @brief 순수 C-ABI 런타임 인터페이스 (INTERFACE 라이브러리)
# ==============================================================================

add_library(RuntimeAPI INTERFACE)
target_include_directories(RuntimeAPI INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(RuntimeAPI INTERFACE Engine)
set_target_properties(RuntimeAPI PROPERTIES FOLDER "Source/RuntimeAPI")
