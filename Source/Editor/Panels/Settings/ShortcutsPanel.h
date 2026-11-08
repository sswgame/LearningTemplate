/**
 * @file ShortcutsPanel.h
 * @brief 단축키 편집기(Keyboard Shortcuts)입니다. 에디터 입력 맵(`EditorInputActions`)의 액션마다 조합을 받아 바꾸고, 기본과 다른 것만 `Saved/Editor/editorbindings.xml` 에 저장합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Settings/Model/ShortcutCaptureModel.h"
#include "Editor/Panels/Settings/Model/ShortcutsViewModel.h"

namespace sw::editor
{
    struct EditorCommandDesc;

    class EditorInputActions;

    /**
     * @class ShortcutsPanel
     * @brief 언리얼 Keyboard Shortcuts · 유니티 Shortcuts Manager 에 해당합니다. Set 을 누르면 다음 조합을 받고(Esc 취소),
     *        같은 범위(입력 맵 레이어)의 다른 액션과 겹치면 두 줄을 빨갛게 하고 바꾸기 · 취소를 묻습니다.
     * @details 키 · 수정 키 · 겹침 규칙 · 저장 형식은 엔진 입력 맵의 것이다. 조합 받기의 상태는 `ShortcutCaptureModel` 에 있고 이 창은 눌린 조합을 읽어 넘기고 그리기만 한다.
     *          창을 닫거나 탭이 가려지면 받기를 그만둔다 — 그리지 않는 창은 조합을 받을 수 없고 멈춘 명령 실행을 풀 길도 없다.
     *          단축키가 있는 명령(입력 액션이 있는 명령)만 줄로 나온다 — 액션이 없는 명령은 `editor.input.xml` 에 액션을 더해야 단축키를 가질 수 있다.
     */
    class ShortcutsPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "shortcuts";

        ShortcutsPanel();

        const utf8* getPanelTitle() const override { return "Keyboard Shortcuts"; }
        void        drawContent() override;
        void        shutdown() override;

        /** @brief 지금 묻고 있는 충돌 수입니다(받은 조합과 겹친 액션 — 감시 값 `Editor.ShortcutConflictCount`). */
        static uint32 getPendingConflictCount();

    private:
        void onPanelCollapsed() override;
        void onPanelClosed() override;
        /** @brief 조합을 기다리는 중이면 눌린 조합을 읽어 모델에 넘깁니다. */
        void pollShortcutCapture( IShortcutBindings& bindings );
        /** @brief 충돌을 묻는 줄(바꾸기 · 취소)입니다. */
        void drawConflictPrompt( IShortcutBindings& bindings );
        /** @brief 액션이 있는 명령의 조합 표입니다. */
        void drawShortcutTable( const EditorInputActions& inputActions, IShortcutBindings& bindings );
        /** @brief 테이블의 한 줄을 그립니다. */
        void drawShortcutRow( const EditorInputActions& inputActions, IShortcutBindings& bindings, const EditorCommandDesc& desc, const string& shortcutLabel );

    private:
        fixed_string<constant::buffer::kMax64> _filter;
        ShortcutCaptureModel                   _capture;
        ShortcutsViewModel                     _viewModel; ///< 입력 맵 · 명령 표를 찾고 조합 받기의 바꾸기를 입력 맵으로 잇는다(MVVM 계약)
    };
} // namespace sw::editor
