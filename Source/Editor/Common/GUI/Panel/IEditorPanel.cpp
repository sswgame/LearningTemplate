#include "pch.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include <imgui.h>

namespace sw::editor
{
    void IEditorPanel::draw()
    {
        EditorChrome::setNextPanelSize( getInitialPanelSize() );
        syncDocumentDirty();

        // 미저장 표시는 파생의 getPanelFlags() 재정의와 무관하게 항상 붙는다.
        EditorPanelFlags panelFlags = getPanelFlags();
        if ( _bDocumentDirty )
            panelFlags |= EditorPanelFlags::UnsavedDocument;

        const bool bVisible = EditorChrome::beginPanel( getPanelTitle(), getOpenFlagAddress(), panelFlags );
        absorbCloseButton(); // 접힌 창과 숨은 탭의 X 도 같은 확인을 거친다
        if ( bVisible )
        {
            _bWindowFocused     = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
            _bContentDrawn      = true;
            _contentLineCount   = ImGui::GetContentRegionAvail().y / ImGui::GetTextLineHeightWithSpacing();
            _contentColumnCount = ImGui::GetContentRegionAvail().x / ImGui::CalcTextSize( "0" ).x;
            drawContent();
        }
        else
        {
            _bWindowFocused = false;
            _bContentDrawn  = false;
            onPanelCollapsed();
        }
        EditorChrome::endPanel();

        // 창 밖에서 그린다 — 접힌 창은 내용을 그리지 않지만 확인은 떠야 한다.
        if ( _bConfirmClose )
            drawCloseConfirm();
    }

    void IEditorPanel::drawCloseConfirm()
    {
        constexpr const utf8* kPopupID = "##UnsavedPanelClose";
        ImGui::PushID( this ); // 확인이 여러 패널에 동시에 걸릴 수 있다(최대화 · 레이아웃 불러오기)
        if ( ImGui::IsPopupOpen( kPopupID ) == false )
            EditorInputGUI::openPopup( kPopupID );
        applyCloseChoice( EditorWidgets::drawUnsavedChangesModal( kPopupID, "This document has unsaved changes. Close anyway?" ) );
        ImGui::PopID();
    }
} // namespace sw::editor
