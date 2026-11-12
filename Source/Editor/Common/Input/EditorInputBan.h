/**
 * @file EditorInputBan.h
 * @brief 에디터 코드 전체(에디터 모듈의 모든 부분 · 에디터 확장 모듈)에서 imgui.h 끝에 붙는 헤더입니다. ImGui 입력 · 팝업 · 끌어 놓기 함수와 이름표 받는 입력 위젯(아래 절)을 `[[deprecated]]` 로 다시 선언합니다.
 * @details CMake 가 ImGui 의 사용자 헤더 자리(`IMGUI_INCLUDE_IMGUI_USER_H` + `IMGUI_USER_H_FILENAME`)에 이 파일을 두고 사용 중단 경고를 오류로 올린다
 *          (`sw_applyEditorInputBan`, 옵션 `SW_EDITOR_INPUT_BAN` 기본 ON). imgui.h 가 선언을 다 마친 뒤에 붙으므로 이 파일은 imgui.h 를 include 하지 않는다.
 *          에디터 코드가 아래 함수를 부르면 컴파일이 멈춘다. 대신 쓸 곳: 단축키 · 단추 · 몸짓은 에디터 입력 맵(`EditorInputActions`, `editor.input.xml`),
 *          맥락 메뉴 · 끌어 놓기 · 포인터 · 글 칸의 편집 키는 `EditorInputGUI`(`Common/Input`).
 *          금지를 받지 않는 곳은 ImGui 를 직접 부르는 두 파일뿐이다 — 어댑터(`Common/Input/EditorInputGUI.cpp`)와 ImGui 키를 엔진 입력으로 옮기는
 *          `Common/Backend/ImGuiKeyForward.cpp`. 둘은 imgui.h 를 include 하기 전에 `SW_EDITOR_INPUT_ADAPTER` 를 정의하고 유니티 묶음에서 빠진다(CMake).
 *          그 정의가 다른 곳에 생기면 린트가 실패한다 — 매크로는 형식으로 폴더에 묶을 수 없어서다.
 *          imgui_internal.h 의 다른 겹(키 주인 · 입력 플래그를 받는 것)과 그 밖의 여는 길(`OpenPopupEx` · `Shortcut` · `BeginDragDropTargetCustom` …)도 막는다.
 *          C++ 는 앞서 선언한 함수를 속성을 붙여 다시 선언하는 것을 허용한다(기본 인자는 다른 선언의 것을 쓰므로 여기서 적지 않는다).
 */
#pragma once

// imgui.h 끝에서만 뜻이 있다(선언을 다시 하므로). 그 밖에서 읽히면(헤더 단독 검사) · 어댑터 파일이면 아무것도 하지 않는다.
#if defined( IMGUI_VERSION )
    #include "Core/Foundation/Types.h"
#endif

#if defined( IMGUI_VERSION ) && !defined( SW_EDITOR_INPUT_ADAPTER )

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

#endif // defined( IMGUI_VERSION ) && !defined( SW_EDITOR_INPUT_ADAPTER )

// 입력 위젯 — 에디터 코드는 ImGui 의 이름표 받는 입력 위젯을 직접 부르지 않는다. `EditorWidgets` 의 감싼 함수가 숨은 id 형식(`EditorWidgetID`)만 받아
// 보이는 이름표가 칸 오른쪽에 붙을 수 없다. 감싼 함수를 정의하는 `Common/Widgets/EditorWidgets.cpp` 만 `SW_EDITOR_WIDGET_ADAPTER` 를 정의해 이 절을 받지 않는다.
#if defined( IMGUI_VERSION ) && !defined( SW_EDITOR_WIDGET_ADAPTER )

    #define SW_EDITOR_WIDGET_BAN_MESSAGE "editor code does not call ImGui input widgets directly - use EditorWidgets::inputInt / dragFloat / sliderFloat / combo / beginCombo ... with a '##' EditorWidgetID and draw the label first"

