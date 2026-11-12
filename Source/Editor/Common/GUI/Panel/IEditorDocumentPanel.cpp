#include "pch.h"

#include "Editor/Common/GUI/Panel/IEditorDocumentPanel.h"

#include "Core/Path/AbsolutePath.h"

#include "Editor/Common/Commands/Asset/EditorContentWrite.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Content/AssetPaths.h"

#include <imgui.h>

namespace sw::editor
{
    bool IEditorDocumentPanel::saveDocumentAndClearDirty()
    {
        if ( canSaveDocument() == false )
            return false;
        const string_view assetID = getDocumentAssetPath();
        if ( assetID.empty() == false )
        {
            // 쓰기와 같은 해석으로 푼 자리를 본다(R7). 풀지 못하면 저장 자신이 이유를 남기고 진다.
            const optional<AbsolutePath> diskPath = editor::getSubsystem<AssetPaths>().toDiskPath( assetID );
            if ( diskPath.has_value() && EditorContentWrite::refuseReadOnly( diskPath->str(), EditorContentWriteKind::Save ) )
                return false;
        }
        if ( saveDocument() == false )
            return false;
        clearDocumentDirty();
        onDocumentSaved();
        return true;
    }

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
