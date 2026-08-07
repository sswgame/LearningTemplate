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
# @brief DXC DLL 경로를 찾아 타겟 런타임 복사 큐에 등록 (sw_emit_runtime_copies로 출력)
# @param TARGET_NAME 복사 대상 타겟 이름
# ==============================================================================
function(sw_copy_dxc_dlls TARGET_NAME)
    set(_dxc_dll "")
    set(_dxil_dll "")

    # 1) vcpkg directx-dxc (SPIR-V CodeGen 포함 — 설치되어 있으면 우선)
    if(COMMAND sw_get_vcpkg_paths)
        sw_get_vcpkg_paths(_dxc_inc_dirs _dxc_bin_dirs)
        foreach(_dir IN LISTS _dxc_bin_dirs)
            if(_dxc_dll STREQUAL "" AND EXISTS "${_dir}/dxcompiler.dll")
                set(_dxc_dll "${_dir}/dxcompiler.dll")
            endif()
            if(_dxil_dll STREQUAL "" AND EXISTS "${_dir}/dxil.dll")
                set(_dxil_dll "${_dir}/dxil.dll")
            endif()
        endforeach()
        # tools/directx-dxc 에도 둘 수 있음
        if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
            set(_dxc_tools "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc")
            if(_dxc_dll STREQUAL "" AND EXISTS "${_dxc_tools}/dxcompiler.dll")
                set(_dxc_dll "${_dxc_tools}/dxcompiler.dll")
            endif()
            if(_dxil_dll STREQUAL "" AND EXISTS "${_dxc_tools}/dxil.dll")
                set(_dxil_dll "${_dxc_tools}/dxil.dll")
            endif()
        endif()
    endif()

    # 2) vcpkg가 DIRECTX_DXC_TOOL(dxc.exe)을 노출한 경우 같은 폴더의 DLL 사용
    if(_dxc_dll STREQUAL "" AND DEFINED DIRECTX_DXC_TOOL AND EXISTS "${DIRECTX_DXC_TOOL}")
        get_filename_component(_dxc_tool_dir "${DIRECTX_DXC_TOOL}" DIRECTORY)
        if(EXISTS "${_dxc_tool_dir}/dxcompiler.dll")
            set(_dxc_dll "${_dxc_tool_dir}/dxcompiler.dll")
        endif()
        if(_dxil_dll STREQUAL "" AND EXISTS "${_dxc_tool_dir}/dxil.dll")
            set(_dxil_dll "${_dxc_tool_dir}/dxil.dll")
        endif()
    endif()

    # 3) Config/engine_config.json fallback (Windows SDK 등)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Config/engine_config.json")
        file(READ "${CMAKE_SOURCE_DIR}/Config/engine_config.json" _cfg_content)
        if(_dxc_dll STREQUAL "")
            string(JSON _cfg_dxc ERROR_VARIABLE _e1 GET "${_cfg_content}" "dxc_dll_path")
            if(_cfg_dxc AND EXISTS "${_cfg_dxc}")
                set(_dxc_dll "${_cfg_dxc}")
            endif()
        endif()
        if(_dxil_dll STREQUAL "")
            string(JSON _cfg_dxil ERROR_VARIABLE _e2 GET "${_cfg_content}" "dxil_dll_path")
            if(_cfg_dxil AND EXISTS "${_cfg_dxil}")
                set(_dxil_dll "${_cfg_dxil}")
            endif()
        endif()
    endif()

    if(_dxc_dll AND EXISTS "${_dxc_dll}")
        sw_queue_runtime_copy(${TARGET_NAME} "${_dxc_dll}")
    endif()

    if(_dxil_dll AND EXISTS "${_dxil_dll}")
        sw_queue_runtime_copy(${TARGET_NAME} "${_dxil_dll}")
    endif()
endfunction()
