#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorGraphDocumentPanel.h"

#include "Editor/Common/Widgets/EditorNodeGraphID.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    EditorGraphDocumentPanelBase::EditorGraphDocumentPanelBase( const EditorAssetType& kind, const utf8* pNodeMoveEditLabel, const utf8* pNodeMoveCoalesceKey,
                                                                NodeEditorBindFunction bindModuleCopy )
        : EditorDocumentPanel{ kind, true }
        , _nodeGraph{}
        , _bindModuleCopy{ bindModuleCopy }
        , _pNodeMoveEditLabel{ pNodeMoveEditLabel }
        , _pNodeMoveCoalesceKey{ pNodeMoveCoalesceKey }
        , _pendingPlacementNodeID{ 0 }
        , _pendingPlacement{}
        , _bGraphLayoutReady{ SW_FALSE }
        , _reservedGraph{ 0 }
    {
        SW_ASSERT( _bindModuleCopy != nullptr );
    }

    bool EditorGraphDocumentPanelBase::beginGraphCanvas( const utf8* pCanvasID, const utf8* pSettingsFileName, const utf8* pMarkScope )
    {
        if ( _nodeGraph.beginCanvas( pCanvasID, pSettingsFileName, pMarkScope ) == false )
            return false;
        // `beginCanvas` 는 EditorModule 의 사본에 편집기를 걸었다. 패널 모듈의 사본에도 같은 편집기를 건다.
        _bindModuleCopy( _nodeGraph.getContext() );
        return true;
    }

    void EditorGraphDocumentPanelBase::finishGraphCanvas( uint32 drawnNodeCount )
    {
        EditorNodeGraph::noteDrawnNodeCount( drawnNodeCount );
        _nodeGraph.endCanvas();
        _bindModuleCopy( nullptr );
    }

    void EditorGraphDocumentPanelBase::applyNodePlacement( int32 nodeID )
    {
        if ( _pendingPlacementNodeID == 0 || _pendingPlacementNodeID != nodeID )
            return;
        ed::SetNodePosition( toNodeID( nodeID ), ImVec2( _pendingPlacement._x, _pendingPlacement._y ) );
        _pendingPlacementNodeID = 0;
    }

    float2 EditorGraphDocumentPanelBase::getCanvasNodePosition( int32 nodeID ) const
    {
        const ImVec2 position = ed::GetNodePosition( toNodeID( nodeID ) );
        return float2{ position.x, position.y };
    }

    bool EditorGraphDocumentPanelBase::beginCanvasDeletion()
    {
        return ed::BeginDelete();
    }

    bool EditorGraphDocumentPanelBase::takeDeletedLink( int32& outLinkID )
    {
        ed::LinkId linkID;
        while ( ed::QueryDeletedLink( &linkID ) )
        {
            if ( ed::AcceptDeletedItem() == false )
                continue;
            outLinkID = static_cast<int32>( linkID.Get() );
            return true;
        }
        return false;
    }

    bool EditorGraphDocumentPanelBase::takeDeletedNode( int32& outNodeID )
    {
        ed::NodeId nodeID;
        while ( ed::QueryDeletedNode( &nodeID ) )
        {
            if ( ed::AcceptDeletedItem() == false )
                continue;
            outNodeID = static_cast<int32>( nodeID.Get() );
            return true;
        }
        return false;
    }

    void EditorGraphDocumentPanelBase::endCanvasDeletion()
    {
        ed::EndDelete();
    }
} // namespace sw::editor
