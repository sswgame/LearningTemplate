# ==============================================================================
# @file ThirdParty/imgui-notify/CMakeLists.txt
# @brief TyomaVader/ImGuiNotify (header-only) — vcpkg overlay port
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) INTERFACE — vcpkg include/ImGuiNotify.hpp
#    SYSTEM: Editor TU에서 ImGuiNotify.hpp / FontAwesome 경고 억제
#    NOTIFY_RENDER_OUTSIDE_MAIN_WINDOW=false: 메인 뷰포트 기준 (멀티뷰포트 훅 불필요)
# ------------------------------------------------------------------------------
find_path(swImguiNotifyInc NAMES "ImGuiNotify.hpp")

if(NOT swImguiNotifyInc)
	message(FATAL_ERROR "[imgui-notify] Expected headers under vcpkg include dir. Check vcpkg installation.")
endif()

add_library(imgui_notify INTERFACE)

sw_thirdPartySystemIncludes(imgui_notify INTERFACE
	"${swImguiNotifyInc}"
)

target_compile_definitions(imgui_notify INTERFACE
	NOTIFY_RENDER_OUTSIDE_MAIN_WINDOW=false
)

if(TARGET imgui)
	target_link_libraries(imgui_notify INTERFACE imgui)
elseif(TARGET imgui::imgui)
	target_link_libraries(imgui_notify INTERFACE imgui::imgui)
endif()
