# ==============================================================================
# @file ThirdParty/nlohmann-json/CMakeLists.txt
# @brief nlohmann/json — 헤더 온리. 엔진 코드는 JsonDocument만 include
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) INTERFACE — vcpkg include (nlohmann/json.hpp), 없으면 이 폴더의 nlohmann/ 폴백
#    엔진 TU는 JsonDocument.cpp 만 <nlohmann/json.hpp>를 include 한다.
# ------------------------------------------------------------------------------
add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
if(COMMAND sw_linkVcpkgHeaderOnlyTarget)
	sw_linkVcpkgHeaderOnlyTarget(nlohmann_json)
else()
	target_include_directories(nlohmann_json SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(TARGET sw_third_party_includes)
	target_link_libraries(sw_third_party_includes INTERFACE nlohmann_json)
endif()
