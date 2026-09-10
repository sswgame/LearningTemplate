#include "pch.h"

#include "Editor/Common/Gui/IEditorPanel.h"

#include "Editor/Common/Gui/EditorChrome.h"

#include <imgui.h>

namespace sw::editor
{
    void IEditorPanel::draw()
    {
        EditorChrome::setNextPanelSize( getInitialPanelSize() );

        // 미저장 표시는 파생의 getPanelFlags() 재정의와 무관하게 항상 붙는다.
        EditorPanelFlags panelFlags = getPanelFlags();
        if ( _bDocumentDirty )
            panelFlags |= EditorPanelFlags::UnsavedDocument;

        if ( EditorChrome::beginPanel( getPanelTitle(), getOpenPtr(), panelFlags ) == false )
        {
            _bWindowFocused = false;
            onPanelCollapsed();
            EditorChrome::endPanel();
            return;
        }

        _bWindowFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
        drawContent();
        EditorChrome::endPanel();
    }
} // namespace sw::editor
