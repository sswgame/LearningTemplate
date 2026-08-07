# ==============================================================================
# @file ThirdParty/glad/CMakeLists.txt
# @brief OpenGL 확장(GLAD) 로더 외부 라이브러리 연동 스크립트
# ==============================================================================

# vcpkg를 통해 glad 패키지 검색
find_package(glad CONFIG QUIET)

# INTERFACE 타겟 래퍼를 생성하여 glad 의존성 투영
if(TARGET glad::glad)
    add_library(glad INTERFACE)
    target_link_libraries(glad INTERFACE glad::glad)
else()
    add_library(glad INTERFACE)
    add_library(glad::glad ALIAS glad)

    sw_link_vcpkg_header_only_target(glad)
endif()

# 전역 서드파티 인클루드 타겟에 연결하여 사용자 모듈에서 #include 처리 지원
if(TARGET sw_third_party_includes)
    target_link_libraries(sw_third_party_includes INTERFACE glad)
endif()
