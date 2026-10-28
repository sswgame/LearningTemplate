/**
 * @file ShortcutsPanel.h
 * @brief 단축키 편집기(Keyboard Shortcuts)입니다. 에디터 입력 맵(`EditorInputActions`)의 액션마다 조합을 받아 바꾸고, 기본과 다른 것만 `Saved/Editor/editorbindings.xml` 에 저장합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"

#include "Engine/Input/KeyCodeUtil.h"

namespace sw::editor
{
    struct EditorCommandDesc;

    class EditorInputActions;

    /**
     * @class ShortcutsPanel
     * @brief 언리얼 Keyboard Shortcuts · 유니티 Shortcuts Manager 에 해당합니다. Set 을 누르면 다음 조합을 받고(Esc 취소),
     *        같은 범위(입력 맵 레이어)의 다른 액션과 겹치면 두 줄을 빨갛게 하고 바꾸기 · 취소를 묻습니다.
     * @details 키 · 수정 키 · 겹침 규칙 · 저장 형식은 엔진 입력 맵의 것이다. 이 창은 받은 조합을 `EditorInputActions` 에 넘기기만 한다.
     *          단축키가 있는 명령(입력 액션이 있는 명령)만 줄로 나온다 — 액션이 없는 명령은 `editor.input.xml` 에 액션을 더해야 단축키를 가질 수 있다.
     */
    class ShortcutsPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "shortcuts";

        ShortcutsPanel();

        const utf8* getPanelTitle() const override { return "Keyboard Shortcuts"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;
        void        shutdown( IRHIDevice* pRHIDevice ) override;

        /** @brief 지금 묻고 있는 충돌 수입니다(받은 조합과 겹친 액션 — 감시 값 `Editor.ShortcutConflictCount`). */
        static uint32 getPendingConflictCount();

    private:
        /** @brief 조합을 받는 중이면 눌린 조합을 읽어 적용하거나, 겹치면 충돌을 묻는 상태로 넘깁니다. */
        void pollShortcutCapture( EditorInputActions& inputActions );
        /** @brief 충돌을 묻는 줄(바꾸기 · 취소)입니다. */
        void drawConflictPrompt( EditorInputActions& inputActions );
        /** @brief 액션이 있는 명령의 조합 표입니다. */
        void drawShortcutTable( EditorInputActions& inputActions );
        /** @brief 테이블의 한 줄을 그립니다. */
        void drawShortcutRow( EditorInputActions& inputActions, const EditorCommandDesc& desc, const string& shortcutLabel );
        void finishCapture();

        fixed_string<constant::kMaxBuffer64> _filter;
        string                               _captureAction;   ///< 조합을 받는 중인 액션(비면 받지 않는다)
        string                               _conflictAction;  ///< 받은 조합과 겹친 액션(비면 충돌 없음)
        Key                                  _pendingKey;      ///< 충돌을 물을 동안 받아 둔 키
        uint8                                _pendingModifier; ///< 충돌을 물을 동안 받아 둔 수정 키(`ModifierKey`)
    };
} // namespace sw::editor
