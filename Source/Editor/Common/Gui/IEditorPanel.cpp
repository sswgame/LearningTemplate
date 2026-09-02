#include "pch.h"

#include "Editor/Common/Gui/IEditorPanel.h"

#include "Editor/Common/Gui/EditorChrome.h"

#include <imgui.h>

namespace sw::editor
{
    void IEditorPanel::draw()
    {
        EditorChrome::setNextPanelSize( getInitialPanelSize() );

        if ( EditorChrome::beginPanel( getPanelTitle(), getOpenPtr(), getPanelFlags() ) == false )
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
