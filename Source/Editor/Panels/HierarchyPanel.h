/**
 * @file HierarchyPanel.h
 * @brief 씬 GameObject / Component 계층 윈도우
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw::editor
{
    /** @brief 활성 씬의 오브젝트 아웃라이너 */
    class HierarchyPanel : public IEditorPanel
    {
    public:
        HierarchyPanel();
        ~HierarchyPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 윈도우 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Hierarchy"; }
        /** @brief Hierarchy UI를 그립니다. */
        void drawContent() override;

        /** @brief 복제·이름 변경·삭제 단축키를 처리합니다. */
        void handleHierarchyShortcuts( GameObjectManager* pManager );

    private:
        uint64                                _renamingObjectId;
        fixed_string<constant::kMaxBuffer128> _filterBuffer;
        fixed_string<constant::kMaxBuffer256> _renameBuffer;
        bool                                  _bFocusRenameInput;
    };
} // namespace sw::editor
