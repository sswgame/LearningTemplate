# ==============================================================================
# @file ThirdParty/rapidxml/CMakeLists.txt
# @brief RapidXML (XML 파서) 외부 헤더 온리 라이브러리 연동 스크립트
# ==============================================================================

# rapidxml INTERFACE 타겟 및 ALIAS 생성
add_library(rapidxml INTERFACE)
add_library(rapidxml::rapidxml ALIAS rapidxml)

# vcpkg 설치 경로 또는 시스템 경로에서 rapidxml 헤더 탐색
find_path(RAPIDXML_INCLUDE_DIRS "rapidxml/rapidxml.hpp"
    PATHS
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
        "${sw_vcpkg_root}/installed/${VCPKG_TARGET_TRIPLET}/include"
    NO_DEFAULT_PATH
)

if(RAPIDXML_INCLUDE_DIRS)
    # 시스템 디렉토리로 포함시켜 컴파일러 경고 제외 처리
    target_include_directories(rapidxml SYSTEM INTERFACE "${RAPIDXML_INCLUDE_DIRS}")
    target_include_directories(rapidxml SYSTEM INTERFACE "${RAPIDXML_INCLUDE_DIRS}/rapidxml")
else()
    target_include_directories(rapidxml SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# 전역 서드파티 인클루드 타겟에 추가
if(TARGET sw_third_party_includes)
    target_link_libraries(sw_third_party_includes INTERFACE rapidxml)
endif()
