# ==============================================================================
# @file ThirdParty/imgui/CMakeLists.txt
# @brief 에디터 GUI 프레임워크(ImGui) 외부 라이브러리 연동 스크립트
# ==============================================================================

# vcpkg를 통해 imgui 패키지 검색
find_package(imgui CONFIG QUIET)

# INTERFACE 타겟 래퍼를 생성하여 imgui 의존성 투영
if(TARGET imgui::imgui)
    add_library(imgui INTERFACE)
    target_link_libraries(imgui INTERFACE imgui::imgui)
else()
    add_library(imgui INTERFACE)
    add_library(imgui::imgui ALIAS imgui)

    sw_link_vcpkg_header_only_target(imgui)
endif()

# 전역 서드파티 인클루드 타겟에 연결
if(TARGET sw_third_party_includes)
    target_link_libraries(sw_third_party_includes INTERFACE imgui)
endif()
