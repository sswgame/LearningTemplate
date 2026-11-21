#include "pch.h"

#include "Editor/Common/GUI/EditorClipboard.h"

#include <imgui.h>

namespace sw::editor
{
    void EditorClipboard::setText( string_view text )
    {
        if ( ImGui::GetCurrentContext() == nullptr )
            return;
        const string terminated{ text };
        ImGui::SetClipboardText( terminated.c_str() );
    }
} // namespace sw::editor
