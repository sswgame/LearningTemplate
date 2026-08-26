# ==============================================================================
# @file Source/RuntimeAPI/CMakeLists.txt
# @brief 순수 C-ABI 런타임 인터페이스 (INTERFACE 라이브러리)
# @details ABI/ 계약, Service/ 로케이터, Export/ 모듈 매크로, PluginAPI.h
# ==============================================================================

add_library(RuntimeAPI INTERFACE)
target_include_directories(RuntimeAPI INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(RuntimeAPI INTERFACE Engine)
set_target_properties(RuntimeAPI PROPERTIES FOLDER "Source/RuntimeAPI")
