/**
 * @file EditorInputBan.h
 * @brief 패널 티어(Panels · AssetActions · Viewport)에서 imgui.h 끝에 붙는 헤더입니다. ImGui 입력 · 팝업 · 끌어 놓기 함수를 `[[deprecated]]` 로 다시 선언합니다.
 * @details 이 티어들은 `Source/Editor/CMakeLists.txt` 가 ImGui 의 사용자 헤더 자리(`IMGUI_INCLUDE_IMGUI_USER_H` + `IMGUI_USER_H_FILENAME`)에 이 파일을 두고
 *          사용 중단 경고를 오류로 올린다(`SW_EDITOR_INPUT_BAN`, 남은 호출이 0 이 된 티어부터 켠다 — EditorUX 10-5 I4). imgui.h 가 선언을 다 마친 뒤에 붙으므로
 *          이 파일은 imgui.h 를 include 하지 않는다(프로젝트 헤더가 서드파티 헤더를 include 하지 않는 규칙과도 맞는다). 패널이 imgui.h 를 include 하는 순간 금지가 따라온다.
 *          패널이 아래 함수를 부르면 컴파일이 멈춘다. 대신 쓸 곳: 단축키 · 단추 · 몸짓은 에디터 입력 맵(`EditorInputActions`, `editor.input.xml`),
 *          맥락 메뉴 · 끌어 놓기는 `EditorInputGUI`(`Common/Input`). 어댑터 자신(`Common/Input`, 티어 4)과 OS 입력을 ImGui 로 넣는 `Common/Backend` 는 이 헤더를 받지 않는다.
 *          형식으로 먼저 막는다 — 린트(`CheckEditorInputCalls`)는 함수 재선언으로 막지 못하는 `ImGuiIO` 입력 필드 읽기만 센다.
 *          C++ 는 앞서 선언한 함수를 속성을 붙여 다시 선언하는 것을 허용한다(기본 인자는 첫 선언의 것을 쓰므로 여기서 다시 적지 않는다).
 */
#pragma once

// imgui.h 끝에서만 뜻이 있다(선언을 다시 하므로). 그 밖에서 읽히면(헤더 단독 검사) 아무것도 하지 않는다.
#if defined( IMGUI_VERSION )

    #include "Core/Common/Types.h"

    #define SW_EDITOR_INPUT_BAN_MESSAGE "panels do not read ImGui input directly - use the editor input map (EditorInputActions) or EditorInputGUI (Common/Input)"

namespace ImGui
{
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsKeyPressed( ImGuiKey key, bool repeat );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsKeyDown( ImGuiKey key );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsKeyChordPressed( ImGuiKeyChord key_chord );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsMouseClicked( ImGuiMouseButton button, bool repeat );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsMouseReleased( ImGuiMouseButton button );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsMouseDoubleClicked( ImGuiMouseButton button );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                IsItemClicked( ImGuiMouseButton mouse_button );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                BeginPopupContextItem( const utf8* str_id, ImGuiPopupFlags popup_flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                BeginPopupContextWindow( const utf8* str_id, ImGuiPopupFlags popup_flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                OpenPopup( const utf8* str_id, ImGuiPopupFlags popup_flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                OpenPopup( ImGuiID id, ImGuiPopupFlags popup_flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                BeginDragDropSource( ImGuiDragDropFlags flags );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                SetDragDropPayload( const utf8* type, const void* data, size_t sz, ImGuiCond cond );
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API bool                BeginDragDropTarget();
    [[deprecated( SW_EDITOR_INPUT_BAN_MESSAGE )]] IMGUI_API const ImGuiPayload* AcceptDragDropPayload( const utf8* type, ImGuiDragDropFlags flags );
} // namespace ImGui

#endif // defined( IMGUI_VERSION )
