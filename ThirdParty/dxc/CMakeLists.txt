# ==============================================================================
# @file ThirdParty/dxc/CMakeLists.txt
# @brief DirectX Shader Compiler (DXC) 외부 라이브러리 연동 스크립트 및 DLL 복사 유틸리티
# ==============================================================================

# vcpkg를 통해 directx-dxc 패키지 검색
find_package(directx-dxc CONFIG QUIET)

# 발견된 타겟 이름에 따라 dxc INTERFACE 라이브러리 생성 및 링크
if(TARGET Microsoft::DirectXShaderCompiler)
    add_library(dxc INTERFACE)
    target_link_libraries(dxc INTERFACE Microsoft::DirectXShaderCompiler)
elseif(TARGET directx-dxc::directx-dxc)
    add_library(dxc INTERFACE)
    target_link_libraries(dxc INTERFACE directx-dxc::directx-dxc)
else()
    add_library(dxc INTERFACE)
    add_library(dxc::dxc ALIAS dxc)

    sw_link_vcpkg_header_only_target(dxc)
endif()

# 전역 서드파티 인클루드 타겟에 추가
if(TARGET sw_third_party_includes)
    target_link_libraries(sw_third_party_includes INTERFACE dxc)
endif()

# ==============================================================================
# @function sw_copy_dxc_dlls
# @brief 엔진 설정 JSON에서 DXC 관련 DLL 경로를 읽어 타겟 빌드 출력 폴더로 복사
# @param TARGET_NAME 복사 대상 타겟 이름
# ==============================================================================
function(sw_copy_dxc_dlls TARGET_NAME)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Config/engine_config.json")
        file(READ "${CMAKE_SOURCE_DIR}/Config/engine_config.json" _cfg_content)
        string(JSON DXCOMPILER_DLL ERROR_VARIABLE _e1 GET "${_cfg_content}" "dxc_dll_path")
        string(JSON DXIL_DLL ERROR_VARIABLE _e2 GET "${_cfg_content}" "dxil_dll_path")
    endif()

    if(DXCOMPILER_DLL AND EXISTS "${DXCOMPILER_DLL}")
        get_filename_component(_dxc_name "${DXCOMPILER_DLL}" NAME)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DXCOMPILER_DLL}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/${_dxc_name}"
            COMMENT "[${TARGET_NAME}] Copying ${_dxc_name}..."
            VERBATIM
        )
    endif()

    if(DXIL_DLL AND EXISTS "${DXIL_DLL}")
        get_filename_component(_dxil_name "${DXIL_DLL}" NAME)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DXIL_DLL}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/${_dxil_name}"
            COMMENT "[${TARGET_NAME}] Copying ${_dxil_name}..."
            VERBATIM
        )
    endif()
endfunction()