namespace ImGui
{
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool BeginCombo( const utf8* label, const utf8* preview_value, ImGuiComboFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool Combo( const utf8* label, int* current_item, const utf8* const items[], int items_count, int popup_max_height_in_items );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool Combo( const utf8* label, int* current_item, const utf8* items_separated_by_zeros, int popup_max_height_in_items );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool Combo( const utf8* label, int* current_item, const utf8* ( *getter )( void* user_data, int idx ), void* user_data, int items_count, int popup_max_height_in_items );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool DragFloat( const utf8* label, float* v, float v_speed, float v_min, float v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool DragFloat2( const utf8* label, float v[2], float v_speed, float v_min, float v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool DragFloat3( const utf8* label, float v[3], float v_speed, float v_min, float v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool DragFloat4( const utf8* label, float v[4], float v_speed, float v_min, float v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool DragFloatRange2( const utf8* label, float* v_current_min, float* v_current_max, float v_speed, float v_min, float v_max, const utf8* format, const utf8* format_max, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool DragInt( const utf8* label, int* v, float v_speed, int v_min, int v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool DragInt2( const utf8* label, int v[2], float v_speed, int v_min, int v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool DragInt3( const utf8* label, int v[3], float v_speed, int v_min, int v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool DragInt4( const utf8* label, int v[4], float v_speed, int v_min, int v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool DragIntRange2( const utf8* label, int* v_current_min, int* v_current_max, float v_speed, int v_min, int v_max, const utf8* format, const utf8* format_max, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool DragScalar( const utf8* label, ImGuiDataType data_type, void* p_data, float v_speed, const void* p_min, const void* p_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool DragScalarN( const utf8* label, ImGuiDataType data_type, void* p_data, int components, float v_speed, const void* p_min, const void* p_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool SliderFloat( const utf8* label, float* v, float v_min, float v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool SliderFloat2( const utf8* label, float v[2], float v_min, float v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool SliderFloat3( const utf8* label, float v[3], float v_min, float v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool SliderFloat4( const utf8* label, float v[4], float v_min, float v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool SliderAngle( const utf8* label, float* v_rad, float v_degrees_min, float v_degrees_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool SliderInt( const utf8* label, int* v, int v_min, int v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool SliderInt2( const utf8* label, int v[2], int v_min, int v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool SliderInt3( const utf8* label, int v[3], int v_min, int v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool SliderInt4( const utf8* label, int v[4], int v_min, int v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool SliderScalar( const utf8* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool SliderScalarN( const utf8* label, ImGuiDataType data_type, void* p_data, int components, const void* p_min, const void* p_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool VSliderFloat( const utf8* label, const ImVec2& size, float* v, float v_min, float v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool VSliderInt( const utf8* label, const ImVec2& size, int* v, int v_min, int v_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool VSliderScalar( const utf8* label, const ImVec2& size, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const utf8* format, ImGuiSliderFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputText( const utf8* label, utf8* buf, size_t buf_size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputTextMultiline( const utf8* label, utf8* buf, size_t buf_size, const ImVec2& size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputTextWithHint( const utf8* label, const utf8* hint, utf8* buf, size_t buf_size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputFloat( const utf8* label, float* v, float step, float step_fast, const utf8* format, ImGuiInputTextFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputFloat2( const utf8* label, float v[2], const utf8* format, ImGuiInputTextFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputFloat3( const utf8* label, float v[3], const utf8* format, ImGuiInputTextFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputFloat4( const utf8* label, float v[4], const utf8* format, ImGuiInputTextFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputInt( const utf8* label, int* v, int step, int step_fast, ImGuiInputTextFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputInt2( const utf8* label, int v[2], ImGuiInputTextFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputInt3( const utf8* label, int v[3], ImGuiInputTextFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputInt4( const utf8* label, int v[4], ImGuiInputTextFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputDouble( const utf8* label, double* v, double step, double step_fast, const utf8* format, ImGuiInputTextFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputScalar( const utf8* label, ImGuiDataType data_type, void* p_data, const void* p_step, const void* p_step_fast, const utf8* format, ImGuiInputTextFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool InputScalarN( const utf8* label, ImGuiDataType data_type, void* p_data, int components, const void* p_step, const void* p_step_fast, const utf8* format, ImGuiInputTextFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool ColorEdit3( const utf8* label, float col[3], ImGuiColorEditFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool ColorEdit4( const utf8* label, float col[4], ImGuiColorEditFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool ColorPicker3( const utf8* label, float col[3], ImGuiColorEditFlags flags );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool ColorPicker4( const utf8* label, float col[4], ImGuiColorEditFlags flags, const float* ref_col );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool BeginListBox( const utf8* label, const ImVec2& size );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool ListBox( const utf8* label, int* current_item, const utf8* const items[], int items_count, int height_in_items );
    [[deprecated( SW_EDITOR_WIDGET_BAN_MESSAGE )]] IMGUI_API bool ListBox( const utf8* label, int* current_item, const utf8* ( *getter )( void* user_data, int idx ), void* user_data, int items_count, int height_in_items );
} // namespace ImGui

#endif // defined( IMGUI_VERSION ) && !defined( SW_EDITOR_WIDGET_ADAPTER )
