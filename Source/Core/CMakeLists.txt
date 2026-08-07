# ==============================================================================
# @file Source/Core/CMakeLists.txt
# @brief 코어 모듈(RHI, Reflection, Object, Utility, Window 등) 빌드 타겟 스크립트
# @details
# 폴더(Common/Reflection/Utility/...)는 소스 구성을 위한 것이며,
# 모듈 간 순환 참조(Reflection ↔ Graphics 등) 때문에
# 단일 SHARED 라이브러리로 링크합니다.
# ==============================================================================

# 개발: SHARED DLL / 배포(Release): STATIC
if(SW_SHIPPING_BUILD)
    set(_core_lib_type STATIC)
else()
    set(_core_lib_type SHARED)
endif()

# RHI MODULE 엔트리는 Core에 넣지 않음. SW_RHI_AS_MODULES=ON 이면 DX 디바이스도 분리.
set(_core_exclude
    "Graphics/RHI/Modules/"
)
if(SW_RHI_AS_MODULES)
    list(APPEND _core_exclude
        "Graphics/RHI/DX11/.*\\.cpp"
        "Graphics/RHI/DX12/.*\\.cpp"
    )
endif()

sw_add_library(Core ${_core_lib_type}
    EXCLUDE ${_core_exclude}
)

# ThirdParty include/link는 PUBLIC: Core 헤더(glad 등)와 Editor의 imgui가 종속 타겟에 전파되어야 함
if(sw_libraries)
    target_link_libraries(Core PUBLIC ${sw_libraries})
endif()

sw_configure_core_dll_exports(Core ${_core_lib_type})
sw_set_log_tag(Core Engine)

if(SW_RHI_AS_MODULES)
    target_compile_definitions(Core PRIVATE SW_RHI_AS_MODULES)
    message(STATUS "[Core] SW_RHI_AS_MODULES=ON — DX11/DX12 device sources excluded; loading RHI_*.dll")
endif()

if(sw_enable_pch AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
    target_precompile_headers(Core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
endif()

# Optional RHI backend MODULE DLLs (Windows DX11/DX12 first)
if(SW_RHI_AS_MODULES)
    add_subdirectory(Graphics/RHI/Modules)
endif()
