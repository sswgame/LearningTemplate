# ==============================================================================
# @file Source/Core/CMakeLists.txt
# @brief 코어 모듈(RHI, Reflection, Object, Utility, Window 등) 빌드 타겟 스크립트
# @details
# 폴더(Common/Reflection/Utility/...)는 소스 구성을 위한 것이며,
# 모듈 간 순환 참조(Reflection ↔ Graphics 등) 때문에
# 단일 SHARED 라이브러리로 링크합니다.
# ==============================================================================

# 개발: SHARED DLL / Shipping(SW_SHIPPING_BUILD): STATIC
if(SW_SHIPPING_BUILD)
    set(_core_lib_type STATIC)
else()
    set(_core_lib_type SHARED)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/Graphics/RHI/RhiSources.cmake")

# RHI MODULE 엔트리(Modules/)는 항상 Core GLOB에서 제외.
# SW_RHI_AS_MODULES=ON 이면 device .cpp 도 Core에서 빼고 RHI_* 가 동일 리스트를 사용.
set(_core_exclude
    "Graphics/RHI/Modules/"
)
if(SW_RHI_AS_MODULES)
    foreach(_src IN LISTS SW_RHI_ALL_DEVICE_SOURCES)
        file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_src}")
        string(REPLACE "." "\\." _rel_esc "${_rel}")
        list(APPEND _core_exclude "${_rel_esc}")
    endforeach()
endif()

sw_add_library(Core
    TYPE ${_core_lib_type}
    EXCLUDE ${_core_exclude}
)

# "Core/..." · Resource 경로는 Core를 링크하는 모든 타겟에 PUBLIC 전파
target_include_directories(Core
	PUBLIC
		"${CMAKE_SOURCE_DIR}/Source"
		"${CMAKE_SOURCE_DIR}/Resource"
)

# ThirdParty include/link는 PUBLIC: Core 헤더(glad 등)와 Editor의 imgui가 종속 타겟에 전파되어야 함
if(TARGET sw_third_party_includes)
    target_link_libraries(Core PUBLIC sw_third_party_includes)
endif()

# GPU/system graphics — not in sw_flag_libraries (tools/tests stay clean)
if(TARGET sw_graphics_libs)
    target_link_libraries(Core PUBLIC sw_graphics_libs)
endif()

sw_configure_core_dll_exports(Core ${_core_lib_type})
sw_set_log_tag(Core Engine)

if(SW_RHI_AS_MODULES)
    target_compile_definitions(Core PRIVATE SW_RHI_AS_MODULES)
    message(STATUS "[Core] SW_RHI_AS_MODULES=ON — RHI device sources excluded; loading RHI_* modules")
else()
    message(STATUS "[Core] SW_RHI_AS_MODULES=OFF — RHI device sources linked into Core")
endif()

sw_configure_pch(Core)

# Optional RHI backend MODULE plugins (DX Windows-only; GL/Vulkan all platforms)
if(SW_RHI_AS_MODULES)
    add_subdirectory(Graphics/RHI/Modules)
endif()
