# ==============================================================================
# @file ThirdParty/CMakeLists.txt
# @brief 서드파티 래퍼 — **목록이 없다.** CMakeLists.txt 가 있는 하위 폴더를 이름 순서로 들어간다
#        (imgui 가 imgui-* 보다 먼저 — 이름 순서가 곧 의존 순서다). 조건이 있는 래퍼는 그 폴더의 CMakeLists 머리가 스스로 거른다.
# ==============================================================================

file(GLOB list_third_party_directory LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT list_third_party_directory)
foreach(third_party_directory IN LISTS list_third_party_directory)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${third_party_directory}/CMakeLists.txt")
		add_subdirectory(${third_party_directory})
	endif()
endforeach()
