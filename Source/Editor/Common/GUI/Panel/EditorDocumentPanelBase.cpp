#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorDocumentPanelBase.h"

#include "Core/Container/vector.h"

#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include <imgui.h>

namespace sw::editor
{
    bool EditorDocumentPanelBase::saveDocumentAndClearDirty()
    {
        prepareDocumentForSave();
        if ( canSaveDocument() == false )
            return false;
        // 저장이 쓸 파일을 패널이 내놓고 여기서 하나씩 본다 — 문서 하나가 여러 파일을 쓰거나(데이터 테이블) 연 경로가 없는 문서(프로젝트 설정)도 같은 확인을 지난다.
        vector<DiskPath> listPath;
        collectFilesToSave( listPath );
        AssetWriter& writer = getAssetWriter();
        if ( writer.validateSave( listPath ) == false || saveDocument( writer ) == false )
            return false;
        for ( const DiskPath& path : listPath )
            writer.capture( path );
        clearDocumentDirty();
        onDocumentSaved();
        return true;
    }

    void EditorDocumentPanelBase::captureDiskBaseline()
    {
        vector<DiskPath> listPath;
        collectFilesToSave( listPath );
        for ( const DiskPath& path : listPath )
            getAssetWriter().capture( path );
    }

    void EditorDocumentPanelBase::applyCloseChoice( EditorUnsavedChoice choice )
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

    bool EditorDocumentPanelBase::beginCloseConfirmIfNeeded()
    {
        if ( _bDocumentDirty == false || shouldAskBeforeClose() == false )
            return false;
        _bConfirmClose = SW_TRUE;
        return true;
    }

    void EditorDocumentPanelBase::drawCloseConfirm()
    {
        constexpr const utf8* kPopupID = "##UnsavedPanelClose";
        ImGui::PushID( this ); // 확인이 여러 패널에 동시에 걸릴 수 있다(최대화 · 레이아웃 불러오기)
        if ( ImGui::IsPopupOpen( kPopupID ) == false )
            EditorInputGUI::openPopup( kPopupID );
        applyCloseChoice( EditorWidgets::drawUnsavedChangesModal( kPopupID, "This document has unsaved changes. Close anyway?" ) );
        ImGui::PopID();
    }
} // namespace sw::editor
