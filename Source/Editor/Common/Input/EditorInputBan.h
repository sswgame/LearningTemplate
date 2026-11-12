/**
 * @file EditorInputBan.h
 * @brief 에디터 코드 전체(에디터 모듈의 모든 부분 · 에디터 확장 모듈)에서 imgui.h 끝에 붙는 헤더입니다. ImGui 입력 · 팝업 · 끌어 놓기 함수와 노드 캔버스 지우기 구간을 `[[deprecated]]` 로 다시 선언합니다.
 * @details CMake 가 ImGui 의 사용자 헤더 자리(`IMGUI_INCLUDE_IMGUI_USER_H` + `IMGUI_USER_H_FILENAME`)에 이 파일을 두고 사용 중단 경고를 오류로 올린다
 *          (`sw_applyEditorInputBan`, 옵션 `SW_EDITOR_INPUT_BAN` 기본 ON). imgui.h 가 선언을 다 마친 뒤에 붙으므로 이 파일은 imgui.h 를 include 하지 않는다.
 *          에디터 코드가 아래 함수를 부르면 컴파일이 멈춘다. 대신 쓸 곳: 단축키 · 단추 · 몸짓은 에디터 입력 맵(`EditorInputActions`, `editor.input.xml`),
 *          맥락 메뉴 · 끌어 놓기 · 포인터 · 글 칸의 편집 키는 `EditorInputGUI`(`Common/Input`).
 *          금지를 받지 않는 곳은 ImGui 를 직접 부르는 두 파일뿐이다 — 어댑터(`Common/Input/EditorInputGUI.cpp`)와 ImGui 키를 엔진 입력으로 옮기는
 *          `Common/Backend/ImGuiKeyForward.cpp`. 둘에만 CMake 가 소스 속성으로 `SW_EDITOR_INPUT_ADAPTER=1` 을 준다(`sw_applyEditorInputBan`, 유니티 묶음 · PCH 에서 빠진다).
 *          나머지 파일은 강제 포함한 `EditorInputBanGate.h` 가 0 으로 정해 두어, 소스 안에서 1 로 정의하면 다시 정의 오류(`-Werror=macro-redefined`)로 멈춘다.
 *          imgui_internal.h 의 다른 겹(키 주인 · 입력 플래그를 받는 것)과 그 밖의 여는 길(`OpenPopupEx` · `Shortcut` · `BeginDragDropTargetCustom` …)도 막는다.
 *          C++ 는 앞서 선언한 함수를 속성을 붙여 다시 선언하는 것을 허용한다(기본 인자는 다른 선언의 것을 쓰므로 여기서 적지 않는다).
 */
#pragma once

// imgui.h 끝에서만 뜻이 있다(선언을 다시 하므로). 그 밖에서 읽히면(헤더 단독 검사) · 어댑터 파일이면 아무것도 하지 않는다.
#if defined( IMGUI_VERSION ) && SW_EDITOR_INPUT_ADAPTER == 0

    #include "Core/Foundation/Types.h"

    #define SW_EDITOR_INPUT_BAN_MESSAGE "editor code does not read ImGui input directly - use the editor input map (EditorInputActions) or EditorInputGUI (Common/Input)"

struct ImRect; // imgui_internal.h 의 사각형(BeginDragDropTargetCustom)

namespace ImGui
{
    // imgui.h
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsKeyPressed( ImGuiKey key, bool repeat );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsKeyDown( ImGuiKey key );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsKeyReleased( ImGuiKey key );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsKeyChordPressed( ImGuiKeyChord key_chord );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                Shortcut( ImGuiKeyChord key_chord, ImGuiInputFlags flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsMouseClicked( ImGuiMouseButton button, bool repeat );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsMouseReleased( ImGuiMouseButton button );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsMouseDoubleClicked( ImGuiMouseButton button );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsItemClicked( ImGuiMouseButton mouse_button );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                BeginPopupContextItem( const utf8* str_id, ImGuiPopupFlags popup_flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                BeginPopupContextWindow( const utf8* str_id, ImGuiPopupFlags popup_flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                BeginPopupContextVoid( const utf8* str_id, ImGuiPopupFlags popup_flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                OpenPopup( const utf8* str_id, ImGuiPopupFlags popup_flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                OpenPopup( ImGuiID id, ImGuiPopupFlags popup_flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                OpenPopupOnItemClick( const utf8* str_id, ImGuiPopupFlags popup_flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                BeginDragDropSource( ImGuiDragDropFlags flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                SetDragDropPayload( const utf8* type, const void* data, size_t sz, ImGuiCond cond );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                BeginDragDropTarget();
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API const ImGuiPayload* AcceptDragDropPayload( const utf8* type, ImGuiDragDropFlags flags );

    // imgui_internal.h 의 다른 겹 — 키 주인(owner_id) · 입력 플래그를 받는다. 이 선언이 먼저라 뒤의 선언이 기본 인자를 더해도 사용 중단은 남는다.
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool IsKeyDown( ImGuiKey key, ImGuiID owner_id );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool IsKeyPressed( ImGuiKey key, ImGuiInputFlags flags, ImGuiID owner_id );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool IsKeyReleased( ImGuiKey key, ImGuiID owner_id );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool IsKeyChordPressed( ImGuiKeyChord key_chord, ImGuiInputFlags flags, ImGuiID owner_id );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool Shortcut( ImGuiKeyChord key_chord, ImGuiInputFlags flags, ImGuiID owner_id );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool IsMouseClicked( ImGuiMouseButton button, ImGuiInputFlags flags, ImGuiID owner_id );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool IsMouseReleased( ImGuiMouseButton button, ImGuiID owner_id );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool IsMouseDoubleClicked( ImGuiMouseButton button, ImGuiID owner_id );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool OpenPopupEx( ImGuiID id, ImGuiPopupFlags popup_flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool BeginDragDropTargetCustom( const ImRect& bb, ImGuiID id );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool BeginDragDropTargetViewport( ImGuiViewport* viewport, const ImRect* p_bb );
} // namespace ImGui

#endif // defined( IMGUI_VERSION ) && SW_EDITOR_INPUT_ADAPTER == 0

// 노드 캔버스 지우기 구간(imgui-node-editor `BeginDelete`)은 `EditorNodeGraph::collectRemovals` 한 곳만 연다 — 패널마다 손 구간을 두면 링크 거절 · 주석 노드 가르기가
// 갈린다. 이 선언이 imgui_node_editor.h 의 선언보다 먼저라(그 헤더가 imgui.h 를 먼저 끌어 온다) 뒤의 선언도 사용 중단을 잇는다. 정적 라이브러리라 API 매크로는 비어 있다.
// 받지 않는 곳은 `Common/Widgets/EditorNodeGraph.cpp` 하나다 — CMake 가 그 파일에만 `SW_EDITOR_NODE_GRAPH_ADAPTER=1` 을 준다(`EditorInputBanGate.h` 와 같은 방식).
#if defined( IMGUI_VERSION ) && SW_EDITOR_NODE_GRAPH_ADAPTER == 0
namespace ax::NodeEditor
{
    [[deprecated( "editor code does not open the node canvas delete pass - use EditorNodeGraph::collectRemovals" )]] bool BeginDelete();
} // namespace ax::NodeEditor
#endif // defined( IMGUI_VERSION ) && SW_EDITOR_NODE_GRAPH_ADAPTER == 0
