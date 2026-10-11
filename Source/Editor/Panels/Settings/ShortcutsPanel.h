/**
 * @file ShortcutsPanel.h
 * @brief 단축키 편집기(Keyboard Shortcuts)입니다. 커맨드마다 조합을 받아 바꾸고, 기본과 다른 것만 `Saved/Editor/Shortcuts.json` 에 저장합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/IEditorPanel.h"

namespace sw::editor
{
    /**
     * @class ShortcutsPanel
     * @brief 언리얼 Keyboard Shortcuts · 유니티 Shortcuts Manager 에 해당합니다. Set 을 누르면 다음 조합을 받고(Esc 취소 · Backspace 지움),
     *        다른 커맨드와 겹치면 두 줄을 빨갛게 하고 바꾸기 · 취소를 묻습니다.
     */
    class ShortcutsPanel final : public IEditorPanel
    {
    public:
        ShortcutsPanel();

        const utf8* getPanelTitle() const override { return "Keyboard Shortcuts"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;
        void        shutdown( IRHIDevice* pRHIDevice ) override;

        /** @brief 지금 묻고 있는 충돌 수입니다(받은 조합과 겹친 커맨드 — 탐침 `Editor.ShortcutConflictCount`). */
        static uint32 getPendingConflictCount();

    private:
        void finishCapture();
        void applyShortcut( const EditorCommandDesc& desc, const EditorCommandShortcut& shortcut );
        void saveAndRebuild();

        fixed_string<constant::kMaxBuffer64> _filter;
        string                               _captureCommandID;  ///< 조합을 받는 중인 커맨드(비면 받지 않는다)
        string                               _conflictCommandID; ///< 받은 조합과 겹친 커맨드(비면 충돌 없음)
        EditorCommandShortcut                _pendingShortcut;   ///< 충돌을 물을 동안 받아 둔 조합
    };
} // namespace sw::editor
