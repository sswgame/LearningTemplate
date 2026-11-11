#include "pch.h"

#include "Editor/Common/GUI/Panel/IEditorDocumentPanel.h"

#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include <imgui.h>

namespace sw::editor
{
    void IEditorDocumentPanel::applyCloseChoice( EditorUnsavedChoice choice )
    {
        if ( choice == EditorUnsavedChoice::None )
            return;
        _bConfirmClose = SW_FALSE;
        if ( EditorSessionPolicy::shouldSaveBeforeAction( choice ) && saveDocumentAndClearDirty() == false )
            return;
        if ( EditorSessionPolicy::shouldClearDirtyWithoutSave( choice ) )
            discardDirtyDocument();
        if ( EditorSessionPolicy::shouldProceedWithAction( choice ) )
            closeNow();
    }

    bool IEditorDocumentPanel::beginCloseConfirmIfNeeded()
    {
        if ( _bDocumentDirty == false || shouldAskBeforeClose() == false )
            return false;
        _bConfirmClose = SW_TRUE;
        return true;
    }

    void IEditorDocumentPanel::drawCloseConfirm()
    {
        constexpr const utf8* kPopupID = "##UnsavedPanelClose";
        ImGui::PushID( this ); // 확인이 여러 패널에 동시에 걸릴 수 있다(최대화 · 레이아웃 불러오기)
        if ( ImGui::IsPopupOpen( kPopupID ) == false )
            EditorInputGUI::openPopup( kPopupID );
        applyCloseChoice( EditorWidgets::drawUnsavedChangesModal( kPopupID, "This document has unsaved changes. Close anyway?" ) );
        ImGui::PopID();
    }
} // namespace sw::editor
