# ==============================================================================
# @file ThirdParty/vulkan/CMakeLists.txt
# @brief Vulkan API 헤더 외부 라이브러리 연동 스크립트
# ==============================================================================

# vulkan_headers INTERFACE 타겟 래퍼 생성
add_library(vulkan_headers INTERFACE)

# 시스템 또는 vcpkg의 Vulkan SDK 검색
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    if(TARGET Vulkan::Vulkan)
        target_link_libraries(vulkan_headers INTERFACE Vulkan::Vulkan)
    elseif(TARGET Vulkan::Headers)
        target_link_libraries(vulkan_headers INTERFACE Vulkan::Headers)
    endif()
else()
    sw_link_vcpkg_header_only_target(vulkan_headers)
endif()

# 전역 서드파티 인클루드 타겟에 연결
if(TARGET sw_third_party_includes)
    target_link_libraries(sw_third_party_includes INTERFACE vulkan_headers)
endif()
