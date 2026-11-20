#include "pch.h"

#include "Editor/Common/Widgets/EditorNodeGraphID.h"

#include <imgui-node-editor/imgui_node_editor.h>

namespace sw::editor
{
    ax::NodeEditor::NodeId EditorNodeGraphID::toNodeID( int32 id )
    {
        return ax::NodeEditor::NodeId( static_cast<uintptr_t>( id ) );
    }

    ax::NodeEditor::PinId EditorNodeGraphID::toPinID( int32 id )
    {
        return ax::NodeEditor::PinId( static_cast<uintptr_t>( id ) );
    }

    ax::NodeEditor::LinkId EditorNodeGraphID::toLinkID( int32 id )
    {
        return ax::NodeEditor::LinkId( static_cast<uintptr_t>( id ) );
    }
} // namespace sw::editor
