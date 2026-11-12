#include "pch.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/IEditorDocumentPanel.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include <imgui.h>

namespace sw::editor
{
    void IEditorPanel::draw()
    {
        _bDrawing = SW_TRUE;
        if ( _bOpenedNotified == SW_FALSE )
        {
            _bOpenedNotified = SW_TRUE;
            onPanelOpened();
        }
        EditorChrome::setNextPanelSize( getInitialPanelSize() );
        IEditorDocumentPanel* const pDocument = asDocumentPanel();
        if ( pDocument != nullptr )
            pDocument->syncDocumentDirty();

        // 미저장 표시는 파생의 getPanelFlags() 재정의와 무관하게 항상 붙는다.
        EditorPanelFlags panelFlags = getPanelFlags();
        if ( hasUnsavedDocument() )
            panelFlags |= EditorPanelFlags::UnsavedDocument;

        const bool bVisible = EditorChrome::beginPanel( getPanelTitle(), getOpenFlagAddress(), panelFlags );
        absorbCloseButton(); // 접힌 창과 숨은 탭의 X 도 같은 확인을 거친다
        if ( bVisible )
        {
            _bWindowFocused     = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
            _bContentDrawn      = SW_TRUE;
            _contentLineCount   = ImGui::GetContentRegionAvail().y / ImGui::GetTextLineHeightWithSpacing();
            _contentColumnCount = ImGui::GetContentRegionAvail().x / ImGui::CalcTextSize( "0" ).x;
            drawContent();
        }
        else
        {
            _bWindowFocused = SW_FALSE;
            _bContentDrawn  = SW_FALSE;
            onPanelCollapsed();
        }
        EditorChrome::endPanel();

        if ( pDocument != nullptr )
            pDocument->flushDocumentInteraction(); // 끌기가 끝난 프레임에 그 구간을 한 단계로 기록한다
        // 창 밖에서 그린다 — 접힌 창은 내용을 그리지 않지만 확인은 떠야 한다.
        if ( pDocument != nullptr && pDocument->isCloseConfirmPending() )
            pDocument->drawCloseConfirm();

        _bDrawing = SW_FALSE;
        if ( _bClosedWhileDrawing )
        {
            _bClosedWhileDrawing = SW_FALSE;
            onPanelClosed(); // 그리기 중에 닫혔다 — 내용이 다 끝난 뒤에 자원을 놓는다
        }
    }

    bool IEditorPanel::hasUnsavedDocument() const
    {
        const IEditorDocumentPanel* const pDocument = asDocumentPanel();
        return pDocument != nullptr && pDocument->isDocumentDirty();
    }

    void IEditorPanel::requestClose()
    {
        if ( _bOpen == false )
            return;
        IEditorDocumentPanel* const pDocument = asDocumentPanel();
        if ( pDocument != nullptr && pDocument->beginCloseConfirmIfNeeded() )
            return;
        closeNow();
    }

    void IEditorPanel::requestAnimationFrame()
    {
        editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>().requestAnimationFrame();
    }

    bool IEditorPanel::areSceneEditsAllowed()
    {
        return EditorPlaySession::areSceneEditsAllowed();
    }
} // namespace sw::editor
