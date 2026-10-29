# ==============================================================================
# @file Source/EditorFoundation/CMakeLists.txt
# @brief 편집기 토대(개발 빌드 SHARED) — 편집기 모듈(EditorModule · 확장)을 핫 리로드해도 살아남아야 하는 편집기 상태와 OS 도구.
# ==============================================================================

# 되돌리기 스택 · 객체 되돌리기 · 편집 세션 상태 · 복사 버퍼 · 파일 대화상자. Engine 은 이것들을 모른다 — 편집기 기능은 Engine 에 두지 않는다.
# App 이 링크해 프로세스 끝까지 올라와 있고(EditorModule 이 내려가도 DLL 이 남는다), 서비스는 App 이 호스트 범위에 건다(`App::startEditorFoundation`).
# 언리얼 UnrealEd(내리지 않는 편집기 토대)와 편집기 플러그인(리로드하는 위층)의 나눔과 같다. 배포 빌드 · 전용 서버에는 편집기가 없다.
file(GLOB_RECURSE listEditorFoundationSource CONFIGURE_DEPENDS "*.cpp" "*.h")
if(SW_SHIPPING_BUILD OR SW_TARGET_TYPE STREQUAL "Server")
	sw_declareUnbuiltSources(${listEditorFoundationSource})
	return()
endif()
if(NOT WIN32)
	list(FILTER listEditorFoundationSource EXCLUDE REGEX "/Windows/")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(FILTER listEditorFoundationSource EXCLUDE REGEX "/Linux/")
endif()

add_library(EditorFoundation SHARED ${listEditorFoundationSource})
set_target_properties(EditorFoundation PROPERTIES FOLDER "Source/EditorFoundation")
target_link_libraries(EditorFoundation
	PUBLIC
		Engine
	PRIVATE
		sw_global_options
)
target_compile_definitions(EditorFoundation PRIVATE "SW_LOG_TAG=\"Editor\"" SW_EDITOR_FOUNDATION_EXPORTS)
sw_configurePCH(EditorFoundation "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
